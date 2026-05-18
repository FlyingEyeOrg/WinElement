#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

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
    explicit LruCache(std::size_t capacity = 64U)
        : capacity_(std::max<std::size_t>(capacity, 1U)) {}

    void set_capacity(std::size_t capacity) {
        capacity_ = std::max<std::size_t>(capacity, 1U);
        trim_to_capacity();
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
        index_.clear();
    }

    void put(Key key, Value value) {
        const auto iterator = index_.find(key);
        if (iterator != index_.end()) {
            iterator->second->value = std::move(value);
            entries_.splice(entries_.begin(), entries_, iterator->second);
            index_[entries_.front().key] = entries_.begin();
            return;
        }

        entries_.push_front(Entry{.key = std::move(key), .value = std::move(value)});
        index_.emplace(entries_.front().key, entries_.begin());
        trim_to_capacity();
    }

    [[nodiscard]] Value* get(const Key& key) noexcept {
        const auto iterator = index_.find(key);
        if (iterator == index_.end()) {
            return nullptr;
        }
        entries_.splice(entries_.begin(), entries_, iterator->second);
        index_[entries_.front().key] = entries_.begin();
        return &entries_.front().value;
    }

    [[nodiscard]] const Value* get(const Key& key) const noexcept {
        const auto iterator = index_.find(key);
        return iterator == index_.end() ? nullptr : &iterator->second->value;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        return index_.find(key) != index_.end();
    }

    bool erase(const Key& key) noexcept {
        const auto iterator = index_.find(key);
        if (iterator == index_.end()) {
            return false;
        }
        entries_.erase(iterator->second);
        index_.erase(iterator);
        return true;
    }

  private:
    struct Entry {
        Key key;
        Value value;
    };

    using EntryList = std::list<Entry>;
    using EntryIterator = typename EntryList::iterator;

    void trim_to_capacity() {
        while (entries_.size() > capacity_) {
            index_.erase(entries_.back().key);
            entries_.pop_back();
        }
    }

    std::size_t capacity_ = 64U;
    EntryList entries_;
    std::unordered_map<Key, EntryIterator, Hash, Equal> index_;
};

} // namespace winelement::core