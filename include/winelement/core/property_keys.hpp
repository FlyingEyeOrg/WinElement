#pragma once

#include <winelement/core/property.hpp>

#include <string>

namespace winelement::core::property_keys {

inline const Property<std::string>& text() {
    static const auto p = make_property<std::string>("text", PropertyInvalidation::Paint);
    return p;
}

inline const Property<float>& opacity() {
    static const auto p = make_property<float>("opacity", PropertyInvalidation::Paint);
    return p;
}

inline const Property<bool>& visible() {
    static const auto p = make_property<bool>("visible", PropertyInvalidation::Layout | PropertyInvalidation::Paint);
    return p;
}

inline const Property<bool>& disabled() {
    static const auto p = make_property<bool>("disabled", PropertyInvalidation::Paint);
    return p;
}

inline const Property<bool>& hit_test_visible() {
    static const auto p = make_property<bool>("hit_test_visible", PropertyInvalidation::None);
    return p;
}

}  // namespace winelement::core::property_keys
