#pragma once

#include <winelement/core/core_types.hpp>

#include <cstdint>
#include <functional>
#include <optional>

namespace winelement::layout {

class LayoutElement;

using Size = core::Size;
using Point = core::Point;
using Rect = core::Rect;
using EdgeInsets = core::EdgeInsets;
using core::inflate_rect;
using core::intersect_rects;
using core::is_finite_rect;
using core::is_visible_rect;
using core::offset_rect;
using core::rect_bottom;
using core::rect_contains_point;
using core::rect_right;
using core::rects_intersect;
using core::rects_touch_or_intersect;
using core::union_rects;

enum class Direction { Inherit, LeftToRight, RightToLeft };
enum class FlexDirection { Column, ColumnReverse, Row, RowReverse };
enum class JustifyContent { FlexStart, Center, FlexEnd, SpaceBetween, SpaceAround, SpaceEvenly };
enum class Align {
    Auto,
    FlexStart,
    Center,
    FlexEnd,
    Stretch,
    Baseline,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};
enum class PositionType { Static, Relative, Absolute };
enum class Wrap { NoWrap, Wrap, WrapReverse };
enum class Overflow { Visible, Hidden, Scroll };
enum class Display { Flex, None, Contents };
enum class BoxSizing { BorderBox, ContentBox };
enum class Edge { Left, Top, Right, Bottom, Start, End, Horizontal, Vertical, All };
enum class Gutter { Column, Row, All };
enum class MeasureMode { Undefined, Exactly, AtMost };
enum class ElementKind { Default, Text };
enum class LengthUnit { Undefined, Points, Percent, Auto };
enum class Errata : std::uint32_t {
    None = 0,
    StretchFlexBasis = 1,
    AbsolutePositionWithoutInsetsExcludesPadding = 2,
    AbsolutePercentAgainstInnerSize = 4,
    Classic = 2147483646U,
    All = 2147483647U
};
enum class ExperimentalFeature { WebFlexBasis };

[[nodiscard]] constexpr Errata operator|(Errata left, Errata right) noexcept {
    return static_cast<Errata>(static_cast<std::uint32_t>(left) |
                               static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr Errata operator&(Errata left, Errata right) noexcept {
    return static_cast<Errata>(static_cast<std::uint32_t>(left) &
                               static_cast<std::uint32_t>(right));
}

constexpr Errata& operator|=(Errata& left, Errata right) noexcept {
    left = left | right;
    return left;
}

class Length final {
  public:
    constexpr Length() noexcept = default;

    [[nodiscard]] static constexpr Length undefined() noexcept {
        return {};
    }
    [[nodiscard]] static constexpr Length points(float value) noexcept {
        return Length(value, LengthUnit::Points);
    }
    [[nodiscard]] static constexpr Length percent(float value) noexcept {
        return Length(value, LengthUnit::Percent);
    }
    [[nodiscard]] static constexpr Length auto_value() noexcept {
        return Length(0.0F, LengthUnit::Auto);
    }

    [[nodiscard]] constexpr float value() const noexcept {
        return value_;
    }
    [[nodiscard]] constexpr LengthUnit unit() const noexcept {
        return unit_;
    }
    [[nodiscard]] constexpr bool is_defined() const noexcept {
        return unit_ != LengthUnit::Undefined;
    }

  private:
    constexpr Length(float value, LengthUnit unit) noexcept : value_(value), unit_(unit) {}

    float value_ = 0.0F;
    LengthUnit unit_ = LengthUnit::Undefined;
};

struct BaselineInput {
    float width = 0.0F;
    float height = 0.0F;
};

struct MeasureInput {
    float available_width = 0.0F;
    MeasureMode width_mode = MeasureMode::Undefined;
    float available_height = 0.0F;
    MeasureMode height_mode = MeasureMode::Undefined;
};

using MeasureCallback = std::function<Size(const MeasureInput& input)>;
using BaselineCallback = std::function<float(const BaselineInput& input)>;
using DirtiedCallback = std::function<void(LayoutElement& element)>;

struct LayoutConstraints {
    std::optional<float> width;
    std::optional<float> height;
    Direction direction = Direction::LeftToRight;
};

} // namespace winelement::layout
