#include <winelement/controls/feedback.hpp>

#include <winelement/elements/all_icons.hpp>
#include <winelement/rendering/render_context.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace winelement::controls {
namespace {

constexpr auto fallback_viewport = layout::Rect{0.0F, 0.0F, 960.0F, 640.0F};
constexpr auto transparent = rendering::Color::rgba(0, 0, 0, 0);
constexpr auto overlay_shadow = rendering::ShadowStyle{
    .color = rendering::Color::rgba(0, 0, 0, 20), .offset = {0.0F, 6.0F}, .blur_radius = 12.0F};
constexpr auto message_shadow = rendering::ShadowStyle{
    .color = rendering::Color::rgba(0, 0, 0, 22), .offset = {0.0F, 0.0F}, .blur_radius = 12.0F};
constexpr auto modal_overlay_shadow = rendering::ShadowStyle{
    .color = rendering::Color::rgba(0, 0, 0, 31), .offset = {0.0F, 0.0F}, .blur_radius = 12.0F};
constexpr auto floating_overlay_shadow = rendering::ShadowStyle{
    .color = rendering::Color::rgba(0, 0, 0, 31), .offset = {0.0F, 0.0F}, .blur_radius = 12.0F};
constexpr auto close_icon_size = 16.0F;
constexpr auto message_status_icon_size = 16.0F;
constexpr auto message_stack_spacing = 16.0F;
constexpr auto modal_backdrop_color = rendering::Color::rgba(0, 0, 0, 80);
constexpr auto message_box_padding = 16.0F;
constexpr auto message_box_header_height = 24.0F;
constexpr auto message_box_footer_gap = 16.0F;
constexpr auto message_box_prompt_gap = 12.0F;
constexpr auto message_box_header_body_gap = 24.0F;
constexpr auto message_box_status_size = 24.0F;
constexpr auto message_box_footer_height = 32.0F;
constexpr auto message_box_drag_height = 40.0F;
constexpr auto dialog_padding = 16.0F;
constexpr auto dialog_header_min_height = 24.0F;
constexpr auto dialog_header_body_gap = 24.0F;
constexpr auto dialog_drag_height = dialog_padding + dialog_header_min_height;
constexpr auto pi = 3.14159265358979323846F;
constexpr auto loading_icon_size = 42.0F;
constexpr auto loading_fullscreen_icon_size = 50.0F;

[[nodiscard]] rendering::Transform2D rotation_transform(float radians) noexcept {
    const auto sine = std::sin(radians);
    const auto cosine = std::cos(radians);
    return rendering::Transform2D{.m11 = cosine, .m12 = sine, .m21 = -sine, .m22 = cosine};
}

struct FeedbackPalette {
    rendering::Color accent{};
    rendering::Color background{};
    rendering::Color border{};
    rendering::Color text{};
};

[[nodiscard]] FeedbackPalette palette_for(MessageType type) noexcept {
    switch (type) {
    case MessageType::Primary:
        return FeedbackPalette{.accent = rendering::Color::rgba(64, 158, 255),
                               .background = rendering::Color::rgba(236, 245, 255),
                               .border = rendering::Color::rgba(179, 216, 255),
                               .text = rendering::Color::rgba(64, 158, 255)};
    case MessageType::Success:
        return FeedbackPalette{.accent = rendering::Color::rgba(103, 194, 58),
                               .background = rendering::Color::rgba(240, 249, 235),
                               .border = rendering::Color::rgba(225, 243, 216),
                               .text = rendering::Color::rgba(82, 155, 46)};
    case MessageType::Warning:
        return FeedbackPalette{.accent = rendering::Color::rgba(230, 162, 60),
                               .background = rendering::Color::rgba(253, 246, 236),
                               .border = rendering::Color::rgba(250, 236, 216),
                               .text = rendering::Color::rgba(179, 119, 27)};
    case MessageType::Error:
        return FeedbackPalette{.accent = rendering::Color::rgba(245, 108, 108),
                               .background = rendering::Color::rgba(254, 240, 240),
                               .border = rendering::Color::rgba(253, 226, 226),
                               .text = rendering::Color::rgba(196, 86, 86)};
    case MessageType::Info:
    default:
        return FeedbackPalette{.accent = rendering::Color::rgba(144, 147, 153),
                               .background = rendering::Color::rgba(244, 244, 245),
                               .border = rendering::Color::rgba(233, 233, 235),
                               .text = rendering::Color::rgba(96, 98, 102)};
    }
}

[[nodiscard]] const elements::icons::IconPathsBase&
status_icon_paths_for(MessageType type) noexcept {
    switch (type) {
    case MessageType::Success:
        return elements::icons::SuccessFilled;
    case MessageType::Warning:
        return elements::icons::WarningFilled;
    case MessageType::Error:
        return elements::icons::CircleCloseFilled;
    case MessageType::Primary:
    case MessageType::Info:
    default:
        return elements::icons::InfoFilled;
    }
}

void configure_status_icon(elements::SvgIcon& icon, MessageType type, rendering::Color color,
                           float size) {
    icon.set_icon_paths(status_icon_paths_for(type));
    icon.set_icon_color(color);
    icon.set_icon_size(size);
}

void configure_icon_layout(elements::SvgIcon& icon, float size) {
    icon.set_hit_test_visible(false);
    icon.configure_layout([size](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(size), layout::Length::points(size))
            .set_flex_shrink(0.0F);
    });
}

void apply_entry_animation(elements::UIElement& element, float progress, float y_offset) noexcept {
    const auto normalized = std::clamp(progress, 0.0F, 1.0F);
    element.set_opacity(normalized);
    element.set_render_transform(
        rendering::Transform2D::translation(0.0F, y_offset * (1.0F - normalized)));
}

[[nodiscard]] layout::Rect viewport_for(const elements::UIElement& host) noexcept {
    const auto frame = host.absolute_frame();
    if (std::isfinite(frame.width) && std::isfinite(frame.height) && frame.width > 0.0F &&
        frame.height > 0.0F) {
        return frame;
    }
    return fallback_viewport;
}

[[nodiscard]] layout::Rect centered_bounds(layout::Rect viewport, layout::Size size) noexcept {
    return layout::Rect{viewport.x + std::max((viewport.width - size.width) * 0.5F, 0.0F),
                        viewport.y + std::max((viewport.height - size.height) * 0.5F, 0.0F),
                        size.width, size.height};
}

[[nodiscard]] float message_box_height_for(const MessageBoxOptions& options, float width,
                                           layout::Rect viewport) noexcept {
    width = std::max(width, 320.0F);
    const auto content_width =
        std::max(120.0F, width - message_box_padding * 2.0F - message_box_status_size - 12.0F);
    const auto estimated_chars_per_line =
        std::max(12.0F, std::floor(content_width / (14.0F * 0.52F)));
    const auto line_count = options.message.empty()
                                ? 1.0F
                                : std::clamp(std::ceil(static_cast<float>(options.message.size()) /
                                                       estimated_chars_per_line),
                                             1.0F, 10.0F);

    auto content_height =
        options.message.empty() ? 0.0F : std::max(message_box_status_size, line_count * 22.0F);
    if (options.content_builder) {
        content_height += 6.0F + 52.0F;
    }

    auto height = message_box_padding * 2.0F + message_box_header_height +
                  message_box_header_body_gap + content_height + message_box_footer_gap +
                  message_box_footer_height;
    if (options.kind == MessageBoxKind::Prompt) {
        height += message_box_prompt_gap + 32.0F + 22.0F;
    }
    const auto minimum_height = options.kind == MessageBoxKind::Prompt ? 194.0F : 128.0F;
    return std::min(std::ceil(std::max(height, minimum_height)),
                    std::max(viewport.height - 48.0F, minimum_height));
}

[[nodiscard]] float dialog_height_for(const DialogOptions& options, float width,
                                      layout::Rect viewport) noexcept {
    if (options.height > 0.0F) {
        return std::min(std::max(options.height, 180.0F),
                        std::max(viewport.height - 48.0F, 180.0F));
    }

    const auto content_width = std::max(120.0F, width - 32.0F);
    const auto estimated_chars_per_line =
        std::max(12.0F, std::floor(content_width / (14.0F * 0.52F)));
    const auto line_count = options.body.empty()
                                ? 1.0F
                                : std::clamp(std::ceil(static_cast<float>(options.body.size()) /
                                                       estimated_chars_per_line),
                                             1.0F, 14.0F);
    const auto body_height = std::max(22.0F, line_count * 22.0F);
    const auto height = dialog_padding * 2.0F + dialog_header_min_height + dialog_header_body_gap +
                        body_height + 16.0F + 32.0F;
    return std::min(std::max(std::ceil(height), 128.0F), std::max(viewport.height - 48.0F, 128.0F));
}

void set_text_alignment(Text& text, rendering::TextAlignment alignment) {
    auto style = text.text_style();
    style.alignment = alignment;
    text.set_text_style(style);
}

void set_fixed_size(elements::UIElement& element, layout::Size size) {
    element.configure_layout([size](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(size.width), layout::Length::points(size.height));
    });
}

void configure_feedback_surface(elements::UIElement& element, layout::Size size) {
    element.configure_layout([size](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(size.width), layout::Length::points(size.height))
            .set_flex_direction(layout::FlexDirection::Column);
    });
}

void configure_surface_layer(elements::UIElement& surface) {
    // Shadows must be allowed to bleed outside the dialog surface like Element Plus overlays.
    surface.set_hit_test_visible(false).set_repaint_boundary(false);
    surface.configure_layout([](layout::LayoutElement& item) {
        item.set_position_type(layout::PositionType::Absolute)
            .set_position(layout::Edge::Left, layout::Length::points(0.0F))
            .set_position(layout::Edge::Top, layout::Length::points(0.0F))
            .set_size(layout::Length::percent(100.0F), layout::Length::percent(100.0F));
    });
}

void apply_surface_style(elements::UIElement& surface, float radius, rendering::Color background,
                         rendering::Color border, bool shadow) {
    surface.set_background(background)
        .set_border(border, 1.0F)
        .set_corner_radius(rendering::CornerRadius::uniform(radius));
    if (shadow) {
        surface.set_shadow(overlay_shadow);
    } else {
        surface.clear_shadow();
    }
}

void configure_close_button(Button& button, float hit_size, rendering::Color normal_color,
                            rendering::Color hover_color) {
    auto button_style = style::default_button_style();
    button_style.background = transparent;
    button_style.hover_background = transparent;
    button_style.active_background = transparent;
    button_style.read_only_background = transparent;
    button_style.border_color = transparent;
    button_style.semantic.hover_border = transparent;
    button_style.border_width = 0.0F;
    button_style.text_color = normal_color;
    button_style.focus_border_color = hover_color;
    button_style.padding = layout::EdgeInsets{};
    button_style.min_width = 0.0F;
    button_style.min_height = 0.0F;
    button_style.font_size = close_icon_size;

    button.set_text("").set_type(ButtonType::Default).set_icon_paths(elements::icons::Close);
    button.set_style(std::move(button_style));
    button.set_semantics_label("Close");
    button.configure_layout([hit_size](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(hit_size), layout::Length::points(hit_size))
            .set_flex_shrink(0.0F);
    });
}

template <typename ControlType>
[[nodiscard]] ControlType* find_top_layer(elements::UIElement& host) {
    for (auto index = host.top_layer_count(); index > 0U; --index) {
        if (auto* control = dynamic_cast<ControlType*>(&host.top_layer_at(index - 1U))) {
            return control;
        }
    }
    return nullptr;
}

template <typename ControlType>
[[nodiscard]] std::size_t top_layer_count_of(elements::UIElement& host) {
    auto count = std::size_t{};
    for (auto index = std::size_t{}; index < host.top_layer_count(); ++index) {
        if (dynamic_cast<ControlType*>(&host.top_layer_at(index)) != nullptr) {
            ++count;
        }
    }
    return count;
}

void configure_footer_button(Button& button, std::string_view text, ButtonType type,
                             float min_width = 64.0F) {
    button.set_text(text).set_type(type);
    button.configure_layout([min_width](layout::LayoutElement& item) {
        item.set_min_width(layout::Length::points(min_width)).set_flex_shrink(0.0F);
    });
}

bool handle_overlay_drag(elements::UIElement& element, bool draggable, bool& dragging,
                         layout::Point& drag_start_pointer, layout::Rect& drag_start_bounds,
                         layout::Point& drag_current_delta, elements::PointerEvent& event,
                         float drag_height) {
    if (!draggable) {
        return false;
    }

    if (event.kind == elements::PointerEventKind::Down &&
        event.button == elements::PointerButton::Primary && event.local_position.y >= 0.0F &&
        event.local_position.y <= drag_height &&
        !(std::isfinite(element.absolute_frame().width) &&
          event.local_position.x >= element.absolute_frame().width - 48.0F)) {
        try {
            drag_start_bounds = element.top_layer_bounds(element);
            element.bring_top_layer_to_front(element);
        } catch (const std::invalid_argument&) {
            return false;
        }
        dragging = true;
        drag_start_pointer = event.position;
        drag_current_delta = {};
        element.set_render_transform(rendering::Transform2D::identity());
        event.handled = true;
        return true;
    }

    if (event.kind == elements::PointerEventKind::Move && dragging) {
        if (!event.primary_button_down) {
            dragging = false;
            element.set_render_transform(rendering::Transform2D::identity());
            element.set_top_layer_bounds(
                element, layout::Rect{drag_start_bounds.x + drag_current_delta.x,
                                      drag_start_bounds.y + drag_current_delta.y,
                                      drag_start_bounds.width, drag_start_bounds.height});
            event.handled = true;
            return true;
        }
        const auto delta = layout::Point{event.position.x - drag_start_pointer.x,
                                         event.position.y - drag_start_pointer.y};
        const auto* host = element.parent();
        const auto viewport = host != nullptr ? viewport_for(*host) : fallback_viewport;
        const auto min_x = viewport.x - drag_start_bounds.x;
        const auto max_x =
            viewport.x + viewport.width - (drag_start_bounds.x + drag_start_bounds.width);
        const auto min_y = viewport.y - drag_start_bounds.y;
        const auto max_y =
            viewport.y + viewport.height - (drag_start_bounds.y + drag_start_bounds.height);
        const auto clamp_axis = [](float value, float minimum, float maximum) noexcept {
            return minimum <= maximum ? std::clamp(value, minimum, maximum) : value;
        };
        drag_current_delta =
            layout::Point{clamp_axis(delta.x, min_x, max_x), clamp_axis(delta.y, min_y, max_y)};
        element.set_render_transform(
            rendering::Transform2D::translation(drag_current_delta.x, drag_current_delta.y));
        event.handled = true;
        return true;
    }

    if (event.kind == elements::PointerEventKind::Up && dragging) {
        dragging = false;
        element.set_render_transform(rendering::Transform2D::identity());
        element.set_top_layer_bounds(
            element, layout::Rect{drag_start_bounds.x + drag_current_delta.x,
                                  drag_start_bounds.y + drag_current_delta.y,
                                  drag_start_bounds.width, drag_start_bounds.height});
        drag_current_delta = {};
        event.handled = true;
        return true;
    }

    if (event.kind == elements::PointerEventKind::Cancel && dragging) {
        dragging = false;
        element.set_render_transform(rendering::Transform2D::identity());
        drag_current_delta = {};
        event.handled = true;
        return true;
    }

    return false;
}

[[nodiscard]] elements::PointerCursor overlay_drag_cursor_for(const elements::UIElement& element,
                                                              bool draggable,
                                                              layout::Point local_position,
                                                              float drag_height) noexcept {
    if (!draggable || local_position.y < 0.0F || local_position.y > drag_height) {
        return elements::PointerCursor::Default;
    }

    const auto width = element.absolute_frame().width;
    if (std::isfinite(width) && local_position.x >= width - 48.0F) {
        return elements::PointerCursor::Default;
    }
    return elements::PointerCursor::Move;
}

} // namespace

Message::Message() : Control() {
    set_theme_class("wm.message");
    auto& surface = append_new_child<elements::UIElement>();
    configure_surface_layer(surface);
    surface_ = &surface;

    configure_layout([](layout::LayoutElement& item) {
        item.set_flex_direction(layout::FlexDirection::Row)
            .set_align_items(layout::Align::Center)
            .set_gap(layout::Gutter::Column, layout::Length::points(8.0F))
            .set_padding(layout::Edge::Top, layout::Length::points(11.0F))
            .set_padding(layout::Edge::Bottom, layout::Length::points(11.0F))
            .set_padding(layout::Edge::Left, layout::Length::points(15.0F))
            .set_padding(layout::Edge::Right, layout::Length::points(15.0F));
    });

    auto& status_icon = append_new_child<elements::SvgIcon>();
    configure_icon_layout(status_icon, message_status_icon_size);
    status_icon_ = &status_icon;

    auto& label = append_new_child<Text>();
    label.set_font_size(14.0F).set_max_lines(2U);
    label.configure_layout(
        [](layout::LayoutElement& item) { item.set_flex_grow(1.0F).set_flex_shrink(1.0F); });
    text_label_ = &label;

    auto& close = append_new_child<Button>();
    configure_close_button(close, 16.0F, rendering::Color::rgba(192, 196, 204),
                           rendering::Color::rgba(144, 147, 153));
    close.set_on_click([this]() { this->close(); });
    close_button_ = &close;
    apply_visual_state();
    restart_open_animation();
}

Message& Message::set_text(std::string_view text) {
    if (text_ == text) {
        return *this;
    }
    text_ = std::string(text);
    if (text_label_ != nullptr) {
        text_label_->set_text(text_);
    }
    set_semantics_label(text_);
    return *this;
}

Message& Message::set_type(MessageType type) {
    if (type_ == type) {
        return *this;
    }
    type_ = type;
    apply_visual_state();
    return *this;
}

Message& Message::set_show_close(bool show_close) {
    if (show_close_ == show_close) {
        return *this;
    }
    show_close_ = show_close;
    apply_visual_state();
    return *this;
}

Message& Message::set_on_close(CloseHandler handler) {
    close_handler_ = std::move(handler);
    return *this;
}

const std::string& Message::text() const noexcept {
    return text_;
}

MessageType Message::type() const noexcept {
    return type_;
}

bool Message::show_close() const noexcept {
    return show_close_;
}

Message& Message::show(elements::UIElement& host, MessageOptions options) {
    const auto viewport = viewport_for(host);
    const auto width =
        std::min(std::max(options.width, 240.0F), std::max(viewport.width - 32.0F, 240.0F));
    const auto size = layout::Size{width, 40.0F};
    const auto bounds = layout::Rect{viewport.x + std::max((viewport.width - width) * 0.5F, 0.0F),
                                     viewport.y + std::max(options.top, 0.0F), width, size.height};

    auto message = std::make_unique<Message>();
    message->set_text(options.text)
        .set_type(options.type)
        .set_show_close(options.show_close)
        .set_on_close(std::move(options.on_close));
    message->width_ = width;
    message->height_ = size.height;
    message->top_offset_ = std::max(options.top, 0.0F);
    message->stack_top_.set(message->top_offset_);
    message->set_duration(options.duration_ms);
    set_fixed_size(*message, size);
    auto& message_ref = *message;
    auto top_layer_options = elements::TopLayerOptions{};
    top_layer_options.bounds = bounds;
    top_layer_options.light_dismiss = false;
    top_layer_options.close_on_escape = false;
    top_layer_options.preserve_focus = true;
    host.push_top_layer(std::move(message), std::move(top_layer_options));
    relayout_host_messages(host, false);
    return message_ref;
}

void Message::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    static_cast<void>(context);
    static_cast<void>(absolute_frame);
}

void Message::on_paint_overlay(rendering::RenderContext& context,
                               layout::Rect absolute_frame) const {
    static_cast<void>(context);
    static_cast<void>(absolute_frame);
}

bool Message::on_animation_frame(animation::AnimationTimePoint now) {
    const auto entry_active = open_progress_.tick(now);
    const auto stack_active = stack_top_.tick(now);
    if (entry_active) {
        apply_open_animation();
    }
    if (stack_active) {
        sync_stack_bounds();
    }
    if (!closing_ && duration_ms_ > 0 &&
        now - opened_at_ >= std::chrono::milliseconds{duration_ms_}) {
        begin_close();
    }
    if (closing_ && !open_progress_.running() && open_progress_.value() <= 0.001F) {
        if (auto* host = parent()) {
            relayout_host_messages(*host, true);
        }
        const auto handler = close_handler_;
        if (handler) {
            handler();
        }
        dismiss_own_top_layer();
        return false;
    }
    return entry_active || stack_active || duration_ms_ > 0 || closing_;
}

void Message::apply_visual_state() {
    const auto palette = palette_for(type_);
    if (surface_ != nullptr) {
        apply_surface_style(*surface_, 4.0F, palette.background, palette.border, true);
        surface_->set_shadow(message_shadow);
    }
    if (text_label_ != nullptr) {
        text_label_->set_color(palette.text);
    }
    if (status_icon_ != nullptr) {
        configure_status_icon(*status_icon_, type_, palette.accent, message_status_icon_size);
    }
    if (close_button_ != nullptr) {
        close_button_->set_visible(show_close_);
    }
    invalidate_paint();
}

void Message::restart_open_animation() noexcept {
    opened_at_ = animation::AnimationClockType::now();
    closing_ = false;
    open_progress_.set(0.0F);
    apply_open_animation();
    open_progress_.animate_to(1.0F, animation::AnimationDuration{0.3F},
                              animation::EasingFunction::ease_out_cubic());
}

void Message::apply_open_animation() noexcept {
    apply_entry_animation(*this, open_progress_.value(), -std::max(height_, 40.0F));
}

void Message::set_stack_top(float top, bool animate) noexcept {
    if (animate) {
        stack_top_.animate_to(top, animation::AnimationDuration{0.3F},
                              animation::EasingFunction::ease_in_out_cubic());
    } else {
        stack_top_.set(top);
    }
    sync_stack_bounds();
}

void Message::sync_stack_bounds() noexcept {
    auto* host = parent();
    if (host == nullptr) {
        return;
    }

    const auto viewport = viewport_for(*host);
    host->set_top_layer_bounds(
        *this, layout::Rect{viewport.x + std::max((viewport.width - width_) * 0.5F, 0.0F),
                            viewport.y + stack_top_.value(), width_, height_});
}

void Message::begin_close() noexcept {
    if (closing_) {
        return;
    }
    closing_ = true;
    if (auto* host = parent()) {
        relayout_host_messages(*host, true);
    }
    open_progress_.animate_to(0.0F, animation::AnimationDuration{0.25F},
                              animation::EasingFunction::ease_in_out_cubic());
}

void Message::relayout_host_messages(elements::UIElement& host, bool animate) {
    auto next_top = 0.0F;
    auto has_visible_message = false;
    auto previous_height = 0.0F;
    for (auto index = std::size_t{}; index < host.top_layer_count(); ++index) {
        auto* message = dynamic_cast<Message*>(&host.top_layer_at(index));
        if (message == nullptr || message->closing_) {
            continue;
        }
        if (!has_visible_message) {
            next_top = std::max(message->top_offset_, 0.0F);
            has_visible_message = true;
        } else {
            next_top += previous_height + message_stack_spacing;
        }
        previous_height = message->height_;
        message->set_stack_top(next_top, animate);
    }
}

void Message::set_duration(int duration_ms) noexcept {
    duration_ms_ = std::max(duration_ms, 0);
}

void Message::close() {
    begin_close();
}

MessageBox::MessageBox() : Control() {
    set_theme_class("wm.message_box");
    auto& surface = append_new_child<elements::UIElement>();
    configure_surface_layer(surface);
    surface_ = &surface;

    configure_layout([](layout::LayoutElement& item) {
        item.set_flex_direction(layout::FlexDirection::Column)
            .set_padding(layout::Edge::All, layout::Length::points(message_box_padding))
            .set_gap(layout::Gutter::Row, layout::Length::points(0.0F));
    });

    auto& header = append_new_child<StackPanel>();
    header.set_orientation(Orientation::Horizontal)
        .set_align_items(layout::Align::Center)
        .set_justify_content(layout::JustifyContent::FlexStart)
        .set_gap(6.0F);
    header.set_hit_test_visible(false);
    header.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_margin(layout::Edge::Right, layout::Length::points(32.0F))
            .set_padding(layout::Edge::Bottom, layout::Length::points(message_box_header_body_gap))
            .set_flex_shrink(0.0F);
    });
    header_panel_ = &header;

    auto& title_icon = header.append_new_child<elements::SvgIcon>();
    configure_icon_layout(title_icon, 18.0F);
    title_icon.set_visible(false);
    title_status_icon_ = &title_icon;

    auto& title = header.append_new_child<Text>();
    title.set_font_size(18.0F).set_color(rendering::Color::rgba(48, 49, 51));
    title.set_hit_test_visible(false);
    title.configure_layout(
        [](layout::LayoutElement& item) { item.set_flex_grow(1.0F).set_flex_shrink(1.0F); });
    title_label_ = &title;

    auto& body = append_new_child<StackPanel>();
    body.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F)).set_flex_grow(1.0F).set_flex_shrink(1.0F);
    });

    auto& content = body.append_new_child<StackPanel>();
    content.set_orientation(Orientation::Horizontal)
        .set_align_items(layout::Align::FlexStart)
        .set_gap(12.0F);
    content.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(1.0F);
    });
    content_panel_ = &content;

    auto& status_icon = content.append_new_child<elements::SvgIcon>();
    configure_icon_layout(status_icon, message_box_status_size);
    status_icon.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(message_box_status_size),
                      layout::Length::points(message_box_status_size))
            .set_flex_shrink(0.0F);
    });
    status_icon_ = &status_icon;

    auto& text_stack = content.append_new_child<StackPanel>();
    text_stack.configure_layout([](layout::LayoutElement& item) {
        item.set_flex_grow(1.0F).set_flex_shrink(1.0F).set_gap(layout::Gutter::Row,
                                                               layout::Length::points(8.0F));
    });

    auto& message = text_stack.append_new_child<Text>();
    message.set_font_size(14.0F).set_color(rendering::Color::rgba(96, 98, 102)).set_max_lines(10U);
    message.configure_layout([](layout::LayoutElement& item) { item.set_flex_shrink(1.0F); });
    message_label_ = &message;

    auto& custom_content = text_stack.append_new_child<StackPanel>();
    custom_content.set_visible(false);
    custom_content.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_gap(layout::Gutter::Row, layout::Length::points(6.0F))
            .set_flex_shrink(0.0F);
    });
    custom_content_panel_ = &custom_content;

    auto& input = body.append_new_child<Input>();
    input.set_placeholder("Please input");
    input.set_on_input([this](std::string_view) { clear_prompt_error(); });
    input.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_margin(layout::Edge::Top, layout::Length::points(message_box_prompt_gap))
            .set_flex_shrink(0.0F);
    });
    input_ = &input;

    auto& input_error = body.append_new_child<Text>();
    input_error.set_font_size(12.0F).set_color(rendering::Color::rgba(245, 108, 108));
    input_error.set_text(input_error_message_);
    input_error.set_visible(false);
    input_error.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_margin(layout::Edge::Top, layout::Length::points(4.0F))
            .set_flex_shrink(0.0F);
    });
    input_error_label_ = &input_error;

    auto& footer = append_new_child<StackPanel>();
    footer.set_orientation(Orientation::Horizontal)
        .set_wrap(layout::Wrap::Wrap)
        .set_gap(10.0F)
        .set_justify_content(layout::JustifyContent::FlexEnd)
        .set_align_items(layout::Align::Center);
    footer.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_margin(layout::Edge::Top, layout::Length::points(message_box_footer_gap))
            .set_flex_shrink(0.0F);
    });
    footer_panel_ = &footer;

    auto& cancel = footer.append_new_child<Button>();
    configure_footer_button(cancel, "Cancel", ButtonType::Default);
    cancel.set_on_click([this]() { close_with_action(MessageBoxAction::Cancel); });
    cancel_button_ = &cancel;

    auto& confirm = footer.append_new_child<Button>();
    configure_footer_button(confirm, "OK", ButtonType::Primary);
    confirm.set_on_click([this]() { close_with_action(MessageBoxAction::Confirm); });
    confirm_button_ = &confirm;

    auto& close = append_new_child<Button>();
    configure_close_button(close, 40.0F, rendering::Color::rgba(144, 147, 153),
                           rendering::Color::rgba(64, 158, 255));
    close.configure_layout([](layout::LayoutElement& item) {
        item.set_position_type(layout::PositionType::Absolute)
            .set_position(layout::Edge::Right, layout::Length::points(0.0F))
            .set_position(layout::Edge::Top, layout::Length::points(0.0F))
            .set_size(layout::Length::points(40.0F), layout::Length::points(40.0F));
    });
    close.set_on_click([this]() { close_with_action(MessageBoxAction::Close); });
    close_button_ = &close;

    set_title("Message");
    apply_visual_state();
    restart_open_animation();
}

MessageBox& MessageBox::set_title(std::string_view title) {
    if (title_ == title) {
        return *this;
    }
    title_ = std::string(title);
    if (title_label_ != nullptr) {
        title_label_->set_text(title_);
    }
    set_semantics_label(title_);
    return *this;
}

MessageBox& MessageBox::set_message(std::string_view message) {
    if (message_ == message) {
        return *this;
    }
    message_ = std::string(message);
    if (message_label_ != nullptr) {
        message_label_->set_text(message_);
    }
    apply_visual_state();
    return *this;
}

MessageBox& MessageBox::set_kind(MessageBoxKind kind) {
    if (kind_ == kind) {
        return *this;
    }
    kind_ = kind;
    if (kind_ != MessageBoxKind::Prompt) {
        clear_prompt_error();
    }
    apply_visual_state();
    return *this;
}

MessageBox& MessageBox::set_type(MessageType type) {
    if (type_ == type) {
        return *this;
    }
    type_ = type;
    apply_visual_state();
    return *this;
}

MessageBox& MessageBox::set_confirm_button_text(std::string_view text) {
    if (confirm_button_ != nullptr && confirm_button_->text() != text) {
        confirm_button_->set_text(text);
    }
    return *this;
}

MessageBox& MessageBox::set_cancel_button_text(std::string_view text) {
    if (cancel_button_ != nullptr && cancel_button_->text() != text) {
        cancel_button_->set_text(text);
    }
    return *this;
}

MessageBox& MessageBox::set_input_placeholder(std::string_view text) {
    if (input_ != nullptr && input_->placeholder() != text) {
        input_->set_placeholder(text);
    }
    return *this;
}

MessageBox& MessageBox::set_input_text(std::string_view text) {
    if (input_ != nullptr && input_->text() != text) {
        input_->set_text(text);
    }
    clear_prompt_error();
    return *this;
}

MessageBox& MessageBox::set_show_close(bool show_close) {
    if (show_close_ == show_close) {
        return *this;
    }
    show_close_ = show_close;
    apply_visual_state();
    return *this;
}

MessageBox& MessageBox::set_show_cancel_button(bool show_cancel_button) {
    if (show_cancel_button_ == show_cancel_button) {
        return *this;
    }
    show_cancel_button_ = show_cancel_button;
    apply_visual_state();
    return *this;
}

MessageBox& MessageBox::set_confirm_loading(bool loading) {
    if (confirm_loading_ == loading) {
        return *this;
    }
    confirm_loading_ = loading;
    apply_visual_state();
    return *this;
}

MessageBox& MessageBox::set_center(bool center) noexcept {
    if (center_ == center) {
        return *this;
    }
    center_ = center;
    apply_visual_state();
    return *this;
}

MessageBox& MessageBox::set_distinguish_cancel_and_close(bool distinguish) noexcept {
    distinguish_cancel_and_close_ = distinguish;
    return *this;
}

MessageBox& MessageBox::set_close_on_confirm(bool close_on_confirm) noexcept {
    close_on_confirm_ = close_on_confirm;
    return *this;
}

MessageBox& MessageBox::set_content_builder(MessageBoxContentBuilder builder) {
    content_builder_ = std::move(builder);
    if (custom_content_panel_ != nullptr) {
        custom_content_panel_->clear_children();
        custom_content_panel_->set_visible(static_cast<bool>(content_builder_));
        if (content_builder_) {
            content_builder_(*custom_content_panel_);
        }
    }
    apply_visual_state();
    return *this;
}

MessageBox& MessageBox::set_input_error_message(std::string_view text) {
    if (input_error_message_ == text) {
        return *this;
    }
    input_error_message_ = std::string(text);
    sync_prompt_error_label();
    return *this;
}

MessageBox& MessageBox::set_input_validator(MessageBoxInputValidator validator) {
    input_validator_ = std::move(validator);
    clear_prompt_error();
    return *this;
}

MessageBox& MessageBox::set_draggable(bool draggable) noexcept {
    draggable_ = draggable;
    return *this;
}

MessageBox& MessageBox::set_on_action(ActionHandler handler) {
    action_handler_ = std::move(handler);
    return *this;
}

const std::string& MessageBox::title() const noexcept {
    return title_;
}

const std::string& MessageBox::message() const noexcept {
    return message_;
}

MessageBoxKind MessageBox::kind() const noexcept {
    return kind_;
}

MessageType MessageBox::type() const noexcept {
    return type_;
}

bool MessageBox::show_cancel_button() const noexcept {
    return show_cancel_button_;
}

bool MessageBox::center() const noexcept {
    return center_;
}

bool MessageBox::distinguish_cancel_and_close() const noexcept {
    return distinguish_cancel_and_close_;
}

bool MessageBox::draggable() const noexcept {
    return draggable_;
}

std::string MessageBox::input_text() const {
    return input_ != nullptr ? input_->text() : std::string{};
}

MessageBox& MessageBox::show(elements::UIElement& host, MessageBoxOptions options) {
    const auto viewport = viewport_for(host);
    const auto width =
        std::min(std::max(options.width, 320.0F), std::max(viewport.width - 48.0F, 320.0F));
    const auto height = message_box_height_for(options, width, viewport);
    auto box = std::make_unique<MessageBox>();
    box->modal_ = options.modal;
    box->set_title(options.title)
        .set_message(options.message)
        .set_kind(options.kind)
        .set_type(options.type)
        .set_confirm_button_text(options.confirm_button_text)
        .set_cancel_button_text(options.cancel_button_text)
        .set_input_placeholder(options.input_placeholder)
        .set_input_text(options.input_text)
        .set_show_close(options.show_close)
        .set_show_cancel_button(options.show_cancel_button)
        .set_confirm_loading(options.confirm_loading)
        .set_center(options.center)
        .set_distinguish_cancel_and_close(options.distinguish_cancel_and_close)
        .set_close_on_confirm(options.close_on_confirm)
        .set_content_builder(std::move(options.content_builder))
        .set_input_error_message(options.input_error_message)
        .set_input_validator(std::move(options.input_validator))
        .set_draggable(options.draggable)
        .set_on_action(std::move(options.on_action));
    const auto size = layout::Size{width, height};
    configure_feedback_surface(*box, size);
    auto& box_ref = *box;
    auto top_layer_options = elements::TopLayerOptions{};
    top_layer_options.bounds = centered_bounds(viewport, size);
    top_layer_options.light_dismiss = options.close_on_click_modal;
    top_layer_options.close_on_escape = options.close_on_press_escape;
    top_layer_options.modal = options.modal;
    top_layer_options.backdrop_color = options.modal ? modal_backdrop_color : transparent;
    host.push_top_layer(std::move(box), std::move(top_layer_options));
    return box_ref;
}

void MessageBox::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    static_cast<void>(context);
    static_cast<void>(absolute_frame);
}

void MessageBox::on_paint_overlay(rendering::RenderContext& context,
                                  layout::Rect absolute_frame) const {
    static_cast<void>(context);
    static_cast<void>(absolute_frame);
}

bool MessageBox::on_animation_frame(animation::AnimationTimePoint now) {
    const auto active = open_progress_.tick(now);
    if (active) {
        apply_open_animation();
    }
    return active;
}

void MessageBox::on_pointer_event(elements::PointerEvent& event) {
    const auto was_dragging = dragging_;
    if (handle_overlay_drag(*this, draggable_, dragging_, drag_start_pointer_, drag_start_bounds_,
                            drag_current_delta_, event, message_box_drag_height)) {
        if (event.kind == elements::PointerEventKind::Down && dragging_) {
            open_progress_.set(1.0F);
            static_cast<void>(capture_pointer());
        } else if (was_dragging && !dragging_) {
            release_pointer_capture();
        }
        return;
    }
    if (was_dragging && !dragging_) {
        release_pointer_capture();
    }
    elements::UIElement::on_pointer_event(event);
}

elements::PointerCursor
MessageBox::cursor_for_local_point(layout::Point local_position) const noexcept {
    return overlay_drag_cursor_for(*this, draggable_, local_position, message_box_drag_height);
}

void MessageBox::apply_visual_state() {
    const auto palette = palette_for(type_);
    const auto has_message = !message_.empty();
    if (surface_ != nullptr) {
        const auto border =
            modal_ ? rendering::Color::rgba(235, 238, 245) : rendering::Color::rgba(220, 223, 230);
        apply_surface_style(*surface_, 4.0F, rendering::Color::rgba(255, 255, 255), border, true);
        surface_->set_shadow(modal_ ? modal_overlay_shadow : floating_overlay_shadow);
    }
    if (header_panel_ != nullptr) {
        header_panel_->set_justify_content(center_ ? layout::JustifyContent::Center
                                                   : layout::JustifyContent::FlexStart);
    }
    if (title_status_icon_ != nullptr) {
        configure_status_icon(*title_status_icon_, type_, palette.accent, 18.0F);
        title_status_icon_->set_visible(center_);
    }
    if (message_label_ != nullptr) {
        message_label_->set_color(rendering::Color::rgba(96, 98, 102));
        set_text_alignment(*message_label_, center_ ? rendering::TextAlignment::Center
                                                    : rendering::TextAlignment::Start);
    }
    if (title_label_ != nullptr) {
        set_text_alignment(*title_label_, center_ ? rendering::TextAlignment::Center
                                                  : rendering::TextAlignment::Start);
        title_label_->configure_layout([this](layout::LayoutElement& item) {
            item.set_flex_grow(center_ ? 0.0F : 1.0F).set_flex_shrink(1.0F);
        });
    }
    if (content_panel_ != nullptr) {
        content_panel_->set_justify_content(center_ ? layout::JustifyContent::Center
                                                    : layout::JustifyContent::FlexStart);
    }
    if (status_icon_ != nullptr) {
        configure_status_icon(*status_icon_, type_, palette.accent, message_box_status_size);
        status_icon_->set_visible(kind_ != MessageBoxKind::Alert && !center_ && has_message);
    }
    if (input_ != nullptr) {
        input_->set_visible(kind_ == MessageBoxKind::Prompt);
    }
    if (input_error_label_ != nullptr) {
        sync_prompt_error_label();
    }
    if (cancel_button_ != nullptr) {
        cancel_button_->set_visible(show_cancel_button_ && kind_ != MessageBoxKind::Alert);
    }
    if (confirm_button_ != nullptr) {
        if (confirm_button_->type() != ButtonType::Primary) {
            confirm_button_->set_type(ButtonType::Primary);
        }
        confirm_button_->set_loading(confirm_loading_);
    }
    if (footer_panel_ != nullptr) {
        footer_panel_->set_justify_content(center_ ? layout::JustifyContent::Center
                                                   : layout::JustifyContent::FlexEnd);
    }
    if (custom_content_panel_ != nullptr) {
        custom_content_panel_->set_align_items(center_ ? layout::Align::Center
                                                       : layout::Align::FlexStart);
    }
    if (close_button_ != nullptr) {
        close_button_->set_visible(show_close_);
    }
    invalidate_paint();
}

void MessageBox::restart_open_animation() noexcept {
    open_progress_.set(0.82F);
    apply_open_animation();
    open_progress_.animate_to(1.0F, animation::AnimationDuration{0.18F});
}

void MessageBox::apply_open_animation() noexcept {
    apply_entry_animation(*this, open_progress_.value(), -12.0F);
}

void MessageBox::clear_prompt_error() {
    input_error_visible_ = false;
    if (input_ != nullptr) {
        input_->set_status(InputStatus::Default);
    }
    sync_prompt_error_label();
}

void MessageBox::show_prompt_error(std::string_view message) {
    input_error_visible_ = true;
    if (!message.empty()) {
        input_error_message_ = std::string(message);
    }
    if (input_ != nullptr) {
        input_->set_status(InputStatus::Error);
    }
    if (input_error_label_ != nullptr) {
        if (!message.empty()) {
            input_error_label_->set_text(std::string(message));
        } else {
            input_error_label_->set_text(input_error_message_);
        }
        input_error_label_->set_color(rendering::Color::rgba(245, 108, 108));
        input_error_label_->set_visible(kind_ == MessageBoxKind::Prompt);
    }
}

void MessageBox::sync_prompt_error_label() {
    if (input_error_label_ == nullptr) {
        return;
    }
    const auto prompt = kind_ == MessageBoxKind::Prompt;
    input_error_label_->set_visible(prompt);
    if (!prompt) {
        return;
    }
    input_error_label_->set_text(input_error_message_.empty() ? " " : input_error_message_);
    input_error_label_->set_color(input_error_visible_ ? rendering::Color::rgba(245, 108, 108)
                                                       : rendering::Color::rgba(245, 108, 108, 0));
}

void MessageBox::close_with_action(MessageBoxAction action) {
    auto handler = action_handler_;
    auto value = input_text();
    if (action == MessageBoxAction::Confirm && kind_ == MessageBoxKind::Prompt &&
        input_validator_) {
        if (auto error = input_validator_(value)) {
            show_prompt_error(error->empty() ? input_error_message_ : *error);
            return;
        }
        clear_prompt_error();
    }
    const auto reported_action = !distinguish_cancel_and_close_ && action == MessageBoxAction::Close
                                     ? MessageBoxAction::Cancel
                                     : action;
    if (action == MessageBoxAction::Confirm && !close_on_confirm_) {
        if (handler) {
            handler(reported_action, std::move(value));
        }
        return;
    }
    dismiss_own_top_layer();
    if (handler) {
        handler(reported_action, std::move(value));
    }
}

class Loading::Spinner final : public Control {
  public:
    Spinner() {
        set_theme_class("wm.loading.spinner");
        set_hit_test_visible(false).set_repaint_boundary(true);
        icon_.set_icon_paths(elements::icons::Loading);
        configure_layout([](layout::LayoutElement& item) {
            item.set_size(layout::Length::points(loading_icon_size),
                          layout::Length::points(loading_icon_size))
                .set_flex_shrink(0.0F);
        });
    }

    Spinner& set_spinner_color(rendering::Color color) noexcept {
        if (spinner_color_ == color) {
            return *this;
        }
        spinner_color_ = color;
        invalidate_paint();
        return *this;
    }

    Spinner& set_rotation_degrees(float rotation_degrees) noexcept {
        if (rotation_degrees_ == rotation_degrees) {
            return *this;
        }
        rotation_degrees_ = rotation_degrees;
        invalidate_paint();
        return *this;
    }

  protected:
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override {
        const auto center = layout::Point{absolute_frame.x + absolute_frame.width * 0.5F,
                                          absolute_frame.y + absolute_frame.height * 0.5F};
        const auto size = std::max(0.0F, std::min(absolute_frame.width, absolute_frame.height));
        const auto icon_rect =
            layout::Rect{center.x - size * 0.5F, center.y - size * 0.5F, size, size};
        context.push_layer(rendering::RenderLayerOptions{
            .bounds = absolute_frame,
            .transform = rendering::transform_around_point(
                rotation_transform(rotation_degrees_ * pi / 180.0F), center),
            .clips_to_bounds = false});
        icon_.paint_icon(context, icon_rect, spinner_color_);
        context.pop_layer();
    }

  private:
    elements::SvgIcon icon_;
    rendering::Color spinner_color_ = rendering::Color::rgba(64, 158, 255);
    float rotation_degrees_ = 0.0F;
};

Loading::Loading() : Control() {
    set_theme_class("wm.loading");
    configure_layout([](layout::LayoutElement& item) {
        item.set_flex_direction(layout::FlexDirection::Column)
            .set_align_items(layout::Align::Center)
            .set_justify_content(layout::JustifyContent::Center)
            .set_gap(layout::Gutter::Row, layout::Length::points(8.0F));
    });

    auto& surface = append_new_child<elements::UIElement>();
    configure_surface_layer(surface);
    surface_ = &surface;

    auto& spinner = append_new_child<Spinner>();
    spinner_ = &spinner;

    auto& label = append_new_child<Text>();
    label.set_font_size(14.0F).set_color(spinner_color_);
    label.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(0.0F);
    });
    auto label_style = label.text_style();
    label_style.alignment = rendering::TextAlignment::Center;
    label_style.vertical_alignment = rendering::TextVerticalAlignment::Center;
    label.set_text_style(label_style);
    text_label_ = &label;

    set_text("Loading");
    set_spinner_color(spinner_color_);
    set_background(background());
    set_show_close(show_close_);
    restart_open_animation();
}

Loading& Loading::set_text(std::string_view text) {
    if (text_ == text) {
        return *this;
    }
    text_ = std::string(text);
    if (text_label_ != nullptr) {
        text_label_->set_text(text_).set_visible(active_ && !text_.empty());
    }
    return *this;
}

Loading& Loading::set_active(bool active) {
    if (active_ == active) {
        return *this;
    }
    active_ = active;
    if (surface_ != nullptr) {
        surface_->set_visible(active_);
    }
    if (spinner_ != nullptr) {
        spinner_->set_visible(active_);
    }
    if (text_label_ != nullptr) {
        text_label_->set_visible(active_ && !text_.empty());
    }
    if (!active_) {
        dismiss_own_top_layer();
    }
    return *this;
}

Loading& Loading::set_background(rendering::Color color) noexcept {
    if (background() != color) {
        elements::UIElement::set_background(color);
    }
    if (surface_ != nullptr) {
        apply_surface_style(*surface_, 0.0F, color, transparent, false);
    }
    return *this;
}

Loading& Loading::set_spinner_color(rendering::Color color) noexcept {
    if (spinner_color_ == color) {
        return *this;
    }
    spinner_color_ = color;
    if (spinner_ != nullptr) {
        spinner_->set_spinner_color(color);
    }
    if (text_label_ != nullptr) {
        text_label_->set_color(color);
    }
    return *this;
}

Loading& Loading::set_show_close(bool show_close) {
    static_cast<void>(show_close);
    show_close_ = false;
    return *this;
}

const std::string& Loading::text() const noexcept {
    return text_;
}

bool Loading::active() const noexcept {
    return active_;
}

rendering::Color Loading::spinner_color() const noexcept {
    return spinner_color_;
}

bool Loading::show_close() const noexcept {
    return show_close_;
}

Loading& Loading::show(elements::UIElement& host, LoadingOptions options) {
    const auto viewport = viewport_for(host);
    const auto size = layout::Size{viewport.width, viewport.height};
    const auto bounds = layout::Rect{std::numeric_limits<float>::quiet_NaN(),
                                     std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const auto show_close = false;
    if (auto* existing = find_top_layer<Loading>(host)) {
        existing->set_text(options.text)
            .set_background(options.background)
            .set_spinner_color(options.spinner_color)
            .set_show_close(show_close)
            .set_active(true);
        configure_feedback_surface(*existing, size);
        if (existing->spinner_ != nullptr) {
            set_fixed_size(*existing->spinner_,
                           options.fullscreen ? layout::Size{loading_fullscreen_icon_size,
                                                             loading_fullscreen_icon_size}
                                              : layout::Size{loading_icon_size, loading_icon_size});
        }
        host.set_top_layer_bounds(*existing, bounds).bring_top_layer_to_front(*existing);
        existing->restart_open_animation();
        return *existing;
    }

    auto loading = std::make_unique<Loading>();
    loading->set_text(options.text)
        .set_background(options.background)
        .set_spinner_color(options.spinner_color)
        .set_show_close(show_close)
        .set_active(true);
    configure_feedback_surface(*loading, size);
    if (loading->spinner_ != nullptr) {
        set_fixed_size(*loading->spinner_,
                       options.fullscreen ? layout::Size{loading_fullscreen_icon_size,
                                                         loading_fullscreen_icon_size}
                                          : layout::Size{loading_icon_size, loading_icon_size});
    }
    auto& loading_ref = *loading;
    auto top_layer_options = elements::TopLayerOptions{};
    top_layer_options.bounds = bounds;
    top_layer_options.light_dismiss = false;
    top_layer_options.close_on_escape = false;
    top_layer_options.modal = options.fullscreen;
    top_layer_options.preserve_focus = true;
    host.push_top_layer(std::move(loading), std::move(top_layer_options));
    return loading_ref;
}

bool Loading::on_animation_frame(animation::AnimationTimePoint now) {
    if (!active_) {
        return false;
    }
    const auto active = open_progress_.tick(now);
    if (active) {
        apply_open_animation();
    }
    const auto seconds = std::chrono::duration<float>(now.time_since_epoch()).count();
    rotation_degrees_ = std::fmod(seconds * 260.0F, 360.0F);
    if (spinner_ != nullptr) {
        spinner_->set_rotation_degrees(rotation_degrees_);
    }
    return true;
}

void Loading::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    static_cast<void>(context);
    static_cast<void>(absolute_frame);
}

void Loading::restart_open_animation() noexcept {
    open_progress_.set(0.86F);
    apply_open_animation();
    open_progress_.animate_to(1.0F, animation::AnimationDuration{0.18F});
}

void Loading::apply_open_animation() noexcept {
    apply_entry_animation(*this, open_progress_.value(), -8.0F);
}

Dialog::Dialog() : Control() {
    set_theme_class("wm.dialog");
    auto& surface = append_new_child<elements::UIElement>();
    configure_surface_layer(surface);
    apply_surface_style(surface, 4.0F, rendering::Color::rgba(255, 255, 255),
                        rendering::Color::rgba(235, 238, 245), true);
    surface_ = &surface;

    configure_layout([](layout::LayoutElement& item) {
        item.set_flex_direction(layout::FlexDirection::Column)
            .set_padding(layout::Edge::All, layout::Length::points(dialog_padding));
    });

    auto& header = append_new_child<StackPanel>();
    header.set_orientation(Orientation::Horizontal)
        .set_align_items(layout::Align::Center)
        .set_justify_content(layout::JustifyContent::FlexStart);
    header.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_min_height(layout::Length::points(dialog_header_min_height))
            .set_padding(layout::Edge::Bottom, layout::Length::points(dialog_header_body_gap))
            .set_margin(layout::Edge::Right, layout::Length::points(32.0F))
            .set_flex_shrink(0.0F);
    });
    auto& title = header.append_new_child<Text>();
    title.set_font_size(18.0F).set_color(rendering::Color::rgba(48, 49, 51));
    title.configure_layout(
        [](layout::LayoutElement& item) { item.set_flex_grow(1.0F).set_flex_shrink(1.0F); });
    title_label_ = &title;

    auto& body = append_new_child<Text>();
    body.set_font_size(14.0F).set_color(rendering::Color::rgba(96, 98, 102)).set_max_lines(14U);
    body.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F)).set_flex_grow(1.0F).set_flex_shrink(1.0F);
    });
    body_label_ = &body;

    auto& footer = append_new_child<StackPanel>();
    footer.set_orientation(Orientation::Horizontal)
        .set_wrap(layout::Wrap::Wrap)
        .set_gap(10.0F)
        .set_justify_content(layout::JustifyContent::FlexEnd)
        .set_align_items(layout::Align::Center);
    footer.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_margin(layout::Edge::Top, layout::Length::points(16.0F))
            .set_flex_shrink(0.0F);
    });
    auto& cancel = footer.append_new_child<Button>();
    configure_footer_button(cancel, "Cancel", ButtonType::Default);
    cancel.set_on_click([this]() { close_with_action(DialogAction::Cancel); });
    cancel_button_ = &cancel;
    auto& confirm = footer.append_new_child<Button>();
    configure_footer_button(confirm, "Confirm", ButtonType::Primary);
    confirm.set_on_click([this]() { close_with_action(DialogAction::Confirm); });
    confirm_button_ = &confirm;

    auto& close = append_new_child<Button>();
    configure_close_button(close, 48.0F, rendering::Color::rgba(144, 147, 153),
                           rendering::Color::rgba(64, 158, 255));
    close.configure_layout([](layout::LayoutElement& item) {
        item.set_position_type(layout::PositionType::Absolute)
            .set_position(layout::Edge::Right, layout::Length::points(0.0F))
            .set_position(layout::Edge::Top, layout::Length::points(0.0F))
            .set_size(layout::Length::points(48.0F), layout::Length::points(48.0F));
    });
    close.set_on_click([this]() { close_with_action(DialogAction::Close); });
    close_button_ = &close;
    set_title("Dialog");
    apply_visual_state();
    restart_open_animation();
}

Dialog& Dialog::set_title(std::string_view title) {
    if (title_ == title) {
        return *this;
    }
    title_ = std::string(title);
    if (title_label_ != nullptr) {
        title_label_->set_text(title_);
    }
    set_semantics_label(title_);
    return *this;
}

Dialog& Dialog::set_body(std::string_view body) {
    if (body_ == body) {
        return *this;
    }
    body_ = std::string(body);
    if (body_label_ != nullptr) {
        body_label_->set_text(body_);
    }
    return *this;
}

Dialog& Dialog::set_confirm_button_text(std::string_view text) {
    if (confirm_button_ != nullptr && confirm_button_->text() != text) {
        confirm_button_->set_text(text);
    }
    return *this;
}

Dialog& Dialog::set_cancel_button_text(std::string_view text) {
    if (cancel_button_ != nullptr && cancel_button_->text() != text) {
        cancel_button_->set_text(text);
    }
    return *this;
}

Dialog& Dialog::set_show_close(bool show_close) {
    if (show_close_ == show_close) {
        return *this;
    }
    show_close_ = show_close;
    if (close_button_ != nullptr) {
        close_button_->set_visible(show_close_);
    }
    return *this;
}

Dialog& Dialog::set_show_cancel_button(bool show_cancel_button) {
    if (show_cancel_button_ == show_cancel_button) {
        return *this;
    }
    show_cancel_button_ = show_cancel_button;
    if (cancel_button_ != nullptr) {
        cancel_button_->set_visible(show_cancel_button_);
    }
    return *this;
}

Dialog& Dialog::set_close_on_confirm(bool close_on_confirm) noexcept {
    close_on_confirm_ = close_on_confirm;
    return *this;
}

Dialog& Dialog::set_draggable(bool draggable) noexcept {
    draggable_ = draggable;
    return *this;
}

void Dialog::apply_visual_state() {
    if (surface_ != nullptr) {
        const auto border =
            modal_ ? rendering::Color::rgba(235, 238, 245) : rendering::Color::rgba(220, 223, 230);
        apply_surface_style(*surface_, 4.0F, rendering::Color::rgba(255, 255, 255), border, true);
        surface_->set_shadow(modal_ ? modal_overlay_shadow : floating_overlay_shadow);
    }
    invalidate_paint();
}

Dialog& Dialog::set_on_action(ActionHandler handler) {
    action_handler_ = std::move(handler);
    return *this;
}

const std::string& Dialog::title() const noexcept {
    return title_;
}

const std::string& Dialog::body() const noexcept {
    return body_;
}

bool Dialog::show_close() const noexcept {
    return show_close_;
}

bool Dialog::show_cancel_button() const noexcept {
    return show_cancel_button_;
}

bool Dialog::draggable() const noexcept {
    return draggable_;
}

Dialog& Dialog::show(elements::UIElement& host, DialogOptions options) {
    const auto viewport = viewport_for(host);
    const auto width =
        std::min(std::max(options.width, 360.0F), std::max(viewport.width - 48.0F, 360.0F));
    const auto size = options.fullscreen
                          ? layout::Size{viewport.width, viewport.height}
                          : layout::Size{width, dialog_height_for(options, width, viewport)};
    const auto bounds = options.fullscreen ? layout::Rect{viewport.x, viewport.y,
                                                          std::numeric_limits<float>::quiet_NaN(),
                                                          std::numeric_limits<float>::quiet_NaN()}
                                           : centered_bounds(viewport, size);
    auto dialog = std::make_unique<Dialog>();
    dialog->modal_ = options.modal;
    dialog->set_title(options.title)
        .set_body(options.body)
        .set_confirm_button_text(options.confirm_button_text)
        .set_cancel_button_text(options.cancel_button_text)
        .set_show_close(options.show_close)
        .set_show_cancel_button(options.show_cancel_button)
        .set_close_on_confirm(options.close_on_confirm)
        .set_draggable(options.draggable)
        .set_on_action(std::move(options.on_action));
    dialog->apply_visual_state();
    if (options.fullscreen) {
        dialog->configure_layout([](layout::LayoutElement& item) {
            item.set_size(layout::Length::percent(100.0F), layout::Length::percent(100.0F))
                .set_flex_direction(layout::FlexDirection::Column);
        });
    } else {
        configure_feedback_surface(*dialog, size);
    }
    auto& dialog_ref = *dialog;
    auto top_layer_options = elements::TopLayerOptions{};
    top_layer_options.bounds = bounds;
    top_layer_options.light_dismiss = options.close_on_click_modal;
    top_layer_options.close_on_escape = options.close_on_press_escape;
    top_layer_options.modal = options.modal;
    top_layer_options.backdrop_color = options.modal ? modal_backdrop_color : transparent;
    host.push_top_layer(std::move(dialog), std::move(top_layer_options));
    return dialog_ref;
}

void Dialog::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    static_cast<void>(context);
    static_cast<void>(absolute_frame);
}

bool Dialog::on_animation_frame(animation::AnimationTimePoint now) {
    const auto active = open_progress_.tick(now);
    if (active) {
        apply_open_animation();
    }
    return active;
}

void Dialog::on_pointer_event(elements::PointerEvent& event) {
    const auto was_dragging = dragging_;
    if (handle_overlay_drag(*this, draggable_, dragging_, drag_start_pointer_, drag_start_bounds_,
                            drag_current_delta_, event, dialog_drag_height)) {
        if (event.kind == elements::PointerEventKind::Down && dragging_) {
            open_progress_.set(1.0F);
            static_cast<void>(capture_pointer());
        } else if (was_dragging && !dragging_) {
            release_pointer_capture();
        }
        return;
    }
    if (was_dragging && !dragging_) {
        release_pointer_capture();
    }
    elements::UIElement::on_pointer_event(event);
}

elements::PointerCursor
Dialog::cursor_for_local_point(layout::Point local_position) const noexcept {
    return overlay_drag_cursor_for(*this, draggable_, local_position, dialog_drag_height);
}

void Dialog::close_with_action(DialogAction action) {
    auto handler = action_handler_;
    if (action == DialogAction::Confirm && !close_on_confirm_) {
        if (handler) {
            handler(action);
        }
        return;
    }
    dismiss_own_top_layer();
    if (handler) {
        handler(action);
    }
}

void Dialog::restart_open_animation() noexcept {
    open_progress_.set(0.82F);
    apply_open_animation();
    open_progress_.animate_to(1.0F, animation::AnimationDuration{0.18F});
}

void Dialog::apply_open_animation() noexcept {
    apply_entry_animation(*this, open_progress_.value(), -12.0F);
}

} // namespace winelement::controls
