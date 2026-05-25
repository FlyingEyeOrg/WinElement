#include <winelement/core/observable.hpp>

#include <algorithm>

namespace winelement::core {

ObservableObject& ObservableObject::set_value(std::string name, PropertyValue value) {
    auto change = ObservableChange{};
    {
        const std::lock_guard lock(values_mutex_);
        auto iterator = find_entry(name);
        if (iterator != values_.end() && iterator->name == name) {
            iterator->value = std::move(value);
            change = ObservableChange{.kind = ObservableChangeKind::PropertyChanged,
                                      .property_name = iterator->name};
        } else {
            iterator =
                values_.insert(iterator, Entry{.name = std::move(name), .value = std::move(value)});
            change = ObservableChange{.kind = ObservableChangeKind::PropertyChanged,
                                      .property_name = iterator->name};
        }
    }

    notify(change);
    return *this;
}

const PropertyValue* ObservableObject::value(std::string_view name) const noexcept {
    const std::lock_guard lock(values_mutex_);
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
    auto change = ObservableChange{};
    {
        const std::lock_guard lock(values_mutex_);
        const auto iterator = find_entry(name);
        if (iterator == values_.end() || iterator->name != name) {
            return;
        }
        change = ObservableChange{.kind = ObservableChangeKind::PropertyChanged,
                                  .property_name = iterator->name};
        values_.erase(iterator);
    }
    notify(change);
}

void ObservableObject::clear_values() {
    {
        const std::lock_guard lock(values_mutex_);
        values_.clear();
    }
    notify(ObservableChange{.kind = ObservableChangeKind::Reset});
}

ObservableObserverToken ObservableObject::add_observer(ObservableObserver observer) {
    return observers_.add(std::move(observer));
}

void ObservableObject::remove_observer(ObservableObserverToken token) noexcept {
    observers_.remove(token);
}

void ObservableObject::clear_observers() noexcept {
    observers_.clear();
}

std::size_t ObservableObject::value_count() const noexcept {
    const std::lock_guard lock(values_mutex_);
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
    observers_.emit(change);
}

} // namespace winelement::core
