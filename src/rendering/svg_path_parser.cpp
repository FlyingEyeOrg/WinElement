#include <winelement/rendering/svg_path_parser.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <vector>

namespace winelement::rendering {
namespace {

// ---- SVG path parsing utilities ----

struct Cursor {
    std::string_view input;
    std::size_t pos = 0;
};

[[nodiscard]] char peek(const Cursor& c) noexcept {
    return c.pos < c.input.size() ? c.input[c.pos] : '\0';
}

void advance(Cursor& c) noexcept {
    if (c.pos < c.input.size()) {
        ++c.pos;
    }
}

void skip_whitespace_and_commas(Cursor& c) noexcept {
    while (c.pos < c.input.size()) {
        const auto ch = c.input[c.pos];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ',') {
            ++c.pos;
        } else {
            break;
        }
    }
}

[[nodiscard]] std::optional<float> parse_number(Cursor& c) noexcept {
    skip_whitespace_and_commas(c);
    if (c.pos >= c.input.size()) {
        return std::nullopt;
    }
    const auto* start = c.input.data() + c.pos;
    char* end = nullptr;
    const double value = std::strtod(start, &end);
    if (end == start) {
        return std::nullopt;
    }
    c.pos += static_cast<std::size_t>(end - start);
    return static_cast<float>(value);
}

struct Point {
    float x = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] std::optional<Point> parse_coord_pair(Cursor& c) noexcept {
    const auto x = parse_number(c);
    if (!x) {
        return std::nullopt;
    }
    skip_whitespace_and_commas(c);
    const auto y = parse_number(c);
    if (!y) {
        return std::nullopt;
    }
    return Point{*x, *y};
}

[[nodiscard]] char consume_command(Cursor& c) noexcept {
    skip_whitespace_and_commas(c);
    const auto ch = peek(c);
    if (ch == '\0') {
        return '\0';
    }
    advance(c);
    return ch;
}

[[nodiscard]] bool is_command_char(char ch) noexcept {
    return ch == 'M' || ch == 'm' || ch == 'L' || ch == 'l' || ch == 'H' || ch == 'h' ||
           ch == 'V' || ch == 'v' || ch == 'C' || ch == 'c' || ch == 'S' || ch == 's' ||
           ch == 'Q' || ch == 'q' || ch == 'T' || ch == 't' || ch == 'A' || ch == 'a' ||
           ch == 'Z' || ch == 'z';
}

[[nodiscard]] bool is_implicit_command(char ch) noexcept {
    return ch == '-' || ch == '+' || ch == '.' || (ch >= '0' && ch <= '9');
}

// ---- Coordinate helpers ----

constexpr float kPi = 3.14159265358979323846F;

float deg_to_rad(float deg) noexcept {
    return deg * kPi / 180.0F;
}

void add_line(GeometryFigure& fig, Point to) noexcept {
    fig.segments.push_back(GeometrySegment{
        .type = GeometrySegmentType::Line,
        .point = layout::Point{to.x, to.y},
    });
}

void add_quadratic(GeometryFigure& fig, Point ctrl, Point to) noexcept {
    fig.segments.push_back(GeometrySegment{
        .type = GeometrySegmentType::QuadraticBezier,
        .point = layout::Point{to.x, to.y},
        .control_point1 = layout::Point{ctrl.x, ctrl.y},
    });
}

void add_cubic(GeometryFigure& fig, Point ctrl1, Point ctrl2, Point to) noexcept {
    fig.segments.push_back(GeometrySegment{
        .type = GeometrySegmentType::CubicBezier,
        .point = layout::Point{to.x, to.y},
        .control_point1 = layout::Point{ctrl1.x, ctrl1.y},
        .control_point2 = layout::Point{ctrl2.x, ctrl2.y},
    });
}

void add_arc(GeometryFigure& fig, Point radius, float rotation_angle, GeometryArcSize arc_size,
             GeometryArcSweepDirection sweep_dir, Point to) noexcept {
    fig.segments.push_back(GeometrySegment{
        .type = GeometrySegmentType::Arc,
        .point = layout::Point{to.x, to.y},
        .radius = layout::Size{radius.x, radius.y},
        .rotation_angle = rotation_angle,
        .arc_size = arc_size,
        .sweep_direction = sweep_dir,
    });
}

// ---- SVG arc to endpoint parameter conversion ----
// Based on SVG 1.1 spec: https://www.w3.org/TR/SVG/implnote.html#ArcImplementationNotes

struct ArcParams {
    Point center;
    float rx = 0.0F;
    float ry = 0.0F;
    float start_angle = 0.0F;
    float end_angle = 0.0F;
    bool sweep_flag = false;
};

[[nodiscard]] Point rotate_point(Point p, float angle_rad) noexcept {
    const float cos_a = std::cos(angle_rad);
    const float sin_a = std::sin(angle_rad);
    return Point{p.x * cos_a - p.y * sin_a, p.x * sin_a + p.y * cos_a};
}

[[nodiscard]] ArcParams endpoint_to_center(Point p1, Point p2, Point radii, float angle_rad,
                                           bool large_flag, bool sweep_flag) noexcept {
    ArcParams result{};

    // Step 1: Compute transformed coordinates
    const float cos_a = std::cos(angle_rad);
    const float sin_a = std::sin(angle_rad);
    const float dx = (p1.x - p2.x) / 2.0F;
    const float dy = (p1.y - p2.y) / 2.0F;
    const Point p1t{cos_a * dx + sin_a * dy, -sin_a * dx + cos_a * dy};

    // Ensure radii are valid
    float rx = std::abs(radii.x);
    float ry = std::abs(radii.y);
    const float lambda = (p1t.x * p1t.x) / (rx * rx) + (p1t.y * p1t.y) / (ry * ry);
    if (lambda > 1.0F) {
        const float sqrt_lambda = std::sqrt(lambda);
        rx *= sqrt_lambda;
        ry *= sqrt_lambda;
    }

    // Step 2: Compute center
    const float num = rx * rx * ry * ry - rx * rx * p1t.y * p1t.y - ry * ry * p1t.x * p1t.x;
    const float denom = rx * rx * p1t.y * p1t.y + ry * ry * p1t.x * p1t.x;
    float factor = 0.0F;
    if (denom > 0.0F) {
        factor = std::sqrt(std::max(0.0F, num / denom));
    }
    if (large_flag == sweep_flag) {
        factor = -factor;
    }
    const Point cts{factor * rx * p1t.y / ry, factor * -ry * p1t.x / rx};

    // Step 3: Convert to center point
    const float cx = cos_a * cts.x - sin_a * cts.y + (p1.x + p2.x) / 2.0F;
    const float cy = sin_a * cts.x + cos_a * cts.y + (p1.y + p2.y) / 2.0F;

    // Step 4: Compute start and end angles
    const auto vector_angle = [](Point u, Point v) noexcept -> float {
        const float dot = u.x * v.x + u.y * v.y;
        const float len = std::sqrt((u.x * u.x + u.y * u.y) * (v.x * v.x + v.y * v.y));
        float ang = 0.0F;
        if (len > 0.0F) {
            ang = std::acos(std::clamp(dot / len, -1.0F, 1.0F));
        }
        if ((u.x * v.y - u.y * v.x) < 0.0F) {
            ang = -ang;
        }
        return ang;
    };

    const Point v1{(p1t.x - cts.x) / rx, (p1t.y - cts.y) / ry};
    const Point v2{(-p1t.x - cts.x) / rx, (-p1t.y - cts.y) / ry};
    result.start_angle = vector_angle(Point{1.0F, 0.0F}, v1);
    result.end_angle = vector_angle(v1, v2);

    result.center = Point{cx, cy};
    result.rx = rx;
    result.ry = ry;
    result.sweep_flag = sweep_flag;
    return result;
}

} // anonymous namespace

Geometry parse_svg_path(std::string_view svg_path_data) noexcept {
    if (svg_path_data.empty()) {
        return Geometry{};
    }

    Cursor cursor{svg_path_data, 0};
    Geometry geometry{};
    GeometryFigure current_figure{};

    Point current_point{0.0F, 0.0F};
    Point subpath_start{0.0F, 0.0F}; // for Z/z
    Point prev_ctrl{0.0F, 0.0F};     // smooth curve reflection
    bool has_prev_ctrl = false;

    auto close_figure = [&]() {
        if (current_figure.segments.empty()) {
            current_figure = GeometryFigure{};
            return;
        }
        geometry.figures.push_back(current_figure);
        current_figure = GeometryFigure{};
    };

    auto start_new_figure = [&](Point start) {
        close_figure();
        current_figure.start = layout::Point{start.x, start.y};
        current_figure.begin = GeometryFigureBegin::Filled;
        current_figure.end = GeometryFigureEnd::Open;
        current_point = start;
        subpath_start = start;
    };

    char last_command = 'M';

    while (cursor.pos < cursor.input.size()) {
        char cmd_char = peek(cursor);

        // Handle implicit commands (consecutive coordinate pairs without repeating the command
        // char)
        if (is_implicit_command(cmd_char) && last_command != '\0') {
            cmd_char = last_command;
        } else {
            cmd_char = consume_command(cursor);
            if (cmd_char == '\0') {
                break;
            }
            if (!is_command_char(cmd_char)) {
                break; // invalid character
            }
            last_command = cmd_char;
        }

        const bool is_relative = (cmd_char >= 'a' && cmd_char <= 'z');
        const char base_cmd = (is_relative ? static_cast<char>(cmd_char - 'a' + 'A') : cmd_char);

        auto rel = [&](Point p) -> Point {
            return is_relative ? Point{current_point.x + p.x, current_point.y + p.y} : p;
        };

        switch (base_cmd) {
        case 'M': { // moveto
            auto coord = parse_coord_pair(cursor);
            if (!coord) {
                // If no coordinate after M, treat as Z
                close_figure();
                break;
            }
            const auto target = rel(*coord);
            start_new_figure(target);
            // After M, implicit commands are treated as L
            last_command = is_relative ? 'l' : 'L';
            // Additional coordinate pairs after M are treated as L
            while (true) {
                auto next_coord = parse_coord_pair(cursor);
                if (!next_coord) {
                    break;
                }
                const auto line_to = rel(*next_coord);
                add_line(current_figure, line_to);
                current_point = line_to;
            }
            has_prev_ctrl = false;
            break;
        }
        case 'Z': { // closepath
            current_figure.end = GeometryFigureEnd::Closed;
            close_figure();
            // Z resets current_point to subpath_start
            current_point = subpath_start;
            has_prev_ctrl = false;
            break;
        }
        case 'L': { // lineto
            while (true) {
                auto coord = parse_coord_pair(cursor);
                if (!coord) {
                    break;
                }
                const auto target = rel(*coord);
                add_line(current_figure, target);
                current_point = target;
            }
            has_prev_ctrl = false;
            break;
        }
        case 'H': { // horizontal lineto
            while (true) {
                auto x_opt = parse_number(cursor);
                if (!x_opt) {
                    break;
                }
                const float x = is_relative ? current_point.x + *x_opt : *x_opt;
                const Point target{x, current_point.y};
                add_line(current_figure, target);
                current_point = target;
            }
            has_prev_ctrl = false;
            break;
        }
        case 'V': { // vertical lineto
            while (true) {
                auto y_opt = parse_number(cursor);
                if (!y_opt) {
                    break;
                }
                const float y = is_relative ? current_point.y + *y_opt : *y_opt;
                const Point target{current_point.x, y};
                add_line(current_figure, target);
                current_point = target;
            }
            has_prev_ctrl = false;
            break;
        }
        case 'C': { // cubic bezier curveto
            while (true) {
                auto c1 = parse_coord_pair(cursor);
                if (!c1) {
                    break;
                }
                auto c2 = parse_coord_pair(cursor);
                if (!c2) {
                    break;
                }
                auto end = parse_coord_pair(cursor);
                if (!end) {
                    break;
                }
                const auto ctrl1 = rel(*c1);
                const auto ctrl2 = rel(*c2);
                const auto target = rel(*end);
                add_cubic(current_figure, ctrl1, ctrl2, target);
                prev_ctrl = ctrl2;
                has_prev_ctrl = true;
                current_point = target;
            }
            break;
        }
        case 'S': { // smooth cubic bezier curveto
            while (true) {
                auto c2 = parse_coord_pair(cursor);
                if (!c2) {
                    break;
                }
                auto end = parse_coord_pair(cursor);
                if (!end) {
                    break;
                }
                // Reflect previous control point
                const Point ctrl1 = has_prev_ctrl ? Point{2.0F * current_point.x - prev_ctrl.x,
                                                          2.0F * current_point.y - prev_ctrl.y}
                                                  : current_point;
                const auto ctrl2 = rel(*c2);
                const auto target = rel(*end);
                add_cubic(current_figure, ctrl1, ctrl2, target);
                prev_ctrl = ctrl2;
                has_prev_ctrl = true;
                current_point = target;
            }
            break;
        }
        case 'Q': { // quadratic bezier curveto
            while (true) {
                auto ctrl = parse_coord_pair(cursor);
                if (!ctrl) {
                    break;
                }
                auto end = parse_coord_pair(cursor);
                if (!end) {
                    break;
                }
                const auto ctrl_p = rel(*ctrl);
                const auto target = rel(*end);
                add_quadratic(current_figure, ctrl_p, target);
                prev_ctrl = ctrl_p;
                has_prev_ctrl = true;
                current_point = target;
            }
            break;
        }
        case 'T': { // smooth quadratic bezier curveto
            while (true) {
                auto end = parse_coord_pair(cursor);
                if (!end) {
                    break;
                }
                const Point ctrl_p = has_prev_ctrl ? Point{2.0F * current_point.x - prev_ctrl.x,
                                                           2.0F * current_point.y - prev_ctrl.y}
                                                   : current_point;
                const auto target = rel(*end);
                add_quadratic(current_figure, ctrl_p, target);
                prev_ctrl = ctrl_p;
                has_prev_ctrl = true;
                current_point = target;
            }
            break;
        }
        case 'A': { // elliptical arc
            while (true) {
                auto radii_opt = parse_coord_pair(cursor);
                if (!radii_opt) {
                    break;
                }
                auto angle_opt = parse_number(cursor);
                if (!angle_opt) {
                    break;
                }
                auto large_opt = parse_number(cursor);
                if (!large_opt) {
                    break;
                }
                auto sweep_opt = parse_number(cursor);
                if (!sweep_opt) {
                    break;
                }
                auto end_opt = parse_coord_pair(cursor);
                if (!end_opt) {
                    break;
                }
                const auto radii = *radii_opt;
                const float rot_angle = deg_to_rad(*angle_opt);
                const bool large_flag = *large_opt != 0.0F;
                const bool sweep_flag = *sweep_opt != 0.0F;
                const auto target = rel(*end_opt);

                if (radii.x > 0.0F && radii.y > 0.0F &&
                    (current_point.x != target.x || current_point.y != target.y)) {
                    const auto arc =
                        endpoint_to_center(current_point, target, Point{radii.x, radii.y},
                                           rot_angle, large_flag, sweep_flag);

                    add_arc(current_figure, Point{arc.rx, arc.ry}, *angle_opt,
                            large_flag ? GeometryArcSize::Large : GeometryArcSize::Small,
                            sweep_flag ? GeometryArcSweepDirection::Clockwise
                                       : GeometryArcSweepDirection::CounterClockwise,
                            target);
                } else {
                    // Degenerate arc -> line
                    add_line(current_figure, target);
                }
                current_point = target;
                has_prev_ctrl = false;
            }
            break;
        }
        default:
            // Unknown command, skip
            break;
        }
    }

    // Close any open figure
    close_figure();

    return geometry;
}

} // namespace winelement::rendering
