#pragma once

#include <winelement/layout/layout_types.hpp>
#include <winelement/rendering/render_context.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace winelement::controls::detail {

struct PopupListMetrics {
    float min_width = 136.0F;
    float max_width = 320.0F;
    float item_height = 28.0F;
    float vertical_padding = 4.0F;
    float text_padding = 12.0F;
    float font_size = 13.0F;
};

struct PopupItemPaintState {
    bool hovered = false;
    bool pressed = false;
    bool selected = false;
    bool disabled = false;
};

[[nodiscard]] PopupListMetrics sanitize_popup_metrics(PopupListMetrics metrics) noexcept;
[[nodiscard]] bool popup_contains_local_point(layout::Rect rect, layout::Point point) noexcept;
[[nodiscard]] float estimated_popup_text_width(std::string_view text, float font_size) noexcept;
[[nodiscard]] float preferred_popup_width(PopupListMetrics metrics,
                                          const std::vector<std::string_view>& labels) noexcept;
[[nodiscard]] layout::Size preferred_popup_list_size(PopupListMetrics metrics,
                                                     std::size_t item_count, float width,
                                                     float extra_height = 0.0F) noexcept;
[[nodiscard]] std::optional<std::size_t> popup_item_at(layout::Point local_position,
                                                       PopupListMetrics metrics,
                                                       std::size_t item_count,
                                                       float top_offset = 0.0F) noexcept;
template <typename IsEnabled>
[[nodiscard]] std::optional<std::size_t>
next_enabled_popup_item(std::size_t item_count, std::optional<std::size_t> current, int direction,
                        IsEnabled&& is_enabled) {
    if (item_count == 0U) {
        return std::nullopt;
    }

    auto candidate =
        current && *current < item_count ? *current : (direction > 0 ? item_count - 1U : 0U);
    for (auto step = std::size_t{0}; step < item_count; ++step) {
        candidate = direction > 0 ? (candidate + 1U) % item_count
                                  : (candidate + item_count - 1U) % item_count;
        if (std::forward<IsEnabled>(is_enabled)(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}
void paint_popup_surface(rendering::RenderContext& context, layout::Rect absolute_frame,
                         const style::UIElementStyle& popup_style, float open_progress);
void paint_popup_item(rendering::RenderContext& context, layout::Rect item_rect,
                      std::string_view text, const style::UIElementStyle& popup_style,
                      PopupListMetrics metrics, PopupItemPaintState state);

} // namespace winelement::controls::detail
