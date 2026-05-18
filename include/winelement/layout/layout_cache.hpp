#pragma once

#include <winelement/core/cache.hpp>
#include <winelement/layout/layout_element.hpp>
#include <winelement/layout/layout_types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace winelement::layout {

class LayoutEngine;

struct MeasureCacheKey {
    float width = 0.0F;
    MeasureMode width_mode = MeasureMode::Undefined;
    float height = 0.0F;
    MeasureMode height_mode = MeasureMode::Undefined;
    std::uint64_t content_generation = 0U;

    [[nodiscard]] bool operator==(const MeasureCacheKey& other) const noexcept = default;
};

struct MeasureCacheKeyHash {
    [[nodiscard]] std::size_t operator()(const MeasureCacheKey& key) const noexcept;
};

class MeasureCache final {
  public:
    explicit MeasureCache(std::size_t capacity = 32U);

    void set_capacity(std::size_t capacity);
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    void clear() noexcept;
    void invalidate_generation(std::uint64_t content_generation) noexcept;

    [[nodiscard]] std::optional<Size> find(const MeasureCacheKey& key) const noexcept;
    void store(MeasureCacheKey key, Size size);

  private:
    core::LruCache<MeasureCacheKey, Size, MeasureCacheKeyHash> cache_;
    std::uint64_t minimum_generation_ = 0U;
};

class LayoutElementPool final {
  public:
    LayoutElementPool() = default;
    explicit LayoutElementPool(LayoutEngine& engine);

    [[nodiscard]] std::unique_ptr<LayoutElement> acquire();
    void release(std::unique_ptr<LayoutElement> element) noexcept;
    void clear() noexcept;
    [[nodiscard]] std::size_t pooled_count() const noexcept;

  private:
    LayoutEngine* engine_ = nullptr;
    std::vector<std::unique_ptr<LayoutElement>> pool_;
};

} // namespace winelement::layout