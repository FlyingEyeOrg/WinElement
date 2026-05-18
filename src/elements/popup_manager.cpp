#include <winelement/elements/popup_manager.hpp>

#include <winelement/elements/event_router.hpp>

#include <algorithm>
#include <stdexcept>

namespace winelement::elements {
namespace {

[[nodiscard]] bool empty_rect(layout::Rect rect) noexcept {
    return rect.width <= 0.0F || rect.height <= 0.0F;
}

} // namespace

PopupManager::PopupManager(UIElement& root) noexcept : root_(&root) {}

PopupOpenResult PopupManager::open(std::unique_ptr<UIElement> element, PopupOptions options) {
    auto& popup_host = host();
    popup_host.verify_thread_access();
    const auto placement = resolve_placement(popup_host, options);
    const auto* logical_owner = options.logical_owner != nullptr ? options.logical_owner : root_;
    auto id = std::uint64_t{0};
    popup_host.push_top_layer_entry(std::move(element),
                                    TopLayerOptions{.bounds = placement.bounds,
                                                    .light_dismiss = options.light_dismiss,
                                                    .preserve_focus = options.preserve_focus,
                                                    .backdrop_color = options.backdrop_color,
                                                    .close_on_escape = options.close_on_escape,
                                                    .modal = options.modal,
                                                    .on_dismissed = std::move(options.on_dismissed),
                                                    .logical_owner = logical_owner},
                                    &id);
    return PopupOpenResult{.handle = PopupHandle{id},
                           .bounds = placement.bounds,
                           .placement = placement.placement,
                           .flipped = placement.flipped,
                           .shifted = placement.shifted};
}

PopupOpenResult PopupManager::open_for_anchor(UIElement& anchor, std::unique_ptr<UIElement> element,
                                              PopupOptions options) {
    options.anchor_rect = anchor.absolute_frame();
    if (empty_rect(options.viewport_rect)) {
        options.viewport_rect = host().absolute_frame();
    }
    if (options.logical_owner == nullptr) {
        options.logical_owner = &anchor;
    }
    return open(std::move(element), std::move(options));
}

bool PopupManager::close(PopupHandle handle) {
    if (!handle.valid()) {
        return false;
    }

    auto& popup_host = host();
    popup_host.verify_thread_access();
    if (popup_host.top_layer_entry_element(handle.id_) == nullptr) {
        return false;
    }

    if (popup_host.event_router_ != nullptr && popup_host.event_router_->dispatch_active()) {
        return popup_host.mark_top_layer_entry_pending_removal(handle.id_);
    }

    return popup_host.remove_top_layer_entry_by_id(handle.id_);
}

bool PopupManager::bring_to_front(PopupHandle handle) {
    if (!handle.valid()) {
        return false;
    }
    return host().bring_top_layer_entry_to_front(handle.id_);
}

bool PopupManager::update_placement(PopupHandle handle, PopupOptions options) {
    if (!handle.valid()) {
        return false;
    }

    auto& popup_host = host();
    const auto placement = resolve_placement(popup_host, options);
    return popup_host.set_top_layer_entry_bounds(handle.id_, placement.bounds);
}

bool PopupManager::update_placement_for_anchor(PopupHandle handle, UIElement& anchor,
                                               PopupOptions options) {
    options.anchor_rect = anchor.absolute_frame();
    if (empty_rect(options.viewport_rect)) {
        options.viewport_rect = host().absolute_frame();
    }
    if (options.logical_owner == nullptr) {
        options.logical_owner = &anchor;
    }
    return update_placement(handle, std::move(options));
}

UIElement* PopupManager::element(PopupHandle handle) noexcept {
    if (!handle.valid()) {
        return nullptr;
    }
    return host().top_layer_entry_element(handle.id_);
}

const UIElement* PopupManager::element(PopupHandle handle) const noexcept {
    if (!handle.valid()) {
        return nullptr;
    }
    return host().top_layer_entry_element(handle.id_);
}

UIElement& PopupManager::host() noexcept {
    return root_->top_layer_host();
}

const UIElement& PopupManager::host() const noexcept {
    return root_->top_layer_host();
}

PopupPlacementResult PopupManager::resolve_placement(const UIElement& popup_host,
                                                     PopupOptions options) const noexcept {
    auto viewport = options.viewport_rect;
    if (empty_rect(viewport)) {
        viewport = popup_host.absolute_frame();
    }
    if (empty_rect(viewport)) {
        viewport = layout::Rect{0.0F, 0.0F, options.size.width, options.size.height};
    }

    return PlacementEngine::place(
        PopupPlacementOptions{.anchor_rect = options.anchor_rect,
                              .popup_size = options.size,
                              .viewport_rect = viewport,
                              .preferred_placement = options.placement,
                              .gap = options.gap,
                              .viewport_margin = options.viewport_margin,
                              .allow_flip = options.allow_flip,
                              .allow_shift = options.allow_shift,
                              .match_anchor_width = options.match_anchor_width});
}

} // namespace winelement::elements