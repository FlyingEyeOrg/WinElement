#include <winelement/core/observable.hpp>

#include <algorithm>

namespace winelement::core {

ObservableObject& ObservableObject::set_value(std::string name, PropertyValue value) {
    auto iterator = find_entry(name);
    if (iterator != values_.end() && iterator->name == name) {
        iterator->value = std::move(value);
        notify(ObservableChange{.kind = ObservableChangeKind::PropertyChanged,
                                .property_name = iterator->name});
        return *this;
    }

    iterator = values_.insert(iterator, Entry{.name = std::move(name), .value = std::move(value)});
    notify(ObservableChange{.kind = ObservableChangeKind::PropertyChanged,
                            .property_name = iterator->name});
    return *this;
}

const PropertyValue* ObservableObject::value(std::string_view name) const noexcept {
    const auto iterator = find_entry(name);
    if (iterator == values_.end() || iterator->name != name) {
        return nullptr;
    }
    return &iterator->value;
}

bool ObservableObject::has_value(std::string_view name) const noexcept {
    return value(name) != nullptr;
}

void ObservableObject::clear(std::string_view name) {
    const auto iterator = find_entry(name);
    if (iterator == values_.end() || iterator->name != name) {
        return;
    }
    const auto removed_name = iterator->name;
    values_.erase(iterator);
    notify(ObservableChange{.kind = ObservableChangeKind::PropertyChanged,
                            .property_name = removed_name});
}

void ObservableObject::clear_values() {
    values_.clear();
    notify(ObservableChange{.kind = ObservableChangeKind::Reset});
}

ObservableObserverToken ObservableObject::add_observer(ObservableObserver observer) {
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

void ObservableObject::remove_observer(ObservableObserverToken token) noexcept {
    if (token == 0U) {
        return;
    }
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
                       [token](const ObserverEntry& entry) { return entry.token == token; }),
        observers_.end());
}

void ObservableObject::clear_observers() noexcept {
    observers_.clear();
}

std::size_t ObservableObject::value_count() const noexcept {
    return values_.size();
}

std::vector<ObservableObject::Entry>::iterator
ObservableObject::find_entry(std::string_view name) noexcept {
    return std::lower_bound(
        values_.begin(), values_.end(), name,
        [](const Entry& entry, std::string_view value) { return entry.name < value; });
}

std::vector<ObservableObject::Entry>::const_iterator
ObservableObject::find_entry(std::string_view name) const noexcept {
    return std::lower_bound(
        values_.begin(), values_.end(), name,
        [](const Entry& entry, std::string_view value) { return entry.name < value; });
}

void ObservableObject::notify(const ObservableChange& change) {
    const auto observers = observers_;
    for (const auto& entry : observers) {
        if (entry.observer) {
            entry.observer(change);
        }
    }
}

} // namespace winelement::core
