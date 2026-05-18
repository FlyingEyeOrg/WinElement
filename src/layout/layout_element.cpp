#include <winelement/layout/layout_element.hpp>

#include <winelement/layout/layout_engine.hpp>

#include "detail/yoga_conversions.hpp"
#include "detail/yoga_handles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <yoga/Yoga.h>

namespace winelement::layout {
namespace {

struct LayoutElementFreeNode {
    LayoutElementFreeNode* next = nullptr;
};

class LayoutElementThreadPool final {
  public:
    ~LayoutElementThreadPool() {
        while (free_list_ != nullptr) {
            auto* node = free_list_;
            free_list_ = free_list_->next;
            ::operator delete(node);
        }
    }

    [[nodiscard]] void* allocate(std::size_t size) {
        if (size != sizeof(LayoutElement) || free_list_ == nullptr) {
            return ::operator new(size);
        }

        auto* node = free_list_;
        free_list_ = free_list_->next;
        return node;
    }

    void deallocate(void* pointer, std::size_t size) noexcept {
        if (pointer == nullptr) {
            return;
        }

        if (size != sizeof(LayoutElement)) {
            ::operator delete(pointer);
            return;
        }

        auto* node = static_cast<LayoutElementFreeNode*>(pointer);
        node->next = free_list_;
        free_list_ = node;
    }

  private:
    LayoutElementFreeNode* free_list_ = nullptr;
};

[[nodiscard]] LayoutElementThreadPool& layout_element_pool() noexcept {
    static thread_local LayoutElementThreadPool pool;
    return pool;
}

struct LayoutElementContext {
    LayoutElement* owner = nullptr;
    MeasureCallback* measure_callback = nullptr;
    BaselineCallback* baseline_callback = nullptr;
    DirtiedCallback* dirtied_callback = nullptr;
};

[[nodiscard]] float undefined_value() noexcept {
    return std::numeric_limits<float>::quiet_NaN();
}

void validate_finite(float value, std::string_view name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

void validate_non_negative(float value, std::string_view name) {
    validate_finite(value, name);
    if (value < 0.0F) {
        throw std::invalid_argument(std::string(name) + " must be non-negative");
    }
}

void validate_length(Length length, std::string_view name, bool allow_auto) {
    if (length.unit() == LengthUnit::Undefined) {
        return;
    }

    if (length.unit() == LengthUnit::Auto) {
        if (!allow_auto) {
            throw std::invalid_argument(std::string(name) + " does not support auto");
        }
        return;
    }

    validate_finite(length.value(), name);
}

[[nodiscard]] float available_or_undefined(std::optional<float> value, std::string_view name) {
    if (!value.has_value()) {
        return undefined_value();
    }

    validate_non_negative(*value, name);
    return *value;
}

[[nodiscard]] MeasureMode from_yoga_measure_mode(YGMeasureMode measure_mode) noexcept {
    switch (measure_mode) {
    case YGMeasureModeUndefined:
        return MeasureMode::Undefined;
    case YGMeasureModeExactly:
        return MeasureMode::Exactly;
    case YGMeasureModeAtMost:
        return MeasureMode::AtMost;
    }

    return MeasureMode::Undefined;
}

[[nodiscard]] YGSize measure_layout_element(YGNodeConstRef node, float width,
                                            YGMeasureMode width_mode, float height,
                                            YGMeasureMode height_mode) {
    const auto* context = static_cast<const LayoutElementContext*>(YGNodeGetContext(node));
    if (context == nullptr || context->measure_callback == nullptr || !*context->measure_callback) {
        return {0.0F, 0.0F};
    }

    try {
        const MeasureInput input{
            width,
            from_yoga_measure_mode(width_mode),
            height,
            from_yoga_measure_mode(height_mode),
        };
        const Size result = (*context->measure_callback)(input);

        return {
            std::isfinite(result.width) ? std::max(0.0F, result.width) : 0.0F,
            std::isfinite(result.height) ? std::max(0.0F, result.height) : 0.0F,
        };
    } catch (...) {
        return {0.0F, 0.0F};
    }
}

[[nodiscard]] float baseline_layout_element(YGNodeConstRef node, float width, float height) {
    const auto* context = static_cast<const LayoutElementContext*>(YGNodeGetContext(node));
    if (context == nullptr || context->baseline_callback == nullptr ||
        !*context->baseline_callback) {
        return std::isfinite(height) ? std::max(0.0F, height) : 0.0F;
    }

    try {
        const BaselineInput input{width, height};
        const auto result = (*context->baseline_callback)(input);
        return std::isfinite(result) ? std::max(0.0F, result) : 0.0F;
    } catch (...) {
        return std::isfinite(height) ? std::max(0.0F, height) : 0.0F;
    }
}

void dirtied_layout_element(YGNodeConstRef node) {
    auto* context = static_cast<LayoutElementContext*>(YGNodeGetContext(node));
    if (context == nullptr || context->owner == nullptr || context->dirtied_callback == nullptr ||
        !*context->dirtied_callback) {
        return;
    }

    try {
        (*context->dirtied_callback)(*context->owner);
    } catch (...) {
    }
}

template <typename PointSetter, typename PercentSetter, typename AutoSetter>
void set_length_value(Length length, std::string_view name, bool allow_auto,
                      PointSetter point_setter, PercentSetter percent_setter,
                      AutoSetter auto_setter) {
    validate_length(length, name, allow_auto);

    switch (length.unit()) {
    case LengthUnit::Undefined:
        point_setter(undefined_value());
        return;
    case LengthUnit::Points:
        point_setter(length.value());
        return;
    case LengthUnit::Percent:
        percent_setter(length.value());
        return;
    case LengthUnit::Auto:
        auto_setter();
        return;
    }

    throw std::invalid_argument(std::string(name) + " has an unknown unit");
}

[[nodiscard]] std::size_t edge_index(Edge edge) noexcept {
    switch (edge) {
    case Edge::Left:
        return 0U;
    case Edge::Top:
        return 1U;
    case Edge::Right:
        return 2U;
    case Edge::Bottom:
        return 3U;
    case Edge::Start:
        return 4U;
    case Edge::End:
        return 5U;
    case Edge::Horizontal:
        return 6U;
    case Edge::Vertical:
        return 7U;
    case Edge::All:
        return 8U;
    }

    return 0U;
}

[[nodiscard]] std::size_t gutter_index(Gutter gutter) noexcept {
    switch (gutter) {
    case Gutter::Column:
        return 0U;
    case Gutter::Row:
        return 1U;
    case Gutter::All:
        return 2U;
    }

    return 0U;
}

} // namespace

struct LayoutElement::StyleState final {
    std::optional<ElementKind> element_kind;
    std::optional<bool> reference_baseline;
    std::optional<bool> always_forms_containing_block;
    std::optional<Direction> direction;
    std::optional<FlexDirection> flex_direction;
    std::optional<JustifyContent> justify_content;
    std::optional<Align> align_content;
    std::optional<Align> align_items;
    std::optional<Align> align_self;
    std::optional<PositionType> position_type;
    std::optional<Wrap> flex_wrap;
    std::optional<Overflow> overflow;
    std::optional<Display> display;
    std::optional<BoxSizing> box_sizing;
    std::optional<float> flex;
    std::optional<float> flex_grow;
    std::optional<float> flex_shrink;
    std::optional<Length> flex_basis;
    std::optional<Length> width;
    std::optional<Length> height;
    std::optional<Length> min_width;
    std::optional<Length> min_height;
    std::optional<Length> max_width;
    std::optional<Length> max_height;
    std::array<std::optional<Length>, 9U> position;
    std::array<std::optional<Length>, 9U> margin;
    std::array<std::optional<Length>, 9U> padding;
    std::array<std::optional<float>, 9U> border;
    std::array<std::optional<Length>, 3U> gap;
    std::optional<float> aspect_ratio;
};

class LayoutElement::Impl final {
  public:
    explicit Impl(std::shared_ptr<detail::YogaConfigHandle> config)
        : config_(std::move(config)), node_(config_->get()) {}

    [[nodiscard]] YGNodeRef node() noexcept {
        return node_.get();
    }
    [[nodiscard]] YGNodeConstRef node() const noexcept {
        return node_.get();
    }
    [[nodiscard]] LayoutElementContext& context() noexcept {
        return context_;
    }
    [[nodiscard]] const std::shared_ptr<detail::YogaConfigHandle>& config() const noexcept {
        return config_;
    }

  private:
    std::shared_ptr<detail::YogaConfigHandle> config_;
    detail::YogaNodeHandle node_;
    LayoutElementContext context_;
};

void* LayoutElement::operator new(std::size_t size) {
    return layout_element_pool().allocate(size);
}

void LayoutElement::operator delete(void* pointer) noexcept {
    layout_element_pool().deallocate(pointer, sizeof(LayoutElement));
}

LayoutElement::LayoutElement() {
    children_.reserve(inline_child_capacity);
}

LayoutElement::LayoutElement(std::shared_ptr<detail::YogaConfigHandle> config) : LayoutElement() {
    config_ = std::move(config);
}

LayoutElement::~LayoutElement() = default;

LayoutElement::StyleState& LayoutElement::ensure_style_state() {
    if (style_state_ == nullptr) {
        style_state_ = std::make_unique<StyleState>();
    }
    return *style_state_;
}

const LayoutElement::StyleState* LayoutElement::style_state() const noexcept {
    return style_state_.get();
}

LayoutElement::Impl* LayoutElement::impl() noexcept {
    return impl_.get();
}

const LayoutElement::Impl* LayoutElement::impl() const noexcept {
    return impl_.get();
}

bool LayoutElement::has_config() const noexcept {
    return config_ != nullptr;
}

void LayoutElement::apply_callbacks_to_impl() noexcept {
    if (impl_ == nullptr) {
        return;
    }

    impl_->context().owner = this;
    impl_->context().measure_callback = &measure_callback_;
    impl_->context().baseline_callback = &baseline_callback_;
    impl_->context().dirtied_callback = &dirtied_callback_;
    YGNodeSetContext(impl_->node(), &impl_->context());
    YGNodeSetMeasureFunc(impl_->node(), measure_callback_ ? measure_layout_element : nullptr);
    YGNodeSetBaselineFunc(impl_->node(), baseline_callback_ ? baseline_layout_element : nullptr);
    YGNodeSetDirtiedFunc(impl_->node(), dirtied_callback_ ? dirtied_layout_element : nullptr);
}

void LayoutElement::apply_pending_style_to_impl() noexcept {
    if (impl_ == nullptr || style_state_ == nullptr) {
        return;
    }

    auto* node = impl_->node();
    const auto& style = *style_state_;
    if (style.element_kind) {
        YGNodeSetNodeType(node, detail::to_yoga(*style.element_kind));
    }
    if (style.reference_baseline) {
        YGNodeSetIsReferenceBaseline(node, *style.reference_baseline);
    }
    if (style.always_forms_containing_block) {
        YGNodeSetAlwaysFormsContainingBlock(node, *style.always_forms_containing_block);
    }
    if (style.direction) {
        YGNodeStyleSetDirection(node, detail::to_yoga(*style.direction));
    }
    if (style.flex_direction) {
        YGNodeStyleSetFlexDirection(node, detail::to_yoga(*style.flex_direction));
    }
    if (style.justify_content) {
        YGNodeStyleSetJustifyContent(node, detail::to_yoga(*style.justify_content));
    }
    if (style.align_content) {
        YGNodeStyleSetAlignContent(node, detail::to_yoga(*style.align_content));
    }
    if (style.align_items) {
        YGNodeStyleSetAlignItems(node, detail::to_yoga(*style.align_items));
    }
    if (style.align_self) {
        YGNodeStyleSetAlignSelf(node, detail::to_yoga(*style.align_self));
    }
    if (style.position_type) {
        YGNodeStyleSetPositionType(node, detail::to_yoga(*style.position_type));
    }
    if (style.flex_wrap) {
        YGNodeStyleSetFlexWrap(node, detail::to_yoga(*style.flex_wrap));
    }
    if (style.overflow) {
        YGNodeStyleSetOverflow(node, detail::to_yoga(*style.overflow));
    }
    if (style.display) {
        YGNodeStyleSetDisplay(node, detail::to_yoga(*style.display));
    }
    if (style.box_sizing) {
        YGNodeStyleSetBoxSizing(node, detail::to_yoga(*style.box_sizing));
    }
    if (style.flex) {
        YGNodeStyleSetFlex(node, *style.flex);
    }
    if (style.flex_grow) {
        YGNodeStyleSetFlexGrow(node, *style.flex_grow);
    }
    if (style.flex_shrink) {
        YGNodeStyleSetFlexShrink(node, *style.flex_shrink);
    }
    if (style.flex_basis) {
        set_length_value(
            *style.flex_basis, "flex basis", true,
            [node](float value) { YGNodeStyleSetFlexBasis(node, value); },
            [node](float value) { YGNodeStyleSetFlexBasisPercent(node, value); },
            [node]() { YGNodeStyleSetFlexBasisAuto(node); });
    }
    if (style.width) {
        set_length_value(
            *style.width, "width", true, [node](float value) { YGNodeStyleSetWidth(node, value); },
            [node](float value) { YGNodeStyleSetWidthPercent(node, value); },
            [node]() { YGNodeStyleSetWidthAuto(node); });
    }
    if (style.height) {
        set_length_value(
            *style.height, "height", true,
            [node](float value) { YGNodeStyleSetHeight(node, value); },
            [node](float value) { YGNodeStyleSetHeightPercent(node, value); },
            [node]() { YGNodeStyleSetHeightAuto(node); });
    }
    if (style.min_width) {
        set_length_value(
            *style.min_width, "min width", false,
            [node](float value) { YGNodeStyleSetMinWidth(node, value); },
            [node](float value) { YGNodeStyleSetMinWidthPercent(node, value); }, []() {});
    }
    if (style.min_height) {
        set_length_value(
            *style.min_height, "min height", false,
            [node](float value) { YGNodeStyleSetMinHeight(node, value); },
            [node](float value) { YGNodeStyleSetMinHeightPercent(node, value); }, []() {});
    }
    if (style.max_width) {
        set_length_value(
            *style.max_width, "max width", false,
            [node](float value) { YGNodeStyleSetMaxWidth(node, value); },
            [node](float value) { YGNodeStyleSetMaxWidthPercent(node, value); }, []() {});
    }
    if (style.max_height) {
        set_length_value(
            *style.max_height, "max height", false,
            [node](float value) { YGNodeStyleSetMaxHeight(node, value); },
            [node](float value) { YGNodeStyleSetMaxHeightPercent(node, value); }, []() {});
    }
    for (std::size_t index = 0; index < style.position.size(); ++index) {
        if (!style.position[index]) {
            continue;
        }
        const auto yoga_edge = detail::to_yoga(static_cast<Edge>(index));
        set_length_value(
            *style.position[index], "position", true,
            [node, yoga_edge](float value) { YGNodeStyleSetPosition(node, yoga_edge, value); },
            [node, yoga_edge](float value) {
                YGNodeStyleSetPositionPercent(node, yoga_edge, value);
            },
            [node, yoga_edge]() { YGNodeStyleSetPositionAuto(node, yoga_edge); });
    }
    for (std::size_t index = 0; index < style.margin.size(); ++index) {
        if (!style.margin[index]) {
            continue;
        }
        const auto yoga_edge = detail::to_yoga(static_cast<Edge>(index));
        set_length_value(
            *style.margin[index], "margin", true,
            [node, yoga_edge](float value) { YGNodeStyleSetMargin(node, yoga_edge, value); },
            [node, yoga_edge](float value) { YGNodeStyleSetMarginPercent(node, yoga_edge, value); },
            [node, yoga_edge]() { YGNodeStyleSetMarginAuto(node, yoga_edge); });
    }
    for (std::size_t index = 0; index < style.padding.size(); ++index) {
        if (!style.padding[index]) {
            continue;
        }
        const auto yoga_edge = detail::to_yoga(static_cast<Edge>(index));
        set_length_value(
            *style.padding[index], "padding", false,
            [node, yoga_edge](float value) { YGNodeStyleSetPadding(node, yoga_edge, value); },
            [node, yoga_edge](float value) {
                YGNodeStyleSetPaddingPercent(node, yoga_edge, value);
            },
            []() {});
    }
    for (std::size_t index = 0; index < style.border.size(); ++index) {
        if (style.border[index]) {
            YGNodeStyleSetBorder(node, detail::to_yoga(static_cast<Edge>(index)),
                                 *style.border[index]);
        }
    }
    for (std::size_t index = 0; index < style.gap.size(); ++index) {
        if (!style.gap[index]) {
            continue;
        }
        const auto yoga_gutter = detail::to_yoga(static_cast<Gutter>(index));
        set_length_value(
            *style.gap[index], "gap", false,
            [node, yoga_gutter](float value) { YGNodeStyleSetGap(node, yoga_gutter, value); },
            [node, yoga_gutter](float value) {
                YGNodeStyleSetGapPercent(node, yoga_gutter, value);
            },
            []() {});
    }
    if (style.aspect_ratio) {
        YGNodeStyleSetAspectRatio(node, *style.aspect_ratio);
    }
}

LayoutElement::Impl& LayoutElement::ensure_impl() {
    if (impl_ != nullptr) {
        return *impl_;
    }

    if (config_ == nullptr) {
        throw std::logic_error("layout element is not bound to a layout engine");
    }

    impl_ = std::make_unique<Impl>(config_);
    apply_callbacks_to_impl();
    apply_pending_style_to_impl();
    if (measure_dirty_pending_ && measure_callback_) {
        YGNodeMarkDirty(impl_->node());
    }
    measure_dirty_pending_ = false;

    for (std::size_t index = 0; index < children_.size(); ++index) {
        auto& child = *children_[index];
        child.bind_to_config(config_);
        YGNodeInsertChild(impl_->node(), child.ensure_impl().node(), index);
    }

    return *impl_;
}

void LayoutElement::drop_impl() noexcept {
    if (impl_ != nullptr) {
        YGNodeRemoveAllChildren(impl_->node());
        impl_.reset();
    }
}

void LayoutElement::reset_for_pool(std::shared_ptr<detail::YogaConfigHandle> config) {
    clear_children();
    drop_impl();
    config_ = std::move(config);
    parent_ = nullptr;
    measure_callback_ = {};
    baseline_callback_ = {};
    dirtied_callback_ = {};
    style_state_.reset();
    cached_frame_ = {};
    cached_layout_direction_ = Direction::LeftToRight;
    cached_layout_margin_ = {};
    cached_layout_padding_ = {};
    cached_layout_border_ = {};
    cached_has_new_layout_ = false;
    cached_had_overflow_ = false;
    measure_dirty_pending_ = false;
}

void LayoutElement::bind_to_config(std::shared_ptr<detail::YogaConfigHandle> config) {
    if (config_ == config) {
        return;
    }

    drop_impl();
    config_ = std::move(config);
    for (auto& child : children_) {
        child->bind_to_config(config_);
    }
}

void LayoutElement::bind_to_engine(const LayoutEngine& layout_engine) {
    bind_to_config(layout_engine.config_);
}

void LayoutElement::materialize_subtree() {
    static_cast<void>(ensure_impl());
}

LayoutElement* LayoutElement::parent() noexcept {
    return parent_;
}

const LayoutElement* LayoutElement::parent() const noexcept {
    return parent_;
}

std::size_t LayoutElement::child_count() const noexcept {
    return children_.size();
}

LayoutElement& LayoutElement::child_at(std::size_t index) {
    if (index >= children_.size()) {
        throw std::out_of_range("layout child index is out of range");
    }

    return *children_[index];
}

const LayoutElement& LayoutElement::child_at(std::size_t index) const {
    if (index >= children_.size()) {
        throw std::out_of_range("layout child index is out of range");
    }

    return *children_[index];
}

LayoutElement& LayoutElement::append_child(std::unique_ptr<LayoutElement> child) {
    return insert_child(children_.size(), std::move(child));
}

LayoutElement& LayoutElement::insert_child(std::size_t index,
                                           std::unique_ptr<LayoutElement> child) {
    if (!child) {
        throw std::invalid_argument("layout child must not be null");
    }

    if (index > children_.size()) {
        throw std::out_of_range("layout child insertion index is out of range");
    }

    if (measure_callback_) {
        throw std::logic_error("a measured layout element cannot own children");
    }

    if (child->parent_ != nullptr) {
        throw std::logic_error("layout child already has a parent");
    }

    if (config_ != nullptr) {
        child->bind_to_config(config_);
    } else if (child->config_ != nullptr) {
        bind_to_config(child->config_);
    }

    if (config_ != nullptr && child->config_ != config_) {
        throw std::logic_error("layout child was created by a different layout engine");
    }

    if (config_ != nullptr) {
        YGNodeInsertChild(ensure_impl().node(), child->ensure_impl().node(), index);
    }
    child->parent_ = this;
    const auto iterator = children_.begin() + static_cast<std::ptrdiff_t>(index);
    auto inserted = children_.insert(iterator, std::move(child));
    return **inserted;
}

std::unique_ptr<LayoutElement> LayoutElement::remove_child(LayoutElement& child) {
    const auto iterator =
        std::find_if(children_.begin(), children_.end(),
                     [&child](const auto& current_child) { return current_child.get() == &child; });

    if (iterator == children_.end()) {
        throw std::invalid_argument("layout element is not a child of this parent");
    }

    const auto index = static_cast<std::size_t>(std::distance(children_.begin(), iterator));
    return remove_child_at(index);
}

std::unique_ptr<LayoutElement> LayoutElement::remove_child_at(std::size_t index) {
    if (index >= children_.size()) {
        throw std::out_of_range("layout child removal index is out of range");
    }

    auto child = std::move(children_[index]);
    if (impl_ != nullptr && child->impl_ != nullptr) {
        YGNodeRemoveChild(impl_->node(), child->impl_->node());
    }
    child->parent_ = nullptr;
    children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(index));
    return child;
}

void LayoutElement::clear_children() {
    for (auto& child : children_) {
        child->parent_ = nullptr;
    }

    if (impl_ != nullptr) {
        YGNodeRemoveAllChildren(impl_->node());
    }
    children_.clear();
}

void LayoutElement::calculate_layout(LayoutConstraints constraints) {
    auto& backend = ensure_impl();
    YGNodeCalculateLayout(backend.node(),
                          available_or_undefined(constraints.width, "layout constraint width"),
                          available_or_undefined(constraints.height, "layout constraint height"),
                          detail::to_yoga(constraints.direction));
    cached_frame_ = frame();
    cached_layout_direction_ = layout_direction();
    cached_layout_margin_ = layout_margin();
    cached_layout_padding_ = layout_padding();
    cached_layout_border_ = layout_border();
    cached_has_new_layout_ = has_new_layout();
    cached_had_overflow_ = had_overflow();
}

Rect LayoutElement::frame() const noexcept {
    if (impl_ == nullptr) {
        return cached_frame_;
    }

    return {
        YGNodeLayoutGetLeft(impl_->node()),
        YGNodeLayoutGetTop(impl_->node()),
        YGNodeLayoutGetWidth(impl_->node()),
        YGNodeLayoutGetHeight(impl_->node()),
    };
}

Direction LayoutElement::layout_direction() const noexcept {
    if (impl_ == nullptr) {
        return cached_layout_direction_;
    }

    return detail::from_yoga(YGNodeLayoutGetDirection(impl_->node()));
}

EdgeInsets LayoutElement::layout_margin() const noexcept {
    if (impl_ == nullptr) {
        return cached_layout_margin_;
    }

    return {
        YGNodeLayoutGetMargin(impl_->node(), YGEdgeLeft),
        YGNodeLayoutGetMargin(impl_->node(), YGEdgeTop),
        YGNodeLayoutGetMargin(impl_->node(), YGEdgeRight),
        YGNodeLayoutGetMargin(impl_->node(), YGEdgeBottom),
    };
}

EdgeInsets LayoutElement::layout_padding() const noexcept {
    if (impl_ == nullptr) {
        return cached_layout_padding_;
    }

    return {
        YGNodeLayoutGetPadding(impl_->node(), YGEdgeLeft),
        YGNodeLayoutGetPadding(impl_->node(), YGEdgeTop),
        YGNodeLayoutGetPadding(impl_->node(), YGEdgeRight),
        YGNodeLayoutGetPadding(impl_->node(), YGEdgeBottom),
    };
}

EdgeInsets LayoutElement::layout_border() const noexcept {
    if (impl_ == nullptr) {
        return cached_layout_border_;
    }

    return {
        YGNodeLayoutGetBorder(impl_->node(), YGEdgeLeft),
        YGNodeLayoutGetBorder(impl_->node(), YGEdgeTop),
        YGNodeLayoutGetBorder(impl_->node(), YGEdgeRight),
        YGNodeLayoutGetBorder(impl_->node(), YGEdgeBottom),
    };
}

bool LayoutElement::has_new_layout() const noexcept {
    if (impl_ == nullptr) {
        return cached_has_new_layout_;
    }

    return YGNodeGetHasNewLayout(impl_->node());
}

void LayoutElement::clear_has_new_layout() noexcept {
    cached_has_new_layout_ = false;
    if (impl_ != nullptr) {
        YGNodeSetHasNewLayout(impl_->node(), false);
    }
}

bool LayoutElement::had_overflow() const noexcept {
    if (impl_ == nullptr) {
        return cached_had_overflow_;
    }

    return YGNodeLayoutGetHadOverflow(impl_->node());
}

bool LayoutElement::is_dirty() const noexcept {
    if (impl_ == nullptr) {
        return measure_dirty_pending_;
    }

    return YGNodeIsDirty(impl_->node());
}

LayoutElement& LayoutElement::set_measure_callback(MeasureCallback callback) {
    if (!children_.empty()) {
        throw std::logic_error("a layout element with children cannot be measured directly");
    }

    measure_callback_ = std::move(callback);
    if (has_config()) {
        YGNodeSetMeasureFunc(ensure_impl().node(),
                             measure_callback_ ? measure_layout_element : nullptr);
    }
    return *this;
}

LayoutElement& LayoutElement::clear_measure_callback() noexcept {
    measure_callback_ = nullptr;
    measure_dirty_pending_ = false;
    if (impl_ != nullptr) {
        YGNodeSetMeasureFunc(impl_->node(), nullptr);
    }
    return *this;
}

bool LayoutElement::has_measure_callback() const noexcept {
    return static_cast<bool>(measure_callback_);
}

LayoutElement& LayoutElement::mark_measure_dirty() {
    if (!measure_callback_) {
        throw std::logic_error("layout element has no measure callback to dirty");
    }

    if (impl_ != nullptr) {
        YGNodeMarkDirty(impl_->node());
    } else {
        measure_dirty_pending_ = true;
    }
    return *this;
}

LayoutElement& LayoutElement::set_baseline_callback(BaselineCallback callback) {
    baseline_callback_ = std::move(callback);
    if (has_config()) {
        YGNodeSetBaselineFunc(ensure_impl().node(),
                              baseline_callback_ ? baseline_layout_element : nullptr);
    }
    return *this;
}

LayoutElement& LayoutElement::clear_baseline_callback() noexcept {
    baseline_callback_ = nullptr;
    if (impl_ != nullptr) {
        YGNodeSetBaselineFunc(impl_->node(), nullptr);
    }
    return *this;
}

bool LayoutElement::has_baseline_callback() const noexcept {
    return static_cast<bool>(baseline_callback_);
}

LayoutElement& LayoutElement::set_dirtied_callback(DirtiedCallback callback) {
    dirtied_callback_ = std::move(callback);
    if (has_config()) {
        YGNodeSetDirtiedFunc(ensure_impl().node(),
                             dirtied_callback_ ? dirtied_layout_element : nullptr);
    }
    return *this;
}

LayoutElement& LayoutElement::clear_dirtied_callback() noexcept {
    dirtied_callback_ = nullptr;
    if (impl_ != nullptr) {
        YGNodeSetDirtiedFunc(impl_->node(), nullptr);
    }
    return *this;
}

bool LayoutElement::has_dirtied_callback() const noexcept {
    return static_cast<bool>(dirtied_callback_);
}

LayoutElement& LayoutElement::set_element_kind(ElementKind element_kind) {
    ensure_style_state().element_kind = element_kind;
    if (has_config()) {
        YGNodeSetNodeType(ensure_impl().node(), detail::to_yoga(element_kind));
    }
    return *this;
}

LayoutElement& LayoutElement::set_reference_baseline(bool reference_baseline) noexcept {
    ensure_style_state().reference_baseline = reference_baseline;
    if (impl_ != nullptr) {
        YGNodeSetIsReferenceBaseline(impl_->node(), reference_baseline);
    }
    return *this;
}

bool LayoutElement::is_reference_baseline() const noexcept {
    if (impl_ == nullptr) {
        return style_state_ != nullptr && style_state_->reference_baseline.value_or(false);
    }

    return YGNodeIsReferenceBaseline(impl_->node());
}

LayoutElement&
LayoutElement::set_always_forms_containing_block(bool always_forms_containing_block) noexcept {
    ensure_style_state().always_forms_containing_block = always_forms_containing_block;
    if (impl_ != nullptr) {
        YGNodeSetAlwaysFormsContainingBlock(impl_->node(), always_forms_containing_block);
    }
    return *this;
}

bool LayoutElement::always_forms_containing_block() const noexcept {
    if (impl_ == nullptr) {
        return style_state_ != nullptr &&
               style_state_->always_forms_containing_block.value_or(false);
    }

    return YGNodeGetAlwaysFormsContainingBlock(impl_->node());
}

LayoutElement& LayoutElement::set_direction(Direction direction) {
    ensure_style_state().direction = direction;
    if (has_config()) {
        YGNodeStyleSetDirection(ensure_impl().node(), detail::to_yoga(direction));
    }
    return *this;
}

LayoutElement& LayoutElement::set_flex_direction(FlexDirection flex_direction) {
    ensure_style_state().flex_direction = flex_direction;
    if (has_config()) {
        YGNodeStyleSetFlexDirection(ensure_impl().node(), detail::to_yoga(flex_direction));
    }
    return *this;
}

LayoutElement& LayoutElement::set_justify_content(JustifyContent justify_content) {
    ensure_style_state().justify_content = justify_content;
    if (has_config()) {
        YGNodeStyleSetJustifyContent(ensure_impl().node(), detail::to_yoga(justify_content));
    }
    return *this;
}

LayoutElement& LayoutElement::set_align_content(Align align_content) {
    ensure_style_state().align_content = align_content;
    if (has_config()) {
        YGNodeStyleSetAlignContent(ensure_impl().node(), detail::to_yoga(align_content));
    }
    return *this;
}

LayoutElement& LayoutElement::set_align_items(Align align_items) {
    ensure_style_state().align_items = align_items;
    if (has_config()) {
        YGNodeStyleSetAlignItems(ensure_impl().node(), detail::to_yoga(align_items));
    }
    return *this;
}

LayoutElement& LayoutElement::set_align_self(Align align_self) {
    ensure_style_state().align_self = align_self;
    if (has_config()) {
        YGNodeStyleSetAlignSelf(ensure_impl().node(), detail::to_yoga(align_self));
    }
    return *this;
}

LayoutElement& LayoutElement::set_position_type(PositionType position_type) {
    ensure_style_state().position_type = position_type;
    if (has_config()) {
        YGNodeStyleSetPositionType(ensure_impl().node(), detail::to_yoga(position_type));
    }
    return *this;
}

LayoutElement& LayoutElement::set_flex_wrap(Wrap flex_wrap) {
    ensure_style_state().flex_wrap = flex_wrap;
    if (has_config()) {
        YGNodeStyleSetFlexWrap(ensure_impl().node(), detail::to_yoga(flex_wrap));
    }
    return *this;
}

LayoutElement& LayoutElement::set_overflow(Overflow overflow) {
    ensure_style_state().overflow = overflow;
    if (has_config()) {
        YGNodeStyleSetOverflow(ensure_impl().node(), detail::to_yoga(overflow));
    }
    return *this;
}

LayoutElement& LayoutElement::set_display(Display display) {
    ensure_style_state().display = display;
    if (has_config()) {
        YGNodeStyleSetDisplay(ensure_impl().node(), detail::to_yoga(display));
    }
    return *this;
}

LayoutElement& LayoutElement::set_box_sizing(BoxSizing box_sizing) {
    ensure_style_state().box_sizing = box_sizing;
    if (has_config()) {
        YGNodeStyleSetBoxSizing(ensure_impl().node(), detail::to_yoga(box_sizing));
    }
    return *this;
}

LayoutElement& LayoutElement::set_flex(float flex) {
    validate_finite(flex, "flex");
    ensure_style_state().flex = flex;
    if (has_config()) {
        YGNodeStyleSetFlex(ensure_impl().node(), flex);
    }
    return *this;
}

LayoutElement& LayoutElement::set_flex_grow(float flex_grow) {
    validate_non_negative(flex_grow, "flex grow");
    ensure_style_state().flex_grow = flex_grow;
    if (has_config()) {
        YGNodeStyleSetFlexGrow(ensure_impl().node(), flex_grow);
    }
    return *this;
}

LayoutElement& LayoutElement::set_flex_shrink(float flex_shrink) {
    validate_non_negative(flex_shrink, "flex shrink");
    ensure_style_state().flex_shrink = flex_shrink;
    if (has_config()) {
        YGNodeStyleSetFlexShrink(ensure_impl().node(), flex_shrink);
    }
    return *this;
}

LayoutElement& LayoutElement::set_flex_basis(Length flex_basis) {
    validate_length(flex_basis, "flex basis", true);
    ensure_style_state().flex_basis = flex_basis;
    if (!has_config()) {
        return *this;
    }
    auto* node = ensure_impl().node();
    set_length_value(
        flex_basis, "flex basis", true,
        [node](float value) { YGNodeStyleSetFlexBasis(node, value); },
        [node](float value) { YGNodeStyleSetFlexBasisPercent(node, value); },
        [node]() { YGNodeStyleSetFlexBasisAuto(node); });
    return *this;
}

LayoutElement& LayoutElement::set_width(Length width) {
    validate_length(width, "width", true);
    ensure_style_state().width = width;
    if (!has_config()) {
        return *this;
    }
    auto* node = ensure_impl().node();
    set_length_value(
        width, "width", true, [node](float value) { YGNodeStyleSetWidth(node, value); },
        [node](float value) { YGNodeStyleSetWidthPercent(node, value); },
        [node]() { YGNodeStyleSetWidthAuto(node); });
    return *this;
}

LayoutElement& LayoutElement::set_height(Length height) {
    validate_length(height, "height", true);
    ensure_style_state().height = height;
    if (!has_config()) {
        return *this;
    }
    auto* node = ensure_impl().node();
    set_length_value(
        height, "height", true, [node](float value) { YGNodeStyleSetHeight(node, value); },
        [node](float value) { YGNodeStyleSetHeightPercent(node, value); },
        [node]() { YGNodeStyleSetHeightAuto(node); });
    return *this;
}

LayoutElement& LayoutElement::set_min_width(Length min_width) {
    validate_length(min_width, "min width", false);
    ensure_style_state().min_width = min_width;
    if (!has_config()) {
        return *this;
    }
    auto* node = ensure_impl().node();
    set_length_value(
        min_width, "min width", false, [node](float value) { YGNodeStyleSetMinWidth(node, value); },
        [node](float value) { YGNodeStyleSetMinWidthPercent(node, value); }, []() {});
    return *this;
}

LayoutElement& LayoutElement::set_min_height(Length min_height) {
    validate_length(min_height, "min height", false);
    ensure_style_state().min_height = min_height;
    if (!has_config()) {
        return *this;
    }
    auto* node = ensure_impl().node();
    set_length_value(
        min_height, "min height", false,
        [node](float value) { YGNodeStyleSetMinHeight(node, value); },
        [node](float value) { YGNodeStyleSetMinHeightPercent(node, value); }, []() {});
    return *this;
}

LayoutElement& LayoutElement::set_max_width(Length max_width) {
    validate_length(max_width, "max width", false);
    ensure_style_state().max_width = max_width;
    if (!has_config()) {
        return *this;
    }
    auto* node = ensure_impl().node();
    set_length_value(
        max_width, "max width", false, [node](float value) { YGNodeStyleSetMaxWidth(node, value); },
        [node](float value) { YGNodeStyleSetMaxWidthPercent(node, value); }, []() {});
    return *this;
}

LayoutElement& LayoutElement::set_max_height(Length max_height) {
    validate_length(max_height, "max height", false);
    ensure_style_state().max_height = max_height;
    if (!has_config()) {
        return *this;
    }
    auto* node = ensure_impl().node();
    set_length_value(
        max_height, "max height", false,
        [node](float value) { YGNodeStyleSetMaxHeight(node, value); },
        [node](float value) { YGNodeStyleSetMaxHeightPercent(node, value); }, []() {});
    return *this;
}

LayoutElement& LayoutElement::set_size(Length width, Length height) {
    set_width(width);
    set_height(height);
    return *this;
}

LayoutElement& LayoutElement::set_position(Edge edge, Length position) {
    validate_length(position, "position", true);
    ensure_style_state().position[edge_index(edge)] = position;
    if (!has_config()) {
        return *this;
    }
    const auto yoga_edge = detail::to_yoga(edge);
    auto* node = ensure_impl().node();
    set_length_value(
        position, "position", true,
        [node, yoga_edge](float value) { YGNodeStyleSetPosition(node, yoga_edge, value); },
        [node, yoga_edge](float value) { YGNodeStyleSetPositionPercent(node, yoga_edge, value); },
        [node, yoga_edge]() { YGNodeStyleSetPositionAuto(node, yoga_edge); });
    return *this;
}

LayoutElement& LayoutElement::set_margin(Edge edge, Length margin) {
    validate_length(margin, "margin", true);
    ensure_style_state().margin[edge_index(edge)] = margin;
    if (!has_config()) {
        return *this;
    }
    const auto yoga_edge = detail::to_yoga(edge);
    auto* node = ensure_impl().node();
    set_length_value(
        margin, "margin", true,
        [node, yoga_edge](float value) { YGNodeStyleSetMargin(node, yoga_edge, value); },
        [node, yoga_edge](float value) { YGNodeStyleSetMarginPercent(node, yoga_edge, value); },
        [node, yoga_edge]() { YGNodeStyleSetMarginAuto(node, yoga_edge); });
    return *this;
}

LayoutElement& LayoutElement::set_padding(Edge edge, Length padding) {
    validate_length(padding, "padding", false);
    ensure_style_state().padding[edge_index(edge)] = padding;
    if (!has_config()) {
        return *this;
    }
    const auto yoga_edge = detail::to_yoga(edge);
    auto* node = ensure_impl().node();
    set_length_value(
        padding, "padding", false,
        [node, yoga_edge](float value) { YGNodeStyleSetPadding(node, yoga_edge, value); },
        [node, yoga_edge](float value) { YGNodeStyleSetPaddingPercent(node, yoga_edge, value); },
        []() {});
    return *this;
}

LayoutElement& LayoutElement::set_border(Edge edge, float border_width) {
    validate_non_negative(border_width, "border width");
    ensure_style_state().border[edge_index(edge)] = border_width;
    if (has_config()) {
        YGNodeStyleSetBorder(ensure_impl().node(), detail::to_yoga(edge), border_width);
    }
    return *this;
}

LayoutElement& LayoutElement::set_gap(Gutter gutter, Length gap) {
    validate_length(gap, "gap", false);
    ensure_style_state().gap[gutter_index(gutter)] = gap;
    if (!has_config()) {
        return *this;
    }
    const auto yoga_gutter = detail::to_yoga(gutter);
    auto* node = ensure_impl().node();
    set_length_value(
        gap, "gap", false,
        [node, yoga_gutter](float value) { YGNodeStyleSetGap(node, yoga_gutter, value); },
        [node, yoga_gutter](float value) { YGNodeStyleSetGapPercent(node, yoga_gutter, value); },
        []() {});
    return *this;
}

LayoutElement& LayoutElement::set_aspect_ratio(float aspect_ratio) {
    validate_non_negative(aspect_ratio, "aspect ratio");
    ensure_style_state().aspect_ratio = aspect_ratio;
    if (has_config()) {
        YGNodeStyleSetAspectRatio(ensure_impl().node(), aspect_ratio);
    }
    return *this;
}

} // namespace winelement::layout
