#include <winelement/elements/ui_element.hpp>

#include <winelement/elements/event_router.hpp>
#include <winelement/elements/focus_manager.hpp>
#include <winelement/elements/theme_manager.hpp>
#include <winelement/layout/layout_engine.hpp>
#include <winelement/rendering/render_command_list.hpp>
#include <winelement/rendering/render_context.hpp>
#include <winelement/rendering/render_scene.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <utility>

namespace winelement::elements {
TextInputHandler::~TextInputHandler() = default;

namespace {

constexpr auto default_scroll_wheel_step = 48.0F;
constexpr auto ui_text_layout_cache_entries = 128U;

std::uint64_t next_local_theme_generation() noexcept {
    static std::atomic_uint64_t generation{std::uint64_t{1} << 63U};
    return generation.fetch_add(1U, std::memory_order_relaxed);
}

[[nodiscard]] layout::LayoutConstraints constraints_for_bounds(layout::Rect bounds) noexcept {
    return layout::LayoutConstraints{.width = std::max(bounds.width, 0.0F),
                                     .height = std::max(bounds.height, 0.0F)};
}

[[nodiscard]] bool has_visible_backdrop_top_layer(const TopLayerManager& manager) noexcept {
    return std::any_of(manager.entries().begin(), manager.entries().end(), [](const auto& entry) {
        return !entry.pending_removal && entry.element != nullptr &&
               entry.options.backdrop_color.alpha != 0U;
    });
}

[[nodiscard]] layout::Rect
effective_top_layer_bounds(layout::Rect bounds, layout::Rect host_frame,
                           const layout::LayoutConstraints& root_constraints) noexcept {
    const auto fallback_width = root_constraints.width.value_or(host_frame.width);
    const auto fallback_height = root_constraints.height.value_or(host_frame.height);
    bounds.width = std::isfinite(bounds.width) && bounds.width > 0.0F
                       ? bounds.width
                       : std::max(fallback_width, 0.0F);
    bounds.height = std::isfinite(bounds.height) && bounds.height > 0.0F
                        ? bounds.height
                        : std::max(fallback_height, 0.0F);
    if (!std::isfinite(bounds.x)) {
        bounds.x = host_frame.x + (std::max(fallback_width, 0.0F) - bounds.width) * 0.5F;
    }
    if (!std::isfinite(bounds.y)) {
        bounds.y = host_frame.y + (std::max(fallback_height, 0.0F) - bounds.height) * 0.5F;
    }
    return bounds;
}

[[nodiscard]] std::optional<layout::Point>
inverse_transform_point(layout::Point point, rendering::Transform2D transform) noexcept {
    const auto determinant = transform.m11 * transform.m22 - transform.m12 * transform.m21;
    if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001F) {
        return std::nullopt;
    }

    const auto x = point.x - transform.dx;
    const auto y = point.y - transform.dy;
    return layout::Point{(transform.m22 * x - transform.m21 * y) / determinant,
                         (transform.m11 * y - transform.m12 * x) / determinant};
}

[[nodiscard]] bool is_finite_transform(rendering::Transform2D transform) noexcept {
    return std::isfinite(transform.m11) && std::isfinite(transform.m12) &&
           std::isfinite(transform.m21) && std::isfinite(transform.m22) &&
           std::isfinite(transform.dx) && std::isfinite(transform.dy);
}

[[nodiscard]] std::optional<layout::Rect>
intersect_clip_rect(const std::optional<layout::Rect>& current, layout::Rect next) noexcept {
    if (!current.has_value()) {
        return next;
    }
    return layout::intersect_rects(*current, next);
}

[[nodiscard]] bool clip_rect_has_area(const std::optional<layout::Rect>& clip_rect) noexcept {
    return !clip_rect.has_value() || (clip_rect->width > 0.0F && clip_rect->height > 0.0F);
}

template <typename Value> void hash_combine(std::size_t& seed, const Value& value) noexcept {
    seed ^= std::hash<Value>{}(value) + 0x9E3779B9U + (seed << 6U) + (seed >> 2U);
}

void hash_rect(std::size_t& seed, layout::Rect rect) noexcept {
    hash_combine(seed, rect.x);
    hash_combine(seed, rect.y);
    hash_combine(seed, rect.width);
    hash_combine(seed, rect.height);
}

void hash_transform(std::size_t& seed, rendering::Transform2D transform) noexcept {
    hash_combine(seed, transform.m11);
    hash_combine(seed, transform.m12);
    hash_combine(seed, transform.m21);
    hash_combine(seed, transform.m22);
    hash_combine(seed, transform.dx);
    hash_combine(seed, transform.dy);
}

[[nodiscard]] layout::Rect
visual_bounds_for_scene_node(const rendering::RenderNode& node) noexcept {
    if (node.kind == rendering::RenderNodeKind::Layer) {
        return rendering::transform_rect(node.bounds, node.transform);
    }
    return node.bounds;
}

void refresh_scene_node_metadata(rendering::RenderNode& node) noexcept {
    for (auto& child : node.children) {
        refresh_scene_node_metadata(child);
    }

    auto computed_bounds = node.commands.bounds();
    for (const auto& child : node.children) {
        computed_bounds = layout::union_rects(computed_bounds, visual_bounds_for_scene_node(child));
    }

    if (node.kind == rendering::RenderNodeKind::Picture ||
        node.kind == rendering::RenderNodeKind::Surface ||
        (node.kind == rendering::RenderNodeKind::Layer && !node.clips_to_bounds)) {
        node.bounds = computed_bounds;
    }

    auto seed = static_cast<std::size_t>(0xA11CE5CEU);
    hash_combine(seed, static_cast<int>(node.kind));
    hash_rect(seed, node.bounds);
    hash_transform(seed, node.transform);
    hash_combine(seed, node.opacity);
    hash_combine(seed, node.clips_to_bounds);
    hash_combine(seed, node.commands.fingerprint());
    hash_combine(seed, node.commands.command_count());
    for (const auto& child : node.children) {
        hash_combine(seed, child.fingerprint);
    }
    node.fingerprint = static_cast<std::uint64_t>(seed);
}

[[nodiscard]] bool scene_node_has_payload(const rendering::RenderNode& node) noexcept {
    return !node.commands.empty() || !node.children.empty();
}

void append_cached_scene_node(rendering::RenderNode& parent,
                              const rendering::RenderCommandList& commands) {
    if (commands.empty()) {
        return;
    }
    auto node = rendering::render_node_from_commands(commands);
    if (scene_node_has_payload(node)) {
        parent.children.push_back(std::move(node));
    }
}

[[nodiscard]] std::pair<std::size_t, std::size_t> ordered_byte_range(std::size_t first,
                                                                     std::size_t second) noexcept {
    return {std::min(first, second), std::max(first, second)};
}

void apply_layout_margin(layout::LayoutElement& layout, layout::EdgeInsets margin) {
    layout.set_margin(layout::Edge::Left, layout::Length::points(margin.left))
        .set_margin(layout::Edge::Top, layout::Length::points(margin.top))
        .set_margin(layout::Edge::Right, layout::Length::points(margin.right))
        .set_margin(layout::Edge::Bottom, layout::Length::points(margin.bottom));
}

[[nodiscard]] const style::UIElementStyle& fallback_element_style() noexcept {
    return style::default_panel_style();
}

[[nodiscard]] const std::string& empty_text() noexcept {
    static const auto value = std::string{};
    return value;
}

[[nodiscard]] const rendering::TextStyle& default_text_style_value() noexcept {
    static const auto value =
        rendering::TextStyle{.font_size = 14.0F,
                             .color = rendering::Color::rgba(48, 49, 51),
                             .wrapping = rendering::TextWrapping::NoWrap,
                             .trimming = rendering::TextTrimming::CharacterEllipsis};
    return value;
}

[[nodiscard]] const std::optional<layout::Rect>& empty_viewport_override() noexcept {
    static const auto value = std::optional<layout::Rect>{};
    return value;
}

[[nodiscard]] const core::PropertyStore& empty_property_store() noexcept {
    static const auto value = core::PropertyStore{};
    return value;
}

[[nodiscard]] const std::vector<UIElement*>& empty_sorted_children_cache() noexcept {
    static const auto value = std::vector<UIElement*>{};
    return value;
}

} // namespace

struct UIElement::StyleState {
    std::unique_ptr<style::UIElementStyle> local_style;
    std::string theme_class = std::string(style::theme_class::panel);
    std::optional<style::Theme> local_theme;
    std::optional<layout::Rect> viewport_override;
    layout::Point scroll_offset{};
    const style::Theme* applied_theme = nullptr;
    std::uint64_t applied_theme_gen = std::numeric_limits<std::uint64_t>::max();
    bool theme_managed : 1 = true;
    bool scroll_wheel_enabled : 1 = true;
};

struct UIElement::TextState {
    std::string text;
    rendering::TextStyle text_style{.font_size = 14.0F,
                                    .color = rendering::Color::rgba(48, 49, 51),
                                    .wrapping = rendering::TextWrapping::NoWrap,
                                    .trimming = rendering::TextTrimming::CharacterEllipsis};
    std::unique_ptr<TextInputHandler> text_input_handler;
    std::size_t selection_anchor_byte_offset = 0;
    std::size_t selection_active_byte_offset = 0;
    bool intrinsic_measure_callback : 1 = false;
    bool custom_text_style : 1 = false;
    bool selecting : 1 = false;
};

struct UIElement::PropertyState {
    core::PropertyStore properties;
};

struct UIElement::AnimationState {
    animation::Storyboard implicit_property_animations;
};

struct UIElement::SemanticsElementState {
    SemanticsRole role = SemanticsRole::Generic;
    std::string label;
    bool dirty : 1 = true;
};

struct UIElement::RenderState {
    mutable RenderObject render_object;
    mutable layout::Rect visible_subtree_bounds{};
    mutable std::uint64_t visible_subtree_bounds_generation = 0;
    mutable bool visible_subtree_bounds_valid = false;
    mutable bool visible_subtree_bounds_needs_paint = false;
    bool repaint_boundary : 1 = false;
};

UIElement::StyleState& UIElement::ensure_style_state() {
    if (style_state_ == nullptr) {
        style_state_ = std::make_unique<StyleState>();
    }
    return *style_state_;
}

UIElement::TextState& UIElement::ensure_text_state() {
    if (text_state_ == nullptr) {
        text_state_ = std::make_unique<TextState>();
    }
    return *text_state_;
}

UIElement::SemanticsElementState& UIElement::ensure_semantics_state() {
    if (semantics_state_ == nullptr) {
        semantics_state_ = std::make_unique<SemanticsElementState>();
    }
    return *semantics_state_;
}

UIElement::RenderState& UIElement::ensure_render_state() const {
    if (render_state_ == nullptr) {
        render_state_ = std::make_unique<RenderState>();
    }
    return *render_state_;
}

const UIElement::TextState* UIElement::text_state() const noexcept {
    return text_state_.get();
}

const UIElement::SemanticsElementState* UIElement::semantics_state() const noexcept {
    return semantics_state_.get();
}

const UIElement::RenderState* UIElement::render_state() const noexcept {
    return render_state_.get();
}

style::UIElementStyle& UIElement::mutable_style_value() {
    auto& state = ensure_style_state();
    if (state.local_style == nullptr) {
        state.local_style = std::make_unique<style::UIElementStyle>(style_value());
    }
    return *state.local_style;
}

const style::UIElementStyle& UIElement::style_value() const noexcept {
    if (style_state_ != nullptr && style_state_->local_style != nullptr) {
        return *style_state_->local_style;
    }
    return fallback_element_style();
}

style::UIElementStyle& UIElement::style_storage() {
    return mutable_style_value();
}

const style::UIElementStyle& UIElement::style_storage() const noexcept {
    return style_value();
}

std::string& UIElement::text_storage() noexcept {
    return ensure_text_state().text;
}

const std::string& UIElement::text_storage() const noexcept {
    return text_state_ != nullptr ? text_state_->text : empty_text();
}

rendering::TextStyle& UIElement::text_style_storage() noexcept {
    return ensure_text_state().text_style;
}

const rendering::TextStyle& UIElement::text_style_storage() const noexcept {
    return text_state_ != nullptr ? text_state_->text_style : default_text_style_value();
}

core::PropertyStore& UIElement::property_store() noexcept {
    if (property_state_ == nullptr) {
        property_state_ = std::make_unique<PropertyState>();
    }
    return property_state_->properties;
}

animation::Storyboard& UIElement::implicit_property_animations() noexcept {
    if (animation_state_ == nullptr) {
        animation_state_ = std::make_unique<AnimationState>();
    }
    return animation_state_->implicit_property_animations;
}

std::optional<layout::Rect>& UIElement::mutable_viewport_override() {
    return ensure_style_state().viewport_override;
}

const std::optional<layout::Rect>& UIElement::viewport_override() const noexcept {
    return style_state_ != nullptr ? style_state_->viewport_override : empty_viewport_override();
}

layout::Point& UIElement::mutable_scroll_offset() {
    return ensure_style_state().scroll_offset;
}

layout::Point UIElement::scroll_offset_value() const noexcept {
    return style_state_ != nullptr ? style_state_->scroll_offset : layout::Point{};
}

bool UIElement::scroll_wheel_enabled_value() const noexcept {
    return style_state_ == nullptr || style_state_->scroll_wheel_enabled;
}

void UIElement::set_scroll_wheel_enabled_value(bool enabled) {
    ensure_style_state().scroll_wheel_enabled = enabled;
}

void UIElement::reset_text_selection_state() noexcept {
    if (text_state_ == nullptr) {
        return;
    }
    text_state_->selecting = false;
    text_state_->selection_anchor_byte_offset = 0U;
    text_state_->selection_active_byte_offset = 0U;
}

void UIElement::invalidate_render_commands() noexcept {
    if (render_state_ != nullptr) {
        render_state_->render_object.invalidate_commands();
    }
}

void UIElement::adopt_text_clipboard_service(
    std::shared_ptr<TextClipboardService> service) noexcept {
    if (service == nullptr) {
        return;
    }

    text_clipboard_service_ = std::move(service);
    for (auto& child : children_) {
        child->adopt_text_clipboard_service(text_clipboard_service_);
    }
    for (auto& entry : top_layer_manager_.entries()) {
        if (entry.element != nullptr) {
            entry.element->adopt_text_clipboard_service(text_clipboard_service_);
        }
    }
}

void UIElement::ensure_sorted_children() const {
    if (!z_order_dirty_ && sorted_children_cache_ != nullptr) {
        return;
    }
    z_order_dirty_ = false;

    if (children_.empty()) {
        sorted_children_cache_.reset();
        return;
    }

    if (sorted_children_cache_ == nullptr) {
        sorted_children_cache_ = std::make_unique<std::vector<UIElement*>>();
    }

    auto& sorted_children = *sorted_children_cache_;
    sorted_children.clear();
    sorted_children.reserve(children_.size());
    auto has_custom_z_index = false;
    for (auto& child : children_) {
        sorted_children.push_back(child.get());
        has_custom_z_index = has_custom_z_index || child->z_index() != 0;
    }
    if (!has_custom_z_index) {
        return;
    }
    if (std::is_sorted(sorted_children.begin(), sorted_children.end(),
                       [](const UIElement* a, const UIElement* b) noexcept {
                           return a->z_index() < b->z_index();
                       })) {
        return;
    }
    std::stable_sort(sorted_children.begin(), sorted_children.end(),
                     [](const UIElement* a, const UIElement* b) noexcept {
                         return a->z_index() < b->z_index();
                     });
}

const std::vector<UIElement*>& UIElement::sorted_children() const noexcept {
    const_cast<UIElement*>(this)->ensure_sorted_children();
    return sorted_children_cache_ != nullptr ? *sorted_children_cache_
                                             : empty_sorted_children_cache();
}

void UIElement::mark_z_order_dirty() noexcept {
    z_order_dirty_ = true;
    if (parent_ != nullptr) {
        parent_->mark_z_order_dirty();
    }
}

UIElement::UIElement()
    : layout_owner_(std::unique_ptr<layout::LayoutElement>(new layout::LayoutElement())),
      layout_(layout_owner_.get()), owner_thread_id_(std::this_thread::get_id()),
      text_clipboard_service_(std::make_shared<TextClipboardService>()) {
    if (layout_ == nullptr) {
        throw std::bad_alloc();
    }

    children_.reserve(inline_child_capacity);
    layout_->set_dirtied_callback([this](layout::LayoutElement&) { invalidate_layout(); });
}

UIElement::~UIElement() noexcept {
    try {
        clear_layout_callbacks_recursive_noexcept();
        for (auto& entry : top_layer_manager_.entries()) {
            if (entry.element != nullptr) {
                entry.element->parent_ = nullptr;
                entry.element->logical_owner_ = nullptr;
                entry.options.logical_owner = nullptr;
            }
        }

        if (event_router_ != nullptr) {
            auto* event_router = event_router_;
            event_router->on_element_detaching(*this);
            detach_event_router(*event_router);
            if (event_router->root_ == this) {
                event_router->root_ = nullptr;
            }
        }

        if (focus_manager_ != nullptr) {
            auto* focus_manager = focus_manager_;
            focus_manager->on_focus_scope_destroying(*this);
            detach_focus_manager(*focus_manager);
            if (focus_manager->root_ == this) {
                focus_manager->root_ = nullptr;
            }
        }

        children_.clear();
        top_layer_manager_.entries().clear();
        text_state_.reset();
    } catch (...) {
        assert(false && "UIElement destructor must not throw");
    }
}

UIElement* UIElement::parent() noexcept {
    return parent_;
}

const UIElement* UIElement::parent() const noexcept {
    return parent_;
}

std::size_t UIElement::child_count() const noexcept {
    return children_.size();
}

UIElement& UIElement::child_at(std::size_t index) {
    verify_thread_access();

    if (index >= children_.size()) {
        throw std::out_of_range("ui child index is out of range");
    }

    return *children_[index];
}

const UIElement& UIElement::child_at(std::size_t index) const {
    verify_thread_access();

    if (index >= children_.size()) {
        throw std::out_of_range("ui child index is out of range");
    }

    return *children_[index];
}

UIElement& UIElement::append_child(std::unique_ptr<UIElement> child) {
    return insert_child(children_.size(), std::move(child));
}

UIElement& UIElement::insert_child(std::size_t index, std::unique_ptr<UIElement> child) {
    verify_thread_access();

    if (!child) {
        throw std::invalid_argument("ui child must not be null");
    }

    if (index > children_.size()) {
        throw std::out_of_range("ui child insertion index is out of range");
    }

    if (child->parent_ != nullptr) {
        throw std::logic_error("ui child already has a parent");
    }

    child->validate_detached_from_managers();
    child->adopt_thread_access(owner_thread_id_);
    child->adopt_text_clipboard_service(text_clipboard_service_);
    if (layout_engine_ != nullptr) {
        child->bind_layout_tree(*layout_engine_);
    } else if (child->layout_engine_ != nullptr) {
        child->clear_layout_engine_binding();
    }
    child->logical_owner_ = nullptr;
    auto* current_text_state = text_state_.get();
    if (current_text_state != nullptr && current_text_state->intrinsic_measure_callback) {
        layout_->clear_measure_callback();
        current_text_state->intrinsic_measure_callback = false;
    }
    auto child_layout = child->take_layout_ownership();
    layout_->insert_child(index, std::move(child_layout));
    child->parent_ = this;
    child->root_ = root_ != nullptr ? root_ : this;
    const auto iterator = children_.begin() + static_cast<std::ptrdiff_t>(index);
    auto inserted = children_.insert(iterator, std::move(child));
    (*inserted)->mark_theme_dirty();
    mark_z_order_dirty();
    if (event_router_ != nullptr) {
        (*inserted)->attach_event_router(*event_router_);
    }
    if (focus_manager_ != nullptr) {
        (*inserted)->attach_focus_manager(*focus_manager_);
    }
    invalidate_layout();
    return **inserted;
}

std::unique_ptr<UIElement> UIElement::remove_child(UIElement& child) {
    verify_thread_access();

    const auto iterator =
        std::find_if(children_.begin(), children_.end(),
                     [&child](const auto& current_child) { return current_child.get() == &child; });

    if (iterator == children_.end()) {
        throw std::invalid_argument("ui element is not a child of this parent");
    }

    const auto index = static_cast<std::size_t>(std::distance(children_.begin(), iterator));
    return remove_child_at(index);
}

std::unique_ptr<UIElement> UIElement::remove_child_at(std::size_t index) {
    verify_thread_access();

    if (index >= children_.size()) {
        throw std::out_of_range("ui child removal index is out of range");
    }

    auto& child_ref = *children_[index];
    if (event_router_ != nullptr) {
        event_router_->on_element_detaching(child_ref);
    }
    if (focus_manager_ != nullptr) {
        focus_manager_->on_focus_scope_invalidated(child_ref);
    }

    auto& host = top_layer_host();
    if (host.layout_dirty_root_ != nullptr && child_ref.contains(*host.layout_dirty_root_)) {
        host.layout_dirty_root_ = this;
    }
    host.remove_logical_descendant_top_layer_entries(child_ref, 0U);
    host.clear_top_layer_logical_owner_references(child_ref);

    auto child = std::move(children_[index]);
    auto child_layout = layout_->remove_child(*child->layout_);
    child->restore_layout_ownership(std::move(child_layout));
    child->parent_ = nullptr;
    child->clear_layout_engine_binding();
    if (event_router_ != nullptr) {
        child->detach_event_router(*event_router_);
    }
    if (focus_manager_ != nullptr) {
        child->detach_focus_manager(*focus_manager_);
    }
    children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(index));
    mark_z_order_dirty();
    update_intrinsic_text_measure_callback();
    invalidate_layout();
    return child;
}

void UIElement::clear_children() {
    verify_thread_access();

    while (!children_.empty()) {
        static_cast<void>(remove_child_at(children_.size() - 1));
    }
}

UIElement& UIElement::push_top_layer_entry(std::unique_ptr<UIElement> element,
                                           TopLayerOptions options, std::uint64_t* entry_id) {
    auto& host = top_layer_host();
    host.verify_thread_access();

    if (!element) {
        throw std::invalid_argument("top layer element must not be null");
    }

    if (element->parent_ != nullptr) {
        throw std::logic_error("top layer element already has a parent");
    }

    if (options.logical_owner != nullptr) {
        options.logical_owner->verify_thread_access();
        if (&options.logical_owner->top_layer_host() != &host) {
            throw std::invalid_argument("top layer logical owner must belong to the same host");
        }
    }

    if (options.logical_owner != nullptr) {
        for (const auto& entry : host.top_layer_manager_.entries()) {
            if (entry.element->contains(*options.logical_owner)) {
                static_cast<void>(host.bring_top_layer_entry_to_front(entry.id));
                break;
            }
        }
    }

    element->validate_detached_from_managers();
    element->adopt_thread_access(host.owner_thread_id_);
    element->adopt_text_clipboard_service(host.text_clipboard_service_);
    element->bind_layout_tree(host.ensure_layout_engine());
    element->logical_owner_ = options.logical_owner;
    element->parent_ = &host;
    element->mark_theme_dirty();
    if (host.event_router_ != nullptr) {
        element->attach_event_router(*host.event_router_);
    }
    if (host.focus_manager_ != nullptr) {
        element->attach_focus_manager(*host.focus_manager_);
    }

    auto& element_ref = *element;
    const auto id = host.top_layer_manager_.allocate_entry_id();
    host.top_layer_manager_.entries().push_back(
        TopLayerEntry{.element = std::move(element), .options = options, .id = id});
    auto& entry = host.top_layer_manager_.entries().back();
    host.refresh_top_layer_entry_logical_ancestors(entry);
    if (entry.options.modal && host.focus_manager_ != nullptr) {
        auto* focused = host.focus_manager_->focused_element();
        if (focused != nullptr && !entry.element->contains_logical(*focused)) {
            static_cast<void>(host.focus_manager_->clear_focus());
        }
    }
    if (host.layout_generation_ != 0U) {
        layout::LayoutConstraints root_constraints;
        root_constraints.width = host.committed_absolute_frame_.width;
        root_constraints.height = host.committed_absolute_frame_.height;
        const auto bounds = effective_top_layer_bounds(
            options.bounds, host.committed_absolute_frame_, root_constraints);
        entry.element->layout_->calculate_layout(constraints_for_bounds(bounds));
        entry.element->sync_top_layer_snapshot_subtree(bounds, host.layout_generation_);
    }
    if (entry_id != nullptr) {
        *entry_id = id;
    }

    ThemeManager::reapply_current_theme(*entry.element);
    if (host.focus_manager_ != nullptr) {
        host.focus_manager_->invalidate_focusable_cache();
    }
    host.invalidate_layout();
    if (entry.options.modal || entry.options.backdrop_color.alpha != 0) {
        host.invalidate_paint();
    }
    return element_ref;
}

std::unique_ptr<UIElement> UIElement::remove_top_layer_entry(std::size_t index) {
    auto& host = top_layer_host();
    host.verify_thread_access();
    if (index >= host.top_layer_manager_.entries().size()) {
        throw std::out_of_range("top layer index is out of range");
    }

    auto iterator = host.top_layer_manager_.entries().begin() + static_cast<std::ptrdiff_t>(index);
    const auto entry_id = iterator->id;
    host.remove_logical_descendant_top_layer_entries(*iterator->element, entry_id);
    iterator = std::find_if(host.top_layer_manager_.entries().begin(),
                            host.top_layer_manager_.entries().end(),
                            [entry_id](const auto& entry) { return entry.id == entry_id; });
    if (iterator == host.top_layer_manager_.entries().end()) {
        throw std::logic_error("top layer entry disappeared while removing logical descendants");
    }
    host.clear_top_layer_logical_owner_references(*iterator->element);
    const auto affects_backdrop =
        iterator->options.modal || iterator->options.backdrop_color.alpha != 0;
    auto on_dismissed = std::move(iterator->options.on_dismissed);
    auto removed = std::move(iterator->element);
    if (host.layout_dirty_root_ != nullptr && removed->contains(*host.layout_dirty_root_)) {
        host.layout_dirty_root_ = &host;
    }
    if (host.event_router_ != nullptr) {
        host.event_router_->on_element_detaching(*removed);
        removed->detach_event_router(*host.event_router_);
    }
    if (host.focus_manager_ != nullptr) {
        host.focus_manager_->on_focus_scope_invalidated(*removed);
        removed->detach_focus_manager(*host.focus_manager_);
    }
    removed->parent_ = nullptr;
    removed->logical_owner_ = nullptr;
    removed->clear_layout_engine_binding();
    host.top_layer_manager_.entries().erase(iterator);
    if (host.focus_manager_ != nullptr) {
        host.focus_manager_->invalidate_focusable_cache();
    }
    host.invalidate_layout();
    if (affects_backdrop) {
        host.invalidate_paint();
    }
    if (on_dismissed) {
        on_dismissed();
    }
    return removed;
}

bool UIElement::remove_top_layer_entry_by_id(std::uint64_t entry_id) {
    auto& host = top_layer_host();
    host.verify_thread_access();
    const auto index = host.top_layer_manager_.index_of(entry_id);
    if (!index.has_value()) {
        return false;
    }

    static_cast<void>(host.remove_top_layer_entry(*index));
    return true;
}

bool UIElement::mark_top_layer_entry_pending_removal(std::uint64_t entry_id) {
    auto& host = top_layer_host();
    host.verify_thread_access();
    const auto index = host.top_layer_manager_.index_of(entry_id);
    if (!index.has_value()) {
        return false;
    }

    auto iterator = host.top_layer_manager_.entries().begin() + static_cast<std::ptrdiff_t>(*index);
    iterator->pending_removal = true;
    iterator->options.light_dismiss = false;
    iterator->element->visible_ = false;
    host.mark_logical_descendant_top_layer_entries_pending_removal(*iterator->element, entry_id);
    host.invalidate_layout();
    if (iterator->options.modal || iterator->options.backdrop_color.alpha != 0) {
        host.invalidate_paint();
    }
    return true;
}

UIElement* UIElement::top_layer_entry_element(std::uint64_t entry_id) noexcept {
    auto& host = top_layer_host();
    const auto index = host.top_layer_manager_.index_of(entry_id);
    return index.has_value() ? host.top_layer_manager_.entries()[*index].element.get() : nullptr;
}

const UIElement* UIElement::top_layer_entry_element(std::uint64_t entry_id) const noexcept {
    const auto& host = top_layer_host();
    const auto index = host.top_layer_manager_.index_of(entry_id);
    return index.has_value() ? host.top_layer_manager_.entries()[*index].element.get() : nullptr;
}

bool UIElement::set_top_layer_entry_bounds(std::uint64_t entry_id, layout::Rect bounds) {
    auto& host = top_layer_host();
    host.verify_thread_access();
    const auto index = host.top_layer_manager_.index_of(entry_id);
    if (!index.has_value()) {
        return false;
    }

    auto iterator = host.top_layer_manager_.entries().begin() + static_cast<std::ptrdiff_t>(*index);
    const auto current_bounds = iterator->options.bounds;
    if (current_bounds.x == bounds.x && current_bounds.y == bounds.y &&
        current_bounds.width == bounds.width && current_bounds.height == bounds.height) {
        return true;
    }

    iterator->options.bounds = bounds;
    if (host.layout_generation_ != 0U) {
        layout::LayoutConstraints root_constraints;
        root_constraints.width = host.committed_absolute_frame_.width;
        root_constraints.height = host.committed_absolute_frame_.height;
        const auto effective_bounds =
            effective_top_layer_bounds(bounds, host.committed_absolute_frame_, root_constraints);
        iterator->element->layout_->calculate_layout(constraints_for_bounds(effective_bounds));
        iterator->element->sync_top_layer_snapshot_subtree(effective_bounds,
                                                           host.layout_generation_);
    }
    host.invalidate_layout();
    return true;
}

bool UIElement::bring_top_layer_entry_to_front(std::uint64_t entry_id) {
    auto& host = top_layer_host();
    host.verify_thread_access();
    const auto index = host.top_layer_manager_.index_of(entry_id);
    if (!index.has_value()) {
        return false;
    }
    const auto& logical_owner_root = *host.top_layer_manager_.entries()[*index].element;
    auto moving_indices = host.top_layer_manager_.logical_descendant_indices_of(logical_owner_root);
    if (moving_indices.empty()) {
        moving_indices.push_back(*index);
    }
    auto moving_entries = std::vector<TopLayerEntry>{};
    moving_entries.reserve(moving_indices.size());
    auto& entries = host.top_layer_manager_.entries();
    auto remaining_entries = std::vector<TopLayerEntry>{};
    remaining_entries.reserve(entries.size() - std::min(entries.size(), moving_indices.size()));
    for (std::size_t current_index = 0; current_index < entries.size(); ++current_index) {
        if (std::binary_search(moving_indices.begin(), moving_indices.end(), current_index)) {
            moving_entries.push_back(std::move(entries[current_index]));
        } else {
            remaining_entries.push_back(std::move(entries[current_index]));
        }
    }

    if (moving_entries.empty()) {
        return false;
    }
    entries = std::move(remaining_entries);
    entries.reserve(entries.size() + moving_entries.size());
    for (auto& entry : moving_entries) {
        entries.push_back(std::move(entry));
    }
    host.clear_focus_outside_topmost_modal();
    host.invalidate_paint();
    return true;
}

bool UIElement::top_layer_entry_preserves_focus_for(const UIElement& element) const noexcept {
    const auto& host = top_layer_host();
    for (const auto& entry : host.top_layer_manager_.entries()) {
        if (!entry.pending_removal && entry.options.preserve_focus &&
            entry.element->contains(element)) {
            return true;
        }
    }
    return false;
}

void UIElement::clear_focus_outside_topmost_modal() noexcept {
    if (focus_manager_ == nullptr) {
        return;
    }

    auto* modal_scope = focus_manager_->topmost_modal_scope();
    auto* focused = focus_manager_->focused_element();
    if (modal_scope != nullptr && focused != nullptr && !modal_scope->contains_logical(*focused)) {
        static_cast<void>(focus_manager_->clear_focus());
    }
}

void UIElement::sanitize_pending_top_layer_result(RoutedEventResult& result) const noexcept {
    const auto& host = top_layer_host();
    for (const auto& entry : host.top_layer_manager_.entries()) {
        if (!entry.pending_removal) {
            continue;
        }
        if (result.target != nullptr && entry.element->contains(*result.target)) {
            result.target = nullptr;
        }
        if (result.handled_by != nullptr && entry.element->contains(*result.handled_by)) {
            result.handled_by = nullptr;
        }
    }
}

void UIElement::flush_pending_top_layer_removals() {
    auto& host = top_layer_host();
    host.verify_thread_access();
    auto index = host.top_layer_manager_.entries().size();
    while (index > 0U) {
        --index;
        if (host.top_layer_manager_.entries()[index].pending_removal) {
            static_cast<void>(host.remove_top_layer_entry(index));
        }
    }
}

UIElement& UIElement::push_top_layer(std::unique_ptr<UIElement> element, TopLayerOptions options) {
    return top_layer_host().push_top_layer_entry(std::move(element), options, nullptr);
}

std::unique_ptr<UIElement> UIElement::remove_top_layer(UIElement& element) {
    auto& host = top_layer_host();
    host.verify_thread_access();

    const auto index = host.top_layer_manager_.index_of(element);
    if (!index.has_value()) {
        throw std::invalid_argument("ui element is not in this top layer");
    }

    return host.remove_top_layer_entry(*index);
}

void UIElement::clear_top_layer() {
    auto& host = top_layer_host();
    host.verify_thread_access();

    while (!host.top_layer_manager_.entries().empty()) {
        static_cast<void>(host.remove_top_layer(*host.top_layer_manager_.entries().back().element));
    }
}

void UIElement::dismiss_own_top_layer() {
    auto& host = top_layer_host();
    host.verify_thread_access();

    for (auto* current = this; current != nullptr && current != &host; current = current->parent_) {
        if (current->parent_ != &host) {
            continue;
        }
        const auto index = host.top_layer_manager_.index_of(*current);
        if (!index.has_value()) {
            continue;
        }
        const auto& entry = host.top_layer_manager_.entries()[*index];
        if (entry.pending_removal) {
            return;
        }
        static_cast<void>(host.mark_top_layer_entry_pending_removal(entry.id));
        if (host.animation_tick_depth_ == 0U &&
            (host.event_router_ == nullptr || !host.event_router_->dispatch_active())) {
            host.flush_pending_top_layer_removals();
        }
        return;
    }
}

void UIElement::dismiss_topmost_on_escape() {
    auto& host = top_layer_host();
    host.verify_thread_access();

    const auto index = host.top_layer_manager_.topmost_escape_dismiss_index();
    if (!index.has_value()) {
        return;
    }
    static_cast<void>(
        host.mark_top_layer_entry_pending_removal(host.top_layer_manager_.entries()[*index].id));
    if (host.animation_tick_depth_ == 0U &&
        (host.event_router_ == nullptr || !host.event_router_->dispatch_active())) {
        host.flush_pending_top_layer_removals();
    }
}

bool UIElement::light_dismiss_outside(layout::Point position) {
    auto& host = top_layer_host();
    host.verify_thread_access();

    const auto index = host.top_layer_manager_.topmost_light_dismiss_index(position);
    if (!index.has_value()) {
        return false;
    }
    static_cast<void>(
        host.mark_top_layer_entry_pending_removal(host.top_layer_manager_.entries()[*index].id));
    return true;
}

UIElement& UIElement::bring_top_layer_to_front(UIElement& element) {
    auto& host = top_layer_host();
    host.verify_thread_access();

    const auto index = host.top_layer_manager_.index_of(element);
    if (!index.has_value()) {
        throw std::invalid_argument("ui element is not in this top layer");
    }

    if (!host.bring_top_layer_entry_to_front(host.top_layer_manager_.entries()[*index].id)) {
        return *this;
    }
    return *this;
}

UIElement& UIElement::set_top_layer_bounds(UIElement& element, layout::Rect bounds) {
    auto& host = top_layer_host();
    host.verify_thread_access();

    const auto index = host.top_layer_manager_.index_of(element);
    if (!index.has_value()) {
        throw std::invalid_argument("ui element is not in this top layer");
    }

    static_cast<void>(
        host.set_top_layer_entry_bounds(host.top_layer_manager_.entries()[*index].id, bounds));
    return *this;
}

std::size_t UIElement::top_layer_count() const noexcept {
    return top_layer_host().top_layer_manager_.entries().size();
}

UIElement& UIElement::top_layer_at(std::size_t index) {
    auto& host = top_layer_host();
    host.verify_thread_access();
    if (index >= host.top_layer_manager_.entries().size()) {
        throw std::out_of_range("top layer index is out of range");
    }
    return *host.top_layer_manager_.entries()[index].element;
}

const UIElement& UIElement::top_layer_at(std::size_t index) const {
    const auto& host = top_layer_host();
    host.verify_thread_access();
    if (index >= host.top_layer_manager_.entries().size()) {
        throw std::out_of_range("top layer index is out of range");
    }
    return *host.top_layer_manager_.entries()[index].element;
}

layout::Rect UIElement::top_layer_bounds(const UIElement& element) const {
    const auto& host = top_layer_host();
    host.verify_thread_access();
    const auto index = host.top_layer_manager_.index_of(element);
    if (!index.has_value()) {
        throw std::invalid_argument("ui element is not in this top layer");
    }

    layout::LayoutConstraints root_constraints;
    root_constraints.width = host.committed_absolute_frame_.width;
    root_constraints.height = host.committed_absolute_frame_.height;
    return effective_top_layer_bounds(host.top_layer_manager_.entries()[*index].options.bounds,
                                      host.committed_absolute_frame_, root_constraints);
}

UIElement* UIElement::hit_test_top_layer(layout::Point absolute_point) noexcept {
    auto& host = top_layer_host();
    for (auto iterator = host.top_layer_manager_.entries().rbegin();
         iterator != host.top_layer_manager_.entries().rend(); ++iterator) {
        if (iterator->pending_removal || !iterator->element->visible_) {
            continue;
        }
        if (auto* hit = iterator->element->hit_test_subtree(absolute_point)) {
            return hit;
        }
    }
    return nullptr;
}

const UIElement* UIElement::hit_test_top_layer(layout::Point absolute_point) const noexcept {
    const auto& host = top_layer_host();
    for (auto iterator = host.top_layer_manager_.entries().rbegin();
         iterator != host.top_layer_manager_.entries().rend(); ++iterator) {
        if (iterator->pending_removal || !iterator->element->visible_) {
            continue;
        }
        if (const auto* hit = iterator->element->hit_test_subtree(absolute_point)) {
            return hit;
        }
    }
    return nullptr;
}

const layout::LayoutElement& UIElement::layout() const noexcept {
    return *layout_;
}

layout::LayoutElement& UIElement::mutable_layout() noexcept {
    return *layout_;
}

layout::Rect UIElement::frame() const noexcept {
    return committed_frame_;
}

layout::Rect UIElement::absolute_frame() const noexcept {
    return committed_absolute_frame_;
}

std::uint64_t UIElement::layout_generation() const noexcept {
    return layout_generation_;
}

UIElement& UIElement::set_text_clipboard_service(std::shared_ptr<TextClipboardService> service) {
    verify_thread_access();
    if (service == nullptr) {
        throw std::invalid_argument("text clipboard service must not be null");
    }
    adopt_text_clipboard_service(std::move(service));
    return *this;
}

TextClipboardService& UIElement::text_clipboard_service() noexcept {
    if (text_clipboard_service_ == nullptr) {
        text_clipboard_service_ = std::make_shared<TextClipboardService>();
    }
    return *text_clipboard_service_;
}

const TextClipboardService& UIElement::text_clipboard_service() const noexcept {
    return *text_clipboard_service_;
}

ElementSnapshot UIElement::element_snapshot() const {
    verify_thread_access();
    return build_element_snapshot_subtree();
}

ElementSnapshot UIElement::build_element_snapshot_subtree() const {
    const auto& style_state = style_state_;
    ElementSnapshot snapshot{.role = ElementTreeRole::Element,
                             .type_name = typeid(*this).name(),
                             .key = {},
                             .theme_class = style_state != nullptr
                                                ? style_state->theme_class
                                                : std::string(style::theme_class::panel),
                             .layout_generation = layout_generation_,
                             .frame = committed_frame_,
                             .absolute_frame = committed_absolute_frame_,
                             .relayout_boundary = relayout_boundary_,
                             .needs_layout = needs_layout_,
                             .visible = visible_};

    snapshot.children.reserve(children_.size() + top_layer_manager_.entries().size());
    for (const auto& child : children_) {
        snapshot.children.push_back(child->build_element_snapshot_subtree());
    }
    for (const auto& entry : top_layer_manager_.entries()) {
        if (!entry.pending_removal && entry.element != nullptr) {
            snapshot.children.push_back(entry.element->build_element_snapshot_subtree());
        }
    }

    return snapshot;
}

RenderObjectSnapshot UIElement::render_object_snapshot() const {
    verify_thread_access();

    return ensure_render_state().render_object.snapshot(
        RenderObjectState{.layout_generation = layout_generation_,
                          .frame = committed_frame_,
                          .absolute_frame = committed_absolute_frame_,
                          .repaint_boundary = repaint_boundary(),
                          .has_layer = has_render_layer(),
                          .needs_paint = needs_paint_});
}

bool UIElement::check_thread_access() const noexcept {
    return owner_thread_id_ == std::this_thread::get_id();
}

void UIElement::verify_thread_access() const {
    if (!check_thread_access()) {
        throw std::logic_error("ui element accessed from a different thread");
    }
}

core::PropertyStore& UIElement::properties() noexcept {
    return property_store();
}

const core::PropertyStore& UIElement::properties() const noexcept {
    return property_state_ != nullptr ? property_state_->properties : empty_property_store();
}

UIElement& UIElement::clear_property(const core::PropertyMetadata& metadata) {
    verify_thread_access();
    const auto change = property_state_ != nullptr
                            ? property_state_->properties.clear_value(metadata)
                            : core::PropertyChange{.metadata = &metadata};
    apply_property_change(change);
    return *this;
}

bool UIElement::tick_animations(animation::AnimationTimePoint now) {
    verify_thread_access();
    auto& host = top_layer_host();
    ++host.animation_tick_depth_;
    try {
        const auto active =
            tick_animations_subtree(now, layout_generation_ == 0U
                                             ? std::optional<layout::Rect>{}
                                             : std::optional<layout::Rect>{committed_absolute_frame_});
        --host.animation_tick_depth_;
        if (host.animation_tick_depth_ == 0U) {
            host.flush_pending_top_layer_removals();
        }
        return active;
    } catch (...) {
        --host.animation_tick_depth_;
        if (host.animation_tick_depth_ == 0U) {
            host.flush_pending_top_layer_removals();
        }
        throw;
    }
}

bool UIElement::has_running_animations() const noexcept {
    return animation_state_ != nullptr && !animation_state_->implicit_property_animations.empty() &&
           animation_state_->implicit_property_animations.state() ==
               animation::AnimationPlayState::Running;
}

void UIElement::apply_property_change(const core::PropertyChange& change) {
    if (!change.changed) {
        return;
    }

    if (core::has_invalidation(change.invalidation, core::PropertyInvalidation::Semantics)) {
        ensure_semantics_state().dirty = true;
    }
    if (core::has_invalidation(change.invalidation, core::PropertyInvalidation::Style)) {
        detach_theme_management();
    }
    if (core::has_invalidation(change.invalidation, core::PropertyInvalidation::Layout)) {
        invalidate_layout();
        return;
    }
    if (core::has_invalidation(change.invalidation, core::PropertyInvalidation::Paint) ||
        core::has_invalidation(change.invalidation, core::PropertyInvalidation::Style)) {
        invalidate_paint();
    }
}

void UIElement::bind_layout_tree(layout::LayoutEngine& layout_engine) {
    if (parent_ != nullptr || event_router_ != nullptr || focus_manager_ != nullptr) {
        verify_thread_access();
    } else {
        adopt_thread_access(std::this_thread::get_id());
    }

    if (detached_layout_engine_ != nullptr && detached_layout_engine_.get() != &layout_engine) {
        detached_layout_engine_.reset();
    }
    layout_engine_ = &layout_engine;
    layout_->bind_to_engine(layout_engine);
    for (auto& child : children_) {
        child->bind_layout_tree(layout_engine);
    }
    for (auto& entry : top_layer_manager_.entries()) {
        entry.element->bind_layout_tree(layout_engine);
    }

    layout_->materialize_subtree();
}

void UIElement::calculate_layout(layout::LayoutConstraints constraints) {
    verify_thread_access();
    static_cast<void>(ensure_layout_engine());

    auto* paint_dirty_root = layout_dirty_root_;
    layout_dirty_root_ = nullptr;
    layout_->calculate_layout(std::move(constraints));
    const auto generation = layout_generation_ == std::numeric_limits<std::uint64_t>::max()
                                ? std::uint64_t{1}
                                : layout_generation_ + std::uint64_t{1};
    sync_layout_snapshot_subtree({}, generation);
    calculate_top_layer_layouts(constraints, generation);
    mark_layout_clean_subtree();
    if (paint_dirty_root != nullptr && contains(*paint_dirty_root)) {
        paint_dirty_root->mark_paint_dirty_subtree();
    } else {
        mark_paint_dirty_subtree();
    }
}

UIElement& UIElement::set_measure_callback(layout::MeasureCallback callback) {
    verify_thread_access();

    custom_measure_callback_ = true;
    if (text_state_ != nullptr) {
        text_state_->intrinsic_measure_callback = false;
    }
    layout_->set_measure_callback(std::move(callback));
    invalidate_layout();
    return *this;
}

UIElement& UIElement::clear_measure_callback() {
    verify_thread_access();

    custom_measure_callback_ = false;
    if (text_state_ != nullptr) {
        text_state_->intrinsic_measure_callback = false;
    }
    layout_->clear_measure_callback();
    update_intrinsic_text_measure_callback();
    invalidate_layout();
    return *this;
}

UIElement& UIElement::mark_measure_dirty() {
    verify_thread_access();

    layout_->mark_measure_dirty();
    invalidate_layout();
    return *this;
}

UIElement& UIElement::set_visible(bool visible) {
    verify_thread_access();

    if (visible_ == visible) {
        return *this;
    }

    visible_ = visible;
    if (parent_ != nullptr) {
        parent_->top_layer_manager_.invalidate_cache();
    }
    layout_->set_display(visible_ ? layout::Display::Flex : layout::Display::None);
    if (!visible_) {
        if (event_router_ != nullptr) {
            event_router_->on_element_detaching(*this);
        }
        if (focus_manager_ != nullptr) {
            focus_manager_->on_focus_scope_invalidated(*this);
        }
    }
    invalidate_layout();
    invalidate_paint();
    return *this;
}

bool UIElement::visible() const noexcept {
    return visible_;
}

UIElement& UIElement::set_hit_test_visible(bool hit_test_visible) {
    verify_thread_access();

    hit_test_visible_ = hit_test_visible;
    return *this;
}

bool UIElement::hit_test_visible() const noexcept {
    return hit_test_visible_;
}

bool UIElement::is_hovered() const noexcept {
    return hovered_;
}

UIElement& UIElement::set_disabled(bool disabled) {
    verify_thread_access();

    if (disabled_ == disabled) {
        return *this;
    }

    disabled_ = disabled;
    set_focusable(!disabled_);
    if (disabled_) {
        hovered_ = false;
    }
    release_pointer_capture();
    invalidate_paint();
    return *this;
}

bool UIElement::disabled() const noexcept {
    return disabled_;
}

UIElement& UIElement::set_opacity(float opacity) {
    verify_thread_access();

    const auto normalized_opacity = std::isfinite(opacity) ? std::clamp(opacity, 0.0F, 1.0F) : 1.0F;
    auto visual = style_value().visual;
    if (visual.opacity == normalized_opacity) {
        return *this;
    }

    detach_theme_management();
    record_pending_visual_dirty_bounds();
    visual.opacity = normalized_opacity;
    mutable_style_value().visual = visual;
    invalidate_visual();
    return *this;
}

float UIElement::opacity() const noexcept {
    return std::isfinite(style_value().visual.opacity)
               ? std::clamp(style_value().visual.opacity, 0.0F, 1.0F)
               : 1.0F;
}

UIElement& UIElement::set_render_transform(rendering::Transform2D transform) {
    verify_thread_access();

    if (!is_finite_transform(transform)) {
        transform = {};
    }

    auto visual = style_value().visual;
    if (visual.transform == transform) {
        return *this;
    }

    detach_theme_management();
    record_pending_visual_dirty_bounds();
    visual.transform = transform;
    mutable_style_value().visual = visual;
    invalidate_visual();
    return *this;
}

rendering::Transform2D UIElement::render_transform() const noexcept {
    return is_finite_transform(style_value().visual.transform) ? style_value().visual.transform
                                                               : rendering::Transform2D{};
}

UIElement& UIElement::set_layer_enabled(bool enabled) {
    verify_thread_access();

    auto visual = style_value().visual;
    if (visual.layer_enabled == enabled) {
        return *this;
    }

    detach_theme_management();
    record_pending_visual_dirty_bounds();
    visual.layer_enabled = enabled;
    mutable_style_value().visual = visual;
    invalidate_visual();
    return *this;
}

bool UIElement::layer_enabled() const noexcept {
    return style_value().visual.layer_enabled;
}

UIElement& UIElement::set_relayout_boundary(bool enabled) {
    verify_thread_access();

    if (relayout_boundary_ == enabled) {
        return *this;
    }

    relayout_boundary_ = enabled;
    invalidate_layout();
    return *this;
}

bool UIElement::relayout_boundary() const noexcept {
    return relayout_boundary_;
}

UIElement& UIElement::set_repaint_boundary(bool enabled) {
    verify_thread_access();

    if (repaint_boundary() == enabled) {
        return *this;
    }

    auto& render_state = ensure_render_state();
    render_state.repaint_boundary = enabled;
    render_state.render_object.invalidate_commands();
    invalidate_paint();
    return *this;
}

bool UIElement::repaint_boundary() const noexcept {
    return render_state_ != nullptr && render_state_->repaint_boundary;
}

UIElement& UIElement::set_style(style::UIElementStyle style) {
    return apply_style_value(std::move(style), false);
}

UIElement& UIElement::apply_style_value(style::UIElementStyle style, bool theme_managed) {
    verify_thread_access();

    auto& state = ensure_style_state();
    state.local_style = std::make_unique<style::UIElementStyle>(std::move(style));
    state.theme_managed = theme_managed;
    const auto& current_style = *state.local_style;
    if (current_style.text_selection_mode == style::TextSelectionMode::None) {
        reset_text_selection_state();
        if (event_router_ != nullptr && event_router_->text_selection_owner() == this) {
            event_router_->set_text_selection_owner(nullptr);
        }
        release_pointer_capture();
    }
    apply_layout_margin(*layout_, current_style.margin);
    layout_->set_box_sizing(current_style.box_sizing)
        .set_overflow(current_style.overflow)
        .set_min_width(layout::Length::points(current_style.min_width))
        .set_min_height(layout::Length::points(current_style.min_height));
    if (text_state_ != nullptr && !text_state_->custom_text_style) {
        auto& text_style = text_state_->text_style;
        text_style.font_size = current_style.font_size;
        text_style.color = current_style.text_color;
    }
    apply_visual_style_value(current_style.visual);
    mark_intrinsic_text_measure_dirty();
    if (!needs_layout_) {
        invalidate_layout();
    }
    invalidate_paint();
    return *this;
}

void UIElement::detach_theme_management() noexcept {
    auto& state = ensure_style_state();
    state.theme_managed = false;
    state.applied_theme = nullptr;
    mark_theme_dirty();
}

void UIElement::set_text_input_handler(std::unique_ptr<TextInputHandler> handler) noexcept {
    if (handler == nullptr) {
        if (text_state_ != nullptr) {
            text_state_->text_input_handler.reset();
        }
        return;
    }
    ensure_text_state().text_input_handler = std::move(handler);
}

void UIElement::apply_visual_style_value(const style::VisualStyle& visual_style) noexcept {
    auto& style = mutable_style_value();
    style.visual.opacity =
        std::isfinite(visual_style.opacity) ? std::clamp(visual_style.opacity, 0.0F, 1.0F) : 1.0F;
    style.visual.transform = is_finite_transform(visual_style.transform) ? visual_style.transform
                                                                         : rendering::Transform2D{};
    style.visual.layer_enabled = visual_style.layer_enabled;
}

UIElement& UIElement::set_theme_class(std::string_view theme_class) {
    verify_thread_access();

    auto& state = ensure_style_state();
    if (state.theme_class != theme_class) {
        state.theme_class = std::string(theme_class);
        state.applied_theme = nullptr;
        mark_theme_dirty();
    }

    if (!state.theme_managed) {
        mark_theme_dirty();
    }
    state.theme_managed = true;
    return *this;
}

UIElement& UIElement::clear_theme_class() noexcept {
    auto& state = ensure_style_state();
    state.theme_class.clear();
    state.applied_theme = nullptr;
    mark_theme_dirty();
    return *this;
}

std::string_view UIElement::theme_class() const noexcept {
    return style_state_ != nullptr ? std::string_view{style_state_->theme_class}
                                   : style::theme_class::panel;
}

UIElement& UIElement::set_local_theme(style::Theme theme) {
    verify_thread_access();

    theme.generation = next_local_theme_generation();
    auto& state = ensure_style_state();
    state.local_theme = std::move(theme);
    state.applied_theme = nullptr;
    mark_theme_dirty();
    return *this;
}

UIElement& UIElement::clear_local_theme() noexcept {
    if (style_state_ != nullptr) {
        style_state_->local_theme.reset();
        style_state_->applied_theme = nullptr;
        mark_theme_dirty();
    }
    return *this;
}

bool UIElement::has_local_theme() const noexcept {
    return style_state_ != nullptr && style_state_->local_theme.has_value();
}

const style::Theme* UIElement::local_theme() const noexcept {
    return style_state_ != nullptr && style_state_->local_theme
               ? std::addressof(*style_state_->local_theme)
               : nullptr;
}

UIElement& UIElement::set_theme_managed(bool managed) noexcept {
    ensure_style_state().theme_managed = managed;
    mark_theme_dirty();
    return *this;
}

bool UIElement::theme_managed() const noexcept {
    return style_state_ == nullptr || style_state_->theme_managed;
}

UIElement& UIElement::apply_theme(const style::Theme& theme) {
    verify_thread_access();

    auto& state = ensure_style_state();
    if (!theme_managed()) {
        state.applied_theme = std::addressof(theme);
        state.applied_theme_gen = theme.generation;
        theme_dirty_ = false;
        return *this;
    }

    const auto current_theme_class = theme_class();
    if (current_theme_class.empty()) {
        state.applied_theme = std::addressof(theme);
        state.applied_theme_gen = theme.generation;
        theme_dirty_ = false;
        return *this;
    }

    if (!theme_dirty_ && state.applied_theme == std::addressof(theme) &&
        state.applied_theme_gen == theme.generation) {
        theme_dirty_ = false;
        return *this;
    }

    const auto* class_style = style::theme_style_for_class(theme, current_theme_class);
    if (class_style != nullptr) {
        apply_style_value(*class_style, true);
    }
    auto& next_state = ensure_style_state();
    next_state.applied_theme = std::addressof(theme);
    next_state.applied_theme_gen = theme.generation;
    theme_dirty_ = false;
    return *this;
}

const style::UIElementStyle& UIElement::style() const noexcept {
    return style_value();
}

UIElement& UIElement::set_background(rendering::Color color) {
    verify_thread_access();

    if (style_value().background == color) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().background = color;
    invalidate_paint();
    return *this;
}

rendering::Color UIElement::background() const noexcept {
    return style_value().background;
}

UIElement& UIElement::set_border(rendering::Color color, float width) {
    verify_thread_access();

    if (!std::isfinite(width) || width < 0.0F) {
        throw std::invalid_argument("border width must be finite and non-negative");
    }

    if (style_value().border_color == color && style_value().border_width == width) {
        return *this;
    }

    detach_theme_management();
    auto& style = mutable_style_value();
    style.border_color = color;
    style.border_width = width;
    invalidate_paint();
    return *this;
}

rendering::Color UIElement::border_color() const noexcept {
    return style_value().border_color;
}

float UIElement::border_width() const noexcept {
    return style_value().border_width;
}

UIElement& UIElement::set_corner_radius(rendering::CornerRadius radius) {
    verify_thread_access();

    if (style_value().corner_radius == radius) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().corner_radius = radius;
    invalidate_paint();
    return *this;
}

rendering::CornerRadius UIElement::corner_radius() const noexcept {
    return style_value().corner_radius;
}

UIElement& UIElement::set_shadow(rendering::ShadowStyle shadow) {
    verify_thread_access();

    const auto& current_style = style_value();
    if (current_style.shadow_visible && current_style.shadow.color == shadow.color &&
        current_style.shadow.offset.x == shadow.offset.x &&
        current_style.shadow.offset.y == shadow.offset.y &&
        current_style.shadow.blur_radius == shadow.blur_radius &&
        current_style.shadow.spread == shadow.spread) {
        return *this;
    }

    detach_theme_management();
    auto& style = mutable_style_value();
    style.shadow = shadow;
    style.shadow_visible = true;
    invalidate_paint();
    return *this;
}

UIElement& UIElement::clear_shadow() {
    verify_thread_access();

    if (!style_value().shadow_visible) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().shadow_visible = false;
    invalidate_paint();
    return *this;
}

bool UIElement::shadow_visible() const noexcept {
    return style_value().shadow_visible;
}

rendering::ShadowStyle UIElement::shadow() const noexcept {
    return style_value().shadow;
}

UIElement& UIElement::set_padding(layout::EdgeInsets padding) {
    verify_thread_access();

    if (!std::isfinite(padding.left) || !std::isfinite(padding.top) ||
        !std::isfinite(padding.right) || !std::isfinite(padding.bottom) || padding.left < 0.0F ||
        padding.top < 0.0F || padding.right < 0.0F || padding.bottom < 0.0F) {
        throw std::invalid_argument("padding must be finite and non-negative");
    }

    const auto current_padding = style_value().padding;
    if (current_padding.left == padding.left && current_padding.top == padding.top &&
        current_padding.right == padding.right && current_padding.bottom == padding.bottom) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().padding = padding;
    mark_intrinsic_text_measure_dirty();
    invalidate_paint();
    return *this;
}

layout::EdgeInsets UIElement::padding() const noexcept {
    return style_value().padding;
}

UIElement& UIElement::set_margin(layout::EdgeInsets margin) {
    verify_thread_access();

    if (!std::isfinite(margin.left) || !std::isfinite(margin.top) || !std::isfinite(margin.right) ||
        !std::isfinite(margin.bottom)) {
        throw std::invalid_argument("margin must be finite");
    }

    const auto current_margin = style_value().margin;
    if (current_margin.left == margin.left && current_margin.top == margin.top &&
        current_margin.right == margin.right && current_margin.bottom == margin.bottom) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().margin = margin;
    apply_layout_margin(*layout_, margin);
    invalidate_layout();
    return *this;
}

layout::EdgeInsets UIElement::margin() const noexcept {
    return style_value().margin;
}

UIElement& UIElement::set_viewport(layout::Rect viewport) {
    verify_thread_access();

    if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
        !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
        viewport.width < 0.0F || viewport.height < 0.0F) {
        throw std::invalid_argument("viewport must be finite with non-negative size");
    }

    if (viewport_override().has_value() && viewport_override()->x == viewport.x &&
        viewport_override()->y == viewport.y && viewport_override()->width == viewport.width &&
        viewport_override()->height == viewport.height) {
        return *this;
    }

    mutable_viewport_override() = viewport;
    mutable_scroll_offset() = clamped_scroll_offset_for(scroll_offset_value(), committed_frame_);
    if (layout_generation_ != 0U) {
        refresh_snapshot_from_current_layout();
    }
    invalidate_render_commands();
    invalidate_paint();
    return *this;
}

UIElement& UIElement::clear_viewport() {
    verify_thread_access();

    if (!viewport_override().has_value()) {
        return *this;
    }

    mutable_viewport_override().reset();
    mutable_scroll_offset() = clamped_scroll_offset_for(scroll_offset_value(), committed_frame_);
    if (layout_generation_ != 0U) {
        refresh_snapshot_from_current_layout();
    }
    invalidate_render_commands();
    invalidate_paint();
    return *this;
}

bool UIElement::has_custom_viewport() const noexcept {
    return viewport_override().has_value();
}

layout::Rect UIElement::viewport_rect() const noexcept {
    return effective_viewport_rect_for(committed_frame_);
}

layout::Rect UIElement::absolute_viewport_rect() const noexcept {
    return effective_absolute_viewport_rect();
}

layout::Rect UIElement::scrollable_content_rect() const noexcept {
    return scrollable_content_rect_for(committed_frame_);
}

UIElement& UIElement::set_scroll_offset(layout::Point scroll_offset) {
    verify_thread_access();

    if (!std::isfinite(scroll_offset.x) || !std::isfinite(scroll_offset.y)) {
        throw std::invalid_argument("scroll offset must be finite");
    }

    const auto clamped_scroll = layout_generation_ == 0U
                                    ? scroll_offset
                                    : clamped_scroll_offset_for(scroll_offset, committed_frame_);
    const auto current_scroll = scroll_offset_value();
    if (current_scroll.x == clamped_scroll.x && current_scroll.y == clamped_scroll.y) {
        return *this;
    }

    mutable_scroll_offset() = clamped_scroll;
    if (layout_generation_ != 0U) {
        refresh_snapshot_from_current_layout();
    }
    invalidate_render_commands();
    invalidate_paint();
    return *this;
}

UIElement& UIElement::scroll_by(layout::Point delta) {
    verify_thread_access();

    if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) {
        throw std::invalid_argument("scroll delta must be finite");
    }

    const auto current_scroll = scroll_offset_value();
    return set_scroll_offset(layout::Point{current_scroll.x + delta.x, current_scroll.y + delta.y});
}

layout::Point UIElement::scroll_offset() const noexcept {
    return scroll_offset_value();
}

layout::Point UIElement::min_scroll_offset() const noexcept {
    return min_scroll_offset_for(committed_frame_);
}

layout::Point UIElement::max_scroll_offset() const noexcept {
    return max_scroll_offset_for(committed_frame_);
}

UIElement& UIElement::set_scroll_wheel_enabled(bool enabled) {
    verify_thread_access();
    set_scroll_wheel_enabled_value(enabled);
    return *this;
}

bool UIElement::scroll_wheel_enabled() const noexcept {
    return scroll_wheel_enabled_value();
}

UIElement& UIElement::set_overflow(layout::Overflow overflow) {
    verify_thread_access();

    if (style_value().overflow == overflow) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().overflow = overflow;
    layout_->set_overflow(overflow);
    invalidate_paint();
    return *this;
}

layout::Overflow UIElement::overflow() const noexcept {
    return style_value().overflow;
}

UIElement& UIElement::set_box_sizing(layout::BoxSizing box_sizing) {
    verify_thread_access();

    if (style_value().box_sizing == box_sizing) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().box_sizing = box_sizing;
    layout_->set_box_sizing(box_sizing);
    invalidate_layout();
    return *this;
}

layout::BoxSizing UIElement::box_sizing() const noexcept {
    return style_value().box_sizing;
}

UIElement& UIElement::set_min_width(float min_width) {
    verify_thread_access();

    if (!std::isfinite(min_width) || min_width < 0.0F) {
        throw std::invalid_argument("min width must be finite and non-negative");
    }

    if (style_value().min_width == min_width) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().min_width = min_width;
    layout_->set_min_width(layout::Length::points(min_width));
    mark_intrinsic_text_measure_dirty();
    invalidate_layout();
    return *this;
}

float UIElement::min_width() const noexcept {
    return style_value().min_width;
}

UIElement& UIElement::set_min_height(float min_height) {
    verify_thread_access();

    if (!std::isfinite(min_height) || min_height < 0.0F) {
        throw std::invalid_argument("min height must be finite and non-negative");
    }

    if (style_value().min_height == min_height) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().min_height = min_height;
    layout_->set_min_height(layout::Length::points(min_height));
    mark_intrinsic_text_measure_dirty();
    invalidate_layout();
    return *this;
}

float UIElement::min_height() const noexcept {
    return style_value().min_height;
}

UIElement& UIElement::set_z_index(int z_index) {
    verify_thread_access();

    if (style_value().z_index == z_index) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().z_index = z_index;
    mark_z_order_dirty();
    invalidate_paint();
    return *this;
}

int UIElement::z_index() const noexcept {
    return style_value().z_index;
}

UIElement& UIElement::set_text(std::string_view text) {
    verify_thread_access();

    if (text_storage() == text) {
        return *this;
    }

    auto& state = ensure_text_state();
    state.text = text;
    state.selection_anchor_byte_offset =
        rendering::clamp_utf8_boundary(state.text, state.selection_anchor_byte_offset);
    state.selection_active_byte_offset =
        rendering::clamp_utf8_boundary(state.text, state.selection_active_byte_offset);
    if (state.text.empty()) {
        state.selecting = false;
        if (event_router_ != nullptr && event_router_->text_selection_owner() == this) {
            event_router_->set_text_selection_owner(nullptr);
        }
        release_pointer_capture();
    } else if (event_router_ != nullptr) {
        if (has_text_selection()) {
            event_router_->set_text_selection_owner(this);
        } else if (event_router_->text_selection_owner() == this) {
            event_router_->set_text_selection_owner(nullptr);
        }
    }
    update_intrinsic_text_measure_callback();
    mark_intrinsic_text_measure_dirty();
    invalidate_paint();
    return *this;
}

const std::string& UIElement::text() const noexcept {
    return text_storage();
}

UIElement& UIElement::set_text_style(rendering::TextStyle style) {
    verify_thread_access();

    auto& state = ensure_text_state();
    if (state.text_style == style && state.custom_text_style) {
        return *this;
    }

    detach_theme_management();
    state.text_style = std::move(style);
    auto& element_style = mutable_style_value();
    element_style.font_size = state.text_style.font_size;
    element_style.text_color = state.text_style.color;
    state.custom_text_style = true;
    mark_intrinsic_text_measure_dirty();
    invalidate_paint();
    return *this;
}

const rendering::TextStyle& UIElement::text_style() const noexcept {
    return text_style_storage();
}

UIElement& UIElement::set_font_size(float font_size) {
    verify_thread_access();

    if (!std::isfinite(font_size) || font_size <= 0.0F) {
        throw std::invalid_argument("font size must be finite and positive");
    }

    if (style_value().font_size == font_size && text_style_storage().font_size == font_size) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().font_size = font_size;
    text_style_storage().font_size = font_size;
    mark_intrinsic_text_measure_dirty();
    invalidate_paint();
    return *this;
}

float UIElement::font_size() const noexcept {
    return style_value().font_size;
}

UIElement& UIElement::set_text_color(rendering::Color color) {
    verify_thread_access();

    if (style_value().text_color == color && text_style_storage().color == color) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().text_color = color;
    text_style_storage().color = color;
    invalidate_paint();
    return *this;
}

rendering::Color UIElement::text_color() const noexcept {
    return style_value().text_color;
}

UIElement& UIElement::set_text_selection_mode(style::TextSelectionMode mode) {
    verify_thread_access();

    if (style_value().text_selection_mode == mode) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().text_selection_mode = mode;
    if (mode == style::TextSelectionMode::None) {
        reset_text_selection_state();
        if (event_router_ != nullptr && event_router_->text_selection_owner() == this) {
            event_router_->set_text_selection_owner(nullptr);
        }
        release_pointer_capture();
    }
    invalidate_paint();
    return *this;
}

style::TextSelectionMode UIElement::text_selection_mode() const noexcept {
    return style_value().text_selection_mode;
}

UIElement& UIElement::set_text_selectable(bool selectable) {
    return set_text_selection_mode(selectable ? style::TextSelectionMode::Text
                                              : style::TextSelectionMode::None);
}

bool UIElement::text_selectable() const noexcept {
    return style_value().text_selection_mode == style::TextSelectionMode::Text;
}

UIElement& UIElement::set_text_selection_background(rendering::Color color) {
    verify_thread_access();

    if (style_value().text_selection_background == color) {
        return *this;
    }

    detach_theme_management();
    mutable_style_value().text_selection_background = color;
    invalidate_paint();
    return *this;
}

rendering::Color UIElement::text_selection_background() const noexcept {
    return style_value().text_selection_background;
}

UIElement& UIElement::set_text_selection(std::size_t anchor_byte_offset,
                                         std::size_t active_byte_offset) {
    verify_thread_access();

    auto& state = ensure_text_state();
    const auto anchor = rendering::clamp_utf8_boundary(state.text, anchor_byte_offset);
    const auto active = rendering::clamp_utf8_boundary(state.text, active_byte_offset);
    if (state.selection_anchor_byte_offset == anchor &&
        state.selection_active_byte_offset == active) {
        return *this;
    }

    state.selection_anchor_byte_offset = anchor;
    state.selection_active_byte_offset = active;
    if (event_router_ != nullptr) {
        if (has_text_selection()) {
            event_router_->set_text_selection_owner(this);
        } else if (event_router_->text_selection_owner() == this) {
            event_router_->set_text_selection_owner(nullptr);
        }
    }
    invalidate_paint();
    return *this;
}

UIElement& UIElement::clear_text_selection() {
    verify_thread_access();

    const auto* state = text_state();
    if (state == nullptr || (!has_text_selection() && !state->selecting)) {
        return *this;
    }

    reset_text_selection_state();
    if (event_router_ != nullptr && event_router_->text_selection_owner() == this) {
        event_router_->set_text_selection_owner(nullptr);
    }
    release_pointer_capture();
    invalidate_paint();
    return *this;
}

std::size_t UIElement::text_selection_anchor_byte_offset() const noexcept {
    return text_state_ != nullptr ? text_state_->selection_anchor_byte_offset : 0U;
}

std::size_t UIElement::text_selection_active_byte_offset() const noexcept {
    return text_state_ != nullptr ? text_state_->selection_active_byte_offset : 0U;
}

bool UIElement::has_text_selection() const noexcept {
    return text_selection_anchor_byte_offset() != text_selection_active_byte_offset();
}

std::string UIElement::selected_text() const {
    const auto [start, end] = ordered_byte_range(text_selection_anchor_byte_offset(),
                                                 text_selection_active_byte_offset());
    return start < end ? text_storage().substr(start, end - start) : std::string{};
}

UIElement* UIElement::hit_test(layout::Point absolute_point) {
    verify_thread_access();

    return hit_test_subtree(absolute_point);
}

const UIElement* UIElement::hit_test(layout::Point absolute_point) const {
    verify_thread_access();

    return hit_test_subtree(absolute_point);
}

UIElement& UIElement::set_focusable(bool focusable) {
    verify_thread_access();

    if (focusable_ == focusable) {
        return *this;
    }

    focusable_ = focusable;
    if (focus_manager_ != nullptr) {
        if (focusable_) {
            focus_manager_->on_focusable_registered(*this);
        } else {
            focus_manager_->on_focusable_unregistered(*this);
            focus_manager_->on_focus_scope_invalidated(*this);
        }
    }
    return *this;
}

bool UIElement::focusable() const noexcept {
    return focusable_;
}

bool UIElement::focused() const noexcept {
    return focused_;
}

bool UIElement::can_receive_focus() const noexcept {
    return visible_ && focusable_;
}

UIElement& UIElement::set_semantics_role(SemanticsRole role) {
    verify_thread_access();
    if (semantics_role() == role) {
        return *this;
    }
    auto& state = ensure_semantics_state();
    state.role = role;
    state.dirty = true;
    return *this;
}

UIElement& UIElement::set_semantics_label(std::string_view label) {
    verify_thread_access();
    if (semantics_label() == label) {
        return *this;
    }
    auto& state = ensure_semantics_state();
    state.label = std::string(label);
    state.dirty = true;
    return *this;
}

SemanticsRole UIElement::semantics_role() const noexcept {
    return semantics_state_ != nullptr ? semantics_state_->role : SemanticsRole::Generic;
}

std::string_view UIElement::semantics_label() const noexcept {
    return semantics_state_ != nullptr ? std::string_view{semantics_state_->label}
                                       : std::string_view{};
}

SemanticsNode UIElement::build_semantics_tree() const {
    verify_thread_access();
    return build_semantics_node_subtree();
}

SemanticsNode UIElement::build_semantics_node_subtree() const {
    const auto label = semantics_label();
    SemanticsNode node{.role = semantics_role(),
                       .label = label.empty() ? text_storage() : std::string(label),
                       .value = text_storage(),
                       .bounds = committed_absolute_frame_,
                       .state = SemanticsState{
                           .disabled = disabled_, .focusable = focusable_, .focused = focused_}};
    for (const auto& child : children_) {
        if (child->visible_) {
            node.children.push_back(child->build_semantics_node_subtree());
        }
    }
    for (const auto& entry : top_layer_manager_.entries()) {
        if (!entry.pending_removal && entry.element->visible_) {
            node.children.push_back(entry.element->build_semantics_node_subtree());
        }
    }
    return node;
}

void UIElement::invalidate_layout() {
    verify_thread_access();
    note_layout_dirty_root(*this);

    if (needs_layout_) {
        return;
    }

    needs_layout_ = true;
    self_needs_paint_ = true;
    needs_paint_ = true;

    if (parent_ != nullptr && relayout_boundary_) {
        parent_->mark_descendant_layout_dirty();
    } else if (parent_ != nullptr) {
        parent_->invalidate_layout();
    }
}

void UIElement::invalidate_paint() {
    verify_thread_access();

    self_needs_paint_ = true;
    needs_paint_ = true;

    if (parent_ != nullptr && !parent_->needs_paint_) {
        parent_->mark_descendant_paint_dirty();
    }
}

void UIElement::invalidate_visual() {
    verify_thread_access();

    needs_paint_ = true;

    if (parent_ != nullptr && !parent_->needs_paint_) {
        parent_->mark_descendant_paint_dirty();
    }
}

void UIElement::clear_paint_dirty() {
    verify_thread_access();

    self_needs_paint_ = false;
    needs_paint_ = false;
    pending_visual_dirty_bounds_.reset();
}

void UIElement::clear_paint_dirty_subtree() {
    verify_thread_access();
    clear_paint_dirty_subtree_unchecked();
}

bool UIElement::needs_layout() const noexcept {
    return needs_layout_;
}

bool UIElement::needs_paint() const noexcept {
    return needs_paint_;
}

std::optional<layout::Rect> UIElement::text_input_caret_rect() const {
    if (text_state_ != nullptr && text_state_->text_input_handler != nullptr) {
        return text_state_->text_input_handler->caret_rect();
    }
    return std::nullopt;
}

TextInputEditCommandState UIElement::text_input_edit_command_state() const {
    if (text_state_ != nullptr && text_state_->text_input_handler != nullptr) {
        return text_state_->text_input_handler->edit_command_state();
    }

    const auto can_copy = text_selectable() && !selected_text().empty();
    const auto [selection_start, selection_end] = ordered_byte_range(
        text_selection_anchor_byte_offset(), text_selection_active_byte_offset());
    const auto& current_text = text_storage();
    return TextInputEditCommandState{
        .can_copy = can_copy,
        .can_select_all = text_selectable() && !current_text.empty() &&
                          (selection_start != 0U || selection_end != current_text.size())};
}

bool UIElement::invoke_text_input_edit_command(TextInputEditCommand command) {
    if (text_state_ != nullptr && text_state_->text_input_handler != nullptr) {
        return text_state_->text_input_handler->invoke_edit_command(command);
    }

    if (command == TextInputEditCommand::Copy) {
        if (!text_selectable()) {
            return false;
        }

        const auto selection = selected_text();
        if (selection.empty()) {
            return false;
        }

        text_clipboard_service().copy_text(selection);
        return true;
    }

    const auto& current_text = text_storage();
    if (command == TextInputEditCommand::SelectAll && text_selectable() && !current_text.empty()) {
        set_text_selection(0U, current_text.size());
        return true;
    }

    return false;
}

bool UIElement::show_text_input_context_menu(layout::Point absolute_position) {
    if (text_state_ != nullptr && text_state_->text_input_handler != nullptr) {
        return text_state_->text_input_handler->show_context_menu(absolute_position);
    }

    static_cast<void>(absolute_position);
    return false;
}

void UIElement::dismiss_text_input_context_menu() noexcept {
    if (text_state_ != nullptr && text_state_->text_input_handler != nullptr) {
        text_state_->text_input_handler->dismiss_context_menu();
    }
}

bool UIElement::text_input_context_menu_open() const noexcept {
    if (text_state_ != nullptr && text_state_->text_input_handler != nullptr) {
        return text_state_->text_input_handler->context_menu_open();
    }

    return false;
}

bool UIElement::text_input_context_menu_hit_test(layout::Point absolute_position) const noexcept {
    if (text_state_ != nullptr && text_state_->text_input_handler != nullptr) {
        return text_state_->text_input_handler->context_menu_hit_test(absolute_position);
    }

    static_cast<void>(absolute_position);
    return false;
}

bool UIElement::handle_text_input_context_menu_pointer(PointerEvent& event) {
    if (text_state_ != nullptr && text_state_->text_input_handler != nullptr) {
        return text_state_->text_input_handler->handle_context_menu_pointer(event);
    }

    static_cast<void>(event);
    return false;
}

void UIElement::visit_paint_order(const VisitCallback& visitor) {
    verify_thread_access();

    visit_paint_order_subtree(visitor);
}

void UIElement::visit_paint_order(const ConstVisitCallback& visitor) const {
    verify_thread_access();

    visit_paint_order_subtree(visitor);
}

void UIElement::paint(rendering::RenderContext& context) const {
    verify_thread_access();

    paint_content_subtree(context);
    paint_overlay_subtree(context);
    paint_top_layer(context);
}

void UIElement::commit_render_commands(rendering::RenderCommandList& command_list,
                                       rendering::DirtyRegion* dirty_region) const {
    verify_thread_access();

    if (dirty_region != nullptr) {
        collect_paint_dirty_region_subtree(*dirty_region);
    }

    rendering::RenderCommandRecorder recorder(command_list.prepared_cache());
    append_content_commands_subtree(recorder);
    append_overlay_commands_subtree(recorder);
    append_top_layer_commands(recorder);
    command_list = recorder.take_command_list();
}

void UIElement::commit_render_scene(rendering::RenderScene& scene,
                                    rendering::DirtyRegion* dirty_region) const {
    verify_thread_access();

    if (dirty_region != nullptr) {
        collect_paint_dirty_region_subtree(*dirty_region);
    }

    auto root = rendering::RenderNode{.kind = rendering::RenderNodeKind::Picture};
#if !defined(NDEBUG) || defined(WINELEMENT_ENABLE_RENDER_DEBUG_NAMES)
    root.debug_name = "window.content";
#endif
    const auto prepared_cache = scene.prepared_cache();
    append_content_scene_subtree(root, nullptr, prepared_cache);
    append_overlay_scene_subtree(root, nullptr, prepared_cache);
    append_top_layer_scene_nodes(root, prepared_cache);
    refresh_scene_node_metadata(root);

    if (!scene_node_has_payload(root)) {
        scene.clear();
        return;
    }

    scene.set_root(std::move(root));
}

void UIElement::on_pointer_tunnel_event(PointerEvent& event) {
    static_cast<void>(event);
}

void UIElement::on_pointer_event(PointerEvent& event) {
    if (event.kind == PointerEventKind::Enter) {
        hovered_ = true;
        invalidate_paint();
        return;
    }

    if (event.kind == PointerEventKind::Leave) {
        hovered_ = false;
        invalidate_paint();
        return;
    }

    if (disabled_) {
        return;
    }

    auto* state = text_state_.get();
    if (state != nullptr && text_selectable() && !state->text.empty()) {
        if ((event.kind == PointerEventKind::Down || event.kind == PointerEventKind::DoubleClick) &&
            event.button == PointerButton::Primary) {
            const auto byte_offset = text_byte_offset_for_local_point(event.local_position);
            set_text_selection(byte_offset, byte_offset);
            if (event.kind == PointerEventKind::DoubleClick) {
                set_text_selection(0U, state->text.size());
                state->selecting = false;
            } else {
                state->selecting = true;
                static_cast<void>(capture_pointer());
            }
            event.handled = true;
            return;
        }

        if (event.kind == PointerEventKind::Move && state->selecting) {
            if (event.primary_button_down) {
                update_text_selection_for_local_point(event.local_position);
                event.handled = true;
                return;
            }

            state->selecting = false;
            release_pointer_capture();
        }

        if (event.kind == PointerEventKind::Up && state->selecting &&
            event.button == PointerButton::Primary) {
            update_text_selection_for_local_point(event.local_position);
            state->selecting = false;
            release_pointer_capture();
            event.handled = true;
            return;
        }

        if (event.kind == PointerEventKind::Cancel && state->selecting) {
            state->selecting = false;
            release_pointer_capture();
            event.handled = true;
            return;
        }
    }

    if (!scroll_wheel_enabled_value()) {
        return;
    }

    const auto previous_scroll = scroll_offset_value();
    switch (event.kind) {
    case PointerEventKind::Wheel:
        set_scroll_offset(
            layout::Point{previous_scroll.x,
                          previous_scroll.y - event.wheel_delta.y * default_scroll_wheel_step});
        break;
    case PointerEventKind::HorizontalWheel:
        set_scroll_offset(
            layout::Point{previous_scroll.x - event.wheel_delta.x * default_scroll_wheel_step,
                          previous_scroll.y});
        break;
    case PointerEventKind::Move:
    case PointerEventKind::Down:
    case PointerEventKind::Up:
    case PointerEventKind::Click:
    case PointerEventKind::DoubleClick:
    case PointerEventKind::Cancel:
    case PointerEventKind::Enter:
    case PointerEventKind::Leave:
        return;
    }

    const auto current_scroll = scroll_offset_value();
    event.handled = current_scroll.x != previous_scroll.x || current_scroll.y != previous_scroll.y;
}

void UIElement::on_key_event(KeyEvent& event) {
    if (event.kind == KeyEventKind::Down && event.modifiers.control && text_selectable()) {
        const auto& current_text = text_storage();
        switch (event.key) {
        case Key::A:
            if (!current_text.empty()) {
                set_text_selection(0U, current_text.size());
                event.handled = true;
            }
            return;
        case Key::C:
            event.handled = invoke_text_input_edit_command(TextInputEditCommand::Copy);
            return;
        case Key::Unknown:
        case Key::Tab:
        case Key::Enter:
        case Key::Space:
        case Key::Escape:
        case Key::Backspace:
        case Key::Delete:
        case Key::Up:
        case Key::Down:
        case Key::Left:
        case Key::Right:
        case Key::Home:
        case Key::End:
        case Key::V:
        case Key::X:
        case Key::Z:
            return;
        }
    }
}

void UIElement::on_focus_changed(const FocusChangeEvent& event) {
    static_cast<void>(event);
}

PointerCursor UIElement::cursor_for_local_point(layout::Point local_position) const noexcept {
    static_cast<void>(local_position);
    return PointerCursor::Default;
}

bool UIElement::on_animation_frame(animation::AnimationTimePoint now) {
    static_cast<void>(now);
    return false;
}

void UIElement::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    style::paint_rectangle(context, absolute_frame,
                           style::rectangle_style_from(style_value(), style_value().background,
                                                       style_value().border_color));

    const auto& current_text = text_storage();
    if (current_text.empty()) {
        return;
    }

    const auto text_rect = content_rect(absolute_frame);
    if (!std::isfinite(text_rect.width) || !std::isfinite(text_rect.height) ||
        text_rect.width <= 0.0F || text_rect.height <= 0.0F) {
        return;
    }

    const auto text_layout = text_layout_for_rect(text_rect);
    const auto& current_style = style_value();
    if (text_selectable() && has_text_selection() &&
        current_style.text_selection_background.alpha != 0U) {
        for (auto selection_rect : text_engine().selection_rects(
                 text_layout, rendering::TextSelectionRange{
                                  .start_byte_offset = text_selection_anchor_byte_offset(),
                                  .end_byte_offset = text_selection_active_byte_offset()})) {
            selection_rect.x += text_rect.x;
            selection_rect.y += text_rect.y;
            context.fill_rect(selection_rect, current_style.text_selection_background);
        }
    }
    context.draw_text_layout(text_layout, layout::Point{text_rect.x, text_rect.y});
}

void UIElement::on_paint_overlay(rendering::RenderContext& context,
                                 layout::Rect absolute_frame) const {
    static_cast<void>(context);
    static_cast<void>(absolute_frame);
}

bool UIElement::capture_pointer() {
    return event_router_ != nullptr && event_router_->capture_pointer(*this);
}

void UIElement::release_pointer_capture() noexcept {
    if (event_router_ != nullptr) {
        event_router_->release_pointer_capture(this);
    }
}

bool UIElement::has_pointer_capture() const noexcept {
    return event_router_ != nullptr && event_router_->pointer_capture() == this;
}

std::unique_ptr<layout::LayoutElement> UIElement::take_layout_ownership() {
    if (layout_owner_ == nullptr) {
        throw std::logic_error("ui element layout is already owned by its parent");
    }

    return std::move(layout_owner_);
}

void UIElement::restore_layout_ownership(std::unique_ptr<layout::LayoutElement> layout_element) {
    if (!layout_element) {
        throw std::invalid_argument("layout element must not be null");
    }

    if (layout_element.get() != layout_) {
        throw std::logic_error("restored layout element does not belong to this ui element");
    }

    layout_owner_ = std::move(layout_element);
}

void UIElement::mark_layout_clean_subtree() noexcept {
    needs_layout_ = false;
    for (auto& child : children_) {
        child->mark_layout_clean_subtree();
    }
    for (auto& entry : top_layer_manager_.entries()) {
        entry.element->mark_layout_clean_subtree();
    }
}

void UIElement::mark_paint_dirty_subtree() noexcept {
    self_needs_paint_ = true;
    needs_paint_ = true;
    for (auto& child : children_) {
        child->mark_paint_dirty_subtree();
    }
    for (auto& entry : top_layer_manager_.entries()) {
        entry.element->mark_paint_dirty_subtree();
    }
}

void UIElement::mark_descendant_paint_dirty() noexcept {
    if (needs_paint_) {
        return;
    }
    needs_paint_ = true;
    if (parent_ != nullptr) {
        parent_->mark_descendant_paint_dirty();
    }
}

void UIElement::mark_descendant_layout_dirty() noexcept {
    if (needs_layout_) {
        return;
    }
    needs_layout_ = true;
    needs_paint_ = true;
    if (parent_ != nullptr) {
        parent_->mark_descendant_layout_dirty();
    }
}

void UIElement::record_pending_visual_dirty_bounds() noexcept {
    if (layout_generation_ == 0U) {
        return;
    }

    const auto previous_bounds = visible_subtree_bounds();
    pending_visual_dirty_bounds_ =
        pending_visual_dirty_bounds_.has_value()
            ? layout::union_rects(*pending_visual_dirty_bounds_, previous_bounds)
            : previous_bounds;
}

void UIElement::offset_top_layer_entries_for_logical_owner_delta(
    layout::Point delta, std::uint64_t generation) noexcept {
    if ((delta.x == 0.0F && delta.y == 0.0F) || !std::isfinite(delta.x) ||
        !std::isfinite(delta.y)) {
        return;
    }

    auto& host = top_layer_host();
    if (host.top_layer_manager_.entries().empty()) {
        return;
    }

    layout::LayoutConstraints root_constraints;
    root_constraints.width = host.committed_absolute_frame_.width;
    root_constraints.height = host.committed_absolute_frame_.height;

    for (auto& entry : host.top_layer_manager_.entries()) {
        if (entry.pending_removal || entry.element == nullptr ||
            entry.options.logical_owner != this || entry.element.get() == this ||
            entry.options.bounds.width <= 0.0F || entry.options.bounds.height <= 0.0F ||
            !std::isfinite(entry.options.bounds.x) || !std::isfinite(entry.options.bounds.y)) {
            continue;
        }

        entry.options.bounds.x += delta.x;
        entry.options.bounds.y += delta.y;
        const auto bounds = effective_top_layer_bounds(
            entry.options.bounds, host.committed_absolute_frame_, root_constraints);
        entry.element->sync_top_layer_snapshot_subtree(bounds, generation);
        entry.element->invalidate_render_commands();
        entry.element->mark_paint_dirty_subtree();
        if (entry.element->parent_ != nullptr) {
            entry.element->parent_->mark_descendant_paint_dirty();
        }
    }
}

void UIElement::clear_paint_dirty_subtree_unchecked() noexcept {
    self_needs_paint_ = false;
    needs_paint_ = false;
    pending_visual_dirty_bounds_.reset();
    for (auto& child : children_) {
        child->clear_paint_dirty_subtree_unchecked();
    }
    for (auto& entry : top_layer_manager_.entries()) {
        entry.element->clear_paint_dirty_subtree_unchecked();
    }
}

void UIElement::collect_paint_dirty_region_subtree(rendering::DirtyRegion& dirty_region) const {
    if (!visible_) {
        return;
    }

    if (!needs_paint_ && top_layer_manager_.entries().empty()) {
        return;
    }

    if (needs_paint_ && has_visible_backdrop_top_layer(top_layer_manager_)) {
        dirty_region.add(committed_absolute_frame_);
        return;
    }

    const auto has_pending_visual_dirty = pending_visual_dirty_bounds_.has_value();
    if (has_pending_visual_dirty) {
        dirty_region.add(*pending_visual_dirty_bounds_);
    }

    if (self_needs_paint_) {
        dirty_region.add(visible_subtree_bounds());
        return;
    }

    if (needs_paint_ && (has_render_layer() || has_pending_visual_dirty)) {
        dirty_region.add(visible_subtree_bounds());
        return;
    }

    for (const auto& child : children_) {
        child->collect_paint_dirty_region_subtree(dirty_region);
    }
    for (const auto& entry : top_layer_manager_.entries()) {
        entry.element->collect_paint_dirty_region_subtree(dirty_region);
    }
}

void UIElement::refresh_snapshot_from_current_layout() noexcept {
    if (layout_generation_ == 0U) {
        return;
    }

    const auto generation = layout_generation_ == std::numeric_limits<std::uint64_t>::max()
                                ? std::uint64_t{1}
                                : layout_generation_ + std::uint64_t{1};

    if (parent_ != nullptr) {
        for (const auto& entry : parent_->top_layer_manager_.entries()) {
            if (entry.element.get() != this || entry.pending_removal) {
                continue;
            }

            layout::LayoutConstraints root_constraints;
            root_constraints.width = parent_->committed_absolute_frame_.width;
            root_constraints.height = parent_->committed_absolute_frame_.height;
            const auto bounds = effective_top_layer_bounds(
                entry.options.bounds, parent_->committed_absolute_frame_, root_constraints);
            sync_top_layer_snapshot_subtree(bounds, generation);
            return;
        }

        sync_layout_snapshot_subtree(parent_->child_content_absolute_origin(), generation);
        return;
    }

    sync_layout_snapshot_subtree({}, generation);
}

void UIElement::sync_layout_snapshot_subtree(layout::Point parent_content_origin,
                                             std::uint64_t generation) noexcept {
    const auto had_snapshot = layout_generation_ != 0U;
    const auto previous_absolute_frame = committed_absolute_frame_;
    committed_frame_ = layout_->frame();
    committed_absolute_frame_ = layout::offset_rect(committed_frame_, parent_content_origin);
    layout_generation_ = generation;
    if (had_snapshot) {
        offset_top_layer_entries_for_logical_owner_delta(
            layout::Point{committed_absolute_frame_.x - previous_absolute_frame.x,
                          committed_absolute_frame_.y - previous_absolute_frame.y},
            generation);
    }

    const auto sync_child_snapshots = [&]() noexcept {
        for (auto& child : children_) {
            child->sync_layout_snapshot_subtree(child_content_absolute_origin(), generation);
        }
    };

    sync_child_snapshots();
    if (style_state_ != nullptr) {
        const auto previous_scroll_offset = style_state_->scroll_offset;
        style_state_->scroll_offset =
            clamped_scroll_offset_for(style_state_->scroll_offset, committed_frame_);
        if (previous_scroll_offset.x != style_state_->scroll_offset.x ||
            previous_scroll_offset.y != style_state_->scroll_offset.y) {
            sync_child_snapshots();
        }
    }

    refresh_scrollable_extent();
}

void UIElement::refresh_scrollable_extent() noexcept {
    const auto viewport = effective_viewport_rect_for(committed_frame_);
    if (viewport.width <= 0.0F || viewport.height <= 0.0F) {
        has_scrollable_extent_ = false;
        return;
    }

    const auto content = scrollable_content_rect_for(committed_frame_);
    const auto min_scroll = layout::Point{std::min(0.0F, content.x), std::min(0.0F, content.y)};
    const auto max_scroll =
        layout::Point{std::max(min_scroll.x, content.x + content.width - viewport.width),
                      std::max(min_scroll.y, content.y + content.height - viewport.height)};

    has_scrollable_extent_ = min_scroll.x != 0.0F || min_scroll.y != 0.0F || max_scroll.x != 0.0F ||
                             max_scroll.y != 0.0F;
}

void UIElement::adopt_thread_access(std::thread::id owner_thread_id) noexcept {
    owner_thread_id_ = owner_thread_id;
    for (auto& child : children_) {
        child->adopt_thread_access(owner_thread_id);
    }
    for (auto& entry : top_layer_manager_.entries()) {
        entry.element->adopt_thread_access(owner_thread_id);
    }
}

void UIElement::visit_paint_order_subtree(const VisitCallback& visitor) {
    if (!visible_) {
        return;
    }

    const auto current_absolute_frame = committed_absolute_frame_;
    visitor(*this, current_absolute_frame);

    const auto& sorted = sorted_children();
    for (auto* child : sorted) {
        child->visit_paint_order_subtree(visitor);
    }
}

void UIElement::visit_paint_order_subtree(const ConstVisitCallback& visitor) const {
    if (!visible_) {
        return;
    }

    const auto current_absolute_frame = committed_absolute_frame_;
    visitor(*this, current_absolute_frame);

    const auto& sorted = sorted_children();
    for (const auto* child : sorted) {
        child->visit_paint_order_subtree(visitor);
    }
}

UIElement* UIElement::hit_test_subtree(layout::Point absolute_point) noexcept {
    if (!visible_) {
        return nullptr;
    }

    const auto mapped_point = map_point_to_untransformed_space(absolute_point);
    if (!mapped_point) {
        return nullptr;
    }

    const auto current_absolute_frame = committed_absolute_frame_;
    if (!layout::rect_contains_point(current_absolute_frame, *mapped_point)) {
        return nullptr;
    }

    const auto viewport_rect = effective_absolute_child_clip_rect();
    const auto clip_children = clips_children_to_viewport();
    if (clip_children && !layout::rect_contains_point(viewport_rect, *mapped_point)) {
        return hit_test_visible_ ? this : nullptr;
    }

    const auto& sorted = sorted_children();
    for (auto iterator = sorted.rbegin(); iterator != sorted.rend(); ++iterator) {
        auto* child = *iterator;
        if (clip_children) {
            const auto& child_frame = child->committed_absolute_frame_;
            if (child_frame.x >= viewport_rect.x + viewport_rect.width ||
                child_frame.x + child_frame.width <= viewport_rect.x ||
                child_frame.y >= viewport_rect.y + viewport_rect.height ||
                child_frame.y + child_frame.height <= viewport_rect.y) {
                continue;
            }
        }
        if (auto* hit_element = child->hit_test_subtree(*mapped_point)) {
            return hit_element;
        }
    }

    return hit_test_visible_ ? this : nullptr;
}

const UIElement* UIElement::hit_test_subtree(layout::Point absolute_point) const noexcept {
    if (!visible_) {
        return nullptr;
    }

    const auto mapped_point = map_point_to_untransformed_space(absolute_point);
    if (!mapped_point) {
        return nullptr;
    }

    const auto current_absolute_frame = committed_absolute_frame_;
    if (!layout::rect_contains_point(current_absolute_frame, *mapped_point)) {
        return nullptr;
    }

    const auto viewport_rect = effective_absolute_child_clip_rect();
    const auto clip_children = clips_children_to_viewport();
    if (clip_children && !layout::rect_contains_point(viewport_rect, *mapped_point)) {
        return hit_test_visible_ ? this : nullptr;
    }

    const auto& sorted = sorted_children();
    for (auto iterator = sorted.rbegin(); iterator != sorted.rend(); ++iterator) {
        const auto* child = *iterator;
        if (clip_children) {
            const auto& child_frame = child->committed_absolute_frame_;
            if (child_frame.x >= viewport_rect.x + viewport_rect.width ||
                child_frame.x + child_frame.width <= viewport_rect.x ||
                child_frame.y >= viewport_rect.y + viewport_rect.height ||
                child_frame.y + child_frame.height <= viewport_rect.y) {
                continue;
            }
        }
        const auto* hit_element = child->hit_test_subtree(*mapped_point);
        if (hit_element != nullptr) {
            return hit_element;
        }
    }

    return hit_test_visible_ ? this : nullptr;
}

std::optional<layout::Point>
UIElement::map_point_to_untransformed_space(layout::Point absolute_point) const noexcept {
    if (rendering::is_identity_transform(render_transform())) {
        return absolute_point;
    }

    return inverse_transform_point(absolute_point, render_layer_options().transform);
}

void UIElement::append_content_commands_subtree(rendering::RenderCommandRecorder& recorder) const {
    if (!visible_) {
        return;
    }

    auto& render_object = ensure_render_state().render_object;
    const auto cache_subtree_commands = should_cache_render_command_subtree();
    if (cache_subtree_commands &&
        render_object.can_reuse_content(layout_generation_, needs_paint_)) {
        recorder.append(render_object.content_commands());
        return;
    }

    rendering::RenderCommandRecorder subtree_recorder(recorder.command_list().prepared_cache());
    if (has_render_layer()) {
        subtree_recorder.push_layer(render_layer_options());
    }
    on_paint(subtree_recorder, committed_absolute_frame_);
    const auto clip_children = clips_children_to_viewport();
    const auto viewport_rect =
        clip_children ? effective_absolute_child_clip_rect() : layout::Rect{};
    if (clip_children) {
        subtree_recorder.push_clip(viewport_rect);
    }
    const auto& sorted = sorted_children();
    for (const auto& child : sorted) {
        if (clip_children &&
            !layout::rects_intersect(child->visible_subtree_bounds(), viewport_rect)) {
            continue;
        }
        child->append_content_commands_subtree(subtree_recorder);
    }
    if (clip_children) {
        subtree_recorder.pop_clip();
    }
    if (has_render_layer()) {
        subtree_recorder.pop_layer();
    }

    auto subtree_commands = subtree_recorder.take_command_list();
    if (cache_subtree_commands) {
        render_object.store_content(std::move(subtree_commands), layout_generation_);
        recorder.append(render_object.content_commands());
    } else {
        render_object.clear_content();
        recorder.append(std::move(subtree_commands));
    }
}

void UIElement::append_overlay_commands_subtree(rendering::RenderCommandRecorder& recorder) const {
    if (!visible_) {
        return;
    }

    auto& render_object = ensure_render_state().render_object;
    const auto cache_subtree_commands = should_cache_render_command_subtree();
    if (cache_subtree_commands &&
        render_object.can_reuse_overlay(layout_generation_, needs_paint_)) {
        recorder.append(render_object.overlay_commands());
        return;
    }

    rendering::RenderCommandRecorder subtree_recorder(recorder.command_list().prepared_cache());
    on_paint_overlay(subtree_recorder, committed_absolute_frame_);
    const auto clip_children = clips_children_to_viewport();
    const auto viewport_rect =
        clip_children ? effective_absolute_child_clip_rect() : layout::Rect{};
    if (clip_children) {
        subtree_recorder.push_clip(viewport_rect);
    }
    const auto& sorted = sorted_children();
    for (const auto& child : sorted) {
        if (clip_children &&
            !layout::rects_intersect(child->visible_subtree_bounds(), viewport_rect)) {
            continue;
        }
        child->append_overlay_commands_subtree(subtree_recorder);
    }
    if (clip_children) {
        subtree_recorder.pop_clip();
    }

    auto subtree_commands = subtree_recorder.take_command_list();
    if (cache_subtree_commands) {
        render_object.store_overlay(std::move(subtree_commands), layout_generation_);
        recorder.append(render_object.overlay_commands());
    } else {
        render_object.clear_overlay();
        recorder.append(std::move(subtree_commands));
    }
}

void UIElement::append_content_scene_subtree(
    rendering::RenderNode& parent, rendering::RenderCommandRecorder* parent_recorder,
    const std::shared_ptr<rendering::PreparedRenderCache>& prepared_cache,
    const std::optional<layout::Rect>& clip_rect) const {
    if (!visible_) {
        return;
    }
    if (clip_rect.has_value() && !layout::rects_intersect(visible_subtree_bounds(), *clip_rect)) {
        discard_cached_render_commands_subtree();
        return;
    }

    auto& render_object = ensure_render_state().render_object;
    const auto cache_subtree_commands = should_cache_render_command_subtree();
    if (cache_subtree_commands &&
        render_object.can_reuse_content(layout_generation_, needs_paint_)) {
        if (parent_recorder != nullptr) {
            parent_recorder->append(render_object.content_commands());
        }
        append_cached_scene_node(parent, render_object.content_commands());
        return;
    }

    const auto layer_options = render_layer_options();
    auto node =
        rendering::RenderNode{.kind = has_render_layer() ? rendering::RenderNodeKind::Layer
                                                         : rendering::RenderNodeKind::Picture,
                              .bounds = layer_options.bounds,
                              .transform = layer_options.transform,
                              .opacity = layer_options.opacity,
                              .clips_to_bounds = layer_options.clips_to_bounds,
                              .cache_policy = {.generation = layout_generation_}};

    rendering::RenderCommandRecorder subtree_recorder(prepared_cache);
    if (has_render_layer()) {
        subtree_recorder.push_layer(layer_options);
    }

    rendering::RenderCommandRecorder self_recorder(prepared_cache);
    on_paint(self_recorder, committed_absolute_frame_);
    auto self_commands = self_recorder.take_command_list();
    if (!self_commands.empty()) {
        subtree_recorder.append(self_commands);
        node.commands = std::move(self_commands);
    }

    const auto clip_children = clips_children_to_viewport();
    const auto viewport_rect =
        clip_children ? effective_absolute_child_clip_rect() : layout::Rect{};
    const auto child_clip_rect =
        clip_children ? intersect_clip_rect(clip_rect, viewport_rect) : clip_rect;
    rendering::RenderNode clip_node{.kind = rendering::RenderNodeKind::Clip,
                                    .bounds = viewport_rect,
                                    .cache_policy = {.generation = layout_generation_}};
    auto& child_parent = clip_children ? clip_node : node;
    if (clip_children) {
        subtree_recorder.push_clip(viewport_rect);
    }

    const auto& sorted = sorted_children();
    for (const auto& child : sorted) {
        if (child_clip_rect.has_value() &&
            !layout::rects_intersect(child->visible_subtree_bounds(), *child_clip_rect)) {
            child->discard_cached_render_commands_subtree();
            continue;
        }
        child->append_content_scene_subtree(child_parent, &subtree_recorder, prepared_cache,
                                            child_clip_rect);
    }

    if (clip_children) {
        subtree_recorder.pop_clip();
        refresh_scene_node_metadata(clip_node);
        if (scene_node_has_payload(clip_node)) {
            node.children.push_back(std::move(clip_node));
        }
    }

    if (has_render_layer()) {
        subtree_recorder.pop_layer();
    }

    auto subtree_commands = subtree_recorder.take_command_list();
    if (parent_recorder != nullptr) {
        if (cache_subtree_commands) {
            render_object.store_content(std::move(subtree_commands), layout_generation_);
            parent_recorder->append(render_object.content_commands());
        } else {
            render_object.clear_content();
            parent_recorder->append(std::move(subtree_commands));
        }
    } else if (cache_subtree_commands) {
        render_object.store_content(std::move(subtree_commands), layout_generation_);
    } else {
        render_object.clear_content();
    }

    refresh_scene_node_metadata(node);
    if (scene_node_has_payload(node)) {
        parent.children.push_back(std::move(node));
    }
}

void UIElement::append_overlay_scene_subtree(
    rendering::RenderNode& parent, rendering::RenderCommandRecorder* parent_recorder,
    const std::shared_ptr<rendering::PreparedRenderCache>& prepared_cache,
    const std::optional<layout::Rect>& clip_rect) const {
    if (!visible_) {
        return;
    }
    if (clip_rect.has_value() && !layout::rects_intersect(visible_subtree_bounds(), *clip_rect)) {
        discard_cached_render_commands_subtree();
        return;
    }

    auto& render_object = ensure_render_state().render_object;
    const auto cache_subtree_commands = should_cache_render_command_subtree();
    if (cache_subtree_commands &&
        render_object.can_reuse_overlay(layout_generation_, needs_paint_)) {
        if (parent_recorder != nullptr) {
            parent_recorder->append(render_object.overlay_commands());
        }
        append_cached_scene_node(parent, render_object.overlay_commands());
        return;
    }

    auto node = rendering::RenderNode{.kind = rendering::RenderNodeKind::Picture,
                                      .cache_policy = {.generation = layout_generation_}};
    rendering::RenderCommandRecorder subtree_recorder(prepared_cache);

    rendering::RenderCommandRecorder self_recorder(prepared_cache);
    on_paint_overlay(self_recorder, committed_absolute_frame_);
    auto self_commands = self_recorder.take_command_list();
    if (!self_commands.empty()) {
        subtree_recorder.append(self_commands);
        node.commands = std::move(self_commands);
    }

    const auto clip_children = clips_children_to_viewport();
    const auto viewport_rect =
        clip_children ? effective_absolute_child_clip_rect() : layout::Rect{};
    const auto child_clip_rect =
        clip_children ? intersect_clip_rect(clip_rect, viewport_rect) : clip_rect;
    rendering::RenderNode clip_node{.kind = rendering::RenderNodeKind::Clip,
                                    .bounds = viewport_rect,
                                    .cache_policy = {.generation = layout_generation_}};
    auto& child_parent = clip_children ? clip_node : node;
    if (clip_children) {
        subtree_recorder.push_clip(viewport_rect);
    }

    const auto& sorted = sorted_children();
    for (const auto& child : sorted) {
        if (child_clip_rect.has_value() &&
            !layout::rects_intersect(child->visible_subtree_bounds(), *child_clip_rect)) {
            child->discard_cached_render_commands_subtree();
            continue;
        }
        child->append_overlay_scene_subtree(child_parent, &subtree_recorder, prepared_cache,
                                            child_clip_rect);
    }

    if (clip_children) {
        subtree_recorder.pop_clip();
        refresh_scene_node_metadata(clip_node);
        if (scene_node_has_payload(clip_node)) {
            node.children.push_back(std::move(clip_node));
        }
    }

    auto subtree_commands = subtree_recorder.take_command_list();
    if (parent_recorder != nullptr) {
        if (cache_subtree_commands) {
            render_object.store_overlay(std::move(subtree_commands), layout_generation_);
            parent_recorder->append(render_object.overlay_commands());
        } else {
            render_object.clear_overlay();
            parent_recorder->append(std::move(subtree_commands));
        }
    } else if (cache_subtree_commands) {
        render_object.store_overlay(std::move(subtree_commands), layout_generation_);
    } else {
        render_object.clear_overlay();
    }

    refresh_scene_node_metadata(node);
    if (scene_node_has_payload(node)) {
        parent.children.push_back(std::move(node));
    }
}

bool UIElement::tick_animations_subtree(animation::AnimationTimePoint now) {
    return tick_animations_subtree(now, std::nullopt);
}

bool UIElement::tick_animations_subtree(animation::AnimationTimePoint now,
                                        const std::optional<layout::Rect>& clip_rect) {
    if (!visible_) {
        return false;
    }

    const auto subtree_visible =
        !clip_rect.has_value() || layout::rects_intersect(visible_subtree_bounds(), *clip_rect);
    auto active = false;
    if (subtree_visible) {
        active = on_animation_frame(now);
    }
    if (subtree_visible && animation_state_ != nullptr &&
        !animation_state_->implicit_property_animations.empty()) {
        active = animation_state_->implicit_property_animations.tick(now) || active;
    }

    auto child_clip_rect = clip_rect;
    if (subtree_visible && clips_children_to_viewport()) {
        child_clip_rect = intersect_clip_rect(child_clip_rect, effective_absolute_child_clip_rect());
    }

    if (subtree_visible && clip_rect_has_area(child_clip_rect)) {
        for (auto& child : children_) {
            active = child->tick_animations_subtree(now, child_clip_rect) || active;
        }
    }
    for (auto& entry : top_layer_manager_.entries()) {
        if (!entry.pending_removal && entry.element != nullptr) {
            active = entry.element->tick_animations_subtree(now, std::nullopt) || active;
        }
    }
    return active;
}

void UIElement::append_top_layer_commands(rendering::RenderCommandRecorder& recorder) const {
    for (const auto& entry : top_layer_manager_.entries()) {
        if (entry.pending_removal) {
            continue;
        }
        if (entry.options.backdrop_color.alpha != 0) {
            recorder.fill_rect(committed_absolute_frame_, entry.options.backdrop_color);
        }
        entry.element->append_content_commands_subtree(recorder);
        entry.element->append_overlay_commands_subtree(recorder);
        entry.element->append_top_layer_commands(recorder);
    }
}

void UIElement::append_top_layer_scene_nodes(
    rendering::RenderNode& parent,
    const std::shared_ptr<rendering::PreparedRenderCache>& prepared_cache) const {
    for (const auto& entry : top_layer_manager_.entries()) {
        if (entry.pending_removal) {
            continue;
        }
        if (entry.options.backdrop_color.alpha != 0) {
            rendering::RenderCommandRecorder backdrop_recorder(prepared_cache);
            backdrop_recorder.fill_rect(committed_absolute_frame_, entry.options.backdrop_color);
            auto node = rendering::RenderNode{.kind = rendering::RenderNodeKind::Picture,
                                              .commands = backdrop_recorder.take_command_list()};
            refresh_scene_node_metadata(node);
            if (scene_node_has_payload(node)) {
                parent.children.push_back(std::move(node));
            }
        }
        entry.element->append_content_scene_subtree(parent, nullptr, prepared_cache);
        entry.element->append_overlay_scene_subtree(parent, nullptr, prepared_cache);
        entry.element->append_top_layer_scene_nodes(parent, prepared_cache);
    }
}

void UIElement::discard_cached_render_commands_subtree() const noexcept {
    if (render_state_ != nullptr) {
        render_state_->render_object.discard_commands();
    }
    for (const auto& child : children_) {
        child->discard_cached_render_commands_subtree();
    }
    for (const auto& entry : top_layer_manager_.entries()) {
        if (!entry.pending_removal && entry.element != nullptr) {
            entry.element->discard_cached_render_commands_subtree();
        }
    }
}

bool UIElement::should_cache_render_command_subtree() const noexcept {
    return children_.empty() || has_render_layer() || repaint_boundary() ||
           !top_layer_manager_.entries().empty();
}

bool UIElement::has_render_layer() const noexcept {
    return layer_enabled() || repaint_boundary() || opacity() < 1.0F ||
           !rendering::is_identity_transform(render_transform());
}

rendering::RenderLayerOptions UIElement::render_layer_options() const noexcept {
    const auto origin = layout::Point{committed_absolute_frame_.x, committed_absolute_frame_.y};
    return rendering::RenderLayerOptions{
        .bounds = committed_absolute_frame_,
        .opacity = opacity(),
        .transform = rendering::transform_around_point(render_transform(), origin),
        .clips_to_bounds = false};
}

void UIElement::calculate_top_layer_layouts(layout::LayoutConstraints constraints,
                                            std::uint64_t generation) {
    for (auto& entry : top_layer_manager_.entries()) {
        if (entry.pending_removal) {
            continue;
        }
        const auto bounds = effective_top_layer_bounds(entry.options.bounds,
                                                       committed_absolute_frame_, constraints);
        layout::LayoutConstraints top_layer_constraints;
        top_layer_constraints.width = bounds.width;
        top_layer_constraints.height = bounds.height;
        top_layer_constraints.direction = constraints.direction;

        entry.element->layout_->calculate_layout(top_layer_constraints);
        entry.element->sync_top_layer_snapshot_subtree(bounds, generation);
    }
}

void UIElement::sync_top_layer_snapshot_subtree(layout::Rect containing_block,
                                                std::uint64_t generation) noexcept {
    const auto had_snapshot = layout_generation_ != 0U;
    const auto previous_absolute_frame = committed_absolute_frame_;
    committed_frame_ = layout_->frame();
    committed_absolute_frame_ = layout::offset_rect(committed_frame_, containing_block);
    layout_generation_ = generation;
    if (had_snapshot) {
        offset_top_layer_entries_for_logical_owner_delta(
            layout::Point{committed_absolute_frame_.x - previous_absolute_frame.x,
                          committed_absolute_frame_.y - previous_absolute_frame.y},
            generation);
    }

    const auto sync_child_snapshots = [&]() noexcept {
        for (auto& child : children_) {
            child->sync_layout_snapshot_subtree(child_content_absolute_origin(), generation);
        }
    };

    sync_child_snapshots();
    if (style_state_ != nullptr) {
        const auto previous_scroll_offset = style_state_->scroll_offset;
        style_state_->scroll_offset =
            clamped_scroll_offset_for(style_state_->scroll_offset, committed_frame_);
        if (previous_scroll_offset.x != style_state_->scroll_offset.x ||
            previous_scroll_offset.y != style_state_->scroll_offset.y) {
            sync_child_snapshots();
        }
    }
    for (auto& entry : top_layer_manager_.entries()) {
        if (entry.pending_removal) {
            continue;
        }
        layout::LayoutConstraints root_constraints;
        root_constraints.width = committed_absolute_frame_.width;
        root_constraints.height = committed_absolute_frame_.height;
        const auto bounds = effective_top_layer_bounds(entry.options.bounds,
                                                       committed_absolute_frame_, root_constraints);
        entry.element->sync_top_layer_snapshot_subtree(bounds, generation);
    }

    refresh_scrollable_extent();
}

void UIElement::update_intrinsic_text_measure_callback() {
    if (custom_measure_callback_) {
        return;
    }

    auto* state = text_state_.get();
    if (state == nullptr || state->text.empty() || !children_.empty()) {
        if (state != nullptr && state->intrinsic_measure_callback) {
            layout_->clear_measure_callback();
            state->intrinsic_measure_callback = false;
            invalidate_layout();
        }
        return;
    }

    if (state->intrinsic_measure_callback) {
        return;
    }

    layout_->set_measure_callback(
        [this](const layout::MeasureInput& input) { return measure_text_content(input); });
    layout_->mark_measure_dirty();
    state->intrinsic_measure_callback = true;
    invalidate_layout();
}

void UIElement::mark_intrinsic_text_measure_dirty() {
    if (text_state_ == nullptr || !text_state_->intrinsic_measure_callback) {
        return;
    }

    layout_->mark_measure_dirty();
    invalidate_layout();
}

rendering::TextStyle UIElement::effective_text_style() const {
    auto text_style = text_style_storage();
    if (text_state_ == nullptr || !text_state_->custom_text_style) {
        text_style.font_size = style_value().font_size;
        text_style.color = style_value().text_color;
    }
    return text_style;
}

layout::Rect UIElement::content_rect(layout::Rect rect) const noexcept {
    const auto padding = style_value().padding;
    rect.x += padding.left;
    rect.y += padding.top;
    rect.width = std::max(0.0F, rect.width - padding.left - padding.right);
    rect.height = std::max(0.0F, rect.height - padding.top - padding.bottom);
    return rect;
}

layout::Rect UIElement::effective_viewport_rect_for(layout::Rect element_frame) const noexcept {
    const auto local_bounds = layout::Rect{0.0F, 0.0F, std::max(element_frame.width, 0.0F),
                                           std::max(element_frame.height, 0.0F)};
    if (!viewport_override().has_value()) {
        return local_bounds;
    }

    return layout::intersect_rects(*viewport_override(), local_bounds);
}

layout::Rect UIElement::effective_child_clip_rect_for(layout::Rect element_frame) const noexcept {
    auto viewport = effective_viewport_rect_for(element_frame);
    const auto& style = style_value();
    if (style.border_width <= 0.0F || style.border_color.alpha == 0U || viewport.width <= 0.0F ||
        viewport.height <= 0.0F) {
        return viewport;
    }

    const auto border_inset =
        std::min(style.border_width, std::min(element_frame.width, element_frame.height) * 0.5F);
    const auto inner_bounds = layout::Rect{
        border_inset, border_inset, std::max(0.0F, element_frame.width - border_inset * 2.0F),
        std::max(0.0F, element_frame.height - border_inset * 2.0F)};
    return layout::intersect_rects(viewport, inner_bounds);
}

layout::Rect UIElement::effective_absolute_viewport_rect() const noexcept {
    return layout::offset_rect(
        effective_viewport_rect_for(committed_frame_),
        layout::Point{committed_absolute_frame_.x, committed_absolute_frame_.y});
}

layout::Rect UIElement::effective_absolute_child_clip_rect() const noexcept {
    return layout::offset_rect(
        effective_child_clip_rect_for(committed_frame_),
        layout::Point{committed_absolute_frame_.x, committed_absolute_frame_.y});
}

layout::Rect UIElement::scrollable_content_rect_for(layout::Rect /*element_frame*/) const noexcept {
    auto left = 0.0F;
    auto top = 0.0F;
    auto right = 0.0F;
    auto bottom = 0.0F;

    for (const auto& child : children_) {
        if (!child->visible_) {
            continue;
        }

        const auto child_frame = child->committed_frame_;
        left = std::min(left, child_frame.x);
        top = std::min(top, child_frame.y);
        right = std::max(right, child_frame.x + child_frame.width);
        bottom = std::max(bottom, child_frame.y + child_frame.height);
    }

    return layout::Rect{left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top)};
}

layout::Point UIElement::min_scroll_offset_for(layout::Rect element_frame) const noexcept {
    const auto viewport = effective_viewport_rect_for(element_frame);
    if (viewport.width <= 0.0F || viewport.height <= 0.0F) {
        return {};
    }

    const auto content = scrollable_content_rect_for(element_frame);
    return layout::Point{std::min(0.0F, content.x), std::min(0.0F, content.y)};
}

layout::Point UIElement::max_scroll_offset_for(layout::Rect element_frame) const noexcept {
    const auto viewport = effective_viewport_rect_for(element_frame);
    if (viewport.width <= 0.0F || viewport.height <= 0.0F) {
        return {};
    }

    const auto content = scrollable_content_rect_for(element_frame);
    const auto min_scroll = min_scroll_offset_for(element_frame);
    return layout::Point{std::max(min_scroll.x, content.x + content.width - viewport.width),
                         std::max(min_scroll.y, content.y + content.height - viewport.height)};
}

layout::Point UIElement::clamped_scroll_offset_for(layout::Point requested_scroll_offset,
                                                   layout::Rect element_frame) const noexcept {
    const auto min_scroll = min_scroll_offset_for(element_frame);
    const auto max_scroll = max_scroll_offset_for(element_frame);
    return layout::Point{std::clamp(requested_scroll_offset.x, min_scroll.x, max_scroll.x),
                         std::clamp(requested_scroll_offset.y, min_scroll.y, max_scroll.y)};
}

bool UIElement::clips_children_to_viewport() const noexcept {
    const auto current_scroll = scroll_offset_value();
    return style_value().overflow != layout::Overflow::Visible || viewport_override().has_value() ||
           current_scroll.x != 0.0F || current_scroll.y != 0.0F || has_scrollable_extent_;
}

layout::Rect UIElement::visible_subtree_bounds() const noexcept {
    auto& render_state = ensure_render_state();
    if (!needs_paint_ && render_state.visible_subtree_bounds_valid &&
        render_state.visible_subtree_bounds_generation == layout_generation_ &&
        render_state.visible_subtree_bounds_needs_paint == needs_paint_) {
        return render_state.visible_subtree_bounds;
    }

    auto bounds = layout::Rect{};
    if (visible_) {
        bounds = committed_absolute_frame_;
        if (shadow_visible()) {
            const auto shadow_style = shadow();
            bounds = layout::union_rects(
                bounds, layout::inflate_rect(
                            layout::offset_rect(committed_absolute_frame_, shadow_style.offset),
                            std::max(shadow_style.spread, 0.0F) +
                                std::max(shadow_style.blur_radius, 0.0F)));
        }
        for (const auto& child : children_) {
            bounds = layout::union_rects(bounds, child->visible_subtree_bounds());
        }
        for (const auto& entry : top_layer_manager_.entries()) {
            if (!entry.pending_removal && entry.element != nullptr) {
                bounds = layout::union_rects(bounds, entry.element->visible_subtree_bounds());
            }
        }
        if (has_render_layer()) {
            bounds = rendering::transform_rect(bounds, render_layer_options().transform);
        }
    }

    render_state.visible_subtree_bounds = bounds;
    render_state.visible_subtree_bounds_generation = layout_generation_;
    render_state.visible_subtree_bounds_needs_paint = needs_paint_;
    render_state.visible_subtree_bounds_valid = !needs_paint_;
    return bounds;
}

layout::Point UIElement::child_content_absolute_origin() const noexcept {
    const auto viewport = effective_absolute_viewport_rect();
    const auto current_scroll = scroll_offset_value();
    return layout::Point{viewport.x - current_scroll.x, viewport.y - current_scroll.y};
}

layout::Size UIElement::measure_text_content(const layout::MeasureInput& input) const {
    const auto& current_style = style_value();
    const auto horizontal_padding = current_style.padding.left + current_style.padding.right;
    const auto vertical_padding = current_style.padding.top + current_style.padding.bottom;
    const auto content_width = input.width_mode == layout::MeasureMode::Undefined
                                   ? 0.0F
                                   : std::max(0.0F, input.available_width - horizontal_padding);
    const auto content_height = input.height_mode == layout::MeasureMode::Undefined
                                    ? 0.0F
                                    : std::max(0.0F, input.available_height - vertical_padding);

    const auto text_style = effective_text_style();
    const auto text_size =
        text_style.wrapping == rendering::TextWrapping::NoWrap ||
                input.width_mode == layout::MeasureMode::Undefined
            ? text_engine().measure_single_line(text_storage(), text_style)
            : text_engine()
                  .layout_text(text_storage(), text_style,
                               rendering::TextLayoutOptions{.max_width = content_width,
                                                            .max_height = content_height})
                  .size;

    return layout::Size{std::max(text_size.width + horizontal_padding, current_style.min_width),
                        std::max(text_size.height + vertical_padding, current_style.min_height)};
}

rendering::TextLayout UIElement::text_layout_for_rect(layout::Rect text_rect) const {
    return text_engine().layout_text(
        text_storage(), effective_text_style(),
        rendering::TextLayoutOptions{.max_width = std::max(text_rect.width, 0.0F),
                                     .max_height = std::max(text_rect.height, 0.0F)});
}

std::size_t UIElement::text_byte_offset_for_local_point(layout::Point local_point) const {
    const auto local_bounds =
        layout::Rect{0.0F, 0.0F, committed_frame_.width, committed_frame_.height};
    const auto text_rect = content_rect(local_bounds);
    if (!std::isfinite(text_rect.width) || !std::isfinite(text_rect.height) ||
        text_rect.width <= 0.0F || text_rect.height <= 0.0F) {
        return 0U;
    }

    const auto text_layout = text_layout_for_rect(text_rect);
    const auto text_point = layout::Point{local_point.x - text_rect.x, local_point.y - text_rect.y};
    return text_engine().hit_test_byte_offset(text_layout, text_point);
}

void UIElement::update_text_selection_for_local_point(layout::Point local_point) {
    set_text_selection(text_selection_anchor_byte_offset(),
                       text_byte_offset_for_local_point(local_point));
}

void UIElement::paint_content_subtree(rendering::RenderContext& context) const {
    if (!visible_) {
        return;
    }

    if (has_render_layer()) {
        context.push_layer(render_layer_options());
    }

    const auto current_absolute_frame = committed_absolute_frame_;
    on_paint(context, current_absolute_frame);

    const auto clip_children = clips_children_to_viewport();
    if (clip_children) {
        context.push_clip(effective_absolute_child_clip_rect());
    }
    const auto& sorted = sorted_children();
    for (const auto* child : sorted) {
        child->paint_content_subtree(context);
    }
    if (clip_children) {
        context.pop_clip();
    }

    if (has_render_layer()) {
        context.pop_layer();
    }
}

void UIElement::paint_overlay_subtree(rendering::RenderContext& context) const {
    if (!visible_) {
        return;
    }

    const auto current_absolute_frame = committed_absolute_frame_;
    on_paint_overlay(context, current_absolute_frame);

    const auto clip_children = clips_children_to_viewport();
    if (clip_children) {
        context.push_clip(effective_absolute_child_clip_rect());
    }
    const auto& sorted = sorted_children();
    for (const auto* child : sorted) {
        child->paint_overlay_subtree(context);
    }
    if (clip_children) {
        context.pop_clip();
    }
}

void UIElement::paint_top_layer(rendering::RenderContext& context) const {
    for (const auto& entry : top_layer_manager_.entries()) {
        if (entry.pending_removal) {
            continue;
        }
        if (entry.options.backdrop_color.alpha != 0) {
            context.fill_rect(committed_absolute_frame_, entry.options.backdrop_color);
        }
        entry.element->paint_content_subtree(context);
        entry.element->paint_overlay_subtree(context);
        entry.element->paint_top_layer(context);
    }
}

void UIElement::validate_detached_from_managers() const {
    if (event_router_ != nullptr) {
        throw std::logic_error("ui child already belongs to an event router");
    }

    if (focus_manager_ != nullptr) {
        throw std::logic_error("ui child already belongs to a focus manager");
    }

    for (const auto& child : children_) {
        child->validate_detached_from_managers();
    }
    for (const auto& entry : top_layer_manager_.entries()) {
        entry.element->validate_detached_from_managers();
    }
}

void UIElement::attach_event_router(EventRouter& event_router) noexcept {
    root_ = &event_router.root();
    event_router_ = &event_router;
    for (auto& child : children_) {
        child->attach_event_router(event_router);
    }
    for (auto& entry : top_layer_manager_.entries()) {
        entry.element->attach_event_router(event_router);
    }
}

void UIElement::detach_event_router(EventRouter& event_router) noexcept {
    if (event_router_ == &event_router) {
        event_router_ = nullptr;
        root_ = nullptr;
    }

    for (auto& child : children_) {
        child->detach_event_router(event_router);
    }
    for (auto& entry : top_layer_manager_.entries()) {
        entry.element->detach_event_router(event_router);
    }
}

void UIElement::attach_focus_manager(FocusManager& focus_manager) noexcept {
    focus_manager_ = &focus_manager;
    if (focusable_) {
        focus_manager.on_focusable_registered(*this);
    }
    for (auto& child : children_) {
        child->attach_focus_manager(focus_manager);
    }
    for (auto& entry : top_layer_manager_.entries()) {
        entry.element->attach_focus_manager(focus_manager);
    }
}

void UIElement::detach_focus_manager(FocusManager& focus_manager) noexcept {
    if (focus_manager_ == &focus_manager) {
        if (focusable_) {
            focus_manager.on_focusable_unregistered(*this);
        }
        focus_manager_ = nullptr;
    }

    for (auto& child : children_) {
        child->detach_focus_manager(focus_manager);
    }
    for (auto& entry : top_layer_manager_.entries()) {
        entry.element->detach_focus_manager(focus_manager);
    }
}

void UIElement::mark_theme_dirty() noexcept {
    theme_dirty_ = true;
    for (auto* current = this; current != nullptr; current = current->parent_) {
        current->theme_subtree_dirty_ = true;
    }
    if (root_ != nullptr) {
        root_->theme_subtree_dirty_ = true;
    }
}

void UIElement::mark_theme_current(const style::Theme& theme) noexcept {
    auto& state = ensure_style_state();
    state.applied_theme = std::addressof(theme);
    state.applied_theme_gen = theme.generation;
    theme_dirty_ = false;
}

bool UIElement::theme_current_for(const style::Theme& theme) const noexcept {
    return style_state_ != nullptr && !theme_dirty_ &&
           style_state_->applied_theme == std::addressof(theme) &&
           style_state_->applied_theme_gen == theme.generation;
}

bool UIElement::theme_subtree_dirty() const noexcept {
    return theme_subtree_dirty_;
}

void UIElement::mark_theme_subtree_clean() noexcept {
    theme_dirty_ = false;
    theme_subtree_dirty_ = false;
}

UIElement* UIElement::logical_parent() noexcept {
    return const_cast<UIElement*>(std::as_const(*this).logical_parent());
}

const UIElement* UIElement::logical_parent() const noexcept {
    return logical_owner_ != nullptr ? logical_owner_ : parent_;
}

bool UIElement::contains_logical(const UIElement& element) const noexcept {
    for (const auto* current = &element; current != nullptr; current = current->logical_parent()) {
        if (current == this) {
            return true;
        }
    }

    return false;
}

void UIElement::clear_top_layer_logical_owner_references(const UIElement& subtree_root) noexcept {
    auto& host = top_layer_host();
    for (auto& entry : host.top_layer_manager_.entries()) {
        if (entry.element->logical_owner_ != nullptr &&
            subtree_root.contains(*entry.element->logical_owner_)) {
            entry.element->logical_owner_ = nullptr;
            host.refresh_top_layer_entry_logical_ancestors(entry);
        }
    }
}

void UIElement::mark_logical_descendant_top_layer_entries_pending_removal(
    const UIElement& logical_owner_root, std::uint64_t owner_entry_id) noexcept {
    auto& host = top_layer_host();
    auto descendant_indices =
        host.top_layer_manager_.logical_descendant_indices_of(logical_owner_root);
    auto& entries = host.top_layer_manager_.entries();
    for (const auto index : descendant_indices) {
        if (index >= entries.size()) {
            continue;
        }
        auto& entry = entries[index];
        if (entry.id == owner_entry_id || entry.pending_removal || !entry.element->visible_) {
            continue;
        }
        entry.pending_removal = true;
        entry.options.light_dismiss = false;
        entry.element->visible_ = false;
    }
}

void UIElement::remove_logical_descendant_top_layer_entries(const UIElement& logical_owner_root,
                                                            std::uint64_t owner_entry_id) {
    auto& host = top_layer_host();
    auto descendant_indices =
        host.top_layer_manager_.logical_descendant_indices_of(logical_owner_root);
    for (auto iterator = descendant_indices.rbegin(); iterator != descendant_indices.rend();
         ++iterator) {
        const auto index = *iterator;
        if (index >= host.top_layer_manager_.entries().size() ||
            host.top_layer_manager_.entries()[index].id == owner_entry_id) {
            continue;
        }
        static_cast<void>(host.remove_top_layer_entry(index));
    }
}

void UIElement::refresh_top_layer_entry_logical_ancestors(TopLayerEntry& entry) noexcept {
    entry.logical_ancestors.clear();
    if (entry.element == nullptr) {
        return;
    }

    entry.logical_ancestors.push_back(entry.element.get());
    for (const auto* current = entry.element->logical_owner_; current != nullptr;
         current = current->logical_parent()) {
        entry.logical_ancestors.push_back(current);
    }
}

bool UIElement::top_layer_entry_contains_logical(const TopLayerEntry& entry,
                                                 const UIElement& element) const noexcept {
    return std::find(entry.logical_ancestors.begin(), entry.logical_ancestors.end(), &element) !=
           entry.logical_ancestors.end();
}

bool UIElement::contains(const UIElement& element) const noexcept {
    for (const auto* current = &element; current != nullptr; current = current->parent_) {
        if (current == this) {
            return true;
        }
    }

    return false;
}

UIElement& UIElement::top_layer_host() noexcept {
    auto* current = root_ != nullptr ? root_ : this;
    while (current->parent_ != nullptr) {
        current = current->parent_;
    }
    return *current;
}

const UIElement& UIElement::top_layer_host() const noexcept {
    const auto* current = root_ != nullptr ? root_ : this;
    while (current->parent_ != nullptr) {
        current = current->parent_;
    }
    return *current;
}

void UIElement::clear_layout_callbacks_recursive_noexcept() noexcept {
    if (layout_ != nullptr) {
        layout_->clear_measure_callback();
        layout_->clear_baseline_callback();
        layout_->clear_dirtied_callback();
    }
    if (text_state_ != nullptr) {
        text_state_->intrinsic_measure_callback = false;
    }

    for (auto& child : children_) {
        if (child != nullptr) {
            child->clear_layout_callbacks_recursive_noexcept();
        }
    }

    for (auto& entry : top_layer_manager_.entries()) {
        if (entry.element != nullptr) {
            entry.element->clear_layout_callbacks_recursive_noexcept();
        }
    }
}

layout::LayoutEngine& UIElement::ensure_layout_engine() {
    if (layout_engine_ == nullptr) {
        detached_layout_engine_ = std::make_unique<layout::LayoutEngine>();
        bind_layout_tree(*detached_layout_engine_);
    }
    return *layout_engine_;
}

void UIElement::clear_layout_engine_binding() {
    layout_engine_ = nullptr;
    detached_layout_engine_.reset();
    if (event_router_ == nullptr) {
        root_ = nullptr;
    }
    layout_dirty_root_ = nullptr;
    layout_->bind_to_config(nullptr);
    for (auto& child : children_) {
        child->clear_layout_engine_binding();
    }
    for (auto& entry : top_layer_manager_.entries()) {
        entry.element->clear_layout_engine_binding();
    }
}

rendering::TextEngine& UIElement::text_engine() const {
    thread_local rendering::TextEngine engine;
    thread_local bool configured = false;
    if (!configured) {
        engine.set_max_cached_layouts(ui_text_layout_cache_entries);
        configured = true;
    }
    return engine;
}

void UIElement::note_layout_dirty_root(UIElement& dirty_root) noexcept {
    auto& host = top_layer_host();
    if (host.layout_dirty_root_ == nullptr || dirty_root.contains(*host.layout_dirty_root_)) {
        host.layout_dirty_root_ = &dirty_root;
        return;
    }

    if (host.layout_dirty_root_->contains(dirty_root)) {
        return;
    }

    auto* ancestor = dirty_root.parent_;
    while (ancestor != nullptr && !ancestor->contains(*host.layout_dirty_root_)) {
        ancestor = ancestor->parent_;
    }
    host.layout_dirty_root_ = ancestor != nullptr ? ancestor : &host;
}

UIElement* UIElement::top_layer_key_target() noexcept {
    for (auto iterator = top_layer_manager_.entries().rbegin();
         iterator != top_layer_manager_.entries().rend(); ++iterator) {
        if (iterator->pending_removal || !iterator->element->visible_) {
            continue;
        }
        return iterator->element.get();
    }
    return nullptr;
}

const UIElement* UIElement::top_layer_key_target() const noexcept {
    for (auto iterator = top_layer_manager_.entries().rbegin();
         iterator != top_layer_manager_.entries().rend(); ++iterator) {
        if (iterator->pending_removal || !iterator->element->visible_) {
            continue;
        }
        return iterator->element.get();
    }
    return nullptr;
}

UIElement* UIElement::top_layer_pointer_target(layout::Point absolute_point) noexcept {
    for (auto iterator = top_layer_manager_.entries().rbegin();
         iterator != top_layer_manager_.entries().rend(); ++iterator) {
        if (iterator->pending_removal || !iterator->element->visible_) {
            continue;
        }
        if (auto* hit = iterator->element->hit_test_subtree(absolute_point)) {
            return hit;
        }
        if (iterator->options.light_dismiss || iterator->options.modal ||
            iterator->options.backdrop_color.alpha != 0) {
            return iterator->element.get();
        }
    }
    return nullptr;
}

const UIElement* UIElement::top_layer_pointer_target(layout::Point absolute_point) const noexcept {
    for (auto iterator = top_layer_manager_.entries().rbegin();
         iterator != top_layer_manager_.entries().rend(); ++iterator) {
        if (iterator->pending_removal || !iterator->element->visible_) {
            continue;
        }
        if (const auto* hit = iterator->element->hit_test_subtree(absolute_point)) {
            return hit;
        }
        if (iterator->options.light_dismiss || iterator->options.modal ||
            iterator->options.backdrop_color.alpha != 0) {
            return iterator->element.get();
        }
    }
    return nullptr;
}

} // namespace winelement::elements
