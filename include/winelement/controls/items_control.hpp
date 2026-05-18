#pragma once

#include <winelement/controls/control.hpp>
#include <winelement/controls/virtualization.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace winelement::controls {

class ItemsControl final : public Control {
  public:
    enum class SelectionMode { None, Single, Multiple };

    struct ItemGroup {
        std::string name;
        std::size_t start_index = 0;
        std::size_t count = 0;
    };

    struct ItemContext {
        std::string_view item;
        std::size_t index = 0;
        bool selected = false;
    };

    using ItemsSource = std::function<std::vector<std::string>()>;
    using ItemFactory = std::function<std::unique_ptr<elements::UIElement>(ItemContext context)>;
    using SelectionChangedHandler = std::function<void(std::optional<std::size_t> index)>;
    using MultiSelectionChangedHandler =
        std::function<void(const std::vector<std::size_t>& indices)>;
    using ReorderHandler = std::function<void(std::size_t from_index, std::size_t to_index)>;

    ItemsControl();
    ~ItemsControl() override;

    ItemsControl& set_items(std::vector<std::string> items);
    ItemsControl& bind_items(ItemsSource source);
    ItemsControl& set_item_factory(ItemFactory factory);
    ItemsControl& set_selection_mode(SelectionMode mode);
    ItemsControl& set_selected_index(std::optional<std::size_t> index);
    ItemsControl& set_selected_indices(std::vector<std::size_t> indices);
    ItemsControl& set_on_selection_changed(SelectionChangedHandler handler);
    ItemsControl& set_on_multi_selection_changed(MultiSelectionChangedHandler handler);
    ItemsControl& set_on_reorder(ReorderHandler handler);
    ItemsControl& set_virtualized(bool virtualized);
    ItemsControl& set_realized_range(std::size_t start_index, std::size_t count);
    ItemsControl& set_virtualization_window(float scroll_offset, float viewport_extent,
                                            float item_extent, std::size_t overscan = 2U);
    ItemsControl& set_virtualization_window(float scroll_offset, float viewport_extent,
                                            std::vector<float> item_extents,
                                            std::size_t overscan = 2U);
    ItemsControl& set_groups(std::vector<ItemGroup> groups);
    ItemsControl& clear_groups();
    ItemsControl& refresh_items();
    ItemsControl& set_style(style::UIElementStyle style) override;
    [[nodiscard]] const std::vector<std::string>& items() const noexcept;
    [[nodiscard]] SelectionMode selection_mode() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
    [[nodiscard]] std::vector<std::size_t> selected_indices() const;
    [[nodiscard]] const std::vector<ItemGroup>& groups() const noexcept;
    [[nodiscard]] bool virtualized() const noexcept;
    [[nodiscard]] std::size_t realized_start_index() const noexcept;
    [[nodiscard]] std::size_t realized_count() const noexcept;

  private:
    friend class ItemsControlItemContainer;

    void rebuild_children();
    void update_container(elements::UIElement& container, std::size_t item_index);
    void update_realized_children();
    void select_index_from_item(std::size_t index);
    void reorder_from_item(std::size_t from_index, std::size_t to_index);
    [[nodiscard]] bool is_index_selected(std::size_t index) const;
    [[nodiscard]] std::unique_ptr<elements::UIElement> create_item_content(ItemContext context);
    [[nodiscard]] std::unique_ptr<ItemsControlItemContainer> take_reusable_container();
    void recycle_container(std::unique_ptr<elements::UIElement> container);

    std::vector<std::string> items_;
    ItemsSource items_source_;
    ItemFactory item_factory_;
    SelectionChangedHandler selection_changed_handler_;
    MultiSelectionChangedHandler multi_selection_changed_handler_;
    ReorderHandler reorder_handler_;
    SelectionMode selection_mode_ = SelectionMode::Single;
    std::optional<std::size_t> selected_index_;
    std::unordered_set<std::size_t> selected_indices_;
    std::vector<ItemGroup> groups_;
    std::vector<std::unique_ptr<ItemsControlItemContainer>> reusable_containers_;
    std::size_t realized_start_index_ = 0;
    std::size_t realized_count_ = 0;
    bool virtualized_ = false;
    VirtualizationPlanner virtualization_planner_;
};

} // namespace winelement::controls
