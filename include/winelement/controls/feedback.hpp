#pragma once

#include <winelement/controls/button.hpp>
#include <winelement/controls/control.hpp>
#include <winelement/controls/control_animation.hpp>
#include <winelement/controls/input.hpp>
#include <winelement/controls/stack_panel.hpp>
#include <winelement/controls/text.hpp>
#include <winelement/elements/svg_icon.hpp>
#include <winelement/elements/ui_element.hpp>
#include <winelement/rendering/render_types.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace winelement::platform {
class Window;
}

namespace winelement::controls {

namespace detail {
class DialogWindowImpl;
}

enum class MessageType { Primary, Success, Warning, Info, Error };

struct MessageOptions {
    std::string text;
    MessageType type = MessageType::Info;
    bool show_close = false;
    int duration_ms = 3000;
    float width = 360.0F;
    float top = 20.0F;
};

class Message final : public Control {
  public:
    using CloseEventSignal = core::EventSignal<>;

    Message();
    ~Message() override;

    Message& set_text(std::string_view text);
    Message& set_type(MessageType type);
    Message& set_show_close(bool show_close);
    [[nodiscard]] CloseEventSignal& closed() noexcept;
    [[nodiscard]] const std::string& text() const noexcept;
    [[nodiscard]] MessageType type() const noexcept;
    [[nodiscard]] bool show_close() const noexcept;

    static Message& show(elements::UIElement& host, MessageOptions options);

  protected:
    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override;
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;
    void on_paint_overlay(rendering::RenderContext& context,
                          layout::Rect absolute_frame) const override;

  private:
    void apply_visual_state();
    void restart_open_animation() noexcept;
    void apply_open_animation() noexcept;
    void set_stack_top(float top, bool animate) noexcept;
    void sync_stack_bounds() noexcept;
    void begin_close() noexcept;
    void set_duration(int duration_ms) noexcept;
    static void relayout_host_messages(elements::UIElement& host, bool animate);
    void close();
    struct EventState;
    [[nodiscard]] EventState& ensure_event_state();

    elements::UIElement* surface_ = nullptr;
    elements::SvgIcon* status_icon_ = nullptr;
    Text* text_label_ = nullptr;
    Button* close_button_ = nullptr;
    std::string text_;
    MessageType type_ = MessageType::Info;
    bool show_close_ = false;
    bool closing_ = false;
    int duration_ms_ = 3000;
    float width_ = 360.0F;
    float height_ = 40.0F;
    float top_offset_ = 20.0F;
    animation::AnimationTimePoint opened_at_{};
    std::unique_ptr<EventState> event_state_;
    AnimatedFloat open_progress_{0.0F};
    AnimatedFloat stack_top_{20.0F};
};

enum class MessageBoxKind { Alert, Confirm, Prompt };
enum class MessageBoxAction { Confirm, Cancel, Close };

using MessageBoxContentBuilder = std::function<void(StackPanel&)>;
using MessageBoxInputValidator = std::function<std::optional<std::string>(std::string_view)>;

struct MessageBoxOptions {
    std::string title = "Message";
    std::string message;
    MessageBoxKind kind = MessageBoxKind::Alert;
    MessageType type = MessageType::Info;
    std::string confirm_button_text = "OK";
    std::string cancel_button_text = "Cancel";
    std::string input_placeholder;
    std::string input_text;
    bool show_close = true;
    bool show_cancel_button = true;
    bool confirm_loading = false;
    bool center = false;
    bool distinguish_cancel_and_close = false;
    bool draggable = true;
    bool modal = true;
    bool close_on_click_modal = true;
    bool close_on_press_escape = true;
    bool close_on_confirm = true;
    float width = 420.0F;
    MessageBoxContentBuilder content_builder;
    std::string input_error_message = "Invalid input";
    MessageBoxInputValidator input_validator;
};

class MessageBox final : public Control {
  public:
    struct ActionEvent {
        MessageBoxAction action = MessageBoxAction::Close;
        std::string_view input_text;
    };
    using ActionEventSignal = core::EventSignal<const ActionEvent&>;

    MessageBox();
    ~MessageBox() override;

    MessageBox& set_title(std::string_view title);
    MessageBox& set_message(std::string_view message);
    MessageBox& set_kind(MessageBoxKind kind);
    MessageBox& set_type(MessageType type);
    MessageBox& set_confirm_button_text(std::string_view text);
    MessageBox& set_cancel_button_text(std::string_view text);
    MessageBox& set_input_placeholder(std::string_view text);
    MessageBox& set_input_text(std::string_view text);
    MessageBox& set_show_close(bool show_close);
    MessageBox& set_show_cancel_button(bool show_cancel_button);
    MessageBox& set_confirm_loading(bool loading);
    MessageBox& set_center(bool center) noexcept;
    MessageBox& set_distinguish_cancel_and_close(bool distinguish) noexcept;
    MessageBox& set_close_on_confirm(bool close_on_confirm) noexcept;
    MessageBox& set_content_builder(MessageBoxContentBuilder builder);
    MessageBox& set_input_error_message(std::string_view text);
    MessageBox& set_input_validator(MessageBoxInputValidator validator);
    MessageBox& set_draggable(bool draggable) noexcept;
    [[nodiscard]] ActionEventSignal& action_invoked() noexcept;
    [[nodiscard]] const std::string& title() const noexcept;
    [[nodiscard]] const std::string& message() const noexcept;
    [[nodiscard]] MessageBoxKind kind() const noexcept;
    [[nodiscard]] MessageType type() const noexcept;
    [[nodiscard]] bool show_cancel_button() const noexcept;
    [[nodiscard]] bool center() const noexcept;
    [[nodiscard]] bool distinguish_cancel_and_close() const noexcept;
    [[nodiscard]] bool draggable() const noexcept;
    [[nodiscard]] std::string input_text() const;

    static MessageBox& show(elements::UIElement& host, MessageBoxOptions options);

  protected:
    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override;
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;
    void on_paint_overlay(rendering::RenderContext& context,
                          layout::Rect absolute_frame) const override;
    void on_pointer_event(elements::PointerEvent& event) override;
    [[nodiscard]] elements::PointerCursor
    cursor_for_local_point(layout::Point local_position) const noexcept override;

  private:
    void apply_visual_state();
    void restart_open_animation() noexcept;
    void apply_open_animation() noexcept;
    void close_with_action(MessageBoxAction action);
    void clear_prompt_error();
    void show_prompt_error(std::string_view message);
    void sync_prompt_error_label();
    struct EventState;
    [[nodiscard]] EventState& ensure_event_state();

    elements::UIElement* surface_ = nullptr;
    StackPanel* header_panel_ = nullptr;
    elements::SvgIcon* title_status_icon_ = nullptr;
    elements::SvgIcon* status_icon_ = nullptr;
    StackPanel* content_panel_ = nullptr;
    StackPanel* custom_content_panel_ = nullptr;
    StackPanel* footer_panel_ = nullptr;
    Text* title_label_ = nullptr;
    Text* message_label_ = nullptr;
    Text* input_error_label_ = nullptr;
    Input* input_ = nullptr;
    Button* close_button_ = nullptr;
    Button* confirm_button_ = nullptr;
    Button* cancel_button_ = nullptr;
    std::string title_;
    std::string message_;
    MessageBoxKind kind_ = MessageBoxKind::Alert;
    MessageType type_ = MessageType::Info;
    bool show_close_ = true;
    bool show_cancel_button_ = true;
    bool confirm_loading_ = false;
    bool center_ = false;
    bool distinguish_cancel_and_close_ = false;
    bool close_on_confirm_ = true;
    bool draggable_ = true;
    bool modal_ = true;
    bool input_error_visible_ = false;
    std::string input_error_message_ = "Invalid input";
    MessageBoxContentBuilder content_builder_;
    MessageBoxInputValidator input_validator_;
    bool dragging_ = false;
    layout::Point drag_start_pointer_{};
    layout::Point drag_current_delta_{};
    layout::Rect drag_start_bounds_{};
    std::unique_ptr<EventState> event_state_;
    AnimatedFloat open_progress_{0.82F};
};

struct LoadingOptions {
    std::string text = "Loading";
    rendering::Color background = rendering::Color::rgba(255, 255, 255, 204);
    rendering::Color spinner_color = rendering::Color::rgba(64, 158, 255);
    bool fullscreen = true;
    bool show_close = false;
};

class Loading final : public Control {
  public:
    class Spinner;

    Loading();

    Loading& set_text(std::string_view text);
    Loading& set_active(bool active);
    Loading& set_background(rendering::Color color) noexcept;
    Loading& set_spinner_color(rendering::Color color) noexcept;
    Loading& set_show_close(bool show_close);
    [[nodiscard]] const std::string& text() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] rendering::Color spinner_color() const noexcept;
    [[nodiscard]] bool show_close() const noexcept;

    static Loading& show(elements::UIElement& host, LoadingOptions options = {});

  protected:
    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override;
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;

  private:
    void restart_open_animation() noexcept;
    void apply_open_animation() noexcept;

    elements::UIElement* surface_ = nullptr;
    Spinner* spinner_ = nullptr;
    Text* text_label_ = nullptr;
    std::string text_;
    rendering::Color spinner_color_ = rendering::Color::rgba(64, 158, 255);
    float rotation_degrees_ = 0.0F;
    bool active_ = true;
    bool show_close_ = false;
    AnimatedFloat open_progress_{0.86F};
};

enum class DialogAction { Confirm, Cancel, Close };

struct DialogOptions {
    std::string title = "Dialog";
    std::string body;
    std::string confirm_button_text = "Confirm";
    std::string cancel_button_text = "Cancel";
    bool show_close = true;
    bool show_cancel_button = true;
    bool modal = true;
    bool close_on_click_modal = true;
    bool close_on_press_escape = true;
    bool close_on_confirm = true;
    bool fullscreen = false;
    bool draggable = true;
    float width = 520.0F;
    float height = 0.0F;
};

struct DialogWindowOptions {
    std::string title = "Dialog";
    std::string body;
    std::string confirm_button_text = "Confirm";
    std::string cancel_button_text = "Cancel";
    bool show_cancel_button = true;
    bool modal = true;
    bool close_on_confirm = true;
    int width = 520;
    int height = 0;
    platform::Window* owner = nullptr;
};

class DialogWindow final {
  public:
    using ActionEventSignal = core::EventSignal<DialogAction>;

    DialogWindow();
    ~DialogWindow();

    DialogWindow(const DialogWindow&) = delete;
    DialogWindow& operator=(const DialogWindow&) = delete;
    DialogWindow(DialogWindow&&) noexcept;
    DialogWindow& operator=(DialogWindow&&) noexcept;

    DialogWindow& set_title(std::string_view title);
    DialogWindow& set_body(std::string_view body);
    DialogWindow& set_confirm_button_text(std::string_view text);
    DialogWindow& set_cancel_button_text(std::string_view text);
    DialogWindow& set_show_cancel_button(bool show_cancel_button);
    DialogWindow& set_close_on_confirm(bool close_on_confirm) noexcept;
    DialogWindow& set_modal(bool modal) noexcept;
    DialogWindow& set_window_size(int width, int height = 0) noexcept;
    DialogWindow& set_owner(platform::Window* owner) noexcept;
    [[nodiscard]] ActionEventSignal& action_invoked() noexcept;
    [[nodiscard]] const std::string& title() const noexcept;
    [[nodiscard]] const std::string& body() const noexcept;
    [[nodiscard]] bool show_cancel_button() const noexcept;
    [[nodiscard]] bool modal() const noexcept;
    [[nodiscard]] bool is_open() const noexcept;

    void show();
    [[nodiscard]] DialogAction show_modal();
    void close();

  private:
    std::unique_ptr<detail::DialogWindowImpl> impl_;
};

class Dialog final : public Control {
  public:
    using ActionEventSignal = core::EventSignal<DialogAction>;

    Dialog();
    ~Dialog() override;

    Dialog& set_title(std::string_view title);
    Dialog& set_body(std::string_view body);
    Dialog& set_confirm_button_text(std::string_view text);
    Dialog& set_cancel_button_text(std::string_view text);
    Dialog& set_show_close(bool show_close);
    Dialog& set_show_cancel_button(bool show_cancel_button);
    Dialog& set_close_on_confirm(bool close_on_confirm) noexcept;
    Dialog& set_draggable(bool draggable) noexcept;
    [[nodiscard]] ActionEventSignal& action_invoked() noexcept;
    [[nodiscard]] const std::string& title() const noexcept;
    [[nodiscard]] const std::string& body() const noexcept;
    [[nodiscard]] bool show_close() const noexcept;
    [[nodiscard]] bool show_cancel_button() const noexcept;
    [[nodiscard]] bool draggable() const noexcept;

    static Dialog& show(elements::UIElement& host, DialogOptions options);

  protected:
    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override;
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;
    void on_pointer_event(elements::PointerEvent& event) override;
    [[nodiscard]] elements::PointerCursor
    cursor_for_local_point(layout::Point local_position) const noexcept override;

  private:
    void apply_visual_state();
    void restart_open_animation() noexcept;
    void apply_open_animation() noexcept;
    void close_with_action(DialogAction action);
    struct EventState;
    [[nodiscard]] EventState& ensure_event_state();

    elements::UIElement* surface_ = nullptr;
    Text* title_label_ = nullptr;
    Text* body_label_ = nullptr;
    Button* close_button_ = nullptr;
    Button* confirm_button_ = nullptr;
    Button* cancel_button_ = nullptr;
    std::string title_;
    std::string body_;
    bool show_close_ = true;
    bool show_cancel_button_ = true;
    bool draggable_ = true;
    bool close_on_confirm_ = true;
    bool modal_ = true;
    bool dragging_ = false;
    layout::Point drag_start_pointer_{};
    layout::Point drag_current_delta_{};
    layout::Rect drag_start_bounds_{};
    std::unique_ptr<EventState> event_state_;
    AnimatedFloat open_progress_{0.82F};
};

} // namespace winelement::controls
