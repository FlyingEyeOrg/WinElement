#pragma once

#include <winelement/animation/timeline.hpp>
#include <winelement/core/property.hpp>
#include <winelement/elements/element_descriptor.hpp>
#include <winelement/elements/input_event.hpp>
#include <winelement/elements/render_object.hpp>
#include <winelement/elements/semantics.hpp>
#include <winelement/elements/text_clipboard_service.hpp>
#include <winelement/elements/top_layer_manager.hpp>
#include <winelement/layout/layout_element.hpp>
#include <winelement/layout/layout_types.hpp>
#include <winelement/rendering/render_command_list.hpp>
#include <winelement/rendering/text_engine.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace winelement::rendering {
class DirtyRegion;
class RenderContext;
class RenderCommandList;
class RenderCommandRecorder;
class RenderScene;
struct RenderNode;
} // namespace winelement::rendering

namespace winelement::layout {
class LayoutEngine;
} // namespace winelement::layout

namespace winelement::platform::detail {
class UIElementThreadAccessScope;
} // namespace winelement::platform::detail

namespace winelement::elements {

class EventRouter;
class FocusManager;
class PopupManager;
class ThemeManager;
class UIElement;

enum class TextInputEditCommand { Cut, Copy, Paste, SelectAll };

struct TextInputEditCommandState {
    bool can_cut = false;
    bool can_copy = false;
    bool can_paste = false;
    bool can_select_all = false;
};

class TextInputHandler {
  public:
    virtual ~TextInputHandler();

    [[nodiscard]] virtual std::optional<layout::Rect> caret_rect() const = 0;
    [[nodiscard]] virtual TextInputEditCommandState edit_command_state() const = 0;
    virtual bool invoke_edit_command(TextInputEditCommand command) = 0;
    virtual bool show_context_menu(layout::Point absolute_position) = 0;
    virtual void dismiss_context_menu() noexcept = 0;
    [[nodiscard]] virtual bool context_menu_open() const noexcept = 0;
    [[nodiscard]] virtual bool
    context_menu_hit_test(layout::Point absolute_position) const noexcept = 0;
    virtual bool handle_context_menu_pointer(PointerEvent& event) = 0;
};

class UIElement {
  private:
    struct StyleState;
    struct TextState;
    struct PropertyState;
    struct AnimationState;
    struct SemanticsElementState;
    struct RenderState;

  public:
    using VisitCallback = std::function<void(UIElement& element, layout::Rect absolute_frame)>;
    using ConstVisitCallback =
        std::function<void(const UIElement& element, layout::Rect absolute_frame)>;

    UIElement();
    virtual ~UIElement() noexcept;

    UIElement(const UIElement&) = delete;
    UIElement& operator=(const UIElement&) = delete;
    UIElement(UIElement&&) = delete;
    UIElement& operator=(UIElement&&) = delete;

    [[nodiscard]] UIElement* parent() noexcept;
    [[nodiscard]] const UIElement* parent() const noexcept;
    [[nodiscard]] std::size_t child_count() const noexcept;
    [[nodiscard]] UIElement& child_at(std::size_t index);
    [[nodiscard]] const UIElement& child_at(std::size_t index) const;

    UIElement& append_child(std::unique_ptr<UIElement> child);
    UIElement& insert_child(std::size_t index, std::unique_ptr<UIElement> child);
    template <typename T, typename... Args>
    [[nodiscard]] std::unique_ptr<T> make_child(Args&&... args) {
        verify_thread_access();
        static_assert(std::is_base_of_v<UIElement, T>,
                      "UIElement::make_child requires a UIElement-derived type");
        static_assert(std::is_constructible_v<T, Args...>,
                      "UIElement child type is not constructible from the provided arguments");
        return std::make_unique<T>(std::forward<Args>(args)...);
    }
    template <typename T, typename... Args> T& append_new_child(Args&&... args) {
        auto child = make_child<T>(std::forward<Args>(args)...);
        auto& child_ref = *child;
        append_child(std::move(child));
        return child_ref;
    }
    [[nodiscard]] std::unique_ptr<UIElement> remove_child(UIElement& child);
    [[nodiscard]] std::unique_ptr<UIElement> remove_child_at(std::size_t index);
    void clear_children();

    UIElement& push_top_layer(std::unique_ptr<UIElement> element, TopLayerOptions options);
    [[nodiscard]] std::unique_ptr<UIElement> remove_top_layer(UIElement& element);
    void clear_top_layer();
    void dismiss_own_top_layer();
    UIElement& bring_top_layer_to_front(UIElement& element);
    UIElement& set_top_layer_bounds(UIElement& element, layout::Rect bounds);
    [[nodiscard]] std::size_t top_layer_count() const noexcept;
    [[nodiscard]] UIElement& top_layer_at(std::size_t index);
    [[nodiscard]] const UIElement& top_layer_at(std::size_t index) const;
    [[nodiscard]] layout::Rect top_layer_bounds(const UIElement& element) const;
    [[nodiscard]] UIElement* hit_test_top_layer(layout::Point absolute_point) noexcept;
    [[nodiscard]] const UIElement* hit_test_top_layer(layout::Point absolute_point) const noexcept;

    [[nodiscard]] const layout::LayoutElement& layout() const noexcept;
    [[nodiscard]] layout::Rect frame() const noexcept;
    [[nodiscard]] layout::Rect absolute_frame() const noexcept;
    [[nodiscard]] std::uint64_t layout_generation() const noexcept;
    UIElement& set_text_clipboard_service(std::shared_ptr<TextClipboardService> service);
    [[nodiscard]] TextClipboardService& text_clipboard_service() noexcept;
    [[nodiscard]] const TextClipboardService& text_clipboard_service() const noexcept;
    [[nodiscard]] ElementSnapshot element_snapshot() const;
    [[nodiscard]] RenderObjectSnapshot render_object_snapshot() const;
    [[nodiscard]] bool check_thread_access() const noexcept;
    void verify_thread_access() const;
    void bind_layout_tree(layout::LayoutEngine& layout_engine);

    [[nodiscard]] core::PropertyStore& properties() noexcept;
    [[nodiscard]] const core::PropertyStore& properties() const noexcept;
    template <typename T> UIElement& set_property(const core::PropertyMetadata& metadata, T value) {
        verify_thread_access();
        const auto change = property_store().set_value<T>(metadata, std::move(value));
        apply_property_change(change);
        return *this;
    }
    template <typename T>
    UIElement&
    animate_property(const core::PropertyMetadata& metadata, T target_value,
                     animation::AnimationTiming timing = animation::make_transition_timing(),
                     T default_value = T{}) {
        verify_thread_access();
        auto& properties = property_store();
        auto& animations = implicit_property_animations();
        const auto from = properties.value<T>(metadata, default_value);
        animations.animate<T>(
            from, std::move(target_value), timing, [this, metadata](const T& value) {
                const auto change = property_store().set_value<T>(metadata, value);
                apply_property_change(change);
            });
        animations.play(animation::AnimationClockType::now());
        return *this;
    }
    UIElement& clear_property(const core::PropertyMetadata& metadata);
    [[nodiscard]] bool
    tick_animations(animation::AnimationTimePoint now = animation::AnimationClockType::now());
    [[nodiscard]] bool has_running_animations() const noexcept;

    void calculate_layout(layout::LayoutConstraints constraints = {});

    template <typename Configure> UIElement& configure_layout(Configure&& configure) {
        verify_thread_access();
        std::forward<Configure>(configure)(mutable_layout());
        invalidate_layout();
        return *this;
    }

    UIElement& set_measure_callback(layout::MeasureCallback callback);
    UIElement& clear_measure_callback();
    UIElement& mark_measure_dirty();

    UIElement& set_visible(bool visible);
    [[nodiscard]] bool visible() const noexcept;

    UIElement& set_hit_test_visible(bool hit_test_visible);
    [[nodiscard]] bool hit_test_visible() const noexcept;
    [[nodiscard]] UIElement* hit_test(layout::Point absolute_point);
    [[nodiscard]] const UIElement* hit_test(layout::Point absolute_point) const;
    [[nodiscard]] bool is_hovered() const noexcept;

    UIElement& set_disabled(bool disabled);
    [[nodiscard]] bool disabled() const noexcept;

    UIElement& set_opacity(float opacity);
    [[nodiscard]] float opacity() const noexcept;
    UIElement& set_render_transform(rendering::Transform2D transform);
    [[nodiscard]] rendering::Transform2D render_transform() const noexcept;
    UIElement& set_layer_enabled(bool enabled);
    [[nodiscard]] bool layer_enabled() const noexcept;
    UIElement& set_relayout_boundary(bool enabled);
    [[nodiscard]] bool relayout_boundary() const noexcept;
    UIElement& set_repaint_boundary(bool enabled);
    [[nodiscard]] bool repaint_boundary() const noexcept;

    virtual UIElement& set_style(style::UIElementStyle style);
    template <typename Configure> UIElement& configure_style(Configure&& configure) {
        verify_thread_access();
        auto next_style = style_value();
        std::forward<Configure>(configure)(next_style);
        return apply_style_value(std::move(next_style), false);
    }
    UIElement& set_theme_class(std::string_view theme_class);
    UIElement& clear_theme_class() noexcept;
    [[nodiscard]] std::string_view theme_class() const noexcept;
    UIElement& set_local_theme(style::Theme theme);
    UIElement& clear_local_theme() noexcept;
    [[nodiscard]] bool has_local_theme() const noexcept;
    [[nodiscard]] const style::Theme* local_theme() const noexcept;
    UIElement& set_theme_managed(bool managed) noexcept;
    [[nodiscard]] bool theme_managed() const noexcept;
    virtual UIElement& apply_theme(const style::Theme& theme);
    [[nodiscard]] const style::UIElementStyle& style() const noexcept;
    UIElement& set_background(rendering::Color color);
    [[nodiscard]] rendering::Color background() const noexcept;
    UIElement& set_border(rendering::Color color, float width);
    [[nodiscard]] rendering::Color border_color() const noexcept;
    [[nodiscard]] float border_width() const noexcept;
    UIElement& set_corner_radius(rendering::CornerRadius radius);
    [[nodiscard]] rendering::CornerRadius corner_radius() const noexcept;
    UIElement& set_shadow(rendering::ShadowStyle shadow);
    UIElement& clear_shadow();
    [[nodiscard]] bool shadow_visible() const noexcept;
    [[nodiscard]] rendering::ShadowStyle shadow() const noexcept;
    UIElement& set_padding(layout::EdgeInsets padding);
    [[nodiscard]] layout::EdgeInsets padding() const noexcept;
    UIElement& set_margin(layout::EdgeInsets margin);
    [[nodiscard]] layout::EdgeInsets margin() const noexcept;
    UIElement& set_viewport(layout::Rect viewport);
    UIElement& clear_viewport();
    [[nodiscard]] bool has_custom_viewport() const noexcept;
    [[nodiscard]] layout::Rect viewport_rect() const noexcept;
    [[nodiscard]] layout::Rect absolute_viewport_rect() const noexcept;
    [[nodiscard]] layout::Rect scrollable_content_rect() const noexcept;
    UIElement& set_scroll_offset(layout::Point scroll_offset);
    UIElement& scroll_by(layout::Point delta);
    [[nodiscard]] layout::Point scroll_offset() const noexcept;
    [[nodiscard]] layout::Point min_scroll_offset() const noexcept;
    [[nodiscard]] layout::Point max_scroll_offset() const noexcept;
    UIElement& set_scroll_wheel_enabled(bool enabled);
    [[nodiscard]] bool scroll_wheel_enabled() const noexcept;

    UIElement& set_overflow(layout::Overflow overflow);
    [[nodiscard]] layout::Overflow overflow() const noexcept;
    UIElement& set_box_sizing(layout::BoxSizing box_sizing);
    [[nodiscard]] layout::BoxSizing box_sizing() const noexcept;
    UIElement& set_min_width(float min_width);
    [[nodiscard]] float min_width() const noexcept;
    UIElement& set_min_height(float min_height);
    [[nodiscard]] float min_height() const noexcept;
    UIElement& set_z_index(int z_index);
    [[nodiscard]] int z_index() const noexcept;
    void mark_z_order_dirty() noexcept;
    void ensure_sorted_children() const;
    [[nodiscard]] const std::vector<UIElement*>& sorted_children() const noexcept;

    UIElement& set_text(std::string_view text);
    [[nodiscard]] const std::string& text() const noexcept;
    UIElement& set_text_style(rendering::TextStyle style);
    [[nodiscard]] const rendering::TextStyle& text_style() const noexcept;
    UIElement& set_font_size(float font_size);
    [[nodiscard]] float font_size() const noexcept;
    UIElement& set_text_color(rendering::Color color);
    [[nodiscard]] rendering::Color text_color() const noexcept;
    UIElement& set_text_selection_mode(style::TextSelectionMode mode);
    [[nodiscard]] style::TextSelectionMode text_selection_mode() const noexcept;
    UIElement& set_text_selectable(bool selectable);
    [[nodiscard]] bool text_selectable() const noexcept;
    UIElement& set_text_selection_background(rendering::Color color);
    [[nodiscard]] rendering::Color text_selection_background() const noexcept;
    UIElement& set_text_selection(std::size_t anchor_byte_offset, std::size_t active_byte_offset);
    UIElement& clear_text_selection();
    [[nodiscard]] std::size_t text_selection_anchor_byte_offset() const noexcept;
    [[nodiscard]] std::size_t text_selection_active_byte_offset() const noexcept;
    [[nodiscard]] bool has_text_selection() const noexcept;
    [[nodiscard]] std::string selected_text() const;

    UIElement& set_focusable(bool focusable);
    [[nodiscard]] bool focusable() const noexcept;
    [[nodiscard]] bool focused() const noexcept;
    [[nodiscard]] bool can_receive_focus() const noexcept;
    UIElement& set_semantics_role(SemanticsRole role);
    UIElement& set_semantics_label(std::string_view label);
    [[nodiscard]] SemanticsRole semantics_role() const noexcept;
    [[nodiscard]] std::string_view semantics_label() const noexcept;
    [[nodiscard]] SemanticsNode build_semantics_tree() const;

    void invalidate_layout();
    void invalidate_paint();
    void clear_paint_dirty();
    void clear_paint_dirty_subtree();
    [[nodiscard]] bool needs_layout() const noexcept;
    [[nodiscard]] bool needs_paint() const noexcept;
    [[nodiscard]] std::optional<layout::Rect> text_input_caret_rect() const;
    [[nodiscard]] TextInputEditCommandState text_input_edit_command_state() const;
    bool invoke_text_input_edit_command(TextInputEditCommand command);
    bool show_text_input_context_menu(layout::Point absolute_position);
    void dismiss_text_input_context_menu() noexcept;
    [[nodiscard]] bool text_input_context_menu_open() const noexcept;
    [[nodiscard]] bool
    text_input_context_menu_hit_test(layout::Point absolute_position) const noexcept;
    bool handle_text_input_context_menu_pointer(PointerEvent& event);

    void visit_paint_order(const VisitCallback& visitor);
    void visit_paint_order(const ConstVisitCallback& visitor) const;
    void paint(rendering::RenderContext& context) const;
    void commit_render_commands(rendering::RenderCommandList& command_list,
                                rendering::DirtyRegion* dirty_region = nullptr) const;
    void commit_render_scene(rendering::RenderScene& scene,
                             rendering::DirtyRegion* dirty_region = nullptr) const;

  protected:
    virtual void on_pointer_tunnel_event(PointerEvent& event);
    virtual void on_pointer_event(PointerEvent& event);
    virtual void on_key_event(KeyEvent& event);
    virtual void on_focus_changed(const FocusChangeEvent& event);
    [[nodiscard]] virtual PointerCursor
    cursor_for_local_point(layout::Point local_position) const noexcept;
    [[nodiscard]] virtual bool on_animation_frame(animation::AnimationTimePoint now);
    virtual void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const;
    virtual void on_paint_overlay(rendering::RenderContext& context,
                                  layout::Rect absolute_frame) const;
    UIElement& apply_style_value(style::UIElementStyle style, bool theme_managed);
    void detach_theme_management() noexcept;
    void set_text_input_handler(std::unique_ptr<TextInputHandler> handler) noexcept;
    void apply_visual_style_value(const style::VisualStyle& visual_style) noexcept;
    bool capture_pointer();
    void release_pointer_capture() noexcept;
    [[nodiscard]] bool has_pointer_capture() const noexcept;
    [[nodiscard]] style::UIElementStyle& style_storage();
    [[nodiscard]] const style::UIElementStyle& style_storage() const noexcept;
    [[nodiscard]] std::string& text_storage() noexcept;
    [[nodiscard]] const std::string& text_storage() const noexcept;
    [[nodiscard]] rendering::TextStyle& text_style_storage() noexcept;
    [[nodiscard]] const rendering::TextStyle& text_style_storage() const noexcept;

  private:
    friend class EventRouter;
    friend class FocusManager;
    friend class platform::detail::UIElementThreadAccessScope;
    friend class PopupManager;
    friend class ThemeManager;
    friend class TopLayerManager;

    [[nodiscard]] std::unique_ptr<layout::LayoutElement> take_layout_ownership();
    void restore_layout_ownership(std::unique_ptr<layout::LayoutElement> layout_element);
    UIElement& push_top_layer_entry(std::unique_ptr<UIElement> element, TopLayerOptions options,
                                    std::uint64_t* entry_id);
    [[nodiscard]] std::unique_ptr<UIElement> remove_top_layer_entry(std::size_t index);
    [[nodiscard]] bool remove_top_layer_entry_by_id(std::uint64_t entry_id);
    [[nodiscard]] bool mark_top_layer_entry_pending_removal(std::uint64_t entry_id);
    [[nodiscard]] UIElement* top_layer_entry_element(std::uint64_t entry_id) noexcept;
    [[nodiscard]] const UIElement* top_layer_entry_element(std::uint64_t entry_id) const noexcept;
    [[nodiscard]] bool set_top_layer_entry_bounds(std::uint64_t entry_id, layout::Rect bounds);
    [[nodiscard]] bool bring_top_layer_entry_to_front(std::uint64_t entry_id);
    [[nodiscard]] bool top_layer_entry_preserves_focus_for(const UIElement& element) const noexcept;
    void dismiss_topmost_on_escape();
    bool light_dismiss_outside(layout::Point position);
    void clear_focus_outside_topmost_modal() noexcept;
    void apply_property_change(const core::PropertyChange& change);
    void sanitize_pending_top_layer_result(RoutedEventResult& result) const noexcept;
    void flush_pending_top_layer_removals();
    void mark_layout_clean_subtree() noexcept;
    void mark_paint_dirty_subtree() noexcept;
    void mark_descendant_paint_dirty() noexcept;
    void mark_descendant_layout_dirty() noexcept;
    void offset_top_layer_entries_for_logical_owner_delta(layout::Point delta,
                                                          std::uint64_t generation) noexcept;
    void clear_paint_dirty_subtree_unchecked() noexcept;
    void collect_paint_dirty_region_subtree(rendering::DirtyRegion& dirty_region) const;
    void sync_layout_snapshot_subtree(layout::Point parent_content_origin,
                                      std::uint64_t generation) noexcept;
    void refresh_snapshot_from_current_layout() noexcept;
    void adopt_thread_access(std::thread::id owner_thread_id) noexcept;
    void visit_paint_order_subtree(const VisitCallback& visitor);
    void visit_paint_order_subtree(const ConstVisitCallback& visitor) const;
    [[nodiscard]] UIElement* hit_test_subtree(layout::Point absolute_point) noexcept;
    [[nodiscard]] const UIElement* hit_test_subtree(layout::Point absolute_point) const noexcept;
    [[nodiscard]] std::optional<layout::Point>
    map_point_to_untransformed_space(layout::Point absolute_point) const noexcept;
    void append_content_commands_subtree(rendering::RenderCommandRecorder& recorder) const;
    void append_overlay_commands_subtree(rendering::RenderCommandRecorder& recorder) const;
    void append_top_layer_commands(rendering::RenderCommandRecorder& recorder) const;
    void append_content_scene_subtree(
        rendering::RenderNode& parent, rendering::RenderCommandRecorder* parent_recorder,
        const std::shared_ptr<rendering::PreparedRenderCache>& prepared_cache,
        const std::optional<layout::Rect>& clip_rect = std::nullopt) const;
    void append_overlay_scene_subtree(
        rendering::RenderNode& parent, rendering::RenderCommandRecorder* parent_recorder,
        const std::shared_ptr<rendering::PreparedRenderCache>& prepared_cache,
        const std::optional<layout::Rect>& clip_rect = std::nullopt) const;
    void append_top_layer_scene_nodes(
        rendering::RenderNode& parent,
        const std::shared_ptr<rendering::PreparedRenderCache>& prepared_cache) const;
    [[nodiscard]] bool has_render_layer() const noexcept;
    [[nodiscard]] rendering::RenderLayerOptions render_layer_options() const noexcept;
    void calculate_top_layer_layouts(layout::LayoutConstraints constraints,
                                     std::uint64_t generation);
    void sync_top_layer_snapshot_subtree(layout::Rect absolute_frame,
                                         std::uint64_t generation) noexcept;
    void refresh_scrollable_extent() noexcept;
    void paint_content_subtree(rendering::RenderContext& context) const;
    void paint_overlay_subtree(rendering::RenderContext& context) const;
    void paint_top_layer(rendering::RenderContext& context) const;
    void update_intrinsic_text_measure_callback();
    void mark_intrinsic_text_measure_dirty();
    [[nodiscard]] rendering::TextStyle effective_text_style() const;
    [[nodiscard]] layout::Rect content_rect(layout::Rect rect) const noexcept;
    [[nodiscard]] layout::Rect
    effective_viewport_rect_for(layout::Rect element_frame) const noexcept;
    [[nodiscard]] layout::Rect
    effective_child_clip_rect_for(layout::Rect element_frame) const noexcept;
    [[nodiscard]] layout::Rect effective_absolute_viewport_rect() const noexcept;
    [[nodiscard]] layout::Rect effective_absolute_child_clip_rect() const noexcept;
    [[nodiscard]] layout::Rect
    scrollable_content_rect_for(layout::Rect element_frame) const noexcept;
    [[nodiscard]] layout::Point min_scroll_offset_for(layout::Rect element_frame) const noexcept;
    [[nodiscard]] layout::Point max_scroll_offset_for(layout::Rect element_frame) const noexcept;
    [[nodiscard]] layout::Point
    clamped_scroll_offset_for(layout::Point requested_scroll_offset,
                              layout::Rect element_frame) const noexcept;
    [[nodiscard]] bool clips_children_to_viewport() const noexcept;
    [[nodiscard]] layout::Rect visible_subtree_bounds() const noexcept;
    [[nodiscard]] layout::Point child_content_absolute_origin() const noexcept;
    [[nodiscard]] layout::Size measure_text_content(const layout::MeasureInput& input) const;
    [[nodiscard]] rendering::TextLayout text_layout_for_rect(layout::Rect text_rect) const;
    [[nodiscard]] std::size_t text_byte_offset_for_local_point(layout::Point local_point) const;
    void update_text_selection_for_local_point(layout::Point local_point);
    void validate_detached_from_managers() const;
    void attach_event_router(EventRouter& event_router) noexcept;
    void detach_event_router(EventRouter& event_router) noexcept;
    void attach_focus_manager(FocusManager& focus_manager) noexcept;
    void detach_focus_manager(FocusManager& focus_manager) noexcept;
    void mark_theme_dirty() noexcept;
    void mark_theme_current(const style::Theme& theme) noexcept;
    [[nodiscard]] bool theme_current_for(const style::Theme& theme) const noexcept;
    [[nodiscard]] bool theme_subtree_dirty() const noexcept;
    void mark_theme_subtree_clean() noexcept;
    [[nodiscard]] UIElement* logical_parent() noexcept;
    [[nodiscard]] const UIElement* logical_parent() const noexcept;
    [[nodiscard]] bool contains_logical(const UIElement& element) const noexcept;
    void clear_top_layer_logical_owner_references(const UIElement& subtree_root) noexcept;
    void mark_logical_descendant_top_layer_entries_pending_removal(
        const UIElement& logical_owner_root, std::uint64_t owner_entry_id) noexcept;
    void remove_logical_descendant_top_layer_entries(const UIElement& logical_owner_root,
                                                     std::uint64_t owner_entry_id);
    void refresh_top_layer_entry_logical_ancestors(TopLayerEntry& entry) noexcept;
    [[nodiscard]] bool top_layer_entry_contains_logical(const TopLayerEntry& entry,
                                                        const UIElement& element) const noexcept;
    [[nodiscard]] bool tick_animations_subtree(animation::AnimationTimePoint now);
    [[nodiscard]] bool tick_animations_subtree(
        animation::AnimationTimePoint now, const std::optional<layout::Rect>& clip_rect);
    [[nodiscard]] bool contains(const UIElement& element) const noexcept;
    [[nodiscard]] UIElement& top_layer_host() noexcept;
    [[nodiscard]] const UIElement& top_layer_host() const noexcept;
    [[nodiscard]] UIElement* top_layer_key_target() noexcept;
    [[nodiscard]] const UIElement* top_layer_key_target() const noexcept;
    [[nodiscard]] UIElement* top_layer_pointer_target(layout::Point absolute_point) noexcept;
    [[nodiscard]] const UIElement*
    top_layer_pointer_target(layout::Point absolute_point) const noexcept;
    void clear_layout_callbacks_recursive_noexcept() noexcept;
    [[nodiscard]] layout::LayoutEngine& ensure_layout_engine();
    void clear_layout_engine_binding();
    void note_layout_dirty_root(UIElement& dirty_root) noexcept;
    [[nodiscard]] ElementSnapshot build_element_snapshot_subtree() const;
    [[nodiscard]] SemanticsNode build_semantics_node_subtree() const;
    [[nodiscard]] core::PropertyStore& property_store() noexcept;
    [[nodiscard]] animation::Storyboard& implicit_property_animations() noexcept;
    [[nodiscard]] StyleState& ensure_style_state();
    [[nodiscard]] TextState& ensure_text_state();
    [[nodiscard]] SemanticsElementState& ensure_semantics_state();
    [[nodiscard]] RenderState& ensure_render_state() const;
    [[nodiscard]] layout::LayoutElement& mutable_layout() noexcept;
    [[nodiscard]] style::UIElementStyle& mutable_style_value();
    [[nodiscard]] const style::UIElementStyle& style_value() const noexcept;
    [[nodiscard]] const TextState* text_state() const noexcept;
    [[nodiscard]] const SemanticsElementState* semantics_state() const noexcept;
    [[nodiscard]] const RenderState* render_state() const noexcept;
    [[nodiscard]] std::optional<layout::Rect>& mutable_viewport_override();
    [[nodiscard]] const std::optional<layout::Rect>& viewport_override() const noexcept;
    [[nodiscard]] layout::Point& mutable_scroll_offset();
    [[nodiscard]] layout::Point scroll_offset_value() const noexcept;
    [[nodiscard]] bool scroll_wheel_enabled_value() const noexcept;
    void set_scroll_wheel_enabled_value(bool enabled);
    void reset_text_selection_state() noexcept;
    void invalidate_render_commands() noexcept;
    void adopt_text_clipboard_service(std::shared_ptr<TextClipboardService> service) noexcept;

  protected:
    [[nodiscard]] rendering::TextEngine& text_engine() const;
    bool disabled_ : 1 = false;
    bool hovered_ : 1 = false;

  private:
    static constexpr std::size_t inline_child_capacity = 8;

    std::unique_ptr<layout::LayoutElement> layout_owner_;
    layout::LayoutElement* layout_ = nullptr;
    std::unique_ptr<layout::LayoutEngine> detached_layout_engine_;
    layout::LayoutEngine* layout_engine_ = nullptr;
    UIElement* parent_ = nullptr;
    UIElement* root_ = nullptr;
    std::shared_ptr<TextClipboardService> text_clipboard_service_;
    UIElement* layout_dirty_root_ = nullptr;
    const UIElement* logical_owner_ = nullptr;
    layout::Rect committed_frame_{};
    layout::Rect committed_absolute_frame_{};
    std::uint64_t layout_generation_ = 0;
    std::optional<layout::Rect> pending_visual_dirty_bounds_;
    std::thread::id owner_thread_id_;
    EventRouter* event_router_ = nullptr;
    FocusManager* focus_manager_ = nullptr;
    std::vector<std::unique_ptr<UIElement>> children_;
    TopLayerManager top_layer_manager_;
    bool visible_ : 1 = true;
    bool hit_test_visible_ : 1 = true;
    bool relayout_boundary_ : 1 = false;
    mutable bool z_order_dirty_ : 1 = false;
    mutable std::unique_ptr<std::vector<UIElement*>> sorted_children_cache_;
    bool custom_measure_callback_ : 1 = false;
    bool focusable_ : 1 = false;
    bool focused_ : 1 = false;
    bool needs_layout_ : 1 = true;
    bool self_needs_paint_ : 1 = true;
    bool needs_paint_ : 1 = true;
    bool has_scrollable_extent_ : 1 = false;
    bool theme_dirty_ : 1 = true;
    bool theme_subtree_dirty_ : 1 = true;
    std::uint32_t animation_tick_depth_ = 0;
    std::uint64_t z_index_gen_ = 0;
    std::unique_ptr<StyleState> style_state_;
    std::unique_ptr<TextState> text_state_;
    std::unique_ptr<PropertyState> property_state_;
    std::unique_ptr<AnimationState> animation_state_;
    std::unique_ptr<SemanticsElementState> semantics_state_;
    mutable std::unique_ptr<RenderState> render_state_;
};

} // namespace winelement::elements
