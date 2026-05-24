#pragma once

#include <winelement/core/event.hpp>
#include <winelement/elements/ui_element.hpp>
#include <winelement/layout/layout_engine.hpp>
#include <winelement/rendering/render_resource_queue.hpp>

#include <functional>
#include <cstdint>
#include <memory>
#include <limits>
#include <string>
#include <string_view>

namespace winelement::platform {

class Window;
using NativeWindowHandle = void*;
inline constexpr int use_default_window_coordinate = (std::numeric_limits<int>::min)();

struct WindowCreateParams {
    std::wstring title = L"WinElement";
    int x = use_default_window_coordinate;
    int y = use_default_window_coordinate;
    int width = 960;
    int height = 640;
    std::uint32_t style = 0;
    std::uint32_t extended_style = 0;
    NativeWindowHandle owner_handle = nullptr;
};

struct WindowMessage {
    NativeWindowHandle hwnd = nullptr;
    std::uint32_t id = 0;
    std::uintptr_t wparam = 0;
    std::intptr_t lparam = 0;
    std::intptr_t result = 0;
    bool handled = false;
};

using WindowCreateHook = std::function<void(WindowCreateParams&)>;
using WindowMessageHook = std::function<void(WindowMessage&)>;

struct WindowOptions {
    std::wstring title = L"WinElement";
    int width = 960;
    int height = 640;
    Window* owner = nullptr;
    bool modal = false;
    bool center_on_owner = true;
    bool use_no_redirection_bitmap = true;
    bool defer_render_thread_until_show = false;
    bool trim_render_memory_on_idle = true;
    WindowCreateHook on_before_create;
};

class Window final {
  public:
    using MessageFilterToken = core::EventToken;

    explicit Window(WindowOptions options = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;

    [[nodiscard]] layout::LayoutEngine& layout_engine() noexcept;
    [[nodiscard]] const layout::LayoutEngine& layout_engine() const noexcept;

    void set_content(std::unique_ptr<elements::UIElement> content);
    [[nodiscard]] elements::UIElement* content() noexcept;
    [[nodiscard]] const elements::UIElement* content() const noexcept;
    void set_title(std::wstring_view title);
    [[nodiscard]] NativeWindowHandle native_handle() const noexcept;
    MessageFilterToken add_window_message_filter(WindowMessageHook filter);
    void remove_window_message_filter(MessageFilterToken token) noexcept;
    MessageFilterToken add_post_window_message_filter(WindowMessageHook filter);
    void remove_post_window_message_filter(MessageFilterToken token) noexcept;
    [[nodiscard]] core::EventSignal<WindowMessage&>& window_message_observers() noexcept;
    [[nodiscard]] core::EventSignal<WindowMessage&>& post_window_message_observers() noexcept;
    [[nodiscard]] core::EventSignal<>& closed_event() noexcept;

    void show();
    int show_modal();
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    void upload_resource(rendering::RenderResourceUpload upload) noexcept;
    void request_repaint() noexcept;
    static int run_message_loop();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace winelement::platform
