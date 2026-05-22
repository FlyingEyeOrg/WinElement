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
    snapshots_.clear();
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
    if (planner_.total_count() == 0 || viewport_extent_ <= 0.0F) {
        return *this;
    }

    ensure_spacers();

    const auto window =
        planner_.window_for(scroll_offset_, viewport_extent_);

    update_spacer_extent(*leading_spacer_, window.leading_spacer);

    std::vector<RealizedItem> keep;
    keep.reserve(realized_.size());

    for (auto& item : realized_) {
        if (item.index >= window.start_index &&
            item.index < window.start_index + window.count) {
            keep.push_back(item);
        } else {
            if (item.element != nullptr) {
                snapshots_[item.index] = item.element->compress_subtree();
                item.element->clear_children();
                const auto child_idx = [this, ptr = item.element]() -> std::size_t {
                    for (std::size_t i = 0; i < child_count(); ++i) {
                        if (&child_at(i) == ptr) return i;
                    }
                    return child_count();
                }();
                auto elem = remove_child_at(child_idx);
                recycle_pool_.release(std::move(elem));
            }
        }
    }

    std::swap(realized_, keep);

    const auto base_index = leading_spacer_index() + 1;

    for (std::size_t i = 0; i < window.count; ++i) {
        const auto idx = window.start_index + i;

        bool already_realized = false;
        for (const auto& item : realized_) {
            if (item.index == idx) {
                already_realized = true;
                break;
            }
        }
        if (already_realized) {
            continue;
        }

        auto element = acquire_reusable(idx);
        if (element == nullptr) {
            continue;
        }

        element->configure_layout([this](layout::LayoutElement& item) {
            item.set_width(layout::Length::percent(100.0F))
                .set_height(layout::Length::points(planner_.item_extent()))
                .set_flex_shrink(0.0F);
        });

        auto snapshot_it = snapshots_.find(idx);
        if (snapshot_it != snapshots_.end()) {
            const auto& snap = snapshot_it->second;
            if (!snap.text_content.empty() || snap.has_text_selection ||
                snap.scroll_offset.x != 0.0F || snap.scroll_offset.y != 0.0F) {
                element->decompress_subtree(snap);
            }
        }

        auto* element_ptr = element.get();
        const auto insert_index = base_index + realized_.size();
        insert_child(insert_index, std::move(element));
        realized_.push_back(RealizedItem{idx, element_ptr});
    }

    update_spacer_extent(*trailing_spacer_, window.trailing_spacer);

    return *this;
}

std::size_t VirtualizingPanel::item_count() const noexcept {
    return planner_.total_count();
}

std::size_t VirtualizingPanel::realized_count() const noexcept {
    return realized_.size();
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

void VirtualizingPanel::ensure_spacers() {
    if (spacers_ready_) {
        return;
    }

    auto leading = std::make_unique<elements::UIElement>();
    leading->configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_height(layout::Length::points(0.0F))
            .set_flex_shrink(0.0F);
    });
    leading_spacer_ = leading.get();
    append_child(std::move(leading));

    auto trailing = std::make_unique<elements::UIElement>();
    trailing->configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_height(layout::Length::points(0.0F))
            .set_flex_shrink(0.0F);
    });
    trailing_spacer_ = trailing.get();
    append_child(std::move(trailing));

    spacers_ready_ = true;
}

void VirtualizingPanel::update_spacer_extent(elements::UIElement& spacer, float extent) {
    spacer.configure_layout([extent](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_height(layout::Length::points(std::max(0.0F, extent)))
            .set_flex_shrink(0.0F);
    });
}

std::unique_ptr<elements::UIElement>
VirtualizingPanel::acquire_reusable(std::size_t index) {
    auto element = recycle_pool_.acquire();
    if (element == nullptr && item_factory_) {
        element = item_factory_(index);
    }
    return element;
}

std::size_t VirtualizingPanel::leading_spacer_index() const noexcept {
    for (std::size_t i = 0; i < child_count(); ++i) {
        if (&child_at(i) == leading_spacer_) {
            return i;
        }
    }
    return 0;
}

}  // namespace winelement::controls
