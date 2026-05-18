#pragma once

#include <winelement/elements/ui_element.hpp>

namespace winelement::elements {

class ThemeManager final {
  public:
    static void apply_theme(UIElement& root, style::Theme theme);
    static void reapply_current_theme(UIElement& root);
    static void reapply_subtree(UIElement& subtree_root);

  private:
    static const style::Theme& inherited_theme_for_subtree_root(const UIElement& element);
    static void apply_theme_subtree(UIElement& element, const style::Theme& inherited_theme,
                                    const style::Theme* previous_inherited_theme,
                                    bool include_top_layer);
};

} // namespace winelement::elements