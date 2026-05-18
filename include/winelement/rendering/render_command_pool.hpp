#pragma once

#include <winelement/rendering/render_command_list.hpp>
#include <winelement/rendering/render_scene.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace winelement::rendering {

class RenderCommandListPool final {
  public:
    explicit RenderCommandListPool(std::size_t capacity = 64U) : capacity_(capacity) {}

    [[nodiscard]] std::unique_ptr<RenderCommandList> acquire() {
        if (pool_.empty()) {
            return std::make_unique<RenderCommandList>();
        }
        auto list = std::move(pool_.back());
        pool_.pop_back();
        return list;
    }

    void release(std::unique_ptr<RenderCommandList> list) {
        if (!list || pool_.size() >= capacity_) {
            return;
        }
        *list = RenderCommandList{};
        pool_.push_back(std::move(list));
    }

    void clear() noexcept {
        pool_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return pool_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

  private:
    std::size_t capacity_ = 64U;
    std::vector<std::unique_ptr<RenderCommandList>> pool_;
};

class RenderNodeArena final {
  public:
    explicit RenderNodeArena(std::size_t reserve_count = 0U) {
        nodes_.reserve(reserve_count);
    }

    [[nodiscard]] RenderNode& create(RenderNode node = {}) {
        nodes_.push_back(std::move(node));
        return nodes_.back();
    }

    void clear() noexcept {
        nodes_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return nodes_.size();
    }

  private:
    std::vector<RenderNode> nodes_;
};

} // namespace winelement::rendering