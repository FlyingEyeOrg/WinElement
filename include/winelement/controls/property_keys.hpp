#pragma once

#include <winelement/controls/button.hpp>
#include <winelement/controls/input.hpp>
#include <winelement/controls/select.hpp>
#include <winelement/controls/switch.hpp>
#include <winelement/controls/text.hpp>
#include <winelement/core/property.hpp>

namespace winelement::controls::property_keys {

// ---- Button ----
inline const core::Property<ButtonType>& button_type() {
    static const auto p = core::make_property<ButtonType>("button.type", core::PropertyInvalidation::Paint);
    return p;
}

inline const core::Property<bool>& button_loading() {
    static const auto p = core::make_property<bool>("button.loading", core::PropertyInvalidation::Paint);
    return p;
}

inline const core::Property<bool>& button_plain() {
    static const auto p = core::make_property<bool>("button.plain", core::PropertyInvalidation::Paint);
    return p;
}

inline const core::Property<ButtonSize>& button_size() {
    static const auto p = core::make_property<ButtonSize>("button.size", core::PropertyInvalidation::Paint | core::PropertyInvalidation::Layout);
    return p;
}

// ---- Input ----
inline const core::Property<InputType>& input_type() {
    static const auto p = core::make_property<InputType>("input.type", core::PropertyInvalidation::Paint);
    return p;
}

inline const core::Property<InputStatus>& input_status() {
    static const auto p = core::make_property<InputStatus>("input.status", core::PropertyInvalidation::Paint);
    return p;
}

inline const core::Property<InputSize>& input_size() {
    static const auto p = core::make_property<InputSize>("input.size", core::PropertyInvalidation::Paint | core::PropertyInvalidation::Layout);
    return p;
}

inline const core::Property<std::string>& input_placeholder() {
    static const auto p = core::make_property<std::string>("input.placeholder", core::PropertyInvalidation::Paint);
    return p;
}

// ---- Text ----
inline const core::Property<TextType>& text_type() {
    static const auto p = core::make_property<TextType>("text.type", core::PropertyInvalidation::Paint);
    return p;
}

inline const core::Property<TextSize>& text_size() {
    static const auto p = core::make_property<TextSize>("text.size", core::PropertyInvalidation::Paint);
    return p;
}

inline const core::Property<bool>& text_truncated() {
    static const auto p = core::make_property<bool>("text.truncated", core::PropertyInvalidation::Paint | core::PropertyInvalidation::Layout);
    return p;
}

inline const core::Property<std::size_t>& text_max_lines() {
    static const auto p = core::make_property<std::size_t>("text.max_lines", core::PropertyInvalidation::Paint | core::PropertyInvalidation::Layout);
    return p;
}

inline const core::Property<bool>& text_selectable() {
    static const auto p = core::make_property<bool>("text.selectable", core::PropertyInvalidation::Paint);
    return p;
}

// ---- Select ----
inline const core::Property<bool>& select_multiple() {
    static const auto p = core::make_property<bool>("select.multiple", core::PropertyInvalidation::Paint | core::PropertyInvalidation::Layout);
    return p;
}

inline const core::Property<SelectSize>& select_size() {
    static const auto p = core::make_property<SelectSize>("select.size", core::PropertyInvalidation::Paint | core::PropertyInvalidation::Layout);
    return p;
}

// ---- Switch ----
inline const core::Property<bool>& switch_checked() {
    static const auto p = core::make_property<bool>("switch.checked", core::PropertyInvalidation::Paint);
    return p;
}

inline const core::Property<bool>& switch_loading() {
    static const auto p = core::make_property<bool>("switch.loading", core::PropertyInvalidation::Paint);
    return p;
}

inline const core::Property<SwitchSize>& switch_size() {
    static const auto p = core::make_property<SwitchSize>("switch.size", core::PropertyInvalidation::Paint | core::PropertyInvalidation::Layout);
    return p;
}

}  // namespace winelement::controls::property_keys
