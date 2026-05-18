#include <winelement/elements/top_layer_manager.hpp>

#include <winelement/elements/ui_element.hpp>

#include <algorithm>
#include <iterator>

namespace winelement::elements {

TopLayerManager::TopLayerManager() = default;

TopLayerManager::~TopLayerManager() = default;

TopLayerManager::TopLayerManager(TopLayerManager&&) noexcept = default;

TopLayerManager& TopLayerManager::operator=(TopLayerManager&&) noexcept = default;

std::uint64_t TopLayerManager::allocate_entry_id() noexcept {
    const auto id = next_entry_id_++;
    if (next_entry_id_ == 0U) {
        next_entry_id_ = 1U;
    }
    return id;
}

std::vector<TopLayerEntry>& TopLayerManager::entries() noexcept {
    invalidate_cache();
    return entries_;
}

const std::vector<TopLayerEntry>& TopLayerManager::entries() const noexcept {
    return entries_;
}

std::optional<std::size_t> TopLayerManager::index_of(std::uint64_t entry_id) const noexcept {
    ensure_cache();
    const auto iterator = index_by_id_.find(entry_id);
    if (iterator == index_by_id_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

std::optional<std::size_t> TopLayerManager::index_of(const UIElement& element) const noexcept {
    ensure_cache();
    const auto iterator = index_by_element_.find(&element);
    if (iterator == index_by_element_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

std::optional<std::size_t> TopLayerManager::topmost_escape_dismiss_index() const noexcept {
    ensure_cache();
    if (!topmost_active_index_.has_value()) {
        return std::nullopt;
    }
    const auto index = *topmost_active_index_;
    return entries_[index].options.close_on_escape ? std::optional<std::size_t>{index}
                                                   : std::nullopt;
}

std::optional<std::size_t>
TopLayerManager::topmost_light_dismiss_index(layout::Point absolute_point) const noexcept {
    ensure_cache();
    if (!topmost_active_index_.has_value()) {
        return std::nullopt;
    }
    const auto index = *topmost_active_index_;
    const auto& entry = entries_[index];
    const auto has_explicit_bounds =
        entry.options.bounds.width > 0.0F && entry.options.bounds.height > 0.0F;
    if ((has_explicit_bounds &&
         layout::rect_contains_point(entry.options.bounds, absolute_point)) ||
        entry.element->hit_test_subtree(absolute_point) != nullptr ||
        !entry.options.light_dismiss) {
        return std::nullopt;
    }
    return index;
}

std::vector<std::size_t>
TopLayerManager::logical_descendant_indices_of(const UIElement& logical_root) const {
    ensure_cache();
    const auto iterator = indices_by_logical_root_.find(&logical_root);
    if (iterator == indices_by_logical_root_.end()) {
        return {};
    }
    return iterator->second;
}

void TopLayerManager::invalidate_cache() const noexcept {
    cache_dirty_ = true;
}

void TopLayerManager::ensure_cache() const {
    if (!cache_dirty_) {
        return;
    }

    index_by_id_.clear();
    index_by_element_.clear();
    indices_by_logical_root_.clear();
    escape_dismiss_indices_.clear();
    light_dismiss_indices_.clear();
    topmost_active_index_.reset();
    index_by_id_.reserve(entries_.size());
    index_by_element_.reserve(entries_.size());
    escape_dismiss_indices_.reserve(entries_.size());
    light_dismiss_indices_.reserve(entries_.size());

    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const auto& entry = entries_[index];
        index_by_id_[entry.id] = index;
        index_by_element_[entry.element.get()] = index;
        for (const auto* logical_ancestor : entry.logical_ancestors) {
            if (logical_ancestor != nullptr) {
                indices_by_logical_root_[logical_ancestor].push_back(index);
            }
        }
        if (!entry.pending_removal && entry.element->visible()) {
            topmost_active_index_ = index;
        }
        if (entry.options.close_on_escape) {
            escape_dismiss_indices_.push_back(index);
        }
        if (entry.options.light_dismiss) {
            light_dismiss_indices_.push_back(index);
        }
    }

    cache_dirty_ = false;
}

} // namespace winelement::elements
