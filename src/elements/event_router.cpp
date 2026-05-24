#include <winelement/elements/event_router.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace winelement::elements {
namespace {

[[nodiscard]] layout::Point to_local_point(layout::Point absolute_point,
                                           layout::Rect absolute_frame) noexcept {
    return layout::Point{absolute_point.x - absolute_frame.x, absolute_point.y - absolute_frame.y};
}

[[nodiscard]] bool is_in_tree(const UIElement& root, const UIElement& element) noexcept {
    for (const auto* current = &element; current != nullptr; current = current->parent()) {
        if (current == &root) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool is_pointer_down(PointerEventKind kind) noexcept {
    return kind == PointerEventKind::Down || kind == PointerEventKind::DoubleClick;
}

[[nodiscard]] bool is_pointer_up(PointerEventKind kind) noexcept {
    return kind == PointerEventKind::Up;
}

[[nodiscard]] bool is_focus_activating_pointer_event(PointerEventKind kind) noexcept {
    return kind == PointerEventKind::Down || kind == PointerEventKind::DoubleClick;
}

[[nodiscard]] RoutedEventType routed_event_type(PointerEventKind kind) noexcept {
    switch (kind) {
    case PointerEventKind::Move:
        return RoutedEventType::PointerMove;
    case PointerEventKind::Down:
        return RoutedEventType::PointerDown;
    case PointerEventKind::Up:
        return RoutedEventType::PointerUp;
    case PointerEventKind::Click:
        return RoutedEventType::PointerClick;
    case PointerEventKind::DoubleClick:
        return RoutedEventType::PointerDoubleClick;
    case PointerEventKind::Wheel:
        return RoutedEventType::PointerWheel;
    case PointerEventKind::HorizontalWheel:
        return RoutedEventType::PointerHorizontalWheel;
    case PointerEventKind::Cancel:
        return RoutedEventType::PointerCancel;
    case PointerEventKind::Enter:
        return RoutedEventType::PointerEnter;
    case PointerEventKind::Leave:
        return RoutedEventType::PointerLeave;
    }
    return RoutedEventType::PointerMove;
}

[[nodiscard]] RoutedEventType routed_event_type(KeyEventKind kind) noexcept {
    switch (kind) {
    case KeyEventKind::Down:
        return RoutedEventType::KeyDown;
    case KeyEventKind::Up:
        return RoutedEventType::KeyUp;
    case KeyEventKind::TextInput:
        return RoutedEventType::KeyTextInput;
    case KeyEventKind::CompositionStart:
        return RoutedEventType::KeyCompositionStart;
    case KeyEventKind::CompositionUpdate:
        return RoutedEventType::KeyCompositionUpdate;
    case KeyEventKind::CompositionEnd:
        return RoutedEventType::KeyCompositionEnd;
    }
    return RoutedEventType::KeyDown;
}

[[nodiscard]] std::size_t pointer_button_index(PointerButton button) noexcept {
    switch (button) {
    case PointerButton::Primary:
        return 0;
    case PointerButton::Secondary:
        return 1;
    case PointerButton::Middle:
        return 2;
    case PointerButton::X1:
        return 3;
    case PointerButton::X2:
        return 4;
    case PointerButton::None:
        return 5;
    }

    return 5;
}

[[nodiscard]] bool is_trackable_button(PointerButton button) noexcept {
    return pointer_button_index(button) < 5U;
}

[[nodiscard]] RoutedEventResult merge_pointer_results(RoutedEventResult first,
                                                      RoutedEventResult second) noexcept {
    if (!first.handled && first.target == nullptr && second.target != nullptr) {
        return second;
    }

    if (!first.handled && second.handled) {
        return second;
    }

    return first;
}

} // namespace

EventRouter::EventRouter(UIElement& root) : root_(&root), focus_manager_(root) {
    root_->attach_event_router(*this);
}

EventRouter::~EventRouter() noexcept {
    if (root_ != nullptr) {
        root_->detach_event_router(*this);
    }
}

UIElement& EventRouter::root() noexcept {
    return *root_;
}

const UIElement& EventRouter::root() const noexcept {
    return *root_;
}

FocusManager& EventRouter::focus_manager() noexcept {
    return focus_manager_;
}

const FocusManager& EventRouter::focus_manager() const noexcept {
    return focus_manager_;
}

GestureArena& EventRouter::gesture_arena() noexcept {
    return gesture_arena_;
}

const GestureArena& EventRouter::gesture_arena() const noexcept {
    return gesture_arena_;
}

UIElement* EventRouter::pointer_capture() noexcept {
    return pointer_capture_;
}

const UIElement* EventRouter::pointer_capture() const noexcept {
    return pointer_capture_;
}

UIElement* EventRouter::text_selection_owner() noexcept {
    return text_selection_owner_;
}

const UIElement* EventRouter::text_selection_owner() const noexcept {
    return text_selection_owner_;
}

RoutedEventResult EventRouter::route_pointer_event(PointerEvent event) {
    const auto outermost_dispatch = dispatch_depth_ == 0U;
    ++dispatch_depth_;
    auto result = [&]() -> RoutedEventResult {
        // Browser-level backdrop/outside click: auto-dismiss light-dismiss top layer entries.
        if (is_pointer_down(event.kind) && root_ != nullptr) {
            if (root_->light_dismiss_outside(event.position)) {
                return RoutedEventResult{.target = nullptr,
                                         .handled_by = nullptr,
                                         .handled_phase = EventRoutePhase::Bubble,
                                         .handled = true};
            }
        }

        if (event.kind == PointerEventKind::Leave) {
            return update_hover_target(nullptr, event);
        }

        auto* target = pointer_target(event.position);
        const auto hover_result =
            event.kind == PointerEventKind::Move || event.kind == PointerEventKind::Enter
                ? update_hover_target(target, event)
                : RoutedEventResult{};
        if (event.kind == PointerEventKind::Enter) {
            return hover_result;
        }

        if (target == nullptr) {
            if (is_focus_activating_pointer_event(event.kind)) {
                focus_manager_.clear_focus();
            }

            if (is_pointer_up(event.kind)) {
                set_pressed_target(event.button, nullptr);
            }

            if (event.kind == PointerEventKind::Cancel) {
                clear_pressed_targets();
                release_pointer_capture();
            }

            return hover_result;
        }

        if (is_focus_activating_pointer_event(event.kind)) {
            auto* focus_target = nearest_focus_target(target);
            if (!root_->top_layer_entry_preserves_focus_for(*target)) {
                focus_manager_.set_focus(focus_target, false);
            }
        }

        if (event.kind == PointerEventKind::Cancel) {
            clear_pressed_targets();
            release_pointer_capture();
        }

        if (is_pointer_down(event.kind) && is_trackable_button(event.button)) {
            set_pressed_target(event.button, target);
        }

        if (!is_pointer_up(event.kind) || !is_trackable_button(event.button)) {
            return merge_pointer_results(hover_result, dispatch_pointer_event(*target, event));
        }

        auto* click_target = pressed_target(event.button);
        const auto up_result = dispatch_pointer_event(*target, event);
        set_pressed_target(event.button, nullptr);

        const auto* hit_target = visual_tree_target(event.position);
        if (click_target == nullptr || !is_in_tree(*root_, *click_target) ||
            hit_target == nullptr || !click_target->contains(*hit_target)) {
            return merge_pointer_results(hover_result, up_result);
        }

        auto click_event = event;
        click_event.kind = PointerEventKind::Click;
        click_event.click_count = click_event.click_count == 0 ? 1 : click_event.click_count;
        click_event.handled = false;
        click_event.target = nullptr;
        click_event.current_target = nullptr;
        const auto click_result = dispatch_pointer_event(*click_target, click_event);
        return merge_pointer_results(hover_result, merge_pointer_results(up_result, click_result));
    }();

    gesture_arena_.route_pointer_event(event);

    --dispatch_depth_;
    if (outermost_dispatch && root_ != nullptr) {
        root_->sanitize_pending_top_layer_result(result);
        root_->flush_pending_top_layer_removals();
    }
    return result;
}

RoutedEventResult EventRouter::route_key_event(KeyEvent event) {
    const auto outermost_dispatch = dispatch_depth_ == 0U;
    ++dispatch_depth_;
    auto result = [&]() {
        if (routing_policy_.routes_to_top_layer(event)) {
            if (auto* top_layer_target = root_->top_layer_key_target()) {
                auto top_layer_result = dispatch_key_event(*top_layer_target, event);
                if (top_layer_result.handled) {
                    return top_layer_result;
                }
            }
        }

        auto* target = focus_manager_.focused_element();
        RoutedEventResult key_result;
        if (target != nullptr) {
            key_result = dispatch_key_event(*target, event);
        }

        if (!key_result.handled && routing_policy_.is_text_selection_shortcut(event) &&
            text_selection_owner_ != nullptr) {
            if (!is_in_tree(*root_, *text_selection_owner_) ||
                !text_selection_owner_->has_text_selection()) {
                text_selection_owner_ = nullptr;
            } else {
                key_result = dispatch_key_event(*text_selection_owner_, event);
            }
        }

        // Browser-level Escape: auto-dismiss the topmost light-dismiss or backdrop top layer entry.
        if (!key_result.handled && routing_policy_.dismisses_top_layer(event) && root_ != nullptr) {
            root_->dismiss_topmost_on_escape();
        }

        if (!key_result.handled && routing_policy_.moves_focus(event)) {
            const auto moved_focus = event.modifiers.shift ? focus_manager_.focus_previous()
                                                           : focus_manager_.focus_next();
            key_result.handled = moved_focus;
            key_result.handled_by = moved_focus ? focus_manager_.focused_element() : nullptr;
        }

        return key_result;
    }();

    --dispatch_depth_;
    if (outermost_dispatch && root_ != nullptr) {
        root_->sanitize_pending_top_layer_result(result);
        root_->flush_pending_top_layer_removals();
    }
    return result;
}

PointerCursor EventRouter::cursor_for_point(layout::Point position) {
    auto* target = pointer_capture_ != nullptr && is_in_tree(*root_, *pointer_capture_)
                       ? pointer_capture_
                       : visual_tree_target(position);
    while (target != nullptr && is_in_tree(*root_, *target)) {
        const auto cursor = target->cursor_for_local_point(local_position_for(*target, position));
        if (cursor != PointerCursor::Default) {
            return cursor;
        }
        target = target->parent_;
    }
    return PointerCursor::Arrow;
}

bool EventRouter::capture_pointer(UIElement& element) {
    if (!is_in_tree(*root_, element)) {
        return false;
    }

    pointer_capture_ = &element;
    return true;
}

void EventRouter::release_pointer_capture(UIElement* owner) noexcept {
    if (owner == nullptr || pointer_capture_ == owner) {
        pointer_capture_ = nullptr;
    }
}

void EventRouter::on_element_detaching(UIElement& element) noexcept {
    if (pointer_capture_ != nullptr && element.contains(*pointer_capture_)) {
        pointer_capture_ = nullptr;
    }

    if (text_selection_owner_ != nullptr && element.contains(*text_selection_owner_)) {
        text_selection_owner_ = nullptr;
    }

    if (hover_target_ != nullptr && element.contains(*hover_target_)) {
        hover_target_ = nullptr;
    }

    for (auto& pressed_target : pressed_targets_) {
        if (pressed_target != nullptr && element.contains(*pressed_target)) {
            pressed_target = nullptr;
        }
    }
}

UIElement* EventRouter::pointer_target(layout::Point position) {
    if (pointer_capture_ != nullptr) {
        if (is_in_tree(*root_, *pointer_capture_)) {
            return pointer_capture_;
        }

        pointer_capture_ = nullptr;
    }

    if (auto* target = top_layer_target(position)) {
        return target;
    }

    return root_->hit_test(position);
}

UIElement* EventRouter::top_layer_target(layout::Point position) {
    return root_->top_layer_pointer_target(position);
}

UIElement* EventRouter::visual_tree_target(layout::Point position) {
    if (auto* target = top_layer_target(position)) {
        return target;
    }

    return root_->hit_test(position);
}

UIElement* EventRouter::nearest_focus_target(UIElement* target) noexcept {
    for (auto* current = target; current != nullptr; current = current->logical_parent()) {
        if (current->can_receive_focus()) {
            return current;
        }
    }

    return nullptr;
}

UIElement* EventRouter::pressed_target(PointerButton button) noexcept {
    const auto index = pointer_button_index(button);
    return index < pressed_targets_.size() ? pressed_targets_[index] : nullptr;
}

void EventRouter::set_pressed_target(PointerButton button, UIElement* target) noexcept {
    const auto index = pointer_button_index(button);
    if (index < pressed_targets_.size()) {
        pressed_targets_[index] = target;
    }
}

void EventRouter::clear_pressed_targets() noexcept {
    pressed_targets_.fill(nullptr);
}

void EventRouter::set_text_selection_owner(UIElement* element) noexcept {
    if (element != nullptr && !is_in_tree(*root_, *element)) {
        return;
    }

    text_selection_owner_ = element;
}

bool EventRouter::dispatch_active() const noexcept {
    return dispatch_depth_ > 0U;
}

RoutedEventResult EventRouter::update_hover_target(UIElement* target, PointerEvent event) {
    if (target != nullptr && !is_in_tree(*root_, *target)) {
        target = nullptr;
    }

    if (target != nullptr && target->disabled_) {
        target = nullptr;
    }

    if (hover_target_ != nullptr && !is_in_tree(*root_, *hover_target_)) {
        hover_target_ = nullptr;
    }
    if (hover_target_ == target) {
        return {};
    }

    auto result = RoutedEventResult{};
    if (hover_target_ != nullptr) {
        auto leave_event = event;
        leave_event.kind = PointerEventKind::Leave;
        leave_event.button = PointerButton::None;
        leave_event.handled = false;
        result = dispatch_pointer_event(*hover_target_, leave_event);
    }

    hover_target_ = target;
    if (hover_target_ != nullptr) {
        auto enter_event = event;
        enter_event.kind = PointerEventKind::Enter;
        enter_event.button = PointerButton::None;
        enter_event.handled = false;
        result = merge_pointer_results(result, dispatch_pointer_event(*hover_target_, enter_event));
    }
    return result;
}

layout::Point EventRouter::local_position_for(const UIElement& element,
                                              layout::Point position) const noexcept {
    auto mapped_position = position;

    const auto apply_from_root = [&mapped_position](const UIElement& current,
                                                    const auto& self) noexcept -> bool {
        if (const auto* parent = current.parent(); parent != nullptr && !self(*parent, self)) {
            return false;
        }

        const auto mapped = current.map_point_to_untransformed_space(mapped_position);
        if (!mapped) {
            return false;
        }
        mapped_position = *mapped;
        return true;
    };

    static_cast<void>(apply_from_root(element, apply_from_root));

    return to_local_point(mapped_position, element.absolute_frame());
}

RoutedEventResult EventRouter::dispatch_pointer_event(UIElement& target, PointerEvent event) {
    RoutedEventResult result{.target = &target};
    event.target = &target;

    auto route = std::vector<UIElement*>{};
    auto depth = std::size_t{0};
    for (auto* current = &target; current != nullptr; current = current->parent_) {
        ++depth;
    }
    route.reserve(depth);
    for (auto* current = &target; current != nullptr; current = current->parent_) {
        route.push_back(current);
    }

    auto local_positions = std::vector<layout::Point>(route.size());
    auto mapped_position = event.position;
    for (auto index = route.size(); index > 0U; --index) {
        auto* current = route[index - 1U];
        if (const auto mapped = current->map_point_to_untransformed_space(mapped_position)) {
            mapped_position = *mapped;
        }
        local_positions[index - 1U] = to_local_point(mapped_position, current->absolute_frame());
    }

    event.phase = EventRoutePhase::Tunnel;
    for (auto index = route.size(); index > 0U; --index) {
        auto* current = route[index - 1U];
        event.current_target = current;
        event.local_position = local_positions[index - 1U];
        auto context = RoutedEventFilterContext{.type = routed_event_type(event.kind),
                                                .phase = EventRoutePhase::Tunnel,
                                                .target = &target,
                                                .current_target = current,
                                                .pointer_event = &event};
        current->dispatch_routed_event_filter(context);
        if (event.handled) {
            result.handled = true;
            result.handled_by = current;
            result.handled_phase = EventRoutePhase::Tunnel;
            return result;
        }
        current->on_pointer_tunnel_event(event);
        if (event.handled) {
            result.handled = true;
            result.handled_by = current;
            result.handled_phase = EventRoutePhase::Tunnel;
            return result;
        }
    }

    event.phase = EventRoutePhase::Bubble;
    for (std::size_t index = 0; index < route.size(); ++index) {
        auto* current = route[index];
        event.current_target = current;
        event.local_position = local_positions[index];
        auto context = RoutedEventFilterContext{.type = routed_event_type(event.kind),
                                                .phase = EventRoutePhase::Bubble,
                                                .target = &target,
                                                .current_target = current,
                                                .pointer_event = &event};
        current->dispatch_routed_event_filter(context);
        if (event.handled) {
            result.handled = true;
            result.handled_by = current;
            result.handled_phase = EventRoutePhase::Bubble;
            return result;
        }
        current->on_pointer_event(event);
        if (event.handled) {
            result.handled = true;
            result.handled_by = current;
            result.handled_phase = EventRoutePhase::Bubble;
            return result;
        }
    }

    return result;
}

RoutedEventResult EventRouter::dispatch_key_event(UIElement& target, KeyEvent event) {
    RoutedEventResult result{.target = &target};
    event.target = &target;

    auto route = std::vector<UIElement*>{};
    for (auto* current = &target; current != nullptr; current = current->parent_) {
        route.push_back(current);
    }

    event.phase = EventRoutePhase::Tunnel;
    for (auto index = route.size(); index > 0U; --index) {
        auto* current = route[index - 1U];
        event.current_target = current;
        auto context = RoutedEventFilterContext{.type = routed_event_type(event.kind),
                                                .phase = EventRoutePhase::Tunnel,
                                                .target = &target,
                                                .current_target = current,
                                                .key_event = &event};
        current->dispatch_routed_event_filter(context);
        if (event.handled) {
            result.handled = true;
            result.handled_by = current;
            result.handled_phase = EventRoutePhase::Tunnel;
            return result;
        }
        current->on_key_tunnel_event(event);
        if (event.handled) {
            result.handled = true;
            result.handled_by = current;
            result.handled_phase = EventRoutePhase::Tunnel;
            return result;
        }
    }

    event.phase = EventRoutePhase::Bubble;
    for (auto* current = &target; current != nullptr; current = current->parent_) {
        event.current_target = current;
        auto context = RoutedEventFilterContext{.type = routed_event_type(event.kind),
                                                .phase = EventRoutePhase::Bubble,
                                                .target = &target,
                                                .current_target = current,
                                                .key_event = &event};
        current->dispatch_routed_event_filter(context);
        if (event.handled) {
            result.handled = true;
            result.handled_by = current;
            result.handled_phase = EventRoutePhase::Bubble;
            return result;
        }
        current->on_key_event(event);
        if (event.handled) {
            result.handled = true;
            result.handled_by = current;
            result.handled_phase = EventRoutePhase::Bubble;
            return result;
        }
    }

    return result;
}

} // namespace winelement::elements
