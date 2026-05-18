#include <winelement/controls/text.hpp>

#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace winelement::controls {

Text::Text() : Control() {
    set_theme_class(style::theme_class::text);
    configure_layout(
        [](layout::LayoutElement& item) { item.set_element_kind(layout::ElementKind::Text); });
    set_measure_callback([this](const layout::MeasureInput& input) { return measure_text(input); });
    apply_semantic_style();
}

Text& Text::set_text(std::string_view text) {
    UIElement::set_text(text);
    mark_measure_dirty();
    return *this;
}

Text& Text::set_type(TextType type) {
    if (type_ == type) {
        return *this;
    }

    type_ = type;
    apply_semantic_style();
    return *this;
}

Text& Text::set_size(TextSize size) {
    if (size_ == size) {
        return *this;
    }

    size_ = size;
    apply_semantic_style();
    return *this;
}

Text& Text::set_truncated(bool truncated) {
    if (truncated_ == truncated) {
        return *this;
    }

    truncated_ = truncated;
    apply_semantic_style();
    return *this;
}

Text& Text::set_max_lines(std::size_t max_lines) {
    if (max_lines_ == max_lines) {
        return *this;
    }
    max_lines_ = max_lines;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Text& Text::set_selectable(bool selectable) {
    if (selectable_ == selectable) {
        return *this;
    }
    selectable_ = selectable;
    set_text_selection_mode(selectable_ ? style::TextSelectionMode::Text
                                        : style::TextSelectionMode::None);
    return *this;
}

Text& Text::set_copyable(bool copyable) {
    copyable_ = copyable;
    if (copyable_) {
        set_selectable(true);
    }
    return *this;
}

Text& Text::set_link_target(std::string_view target) {
    link_target_ = std::string(target);
    return *this;
}

Text& Text::set_font_size(float font_size) {
    UIElement::set_font_size(font_size);
    mark_measure_dirty();
    return *this;
}

Text& Text::set_color(rendering::Color color) noexcept {
    UIElement::set_text_color(color);
    return *this;
}

Text& Text::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    mark_measure_dirty();
    return *this;
}

TextType Text::type() const noexcept {
    return type_;
}

TextSize Text::size() const noexcept {
    return size_;
}

bool Text::truncated() const noexcept {
    return truncated_;
}

std::size_t Text::max_lines() const noexcept {
    return max_lines_;
}

bool Text::selectable() const noexcept {
    return selectable_;
}

bool Text::copyable() const noexcept {
    return copyable_;
}

const std::string& Text::link_target() const noexcept {
    return link_target_;
}

const std::string& Text::text() const noexcept {
    return text_storage();
}

void Text::apply_semantic_style() {
    auto next_style = style::default_text_style();
    switch (type_) {
    case TextType::Success:
        next_style.text_color = next_style.semantic.success;
        set_theme_class(style::theme_class::text_success);
        break;
    case TextType::Warning:
        next_style.text_color = next_style.semantic.warning;
        set_theme_class(style::theme_class::text_warning);
        break;
    case TextType::Danger:
        next_style.text_color = next_style.semantic.danger;
        set_theme_class(style::theme_class::text_danger);
        break;
    case TextType::Info:
        next_style.text_color = next_style.semantic.info;
        set_theme_class(style::theme_class::text_info);
        break;
    case TextType::Primary:
        set_theme_class(style::theme_class::text_primary);
        break;
    }

    switch (size_) {
    case TextSize::Large:
        next_style.font_size = 16.0F;
        break;
    case TextSize::Small:
        next_style.font_size = 12.0F;
        break;
    case TextSize::Default:
        break;
    }

    text_style_storage().wrapping =
        truncated_ ? rendering::TextWrapping::NoWrap : rendering::TextWrapping::Wrap;
    text_style_storage().trimming =
        truncated_ ? rendering::TextTrimming::CharacterEllipsis : rendering::TextTrimming::None;
    apply_style_value(std::move(next_style), true);
    mark_measure_dirty();
}

void Text::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    style::paint_rectangle(context, absolute_frame,
                           style::rectangle_style_from(style_storage(), style_storage().background,
                                                       style_storage().border_color));

    if (text_storage().empty()) {
        return;
    }

    auto text_rect = absolute_frame;
    const auto padding = style_storage().padding;
    text_rect.x += padding.left;
    text_rect.y += padding.top;
    text_rect.width = std::max(0.0F, text_rect.width - padding.left - padding.right);
    text_rect.height = std::max(0.0F, text_rect.height - padding.top - padding.bottom);
    if (!std::isfinite(text_rect.width) || !std::isfinite(text_rect.height) ||
        text_rect.width <= 0.0F || text_rect.height <= 0.0F) {
        return;
    }

    const auto text_layout = text_layout_for_content_rect(text_rect);
    if (max_lines_ == 0U) {
        context.draw_text_layout(text_layout, layout::Point{text_rect.x, text_rect.y});
        return;
    }
    context.save();
    context.push_clip(text_rect);
    context.draw_text_layout(text_layout, layout::Point{text_rect.x, text_rect.y});
    context.pop_clip();
    context.restore();
}

layout::Size Text::measure_text(const layout::MeasureInput& input) const {
    const auto& current_style = style_storage();
    const auto horizontal_padding = current_style.padding.left + current_style.padding.right;
    const auto vertical_padding = current_style.padding.top + current_style.padding.bottom;
    const auto content_width = input.width_mode == layout::MeasureMode::Undefined
                                   ? 0.0F
                                   : std::max(0.0F, input.available_width - horizontal_padding);
    const auto content_height = input.height_mode == layout::MeasureMode::Undefined
                                    ? 0.0F
                                    : std::max(0.0F, input.available_height - vertical_padding);
    auto text_style = text_style_storage();
    text_style.font_size = current_style.font_size;
    text_style.color = current_style.text_color;

    const auto text_size =
        text_style.wrapping == rendering::TextWrapping::NoWrap ||
                input.width_mode == layout::MeasureMode::Undefined
            ? text_engine().measure_single_line(text_storage(), text_style)
            : text_engine()
                  .layout_text(text_storage(), text_style,
                               rendering::TextLayoutOptions{.max_width = content_width,
                                                            .max_height = content_height,
                                                            .max_lines = max_lines_})
                  .size;
    return layout::Size{std::max(text_size.width + horizontal_padding, current_style.min_width),
                        std::max(text_size.height + vertical_padding, current_style.min_height)};
}

rendering::TextLayout Text::text_layout_for_content_rect(layout::Rect rect) const {
    auto text_style = text_style_storage();
    text_style.font_size = style_storage().font_size;
    text_style.color = style_storage().text_color;
    return text_engine().layout_text(
        text_storage(), text_style,
        rendering::TextLayoutOptions{.max_width = std::max(rect.width, 0.0F),
                                     .max_height = std::max(rect.height, 0.0F),
                                     .max_lines = max_lines_});
}

} // namespace winelement::controls
