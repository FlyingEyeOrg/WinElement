#pragma once

#include <winelement/controls/panel.hpp>
#include <winelement/controls/virtualization.hpp>
#include <winelement/elements/input_event.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace winelement::controls {

class VirtualizingPanel final : public Panel {
  public:
    using ItemBinder = std::function<void(elements::UIElement& slot, std::size_t index)>;
    using SlotFactory = std::function<std::unique_ptr<elements::UIElement>()>;

    VirtualizingPanel();

    VirtualizingPanel& set_item_count(std::size_t count);
    VirtualizingPanel& set_item_extent(float extent);
    VirtualizingPanel& set_item_extents(std::vector<float> extents);
    VirtualizingPanel& set_overscan(std::size_t overscan);
    VirtualizingPanel& set_slot_factory(SlotFactory factory);
    VirtualizingPanel& set_item_binder(ItemBinder binder);
    VirtualizingPanel& set_pool_capacity(std::size_t capacity);

    VirtualizingPanel& set_scroll_offset(float offset);
    VirtualizingPanel& set_viewport_extent(float extent);
    VirtualizingPanel& refresh_virtualization();

    [[nodiscard]] std::size_t item_count() const noexcept;

  protected:
    void on_viewport_enter() override;
    void on_viewport_leave() override;
    void on_pointer_event(elements::PointerEvent& event) override;

  private:
    void ensure_pool(std::size_t required_count);

    VirtualizationPlanner planner_;
    SlotFactory slot_factory_;
    ItemBinder item_binder_;
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
