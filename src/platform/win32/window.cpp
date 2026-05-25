#include <winelement/platform/window.hpp>

#include "dispatcher_internal.hpp"
#include "window_render_worker.hpp"

#include <winelement/core/frame_scheduler.hpp>
#include <winelement/elements/event_router.hpp>
#include <winelement/elements/text_clipboard_service.hpp>
#include <winelement/rendering/compositor.hpp>
#include <winelement/rendering/render_command_list.hpp>
#include <winelement/rendering/render_frame_graph.hpp>
#include <winelement/rendering/render_scene.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <imm.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace winelement::platform {

namespace detail {

class UIElementThreadAccessScope final {
  public:
    UIElementThreadAccessScope(elements::UIElement& root, std::thread::id owner_thread_id) noexcept
        : root_(&root), previous_owner_thread_id_(root.owner_thread_id_) {
        root_->adopt_thread_access(owner_thread_id);
    }

    ~UIElementThreadAccessScope() noexcept {
        if (root_ != nullptr) {
            root_->adopt_thread_access(previous_owner_thread_id_);
        }
    }

    UIElementThreadAccessScope(const UIElementThreadAccessScope&) = delete;
    UIElementThreadAccessScope& operator=(const UIElementThreadAccessScope&) = delete;

  private:
    elements::UIElement* root_ = nullptr;
    std::thread::id previous_owner_thread_id_;
};

} // namespace detail

namespace {

constexpr auto class_name = L"WinElementWindow";
constexpr auto default_dpi = 96.0F;
constexpr UINT managed_close_message = WM_APP + 0x513U;
constexpr UINT animation_frame_message = WM_APP + 0x514U;
constexpr UINT_PTR caret_blink_timer_id = 0x574DU;
constexpr UINT_PTR animation_frame_timer_id = 0x574EU;
constexpr UINT caret_blink_timer_interval_ms = 500U;
constexpr UINT animation_timer_resolution_ms = 1U;
// DirectCompositionBridge currently binds the swap chain only; promotion candidates are advisory
// until promoted backing surfaces are implemented. Avoid the per-frame full-scene DFS in windows.
constexpr auto window_max_compositor_promotions = 0U;
constexpr auto window_minimum_compositor_promotion_area = 8192.0F;

[[nodiscard]] rendering::CompositorPromotionOptions window_compositor_promotion_options() noexcept {
    return rendering::CompositorPromotionOptions{.max_candidates = window_max_compositor_promotions,
                                                 .minimum_area =
                                                     window_minimum_compositor_promotion_area,
                                                 .include_stable_layers = false};
}
constexpr auto animation_frame_interval = std::chrono::nanoseconds{16'666'667};
constexpr auto interactive_resize_frame_interval = animation_frame_interval;
constexpr UINT native_text_command_cut_id = 0x7310U;
constexpr UINT native_text_command_copy_id = 0x7311U;
constexpr UINT native_text_command_paste_id = 0x7312U;
constexpr UINT native_text_command_select_all_id = 0x7313U;

struct ClientPixelSize {
    std::uint32_t width = 1U;
    std::uint32_t height = 1U;
};

struct WindowImeState {
    bool composition_active = false;
    bool candidate_window_open = false;
    bool text_input_active = false;

    void start_composition() noexcept {
        composition_active = true;
    }

    void finish_composition() noexcept {
        composition_active = false;
    }

    void commit_result() noexcept {
        composition_active = false;
        candidate_window_open = false;
        text_input_active = false;
    }

    void open_candidate() noexcept {
        candidate_window_open = true;
    }

    void close_candidate() noexcept {
        candidate_window_open = false;
    }

    [[nodiscard]] bool can_end_text_input() const noexcept {
        return text_input_active && !composition_active && !candidate_window_open;
    }
};

struct UiFrameRequest {
    rendering::DirtyRegion dirty_region{};
    layout::LayoutConstraints layout_constraints{};
    std::uint32_t target_pixel_width = 1U;
    std::uint32_t target_pixel_height = 1U;
    bool tick_animations = true;
};

[[nodiscard]] std::runtime_error make_win32_error(std::string_view message) {
    return std::runtime_error(std::string(message));
}

[[nodiscard]] bool
has_text_input_menu_commands(elements::TextInputEditCommandState state) noexcept {
    return state.can_cut || state.can_copy || state.can_paste || state.can_select_all;
}

[[nodiscard]] float query_window_dpi(HWND hwnd) noexcept {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    auto* user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return default_dpi;
    }

    auto* procedure =
        reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
    if (procedure == nullptr) {
        return default_dpi;
    }

    const auto dpi = procedure(hwnd);
    return dpi == 0 ? default_dpi : static_cast<float>(dpi);
}

void enable_process_dpi_awareness() noexcept {
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto* user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }

    auto* procedure = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (procedure != nullptr) {
        procedure(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

[[nodiscard]] layout::Size client_dip_size(HWND hwnd, float dpi) noexcept {
    RECT rect{};
    if (hwnd == nullptr || GetClientRect(hwnd, &rect) == FALSE) {
        return layout::Size{1.0F, 1.0F};
    }
    const auto scale = default_dpi / std::max(dpi, 1.0F);
    const auto width = static_cast<float>(std::max<LONG>(rect.right - rect.left, 0)) * scale;
    const auto height = static_cast<float>(std::max<LONG>(rect.bottom - rect.top, 0)) * scale;
    return layout::Size{width, height};
}

[[nodiscard]] ClientPixelSize client_pixel_size(HWND hwnd) noexcept {
    RECT rect{};
    if (hwnd == nullptr || GetClientRect(hwnd, &rect) == FALSE) {
        return ClientPixelSize{.width = 1U, .height = 1U};
    }
    return ClientPixelSize{
        .width = static_cast<std::uint32_t>(std::max<LONG>(rect.right - rect.left, 1)),
        .height = static_cast<std::uint32_t>(std::max<LONG>(rect.bottom - rect.top, 1))};
}

[[nodiscard]] layout::Point point_from_lparam(LPARAM lparam, float dpi) noexcept {
    const auto scale = default_dpi / std::max(dpi, 1.0F);
    return layout::Point{static_cast<float>(GET_X_LPARAM(lparam)) * scale,
                         static_cast<float>(GET_Y_LPARAM(lparam)) * scale};
}

[[nodiscard]] elements::KeyModifiers current_modifiers() noexcept {
    return elements::KeyModifiers{.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                                  .control = (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                                  .alt = (GetKeyState(VK_MENU) & 0x8000) != 0,
                                  .meta = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                                          (GetKeyState(VK_RWIN) & 0x8000) != 0};
}

[[nodiscard]] elements::Key key_from_virtual_key(WPARAM virtual_key) noexcept {
    switch (virtual_key) {
    case VK_TAB:
        return elements::Key::Tab;
    case VK_RETURN:
        return elements::Key::Enter;
    case VK_SPACE:
        return elements::Key::Space;
    case VK_ESCAPE:
        return elements::Key::Escape;
    case VK_BACK:
        return elements::Key::Backspace;
    case VK_DELETE:
        return elements::Key::Delete;
    case VK_UP:
        return elements::Key::Up;
    case VK_DOWN:
        return elements::Key::Down;
    case VK_LEFT:
        return elements::Key::Left;
    case VK_RIGHT:
        return elements::Key::Right;
    case VK_HOME:
        return elements::Key::Home;
    case VK_END:
        return elements::Key::End;
    case VK_PRIOR:
        return elements::Key::PageUp;
    case VK_NEXT:
        return elements::Key::PageDown;
    case 'A':
        return elements::Key::A;
    case 'C':
        return elements::Key::C;
    case 'V':
        return elements::Key::V;
    case 'X':
        return elements::Key::X;
    case 'Z':
        return elements::Key::Z;
    default:
        return elements::Key::Unknown;
    }
}

[[nodiscard]] std::string wide_text_to_utf8(std::wstring_view value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }

    const auto wide_count = static_cast<int>(value.size());
    const auto byte_count =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), wide_count, nullptr, 0, nullptr, nullptr);
    if (byte_count <= 0) {
        return {};
    }

    std::string text(static_cast<std::size_t>(byte_count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), wide_count, text.data(), byte_count, nullptr,
                        nullptr);
    return text;
}

[[nodiscard]] std::wstring utf8_text_to_wide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }

    const auto byte_count = static_cast<int>(value.size());
    const auto char_count = MultiByteToWideChar(CP_UTF8, 0, value.data(), byte_count, nullptr, 0);
    if (char_count <= 0) {
        return {};
    }

    std::wstring text(static_cast<std::size_t>(char_count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), byte_count, text.data(), char_count);
    return text;
}

[[nodiscard]] bool write_system_text_clipboard(std::string_view text) noexcept {
    auto wide_text = utf8_text_to_wide(text);
    wide_text.push_back(L'\0');

    if (OpenClipboard(nullptr) == FALSE) {
        return false;
    }

    struct ClipboardClose final {
        ~ClipboardClose() {
            CloseClipboard();
        }
    } clipboard_close;

    if (EmptyClipboard() == FALSE) {
        return false;
    }

    const auto byte_count = wide_text.size() * sizeof(wchar_t);
    auto* global_memory = GlobalAlloc(GMEM_MOVEABLE, byte_count);
    if (global_memory == nullptr) {
        return false;
    }

    auto* locked_memory = GlobalLock(global_memory);
    if (locked_memory == nullptr) {
        GlobalFree(global_memory);
        return false;
    }

    std::memcpy(locked_memory, wide_text.data(), byte_count);
    GlobalUnlock(global_memory);

    if (SetClipboardData(CF_UNICODETEXT, global_memory) == nullptr) {
        GlobalFree(global_memory);
        return false;
    }

    return true;
}

[[nodiscard]] std::optional<std::string> read_system_text_clipboard() noexcept {
    if (IsClipboardFormatAvailable(CF_UNICODETEXT) == FALSE) {
        return std::nullopt;
    }

    if (OpenClipboard(nullptr) == FALSE) {
        return std::nullopt;
    }

    struct ClipboardClose final {
        ~ClipboardClose() {
            CloseClipboard();
        }
    } clipboard_close;

    auto* global_memory = GetClipboardData(CF_UNICODETEXT);
    if (global_memory == nullptr) {
        return std::nullopt;
    }

    const auto* locked_text = static_cast<const wchar_t*>(GlobalLock(global_memory));
    if (locked_text == nullptr) {
        return std::nullopt;
    }

    const auto length = std::wcslen(locked_text);
    auto text = wide_text_to_utf8(std::wstring_view{locked_text, length});
    GlobalUnlock(global_memory);
    return text;
}

class WindowRenderCache final {
  public:
    void reset() {
        snapshot_.reset();
        prepared_cache_ = std::make_shared<rendering::PreparedRenderCache>();
        promotion_plan_ = {};
        frame_graph_ = {};
        valid_ = false;
    }

    void invalidate() noexcept {
        valid_ = false;
    }

    void commit_if_needed(elements::UIElement& content, rendering::DirtyRegion& dirty_region) {
        if (valid_ && !content.needs_paint()) {
            return;
        }

        rendering::DirtyRegion ui_dirty_region;
        auto next_scene = rendering::RenderScene{prepared_cache_};
        content.commit_render_scene(next_scene, &ui_dirty_region);
        promotion_plan_ = rendering::build_compositor_promotion_plan(
            next_scene, window_compositor_promotion_options());
        frame_graph_ = rendering::build_render_frame_graph(next_scene);

        const auto had_valid_snapshot = valid_ && snapshot_ != nullptr;
        const auto previous_bounds = had_valid_snapshot ? snapshot_->bounds() : layout::Rect{};
        const auto next_bounds = next_scene.bounds();
        dirty_region.add(ui_dirty_region);
        if (!had_valid_snapshot) {
            dirty_region.add(content.absolute_frame());
        } else if (previous_bounds != next_bounds) {
            dirty_region.add(previous_bounds);
            dirty_region.add(next_bounds);
        }

        snapshot_ = std::make_shared<rendering::RenderScene>(std::move(next_scene));
        valid_ = true;
        content.clear_paint_dirty_subtree();
    }

    [[nodiscard]] std::shared_ptr<const rendering::RenderScene> snapshot() {
        if (snapshot_ == nullptr) {
            snapshot_ = std::make_shared<rendering::RenderScene>();
        }
        return snapshot_;
    }

    [[nodiscard]] const rendering::CompositorPromotionPlan& promotion_plan() const noexcept {
        return promotion_plan_;
    }

    [[nodiscard]] const rendering::RenderFrameGraph& frame_graph() const noexcept {
        return frame_graph_;
    }

  private:
    std::shared_ptr<const rendering::RenderScene> snapshot_;
    std::shared_ptr<rendering::PreparedRenderCache> prepared_cache_ =
        std::make_shared<rendering::PreparedRenderCache>();
    rendering::CompositorPromotionPlan promotion_plan_;
    rendering::RenderFrameGraph frame_graph_;
    bool valid_ = false;
};

[[nodiscard]] std::string wide_char_to_utf8(wchar_t value) {
    return wide_text_to_utf8(std::wstring_view{&value, 1U});
}

[[nodiscard]] LPCWSTR native_cursor_id(elements::PointerCursor cursor) noexcept {
    switch (cursor) {
    case elements::PointerCursor::IBeam:
        return IDC_IBEAM;
    case elements::PointerCursor::Move:
        return IDC_SIZEALL;
    case elements::PointerCursor::Hand:
        return IDC_HAND;
    case elements::PointerCursor::NotAllowed:
        return IDC_NO;
    case elements::PointerCursor::Arrow:
    case elements::PointerCursor::Default:
    default:
        return IDC_ARROW;
    }
}

[[nodiscard]] layout::Rect client_pixel_rect_to_dip(RECT rect, float dpi) noexcept {
    const auto scale = default_dpi / std::max(dpi, 1.0F);
    const auto left = static_cast<float>(rect.left) * scale;
    const auto top = static_cast<float>(rect.top) * scale;
    const auto right = static_cast<float>(rect.right) * scale;
    const auto bottom = static_cast<float>(rect.bottom) * scale;
    return layout::Rect{left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top)};
}

[[nodiscard]] RECT dip_rect_to_client_pixel_rect(layout::Rect rect, float dpi) noexcept {
    const auto scale = std::max(dpi, 1.0F) / default_dpi;
    const auto left = static_cast<LONG>(std::floor(rect.x * scale));
    const auto top = static_cast<LONG>(std::floor(rect.y * scale));
    const auto right = static_cast<LONG>(std::ceil((rect.x + rect.width) * scale));
    const auto bottom = static_cast<LONG>(std::ceil((rect.y + rect.height) * scale));
    return RECT{left, top, right, bottom};
}

[[nodiscard]] std::string ime_composition_text(HWND hwnd, DWORD index) {
    auto* context = ImmGetContext(hwnd);
    if (context == nullptr) {
        return {};
    }

    const auto byte_count = ImmGetCompositionStringW(context, index, nullptr, 0);
    if (byte_count <= 0) {
        ImmReleaseContext(hwnd, context);
        return {};
    }

    std::vector<wchar_t> buffer(static_cast<std::size_t>(byte_count) / sizeof(wchar_t));
    ImmGetCompositionStringW(context, index, buffer.data(), byte_count);
    ImmReleaseContext(hwnd, context);
    return wide_text_to_utf8(std::wstring_view{buffer.data(), buffer.size()});
}

struct ModalOwnerState {
    std::size_t depth = 0U;
    bool restore_enabled = false;
};

std::mutex modal_owner_mutex;
std::unordered_map<HWND, ModalOwnerState> modal_owner_states;
std::mutex window_class_mutex;
std::uint32_t window_class_ref_count = 0U;
bool window_class_owned_by_module = false;

void acquire_modal_owner(HWND owner_hwnd) noexcept {
    if (owner_hwnd == nullptr || IsWindow(owner_hwnd) == FALSE) {
        return;
    }

    const std::lock_guard lock(modal_owner_mutex);
    auto& state = modal_owner_states[owner_hwnd];
    if (state.depth == 0U) {
        state.restore_enabled = IsWindowEnabled(owner_hwnd) != FALSE;
        if (state.restore_enabled) {
            EnableWindow(owner_hwnd, FALSE);
        }
    }
    ++state.depth;
}

void release_modal_owner(HWND owner_hwnd) noexcept {
    if (owner_hwnd == nullptr) {
        return;
    }

    const std::lock_guard lock(modal_owner_mutex);
    const auto it = modal_owner_states.find(owner_hwnd);
    if (it == modal_owner_states.end()) {
        return;
    }

    if (it->second.depth > 0U) {
        --it->second.depth;
    }
    if (it->second.depth == 0U) {
        const auto restore_enabled = it->second.restore_enabled;
        modal_owner_states.erase(it);
        if (restore_enabled && IsWindow(owner_hwnd) != FALSE) {
            EnableWindow(owner_hwnd, TRUE);
            SetActiveWindow(owner_hwnd);
        }
    }
}

void retain_window_class(WNDPROC window_proc) {
    const std::scoped_lock lock(window_class_mutex);
    if (window_class_ref_count++ > 0U) {
        return;
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_DBLCLKS;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = class_name;

    if (RegisterClassExW(&window_class) == 0) {
        const auto error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            --window_class_ref_count;
            throw make_win32_error("failed to register WinElement window class");
        }
        window_class_owned_by_module = false;
        return;
    }

    window_class_owned_by_module = true;
}

void release_window_class() noexcept {
    const std::scoped_lock lock(window_class_mutex);
    if (window_class_ref_count == 0U || --window_class_ref_count != 0U) {
        return;
    }
    if (window_class_owned_by_module) {
        UnregisterClassW(class_name, GetModuleHandleW(nullptr));
    }
    window_class_owned_by_module = false;
}

} // namespace

class Window::Impl final {
  public:
    explicit Impl(WindowOptions options)
        : options_(std::move(options)), dispatcher_(detail::ensure_current_dispatcher_state()) {
        try {
            enable_process_dpi_awareness();
            install_option_hooks();
            register_class();
            create_window();
            update_dpi();
            text_clipboard_service_->set_system_callbacks(&read_system_text_clipboard,
                                                          &write_system_text_clipboard);
            start_ui_frame_worker();

            if (!options_.defer_render_thread_until_show) {
                start_render_worker();
            }
        } catch (...) {
            destroy_native_window();
            if (registered_window_class_) {
                release_window_class();
                registered_window_class_ = false;
            }
            throw;
        }
    }

    ~Impl() {
        destroy_native_window();
        if (winmm_module_ != nullptr) {
            FreeLibrary(winmm_module_);
            winmm_module_ = nullptr;
            time_begin_period_ = nullptr;
            time_end_period_ = nullptr;
        }
        if (registered_window_class_) {
            release_window_class();
            registered_window_class_ = false;
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void install_option_hooks() {
        if (options_.on_message) {
            message_filters_.add(std::move(options_.on_message));
        }
        if (options_.on_post_message) {
            post_message_filters_.add(std::move(options_.on_post_message));
        }
        if (options_.on_closed) {
            closed_event_.add(std::move(options_.on_closed));
        }
    }

    [[nodiscard]] layout::LayoutEngine& layout_engine() noexcept {
        return layout_engine_;
    }

    [[nodiscard]] const layout::LayoutEngine& layout_engine() const noexcept {
        return layout_engine_;
    }

    void set_content(std::unique_ptr<elements::UIElement> content) {
        {
            const std::scoped_lock lock(ui_tree_mutex_);
            ++text_input_menu_target_generation_;
            if (content != nullptr) {
                content->set_text_clipboard_service(text_clipboard_service_);
                content->bind_layout_tree(layout_engine_);
            }
            content_ = std::move(content);
            render_cache_.reset();
            router_ =
                content_ == nullptr ? nullptr : std::make_unique<elements::EventRouter>(*content_);
            if (content_ != nullptr) {
                content_->invalidate_layout();
                content_->invalidate_paint();
            }
        }
        request_repaint();
    }

    [[nodiscard]] elements::UIElement* content() noexcept {
        return content_.get();
    }

    [[nodiscard]] const elements::UIElement* content() const noexcept {
        return content_.get();
    }

    [[nodiscard]] NativeWindowHandle native_handle() const noexcept {
        return hwnd_;
    }

    void set_title(std::wstring_view title) {
        options_.title.assign(title);
        if (hwnd_ != nullptr) {
            SetWindowTextW(hwnd_, options_.title.c_str());
        }
    }

    Window::MessageFilterToken add_window_message_filter(WindowMessageHook filter) {
        return message_filters_.add(std::move(filter));
    }

    void remove_window_message_filter(Window::MessageFilterToken token) noexcept {
        message_filters_.remove(token);
    }

    Window::MessageFilterToken add_post_window_message_filter(WindowMessageHook filter) {
        return post_message_filters_.add(std::move(filter));
    }

    void remove_post_window_message_filter(Window::MessageFilterToken token) noexcept {
        post_message_filters_.remove(token);
    }

    core::EventHandler<WindowMessage&>& window_message_observers() noexcept {
        return message_observers_;
    }

    core::EventHandler<WindowMessage&>& post_window_message_observers() noexcept {
        return post_message_observers_;
    }

    core::EventHandler<>& closed_event() noexcept {
        return closed_event_;
    }

    void show() {
        if (hwnd_ != nullptr) {
            ensure_render_worker();
            if (options_.modal && !modal_owner_acquired_) {
                acquire_modal_owner(owner_hwnd_);
                modal_owner_acquired_ = owner_hwnd_ != nullptr;
            }
            ShowWindow(hwnd_, SW_SHOWNORMAL);
            UpdateWindow(hwnd_);
        }
    }

    int show_modal() {
        show();
        MSG message{};
        while (hwnd_ != nullptr && IsWindow(hwnd_) != FALSE) {
            if (dispatcher_->quit_requested()) {
                close();
                return dispatcher_->exit_code();
            }
            const auto result = GetMessageW(&message, nullptr, 0, 0);
            if (result <= 0) {
                return static_cast<int>(message.wParam);
            }
            if (dispatcher_->process_thread_message(message.hwnd, message.message)) {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return 0;
    }

    void close() noexcept {
        destroy_native_window();
    }

    [[nodiscard]] bool is_open() const noexcept {
        return hwnd_ != nullptr;
    }

    void upload_resource(rendering::RenderResourceUpload upload) noexcept {
        ensure_render_worker();
        if (render_worker_ == nullptr) {
            return;
        }

        render_worker_->upload_resource(std::move(upload));
        request_repaint();
    }

    void request_repaint() noexcept {
        request_repaint(false);
    }

    void request_repaint(bool immediate,
                         std::optional<layout::Rect> immediate_bounds = std::nullopt) noexcept {
        if (hwnd_ == nullptr || repaint_requested_.exchange(true, std::memory_order_acq_rel)) {
            if (immediate && hwnd_ != nullptr && IsWindow(hwnd_) != FALSE) {
                low_latency_repaint_pending_.store(true, std::memory_order_release);
                const auto repaint_rect = immediate_repaint_rect(immediate_bounds);
                RedrawWindow(hwnd_, repaint_rect ? &*repaint_rect : nullptr, nullptr,
                             RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
            }
            return;
        }
        const auto hwnd = hwnd_;
        if (immediate) {
            low_latency_repaint_pending_.store(true, std::memory_order_release);
            if (IsWindow(hwnd) != FALSE) {
                const auto repaint_rect = immediate_repaint_rect(immediate_bounds);
                RedrawWindow(hwnd, repaint_rect ? &*repaint_rect : nullptr, nullptr,
                             RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
            }
            return;
        }
        static_cast<void>(frame_scheduler_.post(
            [hwnd]() noexcept {
                if (hwnd != nullptr && IsWindow(hwnd) != FALSE) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            },
            core::FrameTaskPriority::Render, "window.repaint"));
        static_cast<void>(frame_scheduler_.drain(core::FrameBudget{.max_tasks = 1U}));
    }

    [[nodiscard]] std::optional<RECT>
    immediate_repaint_rect(std::optional<layout::Rect> bounds) const noexcept {
        if (!bounds.has_value() || !layout::is_visible_rect(*bounds) || hwnd_ == nullptr) {
            return std::nullopt;
        }

        RECT client{};
        if (GetClientRect(hwnd_, &client) == FALSE) {
            return std::nullopt;
        }

        auto rect = dip_rect_to_client_pixel_rect(layout::inflate_rect(*bounds, 2.0F), dpi_);
        rect.left = std::clamp(rect.left, client.left, client.right);
        rect.top = std::clamp(rect.top, client.top, client.bottom);
        rect.right = std::clamp(rect.right, client.left, client.right);
        rect.bottom = std::clamp(rect.bottom, client.top, client.bottom);
        if (rect.right <= rect.left || rect.bottom <= rect.top) {
            return std::nullopt;
        }
        return rect;
    }

    int run() {
        return dispatcher_->run();
    }

    [[nodiscard]] LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto hook_message = WindowMessage{.hwnd = hwnd,
                                          .id = message,
                                          .wparam = static_cast<std::uintptr_t>(wparam),
                                          .lparam = static_cast<std::intptr_t>(lparam)};
        if (!message_filters_.empty()) {
            message_filters_.emit(hook_message);
            if (hook_message.handled) {
                return static_cast<LRESULT>(hook_message.result);
            }
        }
        if (!message_observers_.empty()) {
            message_observers_.emit(hook_message);
            if (hook_message.handled) {
                return static_cast<LRESULT>(hook_message.result);
            }
        }

        const auto result = [&]() -> LRESULT {
            switch (message) {
            case WM_ENTERSIZEMOVE:
                interactive_resize_ = true;
                begin_interactive_resize();
                return 0;
            case WM_EXITSIZEMOVE:
                interactive_resize_ = false;
                last_interactive_resize_flush_at_ = {};
                apply_resize_now();
                resume_animation_after_interactive_resize();
                return 0;
            case WM_SIZE:
                on_resize();
                return 0;
            case WM_DPICHANGED:
                on_dpi_changed(wparam, lparam);
                return 0;
            case WM_PAINT:
                on_paint();
                return 0;
            case WM_TIMER:
                if (wparam == caret_blink_timer_id) {
                    invalidate_focused_text_input_caret();
                    request_repaint();
                    return 0;
                }
                if (wparam == animation_frame_timer_id) {
                    on_animation_frame_timer();
                    return 0;
                }
                return DefWindowProcW(hwnd, message, wparam, lparam);
            case WM_ERASEBKGND:
                return 1;
            case WM_SETCURSOR:
                if (LOWORD(lparam) == HTCLIENT) {
                    apply_native_cursor(native_cursor_);
                    return TRUE;
                }
                return DefWindowProcW(hwnd, message, wparam, lparam);
            case WM_MOUSEMOVE:
                on_mouse_move(wparam, lparam);
                return 0;
            case WM_MOUSELEAVE:
                on_mouse_leave();
                return 0;
            case WM_LBUTTONDOWN:
                SetCapture(hwnd_);
                route_pointer(elements::PointerEventKind::Down, point_from_lparam(lparam, dpi_),
                              elements::PointerButton::Primary, wparam);
                return 0;
            case WM_LBUTTONDBLCLK:
                SetCapture(hwnd_);
                route_pointer(elements::PointerEventKind::DoubleClick,
                              point_from_lparam(lparam, dpi_), elements::PointerButton::Primary,
                              wparam, {}, 2);
                return 0;
            case WM_LBUTTONUP:
                route_pointer(elements::PointerEventKind::Up, point_from_lparam(lparam, dpi_),
                              elements::PointerButton::Primary, wparam, {}, 1);
                ReleaseCapture();
                return 0;
            case WM_RBUTTONDOWN:
                SetCapture(hwnd_);
                route_pointer(elements::PointerEventKind::Down, point_from_lparam(lparam, dpi_),
                              elements::PointerButton::Secondary, wparam);
                return 0;
            case WM_RBUTTONDBLCLK:
                SetCapture(hwnd_);
                route_pointer(elements::PointerEventKind::DoubleClick,
                              point_from_lparam(lparam, dpi_), elements::PointerButton::Secondary,
                              wparam, {}, 2);
                return 0;
            case WM_RBUTTONUP: {
                const auto position = point_from_lparam(lparam, dpi_);
                route_pointer(elements::PointerEventKind::Up, position,
                              elements::PointerButton::Secondary, wparam, {}, 1);
                ReleaseCapture();
            }
                return 0;
            case WM_MBUTTONDOWN:
                SetCapture(hwnd_);
                route_pointer(elements::PointerEventKind::Down, point_from_lparam(lparam, dpi_),
                              elements::PointerButton::Middle, wparam);
                return 0;
            case WM_MBUTTONDBLCLK:
                SetCapture(hwnd_);
                route_pointer(elements::PointerEventKind::DoubleClick,
                              point_from_lparam(lparam, dpi_), elements::PointerButton::Middle,
                              wparam, {}, 2);
                return 0;
            case WM_MBUTTONUP:
                route_pointer(elements::PointerEventKind::Up, point_from_lparam(lparam, dpi_),
                              elements::PointerButton::Middle, wparam, {}, 1);
                ReleaseCapture();
                return 0;
            case WM_XBUTTONDOWN:
                SetCapture(hwnd_);
                route_pointer(elements::PointerEventKind::Down, point_from_lparam(lparam, dpi_),
                              xbutton_from_wparam(wparam), wparam);
                return TRUE;
            case WM_XBUTTONDBLCLK:
                SetCapture(hwnd_);
                route_pointer(elements::PointerEventKind::DoubleClick,
                              point_from_lparam(lparam, dpi_), xbutton_from_wparam(wparam), wparam,
                              {}, 2);
                return TRUE;
            case WM_XBUTTONUP:
                route_pointer(elements::PointerEventKind::Up, point_from_lparam(lparam, dpi_),
                              xbutton_from_wparam(wparam), wparam, {}, 1);
                ReleaseCapture();
                return TRUE;
            case WM_MOUSEWHEEL:
                route_wheel(wparam, lparam, false);
                return 0;
            case WM_MOUSEHWHEEL:
                route_wheel(wparam, lparam, true);
                return 0;
            case WM_CONTEXTMENU:
                if (show_text_input_context_menu_from_message(lparam)) {
                    return 0;
                }
                return DefWindowProcW(hwnd, message, wparam, lparam);
            case WM_KEYDOWN:
                route_key(elements::KeyEventKind::Down, key_from_virtual_key(wparam), {});
                return 0;
            case WM_KEYUP:
                route_key(elements::KeyEventKind::Up, key_from_virtual_key(wparam), {});
                return 0;
            case WM_CHAR:
                if (wparam >= 0x20 && wparam != 0x7F) {
                    const auto text = text_from_wm_char(wparam);
                    if (!text.empty()) {
                        route_key(elements::KeyEventKind::TextInput, elements::Key::Unknown, text);
                    }
                }
                return 0;
            case WM_IME_STARTCOMPOSITION:
                ime_state_.start_composition();
                update_ime_window_position();
                start_ime_text_input();
                return 0;
            case WM_IME_COMPOSITION:
                if ((lparam & GCS_RESULTSTR) != 0) {
                    ime_state_.commit_result();
                    commit_ime_text_input(ime_composition_text(hwnd_, GCS_RESULTSTR));
                    return 0;
                }
                if ((lparam & GCS_COMPSTR) != 0) {
                    update_ime_text_input(ime_composition_text(hwnd_, GCS_COMPSTR));
                    return 0;
                }
                return DefWindowProcW(hwnd, message, wparam, lparam);
            case WM_IME_ENDCOMPOSITION:
                ime_state_.finish_composition();
                end_ime_text_input_if_idle();
                update_ime_window_position();
                return 0;
            case WM_IME_NOTIFY:
                switch (wparam) {
                case IMN_OPENCANDIDATE:
                case IMN_CHANGECANDIDATE:
                    ime_state_.open_candidate();
                    update_ime_window_position();
                    start_ime_text_input();
                    if (const auto text = ime_composition_text(hwnd_, GCS_COMPSTR); !text.empty()) {
                        update_ime_text_input(text);
                    }
                    return DefWindowProcW(hwnd, message, wparam, lparam);
                case IMN_CLOSECANDIDATE:
                    ime_state_.close_candidate();
                    end_ime_text_input_if_idle();
                    update_ime_window_position();
                    return DefWindowProcW(hwnd, message, wparam, lparam);
                default:
                    return DefWindowProcW(hwnd, message, wparam, lparam);
                }
            case WM_CLOSE:
                destroy_native_window();
                return 0;
            case animation_frame_message:
                on_animation_frame_message();
                return 0;
            case managed_close_message:
                destroy_native_window_on_owner_thread();
                return 0;
            case WM_DESTROY:
                on_destroy();
                return 0;
            case WM_NCDESTROY:
                on_native_destroyed(hwnd);
                return DefWindowProcW(hwnd, message, wparam, lparam);
            default:
                return DefWindowProcW(hwnd, message, wparam, lparam);
            }
        }();

        if (!post_message_filters_.empty()) {
            hook_message.result = static_cast<std::intptr_t>(result);
            post_message_filters_.emit(hook_message);
            if (hook_message.handled) {
                return static_cast<LRESULT>(hook_message.result);
            }
        }
        if (!post_message_observers_.empty()) {
            hook_message.result = static_cast<std::intptr_t>(result);
            post_message_observers_.emit(hook_message);
            if (hook_message.handled) {
                return static_cast<LRESULT>(hook_message.result);
            }
        }
        return result;
    }

  private:
    using TimePeriodProc = UINT(WINAPI*)(UINT);

    void request_repaint_from_any_thread() noexcept {
        const auto hwnd = async_hwnd_.load(std::memory_order_acquire);
        if (hwnd == nullptr || repaint_requested_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        if (IsWindow(hwnd) != FALSE) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    void request_animation_repaint_from_any_thread() noexcept {
        const auto hwnd = async_hwnd_.load(std::memory_order_acquire);
        if (hwnd == nullptr ||
            animation_repaint_pending_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        if (IsWindow(hwnd) != FALSE) {
            static_cast<void>(PostMessageW(hwnd, animation_frame_message, 0, 0));
        } else {
            animation_repaint_pending_.store(false, std::memory_order_release);
        }
    }

    void on_animation_frame_message() noexcept {
        arm_animation_repaint_timer();
    }

    void on_animation_frame_timer() noexcept {
        KillTimer(hwnd_, animation_frame_timer_id);
        arm_animation_repaint_timer();
    }

    void arm_animation_repaint_timer() noexcept {
        if (hwnd_ == nullptr || interactive_resize_) {
            animation_repaint_pending_.store(false, std::memory_order_release);
            release_animation_timer_resolution();
            return;
        }

        ensure_animation_timer_resolution();
        const auto now = std::chrono::steady_clock::now();
        if (next_animation_repaint_at_ == std::chrono::steady_clock::time_point{}) {
            next_animation_repaint_at_ = now + animation_frame_interval;
        }

        if (now >= next_animation_repaint_at_) {
            fire_animation_repaint(now);
            return;
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(next_animation_repaint_at_ - now);
        const auto delay = static_cast<UINT>(std::max<std::int64_t>(1, remaining.count()));
        SetTimer(hwnd_, animation_frame_timer_id, delay, nullptr);
    }

    void fire_animation_repaint(std::chrono::steady_clock::time_point now) noexcept {
        animation_repaint_pending_.store(false, std::memory_order_release);
        if (next_animation_repaint_at_ == std::chrono::steady_clock::time_point{} ||
            now - next_animation_repaint_at_ > animation_frame_interval) {
            next_animation_repaint_at_ = now + animation_frame_interval;
        } else {
            next_animation_repaint_at_ += animation_frame_interval;
        }
        request_animation_ui_frame();
    }

    void ensure_animation_timer_resolution() noexcept {
        if (animation_timer_resolution_active_) {
            return;
        }

        if (winmm_module_ == nullptr) {
            winmm_module_ = LoadLibraryW(L"winmm.dll");
            if (winmm_module_ != nullptr) {
                time_begin_period_ = reinterpret_cast<TimePeriodProc>(
                    GetProcAddress(winmm_module_, "timeBeginPeriod"));
                time_end_period_ = reinterpret_cast<TimePeriodProc>(
                    GetProcAddress(winmm_module_, "timeEndPeriod"));
            }
        }

        if (time_begin_period_ != nullptr &&
            time_begin_period_(animation_timer_resolution_ms) == 0U) {
            animation_timer_resolution_active_ = true;
        }
    }

    void release_animation_timer_resolution() noexcept {
        if (animation_timer_resolution_active_ && time_end_period_ != nullptr) {
            static_cast<void>(time_end_period_(animation_timer_resolution_ms));
        }
        animation_timer_resolution_active_ = false;
    }

    void request_animation_ui_frame() noexcept {
        if (hwnd_ == nullptr) {
            return;
        }

        try {
            ensure_render_worker();
            ensure_ui_frame_worker();
            if (render_worker_ == nullptr) {
                return;
            }

            const auto target_pixel_size = client_pixel_size(hwnd_);
            const auto layout_size = client_dip_size(hwnd_, dpi_);

            UiFrameRequest request;
            request.layout_constraints.width = layout_size.width;
            request.layout_constraints.height = layout_size.height;
            request.target_pixel_width = target_pixel_size.width;
            request.target_pixel_height = target_pixel_size.height;
            request.tick_animations = !interactive_resize_;

            if (interactive_resize_) {
                cancel_pending_ui_frame();
                prepare_ui_frame(std::move(request), true);
            } else {
                post_ui_frame(std::move(request));
            }
        } catch (...) {
            request_repaint();
        }
    }

    void start_ui_frame_worker() {
        if (ui_worker_.joinable()) {
            return;
        }
        {
            const std::scoped_lock lock(ui_worker_mutex_);
            ui_worker_stopping_.store(false, std::memory_order_release);
        }
        ui_worker_ = std::thread([this]() noexcept { run_ui_frame_worker(); });
    }

    void ensure_ui_frame_worker() noexcept {
        try {
            start_ui_frame_worker();
        } catch (...) {
        }
    }

    void stop_ui_frame_worker() noexcept {
        {
            const std::scoped_lock lock(ui_worker_mutex_);
            if (ui_worker_stopping_.load(std::memory_order_acquire) && !ui_worker_.joinable()) {
                return;
            }
            ui_worker_stopping_.store(true, std::memory_order_release);
            pending_ui_frame_.reset();
        }
        ui_worker_wake_.notify_one();
        if (ui_worker_.joinable()) {
            ui_worker_.join();
        }
    }

    void post_ui_frame(UiFrameRequest request) noexcept {
        try {
            {
                const std::scoped_lock lock(ui_worker_mutex_);
                if (ui_worker_stopping_.load(std::memory_order_acquire)) {
                    return;
                }

                if (pending_ui_frame_.has_value()) {
                    pending_ui_frame_->dirty_region.add(request.dirty_region);
                    pending_ui_frame_->layout_constraints = request.layout_constraints;
                    pending_ui_frame_->target_pixel_width = request.target_pixel_width;
                    pending_ui_frame_->target_pixel_height = request.target_pixel_height;
                } else {
                    pending_ui_frame_ = std::move(request);
                }
            }
            ui_worker_wake_.notify_one();
        } catch (...) {
            request_repaint_from_any_thread();
        }
    }

    void cancel_pending_ui_frame() noexcept {
        const std::scoped_lock lock(ui_worker_mutex_);
        pending_ui_frame_.reset();
    }

    void run_ui_frame_worker() noexcept {
        for (;;) {
            UiFrameRequest request;
            {
                std::unique_lock lock(ui_worker_mutex_);
                ui_worker_wake_.wait(lock, [this]() noexcept {
                    return ui_worker_stopping_.load(std::memory_order_acquire) ||
                           pending_ui_frame_.has_value();
                });
                if (ui_worker_stopping_.load(std::memory_order_acquire)) {
                    return;
                }
                request = std::move(*pending_ui_frame_);
                pending_ui_frame_.reset();
            }

            prepare_ui_frame(std::move(request), false);
        }
    }

    void prepare_ui_frame(UiFrameRequest request, bool wait_for_render) noexcept {
        auto animations_active = false;
        try {
            std::unique_lock lock(ui_tree_mutex_, std::defer_lock);
            while (!lock.try_lock_for(std::chrono::milliseconds(1))) {
                if (ui_worker_stopping_.load(std::memory_order_acquire)) {
                    return;
                }
            }
            if (render_worker_ == nullptr ||
                async_hwnd_.load(std::memory_order_acquire) == nullptr) {
                return;
            }

            if (content_ != nullptr) {
                detail::UIElementThreadAccessScope access_scope(*content_,
                                                                std::this_thread::get_id());
                if (request.tick_animations) {
                    animations_active = content_->tick_animations();
                }
                if (content_->needs_layout()) {
                    content_->calculate_layout(request.layout_constraints);
                }
                render_cache_.commit_if_needed(*content_, request.dirty_region);
            }

            if (!request.dirty_region.empty()) {
                auto frame =
                    win32::RenderFrame{.clear_color = rendering::Color::rgba(255, 255, 255),
                                       .dirty_region = std::move(request.dirty_region),
                                       .render_scene = render_cache_.snapshot(),
                                       .promotion_plan = render_cache_.promotion_plan(),
                                       .frame_graph = render_cache_.frame_graph(),
                                       .target_pixel_width = request.target_pixel_width,
                                       .target_pixel_height = request.target_pixel_height};
                if (wait_for_render) {
                    const auto result = render_worker_->render_sync(std::move(frame));
                    if (result != win32::RenderJobResult::Completed) {
                        request_repaint_from_any_thread();
                    }
                } else {
                    render_worker_->render(std::move(frame));
                }
            }
        } catch (...) {
            std::unique_lock lock(ui_tree_mutex_, std::defer_lock);
            if (lock.try_lock_for(std::chrono::milliseconds(1)) && render_worker_ != nullptr) {
                render_worker_->discard();
            }
            request_repaint_from_any_thread();
        }

        if (animations_active) {
            request_animation_repaint_from_any_thread();
        } else {
            release_animation_timer_resolution();
        }
    }

    void start_render_worker() {
        if (render_worker_ != nullptr || hwnd_ == nullptr) {
            return;
        }
        render_worker_ =
            std::make_unique<win32::WindowRenderWorker>(hwnd_, options_.trim_render_memory_on_idle);
        render_worker_->set_dpi(dpi_);
    }

    void ensure_render_worker() noexcept {
        try {
            start_render_worker();
        } catch (...) {
        }
    }

    void destroy_native_window() noexcept {
        if (hwnd_ != nullptr && !dispatcher_->is_current_thread()) {
            SendMessageW(hwnd_, managed_close_message, 0, 0);
            return;
        }

        destroy_native_window_on_owner_thread();
    }

    void destroy_native_window_on_owner_thread() noexcept {
        stop_ui_frame_worker();
        render_worker_.reset();
        release_animation_timer_resolution();
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
    }

    void register_class() {
        retain_window_class(&Window::Impl::window_proc);
        registered_window_class_ = true;
    }

    void create_window() {
        owner_hwnd_ = options_.owner != nullptr && options_.owner->impl_ != nullptr
                          ? options_.owner->impl_->async_hwnd_.load(std::memory_order_acquire)
                          : nullptr;
        auto create_params = WindowCreateParams{.title = options_.title,
                                                .x = use_default_window_coordinate,
                                                .y = use_default_window_coordinate,
                                                .width = std::max(options_.width, 1),
                                                .height = std::max(options_.height, 1),
                                                .style = WS_OVERLAPPEDWINDOW,
                                                .extended_style = options_.use_no_redirection_bitmap
                                                                      ? WS_EX_NOREDIRECTIONBITMAP
                                                                      : DWORD{0},
                                                .owner_handle = owner_hwnd_};
        if (options_.on_before_create) {
            options_.on_before_create(create_params);
        }

        options_.title = create_params.title;
        RECT rect{0, 0, std::max(create_params.width, 1), std::max(create_params.height, 1)};
        AdjustWindowRectEx(&rect, static_cast<DWORD>(create_params.style), FALSE,
                           static_cast<DWORD>(create_params.extended_style));

        owner_hwnd_ = static_cast<HWND>(create_params.owner_handle);
        auto x = create_params.x == use_default_window_coordinate ? CW_USEDEFAULT : create_params.x;
        auto y = create_params.y == use_default_window_coordinate ? CW_USEDEFAULT : create_params.y;
        if (create_params.x == use_default_window_coordinate &&
            create_params.y == use_default_window_coordinate && owner_hwnd_ != nullptr &&
            options_.center_on_owner) {
            RECT owner_rect{};
            if (GetWindowRect(owner_hwnd_, &owner_rect) != FALSE) {
                const auto width = rect.right - rect.left;
                const auto height = rect.bottom - rect.top;
                x = owner_rect.left +
                    std::max<LONG>((owner_rect.right - owner_rect.left - width) / 2, 0L);
                y = owner_rect.top +
                    std::max<LONG>((owner_rect.bottom - owner_rect.top - height) / 2, 0L);
            }
        }

        hwnd_ = CreateWindowExW(static_cast<DWORD>(create_params.extended_style), class_name,
                                options_.title.c_str(), static_cast<DWORD>(create_params.style), x,
                                y, rect.right - rect.left, rect.bottom - rect.top, owner_hwnd_,
                                nullptr, GetModuleHandleW(nullptr), this);
        if (hwnd_ == nullptr) {
            throw make_win32_error("failed to create WinElement window");
        }
        async_hwnd_.store(hwnd_, std::memory_order_release);

        dispatcher_->register_window(hwnd_);
        counted_as_live_window_ = true;
        SetTimer(hwnd_, caret_blink_timer_id, caret_blink_timer_interval_ms, nullptr);
    }

    void on_destroy() noexcept {
        stop_ui_frame_worker();
        ++text_input_menu_target_generation_;
        native_menu_target_invalidated_ = true;
        if (GetCapture() == hwnd_) {
            ReleaseCapture();
        }
        if (hwnd_ != nullptr) {
            KillTimer(hwnd_, caret_blink_timer_id);
            KillTimer(hwnd_, animation_frame_timer_id);
        }
        animation_repaint_pending_.store(false, std::memory_order_release);
        next_animation_repaint_at_ = {};
        release_animation_timer_resolution();
        tracking_mouse_leave_ = false;
        render_worker_.reset();
    }

    void on_native_destroyed(HWND hwnd) noexcept {
        on_destroy();
        if (modal_owner_acquired_) {
            release_modal_owner(owner_hwnd_);
            modal_owner_acquired_ = false;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        async_hwnd_.store(nullptr, std::memory_order_release);
        if (counted_as_live_window_) {
            counted_as_live_window_ = false;
            dispatcher_->unregister_window(hwnd);
        }
        if (!closed_event_.empty()) {
            closed_event_.emit();
        }
    }

    void on_resize() {
        apply_resize_now();
    }

    void apply_resize_now() {
        ensure_render_worker();
        {
            const std::scoped_lock lock(ui_tree_mutex_);
            render_cache_.invalidate();
            if (content_ != nullptr) {
                content_->invalidate_layout();
            }
        }
        request_repaint();
        const auto now = std::chrono::steady_clock::now();
        const auto should_flush =
            !interactive_resize_ ||
            last_interactive_resize_flush_at_ == std::chrono::steady_clock::time_point{} ||
            now - last_interactive_resize_flush_at_ >= interactive_resize_frame_interval;
        if (hwnd_ != nullptr && should_flush) {
            last_interactive_resize_flush_at_ = now;
            // Flush the invalidated paint during the modal size loop so layout changes
            // track the client rect, but do not render every raw WM_SIZE while dragging.
            UpdateWindow(hwnd_);
        }
    }

    void begin_interactive_resize() noexcept {
        last_interactive_resize_flush_at_ = {};
        animation_repaint_pending_.store(false, std::memory_order_release);
        next_animation_repaint_at_ = {};
        if (hwnd_ != nullptr) {
            KillTimer(hwnd_, animation_frame_timer_id);
        }
        release_animation_timer_resolution();
    }

    void resume_animation_after_interactive_resize() noexcept {
        next_animation_repaint_at_ = {};
        request_animation_repaint_from_any_thread();
    }

    void on_dpi_changed(WPARAM wparam, LPARAM lparam) {
        dpi_ = static_cast<float>(HIWORD(wparam));
        {
            const std::scoped_lock lock(ui_tree_mutex_);
            layout_engine_.set_point_scale_factor(dpi_ / default_dpi);
        }

        const auto* suggested_rect = reinterpret_cast<const RECT*>(lparam);
        if (suggested_rect != nullptr) {
            SetWindowPos(hwnd_, nullptr, suggested_rect->left, suggested_rect->top,
                         suggested_rect->right - suggested_rect->left,
                         suggested_rect->bottom - suggested_rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }

        if (render_worker_ != nullptr) {
            render_worker_->set_dpi(dpi_);
        }

        {
            const std::scoped_lock lock(ui_tree_mutex_);
            render_cache_.invalidate();
            if (content_ != nullptr) {
                content_->invalidate_layout();
            }
        }
        request_repaint();
    }

    void update_dpi() {
        dpi_ = query_window_dpi(hwnd_);
        const std::scoped_lock lock(ui_tree_mutex_);
        layout_engine_.set_point_scale_factor(dpi_ / default_dpi);
    }

    void on_paint() {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd_, &paint);
        repaint_requested_.store(false, std::memory_order_release);
        const auto low_latency_repaint =
            low_latency_repaint_pending_.exchange(false, std::memory_order_acq_rel);

        try {
            ensure_render_worker();
            ensure_ui_frame_worker();
            if (render_worker_ != nullptr) {
                const auto target_pixel_size = client_pixel_size(hwnd_);
                const auto layout_size = client_dip_size(hwnd_, dpi_);
                rendering::DirtyRegion dirty_region;
                dirty_region.add(client_pixel_rect_to_dip(paint.rcPaint, dpi_));

                UiFrameRequest request;
                request.dirty_region = std::move(dirty_region);
                request.layout_constraints.width = layout_size.width;
                request.layout_constraints.height = layout_size.height;
                request.target_pixel_width = target_pixel_size.width;
                request.target_pixel_height = target_pixel_size.height;
                request.tick_animations = !interactive_resize_;
                if (interactive_resize_ || low_latency_repaint) {
                    cancel_pending_ui_frame();
                    prepare_ui_frame(std::move(request),
                                     interactive_resize_ || low_latency_repaint);
                } else {
                    post_ui_frame(std::move(request));
                }
            }
        } catch (...) {
            if (render_worker_ != nullptr) {
                render_worker_->discard();
            }
        }

        EndPaint(hwnd_, &paint);
    }

    [[nodiscard]] layout::Point screen_point_from_lparam(LPARAM lparam) const noexcept {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (hwnd_ == nullptr || ScreenToClient(hwnd_, &point) == FALSE) {
            return last_pointer_position_;
        }
        const auto scale = default_dpi / std::max(dpi_, 1.0F);
        return layout::Point{static_cast<float>(point.x) * scale,
                             static_cast<float>(point.y) * scale};
    }

    [[nodiscard]] elements::PointerButton xbutton_from_wparam(WPARAM wparam) const noexcept {
        return HIWORD(wparam) == XBUTTON1 ? elements::PointerButton::X1
                                          : elements::PointerButton::X2;
    }

    [[nodiscard]] static bool has_pointer_button_state(WPARAM wparam, UINT flag) noexcept {
        return (LOWORD(wparam) & flag) != 0;
    }

    [[nodiscard]] static bool has_any_pointer_button_state(WPARAM wparam) noexcept {
        constexpr auto button_flags =
            MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2;
        return (LOWORD(wparam) & button_flags) != 0;
    }

    [[nodiscard]] static bool
    should_flush_pointer_frame(elements::PointerEventKind kind, WPARAM button_state,
                               elements::RoutedEventResult result) noexcept {
        return kind == elements::PointerEventKind::Move && result.handled &&
               has_any_pointer_button_state(button_state);
    }

    [[nodiscard]] static std::optional<layout::Rect>
    low_latency_pointer_dirty_bounds(const elements::RoutedEventResult& result) noexcept {
        const auto* element = result.handled_by != nullptr ? result.handled_by : result.target;
        if (element == nullptr) {
            return std::nullopt;
        }

        const auto frame = element->absolute_frame();
        if (!layout::is_visible_rect(frame)) {
            return std::nullopt;
        }

        const auto origin = layout::Point{frame.x, frame.y};
        const auto transform =
            rendering::transform_around_point(element->render_transform(), origin);
        return layout::inflate_rect(
            rendering::transform_rect(layout::inflate_rect(frame, 64.0F), transform), 4.0F);
    }

    void on_mouse_move(WPARAM wparam, LPARAM lparam) {
        if (!tracking_mouse_leave_) {
            TRACKMOUSEEVENT track_event{};
            track_event.cbSize = sizeof(track_event);
            track_event.dwFlags = TME_LEAVE;
            track_event.hwndTrack = hwnd_;
            tracking_mouse_leave_ = TrackMouseEvent(&track_event) != FALSE;
            route_pointer(elements::PointerEventKind::Enter, point_from_lparam(lparam, dpi_),
                          elements::PointerButton::None, wparam);
        }

        route_pointer(elements::PointerEventKind::Move, point_from_lparam(lparam, dpi_),
                      elements::PointerButton::None, wparam);
    }

    void on_mouse_leave() {
        tracking_mouse_leave_ = false;
        route_pointer(elements::PointerEventKind::Leave, last_pointer_position_,
                      elements::PointerButton::None, 0);
        apply_native_cursor(elements::PointerCursor::Arrow);
    }

    void route_wheel(WPARAM wparam, LPARAM lparam, bool horizontal) {
        const auto delta =
            static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
        const auto wheel_delta =
            horizontal ? layout::Point{delta, 0.0F} : layout::Point{0.0F, delta};
        route_pointer(horizontal ? elements::PointerEventKind::HorizontalWheel
                                 : elements::PointerEventKind::Wheel,
                      screen_point_from_lparam(lparam), elements::PointerButton::None, wparam,
                      wheel_delta);
    }

    void route_pointer(elements::PointerEventKind kind, layout::Point position,
                       elements::PointerButton button, WPARAM button_state,
                       layout::Point wheel_delta = {}, std::uint8_t click_count = 0) {
        auto low_latency_repaint = false;
        auto low_latency_bounds = std::optional<layout::Rect>{};
        {
            const std::scoped_lock lock(ui_tree_mutex_);
            if (router_ == nullptr) {
                return;
            }

            last_pointer_position_ = position;
            const auto result = router_->route_pointer_event(elements::PointerEvent{
                .kind = kind,
                .position = position,
                .wheel_delta = wheel_delta,
                .button = button,
                .click_count = click_count,
                .primary_button_down = has_pointer_button_state(button_state, MK_LBUTTON),
                .secondary_button_down = has_pointer_button_state(button_state, MK_RBUTTON),
                .middle_button_down = has_pointer_button_state(button_state, MK_MBUTTON),
                .x1_button_down = has_pointer_button_state(button_state, MK_XBUTTON1),
                .x2_button_down = has_pointer_button_state(button_state, MK_XBUTTON2),
                .modifiers = current_modifiers()});
            low_latency_repaint = should_flush_pointer_frame(kind, button_state, result);
            if (low_latency_repaint) {
                const auto current_bounds = low_latency_pointer_dirty_bounds(result);
                if (current_bounds.has_value()) {
                    low_latency_bounds =
                        last_low_latency_repaint_bounds_.has_value()
                            ? layout::union_rects(*last_low_latency_repaint_bounds_,
                                                  *current_bounds)
                            : *current_bounds;
                    last_low_latency_repaint_bounds_ = current_bounds;
                }
            } else if (kind != elements::PointerEventKind::Enter) {
                last_low_latency_repaint_bounds_.reset();
            }

            const auto captured_direct_move =
                low_latency_repaint && router_->pointer_capture() != nullptr;
            if (!captured_direct_move) {
                apply_native_cursor(router_->cursor_for_point(position));
            }
            if (kind != elements::PointerEventKind::Move || !result.handled) {
                update_ime_window_position();
            }
        }
        request_repaint(low_latency_repaint, low_latency_bounds);
    }

    void apply_native_cursor(elements::PointerCursor cursor) noexcept {
        const auto normalized =
            cursor == elements::PointerCursor::Default ? elements::PointerCursor::Arrow : cursor;
        native_cursor_ = normalized;
        SetCursor(LoadCursorW(nullptr, native_cursor_id(normalized)));
    }

    void route_key(elements::KeyEventKind kind, elements::Key key, std::string text) {
        const std::scoped_lock lock(ui_tree_mutex_);
        if (router_ == nullptr) {
            return;
        }
        router_->route_key_event(elements::KeyEvent{
            .kind = kind, .key = key, .text = std::move(text), .modifiers = current_modifiers()});
        update_ime_window_position();
        request_repaint();
    }

    void start_ime_text_input() {
        if (ime_state_.text_input_active) {
            return;
        }

        ime_state_.text_input_active = true;
        route_key(elements::KeyEventKind::CompositionStart, elements::Key::Unknown, {});
    }

    void update_ime_text_input(std::string text) {
        start_ime_text_input();
        route_key(elements::KeyEventKind::CompositionUpdate, elements::Key::Unknown,
                  std::move(text));
    }

    void commit_ime_text_input(std::string text) {
        ime_state_.text_input_active = false;
        route_key(elements::KeyEventKind::CompositionEnd, elements::Key::Unknown, std::move(text));
    }

    [[nodiscard]] std::string text_from_wm_char(WPARAM wparam) {
        const auto value = static_cast<wchar_t>(wparam);
        if (value >= 0xD800 && value <= 0xDBFF) {
            pending_high_surrogate_ = value;
            return {};
        }

        if (value >= 0xDC00 && value <= 0xDFFF) {
            if (pending_high_surrogate_ == 0) {
                return {};
            }
            const std::array<wchar_t, 2U> pair{pending_high_surrogate_, value};
            pending_high_surrogate_ = 0;
            return wide_text_to_utf8(std::wstring_view{pair.data(), pair.size()});
        }

        pending_high_surrogate_ = 0;
        return wide_char_to_utf8(value);
    }

    void end_ime_text_input_if_idle() {
        if (!ime_state_.can_end_text_input()) {
            return;
        }

        ime_state_.text_input_active = false;
        route_key(elements::KeyEventKind::CompositionEnd, elements::Key::Unknown, {});
    }

    void invalidate_focused_text_input_caret() noexcept {
        const std::scoped_lock lock(ui_tree_mutex_);
        if (router_ == nullptr) {
            return;
        }

        auto* focused_element = router_->focus_manager().focused_element();
        if (focused_element == nullptr || !focused_element->text_input_caret_rect()) {
            return;
        }

        focused_element->invalidate_paint();
    }

    [[nodiscard]] elements::UIElement* text_input_context_menu_owner() noexcept {
        if (router_ == nullptr) {
            return nullptr;
        }

        for (auto* current = router_->focus_manager().focused_element(); current != nullptr;
             current = current->parent()) {
            if (current->text_input_context_menu_open()) {
                return current;
            }
        }
        return nullptr;
    }

    [[nodiscard]] elements::UIElement*
    text_input_context_menu_target(layout::Point position) noexcept {
        if (content_ != nullptr) {
            for (auto* current = content_->hit_test(position); current != nullptr;
                 current = current->parent()) {
                if (has_text_input_menu_commands(current->text_input_edit_command_state())) {
                    return current;
                }
            }
        }

        if (router_ == nullptr) {
            return nullptr;
        }
        for (auto* current = router_->focus_manager().focused_element(); current != nullptr;
             current = current->parent()) {
            if (has_text_input_menu_commands(current->text_input_edit_command_state())) {
                return current;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool show_text_input_context_menu(layout::Point position) noexcept {
        auto* target = static_cast<elements::UIElement*>(nullptr);
        auto state = elements::TextInputEditCommandState{};
        auto target_generation = std::uint64_t{};
        {
            const std::scoped_lock lock(ui_tree_mutex_);
            target = text_input_context_menu_target(position);
            if (target == nullptr) {
                return false;
            }

            state = target->text_input_edit_command_state();
            if (!has_text_input_menu_commands(state)) {
                return false;
            }

            if (auto* owner = text_input_context_menu_owner();
                owner != nullptr && owner != target) {
                owner->dismiss_text_input_context_menu();
            }

            if (target->show_text_input_context_menu(position)) {
                update_ime_window_position();
                request_repaint();
                return true;
            }

            if (hwnd_ == nullptr) {
                return false;
            }
            native_menu_target_invalidated_ = false;
            target_generation = text_input_menu_target_generation_;
        }

        struct ScopedMenu final {
            HMENU handle = nullptr;

            ~ScopedMenu() {
                if (handle != nullptr) {
                    DestroyMenu(handle);
                }
            }
        } menu{CreatePopupMenu()};

        if (menu.handle == nullptr) {
            return false;
        }

        auto append_command = [&menu](UINT id, const wchar_t* label) noexcept {
            return AppendMenuW(menu.handle, MF_STRING, id, label) != FALSE;
        };

        auto appended = 0U;
        if (state.can_cut && append_command(native_text_command_cut_id, L"Cut")) {
            ++appended;
        }
        if (state.can_copy && append_command(native_text_command_copy_id, L"Copy")) {
            ++appended;
        }
        if (state.can_paste && append_command(native_text_command_paste_id, L"Paste")) {
            ++appended;
        }
        if (state.can_select_all &&
            append_command(native_text_command_select_all_id, L"Select all")) {
            ++appended;
        }

        if (appended == 0U) {
            return false;
        }

        const auto scale = std::max(dpi_, 1.0F) / default_dpi;
        POINT screen_point{static_cast<LONG>(std::lround(position.x * scale)),
                           static_cast<LONG>(std::lround(position.y * scale))};
        const auto menu_hwnd = hwnd_;
        if (menu_hwnd == nullptr || ClientToScreen(menu_hwnd, &screen_point) == FALSE) {
            return false;
        }

        const auto selected_command =
            TrackPopupMenuEx(menu.handle, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             screen_point.x, screen_point.y, menu_hwnd, nullptr);

        const std::scoped_lock lock(ui_tree_mutex_);
        if (native_menu_target_invalidated_ ||
            target_generation != text_input_menu_target_generation_ || hwnd_ == nullptr) {
            return true;
        }

        auto command = std::optional<elements::TextInputEditCommand>{};
        switch (selected_command) {
        case native_text_command_cut_id:
            command = elements::TextInputEditCommand::Cut;
            break;
        case native_text_command_copy_id:
            command = elements::TextInputEditCommand::Copy;
            break;
        case native_text_command_paste_id:
            command = elements::TextInputEditCommand::Paste;
            break;
        case native_text_command_select_all_id:
            command = elements::TextInputEditCommand::SelectAll;
            break;
        default:
            break;
        }

        if (command.has_value()) {
            static_cast<void>(target->invoke_text_input_edit_command(*command));
            update_ime_window_position();
            request_repaint();
        }

        return true;
    }

    [[nodiscard]] bool show_text_input_context_menu_from_message(LPARAM lparam) noexcept {
        if (hwnd_ == nullptr) {
            return false;
        }

        if (lparam == -1) {
            auto menu_position = layout::Point{};
            {
                const std::scoped_lock lock(ui_tree_mutex_);
                auto* focused_element =
                    router_ == nullptr ? nullptr : router_->focus_manager().focused_element();
                if (focused_element == nullptr) {
                    return false;
                }
                auto caret_rect = focused_element->text_input_caret_rect();
                if (!caret_rect) {
                    caret_rect = focused_element->absolute_frame();
                }
                menu_position = layout::Point{caret_rect->x, caret_rect->y + caret_rect->height};
            }
            return show_text_input_context_menu(menu_position);
        }

        POINT client_point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (ScreenToClient(hwnd_, &client_point) == FALSE) {
            return false;
        }
        const auto scale = default_dpi / std::max(dpi_, 1.0F);
        return show_text_input_context_menu(
            layout::Point{static_cast<float>(client_point.x) * scale,
                          static_cast<float>(client_point.y) * scale});
    }

    void update_ime_window_position() noexcept {
        const std::scoped_lock lock(ui_tree_mutex_);
        if (hwnd_ == nullptr || router_ == nullptr) {
            return;
        }

        const auto* focused_element = router_->focus_manager().focused_element();
        if (focused_element == nullptr) {
            return;
        }

        const auto caret_rect = focused_element->text_input_caret_rect();
        if (!caret_rect) {
            return;
        }

        const auto scale = dpi_ / default_dpi;
        const auto x = static_cast<LONG>(std::lround(caret_rect->x * scale));
        const auto top = static_cast<LONG>(std::lround(caret_rect->y * scale));
        const auto y = static_cast<LONG>(std::lround((caret_rect->y + caret_rect->height) * scale));
        const auto right =
            static_cast<LONG>(std::lround((caret_rect->x + caret_rect->width) * scale));
        const auto bottom =
            static_cast<LONG>(std::lround((caret_rect->y + caret_rect->height) * scale));

        HIMC ime_context = ImmGetContext(hwnd_);
        if (ime_context == nullptr) {
            return;
        }

        COMPOSITIONFORM composition_form{};
        composition_form.dwStyle = CFS_FORCE_POSITION;
        composition_form.ptCurrentPos = POINT{x, y};
        ImmSetCompositionWindow(ime_context, &composition_form);

        CANDIDATEFORM candidate_form{};
        candidate_form.dwIndex = 0;
        candidate_form.dwStyle = CFS_EXCLUDE;
        candidate_form.ptCurrentPos = POINT{x, y};
        candidate_form.rcArea = RECT{x, top, std::max(right, x + 1), std::max(bottom, top + 1)};
        ImmSetCandidateWindow(ime_context, &candidate_form);

        ImmReleaseContext(hwnd_, ime_context);
    }

    [[nodiscard]] static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                                      LPARAM lparam) {
        auto* impl = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
            impl = static_cast<Impl*>(create_struct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl));
            impl->hwnd_ = hwnd;
        }

        if (impl == nullptr) {
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }

        try {
            return impl->handle_message(hwnd, message, wparam, lparam);
        } catch (...) {
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    WindowOptions options_;
    HWND hwnd_ = nullptr;
    std::atomic<HWND> async_hwnd_ = nullptr;
    HWND owner_hwnd_ = nullptr;
    float dpi_ = default_dpi;
    layout::Point last_pointer_position_{};
    elements::PointerCursor native_cursor_ = elements::PointerCursor::Arrow;
    bool tracking_mouse_leave_ = false;
    bool counted_as_live_window_ = false;
    std::shared_ptr<detail::DispatcherState> dispatcher_;
    layout::LayoutEngine layout_engine_;
    std::unique_ptr<elements::UIElement> content_;
    std::unique_ptr<elements::EventRouter> router_;
    std::shared_ptr<elements::TextClipboardService> text_clipboard_service_ =
        std::make_shared<elements::TextClipboardService>();
    std::recursive_timed_mutex ui_tree_mutex_;
    std::mutex ui_worker_mutex_;
    std::condition_variable ui_worker_wake_;
    std::optional<UiFrameRequest> pending_ui_frame_;
    std::thread ui_worker_;
    std::atomic_bool ui_worker_stopping_ = false;
    std::unique_ptr<win32::WindowRenderWorker> render_worker_;
    WindowRenderCache render_cache_;
    core::FrameScheduler frame_scheduler_;
    std::atomic_bool repaint_requested_ = false;
    std::atomic_bool low_latency_repaint_pending_ = false;
    std::optional<layout::Rect> last_low_latency_repaint_bounds_;
    std::atomic_bool animation_repaint_pending_ = false;
    std::chrono::steady_clock::time_point next_animation_repaint_at_{};
    std::chrono::steady_clock::time_point last_interactive_resize_flush_at_{};
    HMODULE winmm_module_ = nullptr;
    TimePeriodProc time_begin_period_ = nullptr;
    TimePeriodProc time_end_period_ = nullptr;
    bool animation_timer_resolution_active_ = false;
    bool interactive_resize_ = false;
    bool modal_owner_acquired_ = false;
    bool registered_window_class_ = false;
    core::EventHandler<WindowMessage&> message_filters_;
    core::EventHandler<WindowMessage&> post_message_filters_;
    core::EventHandler<WindowMessage&> message_observers_;
    core::EventHandler<WindowMessage&> post_message_observers_;
    core::EventHandler<> closed_event_;
    WindowImeState ime_state_{};
    wchar_t pending_high_surrogate_ = 0;
    bool native_menu_target_invalidated_ = false;
    std::uint64_t text_input_menu_target_generation_ = 0;
};

Window::Window(WindowOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}

Window::~Window() = default;

Window::Window(Window&&) noexcept = default;

Window& Window::operator=(Window&&) noexcept = default;

layout::LayoutEngine& Window::layout_engine() noexcept {
    return impl_->layout_engine();
}

const layout::LayoutEngine& Window::layout_engine() const noexcept {
    return impl_->layout_engine();
}

void Window::set_content(std::unique_ptr<elements::UIElement> content) {
    impl_->set_content(std::move(content));
}

elements::UIElement* Window::content() noexcept {
    return impl_->content();
}

const elements::UIElement* Window::content() const noexcept {
    return impl_->content();
}

void Window::set_title(std::wstring_view title) {
    impl_->set_title(title);
}

NativeWindowHandle Window::native_handle() const noexcept {
    return impl_->native_handle();
}

Window::MessageFilterToken Window::add_window_message_filter(WindowMessageHook filter) {
    return impl_->add_window_message_filter(std::move(filter));
}

void Window::remove_window_message_filter(MessageFilterToken token) noexcept {
    impl_->remove_window_message_filter(token);
}

Window::MessageFilterToken Window::add_post_window_message_filter(WindowMessageHook filter) {
    return impl_->add_post_window_message_filter(std::move(filter));
}

void Window::remove_post_window_message_filter(MessageFilterToken token) noexcept {
    impl_->remove_post_window_message_filter(token);
}

core::EventHandler<WindowMessage&>& Window::window_message_observers() noexcept {
    return impl_->window_message_observers();
}

core::EventHandler<WindowMessage&>& Window::post_window_message_observers() noexcept {
    return impl_->post_window_message_observers();
}

core::EventHandler<>& Window::closed_event() noexcept {
    return impl_->closed_event();
}

void Window::show() {
    impl_->show();
}

int Window::show_modal() {
    return impl_->show_modal();
}

void Window::close() noexcept {
    impl_->close();
}

bool Window::is_open() const noexcept {
    return impl_->is_open();
}

void Window::upload_resource(rendering::RenderResourceUpload upload) noexcept {
    impl_->upload_resource(std::move(upload));
}

void Window::request_repaint() noexcept {
    impl_->request_repaint();
}

int Window::run_message_loop() {
    return detail::ensure_current_dispatcher_state()->run();
}

} // namespace winelement::platform
