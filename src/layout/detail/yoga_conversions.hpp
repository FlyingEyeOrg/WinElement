#pragma once

#include <winelement/layout/layout_types.hpp>

#include <yoga/Yoga.h>

namespace winelement::layout::detail {

[[nodiscard]] YGDirection to_yoga(Direction direction);
[[nodiscard]] YGFlexDirection to_yoga(FlexDirection flex_direction);
[[nodiscard]] YGJustify to_yoga(JustifyContent justify_content);
[[nodiscard]] YGAlign to_yoga(Align align);
[[nodiscard]] YGPositionType to_yoga(PositionType position_type);
[[nodiscard]] YGWrap to_yoga(Wrap flex_wrap);
[[nodiscard]] YGOverflow to_yoga(Overflow overflow);
[[nodiscard]] YGDisplay to_yoga(Display display);
[[nodiscard]] YGBoxSizing to_yoga(BoxSizing box_sizing);
[[nodiscard]] YGEdge to_yoga(Edge edge);
[[nodiscard]] YGGutter to_yoga(Gutter gutter);
[[nodiscard]] YGNodeType to_yoga(ElementKind element_kind);
[[nodiscard]] YGErrata to_yoga(Errata errata);
[[nodiscard]] YGExperimentalFeature to_yoga(ExperimentalFeature feature);
[[nodiscard]] Direction from_yoga(YGDirection direction);
[[nodiscard]] Errata from_yoga(YGErrata errata);

} // namespace winelement::layout::detail
