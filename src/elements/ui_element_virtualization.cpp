#include <winelement/elements/ui_element.hpp>

#include "ui_element_virtualization_state.hpp"

#include <winelement/elements/event_router.hpp>
#include <winelement/elements/focus_manager.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace winelement::elements {

namespace {

constexpr auto default_virtualization_min_overscan = 480.0F;
constexpr auto default_virtualization_viewport_overscan_ratio = 0.75F;

[[nodiscard]] bool rect_equals(layout::Rect lhs, layout::Rect rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
           lhs.height == rhs.height;
}

[[nodiscard]] bool point_equals(layout::Point lhs, layout::Point rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

void configure_virtual_extent(layout::LayoutElement& item,
                              UIElement::VirtualChildrenOrientation orientation,
                              float extent) {
    const auto safe_extent = std::max(std::isfinite(extent) ? extent : 0.0F, 0.0F);
    item.set_flex_shrink(0.0F);
    if (orientation == UIElement::VirtualChildrenOrientation::Vertical) {
        item.set_width(layout::Length::percent(100.0F))
            .set_height(layout::Length::points(safe_extent));
    } else {
        item.set_width(layout::Length::points(safe_extent))
            .set_height(layout::Length::percent(100.0F));
    }
}

[[nodiscard]] std::unique_ptr<UIElement>
make_virtual_spacer(UIElement::VirtualChildrenOrientation orientation, float extent) {
    auto spacer = std::make_unique<UIElement>();
    spacer->set_hit_test_visible(false)
        .set_subtree_virtualization_enabled(false)
        .configure_layout([orientation, extent](layout::LayoutElement& item) {
            configure_virtual_extent(item, orientation, extent);
        });
    return spacer;
}

} // namespace

UIElement& UIElement::set_subtree_virtualization_enabled(bool enabled) {
    verify_thread_access();
    if (subtree_virtualization_enabled_ == enabled) {
        return *this;
    }
    subtree_virtualization_enabled_ = enabled;
    if (!enabled) {
        for (auto& child : children_) {
            child->set_subtree_virtualized(false);
        }
    } else {
        refresh_virtualization_from_host();
    }
    invalidate_paint();
    return *this;
}

bool UIElement::subtree_virtualization_enabled() const noexcept {
    return subtree_virtualization_enabled_;
}

UIElement& UIElement::enable_subtree_virtualization() {
    return set_subtree_virtualization_enabled(true);
}

UIElement& UIElement::disable_subtree_virtualization() {
    return set_subtree_virtualization_enabled(false);
}

UIElement& UIElement::set_subtree_virtualization_overscan(float overscan) {
    verify_thread_access();
    subtree_virtualization_overscan_ =
        std::isfinite(overscan) && overscan >= 0.0F ? overscan : -1.0F;
    refresh_virtualization_from_host();
    invalidate_paint();
    return *this;
}

UIElement& UIElement::set_virtualization_overscan(float overscan) {
    return set_subtree_virtualization_overscan(overscan);
}

float UIElement::subtree_virtualization_overscan() const noexcept {
    return subtree_virtualization_overscan_;
}

UIElement& UIElement::set_virtualization_materializer(
    VirtualizationMaterializer materializer) {
    verify_thread_access();
    if (!materializer) {
        if (subtree_virtualized_ && virtualized_snapshot_.has_value() &&
            virtualization_materializer_) {
            set_subtree_virtualized(false);
        }
        virtualization_materializer_ = nullptr;
        virtualized_snapshot_.reset();
        return *this;
    }
    virtualization_materializer_ = std::move(materializer);
    if (parent_ != nullptr && children_.empty() && !virtualized_snapshot_.has_value()) {
        virtualized_snapshot_ = compress_subtree();
        subtree_virtualized_ = true;
        discard_cached_render_commands_subtree();
        mark_z_order_dirty();
        invalidate_render_commands();
        invalidate_paint();
    }
    return *this;
}

bool UIElement::has_virtualization_materializer() const noexcept {
    return static_cast<bool>(virtualization_materializer_);
}

UIElement& UIElement::set_virtual_children(VirtualChildrenOptions options) {
    verify_thread_access();
    if (!std::isfinite(options.item_extent) || options.item_extent <= 0.0F) {
        throw std::invalid_argument("virtual child item extent must be finite and positive");
    }
    if (options.overscan_extent < 0.0F || !std::isfinite(options.overscan_extent)) {
        options.overscan_extent = -1.0F;
    }
    if (!options.materializer && options.count > 0U) {
        throw std::invalid_argument("virtual child materializer must not be empty");
    }

    clear_children();
    virtual_children_ = std::make_unique<VirtualChildrenState>();
    virtual_children_->options = std::move(options);
    virtual_children_->realized_start = 0U;
    virtual_children_->realized_count = 0U;
    const auto total_extent = static_cast<float>(virtual_children_->options.count) *
                              virtual_children_->options.item_extent;
    configure_layout([orientation = virtual_children_->options.orientation,
                      total_extent](layout::LayoutElement& item) {
        item.set_flex_shrink(0.0F);
        if (orientation == VirtualChildrenOrientation::Vertical) {
            item.set_flex_direction(layout::FlexDirection::Column)
                .set_width(layout::Length::percent(100.0F))
                .set_height(layout::Length::points(total_extent));
        } else {
            item.set_flex_direction(layout::FlexDirection::Row)
                .set_height(layout::Length::percent(100.0F))
                .set_width(layout::Length::points(total_extent));
        }
    });

    auto leading = make_virtual_spacer(virtual_children_->options.orientation, 0.0F);
    auto& leading_ref = append_child(std::move(leading));
    virtual_children_->leading_spacer = &leading_ref;

    auto trailing = make_virtual_spacer(virtual_children_->options.orientation, total_extent);
    auto& trailing_ref = append_child(std::move(trailing));
    virtual_children_->trailing_spacer = &trailing_ref;

    virtualization_cache_valid_ = false;
    invalidate_layout();
    return *this;
}

UIElement& UIElement::set_virtual_children(std::size_t count,
                                           float item_extent,
                                           VirtualChildMaterializer materializer,
                                           VirtualChildrenOrientation orientation,
                                           float overscan_extent) {
    return set_virtual_children(VirtualChildrenOptions{.count = count,
                                                       .item_extent = item_extent,
                                                       .orientation = orientation,
                                                       .overscan_extent = overscan_extent,
                                                       .materializer = std::move(materializer)});
}

UIElement& UIElement::set_vertical_virtual_children(std::size_t count,
                                                    float item_extent,
                                                    VirtualChildMaterializer materializer,
                                                    float overscan_extent) {
    return set_virtual_children(count, item_extent, std::move(materializer),
                                VirtualChildrenOrientation::Vertical, overscan_extent);
}

UIElement& UIElement::set_horizontal_virtual_children(std::size_t count,
                                                      float item_extent,
                                                      VirtualChildMaterializer materializer,
                                                      float overscan_extent) {
    return set_virtual_children(count, item_extent, std::move(materializer),
                                VirtualChildrenOrientation::Horizontal, overscan_extent);
}

UIElement& UIElement::clear_virtual_children() {
    verify_thread_access();
    if (virtual_children_ == nullptr) {
        return *this;
    }
    virtual_children_.reset();
    clear_children();
    virtualization_cache_valid_ = false;
    invalidate_layout();
    return *this;
}

bool UIElement::has_virtual_children() const noexcept {
    return virtual_children_ != nullptr;
}

std::size_t UIElement::virtual_child_count() const noexcept {
    return virtual_children_ != nullptr ? virtual_children_->options.count : 0U;
}

std::size_t UIElement::realized_virtual_child_count() const noexcept {
    return virtual_children_ != nullptr ? virtual_children_->realized.size() : 0U;
}

bool UIElement::subtree_virtualized() const noexcept {
    return subtree_virtualized_;
}

std::size_t UIElement::virtualized_child_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        children_.begin(), children_.end(),
        [](const auto& child) noexcept { return child != nullptr && child->subtree_virtualized_; }));
}

std::size_t UIElement::realized_child_count() const noexcept {
    return child_count() - virtualized_child_count();
}

UIElement::VirtualizationMetrics UIElement::virtualization_metrics() const noexcept {
    return VirtualizationMetrics{.subtree_virtualization_enabled =
                                     subtree_virtualization_enabled_,
                                 .subtree_virtualized = subtree_virtualized_,
                                 .child_count = child_count(),
                                 .realized_child_count = realized_child_count(),
                                 .virtualized_child_count = virtualized_child_count(),
                                 .virtual_child_count = virtual_child_count(),
                                 .realized_virtual_child_count =
                                     realized_virtual_child_count()};
}

bool UIElement::effective_subtree_virtualization_enabled() const noexcept {
    return subtree_virtualization_enabled_;
}

float UIElement::effective_subtree_virtualization_overscan(layout::Rect viewport) const noexcept {
    if (std::isfinite(subtree_virtualization_overscan_) &&
        subtree_virtualization_overscan_ >= 0.0F) {
        return subtree_virtualization_overscan_;
    }
    return std::max(viewport.height * default_virtualization_viewport_overscan_ratio,
                    default_virtualization_min_overscan);
}

bool UIElement::can_virtualize_subtree() const noexcept {
    if (focusable_ || focused_ || has_running_animations() || text_input_context_menu_open() ||
        top_layer_count() > 0U) {
        return false;
    }
    if (event_router_ != nullptr) {
        if (auto* capture = event_router_->pointer_capture();
            capture != nullptr && contains(*capture)) {
            return false;
        }
        if (auto* selection_owner = event_router_->text_selection_owner();
            selection_owner != nullptr && contains(*selection_owner)) {
            return false;
        }
    }
    if (focus_manager_ != nullptr) {
        if (auto* focused = focus_manager_->focused_element();
            focused != nullptr && contains(*focused)) {
            return false;
        }
    }
    return true;
}

layout::Rect UIElement::virtualization_bounds_for_child(const UIElement& child) const noexcept {
    if (!child.visible_) {
        return {};
    }
    if (child.shadow_visible() || child.has_render_layer() || child.top_layer_count() > 0U) {
        return child.visible_subtree_bounds();
    }
    return child.committed_absolute_frame_;
}

void UIElement::mark_virtualization_layout_dirty() noexcept {
    virtualization_layout_dirty_ = true;
    virtualization_cache_valid_ = false;

    auto& host = top_layer_host();
    host.virtualization_layout_dirty_ = true;
    host.note_layout_dirty_root(*this);
    host.mark_descendant_layout_dirty();
}

void UIElement::set_subtree_virtualized(bool virtualized) noexcept {
    if (subtree_virtualized_ == virtualized) {
        return;
    }
    subtree_virtualized_ = virtualized;
    if (virtualized) {
        if (virtualization_materializer_ && !virtualized_snapshot_.has_value()) {
            try {
                virtualized_snapshot_ = compress_subtree();
                clear_children();
                mark_virtualization_layout_dirty();
            } catch (...) {
                virtualized_snapshot_.reset();
                assert(false && "UIElement subtree compression must not throw");
            }
        }
    } else if (virtualized_snapshot_.has_value() && virtualization_materializer_) {
        try {
            auto restored_child = virtualization_materializer_(*virtualized_snapshot_);
            clear_children();
            if (restored_child != nullptr) {
                append_child(std::move(restored_child));
            }
            mark_virtualization_layout_dirty();
            decompress_subtree(*virtualized_snapshot_);
            virtualized_snapshot_.reset();
        } catch (...) {
            assert(false && "UIElement subtree decompression must not throw");
        }
    }
    discard_cached_render_commands_subtree();
    if (virtualized) {
        for (auto& child : children_) {
            child->set_subtree_virtualized(true);
        }
    } else {
        for (auto& child : children_) {
            child->set_subtree_virtualized(false);
        }
    }
    mark_z_order_dirty();
    invalidate_render_commands();
    invalidate_paint();
}

bool UIElement::update_virtual_children(layout::Rect clip_rect) noexcept {
    auto* state = virtual_children_.get();
    if (state == nullptr || state->updating) {
        return false;
    }

    const auto& options = state->options;
    if (options.count == 0U || options.item_extent <= 0.0F || !options.materializer) {
        return false;
    }

    const auto orientation = options.orientation;
    const auto viewport_start = orientation == VirtualChildrenOrientation::Vertical
                                    ? clip_rect.y - child_content_absolute_origin().y
                                    : clip_rect.x - child_content_absolute_origin().x;
    const auto viewport_extent =
        orientation == VirtualChildrenOrientation::Vertical ? clip_rect.height : clip_rect.width;
    const auto overscan =
        options.overscan_extent >= 0.0F
            ? options.overscan_extent
            : effective_subtree_virtualization_overscan(clip_rect);
    const auto desired_start_offset =
        std::max(0.0F, (std::isfinite(viewport_start) ? viewport_start : 0.0F) - overscan);
    const auto desired_end_offset =
        std::max(desired_start_offset,
                 (std::isfinite(viewport_start) ? viewport_start : 0.0F) +
                     std::max(std::isfinite(viewport_extent) ? viewport_extent : 0.0F, 0.0F) +
                     overscan);

    auto desired_start =
        std::min(options.count, static_cast<std::size_t>(desired_start_offset / options.item_extent));
    auto desired_end =
        std::min(options.count, static_cast<std::size_t>(
                                    std::ceil(desired_end_offset / options.item_extent)) +
                                    1U);
    if (desired_end < desired_start) {
        desired_end = desired_start;
    }

    for (const auto& entry : state->realized) {
        if (entry.element != nullptr && !entry.element->can_virtualize_subtree()) {
            desired_start = std::min(desired_start, entry.index);
            desired_end = std::max(desired_end, std::min(options.count, entry.index + 1U));
        }
    }

    const auto desired_count = desired_end > desired_start ? desired_end - desired_start : 0U;
    if (desired_start == state->realized_start && desired_count == state->realized_count &&
        state->realized.size() == desired_count) {
        return false;
    }

    state->updating = true;
    struct KeptChild {
        std::size_t index = 0U;
        std::unique_ptr<UIElement> element;
    };
    auto kept = std::vector<KeptChild>{};
    kept.reserve(desired_count);

    for (const auto& entry : state->realized) {
        if (entry.element == nullptr) {
            continue;
        }
        auto child = remove_child(*entry.element);
        if (entry.index >= desired_start && entry.index < desired_end) {
            kept.push_back(KeptChild{.index = entry.index, .element = std::move(child)});
        }
    }
    state->realized.clear();

    if (state->leading_spacer != nullptr) {
        static_cast<void>(remove_child(*state->leading_spacer));
        state->leading_spacer = nullptr;
    }
    if (state->trailing_spacer != nullptr) {
        static_cast<void>(remove_child(*state->trailing_spacer));
        state->trailing_spacer = nullptr;
    }

    std::sort(kept.begin(), kept.end(), [](const KeptChild& lhs, const KeptChild& rhs) noexcept {
        return lhs.index < rhs.index;
    });

    auto leading = make_virtual_spacer(orientation,
                                       static_cast<float>(desired_start) * options.item_extent);
    auto& leading_ref = append_child(std::move(leading));
    state->leading_spacer = &leading_ref;

    auto kept_index = std::size_t{0U};
    state->realized.reserve(desired_count);
    for (auto index = desired_start; index < desired_end; ++index) {
        std::unique_ptr<UIElement> child;
        if (kept_index < kept.size() && kept[kept_index].index == index) {
            child = std::move(kept[kept_index].element);
            ++kept_index;
        } else {
            child = options.materializer(index);
        }
        if (child == nullptr) {
            child = std::make_unique<UIElement>();
        }
        child->configure_layout([orientation, extent = options.item_extent](
                                    layout::LayoutElement& item) {
            configure_virtual_extent(item, orientation, extent);
        });
        auto& child_ref = append_child(std::move(child));
        state->realized.push_back(VirtualChildrenState::RealizedChild{.index = index,
                                                                      .element = &child_ref});
    }

    const auto trailing_count = options.count > desired_end ? options.count - desired_end : 0U;
    auto trailing = make_virtual_spacer(orientation,
                                        static_cast<float>(trailing_count) * options.item_extent);
    auto& trailing_ref = append_child(std::move(trailing));
    state->trailing_spacer = &trailing_ref;

    state->realized_start = desired_start;
    state->realized_count = desired_count;
    state->updating = false;
    mark_virtualization_layout_dirty();
    invalidate_layout();
    return true;
}

void UIElement::update_child_virtualization(layout::Rect clip_rect) noexcept {
    if (children_.empty()) {
        return;
    }

    const auto enabled = effective_subtree_virtualization_enabled();
    const auto overscan = enabled ? effective_subtree_virtualization_overscan(clip_rect) : 0.0F;
    const auto virtualization_rect =
        enabled ? layout::inflate_rect(clip_rect, overscan) : layout::Rect{};
    const auto scroll_offset = scroll_offset_value();
    if (virtualization_cache_valid_ &&
        virtualization_cache_layout_generation_ == layout_generation_ &&
        virtualization_cache_children_revision_ == children_revision_ &&
        virtualization_cache_overscan_ == overscan &&
        rect_equals(virtualization_cache_clip_rect_, clip_rect) &&
        point_equals(virtualization_cache_scroll_offset_, scroll_offset)) {
        return;
    }

    auto changed = update_virtual_children(clip_rect);
    if (changed) {
        mark_z_order_dirty();
        invalidate_render_commands();
        invalidate_paint();
        return;
    }

    for (auto& child : children_) {
        if (child == nullptr) {
            continue;
        }
        if (virtual_children_ != nullptr &&
            (child.get() == virtual_children_->leading_spacer ||
             child.get() == virtual_children_->trailing_spacer)) {
            continue;
        }
        const auto should_virtualize =
            enabled && !layout::rects_intersect(virtualization_bounds_for_child(*child),
                                                virtualization_rect) &&
            child->can_virtualize_subtree();
        if (child->subtree_virtualized_ != should_virtualize) {
            child->set_subtree_virtualized(should_virtualize);
            changed = true;
        }
    }

    if (changed) {
        mark_z_order_dirty();
        invalidate_render_commands();
        invalidate_paint();
    }
    virtualization_cache_valid_ = true;
    virtualization_cache_layout_generation_ = layout_generation_;
    virtualization_cache_children_revision_ = children_revision_;
    virtualization_cache_clip_rect_ = clip_rect;
    virtualization_cache_scroll_offset_ = scroll_offset;
    virtualization_cache_overscan_ = overscan;
}

void UIElement::refresh_virtualization_from_host() noexcept {
    if (layout_generation_ == 0U) {
        return;
    }

    try {
        update_viewport_state_subtree(committed_absolute_frame_);
    } catch (...) {
        assert(false && "UIElement virtualization refresh must not throw");
    }
}

} // namespace winelement::elements
