#include <winelement/controls/button.hpp>

#include <winelement/controls/property_keys.hpp>

#include "control_style.hpp"

#include <winelement/elements/all_icons.hpp>
#include <winelement/rendering/render_context.hpp>
#include <winelement/style/element_colors.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace winelement::controls {
namespace {

constexpr auto pi = 3.14159265358979323846F;
constexpr auto menu_indicator_width = 12.0F;
constexpr auto split_indicator_width = 24.0F;

[[nodiscard]] bool contains_local_point(layout::Rect frame, layout::Point point) noexcept {
    return frame.width > 0.0F && frame.height > 0.0F && point.x >= 0.0F && point.y >= 0.0F &&
           point.x < frame.width && point.y < frame.height;
}

[[nodiscard]] rendering::Transform2D rotation_transform(float radians) noexcept {
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    return rendering::Transform2D{.m11 = cosine, .m12 = sine, .m21 = -sine, .m22 = cosine};
}

[[nodiscard]] float button_indicator_width(bool visible, bool split) noexcept {
    if (!visible) {
        return 0.0F;
    }
    return split ? split_indicator_width : menu_indicator_width;
}

[[nodiscard]] float button_icon_size_for(const style::UIElementStyle& style,
                                         float available_height) noexcept {
    return std::min(std::max(style.font_size, 0.0F), std::max(available_height, 0.0F));
}

struct ButtonColorScale {
    rendering::Color base{};
    rendering::Color dark2{};
    rendering::Color light5{};
    rendering::Color light9{};
};

[[nodiscard]] ButtonColorScale button_color_scale_for(ButtonType type) noexcept {
    constexpr auto colors = style::element_colors();
    switch (type) {
    case ButtonType::Success:
        return ButtonColorScale{colors.success.base, colors.success.dark2, colors.success.light5,
                                colors.success.light9};
    case ButtonType::Warning:
        return ButtonColorScale{colors.warning.base, colors.warning.dark2, colors.warning.light5,
                                colors.warning.light9};
    case ButtonType::Danger:
        return ButtonColorScale{colors.danger.base, colors.danger.dark2, colors.danger.light5,
                                colors.danger.light9};
    case ButtonType::Info:
        return ButtonColorScale{colors.info.base, colors.info.dark2, colors.info.light5,
                                colors.info.light9};
    case ButtonType::Primary:
    case ButtonType::Default:
    case ButtonType::Text:
    default:
        return ButtonColorScale{colors.primary.base, colors.primary.dark2, colors.primary.light5,
                                colors.primary.light9};
    }
}

} // namespace

Button::Button() : Control() {
    text_storage() = "Button";
    set_theme_class(style::theme_class::button);
    set_focusable(true);
    loading_icon_.set_icon_paths(elements::icons::Loading);
    apply_semantic_style();
    update_measure_callback();
}

Button::~Button() {
    hover_progress_.clear();
    pressed_progress_.clear();
    click_progress_.clear();
    loading_progress_.clear();
}

Button& Button::set_text(std::string_view text) {
    UIElement::set_text(text);
    invalidate_display_text_cache();
    mark_measure_dirty();
    return *this;
}

Button& Button::set_type(ButtonType type) {
    set_property(property_keys::button_type(), type);
    return *this;
}

Button& Button::set_size(ButtonSize size) {
    set_property(property_keys::button_size(), size);
    return *this;
}

Button& Button::set_disabled(bool disabled) {
    if (disabled_ == disabled) {
        return *this;
    }

    UIElement::set_disabled(disabled);
    set_pressed(false);
    if (disabled_) {
        animate_hover(0.0F);
        release_pointer_capture();
    }
    invalidate_paint();
    return *this;
}

Button& Button::set_loading(bool loading) {
    set_property(property_keys::button_loading(), loading);
    return *this;
}

Button& Button::set_plain(bool plain) {
    set_property(property_keys::button_plain(), plain);
    return *this;
}

Button& Button::set_round(bool round) {
    if (has_flag(flags_, ButtonFlag::Round) == round) {
        return *this;
    }

    if (round) {
        flags_ |= ButtonFlag::Round;
    } else {
        flags_ &= ~ButtonFlag::Round;
    }
    apply_semantic_style();
    return *this;
}

Button& Button::set_circle(bool circle) {
    if (has_flag(flags_, ButtonFlag::Circle) == circle) {
        return *this;
    }

    if (circle) {
        flags_ |= ButtonFlag::Circle;
    } else {
        flags_ &= ~ButtonFlag::Circle;
    }
    apply_semantic_style();
    return *this;
}

Button& Button::set_on_click(ClickHandler handler) {
    click_handler_ = std::move(handler);
    return *this;
}

Button& Button::set_dashed(bool dashed) {
    if (has_flag(flags_, ButtonFlag::Dashed) == dashed) {
        return *this;
    }

    if (dashed) {
        flags_ |= ButtonFlag::Dashed;
    } else {
        flags_ &= ~ButtonFlag::Dashed;
    }
    apply_semantic_style();
    return *this;
}

Button& Button::set_text_variant(bool text_variant) {
    if (has_flag(flags_, ButtonFlag::TextVariant) == text_variant) {
        return *this;
    }

    if (text_variant) {
        flags_ |= ButtonFlag::TextVariant;
    } else {
        flags_ &= ~ButtonFlag::TextVariant;
    }
    apply_semantic_style();
    return *this;
}

Button& Button::set_background_always(bool bg) {
    if (has_flag(flags_, ButtonFlag::BackgroundAlways) == bg) {
        return *this;
    }

    if (bg) {
        flags_ |= ButtonFlag::BackgroundAlways;
    } else {
        flags_ &= ~ButtonFlag::BackgroundAlways;
    }
    apply_semantic_style();
    return *this;
}

Button& Button::set_link_variant(bool link) {
    if (has_flag(flags_, ButtonFlag::LinkVariant) == link) {
        return *this;
    }

    if (link) {
        flags_ |= ButtonFlag::LinkVariant;
    } else {
        flags_ &= ~ButtonFlag::LinkVariant;
    }
    apply_semantic_style();
    return *this;
}

Button& Button::set_icon_geometry(rendering::Geometry geometry) {
    icon_.set_geometry(std::move(geometry));
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Button& Button::set_icon_data(std::string_view svg_path_data) {
    icon_.set_svg_path(svg_path_data);
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Button& Button::set_icon_paths(const elements::icons::IconPathsBase& icon_data) {
    icon_.set_icon_paths(icon_data);
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Button& Button::clear_icon() {
    if (!has_icon()) {
        return *this;
    }

    icon_.clear_icon();
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Button& Button::set_role(ButtonRole role) {
    role_ = role;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Button& Button::set_group_name(std::string_view group_name) {
    group_name_ = std::string(group_name);
    return *this;
}

Button& Button::set_menu_indicator_visible(bool visible) {
    menu_indicator_visible_ = visible;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Button& Button::set_split(bool split) {
    split_ = split;
    role_ = split_ ? ButtonRole::SplitPrimary : role_;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Button& Button::set_autofocus(bool autofocus) {
    if (autofocus) {
        flags_ |= ButtonFlag::Autofocus;
    } else {
        flags_ &= ~ButtonFlag::Autofocus;
    }
    return *this;
}

Button& Button::set_custom_color(rendering::Color color) {
    custom_color_ = color;
    invalidate_paint();
    return *this;
}

Button& Button::clear_custom_color() {
    if (!custom_color_.has_value()) {
        return *this;
    }

    custom_color_.reset();
    invalidate_paint();
    return *this;
}

Button& Button::set_dark_mode(bool dark) {
    if (has_flag(flags_, ButtonFlag::DarkMode) == dark) {
        return *this;
    }

    if (dark) {
        flags_ |= ButtonFlag::DarkMode;
    } else {
        flags_ &= ~ButtonFlag::DarkMode;
    }
    invalidate_paint();
    return *this;
}

Button& Button::set_auto_insert_space(bool auto_space) {
    if (auto_space) {
        flags_ |= ButtonFlag::AutoInsertSpace;
    } else {
        flags_ &= ~ButtonFlag::AutoInsertSpace;
    }
    invalidate_paint();
    return *this;
}

Button& Button::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    mark_measure_dirty();
    return *this;
}

bool Button::pressed() const noexcept {
    return has_flag(flags_, ButtonFlag::Pressed);
}

ButtonType Button::type() const noexcept {
    return type_;
}

ButtonSize Button::size() const noexcept {
    return size_;
}

bool Button::disabled() const noexcept {
    return disabled_;
}

bool Button::loading() const noexcept {
    return has_flag(flags_, ButtonFlag::Loading);
}

bool Button::plain() const noexcept {
    return has_flag(flags_, ButtonFlag::Plain);
}

bool Button::round() const noexcept {
    return has_flag(flags_, ButtonFlag::Round);
}

bool Button::circle() const noexcept {
    return has_flag(flags_, ButtonFlag::Circle);
}

bool Button::dashed() const noexcept {
    return has_flag(flags_, ButtonFlag::Dashed);
}

bool Button::text_variant() const noexcept {
    return has_flag(flags_, ButtonFlag::TextVariant);
}

bool Button::background_always() const noexcept {
    return has_flag(flags_, ButtonFlag::BackgroundAlways);
}

bool Button::link_variant() const noexcept {
    return has_flag(flags_, ButtonFlag::LinkVariant);
}

bool Button::has_icon() const noexcept {
    return icon_.has_icon();
}

ButtonRole Button::role() const noexcept {
    return role_;
}

const std::string& Button::group_name() const noexcept {
    return group_name_;
}

bool Button::menu_indicator_visible() const noexcept {
    return menu_indicator_visible_;
}

bool Button::split() const noexcept {
    return split_;
}

bool Button::autofocus() const noexcept {
    return has_flag(flags_, ButtonFlag::Autofocus);
}

const std::optional<rendering::Color>& Button::custom_color() const noexcept {
    return custom_color_;
}

bool Button::dark_mode() const noexcept {
    return has_flag(flags_, ButtonFlag::DarkMode);
}

bool Button::auto_insert_space() const noexcept {
    return has_flag(flags_, ButtonFlag::AutoInsertSpace);
}

void Button::on_pointer_event(elements::PointerEvent& event) {
    if (event.kind == elements::PointerEventKind::Leave) {
        hovered_ = false;
        animate_hover(0.0F);
        set_pressed(false);
        release_pointer_capture();
        invalidate_paint();
        return;
    }

    if (event.kind == elements::PointerEventKind::Cancel) {
        const auto was_pressed = has_flag(flags_, ButtonFlag::Pressed);
        set_pressed(false);
        release_pointer_capture();
        event.handled = was_pressed;
        return;
    }

    if (disabled_ || has_flag(flags_, ButtonFlag::Loading)) {
        return;
    }

    if (event.kind == elements::PointerEventKind::Enter ||
        event.kind == elements::PointerEventKind::Move) {
        if (!hovered_) {
            hovered_ = true;
            animate_hover(1.0F);
            invalidate_paint();
        }
    }

    if (event.kind == elements::PointerEventKind::Down ||
        event.kind == elements::PointerEventKind::DoubleClick) {
        if (event.button != elements::PointerButton::Primary) {
            return;
        }

        set_pressed(true);
        static_cast<void>(capture_pointer());
        event.handled = true;
        return;
    }

    if (event.kind == elements::PointerEventKind::Up && has_flag(flags_, ButtonFlag::Pressed)) {
        const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
        const auto should_click = event.button == elements::PointerButton::Primary &&
                                  contains_local_point(local_frame, event.local_position);
        set_pressed(false);
        release_pointer_capture();
        if (should_click) {
            click();
        }
        event.handled = true;
    }
}

void Button::on_key_event(elements::KeyEvent& event) {
    if (disabled_ || has_flag(flags_, ButtonFlag::Loading)) {
        return;
    }

    if (event.kind != elements::KeyEventKind::Down) {
        return;
    }

    if (event.key == elements::Key::Enter || event.key == elements::Key::Space) {
        focus_visible_ = true;
        invalidate_paint();
        click();
        event.handled = true;
    }
}

void Button::on_focus_changed(const elements::FocusChangeEvent& event) {
    const auto next_focus_visible = event.focused && event.focus_visible;
    if (focus_visible_ != next_focus_visible) {
        focus_visible_ = next_focus_visible;
        invalidate_paint();
    }
    if (!event.focused) {
        set_pressed(false);
        release_pointer_capture();
    }
}

elements::PointerCursor
Button::cursor_for_local_point(layout::Point local_position) const noexcept {
    if (disabled_ || has_flag(flags_, ButtonFlag::Loading)) {
        return elements::PointerCursor::Default;
    }
    const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    return contains_local_point(local_frame, local_position) ? elements::PointerCursor::Hand
                                                             : elements::PointerCursor::Default;
}

bool Button::on_animation_frame(animation::AnimationTimePoint now) {
    auto active = hover_progress_.tick(now);
    active = pressed_progress_.tick(now) || active;
    active = click_progress_.tick(now) || active;
    active = loading_progress_.tick(now) || active;
    if (active) {
        invalidate_paint();
    }
    return active;
}

void Button::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    const auto hover_progress = animated_hover_progress();
    const auto pressed_progress = animated_pressed_progress();
    const auto focus_hover_progress = std::max(hover_progress, focus_visible_ ? 1.0F : 0.0F);
    const auto& current_style = style_storage();
    const auto hover_background = animation::interpolate_value(
        current_style.background, current_style.hover_background, focus_hover_progress);
    const auto interactive_background = animation::interpolate_value(
        hover_background, current_style.active_background, pressed_progress);
    const auto background = disabled_ ? current_style.read_only_background : interactive_background;
    const auto hover_border = animation::interpolate_value(
        current_style.border_color, current_style.semantic.hover_border, focus_hover_progress);
    const auto active_border = animation::interpolate_value(
        hover_border, current_style.focus_border_color, pressed_progress);
    const auto border = focus_visible_ ? current_style.focus_border_color : active_border;
    const auto is_link = has_flag(flags_, ButtonFlag::LinkVariant);
    style::paint_rectangle(context, absolute_frame,
                           style::rectangle_style_from(style_storage(), background, border));

    const auto content_rect = detail::inset_rect(absolute_frame, style_storage().padding);
    if (!std::isfinite(content_rect.width) || !std::isfinite(content_rect.height) ||
        content_rect.width <= 0.0F || content_rect.height <= 0.0F) {
        return;
    }

    auto text_rect = content_rect;
    const auto icon_is_visible = icon_visible();
    const auto icon_size = button_icon_size_for(current_style, content_rect.height);
    const auto display = display_text();
    const auto icon_gap = icon_is_visible && !display.empty() ? 6.0F : 0.0F;
    const auto indicator_visible = menu_indicator_displayed();
    const auto indicator_width = button_indicator_width(indicator_visible, split_);
    if (indicator_visible) {
        text_rect.width = std::max(0.0F, text_rect.width - indicator_width);
    }
    if (icon_is_visible) {
        const auto text_metrics =
            display.empty()
                ? layout::Size{}
                : text_engine().measure_single_line(
                      display, rendering::TextStyle{.font_size = current_style.font_size,
                                                    .color = current_style.text_color,
                                                    .alignment = rendering::TextAlignment::Start,
                                                    .wrapping = rendering::TextWrapping::NoWrap});
        const auto text_available_width = std::max(0.0F, text_rect.width - icon_size - icon_gap);
        const auto text_fits = text_metrics.width <= text_available_width;
        const auto text_layout_width =
            display.empty() ? 0.0F : (text_fits ? text_metrics.width : text_available_width);
        const auto occupied_width = icon_size + icon_gap + text_layout_width;
        const auto cluster_x =
            text_rect.x + std::max(0.0F, text_rect.width - occupied_width) * 0.5F;
        const auto icon_rect =
            layout::Rect{cluster_x, content_rect.y + (content_rect.height - icon_size) * 0.5F,
                         icon_size, icon_size};
        const auto icon_color = interactive_text_color();
        if (loading()) {
            const auto center = layout::Point{icon_rect.x + icon_rect.width * 0.5F,
                                              icon_rect.y + icon_rect.height * 0.5F};
            context.push_layer(rendering::RenderLayerOptions{
                .bounds = icon_rect,
                .transform = rendering::transform_around_point(
                    rotation_transform(pi * 2.0F * animated_loading_progress()), center),
                .clips_to_bounds = false});
            loading_icon_.paint_icon(context, icon_rect, icon_color);
            context.pop_layer();
        } else {
            icon_.paint_icon(context, icon_rect, icon_color);
        }
        text_rect.x = cluster_x + icon_size + icon_gap;
        text_rect.width = text_layout_width;
    }

    const auto text_paint_rect =
        layout::Rect{text_rect.x, absolute_frame.y, text_rect.width, absolute_frame.height};
    const auto text_layout = create_text_layout(text_paint_rect);
    const auto origin = layout::Point{text_paint_rect.x, text_paint_rect.y};
    context.save();
    context.push_clip(
        layout::Rect{content_rect.x, absolute_frame.y, content_rect.width, absolute_frame.height});
    context.draw_text_layout(text_layout, origin);
    context.pop_clip();
    context.restore();

    if (indicator_visible) {
        const auto center_x = content_rect.x + content_rect.width - menu_indicator_width * 0.5F;
        const auto center_y = content_rect.y + content_rect.height * 0.5F + 1.0F;
        const auto indicator_color = interactive_text_color();
        context.draw_line(layout::Point{center_x - 3.5F, center_y - 2.0F},
                          layout::Point{center_x, center_y + 2.0F}, indicator_color, 1.4F);
        context.draw_line(layout::Point{center_x, center_y + 2.0F},
                          layout::Point{center_x + 3.5F, center_y - 2.0F}, indicator_color, 1.4F);
        if (split_) {
            const auto divider_x =
                std::floor(content_rect.x + content_rect.width - split_indicator_width + 6.0F) +
                0.5F;
            const auto divider_base = interactive_text_color();
            const auto divider_color = rendering::Color::rgba(
                divider_base.red, divider_base.green, divider_base.blue,
                static_cast<std::uint8_t>(std::clamp(
                    std::round(static_cast<float>(divider_base.alpha) * 0.32F), 0.0F, 255.0F)));
            context.draw_line(layout::Point{divider_x, content_rect.y + 4.0F},
                              layout::Point{divider_x, content_rect.y + content_rect.height - 4.0F},
                              divider_color, 1.0F);
        }
    }

    // link_variant: draw underline on hover
    if (is_link && (hover_progress > 0.0F || focus_visible_)) {
        const auto text_color = interactive_text_color();
        const auto underline_y = origin.y + text_layout.size.height;
        context.draw_line(layout::Point{origin.x, underline_y},
                          layout::Point{origin.x + text_rect.width, underline_y}, text_color, 1.0F);
    }
}

void Button::update_measure_callback() {
    set_measure_callback([this](const layout::MeasureInput&) {
        const auto text = display_text();
        const auto text_size = text_engine().measure_single_line(
            text, rendering::TextStyle{.font_size = style_storage().font_size,
                                       .color = style_storage().text_color,
                                       .alignment = rendering::TextAlignment::Start,
                                       .wrapping = rendering::TextWrapping::NoWrap});
        const auto min_width = has_flag(flags_, ButtonFlag::Circle) ? style_storage().min_height
                                                                    : style_storage().min_width;
        const auto icon_size = button_icon_size_for(style_storage(), style_storage().font_size);
        const auto icon_width = icon_visible() ? icon_size + (text.empty() ? 0.0F : 6.0F) : 0.0F;
        const auto indicator_width = button_indicator_width(menu_indicator_displayed(), split_);
        return layout::Size{std::max(text_size.width + icon_width + style_storage().padding.left +
                                         style_storage().padding.right + indicator_width,
                                     min_width),
                            style_storage().min_height};
    });
}

void Button::apply_property_change(const core::PropertyChange& change) {
    if (!change.changed) {
        return;
    }

    const auto id = change.metadata->id;

    if (id == property_keys::button_type().id()) {
        auto* v = properties().local_value<ButtonType>(property_keys::button_type());
        type_ = v ? *v : ButtonType::Default;
        apply_semantic_style();
        return;
    }
    if (id == property_keys::button_size().id()) {
        auto* v = properties().local_value<ButtonSize>(property_keys::button_size());
        size_ = v ? *v : ButtonSize::Default;
        apply_semantic_style();
        return;
    }
    if (id == property_keys::button_loading().id()) {
        auto* v = properties().local_value<bool>(property_keys::button_loading());
        const auto loading = v ? *v : false;
        if (loading) {
            flags_ |= ButtonFlag::Loading;
            loading_progress_.animate_loop(animation::AnimationDuration{0.9F});
        } else {
            flags_ &= ~ButtonFlag::Loading;
            loading_progress_.set(0.0F);
        }
        invalidate_display_text_cache();
        set_pressed(false);
        release_pointer_capture();
        mark_measure_dirty();
        invalidate_paint();
        return;
    }
    if (id == property_keys::button_plain().id()) {
        auto* v = properties().local_value<bool>(property_keys::button_plain());
        if (v && *v) {
            flags_ |= ButtonFlag::Plain;
        } else {
            flags_ &= ~ButtonFlag::Plain;
        }
        apply_semantic_style();
        return;
    }

    UIElement::apply_property_change(change);
}

void Button::apply_semantic_style() {
    auto next_style = [&]() -> style::UIElementStyle {
        switch (type_) {
        case ButtonType::Primary:
            return style::default_primary_button_style();
        case ButtonType::Success:
            return style::default_success_button_style();
        case ButtonType::Warning:
            return style::default_warning_button_style();
        case ButtonType::Danger:
            return style::default_danger_button_style();
        case ButtonType::Info:
            return style::default_info_button_style();
        case ButtonType::Text:
            return style::default_text_button_style();
        case ButtonType::Default:
            return style::default_button_style();
        }
        return style::default_button_style();
    }();

    apply_type_style(next_style);
    apply_shape_style(next_style);
    apply_variant_style(next_style);
    apply_custom_color_style(next_style);
    apply_dark_mode_style(next_style);

    apply_style_value(std::move(next_style), true);
    update_measure_callback();
}

void Button::apply_type_style(style::UIElementStyle& next_style) const {
    switch (size_) {
    case ButtonSize::Large:
        next_style.min_height = 40.0F;
        next_style.padding = layout::EdgeInsets{19.0F, 11.0F, 19.0F, 11.0F};
        break;
    case ButtonSize::Small:
        next_style.min_height = 24.0F;
        next_style.padding = layout::EdgeInsets{11.0F, 4.0F, 11.0F, 4.0F};
        next_style.font_size = 12.0F;
        break;
    case ButtonSize::Default:
        break;
    }
}

void Button::apply_shape_style(style::UIElementStyle& next_style) const {
    if (has_flag(flags_, ButtonFlag::Plain) && type_ != ButtonType::Default &&
        type_ != ButtonType::Text) {
        const auto colors = button_color_scale_for(type_);
        next_style.background = colors.light9;
        next_style.hover_background = colors.base;
        next_style.active_background = colors.dark2;
        next_style.border_color = colors.light5;
        next_style.semantic.hover_border = colors.base;
        next_style.focus_border_color = colors.base;
        next_style.text_color = colors.base;
    }

    if (has_flag(flags_, ButtonFlag::Round)) {
        next_style.corner_radius = rendering::CornerRadius::uniform(next_style.min_height * 0.5F);
    }
    if (has_flag(flags_, ButtonFlag::Circle)) {
        next_style.corner_radius = rendering::CornerRadius::uniform(next_style.min_height * 0.5F);
        next_style.min_width = next_style.min_height;
        next_style.padding = layout::EdgeInsets{};
    }

    if (has_flag(flags_, ButtonFlag::Dashed)) {
        next_style.border_dash_style = rendering::StrokeDashStyle::Dash;
    } else {
        next_style.border_dash_style = rendering::StrokeDashStyle::Solid;
    }
}

void Button::apply_variant_style(style::UIElementStyle& next_style) const {
    const auto is_text_variant = has_flag(flags_, ButtonFlag::TextVariant);
    const auto is_link_variant = has_flag(flags_, ButtonFlag::LinkVariant);
    const auto is_background_always = has_flag(flags_, ButtonFlag::BackgroundAlways);

    if (is_text_variant) {
        const auto accent = style_storage().text_color;
        auto transparent = rendering::Color::rgba(0, 0, 0, 0);
        next_style.background = transparent;
        next_style.hover_background =
            rendering::Color::rgba(accent.red, accent.green, accent.blue, 24);
        next_style.active_background =
            rendering::Color::rgba(accent.red, accent.green, accent.blue, 40);
        next_style.border_color = transparent;
        next_style.focus_border_color = transparent;
        next_style.border_width = 0.0F;
        if (!is_background_always) {
            next_style.read_only_background = transparent;
        }
    }

    if (is_link_variant) {
        const auto accent = style_storage().text_color;
        auto transparent = rendering::Color::rgba(0, 0, 0, 0);
        next_style.background = transparent;
        next_style.hover_background = transparent;
        next_style.active_background = transparent;
        next_style.border_color = transparent;
        next_style.focus_border_color = transparent;
        next_style.border_width = 0.0F;
        if (!is_background_always) {
            next_style.read_only_background = transparent;
        }
    }
}

void Button::apply_custom_color_style(style::UIElementStyle& next_style) const {
    if (!custom_color_.has_value()) {
        return;
    }

    const auto is_text_variant = has_flag(flags_, ButtonFlag::TextVariant);
    const auto is_link_variant = has_flag(flags_, ButtonFlag::LinkVariant);
    const auto c = custom_color_.value();
    next_style.text_color = c;
    if (is_text_variant || is_link_variant) {
        next_style.hover_background = rendering::Color::rgba(c.red, c.green, c.blue, 24);
        next_style.active_background = rendering::Color::rgba(c.red, c.green, c.blue, 40);
    } else {
        next_style.border_color = c;
        next_style.focus_border_color = c;
        next_style.background = c;
        next_style.hover_background = rendering::Color::rgba(c.red, c.green, c.blue, 160);
        next_style.active_background = rendering::Color::rgba(c.red, c.green, c.blue, 200);
    }
}

void Button::apply_dark_mode_style(style::UIElementStyle& next_style) const {
    if (!has_flag(flags_, ButtonFlag::DarkMode)) {
        return;
    }

    const auto is_text_variant = has_flag(flags_, ButtonFlag::TextVariant);
    const auto is_link_variant = has_flag(flags_, ButtonFlag::LinkVariant);

    next_style.background = rendering::Color::rgba(48, 49, 51);
    if (!(is_text_variant || is_link_variant)) {
        next_style.text_color = rendering::Color::rgba(220, 223, 230);
        next_style.border_color = rendering::Color::rgba(96, 98, 102);
        next_style.focus_border_color = rendering::Color::rgba(64, 158, 255);
    }
    next_style.hover_background = rendering::Color::rgba(64, 66, 68);
    next_style.active_background = rendering::Color::rgba(32, 33, 35);
    next_style.read_only_background = rendering::Color::rgba(56, 57, 59);
}

rendering::TextLayout Button::create_text_layout(layout::Rect content_rect) const {
    return text_engine().layout_text(
        display_text(),
        rendering::TextStyle{.font_size = style_storage().font_size,
                             .color = interactive_text_color(),
                             .alignment = rendering::TextAlignment::Center,
                             .vertical_alignment = rendering::TextVerticalAlignment::Center,
                             .wrapping = rendering::TextWrapping::NoWrap,
                             .trimming = rendering::TextTrimming::CharacterEllipsis},
        rendering::TextLayoutOptions{.max_width = content_rect.width,
                                     .max_height = content_rect.height});
}

rendering::Color Button::interactive_text_color() const noexcept {
    const auto& current_style = style_storage();
    if (disabled_) {
        return current_style.semantic.disabled_text;
    }
    const auto plain_semantic = has_flag(flags_, ButtonFlag::Plain) &&
                                type_ != ButtonType::Default && type_ != ButtonType::Text &&
                                !custom_color_.has_value();
    if (plain_semantic) {
        const auto progress = std::max(animated_hover_progress(), animated_pressed_progress());
        return animation::interpolate_value(current_style.text_color,
                                            rendering::Color::rgba(255, 255, 255), progress);
    }
    const auto default_variant = type_ == ButtonType::Default && !custom_color_.has_value() &&
                                 !has_flag(flags_, ButtonFlag::TextVariant) &&
                                 !has_flag(flags_, ButtonFlag::LinkVariant) &&
                                 !has_flag(flags_, ButtonFlag::DarkMode);
    if (!default_variant) {
        return current_style.text_color;
    }
    const auto progress = std::max(animated_hover_progress(), focused() ? 1.0F : 0.0F);
    return animation::interpolate_value(current_style.text_color, current_style.focus_border_color,
                                        progress);
}

std::string_view Button::display_text() const {
    return text_storage();
}

void Button::invalidate_display_text_cache() const noexcept {
    display_text_cache_valid_ = false;
}

bool Button::icon_visible() const noexcept {
    return loading() || icon_.has_icon();
}

bool Button::menu_indicator_displayed() const noexcept {
    return menu_indicator_visible_ || role_ == ButtonRole::Menu ||
           role_ == ButtonRole::SplitPrimary || role_ == ButtonRole::SplitSecondary || split_;
}

float Button::animated_hover_progress() const {
    return std::clamp(hover_progress_.value(), 0.0F, 1.0F);
}

float Button::animated_pressed_progress() const {
    return std::clamp(pressed_progress_.value(), 0.0F, 1.0F);
}

float Button::animated_click_progress() const {
    return std::clamp(click_progress_.value(), 0.0F, 1.0F);
}

float Button::animated_loading_progress() const {
    return loading_progress_.value();
}

void Button::animate_hover(float target) {
    hover_progress_.animate_to(target);
}

void Button::animate_pressed(float target) {
    pressed_progress_.animate_to(target, animation::AnimationDuration{0.08F});
}

void Button::animate_click() {
    click_progress_.set(1.0F);
    click_progress_.animate_to(0.0F, animation::AnimationDuration{0.22F});
    invalidate_paint();
}

void Button::set_pressed(bool pressed) {
    const auto old_pressed = has_flag(flags_, ButtonFlag::Pressed);
    if (old_pressed == pressed) {
        return;
    }

    if (pressed) {
        flags_ |= ButtonFlag::Pressed;
    } else {
        flags_ &= ~ButtonFlag::Pressed;
    }
    animate_pressed(pressed ? 1.0F : 0.0F);
    invalidate_paint();
}

void Button::click() {
    if (disabled_ || has_flag(flags_, ButtonFlag::Loading)) {
        return;
    }

    animate_click();
    if (click_handler_) {
        click_handler_();
    }
}

} // namespace winelement::controls
