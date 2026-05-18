#pragma once

#include <winelement/rendering/render_resource_queue.hpp>
#include <winelement/rendering/render_types.hpp>
#include <winelement/rendering/text_engine.hpp>

#include <span>
#include <string_view>

namespace winelement::rendering {

class RenderContext {
  public:
    virtual ~RenderContext();

    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;
    RenderContext(RenderContext&&) = delete;
    RenderContext& operator=(RenderContext&&) = delete;

    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void push_clip(layout::Rect rect) = 0;
    virtual void pop_clip() = 0;
    virtual void push_geometry_clip(const Geometry& geometry) = 0;
    virtual void pop_geometry_clip() = 0;
    virtual void push_layer(const RenderLayerOptions& options) = 0;
    virtual void pop_layer() = 0;
    virtual void draw_line(layout::Point start, layout::Point end, Color color,
                           float stroke_width) = 0;
    virtual void fill_rect(layout::Rect rect, Color color) = 0;
    virtual void fill_pixel_snapped_rect(layout::Rect rect, Color color) = 0;
    virtual void stroke_pixel_snapped_rect(layout::Rect rect, Color color, float stroke_width) = 0;
    virtual void stroke_rect(layout::Rect rect, Color color, float stroke_width) = 0;
    virtual void fill_rounded_rect(layout::Rect rect, CornerRadius radius, Color color) = 0;
    virtual void stroke_rounded_rect(layout::Rect rect, CornerRadius radius, Color color,
                                     float stroke_width) = 0;
    virtual void fill_ellipse(layout::Rect rect, Color color) = 0;
    virtual void stroke_ellipse(layout::Rect rect, Color color, float stroke_width) = 0;
    virtual void fill_geometry(const Geometry& geometry, Color color) = 0;
    virtual void stroke_geometry(const Geometry& geometry, Color color,
                                 const GeometryStrokeStyle& style) = 0;
    virtual void draw_image(RenderResourceId resource_id, const RenderImageOptions& options) = 0;
    virtual void draw_box_shadow(layout::Rect rect, const ShadowStyle& style) = 0;
    virtual void draw_text(std::string_view text, layout::Rect rect, const TextStyle& style) = 0;
    virtual void draw_text_layout(const TextLayout& layout, layout::Point origin) = 0;

    void draw_point(layout::Point center, Color color, float radius = 1.0F);
    void draw_polyline(std::span<const layout::Point> points, Color color,
                       const GeometryStrokeStyle& style = GeometryStrokeStyle{});
    void fill_polygon(std::span<const layout::Point> points, Color color,
                      GeometryFillRule fill_rule = GeometryFillRule::NonZero);
    void stroke_polygon(std::span<const layout::Point> points, Color color,
                        const GeometryStrokeStyle& style = GeometryStrokeStyle{},
                        GeometryFillRule fill_rule = GeometryFillRule::NonZero);

  protected:
    RenderContext() = default;
};

} // namespace winelement::rendering
