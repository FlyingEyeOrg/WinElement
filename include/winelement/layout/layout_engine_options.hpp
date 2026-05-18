#pragma once

#include <winelement/layout/layout_types.hpp>

namespace winelement::layout {

struct LayoutEngineOptions {
    bool use_web_defaults = true;
    float point_scale_factor = 1.0F;
    Errata errata = Errata::None;
    bool web_flex_basis_enabled = false;
};

} // namespace winelement::layout
