#pragma once

#include <winelement/layout/layout_types.hpp>
#include <winelement/rendering/render_types.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace winelement::elements {

class UIElement;

struct TopLayerOptions {
    layout::Rect bounds{};
    bool light_dismiss = true;
    bool preserve_focus = false;
    rendering::Color backdrop_color = rendering::Color{0, 0, 0, 0};
    bool close_on_escape = true;
    bool modal = false;
    const UIElement* logical_owner = nullptr;
};

struct TopLayerEntry {
    std::unique_ptr<UIElement> element;
    TopLayerOptions options{};
    std::vector<const UIElement*> logical_ancestors;
    std::uint64_t id = 0;
    bool pending_removal = false;
};

class TopLayerManager final {
  public:
    TopLayerManager();
    ~TopLayerManager();

    TopLayerManager(const TopLayerManager&) = delete;
    TopLayerManager& operator=(const TopLayerManager&) = delete;
    TopLayerManager(TopLayerManager&&) noexcept;
    TopLayerManager& operator=(TopLayerManager&&) noexcept;

    [[nodiscard]] std::uint64_t allocate_entry_id() noexcept;
    [[nodiscard]] std::vector<TopLayerEntry>& entries() noexcept;
    [[nodiscard]] const std::vector<TopLayerEntry>& entries() const noexcept;
    [[nodiscard]] std::optional<std::size_t> index_of(std::uint64_t entry_id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> index_of(const UIElement& element) const noexcept;
    [[nodiscard]] std::optional<std::size_t> topmost_escape_dismiss_index() const noexcept;
    [[nodiscard]] std::optional<std::size_t>
    topmost_light_dismiss_index(layout::Point absolute_point) const noexcept;
    [[nodiscard]] std::vector<std::size_t>
    logical_descendant_indices_of(const UIElement& logical_root) const;
    void invalidate_cache() const noexcept;

  private:
    void ensure_cache() const;

    std::vector<TopLayerEntry> entries_;
    mutable std::unordered_map<std::uint64_t, std::size_t> index_by_id_;
    mutable std::unordered_map<const UIElement*, std::size_t> index_by_element_;
    mutable std::unordered_map<const UIElement*, std::vector<std::size_t>> indices_by_logical_root_;
    mutable std::vector<std::size_t> escape_dismiss_indices_;
    mutable std::vector<std::size_t> light_dismiss_indices_;
    mutable std::optional<std::size_t> topmost_active_index_;
    std::uint64_t next_entry_id_ = 1;
    mutable bool cache_dirty_ = true;
};

} // namespace winelement::elements
