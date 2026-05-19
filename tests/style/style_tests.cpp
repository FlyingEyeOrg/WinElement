#include <winelement/style.hpp>

#include <winelement/rendering/render_command_list.hpp>

#include <gtest/gtest.h>

namespace {

using namespace winelement::layout;
using namespace winelement::rendering;
using namespace winelement::style;

struct ScopedThemeReset {
    ~ScopedThemeReset() {
        reset_theme();
    }
};

[[nodiscard]] const UIElementStyle& require_theme_style(const Theme& theme,
                                                        std::string_view style_class) {
    const auto* style = theme_style_for_class(theme, style_class);
    EXPECT_NE(style, nullptr);
    static const auto fallback = UIElementStyle{};
    return style != nullptr ? *style : fallback;
}

TEST(StyleTests, DefaultControlStylesExposeShapeMotionAndVisualTokens) {
    const auto& button = default_button_style();
    EXPECT_TRUE(has_rounded_corners(button.corner_radius));
    EXPECT_TRUE(button.pixel_snapped_border);
    EXPECT_TRUE(button.transition.enabled);
    EXPECT_EQ(button.transition.property, StyleProperty::All);
    EXPECT_GT(button.transition.duration.count(), 0.0F);
    EXPECT_TRUE(is_identity_transform(button.visual.transform));
    EXPECT_FLOAT_EQ(button.visual.opacity, 1.0F);

    const auto& text = default_text_style();
    EXPECT_FALSE(has_rounded_corners(text.corner_radius));
    EXPECT_FALSE(text.transition.enabled);
    EXPECT_EQ(text.text_selection_mode, TextSelectionMode::None);
    EXPECT_EQ(text.text_selection_background, Color::rgba(64, 158, 255, 72));

    const auto& panel = default_panel_style();
    EXPECT_EQ(panel.background.alpha, 0U);
    EXPECT_FLOAT_EQ(panel.border_width, 0.0F);
}

TEST(StyleTests, ThemeProviderCanOptTextIntoSelectionStyle) {
    ScopedThemeReset reset;
    auto theme = make_default_theme();
    auto text_style = require_theme_style(theme, theme_class::text);
    text_style.text_selection_mode = TextSelectionMode::Text;
    text_style.text_selection_background = Color::rgba(10, 20, 30, 96);
    set_theme_style_class(theme, theme_class::text, text_style);

    set_theme(theme);

    EXPECT_EQ(default_text_style().text_selection_mode, TextSelectionMode::Text);
    EXPECT_EQ(default_text_style().text_selection_background, Color::rgba(10, 20, 30, 96));
}

TEST(StyleTests, ComputedStyleResolverAndCacheTrackThemeGeneration) {
    auto theme = make_default_theme();
    auto button_style = require_theme_style(theme, theme_class::button);
    button_style.background = Color::rgba(1, 2, 3);
    set_theme_style_class(theme, "sample.cached", button_style);

    auto computed = resolve_computed_style(theme, "sample.cached", default_panel_style(), 12U);
    EXPECT_TRUE(computed.theme_class_matched);
    EXPECT_EQ(computed.value.background, Color::rgba(1, 2, 3));

    ComputedStyleCache cache(2U);
    cache.store(computed);
    const auto cached = cache.find(computed.key);
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->value.background, Color::rgba(1, 2, 3));
}

TEST(StyleTests, ThemeCarriesSystemColorsAndMaterialTokens) {
    auto theme = make_default_theme();
    theme.system_colors.accent = Color::rgba(12, 34, 56);
    theme.system_colors.dark_mode = true;

    auto panel_style = require_theme_style(theme, theme_class::panel);
    panel_style.material = MaterialStyle{.kind = MaterialKind::Mica,
                                         .tint_color = Color::rgba(12, 34, 56),
                                         .tint_opacity = 0.7F,
                                         .fallback_opacity = 0.9F};
    set_theme_style_class(theme, theme_class::panel, panel_style);

    const auto* stored = theme_style_for_class(theme, theme_class::panel);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(theme.system_colors.accent, Color::rgba(12, 34, 56));
    EXPECT_TRUE(theme.system_colors.dark_mode);
    EXPECT_EQ(stored->material.kind, MaterialKind::Mica);
    EXPECT_FLOAT_EQ(stored->material.tint_opacity, 0.7F);
}

TEST(StyleTests, DefaultInputStyleExposesSemanticDesignTokens) {
    const auto& input = default_input_style();

    EXPECT_EQ(input.semantic.secondary_text, Color::rgba(144, 147, 153));
    EXPECT_EQ(input.semantic.disabled_text, Color::rgba(168, 171, 178));
    EXPECT_EQ(input.semantic.hover_border, Color::rgba(192, 196, 204));
    EXPECT_EQ(input.semantic.surface_subtle, Color::rgba(245, 247, 250));
    EXPECT_EQ(input.semantic.info, Color::rgba(144, 147, 153));
    EXPECT_EQ(input.semantic.success, Color::rgba(103, 194, 58));
    EXPECT_EQ(input.semantic.warning, Color::rgba(230, 162, 60));
    EXPECT_EQ(input.semantic.danger, Color::rgba(245, 108, 108));
}

TEST(StyleTests, DefaultControlStylesUseElementSizingAndStateTokens) {
    const auto& button = default_button_style();
    EXPECT_FLOAT_EQ(button.padding.left, 15.0F);
    EXPECT_FLOAT_EQ(button.padding.top, 8.0F);
    EXPECT_FLOAT_EQ(button.padding.right, 15.0F);
    EXPECT_FLOAT_EQ(button.padding.bottom, 8.0F);
    EXPECT_EQ(button.border_color, Color::rgba(220, 223, 230));
    EXPECT_EQ(button.semantic.hover_border, Color::rgba(198, 226, 255));
    EXPECT_EQ(button.text_color, Color::rgba(96, 98, 102));

    const auto& input = default_input_style();
    EXPECT_FLOAT_EQ(input.padding.left, 11.0F);
    EXPECT_FLOAT_EQ(input.padding.top, 5.0F);
    EXPECT_FLOAT_EQ(input.padding.right, 11.0F);
    EXPECT_FLOAT_EQ(input.padding.bottom, 5.0F);
    EXPECT_EQ(input.text_color, Color::rgba(48, 49, 51));

    const auto& option = default_select_option_style();
    EXPECT_FLOAT_EQ(option.border_width, 1.0F);
    EXPECT_TRUE(option.pixel_snapped_border);
    EXPECT_TRUE(has_rounded_corners(option.corner_radius));
    EXPECT_EQ(option.background, Color::rgba(255, 255, 255));
    EXPECT_EQ(option.active_background, Color::rgba(236, 245, 255));
    EXPECT_EQ(option.focus_border_color, Color::rgba(64, 158, 255));

    const auto& context_menu = default_context_menu_style();
    EXPECT_FLOAT_EQ(context_menu.min_width, 136.0F);
    EXPECT_EQ(context_menu.background, option.background);

    const auto& scrollbar = default_scrollbar_style();
    EXPECT_EQ(scrollbar.background, Color::rgba(0, 0, 0, 0));
    EXPECT_EQ(scrollbar.hover_background, Color::rgba(144, 147, 153, 76));
    EXPECT_EQ(scrollbar.active_background, Color::rgba(144, 147, 153, 128));
    EXPECT_FLOAT_EQ(scrollbar.min_width, 6.0F);
    EXPECT_TRUE(has_rounded_corners(scrollbar.corner_radius));
}

TEST(StyleTests, BuiltInControlNamedClassesExposeElementSurfaceTokens) {
    const auto theme = make_default_theme();
    for (const auto style_class : {theme_class::button_primary, theme_class::button_success,
                                   theme_class::button_warning, theme_class::button_danger,
                                   theme_class::button_info,    theme_class::button_text,
                                   theme_class::input_large,    theme_class::input_small,
                                   theme_class::text_primary,   theme_class::text_success,
                                   theme_class::text_warning,   theme_class::text_danger,
                                   theme_class::text_info,      theme_class::border,
                                   theme_class::select,         theme_class::context_menu,
                                   theme_class::select_option,  theme_class::scrollbar,
                                   theme_class::radio,          theme_class::switch_control,
                                   theme_class::items_control,  theme_class::path}) {
        EXPECT_NE(theme_style_for_class(theme, style_class), nullptr) << style_class;
    }

    EXPECT_GT(default_primary_button_style().min_height, 0.0F);
    EXPECT_GT(default_large_input_style().min_height, default_input_style().min_height);
    EXPECT_LT(default_small_input_style().min_height, default_input_style().min_height);
    EXPECT_FLOAT_EQ(default_scrollbar_style().min_width, 6.0F);
    EXPECT_GT(default_radio_style().min_height, 0.0F);
    EXPECT_GT(default_switch_style().min_width, 0.0F);
    EXPECT_EQ(default_items_control_style().padding.left, 0.0F);
}

TEST(StyleTests, ElementColorPaletteMatchesDocumentedTokens) {
    constexpr auto colors = element_colors();

    EXPECT_EQ(colors.primary.base, Color::rgba(64, 158, 255));
    EXPECT_EQ(colors.primary.dark2, Color::rgba(51, 126, 204));
    EXPECT_EQ(colors.primary.light9, Color::rgba(236, 245, 255));
    EXPECT_EQ(colors.success.base, Color::rgba(103, 194, 58));
    EXPECT_EQ(colors.warning.base, Color::rgba(230, 162, 60));
    EXPECT_EQ(colors.danger.base, colors.error.base);
    EXPECT_EQ(colors.info.light8, Color::rgba(233, 233, 235));
    EXPECT_EQ(colors.neutral.text_primary, Color::rgba(48, 49, 51));
    EXPECT_EQ(colors.neutral.border_base, Color::rgba(220, 223, 230));
    EXPECT_EQ(colors.neutral.fill_blank, Color::rgba(255, 255, 255));
}

TEST(StyleTests, ThemeProviderOverridesAndResetsDefaultStyles) {
    ScopedThemeReset reset;
    auto theme = make_default_theme();
    auto input_style = require_theme_style(theme, theme_class::input);
    input_style.background = Color::rgba(1, 2, 3);
    set_theme_style_class(theme, theme_class::input, input_style);
    auto button_style = require_theme_style(theme, theme_class::button);
    button_style.text_color = Color::rgba(4, 5, 6);
    set_theme_style_class(theme, theme_class::button, button_style);
    auto panel_style = require_theme_style(theme, theme_class::panel);
    panel_style.border_width = 2.0F;
    set_theme_style_class(theme, theme_class::panel, panel_style);

    set_theme(theme);

    EXPECT_EQ(default_input_style().background, Color::rgba(1, 2, 3));
    EXPECT_EQ(default_button_style().text_color, Color::rgba(4, 5, 6));
    EXPECT_FLOAT_EQ(default_panel_style().border_width, 2.0F);

    reset_theme();

    const auto baseline = make_default_theme();
    EXPECT_EQ(default_input_style().background,
              require_theme_style(baseline, theme_class::input).background);
    EXPECT_EQ(default_button_style().text_color,
              require_theme_style(baseline, theme_class::button).text_color);
    EXPECT_FLOAT_EQ(default_panel_style().border_width,
                    require_theme_style(baseline, theme_class::panel).border_width);
}

TEST(StyleTests, ThemeStyleClassesRegisterUpdateAndRemoveNamedStyles) {
    auto theme = make_default_theme();
    const auto builtin_class_count = theme.style_classes.size();
    auto danger_button = require_theme_style(theme, theme_class::button);
    danger_button.background = Color::rgba(245, 108, 108);
    danger_button.text_color = Color::rgba(255, 255, 255);

    EXPECT_EQ(theme_style_for_class(theme, "button.danger"), nullptr);

    set_theme_style_class(theme, "button.danger", danger_button);

    const auto* registered = theme_style_for_class(theme, "button.danger");
    ASSERT_NE(registered, nullptr);
    EXPECT_EQ(registered->background, Color::rgba(245, 108, 108));
    EXPECT_EQ(registered->text_color, Color::rgba(255, 255, 255));

    auto updated = danger_button;
    updated.background = Color::rgba(196, 86, 86);
    set_theme_style_class(theme, "button.danger", updated);

    ASSERT_EQ(theme.style_classes.size(), builtin_class_count + 1U);
    ASSERT_NE(theme_style_for_class(theme, "button.danger"), nullptr);
    EXPECT_EQ(theme_style_for_class(theme, "button.danger")->background, Color::rgba(196, 86, 86));

    EXPECT_TRUE(remove_theme_style_class(theme, "button.danger"));
    EXPECT_FALSE(remove_theme_style_class(theme, "button.danger"));
    EXPECT_EQ(theme_style_for_class(theme, "button.danger"), nullptr);
    EXPECT_EQ(theme.style_classes.size(), builtin_class_count);
}

TEST(StyleTests, StyleFromAndThemeClassConfigurationSupportPartialEdits) {
    auto theme = make_default_theme();

    const auto cta = style_from(default_button_style(), [](UIElementStyle& style) {
        style.background = Color::rgba(10, 20, 30);
        style.text_color = Color::rgba(250, 251, 252);
        style.padding = EdgeInsets{18.0F, 9.0F, 18.0F, 9.0F};
    });
    set_theme_style_class(theme, "brand.cta", cta);

    configure_theme_style_class(theme, "brand.cta", [](UIElementStyle& style) {
        style.hover_background = Color::rgba(20, 40, 60);
        style.border_width = 2.0F;
    });

    const auto* registered = theme_style_for_class(theme, "brand.cta");
    ASSERT_NE(registered, nullptr);
    EXPECT_EQ(registered->background, Color::rgba(10, 20, 30));
    EXPECT_EQ(registered->hover_background, Color::rgba(20, 40, 60));
    EXPECT_EQ(registered->text_color, Color::rgba(250, 251, 252));
    EXPECT_FLOAT_EQ(registered->border_width, 2.0F);
    EXPECT_FLOAT_EQ(registered->padding.left, 18.0F);

    configure_theme_style_class(
        theme, "brand.badge", default_text_style(), [](UIElementStyle& style) {
            style.background = Color::rgba(236, 245, 255);
            style.text_color = Color::rgba(64, 158, 255);
            style.corner_radius = CornerRadius::uniform(999.0F);
        });

    const auto badge = style_for_class_or(theme, "brand.badge", default_panel_style());
    EXPECT_EQ(badge.background, Color::rgba(236, 245, 255));
    EXPECT_EQ(badge.text_color, Color::rgba(64, 158, 255));
    EXPECT_EQ(style_for_class_or(theme, "missing", default_panel_style()).background,
              default_panel_style().background);
}

TEST(StyleTests, BuiltInDarkThemeUsesDarkSurfaceAndReadableTextTokens) {
    const auto dark = make_dark_theme();
    const auto& dark_input = require_theme_style(dark, theme_class::input);

    EXPECT_EQ(dark_input.background, Color::rgba(30, 31, 34));
    EXPECT_EQ(dark_input.border_color, Color::rgba(76, 79, 85));
    EXPECT_EQ(dark_input.text_color, Color::rgba(229, 234, 243));
    EXPECT_EQ(dark_input.placeholder_color, Color::rgba(141, 144, 149));
    EXPECT_EQ(dark_input.semantic.surface_subtle, Color::rgba(36, 38, 43));
    EXPECT_EQ(dark_input.semantic.disabled_text, Color::rgba(110, 113, 122));
    EXPECT_EQ(dark_input.semantic.danger, Color::rgba(248, 152, 152));

    const auto light = make_default_theme();
    const auto& light_input = require_theme_style(light, theme_class::input);
    EXPECT_NE(dark_input.background, light_input.background);
    EXPECT_NE(dark_input.text_color, light_input.text_color);

    EXPECT_EQ(require_theme_style(dark, theme_class::select).background, dark_input.background);
    EXPECT_EQ(require_theme_style(dark, theme_class::input_large).background,
              dark_input.background);
    EXPECT_EQ(require_theme_style(dark, theme_class::input_small).text_color,
              dark_input.text_color);
    EXPECT_EQ(require_theme_style(dark, theme_class::radio).text_color, dark_input.text_color);
    EXPECT_EQ(require_theme_style(dark, theme_class::switch_control).active_background,
              dark_input.focus_border_color);
    EXPECT_EQ(require_theme_style(dark, theme_class::select_option).background,
              dark_input.background);
    EXPECT_EQ(require_theme_style(dark, theme_class::context_menu).background,
              dark_input.background);
    EXPECT_EQ(require_theme_style(dark, theme_class::text_primary).text_color,
              require_theme_style(dark, theme_class::text).text_color);
}

TEST(StyleTests, RectangleStylePaintsShadowRoundedFillAndBorder) {
    RenderCommandRecorder context;
    const auto rect = Rect{2.0F, 4.0F, 80.0F, 32.0F};
    const auto style = RectangleStyle{.background = Color::rgba(10, 20, 30),
                                      .border_color = Color::rgba(40, 50, 60),
                                      .border_width = 2.0F,
                                      .corner_radius = CornerRadius::uniform(4.0F),
                                      .shadow = ShadowStyle{.color = Color::rgba(0, 0, 0, 48),
                                                            .offset = {0.0F, 2.0F},
                                                            .blur_radius = 8.0F},
                                      .shadow_visible = true};

    paint_rectangle(context, rect, style);

    ASSERT_EQ(context.commands().size(), 3U);
    EXPECT_EQ(context.commands()[0].type(), RenderCommandType::DrawBoxShadow);
    EXPECT_EQ(context.commands()[1].type(), RenderCommandType::FillRoundedRect);
    EXPECT_EQ(context.commands()[2].type(), RenderCommandType::StrokeRoundedRect);
    EXPECT_EQ(context.payload<FillRoundedRectCommand>(1U).color, style.background);
    EXPECT_EQ(context.payload<StrokeRoundedRectCommand>(2U).color, style.border_color);
}

TEST(StyleTests, RectangleStyleCanUsePixelSnappedSquareBorder) {
    RenderCommandRecorder context;
    const auto style = RectangleStyle{.background = Color::rgba(255, 255, 255),
                                      .border_color = Color::rgba(220, 223, 230),
                                      .border_width = 1.0F,
                                      .pixel_snapped_border = true};

    paint_rectangle(context, Rect{0.0F, 0.0F, 40.0F, 20.0F}, style);

    ASSERT_EQ(context.commands().size(), 2U);
    EXPECT_EQ(context.commands()[0].type(), RenderCommandType::FillRect);
    EXPECT_EQ(context.commands()[1].type(), RenderCommandType::StrokePixelSnappedRect);
}

TEST(StyleTests, UiElementStyleResolvesRectangleStateValues) {
    auto style = default_button_style();
    style.border_width = -4.0F;
    style.corner_radius = CornerRadius{-2.0F, 6.0F};

    const auto rect_style =
        rectangle_style_from(style, style.hover_background, style.focus_border_color);

    EXPECT_EQ(rect_style.background, style.hover_background);
    EXPECT_EQ(rect_style.border_color, style.focus_border_color);
    EXPECT_FLOAT_EQ(rect_style.border_width, 0.0F);
    EXPECT_FLOAT_EQ(rect_style.corner_radius.x, 0.0F);
    EXPECT_FLOAT_EQ(rect_style.corner_radius.y, 6.0F);
}

} // namespace
