#pragma once

#include <winelement/elements/focus_manager.hpp>
#include <winelement/elements/input_event.hpp>
#include <winelement/elements/event_routing_policy.hpp>
#include <winelement/elements/ui_element.hpp>

#include <array>
#include <cstdint>

namespace winelement::elements {

class EventRouter final {
  public:
    explicit EventRouter(UIElement& root);
    ~EventRouter() noexcept;

    [[nodiscard]] UIElement& root() noexcept;
    [[nodiscard]] const UIElement& root() const noexcept;
    [[nodiscard]] FocusManager& focus_manager() noexcept;
    [[nodiscard]] const FocusManager& focus_manager() const noexcept;
    [[nodiscard]] GestureArena& gesture_arena() noexcept;
    [[nodiscard]] const GestureArena& gesture_arena() const noexcept;
    [[nodiscard]] UIElement* pointer_capture() noexcept;
    [[nodiscard]] const UIElement* pointer_capture() const noexcept;
    [[nodiscard]] UIElement* text_selection_owner() noexcept;
    [[nodiscard]] const UIElement* text_selection_owner() const noexcept;

    RoutedEventResult route_pointer_event(PointerEvent event);
    RoutedEventResult route_key_event(KeyEvent event);
    [[nodiscard]] PointerCursor cursor_for_point(layout::Point position);
    bool capture_pointer(UIElement& element);
    void release_pointer_capture(UIElement* owner = nullptr) noexcept;

  private:
    friend class UIElement;
    friend class PopupManager;

    [[nodiscard]] UIElement* pointer_target(layout::Point position);
    [[nodiscard]] UIElement* top_layer_target(layout::Point position);
    [[nodiscard]] UIElement* visual_tree_target(layout::Point position);
    [[nodiscard]] UIElement* nearest_focus_target(UIElement* target) noexcept;
    [[nodiscard]] UIElement* pressed_target(PointerButton button) noexcept;
    void set_pressed_target(PointerButton button, UIElement* target) noexcept;
    void clear_pressed_targets() noexcept;
    void set_text_selection_owner(UIElement* element) noexcept;
    void on_element_detaching(UIElement& element) noexcept;
    [[nodiscard]] bool dispatch_active() const noexcept;
    [[nodiscard]] RoutedEventResult update_hover_target(UIElement* target, PointerEvent event);
    [[nodiscard]] layout::Point local_position_for(const UIElement& element,
                                                   layout::Point position) const noexcept;
    [[nodiscard]] RoutedEventResult dispatch_pointer_event(UIElement& target, PointerEvent event);
    [[nodiscard]] RoutedEventResult dispatch_key_event(UIElement& target, KeyEvent event);

    UIElement* root_ = nullptr;
    FocusManager focus_manager_;
    EventRoutingPolicy routing_policy_;
    GestureArena gesture_arena_;
    UIElement* pointer_capture_ = nullptr;
    UIElement* text_selection_owner_ = nullptr;
    UIElement* hover_target_ = nullptr;
    std::array<UIElement*, 5> pressed_targets_{};
    std::uint32_t dispatch_depth_ = 0;
};

} // namespace winelement::elements
