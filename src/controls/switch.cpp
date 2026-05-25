#include <winelement/controls/switch.hpp>

#include <winelement/controls/property_keys.hpp>
#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <utility>

namespace winelement::controls {

namespace {
[[nodiscard]] float height_for(SwitchSize size) noexcept {
    switch (size) {
    case SwitchSize::Large:
        return 24.0F;
    case SwitchSize::Small:
        return 16.0F;
    case SwitchSize::Default:
        return 20.0F;
    }
    return 20.0F;
}
} // namespace

struct Switch::EventState {
    ChangeEventHandler changed;
};

Switch::Switch() : Control() {
    apply_style_value(style::default_switch_style(), true);
    set_theme_class(style::theme_class::switch_control);
    set_focusable(true);
    update_measure_callback();
}

Switch::~Switch() = default;

Switch& Switch::set_checked(bool checked) {
    set_property(property_keys::switch_checked(), checked);
    return *this;
}

Switch& Switch::set_disabled(bool disabled) noexcept {
    UIElement::set_disabled(disabled);
    invalidate_paint();
    return *this;
}

Switch& Switch::set_loading(bool loading) noexcept {
    set_property(property_keys::switch_loading(), loading);
    return *this;
}

Switch& Switch::set_size(SwitchSize size) {
    set_property(property_keys::switch_size(), size);
    return *this;
}

Switch& Switch::set_active_text(std::string_view text) {
    active_text_ = text;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Switch& Switch::set_inactive_text(std::string_view text) {
    inactive_text_ = text;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Switch& Switch::set_active_value(std::string_view value) {
    active_value_ = std::string(value);
    return *this;
}

Switch& Switch::set_inactive_value(std::string_view value) {
    inactive_value_ = std::string(value);
    return *this;
}

Switch& Switch::set_controlled(bool controlled) noexcept {
    controlled_ = controlled;
    return *this;
}

Switch::ChangeEventHandler& Switch::changed() noexcept {
    return ensure_event_state().changed;
}

Switch::EventState& Switch::ensure_event_state() {
    if (event_state_ == nullptr) {
        event_state_ = std::make_unique<EventState>();
    }
    return *event_state_;
}

Switch& Switch::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    update_measure_callback();
    return *this;
}

bool Switch::checked() const noexcept {
    return checked_;
}

bool Switch::disabled() const noexcept {
    return disabled_;
}

bool Switch::loading() const noexcept {
    return loading_;
}

SwitchSize Switch::size() const noexcept {
    return size_;
}

const std::string& Switch::active_text() const noexcept {
    return active_text_;
}

const std::string& Switch::inactive_text() const noexcept {
    return inactive_text_;
}

const std::string& Switch::active_value() const noexcept {
    return active_value_;
}

const std::string& Switch::inactive_value() const noexcept {
    return inactive_value_;
}

const std::string& Switch::value() const noexcept {
    return checked_ ? active_value_ : inactive_value_;
}

bool Switch::controlled() const noexcept {
    return controlled_;
}

void Switch::apply_property_change(const core::PropertyChange& change) {
    if (!change.changed) {
        return;
    }

    const auto id = change.metadata->id;

    if (id == property_keys::switch_checked().id()) {
        auto* v = properties().local_value<bool>(property_keys::switch_checked());
        checked_ = v ? *v : false;
        animate_checked(checked_ ? 1.0F : 0.0F);
        invalidate_paint();
        if (event_state_ != nullptr && !event_state_->changed.empty()) {
            event_state_->changed.emit(checked_);
        }
        return;
    }
    if (id == property_keys::switch_loading().id()) {
        auto* v = properties().local_value<bool>(property_keys::switch_loading());
        loading_ = v ? *v : false;
        invalidate_paint();
        return;
    }
    if (id == property_keys::switch_size().id()) {
        auto* v = properties().local_value<SwitchSize>(property_keys::switch_size());
        size_ = v ? *v : SwitchSize::Default;
        update_measure_callback();
        mark_measure_dirty();
        invalidate_paint();
        return;
    }

    UIElement::apply_property_change(change);
}

void Switch::on_pointer_event(elements::PointerEvent& event) {
    if (event.kind == elements::PointerEventKind::Enter) {
        hovered_ = true;
        animate_hover(1.0F);
        invalidate_paint();
        return;
    }
    if (event.kind == elements::PointerEventKind::Leave) {
        hovered_ = false;
        animate_hover(0.0F);
        invalidate_paint();
        return;
    }
    if (disabled_ || loading_ || event.button != elements::PointerButton::Primary) {
        return;
    }
    if (event.kind == elements::PointerEventKind::Down ||
        event.kind == elements::PointerEventKind::DoubleClick) {
        event.handled = true;
        return;
    }
    if (event.kind == elements::PointerEventKind::Up) {
        toggle();
        event.handled = true;
    }
}

void Switch::on_key_event(elements::KeyEvent& event) {
    if (disabled_ || loading_ || event.kind != elements::KeyEventKind::Down) {
        return;
    }
    if (event.key == elements::Key::Space || event.key == elements::Key::Enter) {
        toggle();
        event.handled = true;
    }
}

elements::PointerCursor
Switch::cursor_for_local_point(layout::Point local_position) const noexcept {
    if (disabled_ || loading_) {
        return elements::PointerCursor::NotAllowed;
    }
    const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    return layout::rect_contains_point(local_frame, local_position)
               ? elements::PointerCursor::Hand
               : elements::PointerCursor::Default;
}

bool Switch::on_animation_frame(animation::AnimationTimePoint now) {
    auto active = checked_progress_.tick(now);
    active = hover_progress_.tick(now) || active;
    if (active) {
        invalidate_paint();
    }
    return active;
}

void Switch::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    const auto height = height_for(size_);
    const auto width = height * 2.0F;
    const auto track = layout::Rect{
        absolute_frame.x,
        absolute_frame.y + std::max(0.0F, (absolute_frame.height - height) * 0.5F), width, height};
    const auto radius = rendering::CornerRadius::uniform(height * 0.5F);
    const auto hover_progress = animated_hover_progress();
    const auto checked_progress = animated_checked_progress();
    const auto idle_track = animation::interpolate_value(
        style_storage().background, style_storage().hover_background, hover_progress);
    const auto track_color =
        disabled_ ? style_storage().read_only_background
                  : animation::interpolate_value(idle_track, style_storage().active_background,
                                                 checked_progress);
    context.fill_rounded_rect(track, radius, track_color);

    const auto knob_extent = std::max(2.0F, height - 4.0F);
    const auto knob_x =
        track.x + 2.0F +
        (track.width - knob_extent - 4.0F) * std::clamp(checked_progress, 0.0F, 1.0F);
    const auto knob = layout::Rect{knob_x, track.y + 2.0F, knob_extent, knob_extent};
    context.fill_ellipse(knob, rendering::Color::rgba(255, 255, 255));

    const auto& label = checked_ ? active_text_ : inactive_text_;
    if (!label.empty()) {
        const auto text_rect = layout::Rect{
            track.x + track.width + 8.0F, absolute_frame.y,
            std::max(0.0F, absolute_frame.width - track.width - 8.0F), absolute_frame.height};
        context.draw_text(
            label, text_rect,
            rendering::TextStyle{.font_size = style_storage().font_size,
                                 .color = disabled_ ? style_storage().semantic.disabled_text
                                                    : style_storage().text_color,
                                 .vertical_alignment = rendering::TextVerticalAlignment::Center,
                                 .wrapping = rendering::TextWrapping::NoWrap,
                                 .trimming = rendering::TextTrimming::CharacterEllipsis});
    }
}

void Switch::update_measure_callback() {
    set_measure_callback([this](const layout::MeasureInput&) {
        const auto height = height_for(size_);
        const auto measure_label = [this](const std::string& label) {
            return label.empty()
                       ? layout::Size{}
                       : text_engine().measure_single_line(
                             label, rendering::TextStyle{.font_size = style_storage().font_size,
                                                         .color = style_storage().text_color});
        };
        const auto active_text_size = measure_label(active_text_);
        const auto inactive_text_size = measure_label(inactive_text_);
        const auto text_width = std::max(active_text_size.width, inactive_text_size.width);
        const auto text_height = std::max(active_text_size.height, inactive_text_size.height);
        const auto has_label = !active_text_.empty() || !inactive_text_.empty();
        return layout::Size{height * 2.0F + (has_label ? 8.0F + text_width : 0.0F),
                            std::max(height, text_height)};
    });
}

float Switch::animated_checked_progress() const {
    return std::clamp(checked_progress_.value(), 0.0F, 1.0F);
}

float Switch::animated_hover_progress() const {
    return std::clamp(hover_progress_.value(), 0.0F, 1.0F);
}

void Switch::animate_checked(float target) {
    checked_progress_.animate_to(target);
}

void Switch::animate_hover(float target) {
    hover_progress_.animate_to(target);
}

void Switch::toggle() {
    set_checked(!checked_);
}

} // namespace winelement::controls
