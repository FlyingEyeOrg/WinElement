#include <winelement/layout/pixel_snap.hpp>

#include <cmath>

namespace winelement::layout {

float snap_to_pixel(float value, float point_scale_factor) noexcept {
    if (!std::isfinite(value) || !std::isfinite(point_scale_factor) || point_scale_factor <= 0.0F) {
        return value;
    }

    return std::round(value * point_scale_factor) / point_scale_factor;
}

Rect snap_rect_to_pixels(Rect rect, float point_scale_factor) noexcept {
    const float left = snap_to_pixel(rect.x, point_scale_factor);
    const float top = snap_to_pixel(rect.y, point_scale_factor);
    const float right = snap_to_pixel(rect.x + rect.width, point_scale_factor);
    const float bottom = snap_to_pixel(rect.y + rect.height, point_scale_factor);

    return {
        left,
        top,
        right - left,
        bottom - top,
    };
}

} // namespace winelement::layout
