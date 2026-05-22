#include <winelement/controls/virtualizing_panel.hpp>

#include <algorithm>

namespace winelement::controls {

VirtualizingPanel::VirtualizingPanel() : Panel() {
    configure_layout([](layout::LayoutElement& item) {
        item.set_flex_direction(layout::FlexDirection::Column)
            .set_align_items(layout::Align::Stretch);
    });
    set_overflow(layout::Overflow::Hidden);
    set_scroll_wheel_enabled(true);
}

VirtualizingPanel& VirtualizingPanel::set_item_count(std::size_t count) {
    planner_.set_total_count(count);
    build_slots();
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_item_extent(float extent) {
    planner_.set_item_extent(extent);
    for (auto& slot : slots_) {
        slot.extent = extent;
    }
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_item_extents(std::vector<float> extents) {
    planner_.set_item_extents(extents);
    for (std::size_t i = 0; i < extents.size() && i < slots_.size(); ++i) {
        slots_[i].extent = extents[i];
    }
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
    recycle_pool_.set_capacity(limit);
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
    if (slots_.empty() || viewport_extent_ <= 0.0F) {
        return *this;
    }

    const auto window =
        planner_.window_for(scroll_offset_, viewport_extent_);

    for (std::size_t i = 0; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        const auto in_window =
            i >= window.start_index && i < window.start_index + window.count;

        if (in_window && slot.realized == nullptr && item_factory_) {
            realize_slot(slot);
        } else if (!in_window && slot.realized != nullptr) {
            unrealize_slot(slot);
        }
    }

    return *this;
}

std::size_t VirtualizingPanel::item_count() const noexcept {
    return planner_.total_count();
}

std::size_t VirtualizingPanel::realized_start_index() const noexcept {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].realized != nullptr) {
            return i;
        }
    }
    return 0;
}

std::size_t VirtualizingPanel::realized_count() const noexcept {
    std::size_t count = 0;
    for (const auto& slot : slots_) {
        if (slot.realized != nullptr) {
            ++count;
        }
    }
    return count;
}

std::size_t VirtualizingPanel::reusable_container_count() const noexcept {
    return recycle_pool_.size();
}

void VirtualizingPanel::on_viewport_enter() {
    Panel::on_viewport_enter();
    refresh_virtualization();
}

void VirtualizingPanel::on_viewport_leave() {
    Panel::on_viewport_leave();
}

void VirtualizingPanel::realize_slot(Slot& slot) {
    if (slot.realized != nullptr || slot.spacer == nullptr) {
        return;
    }

    auto element = acquire_reusable();
    if (element == nullptr && item_factory_) {
        element = item_factory_(slot.item_index);
    }
    if (element == nullptr) {
        return;
    }

    element->configure_layout([extent = slot.extent](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_height(layout::Length::points(extent))
            .set_flex_shrink(0.0F);
    });

    if (!slot.snapshot.text_content.empty() || slot.snapshot.has_text_selection ||
        slot.snapshot.scroll_offset.x != 0.0F || slot.snapshot.scroll_offset.y != 0.0F) {
        element->decompress_subtree(slot.snapshot);
    }

    const auto spacer_index = [this, &slot]() -> std::size_t {
        for (std::size_t i = 0; i < child_count(); ++i) {
            if (&child_at(i) == slot.spacer) {
                return i;
            }
        }
        return child_count();
    }();

    slot.realized = element.get();
    remove_child(*slot.spacer);
    insert_child(spacer_index, std::move(element));
    slot.spacer = nullptr;
}

void VirtualizingPanel::unrealize_slot(Slot& slot) {
    if (slot.realized == nullptr) {
        return;
    }

    slot.snapshot = slot.realized->compress_subtree();

    const auto realized_index = [this, &slot]() -> std::size_t {
        for (std::size_t i = 0; i < child_count(); ++i) {
            if (&child_at(i) == slot.realized) {
                return i;
            }
        }
        return child_count();
    }();

    auto spacer = std::make_unique<elements::UIElement>();
    spacer->configure_layout([extent = slot.extent](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_height(layout::Length::points(extent))
            .set_flex_shrink(0.0F);
    });

    slot.spacer = spacer.get();
    auto element = remove_child_at(realized_index);
    insert_child(realized_index, std::move(spacer));
    slot.realized = nullptr;

    release_reusable(std::move(element));
}

void VirtualizingPanel::build_slots() {
    const auto count = planner_.total_count();
    clear_children();
    slots_.clear();
    slots_.reserve(count);

    const auto extent = planner_.item_extent();
    for (std::size_t i = 0; i < count; ++i) {
        auto spacer = std::make_unique<elements::UIElement>();
        spacer->configure_layout([extent](layout::LayoutElement& item) {
            item.set_width(layout::Length::percent(100.0F))
                .set_height(layout::Length::points(extent))
                .set_flex_shrink(0.0F);
        });

        auto& slot = slots_.emplace_back();
        slot.item_index = i;
        slot.extent = extent;
        slot.spacer = spacer.get();
        append_child(std::move(spacer));
    }
}

std::unique_ptr<elements::UIElement> VirtualizingPanel::acquire_reusable() {
    return recycle_pool_.acquire();
}

void VirtualizingPanel::release_reusable(std::unique_ptr<elements::UIElement> element) {
    if (element != nullptr) {
        element->clear_children();
        recycle_pool_.release(std::move(element));
    }
}

}  // namespace winelement::controls
