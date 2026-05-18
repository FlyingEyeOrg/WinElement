#pragma once

#include <winelement/controls/panel.hpp>
#include <winelement/layout/layout_types.hpp>

namespace winelement::controls {

enum class Orientation { Horizontal, Vertical };

class StackPanel final : public Panel {
  public:
    StackPanel();

    StackPanel& set_orientation(Orientation orientation);
    StackPanel& set_gap(float gap);
    StackPanel& set_wrap(layout::Wrap wrap);
    StackPanel& set_justify_content(layout::JustifyContent justify_content);
    StackPanel& set_align_items(layout::Align align_items);
    [[nodiscard]] Orientation orientation() const noexcept;
    [[nodiscard]] float gap() const noexcept;
    [[nodiscard]] layout::Wrap wrap() const noexcept;
    [[nodiscard]] layout::JustifyContent justify_content() const noexcept;
    [[nodiscard]] layout::Align align_items() const noexcept;

  private:
    void apply_stack_layout();

    Orientation orientation_ = Orientation::Vertical;
    float gap_ = 0.0F;
    layout::Wrap wrap_ = layout::Wrap::NoWrap;
    layout::JustifyContent justify_content_ = layout::JustifyContent::FlexStart;
    layout::Align align_items_ = layout::Align::Stretch;
};

} // namespace winelement::controls
