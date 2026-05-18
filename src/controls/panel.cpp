#include <winelement/controls/panel.hpp>

#include "control_style.hpp"

#include <winelement/rendering/render_context.hpp>

#include <cmath>
#include <stdexcept>

namespace winelement::controls {

Panel::Panel() = default;

Panel& Panel::set_semantic_role(PanelSemanticRole role) noexcept {
    semantic_role_ = role;
    return *this;
}

Panel& Panel::set_title(std::string_view title) {
    title_ = std::string(title);
    set_semantics_label(title_);
    return *this;
}

Panel& Panel::set_background(rendering::Color color) noexcept {
    if (style_storage().background == color) {
        return *this;
    }

    detach_theme_management();
    style_storage().background = color;
    invalidate_paint();
    return *this;
}

Panel& Panel::set_border(rendering::Color color, float width) {
    if (!std::isfinite(width) || width < 0.0F) {
        throw std::invalid_argument("border width must be finite and non-negative");
    }

    if (style_storage().border_color == color && style_storage().border_width == width) {
        return *this;
    }

    detach_theme_management();
    style_storage().border_color = color;
    style_storage().border_width = width;
    invalidate_paint();
    return *this;
}

Panel& Panel::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    return *this;
}

PanelSemanticRole Panel::semantic_role() const noexcept {
    return semantic_role_;
}

const std::string& Panel::title() const noexcept {
    return title_;
}

void Panel::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    style::paint_rectangle(context, absolute_frame,
                           style::rectangle_style_from(style_storage(), style_storage().background,
                                                       style_storage().border_color));
}

} // namespace winelement::controls
