#pragma once

#include <winelement/animation.hpp>
#include <winelement/controls/control.hpp>
#include <winelement/controls/control_animation.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace winelement::controls {

enum class SwitchSize { Default, Large, Small };

class Switch final : public Control {
  public:
    using ChangeHandler = std::function<void(bool)>;

    Switch();

    Switch& set_checked(bool checked);
    Switch& set_disabled(bool disabled) noexcept;
    Switch& set_loading(bool loading) noexcept;
    Switch& set_size(SwitchSize size);
    Switch& set_active_text(std::string_view text);
    Switch& set_inactive_text(std::string_view text);
    Switch& set_active_value(std::string_view value);
    Switch& set_inactive_value(std::string_view value);
    Switch& set_controlled(bool controlled) noexcept;
    Switch& set_on_change(ChangeHandler handler);
    Switch& set_style(style::UIElementStyle style) override;
    [[nodiscard]] bool checked() const noexcept;
    [[nodiscard]] bool disabled() const noexcept;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] SwitchSize size() const noexcept;
    [[nodiscard]] const std::string& active_text() const noexcept;
    [[nodiscard]] const std::string& inactive_text() const noexcept;
    [[nodiscard]] const std::string& active_value() const noexcept;
    [[nodiscard]] const std::string& inactive_value() const noexcept;
    [[nodiscard]] const std::string& value() const noexcept;
    [[nodiscard]] bool controlled() const noexcept;

  protected:
    void on_pointer_event(elements::PointerEvent& event) override;
    void on_key_event(elements::KeyEvent& event) override;
    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override;
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;

  private:
    void update_measure_callback();
    [[nodiscard]] float animated_checked_progress() const;
    [[nodiscard]] float animated_hover_progress() const;
    void animate_checked(float target);
    void animate_hover(float target);
    void toggle();

    ChangeHandler change_handler_;
    std::string active_text_;
    std::string inactive_text_;
    std::string active_value_ = "true";
    std::string inactive_value_ = "false";
    SwitchSize size_ = SwitchSize::Default;
    bool checked_ = false;
    bool loading_ = false;
    bool controlled_ = false;
    AnimatedFloat checked_progress_{0.0F};
    AnimatedFloat hover_progress_{0.0F};
};

} // namespace winelement::controls
