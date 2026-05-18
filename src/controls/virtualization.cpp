#include <winelement/controls/virtualization.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace winelement::controls {

void VirtualizationPlanner::set_total_count(std::size_t total_count) {
    total_count_ = total_count;
    if (!item_extents_.empty()) {
        rebuild_prefix_extents();
    }
}

void VirtualizationPlanner::set_item_extent(float item_extent) noexcept {
    item_extent_ = std::isfinite(item_extent) && item_extent > 0.0F ? item_extent : 1.0F;
    if (item_extents_.empty()) {
        prefix_extents_.clear();
    }
}

void VirtualizationPlanner::set_item_extents(std::vector<float> item_extents) {
    item_extents_ = std::move(item_extents);
    for (auto& extent : item_extents_) {
        extent = std::isfinite(extent) && extent > 0.0F ? extent : item_extent_;
    }
    rebuild_prefix_extents();
}

void VirtualizationPlanner::clear_item_extents() noexcept {
    item_extents_.clear();
    prefix_extents_.clear();
}

void VirtualizationPlanner::set_overscan(std::size_t overscan) noexcept {
    overscan_ = overscan;
}

VirtualizationWindow VirtualizationPlanner::window_for(float scroll_offset,
                                                       float viewport_extent) const noexcept {
    const auto safe_offset = std::max(std::isfinite(scroll_offset) ? scroll_offset : 0.0F, 0.0F);
    const auto safe_viewport =
        std::max(std::isfinite(viewport_extent) ? viewport_extent : 0.0F, 0.0F);
    if (total_count_ == 0U || item_extent_ <= 0.0F) {
        return VirtualizationWindow{};
    }

    if (!prefix_extents_.empty()) {
        const auto total_extent = prefix_extents_.back();
        const auto first_iterator =
            std::upper_bound(prefix_extents_.begin(), prefix_extents_.end(), safe_offset);
        const auto first_visible = first_iterator == prefix_extents_.begin()
                                       ? std::size_t{0U}
                                       : static_cast<std::size_t>(std::distance(
                                             prefix_extents_.begin(), first_iterator - 1));
        const auto visible_end_offset = safe_offset + safe_viewport;
        const auto end_iterator =
            std::lower_bound(prefix_extents_.begin(), prefix_extents_.end(), visible_end_offset);
        const auto last_visible = std::min(
            total_count_,
            static_cast<std::size_t>(std::distance(prefix_extents_.begin(), end_iterator)) + 1U);
        const auto start = first_visible > overscan_ ? first_visible - overscan_ : 0U;
        const auto end = std::min(total_count_, last_visible + overscan_);
        const auto count = end > start ? end - start : 0U;
        const auto leading = prefix_extents_[start];
        const auto trailing = total_extent - prefix_extents_[start + count];
        return VirtualizationWindow{.start_index = start,
                                    .count = count,
                                    .total_count = total_count_,
                                    .leading_spacer = leading,
                                    .trailing_spacer = std::max(trailing, 0.0F)};
    }

    const auto first_visible = static_cast<std::size_t>(safe_offset / item_extent_);
    const auto visible_count =
        static_cast<std::size_t>(std::ceil(safe_viewport / item_extent_)) + 1U;
    const auto start = first_visible > overscan_ ? first_visible - overscan_ : 0U;
    const auto end = std::min(total_count_, first_visible + visible_count + overscan_);
    const auto count = end > start ? end - start : 0U;
    const auto leading = static_cast<float>(start) * item_extent_;
    const auto trailing_count = total_count_ > start + count ? total_count_ - start - count : 0U;
    return VirtualizationWindow{.start_index = start,
                                .count = count,
                                .total_count = total_count_,
                                .leading_spacer = leading,
                                .trailing_spacer =
                                    static_cast<float>(trailing_count) * item_extent_};
}

std::size_t VirtualizationPlanner::total_count() const noexcept {
    return total_count_;
}

float VirtualizationPlanner::item_extent() const noexcept {
    return item_extent_;
}

std::size_t VirtualizationPlanner::overscan() const noexcept {
    return overscan_;
}

void VirtualizationPlanner::rebuild_prefix_extents() {
    const auto count = std::min(total_count_, item_extents_.size());
    prefix_extents_.assign(count + 1U, 0.0F);
    for (std::size_t index = 0; index < count; ++index) {
        prefix_extents_[index + 1U] = prefix_extents_[index] + item_extents_[index];
    }
    if (count < total_count_) {
        const auto old_size = prefix_extents_.size();
        prefix_extents_.resize(total_count_ + 1U, prefix_extents_.back());
        for (std::size_t index = old_size; index < prefix_extents_.size(); ++index) {
            prefix_extents_[index] = prefix_extents_[index - 1U] + item_extent_;
        }
    }
}

} // namespace winelement::controls