#include <winelement/elements/all_icons.hpp>
#include <winelement/elements/svg_icon.hpp>
#include <winelement/rendering/render_context.hpp>
#include <winelement/rendering/svg_path_parser.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>

namespace winelement::elements {

namespace {

[[nodiscard]] layout::Rect geometry_bounds(const rendering::Geometry& geometry) noexcept {
    auto left = 0.0F;
    auto top = 0.0F;
    auto right = 0.0F;
    auto bottom = 0.0F;
    auto valid = false;

    const auto include = [&](layout::Point point) noexcept {
        if (!valid) {
            left = right = point.x;
            top = bottom = point.y;
            valid = true;
            return;
        }

        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
    };

    for (const auto& figure : geometry.figures) {
        include(figure.start);
        for (const auto& segment : figure.segments) {
            include(segment.point);
            include(segment.control_point1);
            include(segment.control_point2);
            include(layout::Point{segment.point.x - std::abs(segment.radius.width),
                                  segment.point.y - std::abs(segment.radius.height)});
            include(layout::Point{segment.point.x + std::abs(segment.radius.width),
                                  segment.point.y + std::abs(segment.radius.height)});
        }
    }

    if (!valid || right <= left || bottom <= top) {
        return layout::Rect{0.0F, 0.0F, 24.0F, 24.0F};
    }
    return layout::Rect{left, top, right - left, bottom - top};
}

[[nodiscard]] rendering::Geometry transform_geometry_to_bounds(const rendering::Geometry& src,
                                                               layout::Rect bounds,
                                                               layout::Rect viewbox) noexcept {
    if (bounds.width <= 0.0F || bounds.height <= 0.0F || viewbox.width <= 0.0F ||
        viewbox.height <= 0.0F) {
        return src;
    }

    const float scale_x = bounds.width / viewbox.width;
    const float scale_y = bounds.height / viewbox.height;
    const float scale = std::min(scale_x, scale_y);

    const float scaled_width = viewbox.width * scale;
    const float scaled_height = viewbox.height * scale;
    const float offset_x = bounds.x + (bounds.width - scaled_width) / 2.0F;
    const float offset_y = bounds.y + (bounds.height - scaled_height) / 2.0F;

    auto transform_point = [&](layout::Point p) -> layout::Point {
        return layout::Point{(p.x - viewbox.x) * scale + offset_x,
                             (p.y - viewbox.y) * scale + offset_y};
    };

    rendering::Geometry result;
    result.fill_rule = src.fill_rule;
    result.figures.reserve(src.figures.size());

    for (const auto& fig : src.figures) {
        rendering::GeometryFigure new_fig;
        new_fig.start = transform_point(fig.start);
        new_fig.begin = fig.begin;
        new_fig.end = fig.end;
        new_fig.segments.reserve(fig.segments.size());

        for (const auto& seg : fig.segments) {
            auto new_seg = seg;
            new_seg.point = transform_point(seg.point);
            new_seg.control_point1 = transform_point(seg.control_point1);
            new_seg.control_point2 = transform_point(seg.control_point2);
            if (seg.type == rendering::GeometrySegmentType::Arc) {
                new_seg.radius = layout::Size{seg.radius.width * scale, seg.radius.height * scale};
            }
            new_fig.segments.push_back(new_seg);
        }
        result.figures.push_back(new_fig);
    }
    return result;
}

} // anonymous namespace

SvgIcon::SvgIcon() {
    set_focusable(false);
    configure_layout([](layout::LayoutElement& l) {
        l.set_width(layout::Length::points(16.0F));
        l.set_height(layout::Length::points(16.0F));
    });
}

SvgIcon::SvgIcon(rendering::Geometry icon_geometry) : geometry_(std::move(icon_geometry)) {
    viewbox_rect_ = geometry_bounds(*geometry_);
    set_focusable(false);
    configure_layout([](layout::LayoutElement& l) {
        l.set_width(layout::Length::points(16.0F));
        l.set_height(layout::Length::points(16.0F));
    });
}

SvgIcon::~SvgIcon() noexcept = default;

void SvgIcon::set_svg_path(std::string_view svg_path_data) {
    auto geometry = rendering::parse_svg_path(svg_path_data);
    viewbox_rect_ = geometry_bounds(geometry);
    geometry_ = std::move(geometry);
    ++geometry_gen_;
    invalidate_layout();
    invalidate_paint();
}

void SvgIcon::set_geometry(rendering::Geometry geometry) {
    viewbox_rect_ = geometry_bounds(geometry);
    geometry_ = std::move(geometry);
    ++geometry_gen_;
    invalidate_layout();
    invalidate_paint();
}

void SvgIcon::clear_icon() noexcept {
    geometry_.reset();
    invalidate_paint();
}

void SvgIcon::set_icon_size(float size) noexcept {
    icon_width_ = size;
    icon_height_ = size;
    configure_layout([s = size](layout::LayoutElement& l) {
        l.set_width(layout::Length::points(s));
        l.set_height(layout::Length::points(s));
    });
}

void SvgIcon::set_icon_size(float width, float height) noexcept {
    icon_width_ = width;
    icon_height_ = height;
    configure_layout([w = width, h = height](layout::LayoutElement& l) {
        l.set_width(layout::Length::points(w));
        l.set_height(layout::Length::points(h));
    });
}

void SvgIcon::set_viewbox_size(float size) noexcept {
    if (!std::isfinite(size) || size <= 0.0F) {
        size = 24.0F;
    }
    viewbox_size_ = size;
    viewbox_rect_ = layout::Rect{0.0F, 0.0F, size, size};
    cached_gen_ = 0;
    invalidate_paint();
}

void SvgIcon::set_icon_paths(const icons::IconPathsBase& icon_data) {
    rendering::Geometry combined;
    combined.fill_rule = rendering::GeometryFillRule::EvenOdd;
    for (const auto& svg_path : icon_data.paths) {
        auto geom = rendering::parse_svg_path(svg_path);
        combined.figures.insert(combined.figures.end(),
                                std::make_move_iterator(geom.figures.begin()),
                                std::make_move_iterator(geom.figures.end()));
    }
    geometry_ = std::move(combined);
    viewbox_size_ = 1024.0F;
    viewbox_rect_ = layout::Rect{0.0F, 0.0F, viewbox_size_, viewbox_size_};
    ++geometry_gen_;
    invalidate_layout();
    invalidate_paint();
}

void SvgIcon::set_icon_color(std::optional<rendering::Color> color) noexcept {
    icon_color_ = color;
    invalidate_paint();
}

void SvgIcon::on_paint(rendering::RenderContext& ctx, layout::Rect absolute_frame) const {
    paint_icon(ctx, absolute_frame);
}

void SvgIcon::paint_icon(rendering::RenderContext& ctx, layout::Rect bounds,
                         std::optional<rendering::Color> color) const {
    if (!geometry_) {
        return;
    }

    const auto resolved_color = color.value_or(icon_color_.value_or(text_color()));

    // Snap icon bounds to integer pixels for crisp edges at small sizes.
    auto snapped = bounds;
    snapped.x = std::round(snapped.x);
    snapped.y = std::round(snapped.y);
    snapped.width = std::round(snapped.width);
    snapped.height = std::round(snapped.height);

    if (cached_gen_ != geometry_gen_ || cached_bounds_ != snapped ||
        cached_viewbox_ != viewbox_rect_) {
        cached_transformed_ = transform_geometry_to_bounds(*geometry_, snapped, viewbox_rect_);
        cached_bounds_ = snapped;
        cached_viewbox_ = viewbox_rect_;
        cached_gen_ = geometry_gen_;
    }
    ctx.fill_geometry(*cached_transformed_, resolved_color);
}

// ---- Free functions ----

std::unique_ptr<SvgIcon> make_svg_icon(std::string_view svg_path_data) {
    auto icon = std::make_unique<SvgIcon>();
    icon->set_svg_path(svg_path_data);
    return icon;
}

std::unique_ptr<SvgIcon> make_svg_icon(rendering::Geometry geometry) {
    return std::make_unique<SvgIcon>(std::move(geometry));
}

} // namespace winelement::elements
