#include <winelement/controls/path.hpp>

#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace winelement::controls {
namespace {

struct GeometryBounds {
    float left = std::numeric_limits<float>::max();
    float top = std::numeric_limits<float>::max();
    float right = std::numeric_limits<float>::lowest();
    float bottom = std::numeric_limits<float>::lowest();
    bool valid = false;

    void include(layout::Point point) noexcept {
        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
        valid = true;
    }

    void include_radius(layout::Point center, layout::Size radius) noexcept {
        include(
            layout::Point{center.x - std::abs(radius.width), center.y - std::abs(radius.height)});
        include(
            layout::Point{center.x + std::abs(radius.width), center.y + std::abs(radius.height)});
    }

    [[nodiscard]] float width() const noexcept {
        return std::max(right - left, 0.0F);
    }
    [[nodiscard]] float height() const noexcept {
        return std::max(bottom - top, 0.0F);
    }
};

[[nodiscard]] char lower_ascii(char value) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

[[nodiscard]] bool is_command(char value) noexcept {
    switch (lower_ascii(value)) {
    case 'm':
    case 'l':
    case 'h':
    case 'v':
    case 'c':
    case 's':
    case 'q':
    case 't':
    case 'a':
    case 'z':
    case 'f':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_number_start(char value) noexcept {
    return (value >= '0' && value <= '9') || value == '+' || value == '-' || value == '.';
}

[[nodiscard]] layout::Point absolute_or_relative(layout::Point current, layout::Point point,
                                                 bool relative) noexcept {
    return relative ? layout::Point{current.x + point.x, current.y + point.y} : point;
}

[[nodiscard]] layout::Point reflect_point(layout::Point origin, layout::Point control) noexcept {
    return layout::Point{origin.x * 2.0F - control.x, origin.y * 2.0F - control.y};
}

[[nodiscard]] GeometryBounds bounds_for_geometry(const rendering::Geometry& geometry) noexcept {
    auto bounds = GeometryBounds{};
    for (const auto& figure : geometry.figures) {
        auto current = figure.start;
        bounds.include(current);
        for (const auto& segment : figure.segments) {
            switch (segment.type) {
            case rendering::GeometrySegmentType::Line:
                bounds.include(segment.point);
                current = segment.point;
                break;
            case rendering::GeometrySegmentType::QuadraticBezier:
                bounds.include(segment.control_point1);
                bounds.include(segment.point);
                current = segment.point;
                break;
            case rendering::GeometrySegmentType::CubicBezier:
                bounds.include(segment.control_point1);
                bounds.include(segment.control_point2);
                bounds.include(segment.point);
                current = segment.point;
                break;
            case rendering::GeometrySegmentType::Arc:
                bounds.include_radius(current, segment.radius);
                bounds.include_radius(segment.point, segment.radius);
                current = segment.point;
                break;
            }
        }
    }
    return bounds;
}

class PathDataParser {
  public:
    explicit PathDataParser(std::string_view data) : data_(data) {}

    [[nodiscard]] rendering::Geometry parse() {
        auto command = char{};
        while (true) {
            skip_separators();
            if (eof()) {
                break;
            }
            if (is_command(peek())) {
                command = get();
            } else if (command == '\0') {
                throw std::invalid_argument{"path data must start with a command"};
            }
            parse_command(command);
        }
        return geometry_;
    }

  private:
    [[nodiscard]] bool eof() const noexcept {
        return index_ >= data_.size();
    }
    [[nodiscard]] char peek() const noexcept {
        return eof() ? '\0' : data_[index_];
    }
    [[nodiscard]] char get() noexcept {
        return eof() ? '\0' : data_[index_++];
    }

    void skip_separators() noexcept {
        while (!eof()) {
            const auto value = peek();
            if (value == ',' || value == ' ' || value == '\t' || value == '\r' || value == '\n') {
                ++index_;
                continue;
            }
            break;
        }
    }

    [[nodiscard]] bool has_number() noexcept {
        skip_separators();
        return !eof() && is_number_start(peek());
    }

    [[nodiscard]] float parse_number() {
        skip_separators();
        if (eof() || !is_number_start(peek())) {
            throw std::invalid_argument{"expected number in path data"};
        }

        auto value = 0.0F;
        const auto* begin = data_.data() + index_;
        const auto* end = data_.data() + data_.size();
        const auto result = std::from_chars(begin, end, value, std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr == begin || !std::isfinite(value)) {
            throw std::invalid_argument{"invalid number in path data"};
        }
        index_ += static_cast<std::size_t>(result.ptr - begin);
        return value;
    }

    [[nodiscard]] int parse_flag() {
        const auto value = parse_number();
        if (std::abs(value) < 0.5F) {
            return 0;
        }
        if (std::abs(value - 1.0F) < 0.5F) {
            return 1;
        }
        throw std::invalid_argument{"expected 0 or 1 flag in path data"};
    }

    [[nodiscard]] layout::Point parse_point(bool relative) {
        const auto point = layout::Point{parse_number(), parse_number()};
        return absolute_or_relative(current_, point, relative);
    }

    void begin_figure(layout::Point point) {
        geometry_.figures.push_back(
            rendering::GeometryFigure{.start = point,
                                      .begin = rendering::GeometryFigureBegin::Filled,
                                      .end = rendering::GeometryFigureEnd::Open});
        current_ = point;
        figure_start_ = point;
        has_current_figure_ = true;
        reset_smooth_controls();
    }

    [[nodiscard]] rendering::GeometryFigure& current_figure() {
        if (!has_current_figure_ || geometry_.figures.empty()) {
            begin_figure(layout::Point{});
        }
        return geometry_.figures.back();
    }

    void reset_smooth_controls() noexcept {
        has_last_cubic_control_ = false;
        has_last_quadratic_control_ = false;
    }

    void add_line(layout::Point point) {
        current_figure().segments.push_back(rendering::GeometrySegment{
            .type = rendering::GeometrySegmentType::Line, .point = point});
        current_ = point;
        reset_smooth_controls();
    }

    void add_quadratic(layout::Point control, layout::Point point) {
        current_figure().segments.push_back(
            rendering::GeometrySegment{.type = rendering::GeometrySegmentType::QuadraticBezier,
                                       .point = point,
                                       .control_point1 = control});
        current_ = point;
        last_quadratic_control_ = control;
        has_last_quadratic_control_ = true;
        has_last_cubic_control_ = false;
    }

    void add_cubic(layout::Point control1, layout::Point control2, layout::Point point) {
        current_figure().segments.push_back(
            rendering::GeometrySegment{.type = rendering::GeometrySegmentType::CubicBezier,
                                       .point = point,
                                       .control_point1 = control1,
                                       .control_point2 = control2});
        current_ = point;
        last_cubic_control_ = control2;
        has_last_cubic_control_ = true;
        has_last_quadratic_control_ = false;
    }

    void add_arc(layout::Size radius, float rotation, int large_arc, int sweep,
                 layout::Point point) {
        current_figure().segments.push_back(rendering::GeometrySegment{
            .type = rendering::GeometrySegmentType::Arc,
            .point = point,
            .radius = layout::Size{std::abs(radius.width), std::abs(radius.height)},
            .rotation_angle = rotation,
            .arc_size = large_arc != 0 ? rendering::GeometryArcSize::Large
                                       : rendering::GeometryArcSize::Small,
            .sweep_direction = sweep != 0
                                   ? rendering::GeometryArcSweepDirection::Clockwise
                                   : rendering::GeometryArcSweepDirection::CounterClockwise});
        current_ = point;
        reset_smooth_controls();
    }

    void parse_command(char command) {
        const auto relative = std::islower(static_cast<unsigned char>(command)) != 0;
        switch (lower_ascii(command)) {
        case 'f':
            parse_fill_rule();
            return;
        case 'm':
            parse_move(relative);
            return;
        case 'l':
            while (has_number()) {
                add_line(parse_point(relative));
            }
            return;
        case 'h':
            while (has_number()) {
                const auto x = parse_number();
                add_line(layout::Point{relative ? current_.x + x : x, current_.y});
            }
            return;
        case 'v':
            while (has_number()) {
                const auto y = parse_number();
                add_line(layout::Point{current_.x, relative ? current_.y + y : y});
            }
            return;
        case 'q':
            while (has_number()) {
                const auto control = parse_point(relative);
                const auto point = parse_point(relative);
                add_quadratic(control, point);
            }
            return;
        case 't':
            while (has_number()) {
                const auto control = has_last_quadratic_control_
                                         ? reflect_point(current_, last_quadratic_control_)
                                         : current_;
                const auto point = parse_point(relative);
                add_quadratic(control, point);
            }
            return;
        case 'c':
            while (has_number()) {
                const auto control1 = parse_point(relative);
                const auto control2 = parse_point(relative);
                const auto point = parse_point(relative);
                add_cubic(control1, control2, point);
            }
            return;
        case 's':
            while (has_number()) {
                const auto control1 = has_last_cubic_control_
                                          ? reflect_point(current_, last_cubic_control_)
                                          : current_;
                const auto control2 = parse_point(relative);
                const auto point = parse_point(relative);
                add_cubic(control1, control2, point);
            }
            return;
        case 'a':
            while (has_number()) {
                const auto radius = layout::Size{parse_number(), parse_number()};
                const auto rotation = parse_number();
                const auto large_arc = parse_flag();
                const auto sweep = parse_flag();
                const auto point = parse_point(relative);
                add_arc(radius, rotation, large_arc, sweep, point);
            }
            return;
        case 'z':
            close_figure();
            return;
        default:
            throw std::invalid_argument{"unsupported path command"};
        }
    }

    void parse_fill_rule() {
        const auto flag = parse_flag();
        geometry_.fill_rule =
            flag == 0 ? rendering::GeometryFillRule::EvenOdd : rendering::GeometryFillRule::NonZero;
        reset_smooth_controls();
    }

    void parse_move(bool relative) {
        if (!has_number()) {
            throw std::invalid_argument{"move command requires a point"};
        }
        begin_figure(parse_point(relative));
        while (has_number()) {
            add_line(parse_point(relative));
        }
    }

    void close_figure() {
        current_figure().end = rendering::GeometryFigureEnd::Closed;
        current_ = figure_start_;
        reset_smooth_controls();
    }

    std::string_view data_;
    std::size_t index_ = 0U;
    rendering::Geometry geometry_;
    layout::Point current_{};
    layout::Point figure_start_{};
    layout::Point last_cubic_control_{};
    layout::Point last_quadratic_control_{};
    bool has_current_figure_ = false;
    bool has_last_cubic_control_ = false;
    bool has_last_quadratic_control_ = false;
};

[[nodiscard]] rendering::Geometry transform_geometry(const rendering::Geometry& geometry,
                                                     layout::Rect frame, PathStretch stretch) {
    auto transformed = geometry;
    if (transformed.figures.empty()) {
        return transformed;
    }

    const auto bounds = bounds_for_geometry(geometry);
    auto scale_x = 1.0F;
    auto scale_y = 1.0F;
    auto offset_x = frame.x;
    auto offset_y = frame.y;
    auto source_left = 0.0F;
    auto source_top = 0.0F;

    if (stretch != PathStretch::None && bounds.valid && bounds.width() > 0.0F &&
        bounds.height() > 0.0F && frame.width > 0.0F && frame.height > 0.0F) {
        const auto fill_x = frame.width / bounds.width();
        const auto fill_y = frame.height / bounds.height();
        switch (stretch) {
        case PathStretch::Fill:
            scale_x = fill_x;
            scale_y = fill_y;
            break;
        case PathStretch::Uniform:
            scale_x = scale_y = std::min(fill_x, fill_y);
            break;
        case PathStretch::UniformToFill:
            scale_x = scale_y = std::max(fill_x, fill_y);
            break;
        case PathStretch::None:
            break;
        }
        source_left = bounds.left;
        source_top = bounds.top;
        offset_x += (frame.width - bounds.width() * scale_x) * 0.5F;
        offset_y += (frame.height - bounds.height() * scale_y) * 0.5F;
    }

    const auto transform_point = [&](layout::Point point) noexcept {
        return layout::Point{offset_x + (point.x - source_left) * scale_x,
                             offset_y + (point.y - source_top) * scale_y};
    };
    const auto transform_size = [&](layout::Size size) noexcept {
        return layout::Size{std::abs(size.width * scale_x), std::abs(size.height * scale_y)};
    };

    for (auto& figure : transformed.figures) {
        figure.start = transform_point(figure.start);
        for (auto& segment : figure.segments) {
            segment.point = transform_point(segment.point);
            segment.control_point1 = transform_point(segment.control_point1);
            segment.control_point2 = transform_point(segment.control_point2);
            segment.radius = transform_size(segment.radius);
        }
    }
    return transformed;
}

} // namespace

Path::Path() : Control() {
    set_theme_class(style::theme_class::path);
    auto next_style = style::UIElementStyle{};
    next_style.background = rendering::Color::rgba(255, 255, 255, 0);
    next_style.border_color = rendering::Color::rgba(255, 255, 255, 0);
    next_style.min_width = 24.0F;
    next_style.min_height = 24.0F;
    apply_style_value(std::move(next_style), true);
    stroke_ = style_storage().focus_border_color;
    stroke_style_.width = 1.5F;
    stroke_style_.start_cap = rendering::StrokeLineCap::Round;
    stroke_style_.end_cap = rendering::StrokeLineCap::Round;
    stroke_style_.dash_cap = rendering::StrokeLineCap::Round;
    stroke_style_.line_join = rendering::StrokeLineJoin::Round;
}

Path& Path::set_data(std::string_view data) {
    auto parsed = parse_path_data(data);
    data_ = data;
    geometry_ = std::move(parsed);
    bounds_dirty_ = true;
    invalidate_paint();
    return *this;
}

Path& Path::set_geometry(rendering::Geometry geometry) {
    data_.clear();
    geometry_ = std::move(geometry);
    bounds_dirty_ = true;
    invalidate_paint();
    return *this;
}

Path& Path::set_fill(rendering::Color fill) noexcept {
    fill_ = fill;
    invalidate_paint();
    return *this;
}

Path& Path::clear_fill() noexcept {
    fill_.reset();
    invalidate_paint();
    return *this;
}

Path& Path::set_stroke(rendering::Color stroke) noexcept {
    stroke_ = stroke;
    invalidate_paint();
    return *this;
}

Path& Path::clear_stroke() noexcept {
    stroke_.reset();
    invalidate_paint();
    return *this;
}

Path& Path::set_stroke_width(float width) noexcept {
    stroke_style_.width = std::max(width, 0.0F);
    invalidate_paint();
    return *this;
}

Path& Path::set_stroke_style(rendering::GeometryStrokeStyle style) {
    stroke_style_ = std::move(style);
    stroke_style_.width = std::max(stroke_style_.width, 0.0F);
    invalidate_paint();
    return *this;
}

Path& Path::set_stretch(PathStretch stretch) noexcept {
    stretch_ = stretch;
    invalidate_paint();
    return *this;
}

Path& Path::set_cache_bounds(bool cache_bounds) noexcept {
    cache_bounds_ = cache_bounds;
    bounds_dirty_ = true;
    return *this;
}

Path& Path::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    return *this;
}

const std::string& Path::data() const noexcept {
    return data_;
}

const rendering::Geometry& Path::geometry() const noexcept {
    return geometry_;
}

std::optional<rendering::Color> Path::fill() const noexcept {
    return fill_;
}

std::optional<rendering::Color> Path::stroke() const noexcept {
    return stroke_;
}

const rendering::GeometryStrokeStyle& Path::stroke_style() const noexcept {
    return stroke_style_;
}

PathStretch Path::stretch() const noexcept {
    return stretch_;
}

PathBounds Path::geometry_bounds() const noexcept {
    if (!cache_bounds_) {
        const auto bounds = bounds_for_geometry(geometry_);
        return PathBounds{
            .bounds = layout::Rect{bounds.left, bounds.top, bounds.width(), bounds.height()},
            .valid = bounds.valid};
    }
    if (bounds_dirty_) {
        const auto bounds = bounds_for_geometry(geometry_);
        cached_bounds_ = PathBounds{
            .bounds = layout::Rect{bounds.left, bounds.top, bounds.width(), bounds.height()},
            .valid = bounds.valid};
        bounds_dirty_ = false;
    }
    return cached_bounds_;
}

bool Path::cache_bounds() const noexcept {
    return cache_bounds_;
}

rendering::Geometry Path::parse_path_data(std::string_view data) {
    return PathDataParser{data}.parse();
}

void Path::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    if (style_storage().background.alpha != 0U || style_storage().border_color.alpha != 0U) {
        UIElement::on_paint(context, absolute_frame);
    }
    if (geometry_.figures.empty()) {
        return;
    }

    const auto geometry = transform_geometry(geometry_, absolute_frame, stretch_);
    if (fill_ && fill_->alpha != 0U) {
        context.fill_geometry(geometry, *fill_);
    }
    if (stroke_ && stroke_->alpha != 0U && stroke_style_.width > 0.0F) {
        context.stroke_geometry(geometry, *stroke_, stroke_style_);
    }
}

} // namespace winelement::controls