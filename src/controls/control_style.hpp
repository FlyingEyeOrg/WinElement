#pragma once

#include <winelement/elements/ui_element.hpp>
#include <winelement/layout/layout_types.hpp>
#include <winelement/rendering/render_types.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <string_view>

#include <algorithm>
#include <cstddef>

namespace winelement::controls::detail {

[[nodiscard]] inline bool has_visible_alpha(rendering::Color color) noexcept {
    return color.alpha != 0;
}

[[nodiscard]] inline bool is_utf8_continuation_byte(unsigned char value) noexcept {
    return (value & 0xC0U) == 0x80U;
}

[[nodiscard]] inline std::size_t utf8_code_point_count(std::string_view text) noexcept {
    std::size_t count = 0;
    for (const auto value : text) {
        if (!is_utf8_continuation_byte(static_cast<unsigned char>(value))) {
            ++count;
        }
    }

    return count;
}

[[nodiscard]] inline layout::Size measure_single_line_text(std::string_view text, float font_size,
                                                           float horizontal_padding) noexcept {
    const auto text_width = static_cast<float>(utf8_code_point_count(text)) * font_size * 0.55F;
    const auto line_height = font_size * 1.25F;
    return layout::Size{std::max(text_width + horizontal_padding, 1.0F), line_height};
}

[[nodiscard]] inline float clamp_non_negative(float value) noexcept {
    return std::max(value, 0.0F);
}

[[nodiscard]] inline layout::Rect inset_rect(layout::Rect rect,
                                             layout::EdgeInsets padding) noexcept {
    rect.x += padding.left;
    rect.y += padding.top;
    rect.width = clamp_non_negative(rect.width - padding.left - padding.right);
    rect.height = clamp_non_negative(rect.height - padding.top - padding.bottom);
    return rect;
}

inline void apply_visual_style(elements::UIElement& element,
                               const style::VisualStyle& visual_style) {
    element.set_opacity(visual_style.opacity)
        .set_render_transform(visual_style.transform)
        .set_layer_enabled(visual_style.layer_enabled);
}

[[nodiscard]] inline float centered_origin_y(layout::Rect rect, float content_height) noexcept {
    return rect.y + std::max((rect.height - content_height) * 0.5F, 0.0F);
}

} // namespace winelement::controls::detail
