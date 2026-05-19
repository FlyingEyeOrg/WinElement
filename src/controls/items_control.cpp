#include <winelement/controls/items_control.hpp>

#include <winelement/controls/text.hpp>
#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_map>
#include <utility>

namespace winelement::controls {

class ItemsControlItemContainer final : public elements::UIElement {
  public:
    ItemsControlItemContainer(ItemsControl& owner, std::size_t item_index)
        : owner_(owner), item_index_(item_index), selected_(false) {
        set_focusable(true);
        configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::percent(100.0F))
                .set_min_height(layout::Length::points(36.0F))
                .set_padding(layout::Edge::Left, layout::Length::points(10.0F))
                .set_padding(layout::Edge::Right, layout::Length::points(10.0F))
                .set_padding(layout::Edge::Top, layout::Length::points(8.0F))
                .set_padding(layout::Edge::Bottom, layout::Length::points(8.0F));
        });
    }
    [[nodiscard]] std::size_t item_index() const noexcept {
        return item_index_;
    }
    void set_item_index(std::size_t item_index) noexcept {
        item_index_ = item_index;
    }
    void set_selected(bool selected) {
        if (selected_ != selected) {
            selected_ = selected;
            invalidate_paint();
        }
    }

  protected:
    void on_pointer_event(elements::PointerEvent& event) override {
        if (event.kind == elements::PointerEventKind::Enter) {
            hovered_ = true;
            invalidate_paint();
            return;
        }
        if (event.kind == elements::PointerEventKind::Leave) {
            hovered_ = false;
            if (!pressed_) {
                invalidate_paint();
            }
            return;
        }
        if (event.button != elements::PointerButton::Primary) {
            return;
        }
        if (event.kind == elements::PointerEventKind::Down ||
            event.kind == elements::PointerEventKind::DoubleClick) {
            pressed_ = true;
            static_cast<void>(capture_pointer());
            invalidate_paint();
            event.handled = true;
            return;
        }
        if (event.kind == elements::PointerEventKind::Move && pressed_) {
            event.handled = true;
            return;
        }
        if (event.kind == elements::PointerEventKind::Up) {
            const auto reorder_target = target_index_for_drag(event.local_position);
            pressed_ = false;
            release_pointer_capture();
            invalidate_paint();
            if (reorder_target && *reorder_target != item_index_) {
                owner_.reorder_from_item(item_index_, *reorder_target);
            } else {
                owner_.select_index_from_item(item_index_);
            }
            event.handled = true;
            return;
        }
        if (event.kind == elements::PointerEventKind::Cancel) {
            pressed_ = false;
            release_pointer_capture();
            invalidate_paint();
            event.handled = true;
        }
    }

    void on_key_event(elements::KeyEvent& event) override {
        if (event.kind != elements::KeyEventKind::Down) {
            return;
        }
        if (event.key == elements::Key::Enter || event.key == elements::Key::Space) {
            owner_.select_index_from_item(item_index_);
            event.handled = true;
            return;
        }
        if (event.modifiers.alt && event.key == elements::Key::Up && item_index_ > 0U) {
            owner_.reorder_from_item(item_index_, item_index_ - 1U);
            event.handled = true;
            return;
        }
        if (event.modifiers.alt && event.key == elements::Key::Down &&
            item_index_ + 1U < owner_.items_.size()) {
            owner_.reorder_from_item(item_index_, item_index_ + 1U);
            event.handled = true;
        }
    }

    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override {
        const auto style = owner_.style();
        const auto radius = rendering::CornerRadius::uniform(6.0F);
        const auto hover_background = rendering::Color::rgba(style.hover_background.red,
                                                             style.hover_background.green,
                                                             style.hover_background.blue, 132);
        const auto active_background = rendering::Color::rgba(style.active_background.red,
                                                              style.active_background.green,
                                                              style.active_background.blue, 188);
        if (pressed_) {
            context.fill_rounded_rect(absolute_frame, radius, active_background);
        } else if (selected_) {
            context.fill_rounded_rect(absolute_frame, radius, style.active_background);
        } else if (hovered_) {
            context.fill_rounded_rect(absolute_frame, radius, hover_background);
        }

        if (selected_ || focused() || hovered_) {
            context.stroke_rounded_rect(
                absolute_frame, radius, selected_ || focused() ? style.focus_border_color
                                                               : style.border_color,
                std::max(style.border_width, 1.0F));
        }
    }

  private:
    [[nodiscard]] std::optional<std::size_t>
    target_index_for_drag(layout::Point local_position) const noexcept {
        if (owner_.items_.empty()) {
            return std::nullopt;
        }
        const auto item_extent = std::max(frame().height, 1.0F);
        const auto delta = static_cast<int>(std::floor(local_position.y / item_extent));
        const auto raw_target = static_cast<int>(item_index_) + delta;
        const auto clamped = std::clamp(raw_target, 0, static_cast<int>(owner_.items_.size() - 1U));
        return static_cast<std::size_t>(clamped);
    }

    ItemsControl& owner_;
    std::size_t item_index_ = 0;
    bool selected_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
};

namespace {

void configure_items_layout(elements::UIElement& element) {
    element.configure_layout([](layout::LayoutElement& item) {
        item.set_flex_direction(layout::FlexDirection::Column)
            .set_gap(layout::Gutter::Row, layout::Length::points(6.0F))
            .set_padding(layout::Edge::All, layout::Length::points(8.0F));
    });
}

} // namespace

ItemsControl::ItemsControl() : Control() {
    apply_style_value(style::default_items_control_style(), true);
    set_theme_class(style::theme_class::items_control);
    configure_items_layout(*this);
}

ItemsControl::~ItemsControl() = default;

ItemsControl& ItemsControl::set_items(std::vector<std::string> items) {
    items_source_ = nullptr;
    items_ = std::move(items);
    if (selected_index_ && *selected_index_ >= items_.size()) {
        selected_index_.reset();
    }
    for (auto iterator = selected_indices_.begin(); iterator != selected_indices_.end();) {
        if (*iterator >= items_.size()) {
            iterator = selected_indices_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    rebuild_children();
    return *this;
}

ItemsControl& ItemsControl::bind_items(ItemsSource source) {
    items_source_ = std::move(source);
    return refresh_items();
}

ItemsControl& ItemsControl::set_item_factory(ItemFactory factory) {
    item_factory_ = std::move(factory);
    rebuild_children();
    return *this;
}

ItemsControl& ItemsControl::set_selection_mode(SelectionMode mode) {
    if (selection_mode_ == mode) {
        return *this;
    }
    selection_mode_ = mode;
    if (selection_mode_ == SelectionMode::None) {
        set_selected_index(std::nullopt);
        set_selected_indices({});
    } else {
        rebuild_children();
    }
    return *this;
}

ItemsControl& ItemsControl::set_selected_index(std::optional<std::size_t> index) {
    if (selection_mode_ == SelectionMode::None) {
        index.reset();
    } else if (selection_mode_ == SelectionMode::Multiple && index) {
        return set_selected_indices({*index});
    }
    if (index && *index >= items_.size()) {
        index.reset();
    }
    if (selected_index_ == index) {
        return *this;
    }
    selected_index_ = index;
    for (std::size_t i = 0; i < child_count(); ++i) {
        auto* container = dynamic_cast<ItemsControlItemContainer*>(&child_at(i));
        if (container != nullptr) {
            container->set_selected(is_index_selected(container->item_index()));
        }
    }
    if (selection_changed_handler_) {
        selection_changed_handler_(selected_index_);
    }
    return *this;
}

ItemsControl& ItemsControl::set_selected_indices(std::vector<std::size_t> indices) {
    std::unordered_set<std::size_t> next;
    if (selection_mode_ == SelectionMode::Multiple) {
        for (const auto index : indices) {
            if (index < items_.size()) {
                next.insert(index);
            }
        }
    } else if (selection_mode_ == SelectionMode::Single && !indices.empty()) {
        return set_selected_index(indices.front());
    }
    if (selected_indices_ == next) {
        return *this;
    }
    selected_indices_ = std::move(next);
    selected_index_ = selected_indices_.empty()
                          ? std::optional<std::size_t>{}
                          : std::optional<std::size_t>{*selected_indices_.begin()};
    for (std::size_t i = 0; i < child_count(); ++i) {
        auto* container = dynamic_cast<ItemsControlItemContainer*>(&child_at(i));
        if (container != nullptr) {
            container->set_selected(is_index_selected(container->item_index()));
        }
    }
    if (multi_selection_changed_handler_) {
        multi_selection_changed_handler_(selected_indices());
    }
    if (selection_changed_handler_) {
        selection_changed_handler_(selected_index_);
    }
    return *this;
}

ItemsControl& ItemsControl::set_on_selection_changed(SelectionChangedHandler handler) {
    selection_changed_handler_ = std::move(handler);
    return *this;
}

ItemsControl& ItemsControl::set_on_multi_selection_changed(MultiSelectionChangedHandler handler) {
    multi_selection_changed_handler_ = std::move(handler);
    return *this;
}

ItemsControl& ItemsControl::set_on_reorder(ReorderHandler handler) {
    reorder_handler_ = std::move(handler);
    return *this;
}

ItemsControl& ItemsControl::set_virtualized(bool virtualized) {
    if (virtualized_ == virtualized) {
        return *this;
    }
    virtualized_ = virtualized;
    rebuild_children();
    return *this;
}

ItemsControl& ItemsControl::set_reusable_container_limit(std::size_t limit) {
    reusable_container_limit_ = limit;
    if (reusable_containers_.size() > reusable_container_limit_) {
        reusable_containers_.resize(reusable_container_limit_);
    }
    return *this;
}

ItemsControl& ItemsControl::set_realized_range(std::size_t start_index, std::size_t count) {
    realized_start_index_ = std::min(start_index, items_.size());
    realized_count_ = count;
    update_realized_children();
    return *this;
}

ItemsControl& ItemsControl::set_virtualization_window(float scroll_offset, float viewport_extent,
                                                      float item_extent, std::size_t overscan) {
    virtualization_planner_.set_total_count(items_.size());
    virtualization_planner_.set_item_extent(item_extent);
    virtualization_planner_.set_overscan(overscan);
    const auto window = virtualization_planner_.window_for(scroll_offset, viewport_extent);
    virtualized_ = true;
    return set_realized_range(window.start_index, window.count);
}

ItemsControl& ItemsControl::set_virtualization_window(float scroll_offset, float viewport_extent,
                                                      std::vector<float> item_extents,
                                                      std::size_t overscan) {
    virtualization_planner_.set_total_count(items_.size());
    virtualization_planner_.set_item_extents(std::move(item_extents));
    virtualization_planner_.set_overscan(overscan);
    const auto window = virtualization_planner_.window_for(scroll_offset, viewport_extent);
    virtualized_ = true;
    return set_realized_range(window.start_index, window.count);
}

ItemsControl& ItemsControl::set_groups(std::vector<ItemGroup> groups) {
    groups_ = std::move(groups);
    std::sort(groups_.begin(), groups_.end(), [](const ItemGroup& lhs, const ItemGroup& rhs) {
        return lhs.start_index < rhs.start_index;
    });
    return *this;
}

ItemsControl& ItemsControl::clear_groups() {
    groups_.clear();
    return *this;
}

ItemsControl& ItemsControl::refresh_items() {
    if (items_source_) {
        items_ = items_source_();
        if (selected_index_ && *selected_index_ >= items_.size()) {
            selected_index_.reset();
        }
        for (auto iterator = selected_indices_.begin(); iterator != selected_indices_.end();) {
            if (*iterator >= items_.size()) {
                iterator = selected_indices_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }
    update_realized_children();
    return *this;
}

ItemsControl& ItemsControl::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    return *this;
}

const std::vector<std::string>& ItemsControl::items() const noexcept {
    return items_;
}

std::size_t ItemsControl::item_count() const noexcept {
    return items_.size();
}

ItemsControl::SelectionMode ItemsControl::selection_mode() const noexcept {
    return selection_mode_;
}

std::optional<std::size_t> ItemsControl::selected_index() const noexcept {
    return selected_index_;
}

std::vector<std::size_t> ItemsControl::selected_indices() const {
    std::vector<std::size_t> indices(selected_indices_.begin(), selected_indices_.end());
    std::sort(indices.begin(), indices.end());
    return indices;
}

const std::vector<ItemsControl::ItemGroup>& ItemsControl::groups() const noexcept {
    return groups_;
}

bool ItemsControl::virtualized() const noexcept {
    return virtualized_;
}

std::size_t ItemsControl::realized_start_index() const noexcept {
    return realized_start_index_;
}

std::size_t ItemsControl::realized_end_index() const noexcept {
    return std::min(realized_start_index_ + realized_count(), items_.size());
}

std::size_t ItemsControl::realized_count() const noexcept {
    const auto start = std::min(realized_start_index_, items_.size());
    return virtualized_ ? std::min(realized_count_, items_.size() - start)
                        : items_.size();
}

std::size_t ItemsControl::reusable_container_count() const noexcept {
    return reusable_containers_.size();
}

std::size_t ItemsControl::reusable_container_limit() const noexcept {
    return reusable_container_limit_;
}

void ItemsControl::rebuild_children() {
    while (child_count() > 0U) {
        recycle_container(remove_child_at(child_count() - 1U));
    }
    update_realized_children();
}

void ItemsControl::update_container(elements::UIElement& element, std::size_t item_index) {
    auto& container = static_cast<ItemsControlItemContainer&>(element);
    container.set_item_index(item_index);
    const auto selected = is_index_selected(item_index);
    container.set_selected(selected);
    container.set_style(style_storage());
    container.clear_children();
    auto content = create_item_content(
        ItemContext{.item = items_[item_index], .index = item_index, .selected = selected});
    if (content) {
        content->configure_layout(
            [](layout::LayoutElement& item) { item.set_width(layout::Length::percent(100.0F)); });
        container.append_child(std::move(content));
    }
}

void ItemsControl::update_realized_children() {
    const auto start = virtualized_ ? std::min(realized_start_index_, items_.size()) : 0U;
    const auto count =
        virtualized_ ? std::min(realized_count_, items_.size() - start) : items_.size();

    std::unordered_map<std::size_t, std::unique_ptr<elements::UIElement>> survivors;
    survivors.reserve(count);
    while (child_count() > 0U) {
        auto child = remove_child_at(0U);
        auto* container = dynamic_cast<ItemsControlItemContainer*>(child.get());
        if (container != nullptr && container->item_index() >= start &&
            container->item_index() < start + count) {
            survivors.emplace(container->item_index(), std::move(child));
        } else {
            recycle_container(std::move(child));
        }
    }

    for (std::size_t visible_index = 0; visible_index < count; ++visible_index) {
        const auto item_index = start + visible_index;
        auto existing = survivors.find(item_index);
        std::unique_ptr<elements::UIElement> container;
        if (existing != survivors.end()) {
            container = std::move(existing->second);
            survivors.erase(existing);
        } else {
            auto reusable = take_reusable_container();
            if (reusable) {
                container = std::move(reusable);
            } else {
                container = make_child<ItemsControlItemContainer>(*this, item_index);
            }
        }
        update_container(*container, item_index);
        append_child(std::move(container));
    }

    for (auto& [item_index, child] : survivors) {
        static_cast<void>(item_index);
        recycle_container(std::move(child));
    }
}

void ItemsControl::select_index_from_item(std::size_t index) {
    if (selection_mode_ == SelectionMode::Single) {
        set_selected_index(index);
    } else if (selection_mode_ == SelectionMode::Multiple) {
        auto indices = selected_indices();
        const auto iterator = std::find(indices.begin(), indices.end(), index);
        if (iterator == indices.end()) {
            indices.push_back(index);
        } else {
            indices.erase(iterator);
        }
        set_selected_indices(std::move(indices));
    }
}

void ItemsControl::reorder_from_item(std::size_t from_index, std::size_t to_index) {
    if (from_index >= items_.size() || to_index >= items_.size() || from_index == to_index) {
        return;
    }
    if (reorder_handler_) {
        reorder_handler_(from_index, to_index);
    }
}

bool ItemsControl::is_index_selected(std::size_t index) const {
    if (selection_mode_ == SelectionMode::Multiple) {
        return selected_indices_.find(index) != selected_indices_.end();
    }
    return selected_index_ == index;
}

std::unique_ptr<elements::UIElement> ItemsControl::create_item_content(ItemContext context) {
    if (item_factory_) {
        return item_factory_(context);
    }

    auto item = make_child<Text>();
    item->set_text(context.item);
    item->set_style(style::default_text_style());
    item->set_color(context.selected ? rendering::Color::rgba(48, 49, 51)
                                     : rendering::Color::rgba(96, 98, 102));
    return item;
}

std::unique_ptr<ItemsControlItemContainer> ItemsControl::take_reusable_container() {
    if (reusable_containers_.empty()) {
        return nullptr;
    }
    auto container = std::move(reusable_containers_.back());
    reusable_containers_.pop_back();
    return container;
}

void ItemsControl::recycle_container(std::unique_ptr<elements::UIElement> container) {
    if (!container) {
        return;
    }
    auto* raw_container = dynamic_cast<ItemsControlItemContainer*>(container.get());
    if (raw_container == nullptr) {
        return;
    }
    if (reusable_containers_.size() >= reusable_container_limit_) {
        return;
    }
    container.release();
    raw_container->clear_children();
    reusable_containers_.push_back(std::unique_ptr<ItemsControlItemContainer>(raw_container));
}

} // namespace winelement::controls
