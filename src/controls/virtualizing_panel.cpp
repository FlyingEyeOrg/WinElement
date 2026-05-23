#include <winelement/controls/virtualizing_panel.hpp>

#include <algorithm>

namespace {

constexpr auto scroll_wheel_step = 48.0F;

}  // namespace

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

VirtualizingPanel& VirtualizingPanel::set_slot_factory(SlotFactory factory) {
    slot_factory_ = std::move(factory);
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_item_binder(ItemBinder binder) {
    item_binder_ = std::move(binder);
    return *this;
}

VirtualizingPanel& VirtualizingPanel::set_pool_capacity(std::size_t capacity) {
    pool_capacity_ = std::max(capacity, std::size_t{4});
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

    const auto window =
        planner_.window_for(scroll_offset_, viewport_extent_);
    const auto item_extent = planner_.item_extent();

    ensure_pool(window.count);

    for (std::size_t slot = 0; slot < pool_.size(); ++slot) {
        auto& s = pool_[slot];

        if (slot < window.count) {
            const auto item_index = window.start_index + slot;
            const auto y = static_cast<float>(item_index) * item_extent;

            if (s.item_index != item_index && item_binder_) {
                item_binder_(*s.element, item_index);
                s.item_index = item_index;
            }
            if (!s.element->visible()) {
                s.element->set_visible(true);
            }
            s.element->set_render_transform(
                rendering::Transform2D::translation(0.0F, y));
        } else {
            if (s.element->visible()) {
                s.element->set_visible(false);
            }
        }
    }

    return *this;
}

std::size_t VirtualizingPanel::item_count() const noexcept {
    return planner_.total_count();
}

void VirtualizingPanel::on_viewport_enter() {
    Panel::on_viewport_enter();
    refresh_virtualization();
}

void VirtualizingPanel::on_viewport_leave() {
    Panel::on_viewport_leave();
}

void VirtualizingPanel::on_pointer_event(elements::PointerEvent& event) {
    if (event.kind == elements::PointerEventKind::Wheel ||
        event.kind == elements::PointerEventKind::HorizontalWheel) {
        if (!scroll_wheel_enabled()) {
            Panel::on_pointer_event(event);
            return;
        }

        const auto direction =
            event.kind == elements::PointerEventKind::Wheel ? event.wheel_delta.y
                                                             : event.wheel_delta.x;
        const auto step = direction * scroll_wheel_step;
        const auto max_scroll =
            static_cast<float>(planner_.total_count()) * planner_.item_extent() -
            viewport_extent_;
        const auto previous = scroll_offset_;
        scroll_offset_ =
            std::clamp(scroll_offset_ - step, 0.0F, std::max(0.0F, max_scroll));
        if (scroll_offset_ != previous) {
            refresh_virtualization();
            event.handled = true;
        }
    } else {
        Panel::on_pointer_event(event);
    }
}

void VirtualizingPanel::ensure_pool(std::size_t required_count) {
    if (pool_.empty()) {
        const auto item_extent = planner_.item_extent();
        const auto total_height =
            static_cast<float>(planner_.total_count()) * item_extent;

        configure_layout([total_height](layout::LayoutElement& item) {
            item.set_flex_direction(layout::FlexDirection::Column)
                .set_align_items(layout::Align::Stretch)
                .set_width(layout::Length::percent(100.0F))
                .set_height(layout::Length::points(total_height))
                .set_flex_shrink(0.0F);
        });
    }

    const auto needed =
        std::min(std::max(required_count, pool_capacity_), planner_.total_count());
    if (pool_.size() >= needed) {
        return;
    }

    const auto item_extent = planner_.item_extent();
    const auto start = pool_.size();
    pool_.reserve(needed);
    for (std::size_t i = start; i < needed; ++i) {
        auto element = slot_factory_ ? slot_factory_()
                                     : std::make_unique<elements::UIElement>();
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

}  // namespace winelement::controls
