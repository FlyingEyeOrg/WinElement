#pragma once

#include <winelement/animation/easing.hpp>
#include <winelement/layout/layout_types.hpp>
#include <winelement/rendering/render_types.hpp>

#include <chrono>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace winelement::rendering {
class RenderContext;
}

namespace winelement::style {

using StyleDuration = std::chrono::duration<float>;

enum class StyleProperty {
    All,
    Background,
    BorderColor,
    BorderWidth,
    CornerRadius,
    Shadow,
    Margin,
    Padding,
    Overflow,
    TextColor,
    TextSelection,
    Opacity,
    Transform,
    ZIndex
};

enum class TextSelectionMode {
    None,
    Text,
};

struct TransitionStyle {
    bool enabled = false;
    StyleProperty property = StyleProperty::All;
    StyleDuration delay{0.0F};
    StyleDuration duration{0.15F};
    animation::EasingFunction easing = animation::EasingFunction::ease_out_cubic();

    [[nodiscard]] friend bool operator==(const TransitionStyle&,
                                         const TransitionStyle&) = default;
};

struct VisualStyle {
    float opacity = 1.0F;
    rendering::Transform2D transform{};
    bool layer_enabled = false;

    [[nodiscard]] friend constexpr bool operator==(VisualStyle, VisualStyle) noexcept = default;
};

enum class MaterialKind {
    None,
    Mica,
    Acrylic,
};

struct MaterialStyle {
    MaterialKind kind = MaterialKind::None;
    rendering::Color tint_color = rendering::Color::rgba(255, 255, 255);
    float tint_opacity = 0.0F;
    float fallback_opacity = 1.0F;

    [[nodiscard]] friend constexpr bool operator==(MaterialStyle, MaterialStyle) noexcept = default;
};

struct SystemColorTokens {
    rendering::Color accent = rendering::Color::rgba(64, 158, 255);
    rendering::Color accent_text = rendering::Color::rgba(255, 255, 255);
    bool dark_mode = false;
    bool high_contrast = false;

    [[nodiscard]] friend constexpr bool operator==(SystemColorTokens,
                                                   SystemColorTokens) noexcept = default;
};

struct RectangleStyle {
    rendering::Color background = rendering::Color::rgba(255, 255, 255);
    rendering::Color border_color = rendering::Color::rgba(220, 223, 230);
    float border_width = 1.0F;
    rendering::CornerRadius corner_radius{};
    rendering::ShadowStyle shadow{};
    bool shadow_visible = false;
    bool pixel_snapped_border = false;
    rendering::StrokeDashStyle border_dash_style = rendering::StrokeDashStyle::Solid;

    [[nodiscard]] friend constexpr bool operator==(const RectangleStyle&,
                                                   const RectangleStyle&) noexcept = default;
};

struct SemanticColorTokens {
    rendering::Color secondary_text = rendering::Color::rgba(144, 147, 153);
    rendering::Color disabled_text = rendering::Color::rgba(168, 171, 178);
    rendering::Color hover_border = rendering::Color::rgba(192, 196, 204);
    rendering::Color surface_subtle = rendering::Color::rgba(245, 247, 250);
    rendering::Color info = rendering::Color::rgba(144, 147, 153);
    rendering::Color success = rendering::Color::rgba(103, 194, 58);
    rendering::Color warning = rendering::Color::rgba(230, 162, 60);
    rendering::Color danger = rendering::Color::rgba(245, 108, 108);

    [[nodiscard]] friend constexpr bool operator==(SemanticColorTokens,
                                                   SemanticColorTokens) noexcept = default;
};

struct UIElementStyle {
    rendering::Color background = rendering::Color::rgba(255, 255, 255);
    rendering::Color hover_background = rendering::Color::rgba(236, 245, 255);
    rendering::Color active_background = rendering::Color::rgba(51, 126, 204);
    rendering::Color read_only_background = rendering::Color::rgba(245, 247, 250);
    rendering::Color border_color = rendering::Color::rgba(220, 223, 230);
    rendering::Color focus_border_color = rendering::Color::rgba(64, 158, 255);
    rendering::Color text_color = rendering::Color::rgba(48, 49, 51);
    rendering::Color placeholder_color = rendering::Color::rgba(168, 171, 178);
    rendering::Color caret_color = rendering::Color::rgba(64, 158, 255);
    rendering::Color text_selection_background = rendering::Color::rgba(64, 158, 255, 72);
    layout::EdgeInsets margin{};
    layout::EdgeInsets padding{8.0F, 6.0F, 8.0F, 6.0F};
    float border_width = 1.0F;
    float font_size = 14.0F;
    float min_width = 0.0F;
    float min_height = 0.0F;
    float caret_width = 1.0F;
    rendering::CornerRadius corner_radius{};
    rendering::ShadowStyle shadow{};
    bool shadow_visible = false;
    bool pixel_snapped_border = false;
    rendering::StrokeDashStyle border_dash_style = rendering::StrokeDashStyle::Solid;
    layout::Overflow overflow = layout::Overflow::Visible;
    layout::BoxSizing box_sizing = layout::BoxSizing::BorderBox;
    TextSelectionMode text_selection_mode = TextSelectionMode::None;
    int z_index = 0;
    SemanticColorTokens semantic{};
    VisualStyle visual{};
    MaterialStyle material{};
    TransitionStyle transition{};

    [[nodiscard]] friend bool operator==(const UIElementStyle&,
                                         const UIElementStyle&) = default;
};

struct ThemeClassLess {
    using is_transparent = void;

    [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs < rhs;
    }
};

using ThemeStyleMap = std::map<std::string, UIElementStyle, ThemeClassLess>;

namespace theme_class {
inline constexpr std::string_view panel = "wm.panel";
inline constexpr std::string_view button = "wm.button";
inline constexpr std::string_view button_primary = "wm.button.primary";
inline constexpr std::string_view button_success = "wm.button.success";
inline constexpr std::string_view button_warning = "wm.button.warning";
inline constexpr std::string_view button_danger = "wm.button.danger";
inline constexpr std::string_view button_info = "wm.button.info";
inline constexpr std::string_view button_text = "wm.button.text";
inline constexpr std::string_view input = "wm.input";
inline constexpr std::string_view input_large = "wm.input.large";
inline constexpr std::string_view input_small = "wm.input.small";
inline constexpr std::string_view text = "wm.text";
inline constexpr std::string_view text_primary = "wm.text.primary";
inline constexpr std::string_view text_success = "wm.text.success";
inline constexpr std::string_view text_warning = "wm.text.warning";
inline constexpr std::string_view text_danger = "wm.text.danger";
inline constexpr std::string_view text_info = "wm.text.info";
inline constexpr std::string_view border = "wm.border";
inline constexpr std::string_view context_menu = "wm.context_menu";
inline constexpr std::string_view select = "wm.select";
inline constexpr std::string_view select_option = "wm.select.option";
inline constexpr std::string_view scrollbar = "wm.scrollbar";
inline constexpr std::string_view radio = "wm.radio";
inline constexpr std::string_view switch_control = "wm.switch";
inline constexpr std::string_view items_control = "wm.items_control";
inline constexpr std::string_view path = "wm.path";
inline constexpr std::string_view image = "wm.image";
} // namespace theme_class

struct Theme {
    ThemeStyleMap style_classes;
    SystemColorTokens system_colors{};
    std::uint64_t generation = 0;
};

[[nodiscard]] bool has_rounded_corners(rendering::CornerRadius radius) noexcept;
[[nodiscard]] RectangleStyle rectangle_style_from(const UIElementStyle& style,
                                                  rendering::Color background,
                                                  rendering::Color border_color) noexcept;
void paint_rectangle(rendering::RenderContext& context, layout::Rect rect,
                     const RectangleStyle& style);

[[nodiscard]] Theme make_default_theme();
[[nodiscard]] Theme make_dark_theme();
[[nodiscard]] const Theme& current_theme();
void set_theme(Theme theme);
void reset_theme();
[[nodiscard]] const UIElementStyle* theme_style_for_class(const Theme& theme,
                                                          std::string_view style_class) noexcept;
[[nodiscard]] UIElementStyle style_for_class_or(const Theme& theme, std::string_view style_class,
                                                UIElementStyle fallback_style);
void set_theme_style_class(Theme& theme, std::string_view style_class, UIElementStyle style);
[[nodiscard]] bool remove_theme_style_class(Theme& theme, std::string_view style_class) noexcept;

template <typename Configure>
[[nodiscard]] UIElementStyle style_from(UIElementStyle base_style, Configure&& configure) {
    std::forward<Configure>(configure)(base_style);
    return base_style;
}

template <typename Configure>
Theme& configure_theme_style_class(Theme& theme, std::string_view style_class,
                                   Configure&& configure) {
    auto next_style = style_for_class_or(theme, style_class, UIElementStyle{});
    std::forward<Configure>(configure)(next_style);
    set_theme_style_class(theme, style_class, std::move(next_style));
    return theme;
}

template <typename Configure>
Theme& configure_theme_style_class(Theme& theme, std::string_view style_class,
                                   UIElementStyle fallback_style, Configure&& configure) {
    auto next_style = style_for_class_or(theme, style_class, std::move(fallback_style));
    std::forward<Configure>(configure)(next_style);
    set_theme_style_class(theme, style_class, std::move(next_style));
    return theme;
}

[[nodiscard]] const UIElementStyle& default_button_style();
[[nodiscard]] const UIElementStyle& default_primary_button_style();
[[nodiscard]] const UIElementStyle& default_success_button_style();
[[nodiscard]] const UIElementStyle& default_warning_button_style();
[[nodiscard]] const UIElementStyle& default_danger_button_style();
[[nodiscard]] const UIElementStyle& default_info_button_style();
[[nodiscard]] const UIElementStyle& default_text_button_style();
[[nodiscard]] const UIElementStyle& default_input_style();
[[nodiscard]] const UIElementStyle& default_large_input_style();
[[nodiscard]] const UIElementStyle& default_small_input_style();
[[nodiscard]] const UIElementStyle& default_text_style();
[[nodiscard]] const UIElementStyle& default_border_style();
[[nodiscard]] const UIElementStyle& default_context_menu_style();
[[nodiscard]] const UIElementStyle& default_select_style();
[[nodiscard]] const UIElementStyle& default_select_option_style();
[[nodiscard]] const UIElementStyle& default_scrollbar_style();
[[nodiscard]] const UIElementStyle& default_radio_style();
[[nodiscard]] const UIElementStyle& default_switch_style();
[[nodiscard]] const UIElementStyle& default_items_control_style();
[[nodiscard]] const UIElementStyle& default_panel_style();
[[nodiscard]] const UIElementStyle& default_image_style();

} // namespace winelement::style
