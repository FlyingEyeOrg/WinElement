#include <winelement/elements/theme_manager.hpp>

namespace winelement::elements {
namespace {

[[nodiscard]] bool theme_style_changed(const style::Theme& previous_theme,
                                       const style::Theme& next_theme,
                                       std::string_view theme_class) noexcept {
    const auto* previous_style = style::theme_style_for_class(previous_theme, theme_class);
    const auto* next_style = style::theme_style_for_class(next_theme, theme_class);
    if (previous_style == nullptr || next_style == nullptr) {
        return previous_style != next_style;
    }
    return *previous_style != *next_style;
}

} // namespace

const style::Theme& ThemeManager::inherited_theme_for_subtree_root(const UIElement& element) {
    for (auto* ancestor = element.logical_parent(); ancestor != nullptr;
         ancestor = ancestor->logical_parent()) {
        if (const auto* local_theme = ancestor->local_theme()) {
            return *local_theme;
        }
    }

    return style::current_theme();
}

void ThemeManager::apply_theme_subtree(UIElement& element, const style::Theme& inherited_theme,
                                       const style::Theme* previous_inherited_theme,
                                       bool include_top_layer) {
    const auto* local_theme = element.local_theme();
    const auto& effective_theme = local_theme != nullptr ? *local_theme : inherited_theme;
    const auto* previous_effective_theme =
        local_theme != nullptr ? local_theme : previous_inherited_theme;
    if (!element.theme_subtree_dirty() && element.theme_current_for(effective_theme)) {
        return;
    }

    const auto theme_class = element.theme_class();
    const auto can_refresh_metadata_only =
        !element.theme_dirty_ && previous_effective_theme != nullptr && !theme_class.empty() &&
        !theme_style_changed(*previous_effective_theme, effective_theme, theme_class);
    if (can_refresh_metadata_only) {
        element.mark_theme_current(effective_theme);
    } else {
        element.apply_theme(effective_theme);
    }

    for (auto index = std::size_t{0}; index < element.child_count(); ++index) {
        apply_theme_subtree(element.child_at(index), effective_theme, previous_effective_theme,
                            false);
    }

    if (include_top_layer && element.parent() == nullptr) {
        for (auto index = std::size_t{0}; index < element.top_layer_count(); ++index) {
            auto& top_layer_element = element.top_layer_at(index);
            apply_theme_subtree(top_layer_element,
                                inherited_theme_for_subtree_root(top_layer_element),
                                previous_effective_theme, false);
        }
    }

    element.mark_theme_subtree_clean();
}

void ThemeManager::apply_theme(UIElement& root, style::Theme theme) {
    root.verify_thread_access();

    const auto previous_theme = style::current_theme();
    style::set_theme(std::move(theme));
    apply_theme_subtree(root, inherited_theme_for_subtree_root(root), &previous_theme,
                        root.parent() == nullptr);
}

void ThemeManager::reapply_current_theme(UIElement& root) {
    root.verify_thread_access();

    apply_theme_subtree(root, inherited_theme_for_subtree_root(root), nullptr,
                        root.parent() == nullptr);
}

void ThemeManager::reapply_subtree(UIElement& subtree_root) {
    subtree_root.verify_thread_access();

    apply_theme_subtree(subtree_root, inherited_theme_for_subtree_root(subtree_root), nullptr,
                        false);
}

} // namespace winelement::elements