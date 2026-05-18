#pragma once

#include <winelement/core/core_types.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace winelement::rendering {

namespace layout = winelement::core;

using Color = core::Color;
using Transform2D = core::Transform2D;
using CornerRadius = core::CornerRadius;
using core::is_identity_transform;
using core::multiply_transforms;
using core::transform_around_point;
using core::transform_point;
using core::transform_rect;

enum class TextAlignment { Start, Center, End };
enum class TextVerticalAlignment { Top, Center, Bottom };
enum class TextWrapping { NoWrap, Wrap };
enum class TextTrimming { None, CharacterEllipsis, WordEllipsis };
enum class ReadingDirection { LeftToRight, RightToLeft };
enum class FontWeight : std::uint16_t { Normal = 400, SemiBold = 600, Bold = 700 };
enum class FontStyle { Normal, Oblique, Italic };
enum class FontStretch : std::uint16_t { Normal = 5, Condensed = 3, Expanded = 7 };
enum class LineSpacingMethod { Default, Uniform, Proportional };
enum class TextDecorationLine : std::uint8_t { None = 0, Underline = 1, Strikethrough = 2 };
enum class RenderCommandType {
    Save,
    Restore,
    PushClip,
    PopClip,
    PushGeometryClip,
    PopGeometryClip,
    PushLayer,
    PopLayer,
    DrawLine,
    FillRect,
    FillPixelSnappedRect,
    StrokePixelSnappedRect,
    StrokeRect,
    FillRoundedRect,
    StrokeRoundedRect,
    FillEllipse,
    StrokeEllipse,
    FillGeometry,
    StrokeGeometry,
    DrawImage,
    DrawText,
    DrawTextLayout,
    DrawBoxShadow
};

struct RenderLayerOptions {
    layout::Rect bounds{};
    float opacity = 1.0F;
    Transform2D transform{};
    bool clips_to_bounds = true;
};

struct RenderImageOptions {
    layout::Rect destination{};
    layout::Rect source{};
    float opacity = 1.0F;
};

enum class GeometryFillRule { NonZero, EvenOdd };
enum class GeometryFigureBegin { Filled, Hollow };
enum class GeometryFigureEnd { Open, Closed };
enum class GeometrySegmentType { Line, QuadraticBezier, CubicBezier, Arc };
enum class GeometryArcSize { Small, Large };
enum class GeometryArcSweepDirection { CounterClockwise, Clockwise };
enum class StrokeLineCap { Flat, Square, Round, Triangle };
enum class StrokeLineJoin { Miter, Bevel, Round };
enum class StrokeDashStyle { Solid, Dash, Dot, DashDot, DashDotDot, Custom };

struct GeometrySegment {
    GeometrySegmentType type = GeometrySegmentType::Line;
    layout::Point point{};
    layout::Point control_point1{};
    layout::Point control_point2{};
    layout::Size radius{};
    float rotation_angle = 0.0F;
    GeometryArcSize arc_size = GeometryArcSize::Small;
    GeometryArcSweepDirection sweep_direction = GeometryArcSweepDirection::Clockwise;

    [[nodiscard]] friend bool operator==(const GeometrySegment&, const GeometrySegment&) = default;
};

struct GeometryFigure {
    layout::Point start{};
    GeometryFigureBegin begin = GeometryFigureBegin::Filled;
    GeometryFigureEnd end = GeometryFigureEnd::Closed;
    std::vector<GeometrySegment> segments;

    [[nodiscard]] friend bool operator==(const GeometryFigure&, const GeometryFigure&) = default;
};

struct Geometry {
    GeometryFillRule fill_rule = GeometryFillRule::NonZero;
    std::vector<GeometryFigure> figures;

    [[nodiscard]] friend bool operator==(const Geometry&, const Geometry&) = default;
};

struct GeometryStrokeStyle {
    float width = 1.0F;
    StrokeLineCap start_cap = StrokeLineCap::Flat;
    StrokeLineCap end_cap = StrokeLineCap::Flat;
    StrokeLineCap dash_cap = StrokeLineCap::Flat;
    StrokeLineJoin line_join = StrokeLineJoin::Miter;
    float miter_limit = 10.0F;
    StrokeDashStyle dash_style = StrokeDashStyle::Solid;
    float dash_offset = 0.0F;
    std::vector<float> dashes;

    [[nodiscard]] friend bool operator==(const GeometryStrokeStyle&,
                                         const GeometryStrokeStyle&) = default;
};

[[nodiscard]] constexpr TextDecorationLine operator|(TextDecorationLine left,
                                                     TextDecorationLine right) noexcept {
    return static_cast<TextDecorationLine>(static_cast<std::uint8_t>(left) |
                                           static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr TextDecorationLine operator&(TextDecorationLine left,
                                                     TextDecorationLine right) noexcept {
    return static_cast<TextDecorationLine>(static_cast<std::uint8_t>(left) &
                                           static_cast<std::uint8_t>(right));
}

constexpr TextDecorationLine& operator|=(TextDecorationLine& left,
                                         TextDecorationLine right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_text_decoration(TextDecorationLine value,
                                                 TextDecorationLine flag) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0U;
}

struct OpenTypeFeature {
    std::uint32_t tag = 0;
    std::uint32_t parameter = 1;

    [[nodiscard]] friend constexpr bool operator==(OpenTypeFeature,
                                                   OpenTypeFeature) noexcept = default;
};

[[nodiscard]] constexpr std::uint32_t make_opentype_tag(char first, char second, char third,
                                                        char fourth) noexcept {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(first)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(second)) << 8U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(third)) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(fourth)) << 24U);
}

struct TextStyle {
    std::string font_family = "Segoe UI";
    std::string locale = "zh-cn";
    float font_size = 14.0F;
    Color color = Color::rgba(48, 49, 51);
    TextAlignment alignment = TextAlignment::Start;
    TextVerticalAlignment vertical_alignment = TextVerticalAlignment::Top;
    TextWrapping wrapping = TextWrapping::NoWrap;
    TextTrimming trimming = TextTrimming::None;
    ReadingDirection reading_direction = ReadingDirection::LeftToRight;
    FontWeight font_weight = FontWeight::Normal;
    FontStyle font_style = FontStyle::Normal;
    FontStretch font_stretch = FontStretch::Normal;
    LineSpacingMethod line_spacing_method = LineSpacingMethod::Default;
    float line_spacing = 0.0F;
    float baseline = 0.0F;
    TextDecorationLine decoration_line = TextDecorationLine::None;
    std::vector<std::string> fallback_font_families;
    std::vector<OpenTypeFeature> features;

    [[nodiscard]] friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

struct ShadowStyle {
    Color color = Color::rgba(0, 0, 0, 64);
    layout::Point offset{0.0F, 2.0F};
    float blur_radius = 8.0F;
    float spread = 0.0F;

    [[nodiscard]] friend constexpr bool operator==(ShadowStyle, ShadowStyle) noexcept = default;
};

} // namespace winelement::rendering
