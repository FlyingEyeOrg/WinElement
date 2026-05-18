#include "detail/yoga_conversions.hpp"

#include <cstdint>
#include <stdexcept>

namespace winelement::layout::detail {

YGDirection to_yoga(Direction direction) {
    switch (direction) {
    case Direction::Inherit:
        return YGDirectionInherit;
    case Direction::LeftToRight:
        return YGDirectionLTR;
    case Direction::RightToLeft:
        return YGDirectionRTL;
    }

    throw std::invalid_argument("unknown layout direction");
}

YGFlexDirection to_yoga(FlexDirection flex_direction) {
    switch (flex_direction) {
    case FlexDirection::Column:
        return YGFlexDirectionColumn;
    case FlexDirection::ColumnReverse:
        return YGFlexDirectionColumnReverse;
    case FlexDirection::Row:
        return YGFlexDirectionRow;
    case FlexDirection::RowReverse:
        return YGFlexDirectionRowReverse;
    }

    throw std::invalid_argument("unknown flex direction");
}

YGJustify to_yoga(JustifyContent justify_content) {
    switch (justify_content) {
    case JustifyContent::FlexStart:
        return YGJustifyFlexStart;
    case JustifyContent::Center:
        return YGJustifyCenter;
    case JustifyContent::FlexEnd:
        return YGJustifyFlexEnd;
    case JustifyContent::SpaceBetween:
        return YGJustifySpaceBetween;
    case JustifyContent::SpaceAround:
        return YGJustifySpaceAround;
    case JustifyContent::SpaceEvenly:
        return YGJustifySpaceEvenly;
    }

    throw std::invalid_argument("unknown justify content");
}

YGAlign to_yoga(Align align) {
    switch (align) {
    case Align::Auto:
        return YGAlignAuto;
    case Align::FlexStart:
        return YGAlignFlexStart;
    case Align::Center:
        return YGAlignCenter;
    case Align::FlexEnd:
        return YGAlignFlexEnd;
    case Align::Stretch:
        return YGAlignStretch;
    case Align::Baseline:
        return YGAlignBaseline;
    case Align::SpaceBetween:
        return YGAlignSpaceBetween;
    case Align::SpaceAround:
        return YGAlignSpaceAround;
    case Align::SpaceEvenly:
        return YGAlignSpaceEvenly;
    }

    throw std::invalid_argument("unknown alignment");
}

YGPositionType to_yoga(PositionType position_type) {
    switch (position_type) {
    case PositionType::Static:
        return YGPositionTypeStatic;
    case PositionType::Relative:
        return YGPositionTypeRelative;
    case PositionType::Absolute:
        return YGPositionTypeAbsolute;
    }

    throw std::invalid_argument("unknown position type");
}

YGWrap to_yoga(Wrap flex_wrap) {
    switch (flex_wrap) {
    case Wrap::NoWrap:
        return YGWrapNoWrap;
    case Wrap::Wrap:
        return YGWrapWrap;
    case Wrap::WrapReverse:
        return YGWrapWrapReverse;
    }

    throw std::invalid_argument("unknown flex wrap");
}

YGOverflow to_yoga(Overflow overflow) {
    switch (overflow) {
    case Overflow::Visible:
        return YGOverflowVisible;
    case Overflow::Hidden:
        return YGOverflowHidden;
    case Overflow::Scroll:
        return YGOverflowScroll;
    }

    throw std::invalid_argument("unknown overflow");
}

YGDisplay to_yoga(Display display) {
    switch (display) {
    case Display::Flex:
        return YGDisplayFlex;
    case Display::None:
        return YGDisplayNone;
    case Display::Contents:
        return YGDisplayContents;
    }

    throw std::invalid_argument("unknown display");
}

YGBoxSizing to_yoga(BoxSizing box_sizing) {
    switch (box_sizing) {
    case BoxSizing::BorderBox:
        return YGBoxSizingBorderBox;
    case BoxSizing::ContentBox:
        return YGBoxSizingContentBox;
    }

    throw std::invalid_argument("unknown box sizing");
}

YGEdge to_yoga(Edge edge) {
    switch (edge) {
    case Edge::Left:
        return YGEdgeLeft;
    case Edge::Top:
        return YGEdgeTop;
    case Edge::Right:
        return YGEdgeRight;
    case Edge::Bottom:
        return YGEdgeBottom;
    case Edge::Start:
        return YGEdgeStart;
    case Edge::End:
        return YGEdgeEnd;
    case Edge::Horizontal:
        return YGEdgeHorizontal;
    case Edge::Vertical:
        return YGEdgeVertical;
    case Edge::All:
        return YGEdgeAll;
    }

    throw std::invalid_argument("unknown edge");
}

YGGutter to_yoga(Gutter gutter) {
    switch (gutter) {
    case Gutter::Column:
        return YGGutterColumn;
    case Gutter::Row:
        return YGGutterRow;
    case Gutter::All:
        return YGGutterAll;
    }

    throw std::invalid_argument("unknown gutter");
}

YGNodeType to_yoga(ElementKind element_kind) {
    switch (element_kind) {
    case ElementKind::Default:
        return YGNodeTypeDefault;
    case ElementKind::Text:
        return YGNodeTypeText;
    }

    throw std::invalid_argument("unknown element kind");
}

YGErrata to_yoga(Errata errata) {
    return static_cast<YGErrata>(static_cast<std::uint32_t>(errata));
}

YGExperimentalFeature to_yoga(ExperimentalFeature feature) {
    switch (feature) {
    case ExperimentalFeature::WebFlexBasis:
        return YGExperimentalFeatureWebFlexBasis;
    }

    throw std::invalid_argument("unknown experimental feature");
}

Direction from_yoga(YGDirection direction) {
    switch (direction) {
    case YGDirectionInherit:
        return Direction::Inherit;
    case YGDirectionLTR:
        return Direction::LeftToRight;
    case YGDirectionRTL:
        return Direction::RightToLeft;
    }

    throw std::invalid_argument("unknown Yoga direction");
}

Errata from_yoga(YGErrata errata) {
    return static_cast<Errata>(static_cast<std::uint32_t>(errata));
}

} // namespace winelement::layout::detail
