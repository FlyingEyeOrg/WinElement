#pragma once

#include <winelement/elements/ui_element.hpp>
#include <winelement/rendering/render_types.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace winelement::elements::icons {
struct IconPathsBase;
} // namespace winelement::elements::icons

namespace winelement::elements {

/// An element that renders an SVG icon from a parsed Geometry.
/// SvgIcon is a lightweight non-interactive element that displays a single icon
/// at a fixed size (default 16x16). It supports color tinting via the icon color
/// in the rendered style.
class SvgIcon : public UIElement {
  public:
    SvgIcon();
    explicit SvgIcon(rendering::Geometry icon_geometry);
    ~SvgIcon() noexcept override;

    SvgIcon(const SvgIcon&) = delete;
    SvgIcon& operator=(const SvgIcon&) = delete;
    SvgIcon(SvgIcon&&) = delete;
    SvgIcon& operator=(SvgIcon&&) = delete;

    /// Set the icon geometry from an SVG path 'd' string (parse at runtime).
    void set_svg_path(std::string_view svg_path_data);

    /// Set the icon geometry from pre-defined IconPathsBase data.
    /// Parses each path string into a combined geometry.
    void set_icon_paths(const icons::IconPathsBase& icon_data);

    /// Set the icon geometry directly (useful for pre-parsed built-in icons).
    void set_geometry(rendering::Geometry geometry);

    /// Clear the icon (render nothing).
    void clear_icon() noexcept;

    /// Set the icon size (width and height). The default is 16x16.
    void set_icon_size(float size) noexcept;
    void set_icon_size(float width, float height) noexcept;

    [[nodiscard]] float icon_width() const noexcept {
        return icon_width_;
    }
    [[nodiscard]] float icon_height() const noexcept {
        return icon_height_;
    }
    [[nodiscard]] bool has_icon() const noexcept {
        return geometry_.has_value();
    }

    /// Set the SVG viewBox size for coordinate transformation.
    /// Default is 24.0F (for typical 24x24 SVG icons).
    /// For Element icons (1024x1024 viewBox), call set_viewbox_size(1024.0F).
    void set_viewbox_size(float size) noexcept;

    [[nodiscard]] float viewbox_size() const noexcept {
        return viewbox_size_;
    }

    /// Set the icon color. If not set, the element's foreground color is used.
    void set_icon_color(std::optional<rendering::Color> color) noexcept;
    [[nodiscard]] std::optional<rendering::Color> icon_color() const noexcept {
        return icon_color_;
    }

    void paint_icon(rendering::RenderContext& ctx, layout::Rect bounds,
                    std::optional<rendering::Color> color = std::nullopt) const;

  protected:
    void on_paint(rendering::RenderContext& ctx, layout::Rect absolute_frame) const override;

  private:
    std::optional<rendering::Geometry> geometry_;
    std::uint64_t geometry_gen_ = 0;
    mutable std::optional<rendering::Geometry> cached_transformed_;
    mutable layout::Rect cached_bounds_{};
    mutable layout::Rect cached_viewbox_{};
    mutable std::uint64_t cached_gen_ = 0;
    float icon_width_ = 16.0F;
    float icon_height_ = 16.0F;
    float viewbox_size_ = 24.0F; // SVG viewBox size for coordinate transformation
    layout::Rect viewbox_rect_{0.0F, 0.0F, 24.0F, 24.0F};
    std::optional<rendering::Color> icon_color_;
};

/// Convenience: create an SvgIcon from an SVG path string.
[[nodiscard]] std::unique_ptr<SvgIcon> make_svg_icon(std::string_view svg_path_data);

/// Convenience: create an SvgIcon from a pre-parsed Geometry.
[[nodiscard]] std::unique_ptr<SvgIcon> make_svg_icon(rendering::Geometry geometry);

} // namespace winelement::elements
