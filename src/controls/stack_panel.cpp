#include <winelement/controls/stack_panel.hpp>

#include <algorithm>

namespace winelement::controls {

StackPanel::StackPanel() : Panel() {
    apply_stack_layout();
}

StackPanel& StackPanel::set_orientation(Orientation orientation) {
    if (orientation_ == orientation) {
        return *this;
    }

    orientation_ = orientation;
    apply_stack_layout();
    return *this;
}

StackPanel& StackPanel::set_gap(float gap) {
    gap_ = std::max(gap, 0.0F);
    apply_stack_layout();
    return *this;
}

StackPanel& StackPanel::set_wrap(layout::Wrap wrap) {
    wrap_ = wrap;
    apply_stack_layout();
    return *this;
}

StackPanel& StackPanel::set_justify_content(layout::JustifyContent justify_content) {
    justify_content_ = justify_content;
    apply_stack_layout();
    return *this;
}

StackPanel& StackPanel::set_align_items(layout::Align align_items) {
    align_items_ = align_items;
    apply_stack_layout();
    return *this;
}

Orientation StackPanel::orientation() const noexcept {
    return orientation_;
}

float StackPanel::gap() const noexcept {
    return gap_;
}

layout::Wrap StackPanel::wrap() const noexcept {
    return wrap_;
}

layout::JustifyContent StackPanel::justify_content() const noexcept {
    return justify_content_;
}

layout::Align StackPanel::align_items() const noexcept {
    return align_items_;
}

void StackPanel::apply_stack_layout() {
    configure_layout([this](layout::LayoutElement& item) {
        item.set_flex_direction(orientation_ == Orientation::Horizontal
                                    ? layout::FlexDirection::Row
                                    : layout::FlexDirection::Column)
            .set_flex_wrap(wrap_)
            .set_justify_content(justify_content_)
            .set_align_items(align_items_)
            .set_gap(layout::Gutter::Row, layout::Length::points(gap_))
            .set_gap(layout::Gutter::Column, layout::Length::points(gap_));
    });
}

} // namespace winelement::controls
