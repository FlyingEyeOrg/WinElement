#pragma once

#include <winelement/animation.hpp>
#include <winelement/controls/control.hpp>
#include <winelement/controls/control_animation.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <functional>
#include <memory>

namespace winelement::controls {

enum class ScrollbarOrientation { Vertical, Horizontal };

enum class ScrollbarVisibility { Auto, Always, Never };

enum class ScrollbarEndDirection { Top, Right, Bottom, Left };

struct ScrollbarRange {
    float minimum = 0.0F;
    float maximum = 100.0F;
    float page_size = 10.0F;
    float value = 0.0F;
};

struct ScrollbarScrollData {
    float scroll_left = 0.0F;
    float scroll_top = 0.0F;
};

class Scrollbar final : public Control {
  public:
    using RangeProvider = std::function<ScrollbarRange()>;
    using ScrollEventSignal = core::EventSignal<float>;
    using ScrollDataEventSignal = core::EventSignal<ScrollbarScrollData>;
    using EndReachedEventSignal = core::EventSignal<ScrollbarEndDirection>;

    Scrollbar();
    ~Scrollbar() override;

    Scrollbar& set_orientation(ScrollbarOrientation orientation);
    Scrollbar& set_disabled(bool disabled) noexcept;
    Scrollbar& set_visibility_mode(ScrollbarVisibility visibility) noexcept;
    Scrollbar& set_range(float minimum, float maximum, float page_size);
    Scrollbar& set_value(float value);
    Scrollbar& set_thickness(float thickness);
    Scrollbar& set_min_thumb_extent(float extent);
    Scrollbar& set_min_size(float extent);
    Scrollbar& set_always_visible(bool always) noexcept;
    Scrollbar& set_noresize(bool noresize) noexcept;
    Scrollbar& set_distance(float distance) noexcept;
    Scrollbar& set_track_click_enabled(bool enabled) noexcept;
    Scrollbar& set_container_mode(bool enabled = true);
    Scrollbar& set_height(float height);
    Scrollbar& set_max_height(float max_height);
    Scrollbar& set_content(std::unique_ptr<elements::UIElement> content);
    Scrollbar& bind_range(RangeProvider provider);
    Scrollbar& refresh_bound_range();
    Scrollbar& update();
    Scrollbar& scroll_to(float scroll_left, float scroll_top);
    Scrollbar& set_scroll_top(float scroll_top);
    Scrollbar& set_scroll_left(float scroll_left);
    [[nodiscard]] ScrollEventSignal& scrolled() noexcept;
    [[nodiscard]] ScrollDataEventSignal& scroll_data_changed() noexcept;
    [[nodiscard]] EndReachedEventSignal& end_reached() noexcept;
    Scrollbar& set_style(style::UIElementStyle style) override;
    [[nodiscard]] ScrollbarOrientation orientation() const noexcept;
    [[nodiscard]] ScrollbarVisibility visibility_mode() const noexcept;
    [[nodiscard]] float minimum() const noexcept;
    [[nodiscard]] float maximum() const noexcept;
    [[nodiscard]] float page_size() const noexcept;
    [[nodiscard]] float value() const noexcept;
    [[nodiscard]] float thickness() const noexcept;
    [[nodiscard]] float min_thumb_extent() const noexcept;
    [[nodiscard]] float min_size() const noexcept;
    [[nodiscard]] bool noresize() const noexcept;
    [[nodiscard]] float distance() const noexcept;
    [[nodiscard]] bool track_click_enabled() const noexcept;
    [[nodiscard]] bool container_mode() const noexcept;

  protected:
    void on_pointer_event(elements::PointerEvent& event) override;
    void on_key_event(elements::KeyEvent& event) override;
    [[nodiscard]] elements::PointerCursor
    cursor_for_local_point(layout::Point local_position) const noexcept override;
    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override;
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;
    void on_paint_overlay(rendering::RenderContext& context,
                          layout::Rect absolute_frame) const override;

  private:
    void sync_bound_range_if_needed();
    void sync_container_range_if_needed();
    void update_measure_callback();
    void reset_interaction_state();
    [[nodiscard]] layout::Rect track_rect(layout::Rect frame) const noexcept;
    [[nodiscard]] layout::Rect track_rect(layout::Rect frame,
                                          ScrollbarOrientation orientation) const noexcept;
    [[nodiscard]] layout::Rect thumb_rect(layout::Rect frame) const noexcept;
    [[nodiscard]] layout::Rect thumb_rect(layout::Rect frame,
                                          ScrollbarOrientation orientation) const noexcept;
    [[nodiscard]] layout::Rect thumb_hit_rect(layout::Rect frame) const noexcept;
    [[nodiscard]] layout::Rect thumb_hit_rect(layout::Rect frame,
                                              ScrollbarOrientation orientation) const noexcept;
    [[nodiscard]] float axis_coordinate(layout::Point point) const noexcept;
    [[nodiscard]] float axis_coordinate(layout::Point point,
                                        ScrollbarOrientation orientation) const noexcept;
    [[nodiscard]] float axis_origin(layout::Rect rect) const noexcept;
    [[nodiscard]] float axis_origin(layout::Rect rect,
                                    ScrollbarOrientation orientation) const noexcept;
    [[nodiscard]] float axis_extent(layout::Rect rect) const noexcept;
    [[nodiscard]] float axis_extent(layout::Rect rect,
                                    ScrollbarOrientation orientation) const noexcept;
    [[nodiscard]] float animated_hover_progress() const;
    [[nodiscard]] float animated_drag_progress() const;
    void animate_hover(float target);
    void animate_drag(float target);
    [[nodiscard]] float value_for_local_point(layout::Point point) const noexcept;
    [[nodiscard]] float value_for_local_point(layout::Point point,
                                              ScrollbarOrientation orientation) const noexcept;
    [[nodiscard]] float clamped_value(float value) const noexcept;
    [[nodiscard]] bool is_scroll_container() const noexcept;
    void set_container_scroll_axis(ScrollbarOrientation orientation, float value);
    void emit_container_scroll_changed(layout::Point previous_scroll);
    void notify_end_reached();
    void notify_end_reached(ScrollbarOrientation orientation, float value, float maximum);
    void reset_end_reached_latches();

    struct EventState;
    [[nodiscard]] EventState& ensure_event_state();

    RangeProvider range_provider_;
    ScrollbarOrientation orientation_ = ScrollbarOrientation::Vertical;
    ScrollbarVisibility visibility_ = ScrollbarVisibility::Auto;
    float minimum_ = 0.0F;
    float maximum_ = 100.0F;
    float page_size_ = 10.0F;
    float value_ = 0.0F;
    float thickness_ = 6.0F;
    float min_thumb_extent_ = 20.0F;
    float distance_ = 0.0F;
    float drag_thumb_offset_ = 0.0F;
    bool dragging_ = false;
    bool container_mode_ = false;
    bool noresize_ = false;
    bool track_click_enabled_ = true;
    ScrollbarOrientation dragging_orientation_ = ScrollbarOrientation::Vertical;
    bool top_end_reported_ = false;
    bool right_end_reported_ = false;
    bool bottom_end_reported_ = false;
    bool left_end_reported_ = false;
    std::unique_ptr<EventState> event_state_;
    AnimatedFloat hover_progress_{0.0F};
    AnimatedFloat drag_progress_{0.0F};
};

} // namespace winelement::controls
