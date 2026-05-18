#include <winelement/rendering/render_context.hpp>

#include <cstddef>
#include <utility>

namespace winelement::rendering {

RenderContext::~RenderContext() = default;

void RenderContext::draw_point(layout::Point center, Color color, float radius) {
    if (radius <= 0.0F) {
        return;
    }

    fill_ellipse(layout::Rect{center.x - radius, center.y - radius, radius * 2.0F, radius * 2.0F},
                 color);
}

void RenderContext::draw_polyline(std::span<const layout::Point> points, Color color,
                                  const GeometryStrokeStyle& style) {
    if (points.size() < 2U) {
        return;
    }

    Geometry geometry;
    GeometryFigure figure;
    figure.start = points.front();
    figure.begin = GeometryFigureBegin::Hollow;
    figure.end = GeometryFigureEnd::Open;
    figure.segments.reserve(points.size() - 1U);
    for (auto index = std::size_t{1}; index < points.size(); ++index) {
        figure.segments.push_back(
            GeometrySegment{.type = GeometrySegmentType::Line, .point = points[index]});
    }
    geometry.figures.push_back(std::move(figure));
    stroke_geometry(geometry, color, style);
}

void RenderContext::fill_polygon(std::span<const layout::Point> points, Color color,
                                 GeometryFillRule fill_rule) {
    if (points.size() < 3U) {
        return;
    }

    Geometry geometry;
    geometry.fill_rule = fill_rule;
    GeometryFigure figure;
    figure.start = points.front();
    figure.begin = GeometryFigureBegin::Filled;
    figure.end = GeometryFigureEnd::Closed;
    figure.segments.reserve(points.size() - 1U);
    for (auto index = std::size_t{1}; index < points.size(); ++index) {
        figure.segments.push_back(
            GeometrySegment{.type = GeometrySegmentType::Line, .point = points[index]});
    }
    geometry.figures.push_back(std::move(figure));
    fill_geometry(geometry, color);
}

void RenderContext::stroke_polygon(std::span<const layout::Point> points, Color color,
                                   const GeometryStrokeStyle& style, GeometryFillRule fill_rule) {
    if (points.size() < 3U) {
        return;
    }

    Geometry geometry;
    geometry.fill_rule = fill_rule;
    GeometryFigure figure;
    figure.start = points.front();
    figure.begin = GeometryFigureBegin::Hollow;
    figure.end = GeometryFigureEnd::Closed;
    figure.segments.reserve(points.size() - 1U);
    for (auto index = std::size_t{1}; index < points.size(); ++index) {
        figure.segments.push_back(
            GeometrySegment{.type = GeometrySegmentType::Line, .point = points[index]});
    }
    geometry.figures.push_back(std::move(figure));
    stroke_geometry(geometry, color, style);
}

} // namespace winelement::rendering
