#include <winelement/controls/scrollbar.hpp>

#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace winelement::controls {

namespace {

constexpr auto element_scrollbar_edge_inset = 2.0F;
constexpr auto element_scrollbar_min_hit_cross_extent = 12.0F;

[[nodiscard]] bool scroll_axis_visible(float maximum) noexcept {
    return maximum > 0.0F;
}

[[nodiscard]] std::uint8_t scaled_alpha(std::uint8_t alpha, float opacity) noexcept {
    return static_cast<std::uint8_t>(std::clamp(
        std::round(static_cast<float>(alpha) * std::clamp(opacity, 0.0F, 1.0F)), 0.0F, 255.0F));
}

} // namespace

struct Scrollbar::EventState {
    ScrollEventSignal scrolled;
    ScrollDataEventSignal scroll_data_changed;
    EndReachedEventSignal end_reached;
    core::EventToken legacy_scroll_token = 0U;
    core::EventToken legacy_scroll_data_token = 0U;
    core::EventToken legacy_end_reached_token = 0U;
};

Scrollbar::Scrollbar() : Control() {
    apply_style_value(style::default_scrollbar_style(), true);
    set_theme_class(style::theme_class::scrollbar);
    set_focusable(true);
    update_measure_callback();
}

Scrollbar::~Scrollbar() = default;

Scrollbar& Scrollbar::set_orientation(ScrollbarOrientation orientation) {
    if (orientation_ == orientation) {
        return *this;
    }

    orientation_ = orientation;
    update_measure_callback();
    if (!is_scroll_container()) {
        mark_measure_dirty();
    }
    invalidate_paint();
    return *this;
}

Scrollbar& Scrollbar::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return *this;
    }

    elements::UIElement::set_disabled(disabled);
    if (disabled_) {
        reset_interaction_state();
    }
    return *this;
}

Scrollbar& Scrollbar::set_visibility_mode(ScrollbarVisibility visibility) noexcept {
    if (visibility_ == visibility) {
        return *this;
    }

    visibility_ = visibility;
    invalidate_paint();
    return *this;
}

Scrollbar& Scrollbar::set_range(float minimum, float maximum, float page_size) {
    minimum_ = std::isfinite(minimum) ? minimum : 0.0F;
    maximum_ = std::isfinite(maximum) ? std::max(maximum, minimum_) : minimum_;
    page_size_ = std::isfinite(page_size) ? std::max(page_size, 0.0F) : 0.0F;
    reset_end_reached_latches();
    set_value(value_);
    invalidate_paint();
    return *this;
}

Scrollbar& Scrollbar::set_value(float value) {
    const auto next_value = clamped_value(value);
    if (value_ == next_value) {
        return *this;
    }

    value_ = next_value;
    invalidate_paint();
    if (event_state_ != nullptr && !event_state_->scrolled.empty()) {
        event_state_->scrolled.emit(value_);
    }
    if (event_state_ != nullptr && !event_state_->scroll_data_changed.empty()) {
        event_state_->scroll_data_changed.emit(orientation_ == ScrollbarOrientation::Vertical
                                                   ? ScrollbarScrollData{.scroll_top = value_}
                                                   : ScrollbarScrollData{.scroll_left = value_});
    }
    notify_end_reached();
    return *this;
}

Scrollbar& Scrollbar::set_thickness(float thickness) {
    thickness_ = std::isfinite(thickness) ? std::max(thickness, 1.0F) : 6.0F;
    update_measure_callback();
    if (!is_scroll_container()) {
        mark_measure_dirty();
    }
    invalidate_paint();
    return *this;
}

Scrollbar& Scrollbar::set_min_thumb_extent(float extent) {
    min_thumb_extent_ = std::isfinite(extent) ? std::max(extent, 1.0F) : 20.0F;
    invalidate_paint();
    return *this;
}

Scrollbar& Scrollbar::set_min_size(float extent) {
    return set_min_thumb_extent(extent);
}

Scrollbar& Scrollbar::set_always_visible(bool always) noexcept {
    return set_visibility_mode(always ? ScrollbarVisibility::Always : ScrollbarVisibility::Auto);
}

Scrollbar& Scrollbar::set_noresize(bool noresize) noexcept {
    noresize_ = noresize;
    return *this;
}

Scrollbar& Scrollbar::set_distance(float distance) noexcept {
    distance_ = std::isfinite(distance) ? std::max(distance, 0.0F) : 0.0F;
    reset_end_reached_latches();
    return *this;
}

Scrollbar& Scrollbar::set_track_click_enabled(bool enabled) noexcept {
    track_click_enabled_ = enabled;
    return *this;
}

Scrollbar& Scrollbar::set_container_mode(bool enabled) {
    if (container_mode_ == enabled) {
        return *this;
    }

    container_mode_ = enabled;
    if (container_mode_) {
        set_overflow(layout::Overflow::Hidden);
        set_scroll_wheel_enabled(true);
    } else {
        set_overflow(layout::Overflow::Visible);
        set_scroll_wheel_enabled(false);
    }
    update_measure_callback();
    if (!is_scroll_container()) {
        mark_measure_dirty();
    }
    invalidate_paint();
    return *this;
}

Scrollbar& Scrollbar::set_height(float height) {
    if (!std::isfinite(height) || height < 0.0F) {
        throw std::invalid_argument("scrollbar height must be finite and non-negative");
    }
    set_container_mode(true);
    configure_layout(
        [height](layout::LayoutElement& item) { item.set_height(layout::Length::points(height)); });
    return *this;
}

Scrollbar& Scrollbar::set_max_height(float max_height) {
    if (!std::isfinite(max_height) || max_height < 0.0F) {
        throw std::invalid_argument("scrollbar max height must be finite and non-negative");
    }
    set_container_mode(true);
    configure_layout([max_height](layout::LayoutElement& item) {
        item.set_max_height(layout::Length::points(max_height));
    });
    return *this;
}

Scrollbar& Scrollbar::set_content(std::unique_ptr<elements::UIElement> content) {
    set_container_mode(true);
    clear_children();
    if (content != nullptr) {
        append_child(std::move(content));
    }
    invalidate_layout();
    invalidate_paint();
    return *this;
}

Scrollbar& Scrollbar::bind_range(RangeProvider provider) {
    range_provider_ = std::move(provider);
    return refresh_bound_range();
}

Scrollbar& Scrollbar::refresh_bound_range() {
    if (is_scroll_container()) {
        sync_container_range_if_needed();
        return *this;
    }
    if (!range_provider_) {
        return *this;
    }
    const auto range = range_provider_();
    set_range(range.minimum, range.maximum, range.page_size);
    set_value(range.value);
    return *this;
}

Scrollbar& Scrollbar::update() {
    return refresh_bound_range();
}

void Scrollbar::sync_bound_range_if_needed() {
    if (noresize_) {
        return;
    }
    if (is_scroll_container()) {
        sync_container_range_if_needed();
        return;
    }
    if (!range_provider_) {
        return;
    }
    refresh_bound_range();
}

void Scrollbar::sync_container_range_if_needed() {
    const auto viewport = viewport_rect();
    const auto maximum = max_scroll_offset();
    minimum_ = 0.0F;
    maximum_ = orientation_ == ScrollbarOrientation::Vertical ? std::max(maximum.y, 0.0F)
                                                              : std::max(maximum.x, 0.0F);
    page_size_ = orientation_ == ScrollbarOrientation::Vertical ? std::max(viewport.height, 0.0F)
                                                                : std::max(viewport.width, 0.0F);
    value_ = orientation_ == ScrollbarOrientation::Vertical ? scroll_offset().y : scroll_offset().x;
}

Scrollbar& Scrollbar::scroll_to(float scroll_left, float scroll_top) {
    if (is_scroll_container()) {
        const auto previous_scroll = scroll_offset();
        set_scroll_offset(layout::Point{scroll_left, scroll_top});
        emit_container_scroll_changed(previous_scroll);
        return *this;
    }
    return orientation_ == ScrollbarOrientation::Vertical ? set_scroll_top(scroll_top)
                                                          : set_scroll_left(scroll_left);
}

Scrollbar& Scrollbar::set_scroll_top(float scroll_top) {
    if (is_scroll_container()) {
        set_container_scroll_axis(ScrollbarOrientation::Vertical, scroll_top);
        return *this;
    }
    if (orientation_ == ScrollbarOrientation::Vertical) {
        set_value(scroll_top);
    }
    return *this;
}

Scrollbar& Scrollbar::set_scroll_left(float scroll_left) {
    if (is_scroll_container()) {
        set_container_scroll_axis(ScrollbarOrientation::Horizontal, scroll_left);
        return *this;
    }
    if (orientation_ == ScrollbarOrientation::Horizontal) {
        set_value(scroll_left);
    }
    return *this;
}

Scrollbar& Scrollbar::set_on_scroll(ScrollHandler handler) {
    auto& state = ensure_event_state();
    core::replace_handler_subscription(
        state.scrolled, state.legacy_scroll_token,
        handler ? ScrollEventSignal::Handler{std::move(handler)} : ScrollEventSignal::Handler{});
    return *this;
}

Scrollbar& Scrollbar::set_on_scroll_data(ScrollDataHandler handler) {
    auto& state = ensure_event_state();
    core::replace_handler_subscription(
        state.scroll_data_changed, state.legacy_scroll_data_token,
        handler ? ScrollDataEventSignal::Handler{std::move(handler)}
                : ScrollDataEventSignal::Handler{});
    return *this;
}

Scrollbar& Scrollbar::set_on_end_reached(EndReachedHandler handler) {
    auto& state = ensure_event_state();
    core::replace_handler_subscription(
        state.end_reached, state.legacy_end_reached_token,
        handler ? EndReachedEventSignal::Handler{std::move(handler)}
                : EndReachedEventSignal::Handler{});
    reset_end_reached_latches();
    return *this;
}

Scrollbar::ScrollEventSignal& Scrollbar::scrolled() noexcept {
    return ensure_event_state().scrolled;
}

Scrollbar::ScrollDataEventSignal& Scrollbar::scroll_data_changed() noexcept {
    return ensure_event_state().scroll_data_changed;
}

Scrollbar::EndReachedEventSignal& Scrollbar::end_reached() noexcept {
    return ensure_event_state().end_reached;
}

Scrollbar::EventState& Scrollbar::ensure_event_state() {
    if (event_state_ == nullptr) {
        event_state_ = std::make_unique<EventState>();
    }
    return *event_state_;
}

Scrollbar& Scrollbar::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    update_measure_callback();
    return *this;
}

ScrollbarOrientation Scrollbar::orientation() const noexcept {
    return orientation_;
}

ScrollbarVisibility Scrollbar::visibility_mode() const noexcept {
    return visibility_;
}

float Scrollbar::minimum() const noexcept {
    return minimum_;
}

float Scrollbar::maximum() const noexcept {
    return maximum_;
}

float Scrollbar::page_size() const noexcept {
    return page_size_;
}

float Scrollbar::value() const noexcept {
    return value_;
}

float Scrollbar::thickness() const noexcept {
    return thickness_;
}

float Scrollbar::min_thumb_extent() const noexcept {
    return min_thumb_extent_;
}

float Scrollbar::min_size() const noexcept {
    return min_thumb_extent_;
}

bool Scrollbar::noresize() const noexcept {
    return noresize_;
}

float Scrollbar::distance() const noexcept {
    return distance_;
}

bool Scrollbar::track_click_enabled() const noexcept {
    return track_click_enabled_;
}

bool Scrollbar::container_mode() const noexcept {
    return container_mode_;
}

void Scrollbar::on_pointer_event(elements::PointerEvent& event) {
    sync_bound_range_if_needed();
    if (disabled_) {
        if (dragging_ && (event.kind == elements::PointerEventKind::Cancel ||
                          event.kind == elements::PointerEventKind::Up)) {
            reset_interaction_state();
            event.handled = true;
        }
        return;
    }

    if (is_scroll_container()) {
        if (event.kind == elements::PointerEventKind::Enter) {
            hovered_ = true;
            animate_hover(1.0F);
            invalidate_paint();
            return;
        }
        if (event.kind == elements::PointerEventKind::Leave) {
            hovered_ = false;
            if (!dragging_) {
                animate_hover(0.0F);
            }
            invalidate_paint();
            return;
        }
        if (event.kind == elements::PointerEventKind::Cancel) {
            dragging_ = false;
            drag_thumb_offset_ = 0.0F;
            animate_drag(0.0F);
            if (!hovered_) {
                animate_hover(0.0F);
            }
            release_pointer_capture();
            event.handled = true;
            return;
        }
        const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
        const auto max_scroll = max_scroll_offset();
        const auto can_scroll_vertical = scroll_axis_visible(max_scroll.y);
        const auto can_scroll_horizontal = scroll_axis_visible(max_scroll.x);
        const auto axis_at_point = [&]() -> std::optional<ScrollbarOrientation> {
            if (can_scroll_vertical &&
                layout::rect_contains_point(
                    thumb_hit_rect(local_frame, ScrollbarOrientation::Vertical),
                    event.local_position)) {
                return ScrollbarOrientation::Vertical;
            }
            if (can_scroll_horizontal &&
                layout::rect_contains_point(
                    thumb_hit_rect(local_frame, ScrollbarOrientation::Horizontal),
                    event.local_position)) {
                return ScrollbarOrientation::Horizontal;
            }
            if (!track_click_enabled_) {
                return std::nullopt;
            }
            if (can_scroll_vertical &&
                layout::rect_contains_point(track_rect(local_frame, ScrollbarOrientation::Vertical),
                                            event.local_position)) {
                return ScrollbarOrientation::Vertical;
            }
            if (can_scroll_horizontal &&
                layout::rect_contains_point(
                    track_rect(local_frame, ScrollbarOrientation::Horizontal),
                    event.local_position)) {
                return ScrollbarOrientation::Horizontal;
            }
            return std::nullopt;
        };

        if (event.kind == elements::PointerEventKind::Down &&
            event.button == elements::PointerButton::Primary) {
            const auto axis = axis_at_point();
            if (axis.has_value()) {
                const auto thumb = thumb_rect(local_frame, *axis);
                dragging_orientation_ = *axis;
                drag_thumb_offset_ =
                    layout::rect_contains_point(thumb_hit_rect(local_frame, *axis),
                                                event.local_position)
                        ? axis_coordinate(event.local_position, *axis) - axis_origin(thumb, *axis)
                        : axis_extent(thumb, *axis) * 0.5F;
                dragging_ = true;
                animate_drag(1.0F);
                animate_hover(1.0F);
                static_cast<void>(capture_pointer());
                set_container_scroll_axis(*axis,
                                          value_for_local_point(event.local_position, *axis));
                event.handled = true;
                return;
            }
        }
        if (event.kind == elements::PointerEventKind::Move && dragging_) {
            set_container_scroll_axis(
                dragging_orientation_,
                value_for_local_point(event.local_position, dragging_orientation_));
            event.handled = true;
            return;
        }
        if (event.kind == elements::PointerEventKind::Up && dragging_) {
            set_container_scroll_axis(
                dragging_orientation_,
                value_for_local_point(event.local_position, dragging_orientation_));
            dragging_ = false;
            drag_thumb_offset_ = 0.0F;
            animate_drag(0.0F);
            if (!hovered_) {
                animate_hover(0.0F);
            }
            release_pointer_capture();
            event.handled = true;
            return;
        }

        const auto previous_scroll = scroll_offset();
        elements::UIElement::on_pointer_event(event);
        emit_container_scroll_changed(previous_scroll);
        return;
    }

    if (event.kind == elements::PointerEventKind::Enter) {
        hovered_ = true;
        animate_hover(1.0F);
        invalidate_paint();
        return;
    }
    if (event.kind == elements::PointerEventKind::Leave) {
        hovered_ = false;
        if (!dragging_) {
            animate_hover(0.0F);
        }
        invalidate_paint();
        return;
    }
    if (event.kind == elements::PointerEventKind::Cancel) {
        dragging_ = false;
        drag_thumb_offset_ = 0.0F;
        animate_drag(0.0F);
        if (!hovered_) {
            animate_hover(0.0F);
        }
        release_pointer_capture();
        event.handled = true;
        return;
    }
    if (event.kind == elements::PointerEventKind::Down &&
        event.button == elements::PointerButton::Primary) {
        const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
        if (!track_click_enabled_ &&
            !layout::rect_contains_point(thumb_hit_rect(local_frame), event.local_position)) {
            return;
        }
        const auto thumb = thumb_rect(local_frame);
        if (layout::rect_contains_point(thumb_hit_rect(local_frame), event.local_position)) {
            drag_thumb_offset_ = axis_coordinate(event.local_position) - axis_origin(thumb);
        } else {
            drag_thumb_offset_ = axis_extent(thumb) * 0.5F;
        }
        dragging_ = true;
        animate_drag(1.0F);
        animate_hover(1.0F);
        static_cast<void>(capture_pointer());
        set_value(value_for_local_point(event.local_position));
        event.handled = true;
        return;
    }
    if (event.kind == elements::PointerEventKind::Move && dragging_) {
        set_value(value_for_local_point(event.local_position));
        event.handled = true;
        return;
    }
    if (event.kind == elements::PointerEventKind::Up && dragging_) {
        set_value(value_for_local_point(event.local_position));
        dragging_ = false;
        drag_thumb_offset_ = 0.0F;
        animate_drag(0.0F);
        if (!hovered_) {
            animate_hover(0.0F);
        }
        release_pointer_capture();
        event.handled = true;
    }
}

bool Scrollbar::on_animation_frame(animation::AnimationTimePoint now) {
    sync_bound_range_if_needed();
    auto active = hover_progress_.tick(now);
    active = drag_progress_.tick(now) || active;
    if (active) {
        invalidate_paint();
    }
    return active;
}

void Scrollbar::on_key_event(elements::KeyEvent& event) {
    sync_bound_range_if_needed();
    if (event.kind != elements::KeyEventKind::Down || disabled_) {
        return;
    }

    if (is_scroll_container()) {
        const auto max_scroll = max_scroll_offset();
        const auto current_scroll = scroll_offset();
        const auto line_step =
            std::max(orientation_ == ScrollbarOrientation::Vertical ? viewport_rect().height * 0.1F
                                                                    : viewport_rect().width * 0.1F,
                     1.0F);
        const auto page_step =
            std::max(orientation_ == ScrollbarOrientation::Vertical ? viewport_rect().height
                                                                    : viewport_rect().width,
                     line_step);
        auto next_scroll = current_scroll;
        switch (event.key) {
        case elements::Key::Up:
            next_scroll.y -= line_step;
            break;
        case elements::Key::Down:
            next_scroll.y += line_step;
            break;
        case elements::Key::Left:
            next_scroll.x -= line_step;
            break;
        case elements::Key::Right:
            next_scroll.x += line_step;
            break;
        case elements::Key::PageUp:
            next_scroll.y -= page_step;
            break;
        case elements::Key::PageDown:
            next_scroll.y += page_step;
            break;
        case elements::Key::Home:
            next_scroll = {};
            break;
        case elements::Key::End:
            next_scroll = max_scroll;
            break;
        default:
            return;
        }
        set_scroll_offset(next_scroll);
        emit_container_scroll_changed(current_scroll);
        event.handled = true;
        return;
    }

    const auto line_step = std::max(page_size_ * 0.1F, 1.0F);
    const auto page_step = std::max(page_size_, line_step);
    auto next_value = value_;
    switch (event.key) {
    case elements::Key::Up:
        if (orientation_ == ScrollbarOrientation::Vertical) {
            next_value -= line_step;
        } else {
            return;
        }
        break;
    case elements::Key::Down:
        if (orientation_ == ScrollbarOrientation::Vertical) {
            next_value += line_step;
        } else {
            return;
        }
        break;
    case elements::Key::Left:
        if (orientation_ == ScrollbarOrientation::Horizontal) {
            next_value -= line_step;
        } else {
            return;
        }
        break;
    case elements::Key::Right:
        if (orientation_ == ScrollbarOrientation::Horizontal) {
            next_value += line_step;
        } else {
            return;
        }
        break;
    case elements::Key::PageUp:
        next_value -= page_step;
        break;
    case elements::Key::PageDown:
        next_value += page_step;
        break;
    case elements::Key::Home:
        next_value = minimum_;
        break;
    case elements::Key::End:
        next_value = maximum_;
        break;
    default:
        return;
    }

    set_value(next_value);
    event.handled = true;
}

elements::PointerCursor
Scrollbar::cursor_for_local_point(layout::Point local_position) const noexcept {
    if (disabled_ || visibility_ == ScrollbarVisibility::Never) {
        return elements::PointerCursor::Default;
    }

    const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    if (is_scroll_container()) {
        const auto max_scroll = max_scroll_offset();
        if (scroll_axis_visible(max_scroll.y) &&
            layout::rect_contains_point(track_rect(local_frame, ScrollbarOrientation::Vertical),
                                        local_position)) {
            return elements::PointerCursor::Hand;
        }
        if (scroll_axis_visible(max_scroll.x) &&
            layout::rect_contains_point(track_rect(local_frame, ScrollbarOrientation::Horizontal),
                                        local_position)) {
            return elements::PointerCursor::Hand;
        }
        return elements::PointerCursor::Default;
    }

    if (visibility_ == ScrollbarVisibility::Auto && maximum_ <= minimum_) {
        return elements::PointerCursor::Default;
    }
    return layout::rect_contains_point(track_rect(local_frame), local_position)
               ? elements::PointerCursor::Hand
               : elements::PointerCursor::Default;
}

void Scrollbar::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    const_cast<Scrollbar*>(this)->sync_bound_range_if_needed();
    if (is_scroll_container()) {
        if (style_storage().background.alpha != 0U) {
            context.fill_rounded_rect(absolute_frame, style_storage().corner_radius,
                                      style_storage().background);
        }
        return;
    }
    if (visibility_ == ScrollbarVisibility::Never ||
        (visibility_ == ScrollbarVisibility::Auto && maximum_ <= minimum_)) {
        return;
    }

    const auto track = track_rect(absolute_frame);
    if (style_storage().background.alpha != 0U) {
        context.fill_rounded_rect(track, style_storage().corner_radius, style_storage().background);
    }
    const auto hover_progress = animated_hover_progress();
    const auto drag_progress = animated_drag_progress();
    const auto visibility_progress =
        visibility_ == ScrollbarVisibility::Always ? 1.0F : std::max(hover_progress, drag_progress);
    if (visibility_progress <= 0.001F) {
        return;
    }
    const auto idle_thumb = rendering::Color::rgba(
        style_storage().hover_background.red, style_storage().hover_background.green,
        style_storage().hover_background.blue,
        scaled_alpha(style_storage().hover_background.alpha, 0.72F * visibility_progress));
    const auto hover_thumb =
        animation::interpolate_value(idle_thumb, style_storage().hover_background, hover_progress);
    const auto thumb =
        animation::interpolate_value(hover_thumb, style_storage().active_background, drag_progress);
    context.fill_rounded_rect(thumb_rect(absolute_frame), style_storage().corner_radius, thumb);
}

void Scrollbar::on_paint_overlay(rendering::RenderContext& context,
                                 layout::Rect absolute_frame) const {
    if (!is_scroll_container()) {
        return;
    }
    if (!noresize_) {
        const_cast<Scrollbar*>(this)->sync_container_range_if_needed();
    }
    if (visibility_ == ScrollbarVisibility::Never) {
        return;
    }

    const auto max_scroll = max_scroll_offset();
    const auto hover_progress = animated_hover_progress();
    const auto drag_progress = animated_drag_progress();
    const auto visibility_progress =
        visibility_ == ScrollbarVisibility::Always ? 1.0F : std::max(hover_progress, drag_progress);
    if (visibility_progress <= 0.001F) {
        return;
    }
    const auto idle_thumb = rendering::Color::rgba(
        style_storage().hover_background.red, style_storage().hover_background.green,
        style_storage().hover_background.blue,
        scaled_alpha(style_storage().hover_background.alpha, 0.72F * visibility_progress));
    const auto hover_thumb =
        animation::interpolate_value(idle_thumb, style_storage().hover_background, hover_progress);
    const auto thumb =
        animation::interpolate_value(hover_thumb, style_storage().active_background, drag_progress);

    const auto paint_axis = [&](ScrollbarOrientation orientation) {
        const auto axis_max =
            orientation == ScrollbarOrientation::Vertical ? max_scroll.y : max_scroll.x;
        if (!scroll_axis_visible(axis_max)) {
            return;
        }
        const auto track = track_rect(absolute_frame, orientation);
        if (style_storage().background.alpha != 0U) {
            context.fill_rounded_rect(track, style_storage().corner_radius,
                                      style_storage().background);
        }
        context.fill_rounded_rect(thumb_rect(absolute_frame, orientation),
                                  style_storage().corner_radius, thumb);
    };
    paint_axis(ScrollbarOrientation::Vertical);
    paint_axis(ScrollbarOrientation::Horizontal);
}

void Scrollbar::update_measure_callback() {
    if (is_scroll_container()) {
        clear_measure_callback();
        return;
    }
    set_measure_callback([this](const layout::MeasureInput&) {
        return orientation_ == ScrollbarOrientation::Vertical ? layout::Size{thickness_, 120.0F}
                                                              : layout::Size{120.0F, thickness_};
    });
}

void Scrollbar::reset_interaction_state() {
    const auto had_interaction = dragging_ || drag_thumb_offset_ != 0.0F ||
                                 animated_hover_progress() > 0.0F ||
                                 animated_drag_progress() > 0.0F;
    dragging_ = false;
    drag_thumb_offset_ = 0.0F;
    hover_progress_.set(0.0F);
    drag_progress_.set(0.0F);
    release_pointer_capture();
    if (had_interaction) {
        invalidate_paint();
    }
}

layout::Rect Scrollbar::track_rect(layout::Rect frame) const noexcept {
    return track_rect(frame, orientation_);
}

layout::Rect Scrollbar::track_rect(layout::Rect frame,
                                   ScrollbarOrientation orientation) const noexcept {
    const auto max_scroll = is_scroll_container() ? max_scroll_offset() : layout::Point{};
    const auto vertical_visible = is_scroll_container() && scroll_axis_visible(max_scroll.y);
    const auto horizontal_visible = is_scroll_container() && scroll_axis_visible(max_scroll.x);
    const auto corner_reserve = thickness_ + element_scrollbar_edge_inset;
    const auto cross_extent =
        orientation == ScrollbarOrientation::Vertical ? frame.width : frame.height;
    const auto axis_length = orientation == ScrollbarOrientation::Vertical
                                 ? frame.height - (horizontal_visible ? corner_reserve : 0.0F)
                                 : frame.width - (vertical_visible ? corner_reserve : 0.0F);
    const auto inset = std::min(element_scrollbar_edge_inset, std::max(axis_length, 0.0F) * 0.5F);
    if (orientation == ScrollbarOrientation::Vertical) {
        const auto width = std::min(std::max(cross_extent, 0.0F), thickness_);
        const auto x =
            is_scroll_container()
                ? frame.x + std::max(0.0F, frame.width - width - element_scrollbar_edge_inset)
                : frame.x + std::max(0.0F, frame.width - width) * 0.5F;
        return layout::Rect{x, frame.y + inset, width, std::max(0.0F, axis_length - inset * 2.0F)};
    }
    const auto height = std::min(std::max(cross_extent, 0.0F), thickness_);
    const auto y =
        is_scroll_container()
            ? frame.y + std::max(0.0F, frame.height - height - element_scrollbar_edge_inset)
            : frame.y + std::max(0.0F, frame.height - height) * 0.5F;
    return layout::Rect{frame.x + inset, y, std::max(0.0F, axis_length - inset * 2.0F), height};
}

layout::Rect Scrollbar::thumb_rect(layout::Rect frame) const noexcept {
    return thumb_rect(frame, orientation_);
}

layout::Rect Scrollbar::thumb_rect(layout::Rect frame,
                                   ScrollbarOrientation orientation) const noexcept {
    const auto track = track_rect(frame, orientation);
    const auto max_scroll = is_scroll_container() ? max_scroll_offset() : layout::Point{};
    const auto range = is_scroll_container() ? (orientation == ScrollbarOrientation::Vertical
                                                    ? std::max(max_scroll.y, 0.0F)
                                                    : std::max(max_scroll.x, 0.0F))
                                             : std::max(maximum_ - minimum_, 0.0F);
    const auto track_extent = axis_extent(track, orientation);
    const auto cross_extent =
        orientation == ScrollbarOrientation::Vertical ? track.width : track.height;
    const auto viewport = viewport_rect();
    const auto page_size = is_scroll_container() ? (orientation == ScrollbarOrientation::Vertical
                                                        ? std::max(viewport.height, 0.0F)
                                                        : std::max(viewport.width, 0.0F))
                                                 : page_size_;
    const auto minimum_thumb_extent =
        std::min(std::max(min_thumb_extent_, 1.0F), std::max(track_extent, 0.0F));
    const auto requested_thumb_extent = range <= 0.0F || range + page_size <= 0.0F
                                            ? track_extent
                                            : track_extent * page_size / (range + page_size);
    const auto thumb_extent =
        std::clamp(requested_thumb_extent, minimum_thumb_extent, std::max(track_extent, 0.0F));
    const auto travel = std::max(track_extent - thumb_extent, 0.0F);
    const auto scroll = scroll_offset();
    const auto value = is_scroll_container()
                           ? (orientation == ScrollbarOrientation::Vertical ? scroll.y : scroll.x)
                           : value_ - minimum_;
    const auto ratio = range <= 0.0F ? 0.0F : value / range;
    const auto offset = travel * std::clamp(ratio, 0.0F, 1.0F);
    if (orientation == ScrollbarOrientation::Vertical) {
        return layout::Rect{track.x, track.y + offset, cross_extent, thumb_extent};
    }
    return layout::Rect{track.x + offset, track.y, thumb_extent, cross_extent};
}

layout::Rect Scrollbar::thumb_hit_rect(layout::Rect frame) const noexcept {
    return thumb_hit_rect(frame, orientation_);
}

layout::Rect Scrollbar::thumb_hit_rect(layout::Rect frame,
                                       ScrollbarOrientation orientation) const noexcept {
    auto hit_rect = thumb_rect(frame, orientation);
    if (orientation == ScrollbarOrientation::Vertical) {
        const auto hit_width =
            std::min(std::max(frame.width, 0.0F),
                     std::max(hit_rect.width, element_scrollbar_min_hit_cross_extent));
        hit_rect.x += (hit_rect.width - hit_width) * 0.5F;
        hit_rect.width = hit_width;
    } else {
        const auto hit_height =
            std::min(std::max(frame.height, 0.0F),
                     std::max(hit_rect.height, element_scrollbar_min_hit_cross_extent));
        hit_rect.y += (hit_rect.height - hit_height) * 0.5F;
        hit_rect.height = hit_height;
    }
    return hit_rect;
}

float Scrollbar::axis_coordinate(layout::Point point) const noexcept {
    return axis_coordinate(point, orientation_);
}

float Scrollbar::axis_coordinate(layout::Point point,
                                 ScrollbarOrientation orientation) const noexcept {
    return orientation == ScrollbarOrientation::Vertical ? point.y : point.x;
}

float Scrollbar::axis_origin(layout::Rect rect) const noexcept {
    return axis_origin(rect, orientation_);
}

float Scrollbar::axis_origin(layout::Rect rect, ScrollbarOrientation orientation) const noexcept {
    return orientation == ScrollbarOrientation::Vertical ? rect.y : rect.x;
}

float Scrollbar::axis_extent(layout::Rect rect) const noexcept {
    return axis_extent(rect, orientation_);
}

float Scrollbar::axis_extent(layout::Rect rect, ScrollbarOrientation orientation) const noexcept {
    return orientation == ScrollbarOrientation::Vertical ? rect.height : rect.width;
}

float Scrollbar::animated_hover_progress() const {
    return std::clamp(hover_progress_.value(), 0.0F, 1.0F);
}

float Scrollbar::animated_drag_progress() const {
    return std::clamp(drag_progress_.value(), 0.0F, 1.0F);
}

void Scrollbar::animate_hover(float target) {
    hover_progress_.animate_to(target, animation::AnimationDuration{0.12F});
}

void Scrollbar::animate_drag(float target) {
    drag_progress_.animate_to(target, animation::AnimationDuration{0.08F});
}

float Scrollbar::value_for_local_point(layout::Point point) const noexcept {
    return value_for_local_point(point, orientation_);
}

float Scrollbar::value_for_local_point(layout::Point point,
                                       ScrollbarOrientation orientation) const noexcept {
    const auto frame_rect = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    const auto track = track_rect(frame_rect, orientation);
    const auto thumb = thumb_rect(frame_rect, orientation);
    const auto travel =
        std::max(axis_extent(track, orientation) - axis_extent(thumb, orientation), 0.0F);
    const auto coordinate =
        axis_coordinate(point, orientation) - axis_origin(track, orientation) - drag_thumb_offset_;
    const auto ratio = travel <= 0.0F ? 0.0F : std::clamp(coordinate / travel, 0.0F, 1.0F);
    if (is_scroll_container()) {
        const auto maximum = max_scroll_offset();
        return ratio * (orientation == ScrollbarOrientation::Vertical ? maximum.y : maximum.x);
    }
    return minimum_ + (maximum_ - minimum_) * ratio;
}

float Scrollbar::clamped_value(float value) const noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum_, maximum_) : minimum_;
}

bool Scrollbar::is_scroll_container() const noexcept {
    return container_mode_;
}

void Scrollbar::set_container_scroll_axis(ScrollbarOrientation orientation, float value) {
    const auto previous_scroll = scroll_offset();
    auto next_scroll = previous_scroll;
    if (orientation == ScrollbarOrientation::Vertical) {
        next_scroll.y = value;
    } else {
        next_scroll.x = value;
    }
    set_scroll_offset(next_scroll);
    emit_container_scroll_changed(previous_scroll);
}

void Scrollbar::emit_container_scroll_changed(layout::Point previous_scroll) {
    const auto current_scroll = scroll_offset();
    if (current_scroll.x == previous_scroll.x && current_scroll.y == previous_scroll.y) {
        return;
    }

    sync_container_range_if_needed();
    if (event_state_ != nullptr && !event_state_->scrolled.empty()) {
        event_state_->scrolled.emit(orientation_ == ScrollbarOrientation::Vertical
                                        ? current_scroll.y
                                        : current_scroll.x);
    }
    if (event_state_ != nullptr && !event_state_->scroll_data_changed.empty()) {
        event_state_->scroll_data_changed.emit(
            ScrollbarScrollData{.scroll_left = current_scroll.x, .scroll_top = current_scroll.y});
    }
    const auto maximum = max_scroll_offset();
    notify_end_reached(ScrollbarOrientation::Vertical, current_scroll.y, maximum.y);
    notify_end_reached(ScrollbarOrientation::Horizontal, current_scroll.x, maximum.x);
}

void Scrollbar::notify_end_reached() {
    if ((event_state_ == nullptr || event_state_->end_reached.empty()) || maximum_ <= minimum_) {
        return;
    }

    notify_end_reached(orientation_, value_ - minimum_, maximum_ - minimum_);
}

void Scrollbar::notify_end_reached(ScrollbarOrientation orientation, float value, float maximum) {
    if ((event_state_ == nullptr || event_state_->end_reached.empty()) || maximum <= 0.0F) {
        return;
    }

    const auto threshold = std::max(distance_, 0.0F);
    const auto at_min = value <= threshold;
    const auto at_max = value >= maximum - threshold;
    auto& min_latch =
        orientation == ScrollbarOrientation::Vertical ? top_end_reported_ : left_end_reported_;
    auto& max_latch =
        orientation == ScrollbarOrientation::Vertical ? bottom_end_reported_ : right_end_reported_;
    if (!at_min) {
        min_latch = false;
    }
    if (!at_max) {
        max_latch = false;
    }
    if (at_min && !min_latch) {
        min_latch = true;
        event_state_->end_reached.emit(orientation == ScrollbarOrientation::Vertical
                                           ? ScrollbarEndDirection::Top
                                           : ScrollbarEndDirection::Left);
    }
    if (at_max && !max_latch) {
        max_latch = true;
        event_state_->end_reached.emit(orientation == ScrollbarOrientation::Vertical
                                           ? ScrollbarEndDirection::Bottom
                                           : ScrollbarEndDirection::Right);
    }
}

void Scrollbar::reset_end_reached_latches() {
    top_end_reported_ = false;
    right_end_reported_ = false;
    bottom_end_reported_ = false;
    left_end_reported_ = false;
}

} // namespace winelement::controls
