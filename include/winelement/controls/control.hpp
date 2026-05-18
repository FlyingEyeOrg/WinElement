#pragma once

#include <winelement/elements/ui_element.hpp>

#include <string>
#include <string_view>

namespace winelement::controls {

enum class ControlValidationState { None, Valid, Warning, Invalid };

class Control : public elements::UIElement {
  public:
    Control();
    ~Control() override;

    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;
    Control(Control&&) = delete;
    Control& operator=(Control&&) = delete;

    Control& set_form_name(std::string_view name);
    Control& set_form_value(std::string_view value);
    Control& set_required(bool required) noexcept;
    Control& set_validation_state(ControlValidationState state);
    Control& set_validation_message(std::string_view message);
    Control& set_accessibility_label(std::string_view label);

    [[nodiscard]] const std::string& form_name() const noexcept;
    [[nodiscard]] const std::string& form_value() const noexcept;
    [[nodiscard]] bool required() const noexcept;
    [[nodiscard]] ControlValidationState validation_state() const noexcept;
    [[nodiscard]] const std::string& validation_message() const noexcept;
    [[nodiscard]] const std::string& accessibility_label() const noexcept;

  protected:
    void mark_control_state_dirty();

  private:
    std::string form_name_;
    std::string form_value_;
    std::string validation_message_;
    std::string accessibility_label_;
    ControlValidationState validation_state_ = ControlValidationState::None;
    bool required_ = false;
};

} // namespace winelement::controls
