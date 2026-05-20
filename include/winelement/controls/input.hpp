#pragma once

#include <winelement/animation.hpp>
#include <winelement/controls/control.hpp>
#include <winelement/controls/control_animation.hpp>
#include <winelement/elements/popup_manager.hpp>
#include <winelement/elements/svg_icon.hpp>
#include <winelement/rendering/text_engine.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winelement::rendering {
class RenderContext;
}

namespace winelement::controls {

enum class InputType { Text, Password, Textarea };
enum class InputSize { Default, Large, Small };
enum class InputStatus { Default, Success, Warning, Error };
enum class InputWordLimitPosition { Inside, Outside };

struct InputAccessibilitySnapshot {
    std::string text;
    std::string placeholder;
    std::string selected_text;
    std::size_t caret_byte_offset = 0;
    std::size_t selection_start_byte_offset = 0;
    std::size_t selection_end_byte_offset = 0;
    bool disabled = false;
    bool read_only = false;
    bool multiline = false;
    bool password = false;
};

struct InputDisplayTextOffsetMapping {
    std::vector<std::size_t> display_boundaries;
    std::vector<std::size_t> text_offsets;
};

class Input final : public Control {
  public:
    using TextTransform = std::function<std::string(std::string_view)>;
    using TextChangeHandler = std::function<void(std::string_view)>;
    using VoidHandler = std::function<void()>;
    using KeyHandler = std::function<void(const elements::KeyEvent&)>;
    using GraphemeCountHandler = std::function<std::size_t(std::string_view)>;

    Input();
    ~Input() override;

    Input& set_text(std::string_view text);
    Input& set_placeholder(std::string_view placeholder);
    Input& set_type(InputType type) noexcept;
    Input& set_disabled(bool disabled) noexcept;
    Input& set_read_only(bool read_only) noexcept;
    Input& set_password(bool password) noexcept;
    Input& set_show_password_toggle(bool show_password_toggle) noexcept;
    Input& set_password_visible(bool password_visible) noexcept;
    Input& set_clearable(bool clearable) noexcept;
    Input& set_size(InputSize size) noexcept;
    Input& set_status(InputStatus status) noexcept;
    Input& set_validate_event(bool validate_event) noexcept;
    Input& set_autocomplete(std::string_view autocomplete);
    Input& set_input_mode(std::string_view input_mode);
    Input& set_spellcheck(bool spellcheck) noexcept;
    Input& set_autofill_hint(std::string_view autofill_hint);
    Input& set_prefix_text(std::string_view prefix_text);
    Input& set_suffix_text(std::string_view suffix_text);
    Input& set_prepend_text(std::string_view prepend_text);
    Input& set_append_text(std::string_view append_text);
    Input& set_prefix_icon_name(std::string_view icon_name);
    Input& set_suffix_icon_name(std::string_view icon_name);
    Input& set_clear_aria_label(std::string_view label);
    Input& set_password_toggle_aria_label(std::string_view label);
    Input& set_max_length(std::optional<std::size_t> max_length);
    Input& set_min_length(std::optional<std::size_t> min_length);
    Input& set_show_word_limit(bool show_word_limit) noexcept;
    Input& set_word_limit_position(InputWordLimitPosition position) noexcept;
    Input& set_count_graphemes(bool count_graphemes) noexcept;
    Input& set_count_graphemes_handler(GraphemeCountHandler handler);
    Input& set_rows(std::size_t rows);
    Input& set_autosize(bool autosize) noexcept;
    Input& set_autosize_limits(std::size_t min_rows, std::size_t max_rows);
    Input& set_formatter(TextTransform formatter);
    Input& set_parser(TextTransform parser);
    Input& set_on_input(TextChangeHandler handler);
    Input& set_on_change(TextChangeHandler handler);
    Input& set_on_clear(VoidHandler handler);
    Input& set_on_focus(VoidHandler handler);
    Input& set_on_blur(VoidHandler handler);
    Input& set_on_key_down(KeyHandler handler);
    Input& set_on_mouse_enter(VoidHandler handler);
    Input& set_on_mouse_leave(VoidHandler handler);
    Input& set_on_composition_start(VoidHandler handler);
    Input& set_on_composition_update(TextChangeHandler handler);
    Input& set_on_composition_end(TextChangeHandler handler);
    Input& set_style(style::UIElementStyle style) override;
    Input& set_caret_byte_offset(std::size_t byte_offset);
    Input& set_selection(std::size_t anchor_byte_offset, std::size_t active_byte_offset);
    Input& clear();
    Input& select_all();
    Input& toggle_password_visibility() noexcept;
    Input& commit_change();
    [[nodiscard]] const std::string& text() const noexcept;
    [[nodiscard]] const std::string& placeholder() const noexcept;
    [[nodiscard]] InputType type() const noexcept;
    [[nodiscard]] bool disabled() const noexcept;
    [[nodiscard]] bool read_only() const noexcept;
    [[nodiscard]] bool password() const noexcept;
    [[nodiscard]] bool show_password_toggle() const noexcept;
    [[nodiscard]] bool password_visible() const noexcept;
    [[nodiscard]] bool clearable() const noexcept;
    [[nodiscard]] InputSize size() const noexcept;
    [[nodiscard]] InputStatus status() const noexcept;
    [[nodiscard]] bool validate_event() const noexcept;
    [[nodiscard]] const std::string& autocomplete() const noexcept;
    [[nodiscard]] const std::string& input_mode() const noexcept;
    [[nodiscard]] bool spellcheck() const noexcept;
    [[nodiscard]] const std::string& autofill_hint() const noexcept;
    [[nodiscard]] const std::string& prefix_text() const noexcept;
    [[nodiscard]] const std::string& suffix_text() const noexcept;
    [[nodiscard]] const std::string& prepend_text() const noexcept;
    [[nodiscard]] const std::string& append_text() const noexcept;
    [[nodiscard]] const std::string& prefix_icon_name() const noexcept;
    [[nodiscard]] const std::string& suffix_icon_name() const noexcept;
    [[nodiscard]] const std::string& clear_aria_label() const noexcept;
    [[nodiscard]] const std::string& password_toggle_aria_label() const noexcept;
    [[nodiscard]] std::optional<std::size_t> max_length() const noexcept;
    [[nodiscard]] std::optional<std::size_t> min_length() const noexcept;
    [[nodiscard]] bool show_word_limit() const noexcept;
    [[nodiscard]] InputWordLimitPosition word_limit_position() const noexcept;
    [[nodiscard]] bool count_graphemes() const noexcept;
    [[nodiscard]] std::size_t rows() const noexcept;
    [[nodiscard]] bool autosize() const noexcept;
    [[nodiscard]] std::size_t autosize_min_rows() const noexcept;
    [[nodiscard]] std::size_t autosize_max_rows() const noexcept;
    [[nodiscard]] std::size_t text_length() const;
    [[nodiscard]] bool length_valid() const;
    [[nodiscard]] bool is_composing() const noexcept;
    [[nodiscard]] std::size_t caret_byte_offset() const noexcept;
    [[nodiscard]] std::size_t selection_anchor_byte_offset() const noexcept;
    [[nodiscard]] std::size_t selection_active_byte_offset() const noexcept;
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] float vertical_scroll_offset() const noexcept;
    [[nodiscard]] bool vertical_scrollbar_visible() const;
    [[nodiscard]] InputAccessibilitySnapshot accessibility_snapshot() const;
    [[nodiscard]] std::optional<layout::Rect> text_input_caret_rect() const;
    [[nodiscard]] elements::TextInputEditCommandState text_input_edit_command_state() const;
    bool invoke_text_input_edit_command(elements::TextInputEditCommand command);
    bool show_text_input_context_menu(layout::Point absolute_position);
    void dismiss_text_input_context_menu() noexcept;
    [[nodiscard]] bool text_input_context_menu_open() const noexcept;
    [[nodiscard]] bool
    text_input_context_menu_hit_test(layout::Point absolute_position) const noexcept;
    bool handle_text_input_context_menu_pointer(elements::PointerEvent& event);

  protected:
    void on_pointer_event(elements::PointerEvent& event) override;
    void on_key_event(elements::KeyEvent& event) override;
    void on_focus_changed(const elements::FocusChangeEvent& event) override;
    [[nodiscard]] elements::PointerCursor
    cursor_for_local_point(layout::Point local_position) const noexcept override;
    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override;
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;
    void on_paint_overlay(rendering::RenderContext& context,
                          layout::Rect absolute_frame) const override;

  private:
    class TextInputHandlerAdapter;

    struct Geometry {
        layout::Rect frame{};
        layout::Rect prepend_rect{};
        layout::Rect control_rect{};
        layout::Rect append_rect{};
        layout::Rect text_rect{};
        layout::Rect prefix_rect{};
        layout::Rect suffix_rect{};
        layout::Rect clear_button_rect{};
        layout::Rect password_button_rect{};
        layout::Rect word_limit_rect{};
        bool has_prepend = false;
        bool has_append = false;
        bool has_prefix = false;
        bool has_suffix = false;
        bool has_clear_button = false;
        bool has_password_button = false;
        bool has_inside_word_limit = false;
    };

    struct UndoState {
        std::string text;
        std::size_t caret_byte_offset = 0;
        std::size_t selection_anchor_byte_offset = 0;
        std::size_t selection_active_byte_offset = 0;
    };

    struct DisplayTextOffsetMappingCache {
        std::string source_text;
        std::string rendered_text;
        std::uint64_t generation = 0;
        bool valid = false;
        InputDisplayTextOffsetMapping mapping;
    };

    void update_measure_callback();
    void restart_caret_blink();
    void ensure_caret_visible();
    bool scroll_textarea_by(float delta_y);
    void scroll_textarea_for_pointer(layout::Point local_position, const Geometry& geometry);
    void update_pointer_selection(layout::Point local_position);
    void select_word_at(layout::Point local_position);
    void select_line_at(layout::Point local_position);
    [[nodiscard]] std::optional<elements::TextInputEditCommand>
    context_menu_command_for_id(std::string_view id) const noexcept;
    void move_caret_visually_left();
    void move_caret_visually_right();
    void move_caret_visually_up();
    void move_caret_visually_down();
    void delete_selection();
    void insert_text_at_caret(std::string_view text);
    void replace_selection_with_text(std::string_view text, bool record_undo = true);
    void apply_user_text(std::string text, std::size_t preferred_caret_offset);
    void push_undo_state();
    void undo();
    void redo();
    void restore_undo_state(const UndoState& state);
    [[nodiscard]] UndoState current_undo_state() const;
    void copy_selection_to_clipboard() const;
    void paste_from_clipboard();
    void emit_input();
    void emit_change_if_needed();
    [[nodiscard]] std::string selected_text() const;
    [[nodiscard]] std::string sanitized_input(std::string_view text) const;
    [[nodiscard]] std::string parsed_text(std::string_view text) const;
    [[nodiscard]] std::string truncated_to_max_length(std::string_view text) const;
    [[nodiscard]] std::string display_text() const;
    [[nodiscard]] std::string display_text_for_rendering() const;
    [[nodiscard]] std::size_t display_caret_offset_for_rendering() const;
    [[nodiscard]] std::string word_limit_text() const;
    [[nodiscard]] std::size_t display_offset_for_text_offset(std::size_t byte_offset) const;
    [[nodiscard]] std::size_t text_offset_for_display_offset(std::size_t display_offset) const;
    [[nodiscard]] const InputDisplayTextOffsetMapping&
    display_text_offset_mapping(std::string_view rendered_text) const;
    void invalidate_display_text_offset_mapping() const noexcept;
    [[nodiscard]] rendering::TextLayout create_text_layout(std::string_view text,
                                                           float max_width = 0.0F) const;
    [[nodiscard]] style::UIElementStyle resolved_style() const noexcept;
    [[nodiscard]] float animated_hover_progress() const;
    [[nodiscard]] float animated_scrollbar_progress() const;
    void animate_hover(float target);
    void animate_scrollbar(float target);
    [[nodiscard]] Geometry create_geometry(layout::Rect frame) const;
    [[nodiscard]] layout::Size measure_affix_text(std::string_view text) const;
    [[nodiscard]] std::size_t count_text_units(std::string_view text) const;
    [[nodiscard]] float row_height() const;
    void initialize_icons();
    [[nodiscard]] layout::Rect
    caret_rect_in_content_space(const Geometry& geometry,
                                const rendering::TextLayout& text_layout) const;
    [[nodiscard]] float max_horizontal_scroll_x(const rendering::TextLayout& text_layout,
                                                const Geometry& geometry) const;
    [[nodiscard]] float clamped_horizontal_scroll_x(const rendering::TextLayout& text_layout,
                                                    const Geometry& geometry) const;
    [[nodiscard]] float max_vertical_scroll_y(const rendering::TextLayout& text_layout,
                                              const Geometry& geometry) const noexcept;
    [[nodiscard]] float clamped_vertical_scroll_y(const rendering::TextLayout& text_layout,
                                                  const Geometry& geometry) const noexcept;
    [[nodiscard]] float text_origin_x(const Geometry& geometry,
                                      const rendering::TextLayout& text_layout) const;
    [[nodiscard]] float text_origin_y(const Geometry& geometry,
                                      const rendering::TextLayout& text_layout) const noexcept;
    [[nodiscard]] bool caret_blink_visible() const;
    [[nodiscard]] layout::Rect
    vertical_scrollbar_track_rect(const Geometry& geometry) const noexcept;
    [[nodiscard]] layout::Rect
    vertical_scrollbar_thumb_rect(const Geometry& geometry,
                                  const rendering::TextLayout& text_layout) const noexcept;
    [[nodiscard]] bool
    vertical_scrollbar_visible_for(const Geometry& geometry,
                                   const rendering::TextLayout& text_layout) const noexcept;
    bool set_vertical_scroll_from_pointer(layout::Point local_position);
    [[nodiscard]] bool textarea() const noexcept;
    [[nodiscard]] bool editable() const noexcept;
    [[nodiscard]] bool can_show_inline_affixes() const noexcept;
    [[nodiscard]] bool can_show_clear_button() const noexcept;
    [[nodiscard]] bool can_show_password_button() const noexcept;
    void mark_text_transform_generation_changed() noexcept;

    std::string committed_text_;
    std::string placeholder_;
    std::string prefix_text_;
    std::string suffix_text_;
    std::string prepend_text_;
    std::string append_text_;
    std::string prefix_icon_name_;
    std::string suffix_icon_name_;
    std::string clear_aria_label_ = "Clear input";
    std::string password_toggle_aria_label_ = "Toggle password visibility";
    std::string autocomplete_;
    std::string input_mode_;
    std::string autofill_hint_;
    InputType type_ = InputType::Text;
    InputSize size_ = InputSize::Default;
    InputStatus status_ = InputStatus::Default;
    InputWordLimitPosition word_limit_position_ = InputWordLimitPosition::Inside;
    std::optional<std::size_t> max_length_;
    std::optional<std::size_t> min_length_;
    std::size_t caret_byte_offset_ = 0;
    std::size_t selection_anchor_byte_offset_ = 0;
    std::size_t selection_active_byte_offset_ = 0;
    std::size_t rows_ = 2;
    std::size_t autosize_min_rows_ = 2;
    std::size_t autosize_max_rows_ = 0;
    float horizontal_scroll_x_ = 0.0F;
    float vertical_scroll_y_ = 0.0F;
    std::size_t pointer_selection_anchor_byte_offset_ = 0;
    layout::Rect context_menu_rect_{};
    elements::PopupHandle context_menu_popup_{};
    std::chrono::steady_clock::time_point caret_blink_epoch_ = std::chrono::steady_clock::now();
    bool read_only_ = false;
    bool show_password_toggle_ = false;
    bool password_visible_ = false;
    bool clearable_ = false;
    bool show_word_limit_ = false;
    bool count_graphemes_ = false;
    bool autosize_ = false;
    bool validate_event_ = true;
    bool pointer_selecting_ = false;
    bool scrollbar_dragging_ = false;
    bool context_menu_open_ = false;
    bool composition_active_ = false;
    bool composition_deleted_selection_ = false;
    bool spellcheck_ = false;
    std::string composition_text_;
    std::vector<UndoState> undo_stack_;
    std::vector<UndoState> redo_stack_;
    std::shared_ptr<bool> lifetime_token_ = std::make_shared<bool>(true);
    mutable DisplayTextOffsetMappingCache display_offset_mapping_cache_;
    std::uint64_t text_transform_generation_ = 0;
    TextTransform formatter_;
    TextTransform parser_;
    GraphemeCountHandler count_graphemes_handler_;
    TextChangeHandler input_handler_;
    TextChangeHandler change_handler_;
    VoidHandler clear_handler_;
    VoidHandler focus_handler_;
    VoidHandler blur_handler_;
    KeyHandler key_down_handler_;
    VoidHandler mouse_enter_handler_;
    VoidHandler mouse_leave_handler_;
    VoidHandler composition_start_handler_;
    TextChangeHandler composition_update_handler_;
    TextChangeHandler composition_end_handler_;
    elements::SvgIcon clear_icon_;
    elements::SvgIcon password_visible_icon_;
    elements::SvgIcon password_hidden_icon_;
    AnimatedFloat hover_progress_{0.0F};
    AnimatedFloat scrollbar_progress_{0.0F};
};

} // namespace winelement::controls
