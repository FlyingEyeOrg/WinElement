#include <winelement/elements/placement_engine.hpp>

#include <algorithm>
#include <cmath>

namespace winelement::elements {
namespace {

[[nodiscard]] float non_negative(float value) noexcept {
    return std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
}

[[nodiscard]] layout::Rect normalized_viewport(layout::Rect viewport, float margin) noexcept {
    viewport.width = non_negative(viewport.width);
    viewport.height = non_negative(viewport.height);
    const auto inset = std::max(margin, 0.0F);
    viewport.x += inset;
    viewport.y += inset;
    viewport.width = non_negative(viewport.width - inset * 2.0F);
    viewport.height = non_negative(viewport.height - inset * 2.0F);
    return viewport;
}

[[nodiscard]] bool has_viewport(layout::Rect viewport) noexcept {
    return viewport.width > 0.0F && viewport.height > 0.0F;
}

[[nodiscard]] PopupPlacement flipped_placement(PopupPlacement placement) noexcept {
    switch (placement) {
    case PopupPlacement::BottomStart:
        return PopupPlacement::TopStart;
    case PopupPlacement::BottomEnd:
        return PopupPlacement::TopEnd;
    case PopupPlacement::TopStart:
        return PopupPlacement::BottomStart;
    case PopupPlacement::TopEnd:
        return PopupPlacement::BottomEnd;
    case PopupPlacement::RightStart:
        return PopupPlacement::LeftStart;
    case PopupPlacement::RightEnd:
        return PopupPlacement::LeftEnd;
    case PopupPlacement::LeftStart:
        return PopupPlacement::RightStart;
    case PopupPlacement::LeftEnd:
        return PopupPlacement::RightEnd;
    }
    return PopupPlacement::BottomStart;
}

[[nodiscard]] layout::Rect bounds_for(PopupPlacement placement, layout::Rect anchor,
                                      layout::Size size, float gap) noexcept {
    const auto popup_width = non_negative(size.width);
    const auto popup_height = non_negative(size.height);
    switch (placement) {
    case PopupPlacement::BottomStart:
        return layout::Rect{anchor.x, layout::rect_bottom(anchor) + gap, popup_width, popup_height};
    case PopupPlacement::BottomEnd:
        return layout::Rect{layout::rect_right(anchor) - popup_width,
                            layout::rect_bottom(anchor) + gap, popup_width, popup_height};
    case PopupPlacement::TopStart:
        return layout::Rect{anchor.x, anchor.y - popup_height - gap, popup_width, popup_height};
    case PopupPlacement::TopEnd:
        return layout::Rect{layout::rect_right(anchor) - popup_width, anchor.y - popup_height - gap,
                            popup_width, popup_height};
    case PopupPlacement::RightStart:
        return layout::Rect{layout::rect_right(anchor) + gap, anchor.y, popup_width, popup_height};
    case PopupPlacement::RightEnd:
        return layout::Rect{layout::rect_right(anchor) + gap,
                            layout::rect_bottom(anchor) - popup_height, popup_width, popup_height};
    case PopupPlacement::LeftStart:
        return layout::Rect{anchor.x - popup_width - gap, anchor.y, popup_width, popup_height};
    case PopupPlacement::LeftEnd:
        return layout::Rect{anchor.x - popup_width - gap,
                            layout::rect_bottom(anchor) - popup_height, popup_width, popup_height};
    }
    return layout::Rect{anchor.x, layout::rect_bottom(anchor) + gap, popup_width, popup_height};
}

[[nodiscard]] float overflow_amount(layout::Rect bounds, layout::Rect viewport) noexcept {
    if (!has_viewport(viewport)) {
        return 0.0F;
    }

    const auto left = std::max(viewport.x - bounds.x, 0.0F);
    const auto top = std::max(viewport.y - bounds.y, 0.0F);
    const auto right = std::max(layout::rect_right(bounds) - layout::rect_right(viewport), 0.0F);
    const auto bottom = std::max(layout::rect_bottom(bounds) - layout::rect_bottom(viewport), 0.0F);
    return left + top + right + bottom;
}

[[nodiscard]] float clamp_axis(float value, float min_value, float max_value) noexcept {
    if (max_value < min_value) {
        return min_value;
    }
    return std::clamp(value, min_value, max_value);
}

[[nodiscard]] layout::Rect shift_into_viewport(layout::Rect bounds,
                                               layout::Rect viewport) noexcept {
    if (!has_viewport(viewport)) {
        return bounds;
    }

    bounds.x = clamp_axis(bounds.x, viewport.x, layout::rect_right(viewport) - bounds.width);
    bounds.y = clamp_axis(bounds.y, viewport.y, layout::rect_bottom(viewport) - bounds.height);
    return bounds;
}

} // namespace

PopupPlacementResult PlacementEngine::place(PopupPlacementOptions options) noexcept {
    options.gap = std::max(options.gap, 0.0F);
    if (options.match_anchor_width) {
        options.popup_size.width = std::max(options.popup_size.width, options.anchor_rect.width);
    }
    const auto viewport = normalized_viewport(options.viewport_rect, options.viewport_margin);
    auto placement = options.preferred_placement;
    auto bounds = bounds_for(placement, options.anchor_rect, options.popup_size, options.gap);
    auto flipped = false;

    if (options.allow_flip && has_viewport(viewport)) {
        const auto alternative_placement = flipped_placement(placement);
        const auto alternative_bounds =
            bounds_for(alternative_placement, options.anchor_rect, options.popup_size, options.gap);
        if (overflow_amount(alternative_bounds, viewport) < overflow_amount(bounds, viewport)) {
            placement = alternative_placement;
            bounds = alternative_bounds;
            flipped = true;
        }
    }

    const auto before_shift = bounds;
    if (options.allow_shift) {
        bounds = shift_into_viewport(bounds, viewport);
    }

    return PopupPlacementResult{.bounds = bounds,
                                .placement = placement,
                                .flipped = flipped,
                                .shifted =
                                    before_shift.x != bounds.x || before_shift.y != bounds.y};
}

} // namespace winelement::elements