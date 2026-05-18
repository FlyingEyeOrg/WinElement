#pragma once

#include <winelement/controls/control.hpp>
#include <winelement/rendering/render_types.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace winelement::rendering {
class RenderContext;
}

namespace winelement::controls {

enum class PathStretch { None, Fill, Uniform, UniformToFill };

struct PathBounds {
    layout::Rect bounds{};
    bool valid = false;
};

class Path final : public Control {
  public:
    Path();

    Path& set_data(std::string_view data);
    Path& set_geometry(rendering::Geometry geometry);
    Path& set_fill(rendering::Color fill) noexcept;
    Path& clear_fill() noexcept;
    Path& set_stroke(rendering::Color stroke) noexcept;
    Path& clear_stroke() noexcept;
    Path& set_stroke_width(float width) noexcept;
    Path& set_stroke_style(rendering::GeometryStrokeStyle style);
    Path& set_stretch(PathStretch stretch) noexcept;
    Path& set_cache_bounds(bool cache_bounds) noexcept;
    Path& set_style(style::UIElementStyle style) override;

    [[nodiscard]] const std::string& data() const noexcept;
    [[nodiscard]] const rendering::Geometry& geometry() const noexcept;
    [[nodiscard]] std::optional<rendering::Color> fill() const noexcept;
    [[nodiscard]] std::optional<rendering::Color> stroke() const noexcept;
    [[nodiscard]] const rendering::GeometryStrokeStyle& stroke_style() const noexcept;
    [[nodiscard]] PathStretch stretch() const noexcept;
    [[nodiscard]] PathBounds geometry_bounds() const noexcept;
    [[nodiscard]] bool cache_bounds() const noexcept;

    [[nodiscard]] static rendering::Geometry parse_path_data(std::string_view data);

  private:
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;

    rendering::Geometry geometry_;
    std::string data_;
    std::optional<rendering::Color> fill_;
    std::optional<rendering::Color> stroke_;
    rendering::GeometryStrokeStyle stroke_style_;
    PathStretch stretch_ = PathStretch::Uniform;
    mutable PathBounds cached_bounds_{};
    mutable bool bounds_dirty_ = true;
    bool cache_bounds_ = true;
};

} // namespace winelement::controls
