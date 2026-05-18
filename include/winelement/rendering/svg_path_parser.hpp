#pragma once

#include <winelement/rendering/render_types.hpp>

#include <string_view>

namespace winelement::rendering {

/// Parse an SVG path 'd' attribute string into a Geometry object.
/// Supports: M/m, L/l, H/h, V/v, C/c, S/s, Q/q, T/t, A/a, Z/z commands.
/// Returns an empty geometry on parse failure (no figures).
[[nodiscard]] Geometry parse_svg_path(std::string_view svg_path_data) noexcept;

} // namespace winelement::rendering