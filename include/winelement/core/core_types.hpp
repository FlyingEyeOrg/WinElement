#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace winelement::core {

struct Size {
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] friend constexpr bool operator==(Size, Size) noexcept = default;
};

struct Point {
    float x = 0.0F;
    float y = 0.0F;

    [[nodiscard]] friend constexpr bool operator==(Point, Point) noexcept = default;
};

struct Rect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] friend constexpr bool operator==(Rect, Rect) noexcept = default;
};

[[nodiscard]] inline bool is_finite_rect(Rect rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
           std::isfinite(rect.height);
}

[[nodiscard]] inline bool is_visible_rect(Rect rect) noexcept {
    return is_finite_rect(rect) && rect.width > 0.0F && rect.height > 0.0F;
}

[[nodiscard]] constexpr float rect_right(Rect rect) noexcept {
    return rect.x + rect.width;
}

[[nodiscard]] constexpr float rect_bottom(Rect rect) noexcept {
    return rect.y + rect.height;
}

[[nodiscard]] inline bool rects_intersect(Rect left, Rect right) noexcept {
    return is_visible_rect(left) && is_visible_rect(right) && left.x < rect_right(right) &&
           right.x < rect_right(left) && left.y < rect_bottom(right) && right.y < rect_bottom(left);
}

[[nodiscard]] inline bool rects_touch_or_intersect(Rect left, Rect right) noexcept {
    return is_visible_rect(left) && is_visible_rect(right) && left.x <= rect_right(right) &&
           right.x <= rect_right(left) && left.y <= rect_bottom(right) &&
           right.y <= rect_bottom(left);
}

[[nodiscard]] inline Rect union_rects(Rect left, Rect right) noexcept {
    if (!is_visible_rect(left)) {
        return is_visible_rect(right) ? right : Rect{};
    }
    if (!is_visible_rect(right)) {
        return left;
    }

    const auto x = std::min(left.x, right.x);
    const auto y = std::min(left.y, right.y);
    const auto right_edge = std::max(rect_right(left), rect_right(right));
    const auto bottom_edge = std::max(rect_bottom(left), rect_bottom(right));
    return Rect{x, y, right_edge - x, bottom_edge - y};
}

[[nodiscard]] inline Rect intersect_rects(Rect left, Rect right) noexcept {
    if (!is_visible_rect(left) || !is_visible_rect(right)) {
        return {};
    }

    const auto x = std::max(left.x, right.x);
    const auto y = std::max(left.y, right.y);
    const auto right_edge = std::min(rect_right(left), rect_right(right));
    const auto bottom_edge = std::min(rect_bottom(left), rect_bottom(right));
    if (right_edge <= x || bottom_edge <= y) {
        return {};
    }
    return Rect{x, y, right_edge - x, bottom_edge - y};
}

[[nodiscard]] constexpr Rect offset_rect(Rect rect, Point offset) noexcept {
    return Rect{rect.x + offset.x, rect.y + offset.y, rect.width, rect.height};
}

[[nodiscard]] constexpr Rect offset_rect(Rect rect, Rect parent_absolute_frame) noexcept {
    return offset_rect(rect, Point{parent_absolute_frame.x, parent_absolute_frame.y});
}

[[nodiscard]] constexpr Rect inflate_rect(Rect rect, float amount) noexcept {
    return Rect{rect.x - amount, rect.y - amount, rect.width + amount * 2.0F,
                rect.height + amount * 2.0F};
}

[[nodiscard]] constexpr bool rect_contains_point(Rect rect, Point point) noexcept {
    return rect.width > 0.0F && rect.height > 0.0F && point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

struct EdgeInsets {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;

    [[nodiscard]] friend constexpr bool operator==(EdgeInsets, EdgeInsets) noexcept = default;
};

struct Color {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;

    [[nodiscard]] static constexpr Color rgba(std::uint8_t red_value, std::uint8_t green_value,
                                              std::uint8_t blue_value,
                                              std::uint8_t alpha_value = 255) noexcept {
        return Color{red_value, green_value, blue_value, alpha_value};
    }

    [[nodiscard]] friend constexpr bool operator==(Color, Color) noexcept = default;
};

struct Transform2D {
    float m11 = 1.0F;
    float m12 = 0.0F;
    float m21 = 0.0F;
    float m22 = 1.0F;
    float dx = 0.0F;
    float dy = 0.0F;

    [[nodiscard]] static constexpr Transform2D identity() noexcept {
        return Transform2D{};
    }

    [[nodiscard]] static constexpr Transform2D translation(float x, float y) noexcept {
        return Transform2D{.dx = x, .dy = y};
    }

    [[nodiscard]] friend constexpr bool operator==(Transform2D, Transform2D) noexcept = default;
};

struct CornerRadius {
    float x = 0.0F;
    float y = 0.0F;

    [[nodiscard]] static constexpr CornerRadius uniform(float value) noexcept {
        return CornerRadius{value, value};
    }

    [[nodiscard]] friend constexpr bool operator==(CornerRadius, CornerRadius) noexcept = default;
};

[[nodiscard]] constexpr bool is_identity_transform(Transform2D transform) noexcept {
    return transform.m11 == 1.0F && transform.m12 == 0.0F && transform.m21 == 0.0F &&
           transform.m22 == 1.0F && transform.dx == 0.0F && transform.dy == 0.0F;
}

[[nodiscard]] constexpr Point transform_point(Point point, Transform2D transform) noexcept {
    return Point{point.x * transform.m11 + point.y * transform.m21 + transform.dx,
                 point.x * transform.m12 + point.y * transform.m22 + transform.dy};
}

[[nodiscard]] constexpr Transform2D multiply_transforms(Transform2D first,
                                                        Transform2D second) noexcept {
    return Transform2D{.m11 = first.m11 * second.m11 + first.m12 * second.m21,
                       .m12 = first.m11 * second.m12 + first.m12 * second.m22,
                       .m21 = first.m21 * second.m11 + first.m22 * second.m21,
                       .m22 = first.m21 * second.m12 + first.m22 * second.m22,
                       .dx = first.dx * second.m11 + first.dy * second.m21 + second.dx,
                       .dy = first.dx * second.m12 + first.dy * second.m22 + second.dy};
}

[[nodiscard]] constexpr Transform2D transform_around_point(Transform2D transform,
                                                           Point origin) noexcept {
    return multiply_transforms(
        multiply_transforms(Transform2D::translation(-origin.x, -origin.y), transform),
        Transform2D::translation(origin.x, origin.y));
}

[[nodiscard]] inline Rect transform_rect(Rect rect, Transform2D transform) noexcept {
    const auto top_left = transform_point(Point{rect.x, rect.y}, transform);
    const auto top_right = transform_point(Point{rect.x + rect.width, rect.y}, transform);
    const auto bottom_left = transform_point(Point{rect.x, rect.y + rect.height}, transform);
    const auto bottom_right =
        transform_point(Point{rect.x + rect.width, rect.y + rect.height}, transform);
    const auto left = std::min({top_left.x, top_right.x, bottom_left.x, bottom_right.x});
    const auto top = std::min({top_left.y, top_right.y, bottom_left.y, bottom_right.y});
    const auto right = std::max({top_left.x, top_right.x, bottom_left.x, bottom_right.x});
    const auto bottom = std::max({top_left.y, top_right.y, bottom_left.y, bottom_right.y});
    return Rect{left, top, right - left, bottom - top};
}

} // namespace winelement::core
