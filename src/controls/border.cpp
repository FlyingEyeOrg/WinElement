#include <winelement/controls/border.hpp>

#include <utility>

namespace winelement::controls {

Border::Border() : Control() {
    set_theme_class(style::theme_class::border);
    apply_semantic_style();
}

Border& Border::set_preset(BorderPreset preset) {
    if (preset_ == preset) {
        return *this;
    }

    preset_ = preset;
    apply_semantic_style();
    return *this;
}

Border& Border::set_shadow_preset(BorderShadow shadow) {
    if (shadow_preset_ == shadow) {
        return *this;
    }

    shadow_preset_ = shadow;
    apply_semantic_style();
    return *this;
}

Border& Border::set_section_role(BorderSectionRole role) noexcept {
    section_role_ = role;
    return *this;
}

Border& Border::set_title(std::string_view title) {
    title_ = std::string(title);
    set_semantics_label(title_);
    return *this;
}

Border& Border::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    return *this;
}

BorderPreset Border::preset() const noexcept {
    return preset_;
}

BorderShadow Border::shadow_preset() const noexcept {
    return shadow_preset_;
}

BorderSectionRole Border::section_role() const noexcept {
    return section_role_;
}

const std::string& Border::title() const noexcept {
    return title_;
}

void Border::apply_semantic_style() {
    auto next_style = style::default_border_style();
    switch (preset_) {
    case BorderPreset::Primary:
        next_style.border_color = rendering::Color::rgba(64, 158, 255);
        break;
    case BorderPreset::Success:
        next_style.border_color = next_style.semantic.success;
        break;
    case BorderPreset::Warning:
        next_style.border_color = next_style.semantic.warning;
        break;
    case BorderPreset::Danger:
        next_style.border_color = next_style.semantic.danger;
        break;
    case BorderPreset::Info:
        next_style.border_color = next_style.semantic.info;
        break;
    case BorderPreset::Plain:
        break;
    }

    switch (shadow_preset_) {
    case BorderShadow::Light:
        next_style.shadow_visible = true;
        next_style.shadow = rendering::ShadowStyle{.color = rendering::Color::rgba(31, 35, 41, 16),
                                                   .offset = {0.0F, 4.0F},
                                                   .blur_radius = 12.0F};
        break;
    case BorderShadow::Base:
        next_style.shadow_visible = true;
        next_style.shadow = rendering::ShadowStyle{.color = rendering::Color::rgba(31, 35, 41, 24),
                                                   .offset = {0.0F, 8.0F},
                                                   .blur_radius = 20.0F};
        break;
    case BorderShadow::Dark:
        next_style.shadow_visible = true;
        next_style.shadow = rendering::ShadowStyle{.color = rendering::Color::rgba(31, 35, 41, 40),
                                                   .offset = {0.0F, 12.0F},
                                                   .blur_radius = 28.0F};
        break;
    case BorderShadow::None:
        next_style.shadow_visible = false;
        break;
    }

    apply_style_value(std::move(next_style), true);
}

} // namespace winelement::controls
