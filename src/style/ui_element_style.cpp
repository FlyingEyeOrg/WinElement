#include <winelement/style/ui_element_style.hpp>

#include <winelement/rendering/render_context.hpp>
#include <winelement/style/element_colors.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace winelement::style {
namespace {

[[nodiscard]] bool has_visible_alpha(rendering::Color color) noexcept {
    return color.alpha != 0U;
}

[[nodiscard]] float non_negative(float value) noexcept {
    return std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
}

[[nodiscard]] rendering::CornerRadius normalized_radius(rendering::CornerRadius radius) noexcept {
    return rendering::CornerRadius{non_negative(radius.x), non_negative(radius.y)};
}

[[nodiscard]] rendering::ShadowStyle normalized_shadow(rendering::ShadowStyle shadow) noexcept {
    shadow.blur_radius = non_negative(shadow.blur_radius);
    shadow.spread = std::isfinite(shadow.spread) ? shadow.spread : 0.0F;
    shadow.offset.x = std::isfinite(shadow.offset.x) ? shadow.offset.x : 0.0F;
    shadow.offset.y = std::isfinite(shadow.offset.y) ? shadow.offset.y : 0.0F;
    return shadow;
}

[[nodiscard]] UIElementStyle make_default_button_style() noexcept {
    constexpr auto colors = element_colors();
    auto style = UIElementStyle{.background = colors.neutral.fill_blank,
                                .hover_background = colors.primary.light9,
                                .active_background = colors.neutral.fill_blank,
                                .read_only_background = colors.neutral.fill_blank,
                                .border_color = colors.neutral.border_base,
                                .focus_border_color = colors.primary.base,
                                .text_color = colors.neutral.text_regular,
                                .placeholder_color = colors.neutral.text_placeholder,
                                .caret_color = colors.primary.base,
                                .padding = layout::EdgeInsets{15.0F, 8.0F, 15.0F, 8.0F},
                                .border_width = 1.0F,
                                .font_size = 14.0F,
                                .min_width = 0.0F,
                                .min_height = 32.0F,
                                .caret_width = 1.0F,
                                .corner_radius = rendering::CornerRadius::uniform(4.0F),
                                .pixel_snapped_border = true,
                                .transition = TransitionStyle{.enabled = true}};
    style.semantic.hover_border = colors.primary.light7;
    return style;
}

[[nodiscard]] UIElementStyle make_accent_button_style(rendering::Color color,
                                                      rendering::Color hover,
                                                      rendering::Color active) noexcept {
    auto style = make_default_button_style();
    style.background = color;
    style.hover_background = hover;
    style.active_background = active;
    style.border_color = color;
    style.semantic.hover_border = hover;
    style.focus_border_color = active;
    style.text_color = rendering::Color::rgba(255, 255, 255);
    return style;
}

[[nodiscard]] UIElementStyle make_default_primary_button_style() noexcept {
    return make_accent_button_style(rendering::Color::rgba(64, 158, 255),
                                    rendering::Color::rgba(121, 187, 255),
                                    rendering::Color::rgba(51, 126, 204));
}

[[nodiscard]] UIElementStyle make_default_success_button_style() noexcept {
    return make_accent_button_style(rendering::Color::rgba(103, 194, 58),
                                    rendering::Color::rgba(149, 212, 117),
                                    rendering::Color::rgba(82, 155, 46));
}

[[nodiscard]] UIElementStyle make_default_warning_button_style() noexcept {
    return make_accent_button_style(rendering::Color::rgba(230, 162, 60),
                                    rendering::Color::rgba(235, 181, 99),
                                    rendering::Color::rgba(184, 129, 48));
}

[[nodiscard]] UIElementStyle make_default_danger_button_style() noexcept {
    return make_accent_button_style(rendering::Color::rgba(245, 108, 108),
                                    rendering::Color::rgba(248, 152, 152),
                                    rendering::Color::rgba(196, 86, 86));
}

[[nodiscard]] UIElementStyle make_default_info_button_style() noexcept {
    return make_accent_button_style(rendering::Color::rgba(144, 147, 153),
                                    rendering::Color::rgba(177, 179, 184),
                                    rendering::Color::rgba(115, 118, 122));
}

[[nodiscard]] UIElementStyle make_default_text_button_style() noexcept {
    auto style = make_default_button_style();
    style.background = rendering::Color::rgba(0, 0, 0, 0);
    style.hover_background = rendering::Color::rgba(236, 245, 255);
    style.active_background = rendering::Color::rgba(236, 245, 255);
    style.border_color = rendering::Color::rgba(0, 0, 0, 0);
    style.border_width = 0.0F;
    style.text_color = rendering::Color::rgba(64, 158, 255);
    style.min_width = 0.0F;
    style.padding = layout::EdgeInsets{6.0F, 4.0F, 6.0F, 4.0F};
    return style;
}

[[nodiscard]] UIElementStyle make_default_input_style() noexcept {
    constexpr auto colors = element_colors();
    return UIElementStyle{.background = colors.neutral.fill_blank,
                          .hover_background = colors.primary.light9,
                          .active_background = colors.primary.dark2,
                          .read_only_background = colors.neutral.fill_light,
                          .border_color = colors.neutral.border_base,
                          .focus_border_color = colors.primary.base,
                          .text_color = colors.neutral.text_primary,
                          .placeholder_color = colors.neutral.text_placeholder,
                          .caret_color = colors.primary.base,
                          .padding = layout::EdgeInsets{11.0F, 5.0F, 11.0F, 5.0F},
                          .border_width = 1.0F,
                          .font_size = 14.0F,
                          .min_width = 120.0F,
                          .min_height = 32.0F,
                          .caret_width = 1.0F,
                          .corner_radius = rendering::CornerRadius::uniform(4.0F),
                          .pixel_snapped_border = true,
                          .transition = TransitionStyle{.enabled = true}};
}

[[nodiscard]] UIElementStyle make_default_large_input_style() noexcept {
    auto style = make_default_input_style();
    style.font_size = 14.0F;
    style.min_height = 40.0F;
    style.padding = layout::EdgeInsets{14.0F, 8.0F, 14.0F, 8.0F};
    return style;
}

[[nodiscard]] UIElementStyle make_default_small_input_style() noexcept {
    auto style = make_default_input_style();
    style.font_size = 12.0F;
    style.min_height = 24.0F;
    style.padding = layout::EdgeInsets{8.0F, 4.0F, 8.0F, 4.0F};
    return style;
}

[[nodiscard]] UIElementStyle make_default_text_style() noexcept {
    return UIElementStyle{.background = rendering::Color::rgba(0, 0, 0, 0),
                          .hover_background = rendering::Color::rgba(0, 0, 0, 0),
                          .active_background = rendering::Color::rgba(0, 0, 0, 0),
                          .read_only_background = rendering::Color::rgba(0, 0, 0, 0),
                          .border_color = rendering::Color::rgba(0, 0, 0, 0),
                          .focus_border_color = rendering::Color::rgba(0, 0, 0, 0),
                          .text_color = rendering::Color::rgba(48, 49, 51),
                          .placeholder_color = rendering::Color::rgba(168, 171, 178),
                          .caret_color = rendering::Color::rgba(64, 158, 255),
                          .padding = layout::EdgeInsets{},
                          .border_width = 0.0F,
                          .font_size = 14.0F,
                          .min_width = 0.0F,
                          .min_height = 0.0F,
                          .caret_width = 1.0F};
}

[[nodiscard]] UIElementStyle make_default_panel_style() noexcept {
    return UIElementStyle{.background = rendering::Color::rgba(0, 0, 0, 0),
                          .hover_background = rendering::Color::rgba(0, 0, 0, 0),
                          .active_background = rendering::Color::rgba(0, 0, 0, 0),
                          .read_only_background = rendering::Color::rgba(0, 0, 0, 0),
                          .border_color = rendering::Color::rgba(220, 223, 230),
                          .focus_border_color = rendering::Color::rgba(64, 158, 255),
                          .text_color = rendering::Color::rgba(48, 49, 51),
                          .placeholder_color = rendering::Color::rgba(168, 171, 178),
                          .caret_color = rendering::Color::rgba(64, 158, 255),
                          .padding = layout::EdgeInsets{},
                          .border_width = 0.0F,
                          .font_size = 14.0F,
                          .min_width = 0.0F,
                          .min_height = 0.0F,
                          .caret_width = 1.0F};
}

[[nodiscard]] UIElementStyle make_default_border_style() noexcept {
    auto style = make_default_panel_style();
    style.background = rendering::Color::rgba(255, 255, 255);
    style.border_color = rendering::Color::rgba(220, 223, 230);
    style.border_width = 1.0F;
    style.corner_radius = rendering::CornerRadius::uniform(4.0F);
    style.padding = layout::EdgeInsets{12.0F, 12.0F, 12.0F, 12.0F};
    style.pixel_snapped_border = true;
    return style;
}

[[nodiscard]] UIElementStyle make_default_select_style() noexcept {
    return make_default_input_style();
}

[[nodiscard]] UIElementStyle make_default_select_option_style() noexcept {
    constexpr auto colors = element_colors();
    auto style = make_default_panel_style();
    style.background = colors.neutral.fill_blank;
    style.hover_background = colors.neutral.fill_light;
    style.active_background = colors.primary.light9;
    style.border_color = colors.neutral.border_light;
    style.focus_border_color = colors.primary.base;
    style.text_color = colors.neutral.text_regular;
    style.padding = layout::EdgeInsets{12.0F, 8.0F, 12.0F, 8.0F};
    style.border_width = 1.0F;
    style.corner_radius = rendering::CornerRadius::uniform(4.0F);
    style.pixel_snapped_border = true;
    style.min_height = 32.0F;
    return style;
}

[[nodiscard]] UIElementStyle make_default_context_menu_style() noexcept {
    auto style = make_default_select_option_style();
    style.padding = layout::EdgeInsets{12.0F, 4.0F, 12.0F, 4.0F};
    style.min_width = 136.0F;
    style.min_height = 0.0F;
    return style;
}

[[nodiscard]] UIElementStyle make_default_scrollbar_style() noexcept {
    constexpr auto colors = element_colors();
    auto style = make_default_panel_style();
    style.background = rendering::Color::rgba(0, 0, 0, 0);
    style.hover_background = rendering::Color::rgba(colors.neutral.text_secondary.red,
                                                    colors.neutral.text_secondary.green,
                                                    colors.neutral.text_secondary.blue, 76);
    style.active_background = rendering::Color::rgba(colors.neutral.text_secondary.red,
                                                     colors.neutral.text_secondary.green,
                                                     colors.neutral.text_secondary.blue, 128);
    style.border_width = 0.0F;
    style.corner_radius = rendering::CornerRadius::uniform(4.0F);
    style.min_width = 6.0F;
    style.min_height = 6.0F;
    return style;
}

[[nodiscard]] UIElementStyle make_default_radio_style() noexcept {
    auto style = make_default_panel_style();
    style.background = rendering::Color::rgba(255, 255, 255);
    style.hover_background = rendering::Color::rgba(255, 255, 255);
    style.active_background = rendering::Color::rgba(64, 158, 255);
    style.border_color = rendering::Color::rgba(220, 223, 230);
    style.focus_border_color = rendering::Color::rgba(64, 158, 255);
    style.text_color = rendering::Color::rgba(96, 98, 102);
    style.padding = layout::EdgeInsets{0.0F, 0.0F, 0.0F, 0.0F};
    style.min_height = 24.0F;
    return style;
}

[[nodiscard]] UIElementStyle make_default_switch_style() noexcept {
    auto style = make_default_panel_style();
    style.background = rendering::Color::rgba(220, 223, 230);
    style.hover_background = rendering::Color::rgba(192, 196, 204);
    style.active_background = rendering::Color::rgba(64, 158, 255);
    style.border_width = 0.0F;
    style.corner_radius = rendering::CornerRadius::uniform(10.0F);
    style.min_width = 40.0F;
    style.min_height = 20.0F;
    return style;
}

[[nodiscard]] UIElementStyle make_default_items_control_style() noexcept {
    auto style = make_default_panel_style();
    style.background = rendering::Color::rgba(255, 255, 255);
    style.hover_background = rendering::Color::rgba(236, 245, 255);
    style.active_background = rendering::Color::rgba(217, 236, 255);
    style.border_color = rendering::Color::rgba(220, 223, 230);
    style.focus_border_color = rendering::Color::rgba(64, 158, 255);
    style.border_width = 1.0F;
    style.corner_radius = rendering::CornerRadius::uniform(6.0F);
    style.pixel_snapped_border = true;
    style.padding = layout::EdgeInsets{};
    return style;
}

[[nodiscard]] SemanticColorTokens make_dark_semantic_tokens() noexcept {
    return SemanticColorTokens{.secondary_text = rendering::Color::rgba(168, 171, 178),
                               .disabled_text = rendering::Color::rgba(110, 113, 122),
                               .hover_border = rendering::Color::rgba(97, 102, 109),
                               .surface_subtle = rendering::Color::rgba(36, 38, 43),
                               .info = rendering::Color::rgba(121, 187, 255),
                               .success = rendering::Color::rgba(133, 206, 97),
                               .warning = rendering::Color::rgba(233, 201, 107),
                               .danger = rendering::Color::rgba(248, 152, 152)};
}

[[nodiscard]] UIElementStyle make_dark_button_style() noexcept {
    auto style = make_default_button_style();
    style.background = rendering::Color::rgba(30, 31, 34);
    style.hover_background = rendering::Color::rgba(52, 65, 88);
    style.active_background = rendering::Color::rgba(64, 158, 255);
    style.read_only_background = rendering::Color::rgba(24, 26, 30);
    style.border_color = rendering::Color::rgba(76, 79, 85);
    style.focus_border_color = rendering::Color::rgba(64, 158, 255);
    style.text_color = rendering::Color::rgba(229, 234, 243);
    style.placeholder_color = rendering::Color::rgba(141, 144, 149);
    style.caret_color = rendering::Color::rgba(64, 158, 255);
    style.semantic = make_dark_semantic_tokens();
    return style;
}

[[nodiscard]] UIElementStyle make_dark_input_style() noexcept {
    auto style = make_default_input_style();
    style.background = rendering::Color::rgba(30, 31, 34);
    style.hover_background = rendering::Color::rgba(52, 65, 88);
    style.active_background = rendering::Color::rgba(64, 158, 255);
    style.read_only_background = rendering::Color::rgba(24, 26, 30);
    style.border_color = rendering::Color::rgba(76, 79, 85);
    style.focus_border_color = rendering::Color::rgba(64, 158, 255);
    style.text_color = rendering::Color::rgba(229, 234, 243);
    style.placeholder_color = rendering::Color::rgba(141, 144, 149);
    style.caret_color = rendering::Color::rgba(64, 158, 255);
    style.semantic = make_dark_semantic_tokens();
    return style;
}

[[nodiscard]] UIElementStyle make_dark_text_style() noexcept {
    auto style = make_default_text_style();
    style.text_color = rendering::Color::rgba(229, 234, 243);
    style.placeholder_color = rendering::Color::rgba(141, 144, 149);
    style.caret_color = rendering::Color::rgba(64, 158, 255);
    style.semantic = make_dark_semantic_tokens();
    return style;
}

[[nodiscard]] UIElementStyle make_dark_panel_style() noexcept {
    auto style = make_default_panel_style();
    style.background = rendering::Color::rgba(0, 0, 0, 0);
    style.border_color = rendering::Color::rgba(76, 79, 85);
    style.text_color = rendering::Color::rgba(229, 234, 243);
    style.placeholder_color = rendering::Color::rgba(141, 144, 149);
    style.caret_color = rendering::Color::rgba(64, 158, 255);
    style.semantic = make_dark_semantic_tokens();
    return style;
}

[[nodiscard]] Theme& active_theme_storage() {
    static Theme theme = make_default_theme();
    return theme;
}

[[nodiscard]] const UIElementStyle& fallback_button_style() noexcept {
    static const auto style = make_default_button_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_primary_button_style() noexcept {
    static const auto style = make_default_primary_button_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_success_button_style() noexcept {
    static const auto style = make_default_success_button_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_warning_button_style() noexcept {
    static const auto style = make_default_warning_button_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_danger_button_style() noexcept {
    static const auto style = make_default_danger_button_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_info_button_style() noexcept {
    static const auto style = make_default_info_button_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_text_button_style() noexcept {
    static const auto style = make_default_text_button_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_input_style() noexcept {
    static const auto style = make_default_input_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_large_input_style() noexcept {
    static const auto style = make_default_large_input_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_small_input_style() noexcept {
    static const auto style = make_default_small_input_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_text_style() noexcept {
    static const auto style = make_default_text_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_panel_style() noexcept {
    static const auto style = make_default_panel_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_border_style() noexcept {
    static const auto style = make_default_border_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_select_style() noexcept {
    static const auto style = make_default_select_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_select_option_style() noexcept {
    static const auto style = make_default_select_option_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_context_menu_style() noexcept {
    static const auto style = make_default_context_menu_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_scrollbar_style() noexcept {
    static const auto style = make_default_scrollbar_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_radio_style() noexcept {
    static const auto style = make_default_radio_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_switch_style() noexcept {
    static const auto style = make_default_switch_style();
    return style;
}

[[nodiscard]] const UIElementStyle& fallback_items_control_style() noexcept {
    static const auto style = make_default_items_control_style();
    return style;
}

void register_builtin_theme_classes(Theme& theme, UIElementStyle panel, UIElementStyle button,
                                    UIElementStyle input, UIElementStyle text) {
    const auto panel_style = std::move(panel);
    const auto button_style = std::move(button);
    const auto input_style = std::move(input);
    const auto text_style = std::move(text);

    set_theme_style_class(theme, theme_class::panel, panel_style);
    set_theme_style_class(theme, theme_class::button, button_style);
    set_theme_style_class(theme, theme_class::button_primary, make_default_primary_button_style());
    set_theme_style_class(theme, theme_class::button_success, make_default_success_button_style());
    set_theme_style_class(theme, theme_class::button_warning, make_default_warning_button_style());
    set_theme_style_class(theme, theme_class::button_danger, make_default_danger_button_style());
    set_theme_style_class(theme, theme_class::button_info, make_default_info_button_style());
    auto text_button = button_style;
    text_button.background = rendering::Color::rgba(0, 0, 0, 0);
    text_button.hover_background = input_style.hover_background;
    text_button.active_background = input_style.hover_background;
    text_button.border_color = rendering::Color::rgba(0, 0, 0, 0);
    text_button.border_width = 0.0F;
    text_button.text_color = input_style.focus_border_color;
    text_button.min_width = 0.0F;
    text_button.padding = layout::EdgeInsets{6.0F, 4.0F, 6.0F, 4.0F};
    set_theme_style_class(theme, theme_class::button_text, text_button);

    set_theme_style_class(theme, theme_class::input, input_style);
    auto large_input = input_style;
    large_input.font_size = 14.0F;
    large_input.min_height = 40.0F;
    large_input.padding = layout::EdgeInsets{14.0F, 8.0F, 14.0F, 8.0F};
    set_theme_style_class(theme, theme_class::input_large, large_input);
    auto small_input = input_style;
    small_input.font_size = 12.0F;
    small_input.min_height = 24.0F;
    small_input.padding = layout::EdgeInsets{8.0F, 4.0F, 8.0F, 4.0F};
    set_theme_style_class(theme, theme_class::input_small, small_input);

    set_theme_style_class(theme, theme_class::text, text_style);
    set_theme_style_class(theme, theme_class::text_primary, text_style);
    auto success_text = text_style;
    success_text.text_color = text_style.semantic.success;
    set_theme_style_class(theme, theme_class::text_success, success_text);
    auto warning_text = text_style;
    warning_text.text_color = text_style.semantic.warning;
    set_theme_style_class(theme, theme_class::text_warning, warning_text);
    auto danger_text = text_style;
    danger_text.text_color = text_style.semantic.danger;
    set_theme_style_class(theme, theme_class::text_danger, danger_text);
    auto info_text = text_style;
    info_text.text_color = text_style.semantic.info;
    set_theme_style_class(theme, theme_class::text_info, info_text);

    auto border_style = panel_style;
    border_style.background = input_style.background;
    border_style.border_color = input_style.border_color;
    border_style.border_width = 1.0F;
    border_style.corner_radius = rendering::CornerRadius::uniform(4.0F);
    border_style.padding = layout::EdgeInsets{12.0F, 12.0F, 12.0F, 12.0F};
    border_style.pixel_snapped_border = true;
    set_theme_style_class(theme, theme_class::border, border_style);
    auto context_menu = make_default_context_menu_style();
    context_menu.background = input_style.background;
    context_menu.hover_background = button_style.hover_background;
    context_menu.active_background = input_style.hover_background;
    context_menu.border_color = input_style.semantic.hover_border;
    context_menu.focus_border_color = input_style.focus_border_color;
    context_menu.text_color = input_style.text_color;
    set_theme_style_class(theme, theme_class::context_menu, context_menu);
    set_theme_style_class(theme, theme_class::select, input_style);
    auto select_option = panel_style;
    select_option.background = input_style.background;
    select_option.hover_background = button_style.hover_background;
    select_option.active_background = input_style.hover_background;
    select_option.border_color = input_style.semantic.hover_border;
    select_option.focus_border_color = input_style.focus_border_color;
    select_option.text_color = input_style.text_color;
    select_option.padding = layout::EdgeInsets{12.0F, 8.0F, 12.0F, 8.0F};
    select_option.border_width = 1.0F;
    select_option.corner_radius = rendering::CornerRadius::uniform(4.0F);
    select_option.pixel_snapped_border = true;
    select_option.min_height = 32.0F;
    set_theme_style_class(theme, theme_class::select_option, select_option);
    auto scrollbar_style = panel_style;
    scrollbar_style.background = rendering::Color::rgba(0, 0, 0, 0);
    scrollbar_style.hover_background = rendering::Color::rgba(
        panel_style.semantic.secondary_text.red, panel_style.semantic.secondary_text.green,
        panel_style.semantic.secondary_text.blue, 76);
    scrollbar_style.active_background = rendering::Color::rgba(
        panel_style.semantic.secondary_text.red, panel_style.semantic.secondary_text.green,
        panel_style.semantic.secondary_text.blue, 128);
    scrollbar_style.border_width = 0.0F;
    scrollbar_style.corner_radius = rendering::CornerRadius::uniform(4.0F);
    scrollbar_style.min_width = 6.0F;
    scrollbar_style.min_height = 6.0F;
    set_theme_style_class(theme, theme_class::scrollbar, scrollbar_style);
    auto radio_style = panel_style;
    radio_style.background = input_style.background;
    radio_style.hover_background = input_style.background;
    radio_style.active_background = input_style.focus_border_color;
    radio_style.border_color = input_style.border_color;
    radio_style.focus_border_color = input_style.focus_border_color;
    radio_style.text_color = input_style.text_color;
    radio_style.padding = layout::EdgeInsets{};
    radio_style.min_height = 24.0F;
    set_theme_style_class(theme, theme_class::radio, radio_style);
    auto switch_style = panel_style;
    switch_style.background = input_style.border_color;
    switch_style.hover_background = input_style.semantic.hover_border;
    switch_style.active_background = input_style.focus_border_color;
    switch_style.border_width = 0.0F;
    switch_style.corner_radius = rendering::CornerRadius::uniform(10.0F);
    switch_style.min_width = 40.0F;
    switch_style.min_height = 20.0F;
    set_theme_style_class(theme, theme_class::switch_control, switch_style);
    auto items_control = panel_style;
    items_control.background = input_style.background;
    items_control.hover_background = rendering::Color::rgba(236, 245, 255);
    items_control.active_background = rendering::Color::rgba(217, 236, 255);
    items_control.border_color = input_style.border_color;
    items_control.focus_border_color = input_style.focus_border_color;
    items_control.border_width = 1.0F;
    items_control.corner_radius = rendering::CornerRadius::uniform(6.0F);
    items_control.pixel_snapped_border = true;
    items_control.padding = layout::EdgeInsets{};
    set_theme_style_class(theme, theme_class::items_control, items_control);
    auto path_style = panel_style;
    path_style.background = rendering::Color::rgba(0, 0, 0, 0);
    path_style.border_color = rendering::Color::rgba(0, 0, 0, 0);
    path_style.text_color = input_style.focus_border_color;
    path_style.min_width = 24.0F;
    path_style.min_height = 24.0F;
    set_theme_style_class(theme, theme_class::path, path_style);
}

[[nodiscard]] const UIElementStyle&
current_theme_style_or_fallback(std::string_view style_class,
                                const UIElementStyle& fallback_style) {
    if (const auto* style = theme_style_for_class(current_theme(), style_class); style != nullptr) {
        return *style;
    }

    return fallback_style;
}

} // namespace

bool has_rounded_corners(rendering::CornerRadius radius) noexcept {
    const auto normalized = normalized_radius(radius);
    return normalized.x > 0.0F || normalized.y > 0.0F;
}

RectangleStyle rectangle_style_from(const UIElementStyle& style, rendering::Color background,
                                    rendering::Color border_color) noexcept {
    return RectangleStyle{.background = background,
                          .border_color = border_color,
                          .border_width = non_negative(style.border_width),
                          .corner_radius = normalized_radius(style.corner_radius),
                          .shadow = normalized_shadow(style.shadow),
                          .shadow_visible = style.shadow_visible,
                          .pixel_snapped_border = style.pixel_snapped_border,
                          .border_dash_style = style.border_dash_style};
}

void paint_rectangle(rendering::RenderContext& context, layout::Rect rect,
                     const RectangleStyle& style) {
    if (!layout::is_finite_rect(rect)) {
        return;
    }

    const auto radius = normalized_radius(style.corner_radius);
    const auto rounded = has_rounded_corners(radius);
    const auto border_width = non_negative(style.border_width);
    const auto shadow = normalized_shadow(style.shadow);

    if (style.shadow_visible && has_visible_alpha(shadow.color)) {
        context.draw_box_shadow(rect, shadow);
    }

    if (has_visible_alpha(style.background)) {
        if (rounded) {
            context.fill_rounded_rect(rect, radius, style.background);
        } else {
            context.fill_rect(rect, style.background);
        }
    }

    if (border_width <= 0.0F || !has_visible_alpha(style.border_color)) {
        return;
    }

    const bool has_dash = style.border_dash_style != rendering::StrokeDashStyle::Solid;

    if (has_dash) {
        // For dashed borders, construct a geometry and use stroke_geometry
        // to support StrokeDashStyle properly.
        rendering::Geometry g;
        rendering::GeometryFigure fig;
        fig.begin = rendering::GeometryFigureBegin::Filled;
        fig.end = rendering::GeometryFigureEnd::Closed;

        if (rounded) {
            const auto rx = std::max(radius.x, 0.0F);
            const auto ry = std::max(radius.y, 0.0F);
            const auto l = rect.x;
            const auto t = rect.y;
            const auto r = rect.x + rect.width;
            const auto b = rect.y + rect.height;

            if (rx > 0.0F && ry > 0.0F) {
                fig.start = layout::Point{l + rx, t};
                // Top-left corner
                fig.segments.push_back(rendering::GeometrySegment{
                    .type = rendering::GeometrySegmentType::Arc,
                    .point = {l, t + ry},
                    .radius = {rx, ry},
                    .rotation_angle = 0.0F,
                    .arc_size = rendering::GeometryArcSize::Small,
                    .sweep_direction = rendering::GeometryArcSweepDirection::CounterClockwise});
                // Left edge
                fig.segments.push_back(rendering::GeometrySegment{
                    .type = rendering::GeometrySegmentType::Line, .point = {l, b - ry}});
                // Bottom-left corner
                fig.segments.push_back(rendering::GeometrySegment{
                    .type = rendering::GeometrySegmentType::Arc,
                    .point = {l + rx, b},
                    .radius = {rx, ry},
                    .rotation_angle = 0.0F,
                    .arc_size = rendering::GeometryArcSize::Small,
                    .sweep_direction = rendering::GeometryArcSweepDirection::CounterClockwise});
                // Bottom edge
                fig.segments.push_back(rendering::GeometrySegment{
                    .type = rendering::GeometrySegmentType::Line, .point = {r - rx, b}});
                // Bottom-right corner
                fig.segments.push_back(rendering::GeometrySegment{
                    .type = rendering::GeometrySegmentType::Arc,
                    .point = {r, b - ry},
                    .radius = {rx, ry},
                    .rotation_angle = 0.0F,
                    .arc_size = rendering::GeometryArcSize::Small,
                    .sweep_direction = rendering::GeometryArcSweepDirection::CounterClockwise});
                // Right edge
                fig.segments.push_back(rendering::GeometrySegment{
                    .type = rendering::GeometrySegmentType::Line, .point = {r, t + ry}});
                // Top-right corner
                fig.segments.push_back(rendering::GeometrySegment{
                    .type = rendering::GeometrySegmentType::Arc,
                    .point = {r - rx, t},
                    .radius = {rx, ry},
                    .rotation_angle = 0.0F,
                    .arc_size = rendering::GeometryArcSize::Small,
                    .sweep_direction = rendering::GeometryArcSweepDirection::CounterClockwise});
            } else {
                fig.start = layout::Point{l, t};
                fig.segments.push_back(rendering::GeometrySegment{
                    .type = rendering::GeometrySegmentType::Line, .point = {r, t}});
                fig.segments.push_back(rendering::GeometrySegment{
                    .type = rendering::GeometrySegmentType::Line, .point = {r, b}});
                fig.segments.push_back(rendering::GeometrySegment{
                    .type = rendering::GeometrySegmentType::Line, .point = {l, b}});
            }
        } else {
            fig.start = layout::Point{rect.x, rect.y};
            fig.segments.push_back(
                rendering::GeometrySegment{.type = rendering::GeometrySegmentType::Line,
                                           .point = {rect.x + rect.width, rect.y}});
            fig.segments.push_back(
                rendering::GeometrySegment{.type = rendering::GeometrySegmentType::Line,
                                           .point = {rect.x + rect.width, rect.y + rect.height}});
            fig.segments.push_back(
                rendering::GeometrySegment{.type = rendering::GeometrySegmentType::Line,
                                           .point = {rect.x, rect.y + rect.height}});
        }

        g.figures.push_back(std::move(fig));
        context.stroke_geometry(g, style.border_color,
                                rendering::GeometryStrokeStyle{
                                    .width = border_width, .dash_style = style.border_dash_style});
    } else if (rounded) {
        context.stroke_rounded_rect(rect, radius, style.border_color, border_width);
    } else if (style.pixel_snapped_border) {
        context.stroke_pixel_snapped_rect(rect, style.border_color, border_width);
    } else {
        context.stroke_rect(rect, style.border_color, border_width);
    }
}

Theme make_default_theme() {
    auto theme = Theme{};
    register_builtin_theme_classes(theme, make_default_panel_style(), make_default_button_style(),
                                   make_default_input_style(), make_default_text_style());
    return theme;
}

Theme make_dark_theme() {
    auto theme = Theme{};
    register_builtin_theme_classes(theme, make_dark_panel_style(), make_dark_button_style(),
                                   make_dark_input_style(), make_dark_text_style());
    return theme;
}

const Theme& current_theme() {
    return active_theme_storage();
}

void set_theme(Theme theme) {
    theme.generation = active_theme_storage().generation + 1;
    active_theme_storage() = std::move(theme);
}

void reset_theme() {
    auto theme = make_default_theme();
    theme.generation = active_theme_storage().generation + 1;
    active_theme_storage() = std::move(theme);
}

const UIElementStyle* theme_style_for_class(const Theme& theme,
                                            std::string_view style_class) noexcept {
    if (style_class.empty()) {
        return nullptr;
    }

    const auto iterator = theme.style_classes.find(style_class);
    return iterator != theme.style_classes.end() ? std::addressof(iterator->second) : nullptr;
}

UIElementStyle style_for_class_or(const Theme& theme, std::string_view style_class,
                                  UIElementStyle fallback_style) {
    if (const auto* style = theme_style_for_class(theme, style_class)) {
        return *style;
    }
    return fallback_style;
}

void set_theme_style_class(Theme& theme, std::string_view style_class, UIElementStyle style) {
    if (style_class.empty()) {
        throw std::invalid_argument("theme style class must not be empty");
    }

    theme.style_classes.insert_or_assign(std::string(style_class), std::move(style));
}

bool remove_theme_style_class(Theme& theme, std::string_view style_class) noexcept {
    const auto iterator = theme.style_classes.find(style_class);
    if (iterator == theme.style_classes.end()) {
        return false;
    }

    theme.style_classes.erase(iterator);
    return true;
}

const UIElementStyle& default_button_style() {
    return current_theme_style_or_fallback(theme_class::button, fallback_button_style());
}

const UIElementStyle& default_primary_button_style() {
    return current_theme_style_or_fallback(theme_class::button_primary,
                                           fallback_primary_button_style());
}

const UIElementStyle& default_success_button_style() {
    return current_theme_style_or_fallback(theme_class::button_success,
                                           fallback_success_button_style());
}

const UIElementStyle& default_warning_button_style() {
    return current_theme_style_or_fallback(theme_class::button_warning,
                                           fallback_warning_button_style());
}

const UIElementStyle& default_danger_button_style() {
    return current_theme_style_or_fallback(theme_class::button_danger,
                                           fallback_danger_button_style());
}

const UIElementStyle& default_info_button_style() {
    return current_theme_style_or_fallback(theme_class::button_info, fallback_info_button_style());
}

const UIElementStyle& default_text_button_style() {
    return current_theme_style_or_fallback(theme_class::button_text, fallback_text_button_style());
}

const UIElementStyle& default_input_style() {
    return current_theme_style_or_fallback(theme_class::input, fallback_input_style());
}

const UIElementStyle& default_large_input_style() {
    return current_theme_style_or_fallback(theme_class::input_large, fallback_large_input_style());
}

const UIElementStyle& default_small_input_style() {
    return current_theme_style_or_fallback(theme_class::input_small, fallback_small_input_style());
}

const UIElementStyle& default_text_style() {
    return current_theme_style_or_fallback(theme_class::text, fallback_text_style());
}

const UIElementStyle& default_border_style() {
    return current_theme_style_or_fallback(theme_class::border, fallback_border_style());
}

const UIElementStyle& default_context_menu_style() {
    return current_theme_style_or_fallback(theme_class::context_menu,
                                           fallback_context_menu_style());
}

const UIElementStyle& default_select_style() {
    return current_theme_style_or_fallback(theme_class::select, fallback_select_style());
}

const UIElementStyle& default_select_option_style() {
    return current_theme_style_or_fallback(theme_class::select_option,
                                           fallback_select_option_style());
}

const UIElementStyle& default_scrollbar_style() {
    return current_theme_style_or_fallback(theme_class::scrollbar, fallback_scrollbar_style());
}

const UIElementStyle& default_radio_style() {
    return current_theme_style_or_fallback(theme_class::radio, fallback_radio_style());
}

const UIElementStyle& default_switch_style() {
    return current_theme_style_or_fallback(theme_class::switch_control, fallback_switch_style());
}

const UIElementStyle& default_items_control_style() {
    return current_theme_style_or_fallback(theme_class::items_control,
                                           fallback_items_control_style());
}

const UIElementStyle& default_panel_style() {
    return current_theme_style_or_fallback(theme_class::panel, fallback_panel_style());
}

} // namespace winelement::style
