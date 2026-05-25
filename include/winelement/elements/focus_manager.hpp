#pragma once

#include <winelement/elements/ui_element.hpp>

#include <vector>

namespace winelement::elements {

class FocusManager final {
  public:
    explicit FocusManager(UIElement& root);
    ~FocusManager() noexcept;

    [[nodiscard]] UIElement& root() noexcept;
    [[nodiscard]] const UIElement& root() const noexcept;
    [[nodiscard]] UIElement* focused_element() noexcept;
    [[nodiscard]] const UIElement* focused_element() const noexcept;

    bool set_focus(UIElement* element, bool focus_visible = true);
    bool clear_focus();
    bool focus_first();
    bool focus_first_within(UIElement& scope);
    bool focus_next();
    bool focus_previous();

  private:
    friend class UIElement;

    [[nodiscard]] bool is_in_tree(const UIElement& element) const noexcept;
    [[nodiscard]] bool is_effectively_visible(const UIElement& element) const noexcept;
    [[nodiscard]] bool can_focus(const UIElement& element) const noexcept;
    [[nodiscard]] UIElement* topmost_modal_scope() const noexcept;
    [[nodiscard]] bool focus_scope_contains(const UIElement& scope,
                                            const UIElement& element) const noexcept;
    void append_focusable_elements_within(UIElement& scope,
                                          std::vector<UIElement*>& focusable_elements) const;
    [[nodiscard]] std::vector<UIElement*>& focusable_elements_within(UIElement& scope) const;
    void invalidate_focusable_cache() const noexcept;
    void on_focusable_registered(UIElement& element) noexcept;
    void on_focusable_unregistered(UIElement& element) noexcept;
    void set_focused_state(UIElement& element, bool focused, bool focus_visible,
                           bool focus_within = false);
    void on_focus_scope_invalidated(UIElement& element);
    void on_focus_scope_destroying(UIElement& element) noexcept;

    UIElement* root_ = nullptr;
    UIElement* focused_ = nullptr;
    bool focus_visible_ = false;
    mutable std::vector<UIElement*> focusable_cache_;
    mutable std::vector<UIElement*> scoped_focusable_cache_;
};

} // namespace winelement::elements
