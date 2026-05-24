#include <winelement/elements/focus_manager.hpp>

#include <algorithm>

namespace winelement::elements {

FocusManager::FocusManager(UIElement& root) : root_(&root) {
    root_->attach_focus_manager(*this);
}

FocusManager::~FocusManager() noexcept {
    if (root_ != nullptr) {
        root_->detach_focus_manager(*this);
    }
}

UIElement& FocusManager::root() noexcept {
    return *root_;
}

const UIElement& FocusManager::root() const noexcept {
    return *root_;
}

UIElement* FocusManager::focused_element() noexcept {
    return focused_;
}

const UIElement* FocusManager::focused_element() const noexcept {
    return focused_;
}

bool FocusManager::set_focus(UIElement* element, bool focus_visible) {
    if (element == nullptr) {
        return clear_focus();
    }

    if (auto* modal_scope = topmost_modal_scope();
        modal_scope != nullptr && !modal_scope->contains_logical(*element)) {
        return false;
    }

    if (!is_in_tree(*element) || !can_focus(*element)) {
        return false;
    }

    if (focused_ == element) {
        if (focus_visible_ != focus_visible) {
            focus_visible_ = focus_visible;
            FocusChangeEvent event{.focused = true, .focus_visible = focus_visible_};
            focused_->on_focus_changed(event);
            focused_->invalidate_paint();
        }
        return true;
    }

    if (focused_ != nullptr) {
        auto* previous_focus = focused_;
        const auto focus_within = previous_focus->contains_logical(*element);
        set_focused_state(*previous_focus, false, false, focus_within);
        if (!is_in_tree(*element) || !can_focus(*element)) {
            focused_ = nullptr;
            focus_visible_ = false;
            return false;
        }
    }

    focused_ = element;
    focus_visible_ = focus_visible;
    set_focused_state(*focused_, true, focus_visible_);
    return true;
}

bool FocusManager::clear_focus() {
    if (focused_ == nullptr) {
        return false;
    }

    set_focused_state(*focused_, false, false);
    focused_ = nullptr;
    focus_visible_ = false;
    return true;
}

bool FocusManager::focus_first() {
    auto* scope = topmost_modal_scope();
    return focus_first_within(scope == nullptr ? *root_ : *scope);
}

bool FocusManager::focus_first_within(UIElement& scope) {
    if (!is_in_tree(scope) && &scope != root_) {
        return false;
    }

    auto scoped_focusable_elements = std::vector<UIElement*>{};
    append_focusable_elements_within(scope, scoped_focusable_elements);
    if (scoped_focusable_elements.empty()) {
        return false;
    }

    for (auto* element : scoped_focusable_elements) {
        if (set_focus(element)) {
            return true;
        }
    }
    return false;
}

bool FocusManager::focus_next() {
    auto scoped_focusable_elements = std::vector<UIElement*>{};
    auto* scope = topmost_modal_scope();
    append_focusable_elements_within(scope == nullptr ? *root_ : *scope, scoped_focusable_elements);
    if (scoped_focusable_elements.empty()) {
        return clear_focus();
    }

    const auto current =
        std::find(scoped_focusable_elements.begin(), scoped_focusable_elements.end(), focused_);
    if (current == scoped_focusable_elements.end()) {
        return set_focus(scoped_focusable_elements.front());
    }

    const auto next_index =
        (static_cast<std::size_t>(current - scoped_focusable_elements.begin()) + 1U) %
        scoped_focusable_elements.size();
    return set_focus(scoped_focusable_elements[next_index]);
}

bool FocusManager::focus_previous() {
    auto scoped_focusable_elements = std::vector<UIElement*>{};
    auto* scope = topmost_modal_scope();
    append_focusable_elements_within(scope == nullptr ? *root_ : *scope, scoped_focusable_elements);
    if (scoped_focusable_elements.empty()) {
        return clear_focus();
    }

    const auto current =
        std::find(scoped_focusable_elements.begin(), scoped_focusable_elements.end(), focused_);
    if (current == scoped_focusable_elements.end()) {
        return set_focus(scoped_focusable_elements.back());
    }

    const auto current_index =
        static_cast<std::size_t>(current - scoped_focusable_elements.begin());
    const auto previous_index =
        current_index == 0U ? scoped_focusable_elements.size() - 1U : current_index - 1U;
    return set_focus(scoped_focusable_elements[previous_index]);
}

void FocusManager::invalidate_focusable_cache() const noexcept {
    focusable_cache_.erase(std::remove_if(focusable_cache_.begin(), focusable_cache_.end(),
                                          [this](const UIElement* element) noexcept {
                                              return element == nullptr || !is_in_tree(*element) ||
                                                     !element->focusable_;
                                          }),
                           focusable_cache_.end());
}

bool FocusManager::is_in_tree(const UIElement& element) const noexcept {
    for (const auto* current = &element; current != nullptr; current = current->parent_) {
        if (current == root_) {
            return true;
        }
    }

    return false;
}

bool FocusManager::is_effectively_visible(const UIElement& element) const noexcept {
    for (const auto* current = &element; current != nullptr; current = current->logical_parent()) {
        if (!current->visible_ || current->subtree_virtualized_) {
            return false;
        }
    }

    return true;
}

bool FocusManager::can_focus(const UIElement& element) const noexcept {
    return element.focusable_ && is_effectively_visible(element);
}

UIElement* FocusManager::topmost_modal_scope() const noexcept {
    if (root_ == nullptr) {
        return nullptr;
    }

    for (auto iterator = root_->top_layer_manager_.entries().rbegin();
         iterator != root_->top_layer_manager_.entries().rend(); ++iterator) {
        if (iterator->pending_removal || !iterator->element->visible_) {
            continue;
        }
        if (iterator->options.modal) {
            return iterator->element.get();
        }
    }

    return nullptr;
}

bool FocusManager::focus_scope_contains(const UIElement& scope,
                                        const UIElement& element) const noexcept {
    return &scope == root_ || &scope == &element || scope.contains(element) ||
           scope.contains_logical(element);
}

void FocusManager::append_focusable_elements_within(
    UIElement& scope, std::vector<UIElement*>& focusable_elements) const {
    invalidate_focusable_cache();
    focusable_elements.reserve(focusable_cache_.size());
    for (auto* element : focusable_cache_) {
        if (element != nullptr && focus_scope_contains(scope, *element) && can_focus(*element)) {
            focusable_elements.push_back(element);
        }
    }
}

void FocusManager::on_focusable_registered(UIElement& element) noexcept {
    if (std::find(focusable_cache_.begin(), focusable_cache_.end(), &element) ==
        focusable_cache_.end()) {
        focusable_cache_.push_back(&element);
    }
}

void FocusManager::on_focusable_unregistered(UIElement& element) noexcept {
    focusable_cache_.erase(std::remove(focusable_cache_.begin(), focusable_cache_.end(), &element),
                           focusable_cache_.end());
}

void FocusManager::set_focused_state(UIElement& element, bool focused, bool focus_visible,
                                     bool focus_within) {
    if (element.focused_ == focused && (!focused || focus_visible_ == focus_visible)) {
        return;
    }

    element.focused_ = focused;
    FocusChangeEvent event{.focused = focused,
                           .focus_visible = focused && focus_visible,
                           .focus_within = focus_within};
    element.on_focus_changed(event);
    element.invalidate_paint();
}

void FocusManager::on_focus_scope_invalidated(UIElement& element) {
    invalidate_focusable_cache();
    if (focused_ != nullptr && element.contains(*focused_)) {
        clear_focus();
    }
}

void FocusManager::on_focus_scope_destroying(UIElement& element) noexcept {
    invalidate_focusable_cache();
    if (focused_ != nullptr && element.contains(*focused_)) {
        focused_->focused_ = false;
        focused_ = nullptr;
    }
}

} // namespace winelement::elements
