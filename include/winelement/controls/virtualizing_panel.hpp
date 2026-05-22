#pragma once

#include <winelement/controls/panel.hpp>
#include <winelement/controls/virtualization.hpp>
#include <winelement/elements/element_descriptor.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace winelement::controls {

class VirtualizingPanel final : public Panel {
  public:
    using ItemFactory = std::function<std::unique_ptr<elements::UIElement>(std::size_t index)>;

    VirtualizingPanel();

    VirtualizingPanel& set_item_count(std::size_t count);
    VirtualizingPanel& set_item_extent(float extent);
    VirtualizingPanel& set_item_extents(std::vector<float> extents);
    VirtualizingPanel& set_overscan(std::size_t overscan);
    VirtualizingPanel& set_item_factory(ItemFactory factory);
    VirtualizingPanel& set_reusable_container_limit(std::size_t limit);

    VirtualizingPanel& set_scroll_offset(float offset);
    VirtualizingPanel& set_viewport_extent(float extent);
    VirtualizingPanel& refresh_virtualization();

    [[nodiscard]] std::size_t item_count() const noexcept;
    [[nodiscard]] std::size_t realized_count() const noexcept;

  protected:
    void on_viewport_enter() override;
    void on_viewport_leave() override;

  private:
    void ensure_pool();
    void bind_slot(std::size_t slot_index, std::size_t item_index);
    void unbind_slot(std::size_t slot_index);

    VirtualizationPlanner planner_;
    ItemFactory item_factory_;
    float scroll_offset_ = 0.0F;
    float viewport_extent_ = 0.0F;

    struct Slot {
        elements::UIElement* element = nullptr;
        std::optional<std::size_t> item_index;
    };
    std::vector<Slot> pool_;
    std::size_t pool_capacity_ = 24;
};

}  // namespace winelement::controls
