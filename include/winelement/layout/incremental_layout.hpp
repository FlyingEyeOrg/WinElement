#pragma once

#include <winelement/layout/layout_types.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace winelement::layout {

struct DpiLayoutCacheKey {
    float dpi = 96.0F;
    Size constraint{};
    std::uint64_t style_generation = 0U;

    [[nodiscard]] friend bool operator==(DpiLayoutCacheKey, DpiLayoutCacheKey) noexcept = default;
};

struct DpiLayoutCacheEntry {
    DpiLayoutCacheKey key{};
    std::uint64_t layout_generation = 0U;
    Rect root_frame{};
};

class DpiLayoutCache final {
  public:
    explicit DpiLayoutCache(std::size_t max_entries = 4U) noexcept
        : max_entries_(std::max<std::size_t>(max_entries, 1U)) {}

    void store(DpiLayoutCacheEntry entry) {
        if (entries_.size() >= max_entries_) {
            entries_.erase(entries_.begin());
        }
        entries_.push_back(entry);
    }

    [[nodiscard]] std::optional<DpiLayoutCacheEntry> find(DpiLayoutCacheKey key) const noexcept {
        const auto iterator = std::find_if(entries_.begin(), entries_.end(),
                                           [key](const auto& entry) { return entry.key == key; });
        if (iterator == entries_.end()) {
            return std::nullopt;
        }
        return *iterator;
    }

    void clear() noexcept {
        entries_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

  private:
    std::size_t max_entries_ = 4U;
    std::vector<DpiLayoutCacheEntry> entries_;
};

struct IncrementalLayoutSlice {
    std::size_t first_dirty_index = 0U;
    std::size_t dirty_count = 0U;
    bool continuation_required = false;
};

class IncrementalLayoutPlanner final {
  public:
    void set_max_nodes_per_slice(std::size_t max_nodes) noexcept {
        max_nodes_per_slice_ = std::max<std::size_t>(max_nodes, 1U);
    }

    [[nodiscard]] IncrementalLayoutSlice plan(std::size_t first_dirty_index,
                                              std::size_t total_dirty_count) const noexcept {
        const auto count = std::min(max_nodes_per_slice_, total_dirty_count);
        return IncrementalLayoutSlice{.first_dirty_index = first_dirty_index,
                                      .dirty_count = count,
                                      .continuation_required = count < total_dirty_count};
    }

    [[nodiscard]] std::size_t max_nodes_per_slice() const noexcept {
        return max_nodes_per_slice_;
    }

  private:
    std::size_t max_nodes_per_slice_ = 256U;
};

} // namespace winelement::layout
