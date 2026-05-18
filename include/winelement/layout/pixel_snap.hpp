#pragma once

#include <winelement/layout/layout_types.hpp>

namespace winelement::layout {

[[nodiscard]] float snap_to_pixel(float value, float point_scale_factor) noexcept;
[[nodiscard]] Rect snap_rect_to_pixels(Rect rect, float point_scale_factor) noexcept;

} // namespace winelement::layout
