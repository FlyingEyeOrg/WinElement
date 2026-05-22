#pragma once

#include <winelement/core/property.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace winelement::core {

enum class ObservableChangeKind { PropertyChanged, Reset, Insert, Remove, Replace };

struct ObservableChange {
    ObservableChangeKind kind = ObservableChangeKind::PropertyChanged;
    std::string_view property_name;
    std::size_t index = 0U;
    std::size_t removed_count = 0U;
    std::size_t added_count = 0U;
};

using ObservableObserver = std::function<void(const ObservableChange& change)>;
using ObservableObserverToken = std::uint64_t;

class ObservableObject : public std::enable_shared_from_this<ObservableObject> {
  public:
    virtual ~ObservableObject() = default;

    template <typename T> ObservableObject& set(std::string name, T value) {
        using ValueType = std::decay_t<T>;
        auto iterator = find_entry(name);
        if (iterator != values_.end() && iterator->name == name) {
            if (const auto* current = iterator->value.template get<ValueType>();
                current != nullptr && values_equal(*current, value)) {
                return *this;
            }
            iterator->value.template emplace<ValueType>(std::move(value));
            notify(ObservableChange{.kind = ObservableChangeKind::PropertyChanged,
                                    .property_name = iterator->name});
            return *this;
        }

        iterator =
            values_.insert(iterator, Entry{.name = std::move(name),
                                           .value = PropertyValue{ValueType(std::move(value))}});
        notify(ObservableChange{.kind = ObservableChangeKind::PropertyChanged,
                                .property_name = iterator->name});
        return *this;
    }

    ObservableObject& set_value(std::string name, PropertyValue value);

    template <typename T> [[nodiscard]] std::optional<T> get(std::string_view name) const {
        const auto* property_value = value(name);
        if (property_value == nullptr) {
            return std::nullopt;
        }
        if (const auto* typed = property_value->template get<T>()) {
            return *typed;
        }
        return std::nullopt;
    }

    [[nodiscard]] const PropertyValue* value(std::string_view name) const noexcept;
    [[nodiscard]] bool has_value(std::string_view name) const noexcept;
    void clear(std::string_view name);
    void clear_values();

    ObservableObserverToken add_observer(ObservableObserver observer);
    void remove_observer(ObservableObserverToken token) noexcept;
    void clear_observers() noexcept;
    [[nodiscard]] std::size_t value_count() const noexcept;

  private:
    struct Entry {
        std::string name;
        PropertyValue value;
    };

    struct ObserverEntry {
        ObservableObserverToken token = 0U;
        ObservableObserver observer;
    };

    template <typename T> static bool values_equal(const T& left, const T& right) {
        if constexpr (requires { left == right; }) {
            return left == right;
        } else {
            return false;
        }
    }

    [[nodiscard]] std::vector<Entry>::iterator find_entry(std::string_view name) noexcept;
    [[nodiscard]] std::vector<Entry>::const_iterator
    find_entry(std::string_view name) const noexcept;
    void notify(const ObservableChange& change);

    std::vector<Entry> values_;
    std::vector<ObserverEntry> observers_;
    ObservableObserverToken next_observer_token_ = 1U;
};

template <typename T> class ObservableProperty final {
  public:
    ObservableProperty() = default;

    ObservableProperty(std::shared_ptr<ObservableObject> owner, std::string name)
        : owner_(std::move(owner)), name_(std::move(name)) {}

    void set(T value) const {
        if (auto owner = owner_.lock()) {
            owner->set<T>(name_, std::move(value));
        }
    }

    [[nodiscard]] std::optional<T> get() const {
        if (auto owner = owner_.lock()) {
            return owner->get<T>(name_);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return name_;
    }

  private:
    std::weak_ptr<ObservableObject> owner_;
    std::string name_;
};

template <typename T> class ObservableList final {
  public:
    using value_type = T;
    using Observer = ObservableObserver;
    using ObserverToken = ObservableObserverToken;

    ObservableList() = default;
    explicit ObservableList(std::vector<T> items) : items_(std::move(items)) {}

    [[nodiscard]] std::size_t size() const noexcept {
        return items_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return items_.empty();
    }

    [[nodiscard]] const T& at(std::size_t index) const {
        return items_.at(index);
    }

    [[nodiscard]] T& at(std::size_t index) {
        return items_.at(index);
    }

    [[nodiscard]] const std::vector<T>& items() const noexcept {
        return items_;
    }

    void reset(std::vector<T> items) {
        items_ = std::move(items);
        notify(ObservableChange{.kind = ObservableChangeKind::Reset,
                                .removed_count = 0U,
                                .added_count = items_.size()});
    }

    void append(T item) {
        const auto index = items_.size();
        items_.push_back(std::move(item));
        notify(ObservableChange{
            .kind = ObservableChangeKind::Insert, .index = index, .added_count = 1U});
    }

    void insert(std::size_t index, T item) {
        if (index > items_.size()) {
            index = items_.size();
        }
        items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index), std::move(item));
        notify(ObservableChange{
            .kind = ObservableChangeKind::Insert, .index = index, .added_count = 1U});
    }

    void erase(std::size_t index) {
        if (index >= items_.size()) {
            return;
        }
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
        notify(ObservableChange{
            .kind = ObservableChangeKind::Remove, .index = index, .removed_count = 1U});
    }

    void replace(std::size_t index, T item) {
        if (index >= items_.size()) {
            return;
        }
        items_[index] = std::move(item);
        notify(ObservableChange{.kind = ObservableChangeKind::Replace,
                                .index = index,
                                .removed_count = 1U,
                                .added_count = 1U});
    }

    ObserverToken add_observer(Observer observer) {
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

    void remove_observer(ObserverToken token) noexcept {
        if (token == 0U) {
            return;
        }
        observers_.erase(
            std::remove_if(observers_.begin(), observers_.end(),
                           [token](const ObserverEntry& entry) { return entry.token == token; }),
            observers_.end());
    }

    void clear_observers() noexcept {
        observers_.clear();
    }

  private:
    struct ObserverEntry {
        ObserverToken token = 0U;
        Observer observer;
    };

    void notify(const ObservableChange& change) {
        const auto observers = observers_;
        for (const auto& entry : observers) {
            if (entry.observer) {
                entry.observer(change);
            }
        }
    }

    std::vector<T> items_;
    std::vector<ObserverEntry> observers_;
    ObserverToken next_observer_token_ = 1U;
};

using ObservableObjectPtr = std::shared_ptr<ObservableObject>;
using ObservableObjectList = ObservableList<ObservableObjectPtr>;
using ObservableStringList = ObservableList<std::string>;

template <typename T>
[[nodiscard]] std::optional<T> coerce_property_value(const PropertyValue& value) {
    if (const auto* typed = value.template get<T>()) {
        return *typed;
    }

    if constexpr (std::is_same_v<T, std::string>) {
        if (const auto* bool_value = value.get<bool>()) {
            return *bool_value ? std::string{"true"} : std::string{"false"};
        }
        if (const auto* int_value = value.get<int>()) {
            return std::to_string(*int_value);
        }
        if (const auto* size_value = value.get<std::size_t>()) {
            return std::to_string(*size_value);
        }
        if (const auto* float_value = value.get<float>()) {
            std::ostringstream stream;
            stream << *float_value;
            return stream.str();
        }
        if (const auto* double_value = value.get<double>()) {
            std::ostringstream stream;
            stream << *double_value;
            return stream.str();
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        if (const auto* string_value = value.get<std::string>()) {
            return *string_value == "true" || *string_value == "1";
        }
        if (const auto* int_value = value.get<int>()) {
            return *int_value != 0;
        }
    } else if constexpr (std::is_arithmetic_v<T>) {
        if (const auto* int_value = value.get<int>()) {
            return static_cast<T>(*int_value);
        }
        if (const auto* size_value = value.get<std::size_t>()) {
            return static_cast<T>(*size_value);
        }
        if (const auto* float_value = value.get<float>()) {
            return static_cast<T>(*float_value);
        }
        if (const auto* double_value = value.get<double>()) {
            return static_cast<T>(*double_value);
        }
        if (const auto* string_value = value.get<std::string>()) {
            T parsed{};
            const auto* begin = string_value->data();
            const auto* end = begin + string_value->size();
            const auto result = std::from_chars(begin, end, parsed);
            if (result.ec == std::errc{} && result.ptr == end) {
                return parsed;
            }
        }
    }

    return std::nullopt;
}

} // namespace winelement::core
