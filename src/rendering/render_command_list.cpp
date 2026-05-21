#include <winelement/rendering/render_command_list.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <dwrite.h>
#include <wrl/client.h>
#ifdef DrawText
#undef DrawText
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace winelement::rendering {
namespace {

constexpr auto max_dirty_rects = 32U;
constexpr auto pi = 3.14159265358979323846F;
constexpr auto two_pi = pi * 2.0F;
constexpr auto root_epsilon = 0.00001F;
constexpr auto geometry_epsilon = 0.0001F;
constexpr auto contour_cleanup_epsilon = 0.001F;
constexpr auto geometry_flattening_tolerance = 0.01F;
constexpr auto max_curve_flattening_depth = 12U;
constexpr auto max_prepared_geometry_cache_entries = 384U;
constexpr auto max_prepared_text_glyph_cache_entries = 256U;

template <typename T> void hash_combine(std::size_t& seed, const T& value) noexcept;

template <typename Cache> [[nodiscard]] std::size_t cache_entry_count(const Cache& cache) {
    auto count = std::size_t{};
    for (const auto& [_, entries] : cache) {
        count += entries.size();
    }
    return count;
}

template <typename Cache> void trim_cache_entries(Cache& cache, std::size_t max_entries) {
    if (max_entries == 0U) {
        cache.clear();
        return;
    }

    auto count = cache_entry_count(cache);
    while (count > max_entries && !cache.empty()) {
        count -= cache.begin()->second.size();
        cache.erase(cache.begin());
    }
}

class BoundsBuilder final {
  public:
    void add(layout::Point point) noexcept {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return;
        }

        if (!has_bounds_) {
            left_ = point.x;
            top_ = point.y;
            right_ = point.x;
            bottom_ = point.y;
            has_bounds_ = true;
            return;
        }

        left_ = std::min(left_, point.x);
        top_ = std::min(top_, point.y);
        right_ = std::max(right_, point.x);
        bottom_ = std::max(bottom_, point.y);
    }

    void add(layout::Rect rect) noexcept {
        if (!layout::is_visible_rect(rect)) {
            return;
        }

        add(layout::Point{rect.x, rect.y});
        add(layout::Point{rect.x + rect.width, rect.y + rect.height});
    }

    [[nodiscard]] layout::Rect rect() const noexcept {
        if (!has_bounds_) {
            return {};
        }

        return layout::Rect{left_, top_, right_ - left_, bottom_ - top_};
    }

  private:
    bool has_bounds_ = false;
    float left_ = 0.0F;
    float top_ = 0.0F;
    float right_ = 0.0F;
    float bottom_ = 0.0F;
};

[[nodiscard]] layout::Rect line_bounds(const DrawLineCommand& command) noexcept {
    const auto x = std::min(command.start.x, command.end.x);
    const auto y = std::min(command.start.y, command.end.y);
    const auto right = std::max(command.start.x, command.end.x);
    const auto bottom = std::max(command.start.y, command.end.y);
    return layout::inflate_rect(layout::Rect{x, y, right - x, bottom - y},
                                std::max(command.stroke_width, 0.0F) * 0.5F);
}

[[nodiscard]] layout::Rect text_layout_bounds(const DrawTextLayoutCommand& command) noexcept {
    const auto* layout = command.layout_value();
    if (layout == nullptr) {
        return {};
    }
    return layout::Rect{command.origin.x, command.origin.y, layout->size.width,
                        layout->size.height};
}

[[nodiscard]] RenderBatchKind batch_kind_for(RenderCommandType type) noexcept {
    switch (type) {
    case RenderCommandType::DrawText:
    case RenderCommandType::DrawTextLayout:
        return RenderBatchKind::Text;
    case RenderCommandType::DrawImage:
        return RenderBatchKind::Image;
    case RenderCommandType::DrawBoxShadow:
        return RenderBatchKind::Effect;
    case RenderCommandType::DrawLine:
    case RenderCommandType::FillRect:
    case RenderCommandType::FillPixelSnappedRect:
    case RenderCommandType::StrokePixelSnappedRect:
    case RenderCommandType::StrokeRect:
    case RenderCommandType::FillRoundedRect:
    case RenderCommandType::StrokeRoundedRect:
    case RenderCommandType::FillEllipse:
    case RenderCommandType::StrokeEllipse:
    case RenderCommandType::FillGeometry:
    case RenderCommandType::StrokeGeometry:
        return RenderBatchKind::Geometry;
    case RenderCommandType::Save:
    case RenderCommandType::Restore:
    case RenderCommandType::PushClip:
    case RenderCommandType::PopClip:
    case RenderCommandType::PushGeometryClip:
    case RenderCommandType::PopGeometryClip:
    case RenderCommandType::PushLayer:
    case RenderCommandType::PopLayer:
    default:
        return RenderBatchKind::State;
    }
}

void append_bytes(std::vector<std::byte>& bytes, const void* data, std::size_t size) {
    const auto* first = static_cast<const std::byte*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

void append_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
    append_bytes(bytes, &value, sizeof(value));
}

void append_float(std::vector<std::byte>& bytes, float value) {
    append_bytes(bytes, &value, sizeof(value));
}

[[nodiscard]] bool is_curve_parameter(float value) noexcept {
    return std::isfinite(value) && value > root_epsilon && value < 1.0F - root_epsilon;
}

[[nodiscard]] float lerp(float start, float end, float amount) noexcept {
    return start + (end - start) * amount;
}

[[nodiscard]] layout::Point evaluate_quadratic(layout::Point start, layout::Point control,
                                               layout::Point end, float amount) noexcept {
    const auto first =
        layout::Point{lerp(start.x, control.x, amount), lerp(start.y, control.y, amount)};
    const auto second =
        layout::Point{lerp(control.x, end.x, amount), lerp(control.y, end.y, amount)};
    return layout::Point{lerp(first.x, second.x, amount), lerp(first.y, second.y, amount)};
}

void add_quadratic_extrema(BoundsBuilder& bounds, layout::Point start, layout::Point control,
                           layout::Point end) noexcept {
    const auto add_axis_root = [&](float start_axis, float control_axis, float end_axis) {
        const auto denominator = start_axis - control_axis * 2.0F + end_axis;
        if (std::abs(denominator) <= root_epsilon) {
            return;
        }

        const auto amount = (start_axis - control_axis) / denominator;
        if (is_curve_parameter(amount)) {
            bounds.add(evaluate_quadratic(start, control, end, amount));
        }
    };

    add_axis_root(start.x, control.x, end.x);
    add_axis_root(start.y, control.y, end.y);
}

[[nodiscard]] layout::Point evaluate_cubic(layout::Point start, layout::Point control1,
                                           layout::Point control2, layout::Point end,
                                           float amount) noexcept {
    const auto first =
        layout::Point{lerp(start.x, control1.x, amount), lerp(start.y, control1.y, amount)};
    const auto second =
        layout::Point{lerp(control1.x, control2.x, amount), lerp(control1.y, control2.y, amount)};
    const auto third =
        layout::Point{lerp(control2.x, end.x, amount), lerp(control2.y, end.y, amount)};
    const auto fourth =
        layout::Point{lerp(first.x, second.x, amount), lerp(first.y, second.y, amount)};
    const auto fifth =
        layout::Point{lerp(second.x, third.x, amount), lerp(second.y, third.y, amount)};
    return layout::Point{lerp(fourth.x, fifth.x, amount), lerp(fourth.y, fifth.y, amount)};
}

void add_cubic_extrema(BoundsBuilder& bounds, layout::Point start, layout::Point control1,
                       layout::Point control2, layout::Point end) noexcept {
    const auto add_root = [&](float amount) {
        if (is_curve_parameter(amount)) {
            bounds.add(evaluate_cubic(start, control1, control2, end, amount));
        }
    };
    const auto add_axis_roots = [&](float start_axis, float control1_axis, float control2_axis,
                                    float end_axis) {
        const auto a = -start_axis + control1_axis * 3.0F - control2_axis * 3.0F + end_axis;
        const auto b = (start_axis - control1_axis * 2.0F + control2_axis) * 2.0F;
        const auto c = -start_axis + control1_axis;

        if (std::abs(a) <= root_epsilon) {
            if (std::abs(b) > root_epsilon) {
                add_root(-c / b);
            }
            return;
        }

        const auto discriminant = b * b - 4.0F * a * c;
        if (discriminant < 0.0F) {
            return;
        }

        const auto root = std::sqrt(discriminant);
        add_root((-b + root) / (2.0F * a));
        add_root((-b - root) / (2.0F * a));
    };

    add_axis_roots(start.x, control1.x, control2.x, end.x);
    add_axis_roots(start.y, control1.y, control2.y, end.y);
}

[[nodiscard]] float degrees_to_radians(float degrees) noexcept {
    return degrees * pi / 180.0F;
}

[[nodiscard]] float normalize_angle(float angle) noexcept {
    auto result = std::fmod(angle, two_pi);
    if (result < 0.0F) {
        result += two_pi;
    }
    return result;
}

[[nodiscard]] bool angle_on_arc(float angle, float start_angle, float sweep_angle) noexcept {
    if (sweep_angle >= 0.0F) {
        return normalize_angle(angle - start_angle) <= sweep_angle + root_epsilon;
    }

    return normalize_angle(start_angle - angle) <= -sweep_angle + root_epsilon;
}

[[nodiscard]] layout::Point evaluate_arc_point(layout::Point center, float radius_x, float radius_y,
                                               float rotation, float angle) noexcept {
    const auto cos_rotation = std::cos(rotation);
    const auto sin_rotation = std::sin(rotation);
    const auto cos_angle = std::cos(angle);
    const auto sin_angle = std::sin(angle);
    return layout::Point{
        center.x + radius_x * cos_angle * cos_rotation - radius_y * sin_angle * sin_rotation,
        center.y + radius_x * cos_angle * sin_rotation + radius_y * sin_angle * cos_rotation};
}

void add_arc_bounds(BoundsBuilder& bounds, layout::Point start,
                    const GeometrySegment& segment) noexcept {
    bounds.add(start);
    bounds.add(segment.point);

    auto radius_x = std::abs(segment.radius.width);
    auto radius_y = std::abs(segment.radius.height);
    if (radius_x <= root_epsilon || radius_y <= root_epsilon ||
        (std::abs(start.x - segment.point.x) <= root_epsilon &&
         std::abs(start.y - segment.point.y) <= root_epsilon)) {
        return;
    }

    const auto rotation = degrees_to_radians(segment.rotation_angle);
    const auto cos_rotation = std::cos(rotation);
    const auto sin_rotation = std::sin(rotation);
    const auto half_delta_x = (start.x - segment.point.x) * 0.5F;
    const auto half_delta_y = (start.y - segment.point.y) * 0.5F;
    const auto transformed_start_x = cos_rotation * half_delta_x + sin_rotation * half_delta_y;
    const auto transformed_start_y = -sin_rotation * half_delta_x + cos_rotation * half_delta_y;

    const auto radius_x_squared = radius_x * radius_x;
    const auto radius_y_squared = radius_y * radius_y;
    const auto transformed_start_x_squared = transformed_start_x * transformed_start_x;
    const auto transformed_start_y_squared = transformed_start_y * transformed_start_y;
    const auto radius_scale = transformed_start_x_squared / radius_x_squared +
                              transformed_start_y_squared / radius_y_squared;
    if (radius_scale > 1.0F) {
        const auto scale = std::sqrt(radius_scale);
        radius_x *= scale;
        radius_y *= scale;
    }

    const auto scaled_radius_x_squared = radius_x * radius_x;
    const auto scaled_radius_y_squared = radius_y * radius_y;
    const auto denominator = scaled_radius_x_squared * transformed_start_y_squared +
                             scaled_radius_y_squared * transformed_start_x_squared;
    if (denominator <= root_epsilon) {
        return;
    }

    const auto numerator = scaled_radius_x_squared * scaled_radius_y_squared - denominator;
    auto factor = std::sqrt(std::max(0.0F, numerator / denominator));
    const auto large_arc = segment.arc_size == GeometryArcSize::Large;
    const auto clockwise = segment.sweep_direction == GeometryArcSweepDirection::Clockwise;
    if (large_arc == clockwise) {
        factor = -factor;
    }

    const auto center_x_prime = factor * radius_x * transformed_start_y / radius_y;
    const auto center_y_prime = -factor * radius_y * transformed_start_x / radius_x;
    const auto center =
        layout::Point{cos_rotation * center_x_prime - sin_rotation * center_y_prime +
                          (start.x + segment.point.x) * 0.5F,
                      sin_rotation * center_x_prime + cos_rotation * center_y_prime +
                          (start.y + segment.point.y) * 0.5F};

    const auto start_angle = std::atan2((transformed_start_y - center_y_prime) / radius_y,
                                        (transformed_start_x - center_x_prime) / radius_x);
    const auto end_angle = std::atan2((-transformed_start_y - center_y_prime) / radius_y,
                                      (-transformed_start_x - center_x_prime) / radius_x);
    auto sweep_angle = end_angle - start_angle;
    if (clockwise && sweep_angle < 0.0F) {
        sweep_angle += two_pi;
    } else if (!clockwise && sweep_angle > 0.0F) {
        sweep_angle -= two_pi;
    }

    const auto x_extrema = std::atan2(-radius_y * sin_rotation, radius_x * cos_rotation);
    const auto y_extrema = std::atan2(radius_y * cos_rotation, radius_x * sin_rotation);
    const std::array<float, 4U> candidate_angles{x_extrema, x_extrema + pi, y_extrema,
                                                 y_extrema + pi};
    for (const auto angle : candidate_angles) {
        if (angle_on_arc(angle, start_angle, sweep_angle)) {
            bounds.add(evaluate_arc_point(center, radius_x, radius_y, rotation, angle));
        }
    }
}

[[nodiscard]] layout::Rect geometry_bounds(const Geometry& geometry) noexcept {
    BoundsBuilder bounds;
    for (const auto& figure : geometry.figures) {
        auto current = figure.start;
        bounds.add(current);
        for (const auto& segment : figure.segments) {
            switch (segment.type) {
            case GeometrySegmentType::Line:
                bounds.add(segment.point);
                break;
            case GeometrySegmentType::QuadraticBezier:
                add_quadratic_extrema(bounds, current, segment.control_point1, segment.point);
                bounds.add(segment.point);
                break;
            case GeometrySegmentType::CubicBezier:
                add_cubic_extrema(bounds, current, segment.control_point1, segment.control_point2,
                                  segment.point);
                bounds.add(segment.point);
                break;
            case GeometrySegmentType::Arc:
                add_arc_bounds(bounds, current, segment);
                break;
            }
            current = segment.point;
        }
    }

    return bounds.rect();
}

[[nodiscard]] layout::Point point_lerp(layout::Point start, layout::Point end,
                                       float amount) noexcept {
    return layout::Point{lerp(start.x, end.x, amount), lerp(start.y, end.y, amount)};
}

[[nodiscard]] layout::Point point_add(layout::Point left, layout::Point right) noexcept {
    return layout::Point{left.x + right.x, left.y + right.y};
}

[[nodiscard]] layout::Point point_subtract(layout::Point left, layout::Point right) noexcept {
    return layout::Point{left.x - right.x, left.y - right.y};
}

[[nodiscard]] layout::Point point_scale(layout::Point point, float scale) noexcept {
    return layout::Point{point.x * scale, point.y * scale};
}

[[nodiscard]] float vector_cross(layout::Point left, layout::Point right) noexcept {
    return left.x * right.y - left.y * right.x;
}

[[nodiscard]] float vector_dot(layout::Point left, layout::Point right) noexcept {
    return left.x * right.x + left.y * right.y;
}

[[nodiscard]] float vector_length(layout::Point vector) noexcept {
    return std::sqrt(vector.x * vector.x + vector.y * vector.y);
}

[[nodiscard]] float point_distance(layout::Point left, layout::Point right) noexcept {
    return vector_length(point_subtract(right, left));
}

[[nodiscard]] float point_line_distance(layout::Point point, layout::Point line_start,
                                        layout::Point line_end) noexcept {
    const auto line = point_subtract(line_end, line_start);
    const auto length = vector_length(line);
    if (length <= geometry_epsilon) {
        return point_distance(point, line_start);
    }
    return std::abs(vector_cross(point_subtract(point, line_start), line)) / length;
}

[[nodiscard]] bool nearly_same_point(layout::Point left, layout::Point right) noexcept {
    return point_distance(left, right) <= contour_cleanup_epsilon;
}

[[nodiscard]] bool is_redundant_collinear_point(layout::Point previous, layout::Point current,
                                                layout::Point next) noexcept {
    if (nearly_same_point(previous, current) || nearly_same_point(current, next)) {
        return true;
    }
    const auto before = point_subtract(current, previous);
    const auto after = point_subtract(next, current);
    if (vector_dot(before, after) < -contour_cleanup_epsilon) {
        return false;
    }
    return point_line_distance(current, previous, next) <= contour_cleanup_epsilon;
}

[[nodiscard]] std::vector<layout::Point> clean_closed_contour(std::vector<layout::Point> points) {
    if (points.size() > 1U && nearly_same_point(points.front(), points.back())) {
        points.pop_back();
    }
    if (points.size() < 3U) {
        return {};
    }

    auto write_index = std::size_t{0U};
    for (const auto point : points) {
        if (write_index > 0U && nearly_same_point(points[write_index - 1U], point)) {
            continue;
        }
        points[write_index++] = point;
    }
    points.resize(write_index);
    if (points.size() > 1U && nearly_same_point(points.front(), points.back())) {
        points.pop_back();
    }
    if (points.size() < 3U) {
        return {};
    }

    std::vector<std::uint8_t> keep(points.size(), 1U);
    auto remaining = points.size();
    const auto previous_kept = [&](std::size_t index) noexcept {
        auto cursor = index;
        do {
            cursor = cursor == 0U ? points.size() - 1U : cursor - 1U;
        } while (cursor != index && keep[cursor] == 0U);
        return cursor;
    };
    const auto next_kept = [&](std::size_t index) noexcept {
        auto cursor = index;
        do {
            cursor = (cursor + 1U) % points.size();
        } while (cursor != index && keep[cursor] == 0U);
        return cursor;
    };

    auto changed = true;
    while (changed && remaining >= 3U) {
        changed = false;
        for (std::size_t index = 0U; index < points.size() && remaining >= 3U; ++index) {
            if (keep[index] == 0U) {
                continue;
            }
            const auto previous_index = previous_kept(index);
            const auto next_index = next_kept(index);
            if (previous_index == index || next_index == index || previous_index == next_index) {
                continue;
            }
            const auto previous = points[previous_index];
            const auto current = points[index];
            const auto next = points[next_index];
            if (is_redundant_collinear_point(previous, current, next)) {
                keep[index] = 0U;
                --remaining;
                changed = true;
            }
        }
    }

    if (remaining < 3U) {
        return {};
    }

    std::vector<layout::Point> compacted;
    compacted.reserve(remaining);
    for (std::size_t index = 0U; index < points.size(); ++index) {
        if (keep[index] != 0U) {
            compacted.push_back(points[index]);
        }
    }
    return compacted;
}

struct PreparedArcParameters {
    layout::Point center{};
    float radius_x = 0.0F;
    float radius_y = 0.0F;
    float rotation = 0.0F;
    float start_angle = 0.0F;
    float sweep_angle = 0.0F;
};

[[nodiscard]] std::optional<PreparedArcParameters>
resolve_prepared_arc(layout::Point start, const GeometrySegment& segment) noexcept {
    auto radius_x = std::abs(segment.radius.width);
    auto radius_y = std::abs(segment.radius.height);
    if (radius_x <= geometry_epsilon || radius_y <= geometry_epsilon ||
        point_distance(start, segment.point) <= geometry_epsilon) {
        return std::nullopt;
    }

    const auto rotation = degrees_to_radians(segment.rotation_angle);
    const auto cos_rotation = std::cos(rotation);
    const auto sin_rotation = std::sin(rotation);
    const auto half_delta_x = (start.x - segment.point.x) * 0.5F;
    const auto half_delta_y = (start.y - segment.point.y) * 0.5F;
    const auto transformed_start_x = cos_rotation * half_delta_x + sin_rotation * half_delta_y;
    const auto transformed_start_y = -sin_rotation * half_delta_x + cos_rotation * half_delta_y;

    auto radius_x_squared = radius_x * radius_x;
    auto radius_y_squared = radius_y * radius_y;
    const auto transformed_start_x_squared = transformed_start_x * transformed_start_x;
    const auto transformed_start_y_squared = transformed_start_y * transformed_start_y;
    const auto radius_scale = transformed_start_x_squared / radius_x_squared +
                              transformed_start_y_squared / radius_y_squared;
    if (radius_scale > 1.0F) {
        const auto scale = std::sqrt(radius_scale);
        radius_x *= scale;
        radius_y *= scale;
        radius_x_squared = radius_x * radius_x;
        radius_y_squared = radius_y * radius_y;
    }

    const auto denominator = radius_x_squared * transformed_start_y_squared +
                             radius_y_squared * transformed_start_x_squared;
    if (denominator <= geometry_epsilon) {
        return std::nullopt;
    }

    const auto numerator = radius_x_squared * radius_y_squared - denominator;
    auto factor = std::sqrt(std::max(0.0F, numerator / denominator));
    const auto large_arc = segment.arc_size == GeometryArcSize::Large;
    const auto clockwise = segment.sweep_direction == GeometryArcSweepDirection::Clockwise;
    if (large_arc == clockwise) {
        factor = -factor;
    }

    const auto center_x_prime = factor * radius_x * transformed_start_y / radius_y;
    const auto center_y_prime = -factor * radius_y * transformed_start_x / radius_x;
    const auto center =
        layout::Point{cos_rotation * center_x_prime - sin_rotation * center_y_prime +
                          (start.x + segment.point.x) * 0.5F,
                      sin_rotation * center_x_prime + cos_rotation * center_y_prime +
                          (start.y + segment.point.y) * 0.5F};

    const auto start_angle = std::atan2((transformed_start_y - center_y_prime) / radius_y,
                                        (transformed_start_x - center_x_prime) / radius_x);
    const auto end_angle = std::atan2((-transformed_start_y - center_y_prime) / radius_y,
                                      (-transformed_start_x - center_x_prime) / radius_x);
    auto sweep_angle = end_angle - start_angle;
    if (clockwise && sweep_angle < 0.0F) {
        sweep_angle += two_pi;
    } else if (!clockwise && sweep_angle > 0.0F) {
        sweep_angle -= two_pi;
    }

    return PreparedArcParameters{.center = center,
                                 .radius_x = radius_x,
                                 .radius_y = radius_y,
                                 .rotation = rotation,
                                 .start_angle = start_angle,
                                 .sweep_angle = sweep_angle};
}

[[nodiscard]] layout::Point evaluate_prepared_arc_point(const PreparedArcParameters& arc,
                                                        float angle) noexcept {
    const auto cos_rotation = std::cos(arc.rotation);
    const auto sin_rotation = std::sin(arc.rotation);
    const auto cos_angle = std::cos(angle);
    const auto sin_angle = std::sin(angle);
    return layout::Point{arc.center.x + arc.radius_x * cos_angle * cos_rotation -
                             arc.radius_y * sin_angle * sin_rotation,
                         arc.center.y + arc.radius_x * cos_angle * sin_rotation +
                             arc.radius_y * sin_angle * cos_rotation};
}

[[nodiscard]] std::uint32_t adaptive_curve_segments(float length, std::uint32_t minimum,
                                                    std::uint32_t maximum) noexcept {
    return std::clamp(static_cast<std::uint32_t>(std::ceil(std::max(length, 0.0F) / 0.75F)),
                      minimum, maximum);
}

[[nodiscard]] std::uint32_t adaptive_arc_segments(float radius_x, float radius_y,
                                                  float sweep_radians) noexcept {
    const auto radius = std::max(std::max(radius_x, radius_y), 0.0F);
    if (radius <= geometry_epsilon || std::abs(sweep_radians) <= geometry_epsilon) {
        return 1U;
    }
    const auto ratio = std::clamp(geometry_flattening_tolerance / radius, 0.0F, 1.0F);
    const auto max_angle_step = std::max(2.0F * std::acos(1.0F - ratio), pi / 96.0F);
    return std::clamp(
        static_cast<std::uint32_t>(std::ceil(std::abs(sweep_radians) / max_angle_step)), 8U, 256U);
}

void append_prepared_quadratic(std::vector<layout::Point>& points, layout::Point start,
                               layout::Point control, layout::Point end) {
    struct QuadraticWork {
        layout::Point start{};
        layout::Point control{};
        layout::Point end{};
        std::uint32_t depth = 0U;
    };

    std::vector<QuadraticWork> stack;
    stack.reserve(32U);
    stack.push_back(QuadraticWork{.start = start, .control = control, .end = end});
    while (!stack.empty()) {
        const auto work = stack.back();
        stack.pop_back();
        if (work.depth >= max_curve_flattening_depth ||
            point_line_distance(work.control, work.start, work.end) <=
                geometry_flattening_tolerance) {
            points.push_back(work.end);
            continue;
        }

        const auto start_control = point_lerp(work.start, work.control, 0.5F);
        const auto control_end = point_lerp(work.control, work.end, 0.5F);
        const auto middle = point_lerp(start_control, control_end, 0.5F);
        const auto next_depth = work.depth + 1U;
        stack.push_back(
            QuadraticWork{.start = middle, .control = control_end, .end = work.end,
                          .depth = next_depth});
        stack.push_back(
            QuadraticWork{.start = work.start, .control = start_control, .end = middle,
                          .depth = next_depth});
    }
}

void append_prepared_cubic(std::vector<layout::Point>& points, layout::Point start,
                           layout::Point control1, layout::Point control2, layout::Point end) {
    struct CubicWork {
        layout::Point start{};
        layout::Point control1{};
        layout::Point control2{};
        layout::Point end{};
        std::uint32_t depth = 0U;
    };

    std::vector<CubicWork> stack;
    stack.reserve(32U);
    stack.push_back(CubicWork{.start = start, .control1 = control1, .control2 = control2,
                              .end = end});
    while (!stack.empty()) {
        const auto work = stack.back();
        stack.pop_back();
        const auto flatness =
            std::max(point_line_distance(work.control1, work.start, work.end),
                     point_line_distance(work.control2, work.start, work.end));
        if (work.depth >= max_curve_flattening_depth ||
            flatness <= geometry_flattening_tolerance) {
            points.push_back(work.end);
            continue;
        }

        const auto start_control = point_lerp(work.start, work.control1, 0.5F);
        const auto middle_control = point_lerp(work.control1, work.control2, 0.5F);
        const auto control_end = point_lerp(work.control2, work.end, 0.5F);
        const auto left_control = point_lerp(start_control, middle_control, 0.5F);
        const auto right_control = point_lerp(middle_control, control_end, 0.5F);
        const auto middle = point_lerp(left_control, right_control, 0.5F);
        const auto next_depth = work.depth + 1U;
        stack.push_back(CubicWork{.start = middle,
                                  .control1 = right_control,
                                  .control2 = control_end,
                                  .end = work.end,
                                  .depth = next_depth});
        stack.push_back(CubicWork{.start = work.start,
                                  .control1 = start_control,
                                  .control2 = left_control,
                                  .end = middle,
                                  .depth = next_depth});
    }
}

void append_prepared_arc(std::vector<layout::Point>& points, layout::Point start,
                         const GeometrySegment& segment) {
    const auto arc = resolve_prepared_arc(start, segment);
    if (!arc.has_value()) {
        points.push_back(segment.point);
        return;
    }
    const auto segments = adaptive_arc_segments(arc->radius_x, arc->radius_y, arc->sweep_angle);
    for (auto step = 1U; step <= segments; ++step) {
        const auto t = static_cast<float>(step) / static_cast<float>(segments);
        const auto angle = arc->start_angle + arc->sweep_angle * t;
        points.push_back(evaluate_prepared_arc_point(*arc, angle));
    }
}

[[nodiscard]] std::vector<layout::Point> flatten_prepared_figure(const GeometryFigure& figure) {
    std::vector<layout::Point> points;
    auto reserve_points = std::size_t{1U};
    for (const auto& segment : figure.segments) {
        switch (segment.type) {
        case GeometrySegmentType::Line:
            reserve_points += 1U;
            break;
        case GeometrySegmentType::QuadraticBezier:
            reserve_points += 8U;
            break;
        case GeometrySegmentType::CubicBezier:
            reserve_points += 12U;
            break;
        case GeometrySegmentType::Arc:
            reserve_points += 24U;
            break;
        }
    }
    points.reserve(reserve_points);
    points.push_back(figure.start);
    auto current = figure.start;
    for (const auto& segment : figure.segments) {
        switch (segment.type) {
        case GeometrySegmentType::Line:
            points.push_back(segment.point);
            break;
        case GeometrySegmentType::QuadraticBezier:
            append_prepared_quadratic(points, current, segment.control_point1, segment.point);
            break;
        case GeometrySegmentType::CubicBezier:
            append_prepared_cubic(points, current, segment.control_point1, segment.control_point2,
                                  segment.point);
            break;
        case GeometrySegmentType::Arc:
            append_prepared_arc(points, current, segment);
            break;
        }
        current = segment.point;
    }
    if (figure.end == GeometryFigureEnd::Closed && !points.empty() &&
        (points.front().x != points.back().x || points.front().y != points.back().y)) {
        points.push_back(points.front());
    }
    return points;
}

[[nodiscard]] std::vector<std::vector<layout::Point>>
flatten_prepared_geometry(const Geometry& geometry) {
    std::vector<std::vector<layout::Point>> figures;
    figures.reserve(geometry.figures.size());
    for (const auto& figure : geometry.figures) {
        figures.push_back(flatten_prepared_figure(figure));
    }
    return figures;
}

[[nodiscard]] std::vector<std::vector<layout::Point>>
flatten_prepared_filled_contours(const Geometry& geometry,
                                 const std::vector<std::vector<layout::Point>>& flattened_figures) {
    std::vector<std::vector<layout::Point>> contours;
    contours.reserve(geometry.figures.size());
    const auto figure_count = std::min(geometry.figures.size(), flattened_figures.size());
    for (std::size_t index = 0U; index < figure_count; ++index) {
        const auto& figure = geometry.figures[index];
        if (figure.begin != GeometryFigureBegin::Filled) {
            continue;
        }
        auto points = clean_closed_contour(flattened_figures[index]);
        if (points.size() >= 3U) {
            contours.push_back(std::move(points));
        }
    }
    return contours;
}

struct FillEdge {
    layout::Point start{};
    layout::Point end{};
    float min_y = 0.0F;
    float max_y = 0.0F;
    int winding = 0;
};

struct ActiveFillEdge {
    const FillEdge* edge = nullptr;
    float x = 0.0F;
};

[[nodiscard]] float x_at_y(const FillEdge& edge, float y) noexcept {
    const auto dy = edge.end.y - edge.start.y;
    if (std::abs(dy) <= geometry_epsilon) {
        return edge.start.x;
    }
    const auto t = (y - edge.start.y) / dy;
    return edge.start.x + (edge.end.x - edge.start.x) * t;
}

void add_sorted_y(std::vector<float>& values, float y) {
    if (std::isfinite(y)) {
        values.push_back(y);
    }
}

[[nodiscard]] bool edge_intersection_y(const FillEdge& first, const FillEdge& second,
                                       float& y) noexcept {
    const auto x1 = first.start.x;
    const auto y1 = first.start.y;
    const auto x2 = first.end.x;
    const auto y2 = first.end.y;
    const auto x3 = second.start.x;
    const auto y3 = second.start.y;
    const auto x4 = second.end.x;
    const auto y4 = second.end.y;
    const auto denominator = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(denominator) <= geometry_epsilon) {
        return false;
    }

    const auto determinant1 = x1 * y2 - y1 * x2;
    const auto determinant2 = x3 * y4 - y3 * x4;
    const auto intersection_y = (determinant1 * (y3 - y4) - (y1 - y2) * determinant2) / denominator;
    const auto first_min = std::max(std::min(y1, y2), std::min(y3, y4));
    const auto first_max = std::min(std::max(y1, y2), std::max(y3, y4));
    if (intersection_y <= first_min + geometry_epsilon ||
        intersection_y >= first_max - geometry_epsilon) {
        return false;
    }
    const auto intersection_x = x_at_y(first, intersection_y);
    const auto second_x = x_at_y(second, intersection_y);
    if (std::abs(intersection_x - second_x) > 0.01F) {
        return false;
    }
    y = intersection_y;
    return true;
}

void append_trapezoid(std::vector<layout::Point>& vertices, const FillEdge& left,
                      const FillEdge& right, float top, float bottom) {
    if (bottom - top <= geometry_epsilon) {
        return;
    }
    const auto top_left = layout::Point{x_at_y(left, top), top};
    const auto bottom_left = layout::Point{x_at_y(left, bottom), bottom};
    const auto top_right = layout::Point{x_at_y(right, top), top};
    const auto bottom_right = layout::Point{x_at_y(right, bottom), bottom};
    if (std::abs(top_right.x - top_left.x) <= geometry_epsilon &&
        std::abs(bottom_right.x - bottom_left.x) <= geometry_epsilon) {
        return;
    }

    vertices.push_back(top_left);
    vertices.push_back(top_right);
    vertices.push_back(bottom_right);
    vertices.push_back(top_left);
    vertices.push_back(bottom_right);
    vertices.push_back(bottom_left);
}

[[nodiscard]] std::vector<layout::Point>
tessellate_prepared_geometry_fill(const std::vector<std::vector<layout::Point>>& contours,
                                  GeometryFillRule fill_rule) {
    std::vector<FillEdge> edges;
    std::vector<float> y_values;
    auto estimated_edges = std::size_t{0U};
    for (const auto& points : contours) {
        estimated_edges += points.size();
    }
    edges.reserve(estimated_edges);
    y_values.reserve(estimated_edges * 2U);
    for (const auto& points : contours) {
        if (points.size() < 3U) {
            continue;
        }
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto start = points[index];
            const auto end = points[(index + 1U) % points.size()];
            if (std::abs(start.y - end.y) <= geometry_epsilon) {
                continue;
            }
            edges.push_back(FillEdge{.start = start,
                                     .end = end,
                                     .min_y = std::min(start.y, end.y),
                                     .max_y = std::max(start.y, end.y),
                                     .winding = start.y < end.y ? 1 : -1});
            add_sorted_y(y_values, start.y);
            add_sorted_y(y_values, end.y);
        }
    }

    if (edges.empty() || y_values.size() < 2U) {
        return {};
    }

    for (std::size_t first = 0; first < edges.size(); ++first) {
        for (std::size_t second = first + 1U; second < edges.size(); ++second) {
            if (edges[first].max_y <= edges[second].min_y + geometry_epsilon ||
                edges[second].max_y <= edges[first].min_y + geometry_epsilon) {
                continue;
            }
            auto y = 0.0F;
            if (edge_intersection_y(edges[first], edges[second], y)) {
                add_sorted_y(y_values, y);
            }
        }
    }

    std::sort(y_values.begin(), y_values.end());
    y_values.erase(std::unique(y_values.begin(), y_values.end(),
                               [](float left, float right) {
                                   return std::abs(left - right) <= geometry_epsilon;
                               }),
                   y_values.end());

    std::vector<layout::Point> vertices;
    vertices.reserve(edges.size() * 6U);
    std::vector<ActiveFillEdge> active_edges;
    active_edges.reserve(edges.size());
    for (std::size_t band = 1U; band < y_values.size(); ++band) {
        const auto top = y_values[band - 1U];
        const auto bottom = y_values[band];
        if (bottom - top <= geometry_epsilon) {
            continue;
        }
        const auto mid_y = (top + bottom) * 0.5F;
        active_edges.clear();
        for (const auto& edge : edges) {
            if (mid_y > edge.min_y + geometry_epsilon && mid_y < edge.max_y - geometry_epsilon) {
                active_edges.push_back(ActiveFillEdge{.edge = &edge, .x = x_at_y(edge, mid_y)});
            }
        }
        if (active_edges.size() < 2U) {
            continue;
        }
        std::sort(active_edges.begin(), active_edges.end(),
                  [](const auto& left, const auto& right) { return left.x < right.x; });

        auto winding = 0;
        const FillEdge* span_start = nullptr;
        for (const auto& active : active_edges) {
            const auto filled_before =
                fill_rule == GeometryFillRule::EvenOdd ? (winding % 2) != 0 : winding != 0;
            winding += fill_rule == GeometryFillRule::EvenOdd ? 1 : active.edge->winding;
            const auto filled_after =
                fill_rule == GeometryFillRule::EvenOdd ? (winding % 2) != 0 : winding != 0;
            if (!filled_before && filled_after) {
                span_start = active.edge;
            } else if (filled_before && !filled_after && span_start != nullptr) {
                append_trapezoid(vertices, *span_start, *active.edge, top, bottom);
                span_start = nullptr;
            }
        }
    }
    return vertices;
}

[[nodiscard]] layout::Rect
bounds_from_figures(const std::vector<std::vector<layout::Point>>& figures) noexcept {
    BoundsBuilder bounds;
    for (const auto& figure : figures) {
        for (const auto point : figure) {
            bounds.add(point);
        }
    }
    return bounds.rect();
}

[[nodiscard]] PreparedGeometryFlatten prepare_geometry_flatten(const Geometry& geometry) {
    auto prepared = PreparedGeometryFlatten{};
    prepared.figures = flatten_prepared_geometry(geometry);
    prepared.bounds = bounds_from_figures(prepared.figures);
    return prepared;
}

[[nodiscard]] PreparedGeometryFill
prepare_geometry_fill(const Geometry& geometry,
                      std::shared_ptr<const PreparedGeometryFlatten> flatten) {
    auto prepared = PreparedGeometryFill{};
    prepared.flatten = std::move(flatten);
    if (prepared.flatten != nullptr) {
        prepared.filled_contours =
            flatten_prepared_filled_contours(geometry, prepared.flatten->figures);
    }
    prepared.tessellated_vertices =
        tessellate_prepared_geometry_fill(prepared.filled_contours, geometry.fill_rule);
    prepared.bounds = prepared.flatten != nullptr ? prepared.flatten->bounds : layout::Rect{};
    return prepared;
}

[[nodiscard]] PreparedGeometryStroke
prepare_geometry_stroke(std::shared_ptr<const PreparedGeometryFlatten> flatten) {
    auto prepared = PreparedGeometryStroke{};
    prepared.flatten = std::move(flatten);
    prepared.bounds = prepared.flatten != nullptr ? prepared.flatten->bounds : layout::Rect{};
    return prepared;
}

[[nodiscard]] std::wstring utf8_to_wide_family(std::string_view text) {
    if (text.empty()) {
        return L"Segoe UI";
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return L"Segoe UI";
    }

    const auto byte_count = static_cast<int>(text.size());
    const auto wide_count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), byte_count, nullptr, 0);
    if (wide_count <= 0) {
        return L"Segoe UI";
    }

    std::wstring wide_text(static_cast<std::size_t>(wide_count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), byte_count, wide_text.data(),
                        wide_count);
    return wide_text;
}

[[nodiscard]] Microsoft::WRL::ComPtr<IDWriteFactory> create_preparation_dwrite_factory() {
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    const auto result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                            reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    if (FAILED(result)) {
        return {};
    }
    return factory;
}

[[nodiscard]] IDWriteFactory* preparation_dwrite_factory() noexcept {
    static auto factory = create_preparation_dwrite_factory();
    return factory.Get();
}

[[nodiscard]] DWRITE_FONT_STYLE to_dwrite_font_style(FontStyle style) noexcept {
    switch (style) {
    case FontStyle::Italic:
        return DWRITE_FONT_STYLE_ITALIC;
    case FontStyle::Oblique:
        return DWRITE_FONT_STYLE_OBLIQUE;
    case FontStyle::Normal:
    default:
        return DWRITE_FONT_STYLE_NORMAL;
    }
}

[[nodiscard]] Microsoft::WRL::ComPtr<IDWriteFontFace>
resolve_preparation_font_face(const TextStyle& style) noexcept {
    auto* factory = preparation_dwrite_factory();
    if (factory == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
    if (FAILED(factory->GetSystemFontCollection(&collection)) || collection == nullptr) {
        return {};
    }

    auto family_name = utf8_to_wide_family(style.font_family);
    UINT32 family_index = 0;
    BOOL exists = FALSE;
    auto result = collection->FindFamilyName(family_name.c_str(), &family_index, &exists);
    if (FAILED(result) || !exists) {
        result = collection->FindFamilyName(L"Segoe UI", &family_index, &exists);
    }
    if (FAILED(result) || !exists) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
    if (FAILED(collection->GetFontFamily(family_index, &family)) || family == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFont> font;
    if (FAILED(family->GetFirstMatchingFont(static_cast<DWRITE_FONT_WEIGHT>(style.font_weight),
                                            static_cast<DWRITE_FONT_STRETCH>(style.font_stretch),
                                            to_dwrite_font_style(style.font_style), &font)) ||
        font == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontFace> face;
    if (FAILED(font->CreateFontFace(&face))) {
        return {};
    }
    return face;
}

[[nodiscard]] PreparedTextGlyphCoverage make_prepared_text_key(const TextGlyph& glyph,
                                                               const TextStyle& style) {
    return PreparedTextGlyphCoverage{
        .font_family = glyph.font_family.empty() ? style.font_family : glyph.font_family,
        .glyph_index = glyph.glyph_index,
        .font_size_key = static_cast<std::uint32_t>(std::round(style.font_size * 64.0F)),
        .font_weight = static_cast<std::uint16_t>(style.font_weight),
        .font_stretch = static_cast<std::uint16_t>(style.font_stretch),
        .font_style = static_cast<std::uint8_t>(style.font_style),
        .is_right_to_left = glyph.is_right_to_left};
}

[[nodiscard]] bool same_prepared_text_key(const PreparedTextGlyphCoverage& left,
                                          const PreparedTextGlyphCoverage& right) noexcept {
    return left.font_family == right.font_family && left.glyph_index == right.glyph_index &&
           left.font_size_key == right.font_size_key && left.font_weight == right.font_weight &&
           left.font_stretch == right.font_stretch && left.font_style == right.font_style &&
           left.is_right_to_left == right.is_right_to_left;
}

[[nodiscard]] std::size_t
prepared_text_key_hash(const PreparedTextGlyphCoverage& coverage) noexcept {
    auto seed = std::size_t{};
    hash_combine(seed, std::string_view{coverage.font_family});
    hash_combine(seed, coverage.glyph_index);
    hash_combine(seed, coverage.font_size_key);
    hash_combine(seed, coverage.font_weight);
    hash_combine(seed, coverage.font_stretch);
    hash_combine(seed, coverage.font_style);
    hash_combine(seed, coverage.is_right_to_left);
    return seed;
}

struct PreparedTextGlyphCoverageKeyHash {
    [[nodiscard]] std::size_t operator()(const PreparedTextGlyphCoverage& coverage) const noexcept {
        return prepared_text_key_hash(coverage);
    }
};

struct PreparedTextGlyphCoverageKeyEqual {
    [[nodiscard]] bool operator()(const PreparedTextGlyphCoverage& left,
                                  const PreparedTextGlyphCoverage& right) const noexcept {
        return same_prepared_text_key(left, right);
    }
};

struct PreparedTextGlyphCoveragePtrHash {
    [[nodiscard]] std::size_t operator()(const PreparedTextGlyphCoverage* coverage) const noexcept {
        return coverage == nullptr ? 0U : prepared_text_key_hash(*coverage);
    }
};

struct PreparedTextGlyphCoveragePtrEqual {
    [[nodiscard]] bool operator()(const PreparedTextGlyphCoverage* left,
                                  const PreparedTextGlyphCoverage* right) const noexcept {
        if (left == nullptr || right == nullptr) {
            return left == right;
        }
        return same_prepared_text_key(*left, *right);
    }
};

[[nodiscard]] PreparedTextGlyphCoverage rasterize_prepared_glyph_coverage(IDWriteFontFace& face,
                                                                          const TextGlyph& glyph,
                                                                          const TextStyle& style) {
    constexpr auto glyph_atlas_padding = 2U;
    constexpr auto glyph_atlas_bytes_per_pixel = 4U;

    auto coverage = make_prepared_text_key(glyph, style);
    auto* factory = preparation_dwrite_factory();
    if (factory == nullptr || glyph.glyph_index == 0U || style.font_size <= 0.0F) {
        return coverage;
    }

    const auto glyph_index = static_cast<UINT16>(glyph.glyph_index);
    const auto advance = glyph.advance;
    const DWRITE_GLYPH_OFFSET offset{};
    DWRITE_GLYPH_RUN glyph_run{};
    glyph_run.fontFace = &face;
    glyph_run.fontEmSize = style.font_size;
    glyph_run.glyphCount = 1U;
    glyph_run.glyphIndices = &glyph_index;
    glyph_run.glyphAdvances = &advance;
    glyph_run.glyphOffsets = &offset;
    glyph_run.isSideways = FALSE;
    glyph_run.bidiLevel = glyph.is_right_to_left ? 1U : 0U;

    Microsoft::WRL::ComPtr<IDWriteGlyphRunAnalysis> analysis;
    auto result = factory->CreateGlyphRunAnalysis(
        &glyph_run, 1.0F, nullptr, DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
        DWRITE_MEASURING_MODE_NATURAL, 0.0F, 0.0F, &analysis);
    auto texture_type = DWRITE_TEXTURE_CLEARTYPE_3x1;
    if (FAILED(result) || analysis == nullptr) {
        result = factory->CreateGlyphRunAnalysis(
            &glyph_run, 1.0F, nullptr, DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
            0.0F, 0.0F, &analysis);
        texture_type = DWRITE_TEXTURE_ALIASED_1x1;
    }
    if (FAILED(result) || analysis == nullptr) {
        return coverage;
    }

    RECT bounds{};
    if (FAILED(analysis->GetAlphaTextureBounds(texture_type, &bounds)) ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return coverage;
    }

    const auto width = static_cast<std::uint32_t>(bounds.right - bounds.left);
    const auto height = static_cast<std::uint32_t>(bounds.bottom - bounds.top);
    const auto bytes_per_pixel = texture_type == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3U : 1U;
    std::vector<std::uint8_t> texture(static_cast<std::size_t>(width) * height * bytes_per_pixel);
    if (FAILED(analysis->CreateAlphaTexture(texture_type, &bounds, texture.data(),
                                            static_cast<UINT32>(texture.size())))) {
        return coverage;
    }

    const auto padded_width = width + glyph_atlas_padding * 2U;
    const auto padded_height = height + glyph_atlas_padding * 2U;
    std::vector<std::byte> pixels(static_cast<std::size_t>(padded_width) * padded_height *
                                      glyph_atlas_bytes_per_pixel,
                                  std::byte{0});
    for (std::uint32_t y = 0U; y < height; ++y) {
        const auto destination =
            static_cast<std::size_t>(y + glyph_atlas_padding) * padded_width + glyph_atlas_padding;
        for (std::uint32_t x = 0U; x < width; ++x) {
            const auto source = static_cast<std::size_t>(y) * width + x;
            const auto target = (destination + x) * glyph_atlas_bytes_per_pixel;
            if (bytes_per_pixel == 1U) {
                const auto value = texture[source];
                pixels[target] = static_cast<std::byte>(value);
                pixels[target + 1U] = static_cast<std::byte>(value);
                pixels[target + 2U] = static_cast<std::byte>(value);
                pixels[target + 3U] = static_cast<std::byte>(value);
            } else {
                const auto base = source * 3U;
                const auto red = texture[base];
                const auto green = texture[base + 1U];
                const auto blue = texture[base + 2U];
                pixels[target] = static_cast<std::byte>(red);
                pixels[target + 1U] = static_cast<std::byte>(green);
                pixels[target + 2U] = static_cast<std::byte>(blue);
                pixels[target + 3U] = static_cast<std::byte>(std::max({red, green, blue}));
            }
        }
    }

    coverage.left = bounds.left - static_cast<int>(glyph_atlas_padding);
    coverage.top = bounds.top - static_cast<int>(glyph_atlas_padding);
    coverage.width = padded_width;
    coverage.height = padded_height;
    coverage.pixels = std::move(pixels);
    return coverage;
}

[[nodiscard]] std::shared_ptr<const PreparedTextGlyphCoverageList> prepare_text_glyph_coverages(
    const TextLayout& layout,
    std::unordered_map<std::size_t, std::vector<std::shared_ptr<const PreparedTextGlyphCoverage>>>&
        cache) {
    auto coverages = std::make_shared<PreparedTextGlyphCoverageList>();
    if (layout.glyphs.empty() || layout.style.font_size <= 0.0F) {
        return coverages;
    }
    coverages->glyphs_by_layout_index.resize(layout.glyphs.size());

    auto default_face = resolve_preparation_font_face(layout.style);
    if (default_face == nullptr) {
        return coverages;
    }

    std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDWriteFontFace>> face_cache;
    const auto face_for_glyph = [&](const TextGlyph& glyph) {
        const auto font_family =
            glyph.font_family.empty() ? layout.style.font_family : glyph.font_family;
        if (font_family.empty() || font_family == layout.style.font_family) {
            return default_face;
        }

        if (const auto iterator = face_cache.find(font_family); iterator != face_cache.end()) {
            return iterator->second;
        }

        auto glyph_style = layout.style;
        glyph_style.font_family = font_family;
        auto face = resolve_preparation_font_face(glyph_style);
        if (face == nullptr) {
            face = default_face;
        }
        const auto [iterator, inserted] = face_cache.emplace(font_family, face);
        (void)inserted;
        return iterator->second;
    };

    coverages->glyphs.reserve(layout.glyphs.size());
    std::unordered_set<PreparedTextGlyphCoverage, PreparedTextGlyphCoverageKeyHash,
                       PreparedTextGlyphCoverageKeyEqual>
        unique_keys;
    unique_keys.reserve(layout.glyphs.size());
    for (std::size_t glyph_index = 0U; glyph_index < layout.glyphs.size(); ++glyph_index) {
        const auto& glyph = layout.glyphs[glyph_index];
        if (glyph.glyph_index == 0U) {
            continue;
        }
        const auto key = make_prepared_text_key(glyph, layout.style);
        const auto key_hash = prepared_text_key_hash(key);
        auto prepared_coverage = std::shared_ptr<const PreparedTextGlyphCoverage>{};
        if (const auto iterator = cache.find(key_hash); iterator != cache.end()) {
            for (const auto& cached_coverage : iterator->second) {
                if (cached_coverage != nullptr && same_prepared_text_key(*cached_coverage, key)) {
                    prepared_coverage = cached_coverage;
                    break;
                }
            }
        }

        if (prepared_coverage == nullptr) {
            auto face = face_for_glyph(glyph);
            if (face == nullptr) {
                continue;
            }
            auto coverage = rasterize_prepared_glyph_coverage(*face.Get(), glyph, layout.style);
            if (!coverage.pixels.empty() && coverage.width > 0U && coverage.height > 0U) {
                prepared_coverage =
                    std::make_shared<PreparedTextGlyphCoverage>(std::move(coverage));
                cache[key_hash].push_back(prepared_coverage);
                trim_cache_entries(cache, max_prepared_text_glyph_cache_entries);
            }
        }

        if (prepared_coverage != nullptr) {
            coverages->glyphs_by_layout_index[glyph_index] = prepared_coverage;
            if (unique_keys.insert(key).second) {
                coverages->glyphs_by_hash[key_hash].push_back(prepared_coverage);
                coverages->glyphs.push_back(prepared_coverage);
            }
        }
    }
    return coverages;
}

[[nodiscard]] layout::Rect visual_bounds(const SaveCommand&) noexcept {
    return {};
}
[[nodiscard]] layout::Rect visual_bounds(const RestoreCommand&) noexcept {
    return {};
}
[[nodiscard]] layout::Rect visual_bounds(const PushClipCommand&) noexcept {
    return {};
}
[[nodiscard]] layout::Rect visual_bounds(const PopClipCommand&) noexcept {
    return {};
}
[[nodiscard]] layout::Rect visual_bounds(const PushGeometryClipCommand&) noexcept {
    return {};
}
[[nodiscard]] layout::Rect visual_bounds(const PopGeometryClipCommand&) noexcept {
    return {};
}
[[nodiscard]] layout::Rect visual_bounds(const PushLayerCommand& command) noexcept {
    return transform_rect(command.options.bounds, command.options.transform);
}
[[nodiscard]] layout::Rect visual_bounds(const PopLayerCommand&) noexcept {
    return {};
}
[[nodiscard]] layout::Rect visual_bounds(const DrawLineCommand& command) noexcept {
    return line_bounds(command);
}
[[nodiscard]] layout::Rect visual_bounds(const FillRectCommand& command) noexcept {
    return command.rect;
}
[[nodiscard]] layout::Rect visual_bounds(const FillPixelSnappedRectCommand& command) noexcept {
    return command.rect;
}
[[nodiscard]] layout::Rect visual_bounds(const StrokePixelSnappedRectCommand& command) noexcept {
    return command.rect;
}
[[nodiscard]] layout::Rect visual_bounds(const StrokeRectCommand& command) noexcept {
    return layout::inflate_rect(command.rect, std::max(command.stroke_width, 0.0F) * 0.5F);
}
[[nodiscard]] layout::Rect visual_bounds(const FillRoundedRectCommand& command) noexcept {
    return command.rect;
}
[[nodiscard]] layout::Rect visual_bounds(const StrokeRoundedRectCommand& command) noexcept {
    return layout::inflate_rect(command.rect, std::max(command.stroke_width, 0.0F) * 0.5F);
}
[[nodiscard]] layout::Rect visual_bounds(const FillEllipseCommand& command) noexcept {
    return command.rect;
}
[[nodiscard]] layout::Rect visual_bounds(const StrokeEllipseCommand& command) noexcept {
    return layout::inflate_rect(command.rect, std::max(command.stroke_width, 0.0F) * 0.5F);
}
[[nodiscard]] layout::Rect visual_bounds(const FillGeometryCommand& command) noexcept {
    return geometry_bounds(command.geometry);
}
[[nodiscard]] layout::Rect visual_bounds(const StrokeGeometryCommand& command) noexcept {
    return layout::inflate_rect(geometry_bounds(command.geometry),
                                std::max(command.style.width, 0.0F) * 0.5F);
}
[[nodiscard]] layout::Rect visual_bounds(const DrawImageCommand& command) noexcept {
    return command.options.destination;
}
[[nodiscard]] layout::Rect visual_bounds(const DrawTextCommand& command) noexcept {
    return command.rect;
}
[[nodiscard]] layout::Rect visual_bounds(const DrawTextLayoutCommand& command) noexcept {
    return text_layout_bounds(command);
}
[[nodiscard]] layout::Rect visual_bounds(const DrawBoxShadowCommand& command) noexcept {
    const auto spread = std::max(command.style.spread, 0.0F);
    const auto blur = std::max(command.style.blur_radius, 0.0F);
    return layout::inflate_rect(layout::offset_rect(command.rect, command.style.offset),
                                spread + blur);
}

template <typename T> void hash_combine(std::size_t& seed, const T& value) noexcept {
    seed ^= std::hash<T>{}(value) + 0x9E3779B9U + (seed << 6U) + (seed >> 2U);
}

void hash_point(std::size_t& seed, layout::Point point) noexcept {
    hash_combine(seed, point.x);
    hash_combine(seed, point.y);
}

void hash_rect(std::size_t& seed, layout::Rect rect) noexcept {
    hash_combine(seed, rect.x);
    hash_combine(seed, rect.y);
    hash_combine(seed, rect.width);
    hash_combine(seed, rect.height);
}

void hash_color(std::size_t& seed, Color color) noexcept {
    hash_combine(seed, color.red);
    hash_combine(seed, color.green);
    hash_combine(seed, color.blue);
    hash_combine(seed, color.alpha);
}

void hash_transform(std::size_t& seed, Transform2D transform) noexcept {
    hash_combine(seed, transform.m11);
    hash_combine(seed, transform.m12);
    hash_combine(seed, transform.m21);
    hash_combine(seed, transform.m22);
    hash_combine(seed, transform.dx);
    hash_combine(seed, transform.dy);
}

void hash_geometry(std::size_t& seed, const Geometry& geometry) noexcept {
    hash_combine(seed, static_cast<int>(geometry.fill_rule));
    hash_combine(seed, geometry.figures.size());
    for (const auto& figure : geometry.figures) {
        hash_point(seed, figure.start);
        hash_combine(seed, static_cast<int>(figure.begin));
        hash_combine(seed, static_cast<int>(figure.end));
        hash_combine(seed, figure.segments.size());
        for (const auto& segment : figure.segments) {
            hash_combine(seed, static_cast<int>(segment.type));
            hash_point(seed, segment.point);
            hash_point(seed, segment.control_point1);
            hash_point(seed, segment.control_point2);
            hash_combine(seed, segment.radius.width);
            hash_combine(seed, segment.radius.height);
            hash_combine(seed, segment.rotation_angle);
            hash_combine(seed, static_cast<int>(segment.arc_size));
            hash_combine(seed, static_cast<int>(segment.sweep_direction));
        }
    }
}

template <typename Value> void append_cache_key_bytes(std::string& key, const Value& value) {
    static_assert(std::is_trivially_copyable_v<Value>);
    const auto* bytes = reinterpret_cast<const char*>(&value);
    key.append(bytes, sizeof(Value));
}

void append_geometry_cache_point(std::string& key, layout::Point point) {
    append_cache_key_bytes(key, point.x);
    append_cache_key_bytes(key, point.y);
}

[[nodiscard]] std::string geometry_cache_key(const Geometry& geometry) {
    auto key = std::string{};
    key.reserve(sizeof(std::uint32_t) * 4U + geometry.figures.size() * 96U);
    append_cache_key_bytes(key, geometry.fill_rule);
    append_cache_key_bytes(key, geometry.figures.size());
    for (const auto& figure : geometry.figures) {
        append_geometry_cache_point(key, figure.start);
        append_cache_key_bytes(key, figure.begin);
        append_cache_key_bytes(key, figure.end);
        append_cache_key_bytes(key, figure.segments.size());
        for (const auto& segment : figure.segments) {
            append_cache_key_bytes(key, segment.type);
            append_geometry_cache_point(key, segment.point);
            append_geometry_cache_point(key, segment.control_point1);
            append_geometry_cache_point(key, segment.control_point2);
            append_cache_key_bytes(key, segment.radius.width);
            append_cache_key_bytes(key, segment.radius.height);
            append_cache_key_bytes(key, segment.rotation_angle);
            append_cache_key_bytes(key, segment.arc_size);
            append_cache_key_bytes(key, segment.sweep_direction);
        }
    }
    return key;
}

void hash_text_style(std::size_t& seed, const TextStyle& style) noexcept {
    hash_combine(seed, std::string_view{style.font_family});
    hash_combine(seed, std::string_view{style.locale});
    hash_combine(seed, style.font_size);
    hash_color(seed, style.color);
    hash_combine(seed, static_cast<int>(style.alignment));
    hash_combine(seed, static_cast<int>(style.vertical_alignment));
    hash_combine(seed, static_cast<int>(style.wrapping));
    hash_combine(seed, static_cast<int>(style.trimming));
    hash_combine(seed, static_cast<int>(style.font_weight));
    hash_combine(seed, static_cast<int>(style.font_style));
    hash_combine(seed, static_cast<int>(style.decoration_line));
}

[[nodiscard]] std::size_t payload_fingerprint(const SaveCommand&) noexcept {
    return 0U;
}
[[nodiscard]] std::size_t payload_fingerprint(const RestoreCommand&) noexcept {
    return 0U;
}
[[nodiscard]] std::size_t payload_fingerprint(const PopClipCommand&) noexcept {
    return 0U;
}
[[nodiscard]] std::size_t payload_fingerprint(const PopGeometryClipCommand&) noexcept {
    return 0U;
}
[[nodiscard]] std::size_t payload_fingerprint(const PopLayerCommand&) noexcept {
    return 0U;
}

[[nodiscard]] std::size_t payload_fingerprint(const PushClipCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.rect);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const PushGeometryClipCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_geometry(seed, command.geometry);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const PushLayerCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.options.bounds);
    hash_combine(seed, command.options.opacity);
    hash_transform(seed, command.options.transform);
    hash_combine(seed, command.options.clips_to_bounds);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const DrawLineCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_point(seed, command.start);
    hash_point(seed, command.end);
    hash_color(seed, command.color);
    hash_combine(seed, command.stroke_width);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const FillRectCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.rect);
    hash_color(seed, command.color);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const FillPixelSnappedRectCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.rect);
    hash_color(seed, command.color);
    return seed;
}

[[nodiscard]] std::size_t
payload_fingerprint(const StrokePixelSnappedRectCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.rect);
    hash_color(seed, command.color);
    hash_combine(seed, command.stroke_width);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const StrokeRectCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.rect);
    hash_color(seed, command.color);
    hash_combine(seed, command.stroke_width);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const FillRoundedRectCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.rect);
    hash_combine(seed, command.radius.x);
    hash_combine(seed, command.radius.y);
    hash_color(seed, command.color);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const StrokeRoundedRectCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.rect);
    hash_combine(seed, command.radius.x);
    hash_combine(seed, command.radius.y);
    hash_color(seed, command.color);
    hash_combine(seed, command.stroke_width);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const FillEllipseCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.rect);
    hash_color(seed, command.color);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const StrokeEllipseCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.rect);
    hash_color(seed, command.color);
    hash_combine(seed, command.stroke_width);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const FillGeometryCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_geometry(seed, command.geometry);
    hash_color(seed, command.color);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const StrokeGeometryCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_geometry(seed, command.geometry);
    hash_color(seed, command.color);
    hash_combine(seed, command.style.width);
    hash_combine(seed, static_cast<int>(command.style.start_cap));
    hash_combine(seed, static_cast<int>(command.style.end_cap));
    hash_combine(seed, static_cast<int>(command.style.line_join));
    hash_combine(seed, static_cast<int>(command.style.dash_style));
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const DrawImageCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_combine(seed, command.resource_id.value);
    hash_rect(seed, command.options.destination);
    hash_rect(seed, command.options.source);
    hash_combine(seed, command.options.opacity);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const DrawTextCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_combine(seed, command.text_view());
    hash_rect(seed, command.rect);
    hash_text_style(seed, command.style);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const DrawTextLayoutCommand& command) noexcept {
    auto seed = std::size_t{};
    const auto* layout = command.layout_value();
    if (layout != nullptr) {
        hash_combine(seed, std::string_view{layout->text});
        hash_text_style(seed, layout->style);
        hash_combine(seed, layout->size.width);
        hash_combine(seed, layout->size.height);
    }
    hash_point(seed, command.origin);
    return seed;
}

[[nodiscard]] std::size_t payload_fingerprint(const DrawBoxShadowCommand& command) noexcept {
    auto seed = std::size_t{};
    hash_rect(seed, command.rect);
    hash_color(seed, command.style.color);
    hash_point(seed, command.style.offset);
    hash_combine(seed, command.style.blur_radius);
    hash_combine(seed, command.style.spread);
    return seed;
}

struct DrawBatchKey {
    RenderBatchKind kind = RenderBatchKind::State;
    RenderCommandType first_command_type = RenderCommandType::Save;
    std::uint64_t state_key = 0U;
    std::uint64_t resource_key = 0U;

    [[nodiscard]] friend bool operator==(DrawBatchKey, DrawBatchKey) noexcept = default;
};

[[nodiscard]] std::uint64_t state_key_from_seed(std::size_t seed) noexcept {
    return static_cast<std::uint64_t>(seed == 0U ? 1U : seed);
}

[[nodiscard]] DrawBatchKey draw_batch_key_for(const RenderCommandList& command_list,
                                              const RenderOpcodeRecord& opcode) noexcept {
    auto state_seed = std::size_t{};
    auto resource_seed = std::size_t{};
    hash_combine(state_seed, static_cast<int>(opcode.opcode));
    switch (opcode.opcode) {
    case RenderCommandType::DrawLine: {
        const auto& payload =
            command_list.payload_by_index<DrawLineCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        hash_combine(state_seed, payload.stroke_width);
        break;
    }
    case RenderCommandType::FillRect: {
        const auto& payload =
            command_list.payload_by_index<FillRectCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        break;
    }
    case RenderCommandType::FillPixelSnappedRect: {
        const auto& payload =
            command_list.payload_by_index<FillPixelSnappedRectCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        break;
    }
    case RenderCommandType::StrokePixelSnappedRect: {
        const auto& payload =
            command_list.payload_by_index<StrokePixelSnappedRectCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        hash_combine(state_seed, payload.stroke_width);
        break;
    }
    case RenderCommandType::StrokeRect: {
        const auto& payload =
            command_list.payload_by_index<StrokeRectCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        hash_combine(state_seed, payload.stroke_width);
        break;
    }
    case RenderCommandType::FillRoundedRect: {
        const auto& payload =
            command_list.payload_by_index<FillRoundedRectCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        hash_combine(state_seed, payload.radius.x);
        hash_combine(state_seed, payload.radius.y);
        break;
    }
    case RenderCommandType::StrokeRoundedRect: {
        const auto& payload =
            command_list.payload_by_index<StrokeRoundedRectCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        hash_combine(state_seed, payload.radius.x);
        hash_combine(state_seed, payload.radius.y);
        hash_combine(state_seed, payload.stroke_width);
        break;
    }
    case RenderCommandType::FillEllipse: {
        const auto& payload =
            command_list.payload_by_index<FillEllipseCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        break;
    }
    case RenderCommandType::StrokeEllipse: {
        const auto& payload =
            command_list.payload_by_index<StrokeEllipseCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        hash_combine(state_seed, payload.stroke_width);
        break;
    }
    case RenderCommandType::FillGeometry: {
        const auto& payload =
            command_list.payload_by_index<FillGeometryCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        hash_combine(resource_seed, payload.prepared_fill.get());
        break;
    }
    case RenderCommandType::StrokeGeometry: {
        const auto& payload =
            command_list.payload_by_index<StrokeGeometryCommand>(opcode.payload_index);
        hash_color(state_seed, payload.color);
        hash_combine(state_seed, payload.style.width);
        hash_combine(state_seed, static_cast<int>(payload.style.start_cap));
        hash_combine(state_seed, static_cast<int>(payload.style.end_cap));
        hash_combine(state_seed, static_cast<int>(payload.style.line_join));
        hash_combine(state_seed, static_cast<int>(payload.style.dash_style));
        hash_combine(resource_seed, payload.prepared_stroke.get());
        break;
    }
    case RenderCommandType::DrawImage: {
        const auto& payload =
            command_list.payload_by_index<DrawImageCommand>(opcode.payload_index);
        hash_combine(resource_seed, payload.resource_id.value);
        hash_combine(state_seed, payload.options.opacity);
        hash_rect(state_seed, payload.options.source);
        break;
    }
    case RenderCommandType::DrawText: {
        const auto& payload =
            command_list.payload_by_index<DrawTextCommand>(opcode.payload_index);
        hash_text_style(state_seed, payload.style);
        hash_combine(resource_seed, payload.text_view());
        break;
    }
    case RenderCommandType::DrawTextLayout: {
        const auto& payload =
            command_list.payload_by_index<DrawTextLayoutCommand>(opcode.payload_index);
        if (const auto* layout = payload.layout_value()) {
            hash_text_style(state_seed, layout->style);
            hash_combine(resource_seed, std::string_view{layout->text});
            hash_combine(resource_seed, payload.prepared_glyphs.get());
        }
        break;
    }
    case RenderCommandType::DrawBoxShadow: {
        const auto& payload =
            command_list.payload_by_index<DrawBoxShadowCommand>(opcode.payload_index);
        hash_color(state_seed, payload.style.color);
        hash_combine(state_seed, payload.style.blur_radius);
        hash_combine(state_seed, payload.style.spread);
        break;
    }
    case RenderCommandType::PushLayer: {
        const auto& payload =
            command_list.payload_by_index<PushLayerCommand>(opcode.payload_index);
        hash_combine(state_seed, payload.options.opacity);
        hash_combine(state_seed, payload.options.clips_to_bounds);
        break;
    }
    default:
        break;
    }
    return DrawBatchKey{.kind = batch_kind_for(opcode.opcode),
                        .first_command_type = opcode.opcode,
                        .state_key = state_key_from_seed(state_seed),
                        .resource_key = static_cast<std::uint64_t>(resource_seed)};
}

} // namespace

void DirtyRegion::add(layout::Rect rect) {
    if (!layout::is_visible_rect(rect)) {
        return;
    }

    for (auto iterator = rects_.begin(); iterator != rects_.end();) {
        if (layout::rects_touch_or_intersect(*iterator, rect)) {
            rect = layout::union_rects(*iterator, rect);
            iterator = rects_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    rects_.push_back(rect);
    if (rects_.size() > max_dirty_rects) {
        optimize(DirtyRegionOptimizeOptions{
            .max_rects = max_dirty_rects, .merge_slop = 0.0F, .scanline_merge = true});
        return;
    }
    rebuild_tree();
}

void DirtyRegion::add(const DirtyRegion& region) {
    const auto source_rects = region.rects();
    for (const auto rect : source_rects) {
        add(rect);
    }
}

void DirtyRegion::clip(layout::Rect clip_rect) {
    if (!layout::is_visible_rect(clip_rect)) {
        rects_.clear();
        rebuild_tree();
        return;
    }

    auto clipped_rects = std::vector<layout::Rect>{};
    clipped_rects.reserve(rects_.size());
    for (const auto rect : rects_) {
        if (auto clipped = layout::intersect_rects(rect, clip_rect);
            layout::is_visible_rect(clipped)) {
            clipped_rects.push_back(clipped);
        }
    }

    rects_.clear();
    for (const auto rect : clipped_rects) {
        add(rect);
    }
    rebuild_tree();
}

void DirtyRegion::optimize(DirtyRegionOptimizeOptions options) {
    if (rects_.empty()) {
        rebuild_tree();
        return;
    }

    if (!options.scanline_merge) {
        if (options.max_rects > 0U && rects_.size() > options.max_rects) {
            const auto merged_bounds = bounds();
            rects_.assign(1U, merged_bounds);
        }
        rebuild_tree();
        return;
    }

    std::sort(rects_.begin(), rects_.end(), [](layout::Rect left, layout::Rect right) noexcept {
        if (left.y == right.y) {
            return left.x < right.x;
        }
        return left.y < right.y;
    });

    auto merged = std::vector<layout::Rect>{};
    merged.reserve(rects_.size());
    for (auto rect : rects_) {
        if (!layout::is_visible_rect(rect)) {
            continue;
        }
        const auto merge_slop = std::max(options.merge_slop, 0.0F);
        auto joined = false;
        for (auto& existing : merged) {
            const auto expanded = layout::inflate_rect(existing, merge_slop);
            if (layout::rects_touch_or_intersect(expanded, rect)) {
                existing = layout::union_rects(existing, rect);
                joined = true;
                break;
            }
        }
        if (!joined) {
            merged.push_back(rect);
        }
    }

    rects_ = std::move(merged);
    if (options.max_rects > 0U && rects_.size() > options.max_rects) {
        const auto merged_bounds = bounds();
        rects_.assign(1U, merged_bounds);
    }
    rebuild_tree();
}

void DirtyRegion::cull_occluded(layout::Rect occluder) {
    if (!layout::is_visible_rect(occluder)) {
        return;
    }

    rects_.erase(std::remove_if(rects_.begin(), rects_.end(),
                                [occluder](layout::Rect rect) {
                                    const auto covered = layout::intersect_rects(rect, occluder);
                                    return covered == rect;
                                }),
                 rects_.end());
    rebuild_tree();
}

bool DirtyRegion::empty() const noexcept {
    return rects_.empty();
}

const std::vector<layout::Rect>& DirtyRegion::rects() const noexcept {
    return rects_;
}

const DirtyRegionNode& DirtyRegion::tree() const noexcept {
    return tree_;
}

std::size_t DirtyRegion::node_count() const noexcept {
    auto count_nodes = [](const DirtyRegionNode& node, const auto& self) noexcept -> std::size_t {
        auto count = std::size_t{1};
        for (const auto& child : node.children) {
            count += self(child, self);
        }
        return count;
    };
    return tree_.children.empty() && !layout::is_visible_rect(tree_.bounds)
               ? 0U
               : count_nodes(tree_, count_nodes);
}

layout::Rect DirtyRegion::bounds() const noexcept {
    layout::Rect result{};
    for (const auto rect : rects_) {
        result = layout::union_rects(result, rect);
    }
    return result;
}

void DirtyRegion::rebuild_tree() {
    tree_.children.clear();
    tree_.bounds = bounds();
    if (!layout::is_visible_rect(tree_.bounds)) {
        return;
    }

    tree_.children.reserve(rects_.size());
    for (const auto rect : rects_) {
        tree_.children.push_back(DirtyRegionNode{.bounds = rect});
    }
}

std::shared_ptr<const PreparedGeometryFlatten>
PreparedRenderCache::prepared_geometry_flatten(const Geometry& geometry) {
    auto seed = std::size_t{};
    hash_geometry(seed, geometry);
    auto canonical_key = geometry_cache_key(geometry);
    if (const auto iterator = prepared_geometry_flatten_cache_.find(seed);
        iterator != prepared_geometry_flatten_cache_.end()) {
        for (const auto& entry : iterator->second) {
            if (entry.prepared != nullptr && entry.canonical_key == canonical_key) {
                return entry.prepared;
            }
        }
    }

    auto prepared = std::make_shared<PreparedGeometryFlatten>(prepare_geometry_flatten(geometry));
    prepared_geometry_flatten_cache_[seed].push_back(PreparedGeometryFlattenEntry{
        .canonical_key = std::move(canonical_key), .geometry = geometry, .prepared = prepared});
    trim_cache_entries(prepared_geometry_flatten_cache_, max_prepared_geometry_cache_entries);
    return prepared;
}

std::shared_ptr<const PreparedGeometryFill>
PreparedRenderCache::prepared_geometry_fill(const Geometry& geometry) {
    auto seed = std::size_t{};
    hash_geometry(seed, geometry);
    auto canonical_key = geometry_cache_key(geometry);
    if (const auto iterator = prepared_geometry_fill_cache_.find(seed);
        iterator != prepared_geometry_fill_cache_.end()) {
        for (const auto& entry : iterator->second) {
            if (entry.prepared != nullptr && entry.canonical_key == canonical_key) {
                return entry.prepared;
            }
        }
    }

    auto prepared = std::make_shared<PreparedGeometryFill>(
        prepare_geometry_fill(geometry, prepared_geometry_flatten(geometry)));
    prepared_geometry_fill_cache_[seed].push_back(PreparedGeometryFillEntry{
        .canonical_key = std::move(canonical_key), .geometry = geometry, .prepared = prepared});
    trim_cache_entries(prepared_geometry_fill_cache_, max_prepared_geometry_cache_entries);
    return prepared;
}

std::shared_ptr<const PreparedGeometryStroke>
PreparedRenderCache::prepared_geometry_stroke(const Geometry& geometry) {
    auto seed = std::size_t{};
    hash_geometry(seed, geometry);
    auto canonical_key = geometry_cache_key(geometry);
    if (const auto iterator = prepared_geometry_stroke_cache_.find(seed);
        iterator != prepared_geometry_stroke_cache_.end()) {
        for (const auto& entry : iterator->second) {
            if (entry.prepared != nullptr && entry.canonical_key == canonical_key) {
                return entry.prepared;
            }
        }
    }

    auto prepared = std::make_shared<PreparedGeometryStroke>(
        prepare_geometry_stroke(prepared_geometry_flatten(geometry)));
    prepared_geometry_stroke_cache_[seed].push_back(PreparedGeometryStrokeEntry{
        .canonical_key = std::move(canonical_key), .geometry = geometry, .prepared = prepared});
    trim_cache_entries(prepared_geometry_stroke_cache_, max_prepared_geometry_cache_entries);
    return prepared;
}

std::shared_ptr<const PreparedTextGlyphCoverageList>
PreparedRenderCache::prepared_text_glyph_coverages(const TextLayout& layout) {
    return prepare_text_glyph_coverages(layout, prepared_text_glyph_cache_);
}

void PreparedRenderCache::merge(const PreparedRenderCache& other) {
    const auto merge_geometry_cache = [](auto& target_cache, const auto& source_cache) {
        constexpr auto linear_dedup_threshold = 8U;
        for (const auto& [hash, entries] : source_cache) {
            auto& target_entries = target_cache[hash];
            if (target_entries.empty()) {
                target_entries.insert(target_entries.end(), entries.begin(), entries.end());
                continue;
            }
            if (target_entries.size() + entries.size() <= linear_dedup_threshold) {
                for (const auto& entry : entries) {
                    const auto exists = std::any_of(
                        target_entries.begin(), target_entries.end(), [&](const auto& target) {
                            return target.canonical_key == entry.canonical_key;
                        });
                    if (!exists) {
                        target_entries.push_back(entry);
                    }
                }
                continue;
            }

            auto target_keys = std::unordered_set<std::string>{};
            target_keys.reserve(target_entries.size() + entries.size());
            for (const auto& target : target_entries) {
                target_keys.insert(target.canonical_key);
            }
            for (const auto& entry : entries) {
                if (target_keys.insert(entry.canonical_key).second) {
                    target_entries.push_back(entry);
                }
            }
        }
    };

    merge_geometry_cache(prepared_geometry_flatten_cache_, other.prepared_geometry_flatten_cache_);
    merge_geometry_cache(prepared_geometry_fill_cache_, other.prepared_geometry_fill_cache_);
    merge_geometry_cache(prepared_geometry_stroke_cache_, other.prepared_geometry_stroke_cache_);
    trim_cache_entries(prepared_geometry_flatten_cache_, max_prepared_geometry_cache_entries);
    trim_cache_entries(prepared_geometry_fill_cache_, max_prepared_geometry_cache_entries);
    trim_cache_entries(prepared_geometry_stroke_cache_, max_prepared_geometry_cache_entries);

    for (const auto& [hash, entries] : other.prepared_text_glyph_cache_) {
        auto& target_entries = prepared_text_glyph_cache_[hash];
        if (target_entries.empty()) {
            target_entries.insert(target_entries.end(), entries.begin(), entries.end());
            continue;
        }
        if (target_entries.size() + entries.size() <= 8U) {
            for (const auto& entry : entries) {
                if (entry == nullptr) {
                    continue;
                }
                const auto exists =
                    std::any_of(target_entries.begin(), target_entries.end(),
                                [&](const auto& target) {
                                    return target != nullptr &&
                                           same_prepared_text_key(*target, *entry);
                                });
                if (!exists) {
                    target_entries.push_back(entry);
                }
            }
            continue;
        }
        auto target_keys =
            std::unordered_set<const PreparedTextGlyphCoverage*, PreparedTextGlyphCoveragePtrHash,
                               PreparedTextGlyphCoveragePtrEqual>{};
        target_keys.reserve(target_entries.size() + entries.size());
        for (const auto& target : target_entries) {
            if (target != nullptr) {
                target_keys.insert(target.get());
            }
        }
        for (const auto& entry : entries) {
            if (entry == nullptr) {
                continue;
            }
            if (target_keys.insert(entry.get()).second) {
                target_entries.push_back(entry);
            }
        }
    }
    trim_cache_entries(prepared_text_glyph_cache_, max_prepared_text_glyph_cache_entries);
}

RenderCommandList::RenderCommandList() = default;

RenderCommandList::RenderCommandList(std::shared_ptr<PreparedRenderCache> prepared_cache)
    : prepared_cache_(std::move(prepared_cache)) {}

RenderCommandList::RenderCommandList(const RenderCommandList& other) {
    *this = other;
}

RenderCommandList& RenderCommandList::operator=(const RenderCommandList& other) {
    if (this == &other) {
        return *this;
    }

    prepared_cache_ = other.prepared_cache_;
    opcode_owner_.reset();
    opcodes_ = other.opcodes_;
    opcode_payload_indices_ = other.opcode_payload_indices_;
    push_clip_payloads_ = other.push_clip_payloads_;
    push_geometry_clip_payloads_ = other.push_geometry_clip_payloads_;
    push_layer_payloads_ = other.push_layer_payloads_;
    draw_line_payloads_ = other.draw_line_payloads_;
    fill_rect_payloads_ = other.fill_rect_payloads_;
    fill_pixel_snapped_rect_payloads_ = other.fill_pixel_snapped_rect_payloads_;
    stroke_pixel_snapped_rect_payloads_ = other.stroke_pixel_snapped_rect_payloads_;
    stroke_rect_payloads_ = other.stroke_rect_payloads_;
    fill_rounded_rect_payloads_ = other.fill_rounded_rect_payloads_;
    stroke_rounded_rect_payloads_ = other.stroke_rounded_rect_payloads_;
    fill_ellipse_payloads_ = other.fill_ellipse_payloads_;
    stroke_ellipse_payloads_ = other.stroke_ellipse_payloads_;
    fill_geometry_payloads_ = other.fill_geometry_payloads_;
    stroke_geometry_payloads_ = other.stroke_geometry_payloads_;
    draw_image_payloads_ = other.draw_image_payloads_;
    draw_text_payloads_ = other.draw_text_payloads_;
    draw_text_layout_payloads_ = other.draw_text_layout_payloads_;
    draw_box_shadow_payloads_ = other.draw_box_shadow_payloads_;
    text_parameters_ = other.text_parameters_;
    text_layout_parameters_ = other.text_layout_parameters_;
    bounds_ = other.bounds_;
    fingerprint_ = other.fingerprint_;
    change_signature_ = other.change_signature_;
    serialized_opcodes_cache_.clear();
    draw_batches_cache_.clear();
    serialized_opcodes_dirty_ = true;
    draw_batches_dirty_ = true;
    if (!opcodes_.empty()) {
        rebind_opcode_owners();
    }
    return *this;
}

RenderCommandList::RenderCommandList(RenderCommandList&& other) noexcept
    : prepared_cache_(std::move(other.prepared_cache_)), opcodes_(std::move(other.opcodes_)),
      opcode_payload_indices_(std::move(other.opcode_payload_indices_)),
      push_clip_payloads_(std::move(other.push_clip_payloads_)),
      push_geometry_clip_payloads_(std::move(other.push_geometry_clip_payloads_)),
      push_layer_payloads_(std::move(other.push_layer_payloads_)),
      draw_line_payloads_(std::move(other.draw_line_payloads_)),
      fill_rect_payloads_(std::move(other.fill_rect_payloads_)),
      fill_pixel_snapped_rect_payloads_(std::move(other.fill_pixel_snapped_rect_payloads_)),
      stroke_pixel_snapped_rect_payloads_(std::move(other.stroke_pixel_snapped_rect_payloads_)),
      stroke_rect_payloads_(std::move(other.stroke_rect_payloads_)),
      fill_rounded_rect_payloads_(std::move(other.fill_rounded_rect_payloads_)),
      stroke_rounded_rect_payloads_(std::move(other.stroke_rounded_rect_payloads_)),
      fill_ellipse_payloads_(std::move(other.fill_ellipse_payloads_)),
      stroke_ellipse_payloads_(std::move(other.stroke_ellipse_payloads_)),
      fill_geometry_payloads_(std::move(other.fill_geometry_payloads_)),
      stroke_geometry_payloads_(std::move(other.stroke_geometry_payloads_)),
      draw_image_payloads_(std::move(other.draw_image_payloads_)),
      draw_text_payloads_(std::move(other.draw_text_payloads_)),
      draw_text_layout_payloads_(std::move(other.draw_text_layout_payloads_)),
      draw_box_shadow_payloads_(std::move(other.draw_box_shadow_payloads_)),
      text_parameters_(std::move(other.text_parameters_)),
      text_layout_parameters_(std::move(other.text_layout_parameters_)), bounds_(other.bounds_),
      fingerprint_(other.fingerprint_), change_signature_(other.change_signature_),
      serialized_opcodes_cache_(std::move(other.serialized_opcodes_cache_)),
      draw_batches_cache_(std::move(other.draw_batches_cache_)),
      serialized_opcodes_dirty_(other.serialized_opcodes_dirty_),
      draw_batches_dirty_(other.draw_batches_dirty_) {
    opcode_owner_ = std::move(other.opcode_owner_);
    if (!opcodes_.empty()) {
        reset_opcode_owner();
    } else {
        opcode_owner_.reset();
    }
    other.opcode_owner_.reset();
    other.bounds_ = {};
    other.fingerprint_ = 0U;
    other.change_signature_ = 0U;
    other.invalidate_cached_views();
}

RenderCommandList& RenderCommandList::operator=(RenderCommandList&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    prepared_cache_ = std::move(other.prepared_cache_);
    opcode_owner_ = std::move(other.opcode_owner_);
    opcodes_ = std::move(other.opcodes_);
    opcode_payload_indices_ = std::move(other.opcode_payload_indices_);
    push_clip_payloads_ = std::move(other.push_clip_payloads_);
    push_geometry_clip_payloads_ = std::move(other.push_geometry_clip_payloads_);
    push_layer_payloads_ = std::move(other.push_layer_payloads_);
    draw_line_payloads_ = std::move(other.draw_line_payloads_);
    fill_rect_payloads_ = std::move(other.fill_rect_payloads_);
    fill_pixel_snapped_rect_payloads_ = std::move(other.fill_pixel_snapped_rect_payloads_);
    stroke_pixel_snapped_rect_payloads_ = std::move(other.stroke_pixel_snapped_rect_payloads_);
    stroke_rect_payloads_ = std::move(other.stroke_rect_payloads_);
    fill_rounded_rect_payloads_ = std::move(other.fill_rounded_rect_payloads_);
    stroke_rounded_rect_payloads_ = std::move(other.stroke_rounded_rect_payloads_);
    fill_ellipse_payloads_ = std::move(other.fill_ellipse_payloads_);
    stroke_ellipse_payloads_ = std::move(other.stroke_ellipse_payloads_);
    fill_geometry_payloads_ = std::move(other.fill_geometry_payloads_);
    stroke_geometry_payloads_ = std::move(other.stroke_geometry_payloads_);
    draw_image_payloads_ = std::move(other.draw_image_payloads_);
    draw_text_payloads_ = std::move(other.draw_text_payloads_);
    draw_text_layout_payloads_ = std::move(other.draw_text_layout_payloads_);
    draw_box_shadow_payloads_ = std::move(other.draw_box_shadow_payloads_);
    text_parameters_ = std::move(other.text_parameters_);
    text_layout_parameters_ = std::move(other.text_layout_parameters_);
    bounds_ = other.bounds_;
    fingerprint_ = other.fingerprint_;
    change_signature_ = other.change_signature_;
    serialized_opcodes_cache_ = std::move(other.serialized_opcodes_cache_);
    draw_batches_cache_ = std::move(other.draw_batches_cache_);
    serialized_opcodes_dirty_ = other.serialized_opcodes_dirty_;
    draw_batches_dirty_ = other.draw_batches_dirty_;
    if (!opcodes_.empty()) {
        reset_opcode_owner();
    } else {
        opcode_owner_.reset();
    }
    other.opcode_owner_.reset();
    other.bounds_ = {};
    other.fingerprint_ = 0U;
    other.change_signature_ = 0U;
    other.invalidate_cached_views();
    return *this;
}

void RenderCommandList::rebind_opcode_owners() noexcept {
    if (opcodes_.empty()) {
        opcode_owner_.reset();
        return;
    }
    reset_opcode_owner();
    for (auto& opcode : opcodes_) {
        opcode.owner = opcode_owner_.get();
    }
}

void RenderCommandList::reset_opcode_owner() noexcept {
    if (opcode_owner_ == nullptr) {
        opcode_owner_ = std::make_shared<RenderOpcodeOwner>();
    }
    opcode_owner_->list = this;
}

void RenderCommandList::invalidate_cached_views() const noexcept {
    serialized_opcodes_dirty_ = true;
    draw_batches_dirty_ = true;
}

RenderCommandList::CapacitySnapshot RenderCommandList::capacity_snapshot() const noexcept {
    return CapacitySnapshot{.opcodes = opcodes_.capacity(),
                            .opcode_payload_indices = opcode_payload_indices_.capacity(),
                            .push_clip_payloads = push_clip_payloads_.capacity(),
                            .push_geometry_clip_payloads =
                                push_geometry_clip_payloads_.capacity(),
                            .push_layer_payloads = push_layer_payloads_.capacity(),
                            .draw_line_payloads = draw_line_payloads_.capacity(),
                            .fill_rect_payloads = fill_rect_payloads_.capacity(),
                            .fill_pixel_snapped_rect_payloads =
                                fill_pixel_snapped_rect_payloads_.capacity(),
                            .stroke_pixel_snapped_rect_payloads =
                                stroke_pixel_snapped_rect_payloads_.capacity(),
                            .stroke_rect_payloads = stroke_rect_payloads_.capacity(),
                            .fill_rounded_rect_payloads =
                                fill_rounded_rect_payloads_.capacity(),
                            .stroke_rounded_rect_payloads =
                                stroke_rounded_rect_payloads_.capacity(),
                            .fill_ellipse_payloads = fill_ellipse_payloads_.capacity(),
                            .stroke_ellipse_payloads = stroke_ellipse_payloads_.capacity(),
                            .fill_geometry_payloads = fill_geometry_payloads_.capacity(),
                            .stroke_geometry_payloads = stroke_geometry_payloads_.capacity(),
                            .draw_image_payloads = draw_image_payloads_.capacity(),
                            .draw_text_payloads = draw_text_payloads_.capacity(),
                            .draw_text_layout_payloads =
                                draw_text_layout_payloads_.capacity(),
                            .draw_box_shadow_payloads = draw_box_shadow_payloads_.capacity(),
                            .text_parameters = text_parameters_.capacity(),
                            .text_layout_parameters = text_layout_parameters_.capacity()};
}

void RenderCommandList::reserve(CapacitySnapshot capacities) {
    opcodes_.reserve(capacities.opcodes);
    opcode_payload_indices_.reserve(capacities.opcode_payload_indices);
    push_clip_payloads_.reserve(capacities.push_clip_payloads);
    push_geometry_clip_payloads_.reserve(capacities.push_geometry_clip_payloads);
    push_layer_payloads_.reserve(capacities.push_layer_payloads);
    draw_line_payloads_.reserve(capacities.draw_line_payloads);
    fill_rect_payloads_.reserve(capacities.fill_rect_payloads);
    fill_pixel_snapped_rect_payloads_.reserve(capacities.fill_pixel_snapped_rect_payloads);
    stroke_pixel_snapped_rect_payloads_.reserve(capacities.stroke_pixel_snapped_rect_payloads);
    stroke_rect_payloads_.reserve(capacities.stroke_rect_payloads);
    fill_rounded_rect_payloads_.reserve(capacities.fill_rounded_rect_payloads);
    stroke_rounded_rect_payloads_.reserve(capacities.stroke_rounded_rect_payloads);
    fill_ellipse_payloads_.reserve(capacities.fill_ellipse_payloads);
    stroke_ellipse_payloads_.reserve(capacities.stroke_ellipse_payloads);
    fill_geometry_payloads_.reserve(capacities.fill_geometry_payloads);
    stroke_geometry_payloads_.reserve(capacities.stroke_geometry_payloads);
    draw_image_payloads_.reserve(capacities.draw_image_payloads);
    draw_text_payloads_.reserve(capacities.draw_text_payloads);
    draw_text_layout_payloads_.reserve(capacities.draw_text_layout_payloads);
    draw_box_shadow_payloads_.reserve(capacities.draw_box_shadow_payloads);
    text_parameters_.reserve(capacities.text_parameters);
    text_layout_parameters_.reserve(capacities.text_layout_parameters);
}

void RenderCommandList::reserve_for_append(const RenderCommandList& command_list) {
    opcodes_.reserve(opcodes_.size() + command_list.opcodes_.size());
    opcode_payload_indices_.reserve(opcode_payload_indices_.size() +
                                    command_list.opcode_payload_indices_.size());
    push_clip_payloads_.reserve(push_clip_payloads_.size() +
                                command_list.push_clip_payloads_.size());
    push_geometry_clip_payloads_.reserve(push_geometry_clip_payloads_.size() +
                                         command_list.push_geometry_clip_payloads_.size());
    push_layer_payloads_.reserve(push_layer_payloads_.size() +
                                 command_list.push_layer_payloads_.size());
    draw_line_payloads_.reserve(draw_line_payloads_.size() +
                                command_list.draw_line_payloads_.size());
    fill_rect_payloads_.reserve(fill_rect_payloads_.size() +
                                command_list.fill_rect_payloads_.size());
    fill_pixel_snapped_rect_payloads_.reserve(fill_pixel_snapped_rect_payloads_.size() +
                                              command_list.fill_pixel_snapped_rect_payloads_.size());
    stroke_pixel_snapped_rect_payloads_.reserve(
        stroke_pixel_snapped_rect_payloads_.size() +
        command_list.stroke_pixel_snapped_rect_payloads_.size());
    stroke_rect_payloads_.reserve(stroke_rect_payloads_.size() +
                                  command_list.stroke_rect_payloads_.size());
    fill_rounded_rect_payloads_.reserve(fill_rounded_rect_payloads_.size() +
                                        command_list.fill_rounded_rect_payloads_.size());
    stroke_rounded_rect_payloads_.reserve(stroke_rounded_rect_payloads_.size() +
                                          command_list.stroke_rounded_rect_payloads_.size());
    fill_ellipse_payloads_.reserve(fill_ellipse_payloads_.size() +
                                   command_list.fill_ellipse_payloads_.size());
    stroke_ellipse_payloads_.reserve(stroke_ellipse_payloads_.size() +
                                     command_list.stroke_ellipse_payloads_.size());
    fill_geometry_payloads_.reserve(fill_geometry_payloads_.size() +
                                    command_list.fill_geometry_payloads_.size());
    stroke_geometry_payloads_.reserve(stroke_geometry_payloads_.size() +
                                      command_list.stroke_geometry_payloads_.size());
    draw_image_payloads_.reserve(draw_image_payloads_.size() +
                                 command_list.draw_image_payloads_.size());
    draw_text_payloads_.reserve(draw_text_payloads_.size() +
                                command_list.draw_text_payloads_.size());
    draw_text_layout_payloads_.reserve(draw_text_layout_payloads_.size() +
                                       command_list.draw_text_layout_payloads_.size());
    draw_box_shadow_payloads_.reserve(draw_box_shadow_payloads_.size() +
                                      command_list.draw_box_shadow_payloads_.size());
    text_parameters_.reserve(text_parameters_.size() + command_list.text_parameters_.size());
    text_layout_parameters_.reserve(text_layout_parameters_.size() +
                                    command_list.text_layout_parameters_.size());
}

std::shared_ptr<const PreparedGeometryFlatten>
RenderCommandList::cached_prepared_geometry_flatten(const Geometry& geometry) {
    return ensure_prepared_cache().prepared_geometry_flatten(geometry);
}

std::shared_ptr<const PreparedGeometryFill>
RenderCommandList::cached_prepared_geometry_fill(const Geometry& geometry) {
    return ensure_prepared_cache().prepared_geometry_fill(geometry);
}

std::shared_ptr<const PreparedGeometryStroke>
RenderCommandList::cached_prepared_geometry_stroke(const Geometry& geometry) {
    return ensure_prepared_cache().prepared_geometry_stroke(geometry);
}

std::shared_ptr<const PreparedTextGlyphCoverageList>
RenderCommandList::cached_prepared_text_glyph_coverages(const TextLayout& layout) {
    return ensure_prepared_cache().prepared_text_glyph_coverages(layout);
}

PreparedRenderCache& RenderCommandList::ensure_prepared_cache() {
    if (prepared_cache_ == nullptr) {
        prepared_cache_ = std::make_shared<PreparedRenderCache>();
    }
    return *prepared_cache_;
}

RenderTextHandle RenderCommandList::store_text(std::shared_ptr<const std::string> text) {
    if (text == nullptr) {
        return {};
    }
    text_parameters_.push_back(std::move(text));
    return RenderTextHandle{static_cast<std::uint32_t>(text_parameters_.size())};
}

RenderTextLayoutHandle
RenderCommandList::store_text_layout(std::shared_ptr<const TextLayout> layout) {
    if (layout == nullptr) {
        return {};
    }
    text_layout_parameters_.push_back(std::move(layout));
    return RenderTextLayoutHandle{static_cast<std::uint32_t>(text_layout_parameters_.size())};
}

void RenderCommandList::append_opcode(RenderCommandType type, std::uint32_t payload_index,
                                      layout::Rect bounds, std::size_t payload_hash) {
    reset_opcode_owner();
    append_opcode_unchecked(type, payload_index, bounds, payload_hash);
    invalidate_cached_views();
}

void RenderCommandList::append_opcode_unchecked(RenderCommandType type,
                                                std::uint32_t payload_index,
                                                layout::Rect bounds,
                                                std::size_t payload_hash) {
    bounds_ = layout::union_rects(bounds_, bounds);
    auto seed = static_cast<std::size_t>(fingerprint_);
    hash_combine(seed, static_cast<int>(type));
    hash_rect(seed, bounds);
    hash_combine(seed, payload_hash);
    fingerprint_ = static_cast<std::uint64_t>(seed);
    auto signature_seed = static_cast<std::size_t>(change_signature_);
    hash_combine(signature_seed, opcodes_.size());
    hash_combine(signature_seed, static_cast<int>(type));
    hash_combine(signature_seed, payload_index);
    hash_rect(signature_seed, bounds);
    hash_combine(signature_seed, payload_hash);
    change_signature_ = static_cast<std::uint64_t>(signature_seed);
    opcodes_.push_back(RenderOpcodeRecord{
        .opcode = type,
        .payload_index = payload_index,
        .bounds = bounds,
        .owner = opcode_owner_.get()});
    opcode_payload_indices_.push_back(payload_index);
}

void RenderCommandList::append(SaveCommand command) {
    append_opcode(RenderCommandType::Save, 0U, visual_bounds(command),
                  payload_fingerprint(command));
}

void RenderCommandList::append(RestoreCommand command) {
    append_opcode(RenderCommandType::Restore, 0U, visual_bounds(command),
                  payload_fingerprint(command));
}

void RenderCommandList::append(PushClipCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(push_clip_payloads_.size());
    push_clip_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::PushClip, index, bounds, hash);
}

void RenderCommandList::append(PopClipCommand command) {
    append_opcode(RenderCommandType::PopClip, 0U, visual_bounds(command),
                  payload_fingerprint(command));
}

void RenderCommandList::append(PushGeometryClipCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    command.prepared_fill = cached_prepared_geometry_fill(command.geometry);
    const auto index = static_cast<std::uint32_t>(push_geometry_clip_payloads_.size());
    push_geometry_clip_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::PushGeometryClip, index, bounds, hash);
}

void RenderCommandList::append(PopGeometryClipCommand command) {
    append_opcode(RenderCommandType::PopGeometryClip, 0U, visual_bounds(command),
                  payload_fingerprint(command));
}

void RenderCommandList::append(PushLayerCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(push_layer_payloads_.size());
    push_layer_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::PushLayer, index, bounds, hash);
}

void RenderCommandList::append(PopLayerCommand command) {
    append_opcode(RenderCommandType::PopLayer, 0U, visual_bounds(command),
                  payload_fingerprint(command));
}

void RenderCommandList::append(DrawLineCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(draw_line_payloads_.size());
    draw_line_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::DrawLine, index, bounds, hash);
}

void RenderCommandList::append(FillRectCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(fill_rect_payloads_.size());
    fill_rect_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::FillRect, index, bounds, hash);
}

void RenderCommandList::append_fill_rects(std::span<const FillRectCommand> commands) {
    if (commands.empty()) {
        return;
    }
    reset_opcode_owner();
    fill_rect_payloads_.reserve(fill_rect_payloads_.size() + commands.size());
    opcodes_.reserve(opcodes_.size() + commands.size());
    opcode_payload_indices_.reserve(opcode_payload_indices_.size() + commands.size());

    auto block_bounds = layout::Rect{};
    auto fingerprint_seed = static_cast<std::size_t>(fingerprint_);
    auto signature_seed = static_cast<std::size_t>(change_signature_);
    auto payload_index = static_cast<std::uint32_t>(fill_rect_payloads_.size());
    for (const auto& command : commands) {
        const auto hash = payload_fingerprint(command);
        const auto bounds = visual_bounds(command);
        fill_rect_payloads_.push_back(command);

        hash_combine(fingerprint_seed, static_cast<int>(RenderCommandType::FillRect));
        hash_rect(fingerprint_seed, bounds);
        hash_combine(fingerprint_seed, hash);

        hash_combine(signature_seed, opcodes_.size());
        hash_combine(signature_seed, static_cast<int>(RenderCommandType::FillRect));
        hash_combine(signature_seed, payload_index);
        hash_rect(signature_seed, bounds);
        hash_combine(signature_seed, hash);

        block_bounds = layout::union_rects(block_bounds, bounds);
        opcodes_.push_back(RenderOpcodeRecord{.opcode = RenderCommandType::FillRect,
                                              .payload_index = payload_index,
                                              .bounds = bounds,
                                              .owner = opcode_owner_.get()});
        opcode_payload_indices_.push_back(payload_index);
        ++payload_index;
    }
    fingerprint_ = static_cast<std::uint64_t>(fingerprint_seed);
    change_signature_ = static_cast<std::uint64_t>(signature_seed);
    bounds_ = layout::union_rects(bounds_, block_bounds);
    invalidate_cached_views();
}

void RenderCommandList::append(FillPixelSnappedRectCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(fill_pixel_snapped_rect_payloads_.size());
    fill_pixel_snapped_rect_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::FillPixelSnappedRect, index, bounds, hash);
}

void RenderCommandList::append(StrokePixelSnappedRectCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(stroke_pixel_snapped_rect_payloads_.size());
    stroke_pixel_snapped_rect_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::StrokePixelSnappedRect, index, bounds, hash);
}

void RenderCommandList::append(StrokeRectCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(stroke_rect_payloads_.size());
    stroke_rect_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::StrokeRect, index, bounds, hash);
}

void RenderCommandList::append(FillRoundedRectCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(fill_rounded_rect_payloads_.size());
    fill_rounded_rect_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::FillRoundedRect, index, bounds, hash);
}

void RenderCommandList::append(StrokeRoundedRectCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(stroke_rounded_rect_payloads_.size());
    stroke_rounded_rect_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::StrokeRoundedRect, index, bounds, hash);
}

void RenderCommandList::append(FillEllipseCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(fill_ellipse_payloads_.size());
    fill_ellipse_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::FillEllipse, index, bounds, hash);
}

void RenderCommandList::append(StrokeEllipseCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(stroke_ellipse_payloads_.size());
    stroke_ellipse_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::StrokeEllipse, index, bounds, hash);
}

void RenderCommandList::append(FillGeometryCommand command) {
    const auto hash = payload_fingerprint(command);
    command.prepared_fill = cached_prepared_geometry_fill(command.geometry);
    const auto bounds =
        command.prepared_fill != nullptr ? command.prepared_fill->bounds : visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(fill_geometry_payloads_.size());
    fill_geometry_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::FillGeometry, index, bounds, hash);
}

void RenderCommandList::append(StrokeGeometryCommand command) {
    const auto hash = payload_fingerprint(command);
    command.prepared_stroke = cached_prepared_geometry_stroke(command.geometry);
    const auto prepared_bounds =
        command.prepared_stroke != nullptr ? command.prepared_stroke->bounds
                                           : geometry_bounds(command.geometry);
    const auto bounds = layout::inflate_rect(prepared_bounds,
                                             std::max(command.style.width, 0.0F) * 0.5F);
    const auto index = static_cast<std::uint32_t>(stroke_geometry_payloads_.size());
    stroke_geometry_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::StrokeGeometry, index, bounds, hash);
}

void RenderCommandList::append(DrawImageCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(draw_image_payloads_.size());
    draw_image_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::DrawImage, index, bounds, hash);
}

void RenderCommandList::append_draw_images(std::span<const DrawImageCommand> commands) {
    if (commands.empty()) {
        return;
    }
    reset_opcode_owner();
    draw_image_payloads_.reserve(draw_image_payloads_.size() + commands.size());
    opcodes_.reserve(opcodes_.size() + commands.size());
    opcode_payload_indices_.reserve(opcode_payload_indices_.size() + commands.size());

    auto block_bounds = layout::Rect{};
    auto fingerprint_seed = static_cast<std::size_t>(fingerprint_);
    auto signature_seed = static_cast<std::size_t>(change_signature_);
    auto payload_index = static_cast<std::uint32_t>(draw_image_payloads_.size());
    for (const auto& command : commands) {
        const auto hash = payload_fingerprint(command);
        const auto bounds = visual_bounds(command);
        draw_image_payloads_.push_back(command);

        hash_combine(fingerprint_seed, static_cast<int>(RenderCommandType::DrawImage));
        hash_rect(fingerprint_seed, bounds);
        hash_combine(fingerprint_seed, hash);

        hash_combine(signature_seed, opcodes_.size());
        hash_combine(signature_seed, static_cast<int>(RenderCommandType::DrawImage));
        hash_combine(signature_seed, payload_index);
        hash_rect(signature_seed, bounds);
        hash_combine(signature_seed, hash);

        block_bounds = layout::union_rects(block_bounds, bounds);
        opcodes_.push_back(RenderOpcodeRecord{.opcode = RenderCommandType::DrawImage,
                                              .payload_index = payload_index,
                                              .bounds = bounds,
                                              .owner = opcode_owner_.get()});
        opcode_payload_indices_.push_back(payload_index);
        ++payload_index;
    }
    fingerprint_ = static_cast<std::uint64_t>(fingerprint_seed);
    change_signature_ = static_cast<std::uint64_t>(signature_seed);
    bounds_ = layout::union_rects(bounds_, block_bounds);
    invalidate_cached_views();
}

void RenderCommandList::append(DrawTextCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(draw_text_payloads_.size());
    draw_text_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::DrawText, index, bounds, hash);
}

void RenderCommandList::append(DrawTextLayoutCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    if (const auto* layout = command.layout_value()) {
        command.prepared_glyphs = cached_prepared_text_glyph_coverages(*layout);
    }
    const auto index = static_cast<std::uint32_t>(draw_text_layout_payloads_.size());
    draw_text_layout_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::DrawTextLayout, index, bounds, hash);
}

void RenderCommandList::append(DrawBoxShadowCommand command) {
    const auto hash = payload_fingerprint(command);
    const auto bounds = visual_bounds(command);
    const auto index = static_cast<std::uint32_t>(draw_box_shadow_payloads_.size());
    draw_box_shadow_payloads_.push_back(std::move(command));
    append_opcode(RenderCommandType::DrawBoxShadow, index, bounds, hash);
}

void RenderCommandList::append(const RenderCommandList& command_list) {
    if (command_list.empty()) {
        return;
    }
    reset_opcode_owner();

    if (prepared_cache_ != command_list.prepared_cache_ &&
        command_list.prepared_cache_ != nullptr) {
        ensure_prepared_cache().merge(*command_list.prepared_cache_);
    }

    reserve_for_append(command_list);

    const auto push_clip_offset = static_cast<std::uint32_t>(push_clip_payloads_.size());
    const auto push_geometry_clip_offset =
        static_cast<std::uint32_t>(push_geometry_clip_payloads_.size());
    const auto push_layer_offset = static_cast<std::uint32_t>(push_layer_payloads_.size());
    const auto draw_line_offset = static_cast<std::uint32_t>(draw_line_payloads_.size());
    const auto fill_rect_offset = static_cast<std::uint32_t>(fill_rect_payloads_.size());
    const auto fill_pixel_snapped_rect_offset =
        static_cast<std::uint32_t>(fill_pixel_snapped_rect_payloads_.size());
    const auto stroke_pixel_snapped_rect_offset =
        static_cast<std::uint32_t>(stroke_pixel_snapped_rect_payloads_.size());
    const auto stroke_rect_offset = static_cast<std::uint32_t>(stroke_rect_payloads_.size());
    const auto fill_rounded_rect_offset =
        static_cast<std::uint32_t>(fill_rounded_rect_payloads_.size());
    const auto stroke_rounded_rect_offset =
        static_cast<std::uint32_t>(stroke_rounded_rect_payloads_.size());
    const auto fill_ellipse_offset = static_cast<std::uint32_t>(fill_ellipse_payloads_.size());
    const auto stroke_ellipse_offset = static_cast<std::uint32_t>(stroke_ellipse_payloads_.size());
    const auto fill_geometry_offset = static_cast<std::uint32_t>(fill_geometry_payloads_.size());
    const auto stroke_geometry_offset =
        static_cast<std::uint32_t>(stroke_geometry_payloads_.size());
    const auto draw_image_offset = static_cast<std::uint32_t>(draw_image_payloads_.size());
    const auto draw_text_offset = static_cast<std::uint32_t>(draw_text_payloads_.size());
    const auto draw_text_layout_offset =
        static_cast<std::uint32_t>(draw_text_layout_payloads_.size());
    const auto draw_box_shadow_offset =
        static_cast<std::uint32_t>(draw_box_shadow_payloads_.size());
    const auto text_parameter_offset = static_cast<std::uint32_t>(text_parameters_.size());
    const auto text_layout_parameter_offset =
        static_cast<std::uint32_t>(text_layout_parameters_.size());

    push_clip_payloads_.insert(push_clip_payloads_.end(), command_list.push_clip_payloads_.begin(),
                               command_list.push_clip_payloads_.end());
    push_geometry_clip_payloads_.insert(push_geometry_clip_payloads_.end(),
                                        command_list.push_geometry_clip_payloads_.begin(),
                                        command_list.push_geometry_clip_payloads_.end());
    push_layer_payloads_.insert(push_layer_payloads_.end(),
                                command_list.push_layer_payloads_.begin(),
                                command_list.push_layer_payloads_.end());
    draw_line_payloads_.insert(draw_line_payloads_.end(), command_list.draw_line_payloads_.begin(),
                               command_list.draw_line_payloads_.end());
    fill_rect_payloads_.insert(fill_rect_payloads_.end(), command_list.fill_rect_payloads_.begin(),
                               command_list.fill_rect_payloads_.end());
    fill_pixel_snapped_rect_payloads_.insert(fill_pixel_snapped_rect_payloads_.end(),
                                             command_list.fill_pixel_snapped_rect_payloads_.begin(),
                                             command_list.fill_pixel_snapped_rect_payloads_.end());
    stroke_pixel_snapped_rect_payloads_.insert(
        stroke_pixel_snapped_rect_payloads_.end(),
        command_list.stroke_pixel_snapped_rect_payloads_.begin(),
        command_list.stroke_pixel_snapped_rect_payloads_.end());
    stroke_rect_payloads_.insert(stroke_rect_payloads_.end(),
                                 command_list.stroke_rect_payloads_.begin(),
                                 command_list.stroke_rect_payloads_.end());
    fill_rounded_rect_payloads_.insert(fill_rounded_rect_payloads_.end(),
                                       command_list.fill_rounded_rect_payloads_.begin(),
                                       command_list.fill_rounded_rect_payloads_.end());
    stroke_rounded_rect_payloads_.insert(stroke_rounded_rect_payloads_.end(),
                                         command_list.stroke_rounded_rect_payloads_.begin(),
                                         command_list.stroke_rounded_rect_payloads_.end());
    fill_ellipse_payloads_.insert(fill_ellipse_payloads_.end(),
                                  command_list.fill_ellipse_payloads_.begin(),
                                  command_list.fill_ellipse_payloads_.end());
    stroke_ellipse_payloads_.insert(stroke_ellipse_payloads_.end(),
                                    command_list.stroke_ellipse_payloads_.begin(),
                                    command_list.stroke_ellipse_payloads_.end());
    fill_geometry_payloads_.insert(fill_geometry_payloads_.end(),
                                   command_list.fill_geometry_payloads_.begin(),
                                   command_list.fill_geometry_payloads_.end());
    stroke_geometry_payloads_.insert(stroke_geometry_payloads_.end(),
                                     command_list.stroke_geometry_payloads_.begin(),
                                     command_list.stroke_geometry_payloads_.end());
    draw_image_payloads_.insert(draw_image_payloads_.end(),
                                command_list.draw_image_payloads_.begin(),
                                command_list.draw_image_payloads_.end());
    draw_text_payloads_.insert(draw_text_payloads_.end(), command_list.draw_text_payloads_.begin(),
                               command_list.draw_text_payloads_.end());
    draw_text_layout_payloads_.insert(draw_text_layout_payloads_.end(),
                                      command_list.draw_text_layout_payloads_.begin(),
                                      command_list.draw_text_layout_payloads_.end());
    draw_box_shadow_payloads_.insert(draw_box_shadow_payloads_.end(),
                                     command_list.draw_box_shadow_payloads_.begin(),
                                     command_list.draw_box_shadow_payloads_.end());
    text_parameters_.insert(text_parameters_.end(), command_list.text_parameters_.begin(),
                            command_list.text_parameters_.end());
    text_layout_parameters_.insert(text_layout_parameters_.end(),
                                   command_list.text_layout_parameters_.begin(),
                                   command_list.text_layout_parameters_.end());

    for (auto index = static_cast<std::size_t>(draw_text_offset);
         index < draw_text_payloads_.size(); ++index) {
        auto& payload = draw_text_payloads_[index];
        if (payload.text_handle.value > 0U) {
            payload.text_handle.value += text_parameter_offset;
            payload.text = text_parameter(payload.text_handle);
        }
    }
    for (auto index = static_cast<std::size_t>(draw_text_layout_offset);
         index < draw_text_layout_payloads_.size(); ++index) {
        auto& payload = draw_text_layout_payloads_[index];
        if (payload.layout_handle.value > 0U) {
            payload.layout_handle.value += text_layout_parameter_offset;
            payload.layout = text_layout_parameter(payload.layout_handle);
        }
    }

    const auto offset_for = [&](RenderCommandType type) noexcept -> std::uint32_t {
        switch (type) {
        case RenderCommandType::PushClip:
            return push_clip_offset;
        case RenderCommandType::PushGeometryClip:
            return push_geometry_clip_offset;
        case RenderCommandType::PushLayer:
            return push_layer_offset;
        case RenderCommandType::DrawLine:
            return draw_line_offset;
        case RenderCommandType::FillRect:
            return fill_rect_offset;
        case RenderCommandType::FillPixelSnappedRect:
            return fill_pixel_snapped_rect_offset;
        case RenderCommandType::StrokePixelSnappedRect:
            return stroke_pixel_snapped_rect_offset;
        case RenderCommandType::StrokeRect:
            return stroke_rect_offset;
        case RenderCommandType::FillRoundedRect:
            return fill_rounded_rect_offset;
        case RenderCommandType::StrokeRoundedRect:
            return stroke_rounded_rect_offset;
        case RenderCommandType::FillEllipse:
            return fill_ellipse_offset;
        case RenderCommandType::StrokeEllipse:
            return stroke_ellipse_offset;
        case RenderCommandType::FillGeometry:
            return fill_geometry_offset;
        case RenderCommandType::StrokeGeometry:
            return stroke_geometry_offset;
        case RenderCommandType::DrawImage:
            return draw_image_offset;
        case RenderCommandType::DrawText:
            return draw_text_offset;
        case RenderCommandType::DrawTextLayout:
            return draw_text_layout_offset;
        case RenderCommandType::DrawBoxShadow:
            return draw_box_shadow_offset;
        case RenderCommandType::Save:
        case RenderCommandType::Restore:
        case RenderCommandType::PopClip:
        case RenderCommandType::PopGeometryClip:
        case RenderCommandType::PopLayer:
        default:
            return 0U;
        }
    };

    auto seed = static_cast<std::size_t>(fingerprint_);
    hash_combine(seed, command_list.fingerprint_);
    hash_combine(seed, command_list.opcodes_.size());
    fingerprint_ = static_cast<std::uint64_t>(seed);
    auto signature_seed = static_cast<std::size_t>(change_signature_);
    hash_combine(signature_seed, command_list.change_signature_);
    hash_combine(signature_seed, command_list.opcodes_.size());
    hash_combine(signature_seed, command_list.bounds_.x);
    hash_combine(signature_seed, command_list.bounds_.y);
    hash_combine(signature_seed, command_list.bounds_.width);
    hash_combine(signature_seed, command_list.bounds_.height);
    change_signature_ = static_cast<std::uint64_t>(signature_seed);
    for (auto opcode : command_list.opcodes_) {
        opcode.payload_index += offset_for(opcode.opcode);
        opcode.owner = opcode_owner_.get();
        opcodes_.push_back(opcode);
        opcode_payload_indices_.push_back(opcode.payload_index);
    }
    bounds_ = layout::union_rects(bounds_, command_list.bounds_);
    invalidate_cached_views();
}

void RenderCommandList::append(RenderCommandList&& command_list) {
    if (command_list.empty()) {
        return;
    }
    if (empty() && (prepared_cache_ == command_list.prepared_cache_ ||
                    command_list.prepared_cache_ == nullptr)) {
        *this = std::move(command_list);
        return;
    }
    append(static_cast<const RenderCommandList&>(command_list));
}

bool RenderCommandList::empty() const noexcept {
    return opcodes_.empty();
}

std::size_t RenderCommandList::command_count() const noexcept {
    return opcodes_.size();
}

const std::vector<RenderOpcodeRecord>& RenderCommandList::commands() const noexcept {
    return opcodes_;
}

const std::vector<RenderOpcodeRecord>& RenderCommandList::opcodes() const noexcept {
    return opcodes_;
}

const std::vector<std::uint32_t>& RenderCommandList::opcode_payload_indices() const noexcept {
    return opcode_payload_indices_;
}

const std::vector<PushClipCommand>& RenderCommandList::push_clip_payloads() const noexcept {
    return push_clip_payloads_;
}

const std::vector<PushGeometryClipCommand>&
RenderCommandList::push_geometry_clip_payloads() const noexcept {
    return push_geometry_clip_payloads_;
}

const std::vector<PushLayerCommand>& RenderCommandList::push_layer_payloads() const noexcept {
    return push_layer_payloads_;
}

const std::vector<DrawLineCommand>& RenderCommandList::draw_line_payloads() const noexcept {
    return draw_line_payloads_;
}

const std::vector<FillRectCommand>& RenderCommandList::fill_rect_payloads() const noexcept {
    return fill_rect_payloads_;
}

const std::vector<FillPixelSnappedRectCommand>&
RenderCommandList::fill_pixel_snapped_rect_payloads() const noexcept {
    return fill_pixel_snapped_rect_payloads_;
}

const std::vector<StrokePixelSnappedRectCommand>&
RenderCommandList::stroke_pixel_snapped_rect_payloads() const noexcept {
    return stroke_pixel_snapped_rect_payloads_;
}

const std::vector<StrokeRectCommand>& RenderCommandList::stroke_rect_payloads() const noexcept {
    return stroke_rect_payloads_;
}

const std::vector<FillRoundedRectCommand>&
RenderCommandList::fill_rounded_rect_payloads() const noexcept {
    return fill_rounded_rect_payloads_;
}

const std::vector<StrokeRoundedRectCommand>&
RenderCommandList::stroke_rounded_rect_payloads() const noexcept {
    return stroke_rounded_rect_payloads_;
}

const std::vector<FillEllipseCommand>& RenderCommandList::fill_ellipse_payloads() const noexcept {
    return fill_ellipse_payloads_;
}

const std::vector<StrokeEllipseCommand>&
RenderCommandList::stroke_ellipse_payloads() const noexcept {
    return stroke_ellipse_payloads_;
}

const std::vector<FillGeometryCommand>& RenderCommandList::fill_geometry_payloads() const noexcept {
    return fill_geometry_payloads_;
}

const std::vector<StrokeGeometryCommand>&
RenderCommandList::stroke_geometry_payloads() const noexcept {
    return stroke_geometry_payloads_;
}

const std::vector<DrawImageCommand>& RenderCommandList::draw_image_payloads() const noexcept {
    return draw_image_payloads_;
}

const std::vector<DrawTextCommand>& RenderCommandList::draw_text_payloads() const noexcept {
    return draw_text_payloads_;
}

const std::vector<DrawTextLayoutCommand>&
RenderCommandList::draw_text_layout_payloads() const noexcept {
    return draw_text_layout_payloads_;
}

const std::vector<DrawBoxShadowCommand>&
RenderCommandList::draw_box_shadow_payloads() const noexcept {
    return draw_box_shadow_payloads_;
}

const std::vector<std::shared_ptr<const std::string>>&
RenderCommandList::text_parameters() const noexcept {
    return text_parameters_;
}

const std::vector<std::shared_ptr<const TextLayout>>&
RenderCommandList::text_layout_parameters() const noexcept {
    return text_layout_parameters_;
}

std::string_view RenderCommandList::text_parameter(RenderTextHandle handle) const noexcept {
    if (handle.value == 0U || handle.value > text_parameters_.size()) {
        return {};
    }
    const auto& text = text_parameters_[handle.value - 1U];
    return text == nullptr ? std::string_view{} : std::string_view{*text};
}

const TextLayout*
RenderCommandList::text_layout_parameter(RenderTextLayoutHandle handle) const noexcept {
    if (handle.value == 0U || handle.value > text_layout_parameters_.size()) {
        return nullptr;
    }
    return text_layout_parameters_[handle.value - 1U].get();
}

std::vector<std::byte> RenderCommandList::serialized_opcodes() const {
    if (!serialized_opcodes_dirty_) {
        return serialized_opcodes_cache_;
    }

    auto bytes = std::vector<std::byte>{};
    bytes.reserve(opcodes_.size() * (sizeof(std::uint32_t) * 2U + sizeof(float) * 4U));
    for (const auto& opcode : opcodes_) {
        append_u32(bytes, static_cast<std::uint32_t>(opcode.opcode));
        append_u32(bytes, opcode.payload_index);
        append_float(bytes, opcode.bounds.x);
        append_float(bytes, opcode.bounds.y);
        append_float(bytes, opcode.bounds.width);
        append_float(bytes, opcode.bounds.height);
    }
    serialized_opcodes_cache_ = std::move(bytes);
    serialized_opcodes_dirty_ = false;
    return serialized_opcodes_cache_;
}

std::vector<RenderDrawBatch> RenderCommandList::draw_batches() const {
    if (!draw_batches_dirty_) {
        return draw_batches_cache_;
    }

    auto result = std::vector<RenderDrawBatch>{};
    result.reserve(opcodes_.size());
    auto current_key = DrawBatchKey{};
    auto have_batch = false;
    for (const auto& opcode : opcodes_) {
        const auto key = draw_batch_key_for(*this, opcode);
        if (!have_batch || !(key == current_key)) {
            result.push_back(RenderDrawBatch{.kind = key.kind,
                                             .first_command_type = key.first_command_type,
                                             .bounds = opcode.bounds,
                                             .state_key = key.state_key,
                                             .resource_key = key.resource_key,
                                             .command_count = 1U});
            current_key = key;
            have_batch = true;
            continue;
        }
        auto& batch = result.back();
        ++batch.command_count;
        batch.bounds = layout::union_rects(batch.bounds, opcode.bounds);
    }
    draw_batches_cache_ = std::move(result);
    draw_batches_dirty_ = false;
    return draw_batches_cache_;
}

layout::Rect RenderCommandList::bounds() const noexcept {
    return bounds_;
}

std::uint64_t RenderCommandList::fingerprint() const noexcept {
    return fingerprint_;
}

std::uint64_t RenderCommandList::change_signature() const noexcept {
    return change_signature_;
}

bool RenderCommandList::is_equivalent_to(const RenderCommandList& other) const noexcept {
    return opcodes_.size() == other.opcodes_.size() && fingerprint() == other.fingerprint() &&
           change_signature() == other.change_signature() && bounds_ == other.bounds_ &&
           push_clip_payloads_.size() == other.push_clip_payloads_.size() &&
           push_geometry_clip_payloads_.size() == other.push_geometry_clip_payloads_.size() &&
           push_layer_payloads_.size() == other.push_layer_payloads_.size() &&
           draw_line_payloads_.size() == other.draw_line_payloads_.size() &&
           fill_rect_payloads_.size() == other.fill_rect_payloads_.size() &&
           fill_pixel_snapped_rect_payloads_.size() ==
               other.fill_pixel_snapped_rect_payloads_.size() &&
           stroke_pixel_snapped_rect_payloads_.size() ==
               other.stroke_pixel_snapped_rect_payloads_.size() &&
           stroke_rect_payloads_.size() == other.stroke_rect_payloads_.size() &&
           fill_rounded_rect_payloads_.size() == other.fill_rounded_rect_payloads_.size() &&
           stroke_rounded_rect_payloads_.size() == other.stroke_rounded_rect_payloads_.size() &&
           fill_ellipse_payloads_.size() == other.fill_ellipse_payloads_.size() &&
           stroke_ellipse_payloads_.size() == other.stroke_ellipse_payloads_.size() &&
           fill_geometry_payloads_.size() == other.fill_geometry_payloads_.size() &&
           stroke_geometry_payloads_.size() == other.stroke_geometry_payloads_.size() &&
           draw_image_payloads_.size() == other.draw_image_payloads_.size() &&
           draw_text_payloads_.size() == other.draw_text_payloads_.size() &&
           draw_text_layout_payloads_.size() == other.draw_text_layout_payloads_.size() &&
           draw_box_shadow_payloads_.size() == other.draw_box_shadow_payloads_.size() &&
           text_parameters_.size() == other.text_parameters_.size() &&
           text_layout_parameters_.size() == other.text_layout_parameters_.size();
}

std::shared_ptr<PreparedRenderCache> RenderCommandList::prepared_cache() const noexcept {
    return prepared_cache_;
}

RenderCommandRecorder::RenderCommandRecorder(std::shared_ptr<PreparedRenderCache> prepared_cache)
    : command_list_(std::move(prepared_cache)) {}

std::shared_ptr<const std::string> RenderCommandRecorder::intern_text(std::string_view text) {
    auto seed = std::size_t{};
    hash_combine(seed, text);
    auto& bucket = text_pool_[seed];
    for (const auto& existing : bucket) {
        if (existing != nullptr && *existing == text) {
            return existing;
        }
    }
    auto storage = std::make_shared<const std::string>(text);
    bucket.push_back(storage);
    return storage;
}

std::shared_ptr<const TextLayout> RenderCommandRecorder::share_text_layout(const TextLayout& layout) {
    return std::make_shared<const TextLayout>(layout);
}

void RenderCommandRecorder::flush_pending_saves() {
    while (pending_save_depth_ > 0U) {
        command_list_.append(SaveCommand{});
        --pending_save_depth_;
    }
}

void RenderCommandRecorder::prune_text_pool() {
    constexpr auto max_retained_text_buckets = 512U;
    if (text_pool_.size() <= max_retained_text_buckets) {
        return;
    }
    for (auto iterator = text_pool_.begin(); iterator != text_pool_.end();) {
        auto& bucket = iterator->second;
        bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                    [](const auto& text) {
                                        return text == nullptr || text.use_count() == 1L;
                                    }),
                     bucket.end());
        if (bucket.empty()) {
            iterator = text_pool_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    if (text_pool_.size() > max_retained_text_buckets * 2U) {
        text_pool_.clear();
    }
}

void RenderCommandRecorder::save() {
    ++pending_save_depth_;
}

void RenderCommandRecorder::restore() {
    if (pending_save_depth_ > 0U) {
        --pending_save_depth_;
        return;
    }
    command_list_.append(RestoreCommand{});
}

void RenderCommandRecorder::push_clip(layout::Rect rect) {
    flush_pending_saves();
    command_list_.append(PushClipCommand{.rect = rect});
}

void RenderCommandRecorder::pop_clip() {
    flush_pending_saves();
    command_list_.append(PopClipCommand{});
}

void RenderCommandRecorder::push_geometry_clip(const Geometry& geometry) {
    flush_pending_saves();
    command_list_.append(PushGeometryClipCommand{.geometry = geometry});
}

void RenderCommandRecorder::pop_geometry_clip() {
    flush_pending_saves();
    command_list_.append(PopGeometryClipCommand{});
}

void RenderCommandRecorder::push_layer(const RenderLayerOptions& options) {
    flush_pending_saves();
    command_list_.append(PushLayerCommand{.options = options});
}

void RenderCommandRecorder::pop_layer() {
    flush_pending_saves();
    command_list_.append(PopLayerCommand{});
}

void RenderCommandRecorder::draw_line(layout::Point start, layout::Point end, Color color,
                                      float stroke_width) {
    flush_pending_saves();
    command_list_.append(
        DrawLineCommand{.start = start, .end = end, .color = color, .stroke_width = stroke_width});
}

void RenderCommandRecorder::fill_rect(layout::Rect rect, Color color) {
    flush_pending_saves();
    command_list_.append(FillRectCommand{.rect = rect, .color = color});
}

void RenderCommandRecorder::fill_rects(std::span<const FillRectCommand> commands) {
    flush_pending_saves();
    command_list_.append_fill_rects(commands);
}

void RenderCommandRecorder::fill_pixel_snapped_rect(layout::Rect rect, Color color) {
    flush_pending_saves();
    command_list_.append(FillPixelSnappedRectCommand{.rect = rect, .color = color});
}

void RenderCommandRecorder::stroke_pixel_snapped_rect(layout::Rect rect, Color color,
                                                      float stroke_width) {
    flush_pending_saves();
    command_list_.append(
        StrokePixelSnappedRectCommand{.rect = rect, .color = color, .stroke_width = stroke_width});
}

void RenderCommandRecorder::stroke_rect(layout::Rect rect, Color color, float stroke_width) {
    flush_pending_saves();
    command_list_.append(
        StrokeRectCommand{.rect = rect, .color = color, .stroke_width = stroke_width});
}

void RenderCommandRecorder::fill_rounded_rect(layout::Rect rect, CornerRadius radius, Color color) {
    flush_pending_saves();
    command_list_.append(FillRoundedRectCommand{.rect = rect, .radius = radius, .color = color});
}

void RenderCommandRecorder::stroke_rounded_rect(layout::Rect rect, CornerRadius radius, Color color,
                                                float stroke_width) {
    flush_pending_saves();
    command_list_.append(StrokeRoundedRectCommand{
        .rect = rect, .radius = radius, .color = color, .stroke_width = stroke_width});
}

void RenderCommandRecorder::fill_ellipse(layout::Rect rect, Color color) {
    flush_pending_saves();
    command_list_.append(FillEllipseCommand{.rect = rect, .color = color});
}

void RenderCommandRecorder::stroke_ellipse(layout::Rect rect, Color color, float stroke_width) {
    flush_pending_saves();
    command_list_.append(
        StrokeEllipseCommand{.rect = rect, .color = color, .stroke_width = stroke_width});
}

void RenderCommandRecorder::fill_geometry(const Geometry& geometry, Color color) {
    flush_pending_saves();
    command_list_.append(FillGeometryCommand{.geometry = geometry, .color = color});
}

void RenderCommandRecorder::stroke_geometry(const Geometry& geometry, Color color,
                                            const GeometryStrokeStyle& style) {
    flush_pending_saves();
    command_list_.append(
        StrokeGeometryCommand{.geometry = geometry, .color = color, .style = style});
}

void RenderCommandRecorder::draw_image(RenderResourceId resource_id,
                                       const RenderImageOptions& options) {
    flush_pending_saves();
    command_list_.append(DrawImageCommand{.resource_id = resource_id, .options = options});
}

void RenderCommandRecorder::draw_images(std::span<const DrawImageCommand> commands) {
    flush_pending_saves();
    command_list_.append_draw_images(commands);
}

void RenderCommandRecorder::draw_box_shadow(layout::Rect rect, const ShadowStyle& style) {
    flush_pending_saves();
    command_list_.append(DrawBoxShadowCommand{.rect = rect, .style = style});
}

void RenderCommandRecorder::draw_text(std::string_view text, layout::Rect rect,
                                      const TextStyle& style) {
    flush_pending_saves();
    auto storage = intern_text(text);
    const auto handle = command_list_.store_text(storage);
    command_list_.append(
        DrawTextCommand{.text_handle = handle, .text = *storage, .rect = rect, .style = style});
}

void RenderCommandRecorder::draw_text_layout(const TextLayout& layout, layout::Point origin) {
    flush_pending_saves();
    auto storage = share_text_layout(layout);
    const auto handle = command_list_.store_text_layout(storage);
    command_list_.append(DrawTextLayoutCommand{.layout_handle = handle,
                                               .layout = storage.get(),
                                               .origin = origin});
}

const RenderCommandList& RenderCommandRecorder::command_list() const noexcept {
    return command_list_;
}

const std::vector<RenderOpcodeRecord>& RenderCommandRecorder::commands() const noexcept {
    return command_list_.opcodes();
}

RenderCommandList RenderCommandRecorder::take_command_list() noexcept {
    flush_pending_saves();
    auto prepared_cache = command_list_.prepared_cache();
    const auto capacities = command_list_.capacity_snapshot();
    auto command_list = std::move(command_list_);
    command_list_ = RenderCommandList{std::move(prepared_cache)};
    command_list_.reserve(capacities);
    prune_text_pool();
    return command_list;
}

void RenderCommandRecorder::append(const RenderCommandList& command_list) {
    flush_pending_saves();
    command_list_.append(command_list);
}

void RenderCommandRecorder::append(RenderCommandList&& command_list) {
    flush_pending_saves();
    command_list_.append(std::move(command_list));
}

} // namespace winelement::rendering
