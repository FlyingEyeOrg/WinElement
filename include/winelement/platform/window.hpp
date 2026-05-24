#pragma once

#include <winelement/elements/ui_element.hpp>
#include <winelement/layout/layout_engine.hpp>
#include <winelement/rendering/render_resource_queue.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace winelement::platform {

class Window;

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
    std::function<void()> on_closed;
};

class Window final {
  public:
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
