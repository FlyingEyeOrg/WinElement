#pragma once

#include <winelement/layout/layout_types.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace winelement::elements {
class UIElement;
}

namespace winelement::layout {

namespace detail {
class YogaConfigHandle;
}

class LayoutEngine;
class LayoutElementPool;

class LayoutElement final {
  public:
    ~LayoutElement();

    static void* operator new(std::size_t size);
    static void operator delete(void* pointer) noexcept;

    LayoutElement(const LayoutElement&) = delete;
    LayoutElement& operator=(const LayoutElement&) = delete;
    LayoutElement(LayoutElement&&) = delete;
    LayoutElement& operator=(LayoutElement&&) = delete;

    [[nodiscard]] LayoutElement* parent() noexcept;
    [[nodiscard]] const LayoutElement* parent() const noexcept;
    [[nodiscard]] std::size_t child_count() const noexcept;
    [[nodiscard]] LayoutElement& child_at(std::size_t index);
    [[nodiscard]] const LayoutElement& child_at(std::size_t index) const;

    LayoutElement& append_child(std::unique_ptr<LayoutElement> child);
    LayoutElement& insert_child(std::size_t index, std::unique_ptr<LayoutElement> child);
    [[nodiscard]] std::unique_ptr<LayoutElement> remove_child(LayoutElement& child);
    [[nodiscard]] std::unique_ptr<LayoutElement> remove_child_at(std::size_t index);
    void clear_children();

    void calculate_layout(LayoutConstraints constraints = {});

    [[nodiscard]] Rect frame() const noexcept;
    [[nodiscard]] Direction layout_direction() const noexcept;
    [[nodiscard]] EdgeInsets layout_margin() const noexcept;
    [[nodiscard]] EdgeInsets layout_padding() const noexcept;
    [[nodiscard]] EdgeInsets layout_border() const noexcept;
    [[nodiscard]] bool has_new_layout() const noexcept;
    void clear_has_new_layout() noexcept;
    [[nodiscard]] bool had_overflow() const noexcept;
    [[nodiscard]] bool is_dirty() const noexcept;

    LayoutElement& set_measure_callback(MeasureCallback callback);
    LayoutElement& clear_measure_callback() noexcept;
    [[nodiscard]] bool has_measure_callback() const noexcept;
    LayoutElement& mark_measure_dirty();
    LayoutElement& set_baseline_callback(BaselineCallback callback);
    LayoutElement& clear_baseline_callback() noexcept;
    [[nodiscard]] bool has_baseline_callback() const noexcept;
    LayoutElement& set_dirtied_callback(DirtiedCallback callback);
    LayoutElement& clear_dirtied_callback() noexcept;
    [[nodiscard]] bool has_dirtied_callback() const noexcept;

    LayoutElement& set_element_kind(ElementKind element_kind);
    LayoutElement& set_reference_baseline(bool reference_baseline) noexcept;
    [[nodiscard]] bool is_reference_baseline() const noexcept;
    LayoutElement& set_always_forms_containing_block(bool always_forms_containing_block) noexcept;
    [[nodiscard]] bool always_forms_containing_block() const noexcept;
    LayoutElement& set_direction(Direction direction);
    LayoutElement& set_flex_direction(FlexDirection flex_direction);
    LayoutElement& set_justify_content(JustifyContent justify_content);
    LayoutElement& set_align_content(Align align_content);
    LayoutElement& set_align_items(Align align_items);
    LayoutElement& set_align_self(Align align_self);
    LayoutElement& set_position_type(PositionType position_type);
    LayoutElement& set_flex_wrap(Wrap flex_wrap);
    LayoutElement& set_overflow(Overflow overflow);
    LayoutElement& set_display(Display display);
    LayoutElement& set_box_sizing(BoxSizing box_sizing);

    LayoutElement& set_flex(float flex);
    LayoutElement& set_flex_grow(float flex_grow);
    LayoutElement& set_flex_shrink(float flex_shrink);
    LayoutElement& set_flex_basis(Length flex_basis);
    LayoutElement& set_width(Length width);
    LayoutElement& set_height(Length height);
    LayoutElement& set_min_width(Length min_width);
    LayoutElement& set_min_height(Length min_height);
    LayoutElement& set_max_width(Length max_width);
    LayoutElement& set_max_height(Length max_height);
    LayoutElement& set_size(Length width, Length height);
    LayoutElement& set_position(Edge edge, Length position);
    LayoutElement& set_margin(Edge edge, Length margin);
    LayoutElement& set_padding(Edge edge, Length padding);
    LayoutElement& set_border(Edge edge, float border_width);
    LayoutElement& set_gap(Gutter gutter, Length gap);
    LayoutElement& set_aspect_ratio(float aspect_ratio);

  private:
    LayoutElement();
    explicit LayoutElement(std::shared_ptr<detail::YogaConfigHandle> config);
    void bind_to_engine(const LayoutEngine& layout_engine);
    void materialize_subtree();

    class Impl;
    struct StyleState;

    [[nodiscard]] StyleState& ensure_style_state();
    [[nodiscard]] const StyleState* style_state() const noexcept;
    [[nodiscard]] Impl& ensure_impl();
    [[nodiscard]] Impl* impl() noexcept;
    [[nodiscard]] const Impl* impl() const noexcept;
    [[nodiscard]] bool has_config() const noexcept;
    void bind_to_config(std::shared_ptr<detail::YogaConfigHandle> config);
    void drop_impl() noexcept;
    void apply_pending_style_to_impl() noexcept;
    void apply_callbacks_to_impl() noexcept;
    void reset_for_pool(std::shared_ptr<detail::YogaConfigHandle> config);

    static constexpr std::size_t inline_child_capacity = 8;

    std::vector<std::unique_ptr<LayoutElement>> children_;
    std::shared_ptr<detail::YogaConfigHandle> config_;
    std::unique_ptr<Impl> impl_;
    LayoutElement* parent_ = nullptr;
    MeasureCallback measure_callback_;
    BaselineCallback baseline_callback_;
    DirtiedCallback dirtied_callback_;
    std::unique_ptr<StyleState> style_state_;
    Rect cached_frame_{};
    Direction cached_layout_direction_ = Direction::LeftToRight;
    EdgeInsets cached_layout_margin_{};
    EdgeInsets cached_layout_padding_{};
    EdgeInsets cached_layout_border_{};
    bool cached_has_new_layout_ : 1 = false;
    bool cached_had_overflow_ : 1 = false;
    bool measure_dirty_pending_ : 1 = false;

    friend class LayoutEngine;
    friend class LayoutElementPool;
    friend class winelement::elements::UIElement;
};

} // namespace winelement::layout
