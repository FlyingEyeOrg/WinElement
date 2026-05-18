#include <winelement/style/computed_style.hpp>

#include <functional>
#include <utility>

namespace winelement::style {

std::size_t ComputedStyleKeyHash::operator()(const ComputedStyleKey& key) const noexcept {
    auto hash = std::hash<std::string>{}(key.theme_class);
    const auto combine = [&hash](std::size_t value) noexcept {
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    };
    combine(std::hash<std::uint64_t>{}(key.theme_generation));
    combine(std::hash<std::uint64_t>{}(key.local_generation));
    return hash;
}

ComputedStyleCache::ComputedStyleCache(std::size_t capacity) : cache_(capacity) {}

void ComputedStyleCache::set_capacity(std::size_t capacity) {
    cache_.set_capacity(capacity);
}

void ComputedStyleCache::clear() noexcept {
    cache_.clear();
}

std::size_t ComputedStyleCache::size() const noexcept {
    return cache_.size();
}

std::optional<ComputedStyle> ComputedStyleCache::find(const ComputedStyleKey& key) const noexcept {
    const auto* style = cache_.get(key);
    if (style == nullptr) {
        return std::nullopt;
    }
    return *style;
}

void ComputedStyleCache::store(ComputedStyle style) {
    auto key = style.key;
    cache_.put(std::move(key), std::move(style));
}

ComputedStyle resolve_computed_style(const Theme& theme, std::string_view theme_class,
                                     const UIElementStyle& base_style,
                                     std::uint64_t local_generation) {
    ComputedStyle resolved;
    resolved.key = ComputedStyleKey{.theme_class = std::string(theme_class),
                                    .theme_generation = theme.generation,
                                    .local_generation = local_generation};
    resolved.value = base_style;
    if (const auto* class_style = theme_style_for_class(theme, theme_class)) {
        resolved.value = *class_style;
        resolved.theme_class_matched = true;
    }
    return resolved;
}

} // namespace winelement::style