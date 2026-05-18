#include <winelement/layout/layout_cache.hpp>

#include <winelement/layout/layout_engine.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

namespace winelement::layout {
namespace {

[[nodiscard]] std::size_t hash_float(float value) noexcept {
    if (!std::isfinite(value)) {
        value = 0.0F;
    }
    return std::hash<int>{}(static_cast<int>(std::round(value * 1000.0F)));
}

} // namespace

std::size_t MeasureCacheKeyHash::operator()(const MeasureCacheKey& key) const noexcept {
    auto hash = hash_float(key.width);
    const auto combine = [&hash](std::size_t value) noexcept {
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    };
    combine(std::hash<int>{}(static_cast<int>(key.width_mode)));
    combine(hash_float(key.height));
    combine(std::hash<int>{}(static_cast<int>(key.height_mode)));
    combine(std::hash<std::uint64_t>{}(key.content_generation));
    return hash;
}

MeasureCache::MeasureCache(std::size_t capacity) : cache_(capacity) {}

void MeasureCache::set_capacity(std::size_t capacity) {
    cache_.set_capacity(capacity);
}

std::size_t MeasureCache::capacity() const noexcept {
    return cache_.capacity();
}

std::size_t MeasureCache::size() const noexcept {
    return cache_.size();
}

void MeasureCache::clear() noexcept {
    cache_.clear();
}

void MeasureCache::invalidate_generation(std::uint64_t content_generation) noexcept {
    minimum_generation_ = std::max(minimum_generation_, content_generation + 1U);
    cache_.clear();
}

std::optional<Size> MeasureCache::find(const MeasureCacheKey& key) const noexcept {
    if (key.content_generation < minimum_generation_) {
        return std::nullopt;
    }
    const auto* cached = cache_.get(key);
    if (cached == nullptr) {
        return std::nullopt;
    }
    return *cached;
}

void MeasureCache::store(MeasureCacheKey key, Size size) {
    if (key.content_generation < minimum_generation_) {
        return;
    }
    cache_.put(key, size);
}

LayoutElementPool::LayoutElementPool(LayoutEngine& engine) : engine_(&engine) {}

std::unique_ptr<LayoutElement> LayoutElementPool::acquire() {
    if (pool_.empty()) {
        if (engine_ != nullptr) {
            return engine_->create_element();
        }
        return std::unique_ptr<LayoutElement>(new LayoutElement());
    }
    auto element = std::move(pool_.back());
    pool_.pop_back();
    return element;
}

void LayoutElementPool::release(std::unique_ptr<LayoutElement> element) noexcept {
    if (!element) {
        return;
    }
    element->reset_for_pool(engine_ != nullptr ? engine_->config_ : nullptr);
    pool_.push_back(std::move(element));
}

void LayoutElementPool::clear() noexcept {
    pool_.clear();
}

std::size_t LayoutElementPool::pooled_count() const noexcept {
    return pool_.size();
}

} // namespace winelement::layout