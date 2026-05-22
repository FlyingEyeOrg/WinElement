#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace winelement::core {

class CacheKeyBuilder final {
  public:
    template <typename T> CacheKeyBuilder& add(const T& value) {
        const auto value_hash = std::hash<T>{}(value);
        hash_ ^= value_hash + 0x9e3779b97f4a7c15ULL + (hash_ << 6U) + (hash_ >> 2U);
        return *this;
    }

    [[nodiscard]] std::size_t hash() const noexcept {
        return hash_;
    }

  private:
    std::size_t hash_ = 2166136261U;
};

template <typename Key, typename Value, typename Hash = std::hash<Key>,
          typename Equal = std::equal_to<Key>>
class LruCache final {
  public:
    explicit LruCache(std::size_t capacity = 64U) : capacity_(normalize_capacity(capacity)) {
        entries_.reserve(capacity_);
    }

    void set_capacity(std::size_t capacity) {
        const auto previous_capacity = capacity_;
        capacity_ = normalize_capacity(capacity);
        trim_to_capacity();
        if (capacity_ < previous_capacity && entries_.capacity() > capacity_) {
            entries_.shrink_to_fit();
        }
        if (entries_.capacity() < capacity_) {
            entries_.reserve(capacity_);
        }
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return entries_.empty();
    }

    void clear() noexcept {
        entries_.clear();
    }

    void put(Key key, Value value) {
        const auto index = find_index(key);
        if (index.has_value()) {
            entries_[*index].value = std::move(value);
            move_to_front(*index);
            return;
        }

        if (entries_.size() == capacity_) {
            entries_.pop_back();
        }
        entries_.insert(entries_.begin(), Entry{.key = std::move(key), .value = std::move(value)});
    }

    [[nodiscard]] Value* get(const Key& key) noexcept {
        const auto index = find_index(key);
        if (!index.has_value()) {
            return nullptr;
        }
        move_to_front(*index);
        return &entries_.front().value;
    }

    [[nodiscard]] const Value* get(const Key& key) const noexcept {
        const auto index = find_index(key);
        return index.has_value() ? &entries_[*index].value : nullptr;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        return find_index(key).has_value();
    }

    bool erase(const Key& key) noexcept {
        const auto index = find_index(key);
        if (!index.has_value()) {
            return false;
        }
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(*index));
        return true;
    }

  private:
    struct Entry {
        Key key;
        Value value;
    };

    [[nodiscard]] static std::size_t normalize_capacity(std::size_t capacity) noexcept {
        return std::max<std::size_t>(capacity, 1U);
    }

    [[nodiscard]] std::optional<std::size_t> find_index(const Key& key) const noexcept {
        for (auto index = std::size_t{0}; index < entries_.size(); ++index) {
            if (equal_(entries_[index].key, key)) {
                return index;
            }
        }
        return std::nullopt;
    }

    void move_to_front(std::size_t index) {
        if (index == 0U) {
            return;
        }

        std::rotate(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(index),
                    entries_.begin() + static_cast<std::ptrdiff_t>(index + 1U));
    }

    void trim_to_capacity() {
        while (entries_.size() > capacity_) {
            entries_.pop_back();
        }
    }

    std::size_t capacity_ = 64U;
    [[no_unique_address]] Equal equal_{};
    std::vector<Entry> entries_;
};

} // namespace winelement::core
