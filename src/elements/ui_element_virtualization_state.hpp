#pragma once

#include <winelement/elements/ui_element.hpp>

#include <cstddef>
#include <vector>

namespace winelement::elements {

struct UIElement::VirtualChildrenState {
    struct RealizedChild {
        std::size_t index = 0U;
        UIElement* element = nullptr;
    };

    VirtualChildrenOptions options;
    std::vector<RealizedChild> realized;
    UIElement* leading_spacer = nullptr;
    UIElement* trailing_spacer = nullptr;
    std::size_t realized_start = 0U;
    std::size_t realized_count = 0U;
    bool updating : 1 = false;
};

} // namespace winelement::elements
