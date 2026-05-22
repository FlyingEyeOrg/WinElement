#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace winelement::core {

enum class PropertyInvalidation : std::uint32_t {
    None = 0U,
    Layout = 1U << 0U,
    Paint = 1U << 1U,
    Style = 1U << 2U,
    Semantics = 1U << 3U,
    Inherited = 1U << 4U,
};

[[nodiscard]] constexpr PropertyInvalidation operator|(PropertyInvalidation left,
                                                       PropertyInvalidation right) noexcept {
    return static_cast<PropertyInvalidation>(static_cast<std::uint32_t>(left) |
                                             static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr PropertyInvalidation operator&(PropertyInvalidation left,
                                                       PropertyInvalidation right) noexcept {
    return static_cast<PropertyInvalidation>(static_cast<std::uint32_t>(left) &
                                             static_cast<std::uint32_t>(right));
}

constexpr PropertyInvalidation& operator|=(PropertyInvalidation& left,
                                           PropertyInvalidation right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_invalidation(PropertyInvalidation value,
                                              PropertyInvalidation flag) noexcept {
    return (static_cast<std::uint32_t>(value & flag) != 0U);
}

struct PropertyMetadata {
    std::uint64_t id = 0U;
    std::string name;
    std::type_index value_type{typeid(void)};
    PropertyInvalidation invalidation = PropertyInvalidation::None;
    bool inherits = false;
};

[[nodiscard]] inline std::uint64_t allocate_property_id() noexcept {
    static std::atomic_uint64_t next_id{1U};
    auto id = next_id.fetch_add(1U, std::memory_order_relaxed);
    if (id == 0U) {
        id = next_id.fetch_add(1U, std::memory_order_relaxed);
    }
    return id;
}

template <typename T>
[[nodiscard]] PropertyMetadata
make_property_metadata(std::string name,
                       PropertyInvalidation invalidation = PropertyInvalidation::None,
                       bool inherits = false) {
    return PropertyMetadata{.id = allocate_property_id(),
                            .name = std::move(name),
                            .value_type = std::type_index(typeid(T)),
                            .invalidation = invalidation,
                            .inherits = inherits};
}

template <typename T> struct Property {
    using value_type = std::decay_t<T>;

    PropertyMetadata metadata;

    [[nodiscard]] const PropertyMetadata& descriptor() const noexcept {
        return metadata;
    }

    [[nodiscard]] std::uint64_t id() const noexcept {
        return metadata.id;
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return metadata.name;
    }

    [[nodiscard]] operator const PropertyMetadata&() const noexcept {
        return metadata;
    }
};

template <typename T>
[[nodiscard]] Property<std::decay_t<T>>
make_property(std::string name, PropertyInvalidation invalidation = PropertyInvalidation::None,
              bool inherits = false) {
    return Property<std::decay_t<T>>{.metadata = make_property_metadata<std::decay_t<T>>(
                                         std::move(name), invalidation, inherits)};
}

struct PropertyChange {
    const PropertyMetadata* metadata = nullptr;
    bool changed = false;
    bool had_local_value = false;
    PropertyInvalidation invalidation = PropertyInvalidation::None;
};

class PropertyValue final {
  public:
    PropertyValue() noexcept = default;

    PropertyValue(const PropertyValue& other) {
        if (other.vtable_ != nullptr) {
            other.vtable_->copy(*this, other);
        }
    }

    PropertyValue(PropertyValue&& other) noexcept {
        if (other.vtable_ != nullptr) {
            other.vtable_->move(*this, other);
        }
    }

    ~PropertyValue() noexcept {
        reset();
    }

    PropertyValue& operator=(const PropertyValue& other) {
        if (this == &other) {
            return *this;
        }
        reset();
        if (other.vtable_ != nullptr) {
            other.vtable_->copy(*this, other);
        }
        return *this;
    }

    PropertyValue& operator=(PropertyValue&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        reset();
        if (other.vtable_ != nullptr) {
            other.vtable_->move(*this, other);
        }
        return *this;
    }

    template <typename T> explicit PropertyValue(T value) {
        emplace<std::decay_t<T>>(std::move(value));
    }

    template <typename T> void emplace(T value) {
        reset();
        using ValueType = std::decay_t<T>;
        static_assert(std::is_copy_constructible_v<ValueType>,
                      "property values must be copy constructible");
        vtable_ = &vtable_for<ValueType>();
        if constexpr (stores_inline<ValueType>()) {
            pointer_ = &storage_;
            ::new (pointer_) ValueType(std::move(value));
        } else {
            pointer_ = new ValueType(std::move(value));
        }
    }

    template <typename T> [[nodiscard]] const T* get() const noexcept {
        if (vtable_ == nullptr || *vtable_->type != typeid(T)) {
            return nullptr;
        }
        return static_cast<const T*>(pointer_);
    }

    template <typename T> [[nodiscard]] T* get() noexcept {
        if (vtable_ == nullptr || *vtable_->type != typeid(T)) {
            return nullptr;
        }
        return static_cast<T*>(pointer_);
    }

    void reset() noexcept {
        if (vtable_ != nullptr) {
            vtable_->destroy(*this);
        }
    }

  private:
    static constexpr std::size_t inline_storage_size = 64U;

    struct VTable {
        const std::type_info* type = nullptr;
        void (*destroy)(PropertyValue&) noexcept = nullptr;
        void (*copy)(PropertyValue&, const PropertyValue&) = nullptr;
        void (*move)(PropertyValue&, PropertyValue&) noexcept = nullptr;
    };

    template <typename T> [[nodiscard]] static consteval bool stores_inline() noexcept {
        return sizeof(T) <= inline_storage_size && alignof(T) <= alignof(std::max_align_t) &&
               std::is_nothrow_move_constructible_v<T>;
    }

    template <typename T> [[nodiscard]] static T* value_pointer(PropertyValue& value) noexcept {
        return static_cast<T*>(value.pointer_);
    }

    template <typename T>
    [[nodiscard]] static const T* value_pointer(const PropertyValue& value) noexcept {
        return static_cast<const T*>(value.pointer_);
    }

    template <typename T> static void destroy_value(PropertyValue& value) noexcept {
        if constexpr (stores_inline<T>()) {
            std::destroy_at(value_pointer<T>(value));
        } else {
            delete value_pointer<T>(value);
        }
        value.pointer_ = nullptr;
        value.vtable_ = nullptr;
    }

    template <typename T>
    static void copy_value(PropertyValue& target, const PropertyValue& source) {
        target.vtable_ = &vtable_for<T>();
        if constexpr (stores_inline<T>()) {
            target.pointer_ = &target.storage_;
            ::new (target.pointer_) T(*value_pointer<T>(source));
        } else {
            target.pointer_ = new T(*value_pointer<T>(source));
        }
    }

    template <typename T>
    static void move_value(PropertyValue& target, PropertyValue& source) noexcept {
        target.vtable_ = &vtable_for<T>();
        if constexpr (stores_inline<T>()) {
            target.pointer_ = &target.storage_;
            ::new (target.pointer_) T(std::move(*value_pointer<T>(source)));
            std::destroy_at(value_pointer<T>(source));
        } else {
            target.pointer_ = source.pointer_;
        }
        source.pointer_ = nullptr;
        source.vtable_ = nullptr;
    }

    template <typename T> [[nodiscard]] static const VTable& vtable_for() noexcept {
        static const VTable table{.type = &typeid(T),
                                  .destroy = &destroy_value<T>,
                                  .copy = &copy_value<T>,
                                  .move = &move_value<T>};
        return table;
    }

    alignas(std::max_align_t) std::byte storage_[inline_storage_size]{};
    void* pointer_ = nullptr;
    const VTable* vtable_ = nullptr;
};

class PropertyStore final {
  public:
    using Observer = std::function<void(const PropertyChange& change)>;

    template <typename T>
    [[nodiscard]] T value(const PropertyMetadata& metadata, const T& default_value = T{}) const {
        verify_type<T>(metadata);
        verify_id(metadata);
        const auto iterator = find_entry(metadata.id);
        if (iterator == values_.end() || iterator->id != metadata.id) {
            return default_value;
        }
        const auto* value = iterator->value.template get<T>();
        if (value == nullptr) {
            throw std::invalid_argument("property value type does not match metadata");
        }
        return *value;
    }

    template <typename T>
    [[nodiscard]] T value(const Property<T>& property, const T& default_value = T{}) const {
        return value<T>(property.metadata, default_value);
    }

    template <typename T>
    [[nodiscard]] const T* local_value(const PropertyMetadata& metadata) const {
        verify_type<T>(metadata);
        verify_id(metadata);
        const auto iterator = find_entry(metadata.id);
        if (iterator == values_.end() || iterator->id != metadata.id) {
            return nullptr;
        }
        return iterator->value.template get<T>();
    }

    template <typename T> [[nodiscard]] const T* local_value(const Property<T>& property) const {
        return local_value<T>(property.metadata);
    }

    template <typename T> PropertyChange set_value(const PropertyMetadata& metadata, T value) {
        using ValueType = std::decay_t<T>;
        verify_type<ValueType>(metadata);
        verify_id(metadata);
        auto change = PropertyChange{.metadata = &metadata,
                                     .changed = true,
                                     .had_local_value = false,
                                     .invalidation = metadata.invalidation};
        const auto iterator = find_entry(metadata.id);
        if (iterator != values_.end() && iterator->id == metadata.id) {
            change.had_local_value = true;
            if (const auto* current = iterator->value.template get<ValueType>();
                current != nullptr && values_equal(*current, value)) {
                change.changed = false;
                change.invalidation = PropertyInvalidation::None;
                return change;
            }
            iterator->value.template emplace<ValueType>(std::move(value));
        } else {
            values_.insert(iterator, PropertyEntry{.id = metadata.id,
                                                   .value = PropertyValue{std::move(value)}});
        }

        notify(change);
        return change;
    }

    template <typename T, typename U>
    PropertyChange set_value(const Property<T>& property, U&& value) {
        using ValueType = typename Property<T>::value_type;
        return set_value<ValueType>(property.metadata, ValueType(std::forward<U>(value)));
    }

    [[nodiscard]] bool has_local_value(const PropertyMetadata& metadata) const noexcept;
    template <typename T>
    [[nodiscard]] bool has_local_value(const Property<T>& property) const noexcept {
        return has_local_value(property.metadata);
    }
    PropertyChange clear_value(const PropertyMetadata& metadata);
    template <typename T> PropertyChange clear_value(const Property<T>& property) {
        return clear_value(property.metadata);
    }
    void clear() noexcept;
    void reserve(std::size_t local_value_capacity);
    void add_observer(Observer observer);
    [[nodiscard]] std::size_t local_value_count() const noexcept;

  private:
    struct PropertyEntry {
        std::uint64_t id = 0U;
        PropertyValue value;
    };

    using ValueIterator = std::vector<PropertyEntry>::iterator;
    using ConstValueIterator = std::vector<PropertyEntry>::const_iterator;

    template <typename T> static void verify_type(const PropertyMetadata& metadata) {
        if (metadata.value_type != std::type_index(typeid(T))) {
            throw std::invalid_argument("property value type does not match metadata");
        }
    }

    static void verify_id(const PropertyMetadata& metadata) {
        if (metadata.id == 0U) {
            throw std::invalid_argument("property metadata must have a stable id");
        }
    }

    template <typename T> static bool values_equal(const T& left, const T& right) {
        if constexpr (requires { left == right; }) {
            return left == right;
        } else {
            return false;
        }
    }

    [[nodiscard]] ValueIterator find_entry(std::uint64_t id) noexcept {
        return std::lower_bound(values_.begin(), values_.end(), id,
                                [](const PropertyEntry& entry, std::uint64_t property_id) {
                                    return entry.id < property_id;
                                });
    }

    [[nodiscard]] ConstValueIterator find_entry(std::uint64_t id) const noexcept {
        return std::lower_bound(values_.begin(), values_.end(), id,
                                [](const PropertyEntry& entry, std::uint64_t property_id) {
                                    return entry.id < property_id;
                                });
    }

    void notify(const PropertyChange& change);

    std::vector<PropertyEntry> values_;
    std::vector<Observer> observers_;
};

} // namespace winelement::core
