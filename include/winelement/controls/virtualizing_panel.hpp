#pragma once

#include <winelement/controls/panel.hpp>
#include <winelement/controls/virtualization.hpp>
#include <winelement/elements/element_descriptor.hpp>

#include <cstddef>
#include <functional>
#include <memory>
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
    [[nodiscard]] std::size_t realized_start_index() const noexcept;
    [[nodiscard]] std::size_t realized_count() const noexcept;
    [[nodiscard]] std::size_t reusable_container_count() const noexcept;

  protected:
    void on_viewport_enter() override;
    void on_viewport_leave() override;

  private:
    struct Slot {
        std::size_t item_index = 0;
        float extent = 0.0F;
        elements::UIElement* spacer = nullptr;
        elements::UIElement* realized = nullptr;
        elements::ElementSnapshot snapshot;
    };

    void realize_slot(Slot& slot);
    void unrealize_slot(Slot& slot);
    void sync_slot_spacer(Slot& slot);
    [[nodiscard]] std::unique_ptr<elements::UIElement> acquire_reusable();
    void release_reusable(std::unique_ptr<elements::UIElement> element);
    void build_slots();

    VirtualizationPlanner planner_;
    RecyclePool<elements::UIElement> recycle_pool_;
    ItemFactory item_factory_;
    std::vector<Slot> slots_;
    float scroll_offset_ = 0.0F;
    float viewport_extent_ = 0.0F;
};

}  // namespace winelement::controls
