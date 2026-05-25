#include <winelement/platform/dispatcher.hpp>

#include "dispatcher_internal.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace winelement::platform {
namespace {

constexpr auto dispatcher_class_name = L"WinElementDispatcherWindow";
constexpr UINT dispatcher_wakeup_message = WM_APP + 0x4D0U;

thread_local std::weak_ptr<detail::DispatcherState> thread_dispatcher_state;

std::mutex dispatcher_registry_mutex;
std::vector<std::weak_ptr<detail::DispatcherState>> dispatcher_registry;
std::mutex dispatcher_class_mutex;
std::uint32_t dispatcher_class_ref_count = 0U;
bool dispatcher_class_owned_by_module = false;

[[nodiscard]] std::runtime_error make_win32_error(const char* message) {
    return std::runtime_error(message);
}

[[nodiscard]] LRESULT CALLBACK dispatcher_window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                                      LPARAM lparam) {
    if (message == dispatcher_wakeup_message) {
        return 0;
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void retain_dispatcher_window_class() {
    const std::scoped_lock lock(dispatcher_class_mutex);
    if (dispatcher_class_ref_count++ > 0U) {
        return;
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &dispatcher_window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = dispatcher_class_name;

    if (RegisterClassExW(&window_class) == 0) {
        const auto error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            --dispatcher_class_ref_count;
            throw make_win32_error("failed to register WinElement dispatcher window class");
        }
        dispatcher_class_owned_by_module = false;
        return;
    }

    dispatcher_class_owned_by_module = true;
}

void release_dispatcher_window_class() noexcept {
    const std::scoped_lock lock(dispatcher_class_mutex);
    if (dispatcher_class_ref_count == 0U || --dispatcher_class_ref_count != 0U) {
        return;
    }
    if (dispatcher_class_owned_by_module) {
        UnregisterClassW(dispatcher_class_name, GetModuleHandleW(nullptr));
    }
    dispatcher_class_owned_by_module = false;
}

void prune_expired_dispatchers_locked() {
    dispatcher_registry.erase(
        std::remove_if(dispatcher_registry.begin(), dispatcher_registry.end(),
                       [](const auto& weak_state) { return weak_state.expired(); }),
        dispatcher_registry.end());
}

} // namespace

namespace detail {

struct DispatcherState::Impl final {
    DWORD thread_id = GetCurrentThreadId();
    HWND message_hwnd = nullptr;
    std::atomic_uint live_window_count = 0;
    std::atomic_bool quit_requested = false;
    std::atomic_int exit_code = 0;
    std::mutex mutex;
    std::queue<std::function<void()>> callbacks;
    std::vector<HWND> windows;
};

DispatcherState::DispatcherState() : impl_(std::make_unique<Impl>()) {
    retain_dispatcher_window_class();
    impl_->message_hwnd =
        CreateWindowExW(0, dispatcher_class_name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                        GetModuleHandleW(nullptr), nullptr);
    if (impl_->message_hwnd == nullptr) {
        release_dispatcher_window_class();
        throw make_win32_error("failed to create WinElement dispatcher message window");
    }
}

DispatcherState::~DispatcherState() {
    if (impl_->message_hwnd == nullptr) {
        return;
    }

    if (is_current_thread()) {
        DestroyWindow(impl_->message_hwnd);
    } else {
        PostMessageW(impl_->message_hwnd, WM_CLOSE, 0, 0);
    }
    impl_->message_hwnd = nullptr;
    release_dispatcher_window_class();
}

void DispatcherState::post(std::function<void()> callback) {
    if (!callback) {
        throw std::invalid_argument("WinElement dispatcher cannot post an empty callback");
    }

    {
        const std::lock_guard lock(impl_->mutex);
        impl_->callbacks.push(std::move(callback));
    }

    if (PostMessageW(impl_->message_hwnd, dispatcher_wakeup_message, 0, 0) == FALSE) {
        throw make_win32_error("failed to wake WinElement dispatcher");
    }
}

void DispatcherState::request_quit(int exit_code) noexcept {
    impl_->exit_code.store(exit_code, std::memory_order_release);
    impl_->quit_requested.store(true, std::memory_order_release);
    PostMessageW(impl_->message_hwnd, dispatcher_wakeup_message, 0, 0);
}

bool DispatcherState::is_current_thread() const noexcept {
    return GetCurrentThreadId() == impl_->thread_id;
}

bool DispatcherState::has_live_windows() const noexcept {
    return impl_->live_window_count.load(std::memory_order_acquire) != 0U;
}

bool DispatcherState::quit_requested() const noexcept {
    return impl_->quit_requested.load(std::memory_order_acquire);
}

int DispatcherState::exit_code() const noexcept {
    return impl_->exit_code.load(std::memory_order_acquire);
}

int DispatcherState::run() {
    if (!is_current_thread()) {
        throw std::runtime_error("WinElement dispatcher can only run on its owning UI thread");
    }

    MSG message{};
    for (;;) {
        if (impl_->quit_requested.load(std::memory_order_acquire)) {
            drain_pending_callbacks();
            close_all_windows();
            return impl_->exit_code.load(std::memory_order_acquire);
        }
        if (!has_live_windows()) {
            drain_pending_callbacks();
            return 0;
        }

        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            return -1;
        }
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }

        if (message.hwnd == impl_->message_hwnd && message.message == dispatcher_wakeup_message) {
            run_pending_callbacks();
            continue;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool DispatcherState::process_thread_message(HWND hwnd, unsigned int message) {
    if (hwnd != impl_->message_hwnd || message != dispatcher_wakeup_message) {
        return false;
    }

    run_pending_callbacks();
    return true;
}

void DispatcherState::register_window(HWND hwnd) noexcept {
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->windows.push_back(hwnd);
    }
    impl_->live_window_count.fetch_add(1U, std::memory_order_acq_rel);
}

void DispatcherState::unregister_window(HWND hwnd) noexcept {
    {
        const std::lock_guard lock(impl_->mutex);
        const auto iterator = std::find(impl_->windows.begin(), impl_->windows.end(), hwnd);
        if (iterator != impl_->windows.end()) {
            impl_->windows.erase(iterator);
        }
    }

    auto current = impl_->live_window_count.load(std::memory_order_acquire);
    while (current != 0U &&
           !impl_->live_window_count.compare_exchange_weak(
               current, current - 1U, std::memory_order_acq_rel, std::memory_order_acquire)) {
    }

    if (current == 1U) {
        PostMessageW(impl_->message_hwnd, dispatcher_wakeup_message, 0, 0);
    }
}

void DispatcherState::close_all_windows() noexcept {
    if (!is_current_thread()) {
        request_quit(impl_->exit_code.load(std::memory_order_acquire));
        return;
    }

    std::vector<HWND> windows;
    {
        const std::lock_guard lock(impl_->mutex);
        windows = impl_->windows;
    }

    for (auto* hwnd : windows) {
        if (IsWindow(hwnd) != FALSE) {
            DestroyWindow(hwnd);
        }
    }
}

std::vector<std::function<void()>> DispatcherState::take_callbacks() {
    std::vector<std::function<void()>> callbacks;
    {
        const std::lock_guard lock(impl_->mutex);
        while (!impl_->callbacks.empty()) {
            callbacks.push_back(std::move(impl_->callbacks.front()));
            impl_->callbacks.pop();
        }
    }
    return callbacks;
}

bool DispatcherState::has_pending_callbacks() const noexcept {
    const std::lock_guard lock(impl_->mutex);
    return !impl_->callbacks.empty();
}

void DispatcherState::run_pending_callbacks() {
    for (auto& callback : take_callbacks()) {
        callback();
    }
}

void DispatcherState::drain_pending_callbacks() {
    while (has_pending_callbacks()) {
        run_pending_callbacks();
    }
}

std::shared_ptr<DispatcherState> ensure_current_dispatcher_state() {
    if (auto state = thread_dispatcher_state.lock()) {
        return state;
    }

    auto state = std::make_shared<DispatcherState>();
    thread_dispatcher_state = state;
    {
        const std::lock_guard lock(dispatcher_registry_mutex);
        prune_expired_dispatchers_locked();
        dispatcher_registry.push_back(state);
    }
    return state;
}

void request_all_dispatchers_quit(int exit_code) noexcept {
    std::vector<std::shared_ptr<DispatcherState>> states;
    {
        const std::lock_guard lock(dispatcher_registry_mutex);
        prune_expired_dispatchers_locked();
        for (const auto& weak_state : dispatcher_registry) {
            if (auto state = weak_state.lock()) {
                states.push_back(std::move(state));
            }
        }
    }

    for (const auto& state : states) {
        state->request_quit(exit_code);
    }
}

} // namespace detail

Dispatcher::Dispatcher() : state_(detail::ensure_current_dispatcher_state()) {}

Dispatcher::~Dispatcher() = default;

Dispatcher::Dispatcher(const Dispatcher&) noexcept = default;

Dispatcher& Dispatcher::operator=(const Dispatcher&) noexcept = default;

Dispatcher::Dispatcher(Dispatcher&&) noexcept = default;

Dispatcher& Dispatcher::operator=(Dispatcher&&) noexcept = default;

Dispatcher::Dispatcher(std::shared_ptr<detail::DispatcherState> state) noexcept
    : state_(std::move(state)) {}

void Dispatcher::post(std::function<void()> callback) const {
    if (state_ == nullptr) {
        throw std::runtime_error("WinElement dispatcher has no state");
    }
    state_->post(std::move(callback));
}

void Dispatcher::request_quit(int exit_code) const noexcept {
    if (state_ != nullptr) {
        state_->request_quit(exit_code);
    }
}

bool Dispatcher::is_current_thread() const noexcept {
    return state_ != nullptr && state_->is_current_thread();
}

int Dispatcher::run() const {
    if (state_ == nullptr) {
        throw std::runtime_error("WinElement dispatcher has no state");
    }
    return state_->run();
}

} // namespace winelement::platform
