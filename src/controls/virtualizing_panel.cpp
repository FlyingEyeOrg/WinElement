#include <winelement/controls/virtualizing_panel.hpp>

#include <algorithm>

namespace winelement::controls {

VirtualizingPanel::VirtualizingPanel() : Panel() {
    set_overflow(layout::Overflow::Hidden);
    set_scroll_wheel_enabled(true);
}

VirtualizingPanel& VirtualizingPanel::set_item_count(std::size_t count) {
    planner_.set_total_count(count);
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_item_extent(float extent) {
    planner_.set_item_extent(extent);
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_item_extents(std::vector<float> extents) {
    planner_.set_item_extents(std::move(extents));
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_overscan(std::size_t overscan) {
    planner_.set_overscan(overscan);
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_item_factory(ItemFactory factory) {
    item_factory_ = std::move(factory);
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_reusable_container_limit(std::size_t limit) {
    pool_capacity_ = std::max(limit, std::size_t{4});
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_scroll_offset(float offset) {
    scroll_offset_ = std::max(0.0F, offset);
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_viewport_extent(float extent) {
    viewport_extent_ = std::max(0.0F, extent);
    return *this;
}

VirtualizingPanel& VirtualizingPanel::refresh_virtualization() {
    if (planner_.total_count() == 0 || viewport_extent_ <= 0.0F) {
        return *this;
    }

    ensure_pool();

    const auto window =
        planner_.window_for(scroll_offset_, viewport_extent_);
    const auto item_extent = planner_.item_extent();

    for (std::size_t slot = 0; slot < pool_.size(); ++slot) {
        if (slot < window.count) {
            const auto item_index = window.start_index + slot;
            const auto y = static_cast<float>(item_index) * item_extent;
            auto& s = pool_[slot];

            if (s.item_index != item_index) {
                bind_slot(slot, item_index);
            }
            s.element->set_visible(true);
            s.element->set_render_transform(
                rendering::Transform2D::translation(0.0F, y));
        } else {
            auto& s = pool_[slot];
            if (s.item_index.has_value()) {
                unbind_slot(slot);
            }
            s.element->set_visible(false);
        }
    }

    return *this;
}

std::size_t VirtualizingPanel::item_count() const noexcept {
    return planner_.total_count();
}

std::size_t VirtualizingPanel::realized_count() const noexcept {
    std::size_t count = 0;
    for (const auto& s : pool_) {
        if (s.item_index.has_value()) {
            ++count;
        }
    }
    return count;
}

void VirtualizingPanel::on_viewport_enter() {
    Panel::on_viewport_enter();
}

void VirtualizingPanel::on_viewport_leave() {
    Panel::on_viewport_leave();
}

void VirtualizingPanel::ensure_pool() {
    if (!pool_.empty()) {
        return;
    }

    const auto item_extent = planner_.item_extent();
    const auto total_height =
        static_cast<float>(planner_.total_count()) * item_extent;

    configure_layout([total_height](layout::LayoutElement& item) {
        item.set_flex_direction(layout::FlexDirection::Column)
            .set_align_items(layout::Align::Stretch)
            .set_height(layout::Length::points(total_height))
            .set_flex_shrink(0.0F);
    });

    pool_.reserve(pool_capacity_);
    for (std::size_t i = 0; i < pool_capacity_; ++i) {
        auto element = std::make_unique<elements::UIElement>();
        element->set_visible(false);
        element->configure_layout([item_extent](layout::LayoutElement& item) {
            item.set_position_type(layout::PositionType::Absolute)
                .set_position(layout::Edge::Left, layout::Length::points(0.0F))
                .set_position(layout::Edge::Top, layout::Length::points(0.0F))
                .set_width(layout::Length::percent(100.0F))
                .set_height(layout::Length::points(item_extent))
                .set_flex_shrink(0.0F);
        });
        auto* element_ptr = element.get();
        append_child(std::move(element));
        pool_.push_back(Slot{element_ptr, std::nullopt});
    }
}

void VirtualizingPanel::bind_slot(std::size_t slot_index, std::size_t item_index) {
    auto& s = pool_[slot_index];
    unbind_slot(slot_index);

    if (item_factory_) {
        auto content = item_factory_(item_index);
        if (content != nullptr) {
            s.element->append_child(std::move(content));
            s.item_index = item_index;
        }
    }
}

void VirtualizingPanel::unbind_slot(std::size_t slot_index) {
    auto& s = pool_[slot_index];
    if (s.item_index.has_value()) {
        s.element->clear_children();
        s.item_index = std::nullopt;
    }
}

}  // namespace winelement::controls
