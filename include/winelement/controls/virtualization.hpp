#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace winelement::controls {

struct VirtualizationWindow {
    std::size_t start_index = 0U;
    std::size_t count = 0U;
    std::size_t total_count = 0U;
    float leading_spacer = 0.0F;
    float trailing_spacer = 0.0F;
};

class VirtualizationPlanner final {
  public:
    void set_total_count(std::size_t total_count);
    void set_item_extent(float item_extent) noexcept;
    void set_item_extents(std::vector<float> item_extents);
    void clear_item_extents() noexcept;
    void set_overscan(std::size_t overscan) noexcept;

    [[nodiscard]] VirtualizationWindow window_for(float scroll_offset,
                                                  float viewport_extent) const noexcept;
    [[nodiscard]] std::size_t total_count() const noexcept;
    [[nodiscard]] float item_extent() const noexcept;
    [[nodiscard]] std::size_t overscan() const noexcept;

  private:
    void rebuild_prefix_extents();

    std::size_t total_count_ = 0U;
    float item_extent_ = 28.0F;
    std::size_t overscan_ = 2U;
    std::vector<float> item_extents_;
    std::vector<float> prefix_extents_;
};

template <typename T> class RecyclePool final {
  public:
    void set_capacity(std::size_t capacity) noexcept {
        capacity_ = capacity;
        while (items_.size() > capacity_) {
            items_.pop_back();
        }
    }

    [[nodiscard]] std::unique_ptr<T> acquire() {
        if (items_.empty()) {
            return nullptr;
        }
        auto item = std::move(items_.back());
        items_.pop_back();
        return item;
    }

    void release(std::unique_ptr<T> item) {
        if (item && items_.size() < capacity_) {
            items_.push_back(std::move(item));
        }
    }

    void clear() noexcept {
        items_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return items_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

  private:
    std::size_t capacity_ = 128U;
    std::vector<std::unique_ptr<T>> items_;
};

} // namespace winelement::controls