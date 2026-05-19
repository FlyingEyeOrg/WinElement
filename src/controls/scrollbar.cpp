#include <winelement/controls/scrollbar.hpp>

#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace winelement::controls {

namespace {

constexpr auto element_scrollbar_edge_inset = 2.0F;
constexpr auto element_scrollbar_min_hit_cross_extent = 12.0F;

[[nodiscard]] std::uint8_t scaled_alpha(std::uint8_t alpha, float opacity) noexcept {
    return static_cast<std::uint8_t>(std::clamp(
        std::round(static_cast<float>(alpha) * std::clamp(opacity, 0.0F, 1.0F)), 0.0F, 255.0F));
}

} // namespace

Scrollbar::Scrollbar() : Control() {
    apply_style_value(style::default_scrollbar_style(), true);
    set_theme_class(style::theme_class::scrollbar);
    set_focusable(true);
    update_measure_callback();
}

Scrollbar& Scrollbar::set_orientation(ScrollbarOrientation orientation) {
    if (orientation_ == orientation) {
        return *this;
    }

    orientation_ = orientation;
    update_measure_callback();
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Scrollbar& Scrollbar::set_visibility_mode(ScrollbarVisibility visibility) noexcept {
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
    if (scroll_handler_) {
        scroll_handler_(value_);
    }
    if (scroll_data_handler_) {
        scroll_data_handler_(orientation_ == ScrollbarOrientation::Vertical
                                 ? ScrollbarScrollData{.scroll_top = value_}
                                 : ScrollbarScrollData{.scroll_left = value_});
    }
    notify_end_reached();
    return *this;
}

Scrollbar& Scrollbar::set_thickness(float thickness) {
    thickness_ = std::isfinite(thickness) ? std::max(thickness, 1.0F) : 6.0F;
    update_measure_callback();
    mark_measure_dirty();
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

Scrollbar& Scrollbar::set_distance(float distance) noexcept {
    distance_ = std::isfinite(distance) ? std::max(distance, 0.0F) : 0.0F;
    reset_end_reached_latches();
    return *this;
}

Scrollbar& Scrollbar::set_track_click_enabled(bool enabled) noexcept {
    track_click_enabled_ = enabled;
    return *this;
}

Scrollbar& Scrollbar::bind_range(RangeProvider provider) {
    range_provider_ = std::move(provider);
    return refresh_bound_range();
}

Scrollbar& Scrollbar::refresh_bound_range() {
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
    if (!range_provider_) {
        return;
    }
    refresh_bound_range();
}

Scrollbar& Scrollbar::scroll_to(float scroll_left, float scroll_top) {
    return orientation_ == ScrollbarOrientation::Vertical ? set_scroll_top(scroll_top)
                                                          : set_scroll_left(scroll_left);
}

Scrollbar& Scrollbar::set_scroll_top(float scroll_top) {
    if (orientation_ == ScrollbarOrientation::Vertical) {
        set_value(scroll_top);
    }
    return *this;
}

Scrollbar& Scrollbar::set_scroll_left(float scroll_left) {
    if (orientation_ == ScrollbarOrientation::Horizontal) {
        set_value(scroll_left);
    }
    return *this;
}

Scrollbar& Scrollbar::set_on_scroll(ScrollHandler handler) {
    scroll_handler_ = std::move(handler);
    return *this;
}

Scrollbar& Scrollbar::set_on_scroll_data(ScrollDataHandler handler) {
    scroll_data_handler_ = std::move(handler);
    return *this;
}

Scrollbar& Scrollbar::set_on_end_reached(EndReachedHandler handler) {
    end_reached_handler_ = std::move(handler);
    reset_end_reached_latches();
    return *this;
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

float Scrollbar::distance() const noexcept {
    return distance_;
}

bool Scrollbar::track_click_enabled() const noexcept {
    return track_click_enabled_;
}

void Scrollbar::on_pointer_event(elements::PointerEvent& event) {
    sync_bound_range_if_needed();
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

void Scrollbar::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    const_cast<Scrollbar*>(this)->sync_bound_range_if_needed();
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

void Scrollbar::update_measure_callback() {
    set_measure_callback([this](const layout::MeasureInput&) {
        return orientation_ == ScrollbarOrientation::Vertical ? layout::Size{thickness_, 120.0F}
                                                              : layout::Size{120.0F, thickness_};
    });
}

layout::Rect Scrollbar::track_rect(layout::Rect frame) const noexcept {
    const auto cross_extent =
        orientation_ == ScrollbarOrientation::Vertical ? frame.width : frame.height;
    const auto axis_length =
        orientation_ == ScrollbarOrientation::Vertical ? frame.height : frame.width;
    const auto inset = std::min(element_scrollbar_edge_inset, std::max(axis_length, 0.0F) * 0.5F);
    if (orientation_ == ScrollbarOrientation::Vertical) {
        const auto width = std::min(std::max(cross_extent, 0.0F), thickness_);
        const auto x = frame.x + std::max(0.0F, frame.width - width) * 0.5F;
        return layout::Rect{x, frame.y + inset, width, std::max(0.0F, frame.height - inset * 2.0F)};
    }
    const auto height = std::min(std::max(cross_extent, 0.0F), thickness_);
    const auto y = frame.y + std::max(0.0F, frame.height - height) * 0.5F;
    return layout::Rect{frame.x + inset, y, std::max(0.0F, frame.width - inset * 2.0F), height};
}

layout::Rect Scrollbar::thumb_rect(layout::Rect frame) const noexcept {
    const auto track = track_rect(frame);
    const auto range = std::max(maximum_ - minimum_, 0.0F);
    const auto track_extent = axis_extent(track);
    const auto cross_extent =
        orientation_ == ScrollbarOrientation::Vertical ? track.width : track.height;
    const auto thumb_extent = range <= 0.0F
                                  ? track_extent
                                  : std::clamp(track_extent * page_size_ / (range + page_size_),
                                               min_thumb_extent_, track_extent);
    const auto travel = std::max(track_extent - thumb_extent, 0.0F);
    const auto ratio = range <= 0.0F ? 0.0F : (value_ - minimum_) / range;
    const auto offset = travel * std::clamp(ratio, 0.0F, 1.0F);
    if (orientation_ == ScrollbarOrientation::Vertical) {
        return layout::Rect{track.x, track.y + offset, cross_extent, thumb_extent};
    }
    return layout::Rect{track.x + offset, track.y, thumb_extent, cross_extent};
}

layout::Rect Scrollbar::thumb_hit_rect(layout::Rect frame) const noexcept {
    auto hit_rect = thumb_rect(frame);
    if (orientation_ == ScrollbarOrientation::Vertical) {
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
    return orientation_ == ScrollbarOrientation::Vertical ? point.y : point.x;
}

float Scrollbar::axis_origin(layout::Rect rect) const noexcept {
    return orientation_ == ScrollbarOrientation::Vertical ? rect.y : rect.x;
}

float Scrollbar::axis_extent(layout::Rect rect) const noexcept {
    return orientation_ == ScrollbarOrientation::Vertical ? rect.height : rect.width;
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
    const auto frame_rect = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    const auto track = track_rect(frame_rect);
    const auto thumb = thumb_rect(frame_rect);
    const auto travel = std::max(axis_extent(track) - axis_extent(thumb), 0.0F);
    const auto coordinate = axis_coordinate(point) - axis_origin(track) - drag_thumb_offset_;
    const auto ratio = travel <= 0.0F ? 0.0F : std::clamp(coordinate / travel, 0.0F, 1.0F);
    return minimum_ + (maximum_ - minimum_) * ratio;
}

float Scrollbar::clamped_value(float value) const noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum_, maximum_) : minimum_;
}

void Scrollbar::notify_end_reached() {
    if (!end_reached_handler_ || maximum_ <= minimum_) {
        return;
    }

    const auto threshold = std::max(distance_, 0.0F);
    const auto at_min = value_ <= minimum_ + threshold;
    const auto at_max = value_ >= maximum_ - threshold;
    if (!at_min) {
        min_end_reported_ = false;
    }
    if (!at_max) {
        max_end_reported_ = false;
    }
    if (at_min && !min_end_reported_) {
        min_end_reported_ = true;
        end_reached_handler_(orientation_ == ScrollbarOrientation::Vertical
                                 ? ScrollbarEndDirection::Top
                                 : ScrollbarEndDirection::Left);
    }
    if (at_max && !max_end_reported_) {
        max_end_reported_ = true;
        end_reached_handler_(orientation_ == ScrollbarOrientation::Vertical
                                 ? ScrollbarEndDirection::Bottom
                                 : ScrollbarEndDirection::Right);
    }
}

void Scrollbar::reset_end_reached_latches() {
    min_end_reported_ = false;
    max_end_reported_ = false;
}

} // namespace winelement::controls
