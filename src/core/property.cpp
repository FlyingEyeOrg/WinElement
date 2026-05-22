#include <winelement/core/property.hpp>

#include <algorithm>

namespace winelement::core {

bool PropertyStore::has_local_value(const PropertyMetadata& metadata) const noexcept {
    if (metadata.id == 0U) {
        return false;
    }

    const auto iterator = find_entry(metadata.id);
    return iterator != values_.end() && iterator->id == metadata.id;
}

PropertyChange PropertyStore::clear_value(const PropertyMetadata& metadata) {
    auto change = PropertyChange{.metadata = &metadata,
                                 .changed = false,
                                 .had_local_value = false,
                                 .invalidation = PropertyInvalidation::None};
    verify_id(metadata);
    const auto iterator = find_entry(metadata.id);
    if (iterator == values_.end() || iterator->id != metadata.id) {
        return change;
    }

    values_.erase(iterator);
    change.changed = true;
    change.had_local_value = true;
    change.invalidation = metadata.invalidation;
    notify(change);
    return change;
}

void PropertyStore::clear() noexcept {
    values_.clear();
}

void PropertyStore::reserve(std::size_t local_value_capacity) {
    values_.reserve(local_value_capacity);
}

PropertyStore::ObserverToken PropertyStore::add_observer(Observer observer) {
    if (!observer) {
        return 0U;
    }
    auto token = next_observer_token_++;
    if (token == 0U) {
        token = next_observer_token_++;
    }
    observers_.push_back(ObserverEntry{.token = token, .observer = std::move(observer)});
    return token;
}

void PropertyStore::remove_observer(ObserverToken token) noexcept {
    if (token == 0U) {
        return;
    }
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
                       [token](const ObserverEntry& entry) { return entry.token == token; }),
        observers_.end());
}

void PropertyStore::clear_observers() noexcept {
    observers_.clear();
}

std::size_t PropertyStore::local_value_count() const noexcept {
    return values_.size();
}

void PropertyStore::notify(const PropertyChange& change) {
    const auto observers = observers_;
    for (const auto& entry : observers) {
        if (entry.observer) {
            entry.observer(change);
        }
    }
}

} // namespace winelement::core
