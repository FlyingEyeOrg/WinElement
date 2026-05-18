#pragma once

#include <winelement/core/cache.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace winelement::style {

struct ComputedStyleKey {
    std::string theme_class;
    std::uint64_t theme_generation = 0U;
    std::uint64_t local_generation = 0U;

    [[nodiscard]] bool operator==(const ComputedStyleKey& other) const noexcept = default;
};

struct ComputedStyleKeyHash {
    [[nodiscard]] std::size_t operator()(const ComputedStyleKey& key) const noexcept;
};

struct ComputedStyle {
    UIElementStyle value{};
    ComputedStyleKey key{};
    bool theme_class_matched = false;
};

class ComputedStyleCache final {
  public:
    explicit ComputedStyleCache(std::size_t capacity = 128U);

    void set_capacity(std::size_t capacity);
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] std::optional<ComputedStyle> find(const ComputedStyleKey& key) const noexcept;
    void store(ComputedStyle style);

  private:
    core::LruCache<ComputedStyleKey, ComputedStyle, ComputedStyleKeyHash> cache_;
};

[[nodiscard]] ComputedStyle resolve_computed_style(const Theme& theme, std::string_view theme_class,
                                                   const UIElementStyle& base_style,
                                                   std::uint64_t local_generation = 0U);

} // namespace winelement::style
