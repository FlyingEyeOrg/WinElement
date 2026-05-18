#pragma once

#include <winelement/layout/layout_types.hpp>

namespace winelement::elements {

enum class PopupPlacement {
    BottomStart,
    BottomEnd,
    TopStart,
    TopEnd,
    RightStart,
    RightEnd,
    LeftStart,
    LeftEnd
};

struct PopupPlacementOptions {
    layout::Rect anchor_rect{};
    layout::Size popup_size{};
    layout::Rect viewport_rect{};
    PopupPlacement preferred_placement = PopupPlacement::BottomStart;
    float gap = 4.0F;
    float viewport_margin = 4.0F;
    bool allow_flip = true;
    bool allow_shift = true;
    bool match_anchor_width = false;
};

struct PopupPlacementResult {
    layout::Rect bounds{};
    PopupPlacement placement = PopupPlacement::BottomStart;
    bool flipped = false;
    bool shifted = false;
};

class PlacementEngine final {
  public:
    [[nodiscard]] static PopupPlacementResult place(PopupPlacementOptions options) noexcept;
};

} // namespace winelement::elements