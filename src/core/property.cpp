#include <winelement/core/property.hpp>

namespace winelement::core {

bool PropertyStore::has_local_value(const PropertyMetadata& metadata) const noexcept {
    return metadata.id != 0U && values_.find(metadata.id) != values_.end();
}

PropertyChange PropertyStore::clear_value(const PropertyMetadata& metadata) {
    auto change = PropertyChange{.metadata = &metadata,
                                 .changed = false,
                                 .had_local_value = false,
                                 .invalidation = PropertyInvalidation::None};
    verify_id(metadata);
    const auto iterator = values_.find(metadata.id);
    if (iterator == values_.end()) {
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

void PropertyStore::add_observer(Observer observer) {
    if (observer) {
        observers_.push_back(std::move(observer));
    }
}

std::size_t PropertyStore::local_value_count() const noexcept {
    return values_.size();
}

void PropertyStore::notify(const PropertyChange& change) {
    for (const auto& observer : observers_) {
        observer(change);
    }
}

} // namespace winelement::core