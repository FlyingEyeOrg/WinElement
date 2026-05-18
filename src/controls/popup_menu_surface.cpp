#include "popup_menu_surface.hpp"

#include "control_style.hpp"

#include <algorithm>
#include <cmath>

namespace winelement::controls::detail {
namespace {

[[nodiscard]] rendering::Color popup_shadow_color() noexcept {
    return rendering::Color::rgba(0, 0, 0, 36);
}

[[nodiscard]] float codepoint_width_factor(unsigned char leading_byte) noexcept {
    if (leading_byte < 0x80U) {
        return 0.56F;
    }
    if (leading_byte < 0xE0U) {
        return 0.78F;
    }
    return 1.0F;
}

[[nodiscard]] layout::Rect
popup_item_highlight_rect(layout::Rect item_rect,
                          const style::UIElementStyle& popup_style) noexcept {
    const auto inset = std::max(std::ceil(std::max(popup_style.border_width, 0.0F)), 1.0F);
    return layout::Rect{item_rect.x + inset, item_rect.y,
                        std::max(0.0F, item_rect.width - inset * 2.0F), item_rect.height};
}

void paint_popup_item_highlight(rendering::RenderContext& context, layout::Rect item_rect,
                                const style::UIElementStyle& popup_style, rendering::Color color) {
    if (color.alpha == 0U) {
        return;
    }
    context.fill_rounded_rect(popup_item_highlight_rect(item_rect, popup_style),
                              rendering::CornerRadius::uniform(2.0F), color);
}

} // namespace

PopupListMetrics sanitize_popup_metrics(PopupListMetrics metrics) noexcept {
    metrics.min_width = std::max(metrics.min_width, 1.0F);
    metrics.max_width = std::max(metrics.max_width, metrics.min_width);
    metrics.item_height = std::max(metrics.item_height, 1.0F);
    metrics.vertical_padding = std::max(metrics.vertical_padding, 0.0F);
    metrics.text_padding = std::max(metrics.text_padding, 0.0F);
    metrics.font_size = std::max(metrics.font_size, 1.0F);
    return metrics;
}

bool popup_contains_local_point(layout::Rect rect, layout::Point point) noexcept {
    return rect.width > 0.0F && rect.height > 0.0F && point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

float estimated_popup_text_width(std::string_view text, float font_size) noexcept {
    auto width_em = 0.0F;
    for (auto index = std::size_t{0}; index < text.size();) {
        const auto byte = static_cast<unsigned char>(text[index]);
        width_em += codepoint_width_factor(byte);
        if (byte < 0x80U) {
            ++index;
        } else if (byte < 0xE0U) {
            index += 2U;
        } else if (byte < 0xF0U) {
            index += 3U;
        } else {
            index += 4U;
        }
    }
    return width_em * std::max(font_size, 1.0F);
}

float preferred_popup_width(PopupListMetrics metrics,
                            const std::vector<std::string_view>& labels) noexcept {
    metrics = sanitize_popup_metrics(metrics);
    auto content_width = 0.0F;
    for (const auto label : labels) {
        content_width =
            std::max(content_width, estimated_popup_text_width(label, metrics.font_size));
    }
    return std::clamp(content_width + metrics.text_padding * 2.0F, metrics.min_width,
                      metrics.max_width);
}

layout::Size preferred_popup_list_size(PopupListMetrics metrics, std::size_t item_count,
                                       float width, float extra_height) noexcept {
    metrics = sanitize_popup_metrics(metrics);
    return layout::Size{std::clamp(width, metrics.min_width, metrics.max_width),
                        metrics.vertical_padding * 2.0F + std::max(extra_height, 0.0F) +
                            metrics.item_height * static_cast<float>(item_count)};
}

std::optional<std::size_t> popup_item_at(layout::Point local_position, PopupListMetrics metrics,
                                         std::size_t item_count, float top_offset) noexcept {
    metrics = sanitize_popup_metrics(metrics);
    const auto content_y = local_position.y - metrics.vertical_padding - std::max(top_offset, 0.0F);
    if (content_y < 0.0F || item_count == 0U) {
        return std::nullopt;
    }

    const auto index = static_cast<std::size_t>(content_y / metrics.item_height);
    return index < item_count ? std::optional<std::size_t>{index} : std::nullopt;
}

void paint_popup_surface(rendering::RenderContext& context, layout::Rect absolute_frame,
                         const style::UIElementStyle& popup_style, float open_progress) {
    const auto progress = std::clamp(open_progress, 0.0F, 1.0F);
    context.push_layer(rendering::RenderLayerOptions{
        .bounds = absolute_frame,
        .opacity = progress,
        .transform = rendering::Transform2D::translation(0.0F, -4.0F * (1.0F - progress)),
        .clips_to_bounds = false});
    context.draw_box_shadow(absolute_frame, rendering::ShadowStyle{.color = popup_shadow_color(),
                                                                   .offset = {0.0F, 4.0F},
                                                                   .blur_radius = 12.0F});
    style::paint_rectangle(
        context, absolute_frame,
        style::rectangle_style_from(popup_style, popup_style.background, popup_style.border_color));
}

void paint_popup_item(rendering::RenderContext& context, layout::Rect item_rect,
                      std::string_view text, const style::UIElementStyle& popup_style,
                      PopupListMetrics metrics, PopupItemPaintState state) {
    metrics = sanitize_popup_metrics(metrics);
    if (state.hovered && !state.disabled) {
        paint_popup_item_highlight(context, item_rect, popup_style, popup_style.hover_background);
    }
    if (state.selected && !state.disabled) {
        paint_popup_item_highlight(context, item_rect, popup_style, popup_style.active_background);
    }
    if (state.pressed && !state.disabled) {
        paint_popup_item_highlight(context, item_rect, popup_style,
                                   rendering::Color::rgba(popup_style.focus_border_color.red,
                                                          popup_style.focus_border_color.green,
                                                          popup_style.focus_border_color.blue, 28));
    }

    const auto text_rect = layout::Rect{
        item_rect.x + metrics.text_padding, item_rect.y,
        std::max(item_rect.width - metrics.text_padding * 2.0F, 0.0F), item_rect.height};
    const auto text_color = state.disabled   ? popup_style.semantic.disabled_text
                            : state.selected ? popup_style.focus_border_color
                                             : popup_style.text_color;
    context.draw_text(
        text, text_rect,
        rendering::TextStyle{.font_size = metrics.font_size,
                             .color = text_color,
                             .alignment = rendering::TextAlignment::Start,
                             .vertical_alignment = rendering::TextVerticalAlignment::Center,
                             .wrapping = rendering::TextWrapping::NoWrap,
                             .trimming = rendering::TextTrimming::CharacterEllipsis});
}

} // namespace winelement::controls::detail
