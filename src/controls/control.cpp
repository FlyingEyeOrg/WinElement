#include <winelement/controls/control.hpp>

#include <utility>

namespace winelement::controls {

Control::Control() = default;

Control::~Control() = default;

Control& Control::set_form_name(std::string_view name) {
    if (form_name_ == name) {
        return *this;
    }
    form_name_ = std::string(name);
    mark_control_state_dirty();
    return *this;
}

Control& Control::set_form_value(std::string_view value) {
    if (form_value_ == value) {
        return *this;
    }
    form_value_ = std::string(value);
    mark_control_state_dirty();
    return *this;
}

Control& Control::set_required(bool required) noexcept {
    if (required_ == required) {
        return *this;
    }
    required_ = required;
    mark_control_state_dirty();
    return *this;
}

Control& Control::set_validation_state(ControlValidationState state) {
    if (validation_state_ == state) {
        return *this;
    }
    validation_state_ = state;
    mark_control_state_dirty();
    return *this;
}

Control& Control::set_validation_message(std::string_view message) {
    if (validation_message_ == message) {
        return *this;
    }
    validation_message_ = std::string(message);
    mark_control_state_dirty();
    return *this;
}

Control& Control::set_accessibility_label(std::string_view label) {
    if (accessibility_label_ == label) {
        return *this;
    }
    accessibility_label_ = std::string(label);
    set_semantics_label(accessibility_label_);
    mark_control_state_dirty();
    return *this;
}

const std::string& Control::form_name() const noexcept {
    return form_name_;
}

const std::string& Control::form_value() const noexcept {
    return form_value_;
}

bool Control::required() const noexcept {
    return required_;
}

ControlValidationState Control::validation_state() const noexcept {
    return validation_state_;
}

const std::string& Control::validation_message() const noexcept {
    return validation_message_;
}

const std::string& Control::accessibility_label() const noexcept {
    return accessibility_label_;
}

void Control::mark_control_state_dirty() {
    invalidate_paint();
}

} // namespace winelement::controls
