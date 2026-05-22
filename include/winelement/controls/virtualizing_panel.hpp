#pragma once

#include <winelement/controls/panel.hpp>
#include <winelement/controls/virtualization.hpp>
#include <winelement/elements/element_descriptor.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
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
    [[nodiscard]] std::size_t reusable_container_count() const noexcept;

  protected:
    void on_viewport_enter() override;
    void on_viewport_leave() override;

  private:
    struct RealizedItem {
        std::size_t index = 0;
        elements::UIElement* element = nullptr;
    };

    void ensure_spacers();
    void update_spacer_extent(elements::UIElement& spacer, float extent);
    [[nodiscard]] std::unique_ptr<elements::UIElement> acquire_reusable(std::size_t index);
    [[nodiscard]] std::size_t leading_spacer_index() const noexcept;

    VirtualizationPlanner planner_;
    RecyclePool<elements::UIElement> recycle_pool_;
    ItemFactory item_factory_;
    std::vector<RealizedItem> realized_;
    std::unordered_map<std::size_t, elements::ElementSnapshot> snapshots_;
    elements::UIElement* leading_spacer_ = nullptr;
    elements::UIElement* trailing_spacer_ = nullptr;
    float scroll_offset_ = 0.0F;
    float viewport_extent_ = 0.0F;
    bool spacers_ready_ = false;
};

}  // namespace winelement::controls
