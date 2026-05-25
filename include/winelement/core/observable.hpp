#pragma once

#include <winelement/core/event.hpp>
#include <winelement/core/property.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace winelement::core {

enum class ObservableChangeKind { PropertyChanged, Reset, Insert, Remove, Replace };

struct ObservableChange {
    ObservableChangeKind kind = ObservableChangeKind::PropertyChanged;
    std::string property_name;
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
        auto change = ObservableChange{};
        auto changed = false;
        {
            const std::unique_lock lock(values_mutex_);
            auto iterator = find_entry(name);
            if (iterator != values_.end() && iterator->name == name) {
                if (const auto* current = iterator->value.template get<ValueType>();
                    current != nullptr && values_equal(*current, value)) {
                    return *this;
                }
                iterator->value.template emplace<ValueType>(std::move(value));
                change = ObservableChange{.kind = ObservableChangeKind::PropertyChanged,
                                          .property_name = iterator->name};
                changed = true;
            } else {
                iterator = values_.insert(
                    iterator, Entry{.name = std::move(name),
                                    .value = PropertyValue{ValueType(std::move(value))}});
                change = ObservableChange{.kind = ObservableChangeKind::PropertyChanged,
                                          .property_name = iterator->name};
                changed = true;
            }
        }

        if (changed) {
            notify(change);
        }
        return *this;
    }

    ObservableObject& set_value(std::string name, PropertyValue value);

    template <typename T> [[nodiscard]] std::optional<T> get(std::string_view name) const {
        const std::shared_lock lock(values_mutex_);
        const auto iterator = find_entry(name);
        if (iterator == values_.end() || iterator->name != name) {
            return std::nullopt;
        }
        if (const auto* typed = iterator->value.template get<T>()) {
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
    mutable std::shared_mutex values_mutex_;
    EventHandler<const ObservableChange&> observers_;
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
        const std::shared_lock lock(mutex_);
        return items_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        const std::shared_lock lock(mutex_);
        return items_.empty();
    }

    [[nodiscard]] const T& at(std::size_t index) const {
        const std::shared_lock lock(mutex_);
        return items_.at(index);
    }

    [[nodiscard]] T& at(std::size_t index) {
        const std::unique_lock lock(mutex_);
        return items_.at(index);
    }

    [[nodiscard]] std::vector<T> items() const {
        const std::shared_lock lock(mutex_);
        return items_;
    }

    void reset(std::vector<T> items) {
        auto added_count = std::size_t{0U};
        {
            const std::unique_lock lock(mutex_);
            items_ = std::move(items);
            added_count = items_.size();
        }
        notify(ObservableChange{
            .kind = ObservableChangeKind::Reset, .removed_count = 0U, .added_count = added_count});
    }

    void append(T item) {
        auto index = std::size_t{0U};
        {
            const std::unique_lock lock(mutex_);
            index = items_.size();
            items_.push_back(std::move(item));
        }
        notify(ObservableChange{
            .kind = ObservableChangeKind::Insert, .index = index, .added_count = 1U});
    }

    void insert(std::size_t index, T item) {
        {
            const std::unique_lock lock(mutex_);
            if (index > items_.size()) {
                index = items_.size();
            }
            items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index), std::move(item));
        }
        notify(ObservableChange{
            .kind = ObservableChangeKind::Insert, .index = index, .added_count = 1U});
    }

    void erase(std::size_t index) {
        {
            const std::unique_lock lock(mutex_);
            if (index >= items_.size()) {
                return;
            }
            items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
        }
        notify(ObservableChange{
            .kind = ObservableChangeKind::Remove, .index = index, .removed_count = 1U});
    }

    void replace(std::size_t index, T item) {
        {
            const std::unique_lock lock(mutex_);
            if (index >= items_.size()) {
                return;
            }
            items_[index] = std::move(item);
        }
        notify(ObservableChange{.kind = ObservableChangeKind::Replace,
                                .index = index,
                                .removed_count = 1U,
                                .added_count = 1U});
    }

    ObserverToken add_observer(Observer observer) {
        return observers_.add(std::move(observer));
    }

    void remove_observer(ObserverToken token) noexcept {
        observers_.remove(token);
    }

    void clear_observers() noexcept {
        observers_.clear();
    }

  private:
    void notify(const ObservableChange& change) {
        observers_.emit(change);
    }

    mutable std::shared_mutex mutex_;
    std::vector<T> items_;
    EventHandler<const ObservableChange&> observers_;
};

using ObservableObjectPtr = std::shared_ptr<ObservableObject>;
using ObservableObjectList = ObservableList<ObservableObjectPtr>;
using ObservableStringList = ObservableList<std::string>;

template <typename T> [[nodiscard]] std::string arithmetic_to_string(T value) {
    auto buffer = std::array<char, std::numeric_limits<T>::max_digits10 + 8U>{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec == std::errc{}) {
        return std::string(buffer.data(), result.ptr);
    }
    return std::to_string(value);
}

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
            return arithmetic_to_string(*float_value);
        }
        if (const auto* double_value = value.get<double>()) {
            return arithmetic_to_string(*double_value);
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
