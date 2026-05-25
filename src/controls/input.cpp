#include <winelement/controls/input.hpp>

#include <winelement/controls/property_keys.hpp>

#include "control_style.hpp"

#include <winelement/controls/context_menu.hpp>
#include <winelement/elements/all_icons.hpp>
#include <winelement/elements/popup_manager.hpp>
#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace winelement::controls {
namespace {

constexpr auto default_icon_extent = 20.0F;
constexpr auto input_icon_draw_size = 14.0F;
constexpr auto default_affix_gap = 8.0F;
constexpr auto group_segment_horizontal_padding = 20.0F;
constexpr auto outside_word_limit_height = 16.0F;
constexpr auto caret_blink_interval = std::chrono::milliseconds{500};
constexpr auto wheel_scroll_rows = 3.0F;
constexpr auto password_mask_visual_center_factor = 0.62F;

[[nodiscard]] std::pair<std::size_t, std::size_t> ordered_range(std::size_t first,
                                                                std::size_t second) noexcept {
    return {std::min(first, second), std::max(first, second)};
}

[[nodiscard]] bool contains_local_point(layout::Rect rect, layout::Point point) noexcept {
    return rect.width > 0.0F && rect.height > 0.0F && point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

[[nodiscard]] layout::Rect centered_square_rect(layout::Rect bounds, float size) noexcept {
    const auto extent = std::max(0.0F, std::min({size, bounds.width, bounds.height}));
    return layout::Rect{bounds.x + (bounds.width - extent) * 0.5F,
                        bounds.y + (bounds.height - extent) * 0.5F, extent, extent};
}

[[nodiscard]] bool contains_centered_circle(layout::Rect bounds, float diameter,
                                            layout::Point point) noexcept {
    const auto circle = centered_square_rect(bounds, diameter);
    if (circle.width <= 0.0F || circle.height <= 0.0F) {
        return false;
    }
    const auto radius = circle.width * 0.5F;
    const auto center =
        layout::Point{circle.x + circle.width * 0.5F, circle.y + circle.height * 0.5F};
    const auto dx = point.x - center.x;
    const auto dy = point.y - center.y;
    return dx * dx + dy * dy <= radius * radius;
}

[[nodiscard]] bool is_ascii_word_byte(unsigned char value) noexcept {
    return std::isalnum(value) != 0 || value == '_';
}

[[nodiscard]] bool is_word_byte(std::string_view text, std::size_t offset) noexcept {
    if (offset >= text.size()) {
        return false;
    }
    const auto value = static_cast<unsigned char>(text[offset]);
    if (value < 0x80U) {
        return is_ascii_word_byte(value);
    }
    return !rendering::is_utf8_continuation_byte(value);
}

[[nodiscard]] bool is_line_break_byte(std::string_view text, std::size_t offset) noexcept {
    return offset < text.size() && text[offset] == '\n';
}

[[nodiscard]] bool has_context_menu_commands(elements::TextInputEditCommandState state) noexcept {
    return state.can_cut || state.can_copy || state.can_paste || state.can_select_all;
}

[[nodiscard]] bool
context_menu_command_enabled(elements::TextInputEditCommand command,
                             elements::TextInputEditCommandState state) noexcept {
    switch (command) {
    case elements::TextInputEditCommand::Cut:
        return state.can_cut;
    case elements::TextInputEditCommand::Copy:
        return state.can_copy;
    case elements::TextInputEditCommand::Paste:
        return state.can_paste;
    case elements::TextInputEditCommand::SelectAll:
        return state.can_select_all;
    }
    return false;
}

[[nodiscard]] std::string_view context_menu_label(elements::TextInputEditCommand command) noexcept {
    switch (command) {
    case elements::TextInputEditCommand::Cut:
        return "Cut";
    case elements::TextInputEditCommand::Copy:
        return "Copy";
    case elements::TextInputEditCommand::Paste:
        return "Paste";
    case elements::TextInputEditCommand::SelectAll:
        return "Select all";
    }
    return {};
}

[[nodiscard]] std::string_view
context_menu_command_id(elements::TextInputEditCommand command) noexcept {
    switch (command) {
    case elements::TextInputEditCommand::Cut:
        return "cut";
    case elements::TextInputEditCommand::Copy:
        return "copy";
    case elements::TextInputEditCommand::Paste:
        return "paste";
    case elements::TextInputEditCommand::SelectAll:
        return "select_all";
    }
    return {};
}

[[nodiscard]] std::vector<std::size_t> utf8_boundaries(std::string_view text) {
    std::vector<std::size_t> boundaries;
    boundaries.push_back(0U);
    auto offset = std::size_t{0};
    while (offset < text.size()) {
        offset = rendering::next_utf8_boundary(text, offset);
        boundaries.push_back(offset);
    }
    return boundaries;
}

[[nodiscard]] std::size_t common_prefix_text_offset(std::string_view text,
                                                    std::string_view candidate) noexcept {
    auto text_offset = std::size_t{0};
    auto candidate_offset = std::size_t{0};
    while (text_offset < text.size() && candidate_offset < candidate.size()) {
        const auto next_text_offset = rendering::next_utf8_boundary(text, text_offset);
        const auto next_candidate_offset =
            rendering::next_utf8_boundary(candidate, candidate_offset);
        if (text.substr(text_offset, next_text_offset - text_offset) !=
            candidate.substr(candidate_offset, next_candidate_offset - candidate_offset)) {
            break;
        }
        text_offset = next_text_offset;
        candidate_offset = next_candidate_offset;
    }
    return text_offset;
}

[[nodiscard]] InputDisplayTextOffsetMapping
build_subsequence_display_offset_mapping(std::string_view text, std::string_view display);

[[nodiscard]] InputDisplayTextOffsetMapping
build_parser_display_offset_mapping(std::string_view text, std::string_view display,
                                    const Input::TextTransform& parser) {
    const auto parsed_display = parser(display);
    if (parsed_display == text) {
        auto subsequence_mapping = build_subsequence_display_offset_mapping(text, display);
        if (!subsequence_mapping.text_offsets.empty() &&
            subsequence_mapping.text_offsets.back() == text.size()) {
            return subsequence_mapping;
        }
        if (text.size() == display.size()) {
            subsequence_mapping.display_boundaries = utf8_boundaries(display);
            subsequence_mapping.text_offsets = subsequence_mapping.display_boundaries;
            return subsequence_mapping;
        }
    }

    InputDisplayTextOffsetMapping mapping;
    mapping.display_boundaries = utf8_boundaries(display);
    mapping.text_offsets.reserve(mapping.display_boundaries.size());

    auto previous_text_offset = std::size_t{0};
    for (const auto boundary : mapping.display_boundaries) {
        const auto parsed_prefix = parser(display.substr(0U, boundary));
        previous_text_offset =
            std::max(previous_text_offset, common_prefix_text_offset(text, parsed_prefix));
        mapping.text_offsets.push_back(previous_text_offset);
    }
    return mapping;
}

[[nodiscard]] InputDisplayTextOffsetMapping
build_subsequence_display_offset_mapping(std::string_view text, std::string_view display) {
    InputDisplayTextOffsetMapping mapping;
    mapping.display_boundaries = utf8_boundaries(display);
    mapping.text_offsets.reserve(mapping.display_boundaries.size());

    auto text_offset = std::size_t{0};
    mapping.text_offsets.push_back(text_offset);
    for (std::size_t index = 1; index < mapping.display_boundaries.size(); ++index) {
        const auto previous_boundary = mapping.display_boundaries[index - 1U];
        const auto next_boundary = mapping.display_boundaries[index];
        if (text_offset < text.size()) {
            const auto next_text_offset = rendering::next_utf8_boundary(text, text_offset);
            if (text.substr(text_offset, next_text_offset - text_offset) ==
                display.substr(previous_boundary, next_boundary - previous_boundary)) {
                text_offset = next_text_offset;
            }
        }
        mapping.text_offsets.push_back(text_offset);
    }
    return mapping;
}

[[nodiscard]] InputDisplayTextOffsetMapping
build_display_offset_mapping(std::string_view text, std::string_view display,
                             const Input::TextTransform* parser) {
    if (parser != nullptr && static_cast<bool>(*parser)) {
        return build_parser_display_offset_mapping(text, display, *parser);
    }
    return build_subsequence_display_offset_mapping(text, display);
}

[[nodiscard]] std::size_t
mapped_display_offset_for_text_offset(std::string_view text,
                                      const InputDisplayTextOffsetMapping& mapping,
                                      std::size_t text_offset) noexcept {
    if (mapping.display_boundaries.empty() || mapping.text_offsets.empty()) {
        return 0U;
    }

    const auto clamped_text_offset =
        rendering::clamp_utf8_boundary(text, std::min(text_offset, text.size()));
    auto first_match = std::optional<std::size_t>{};
    auto last_match = std::optional<std::size_t>{};
    for (std::size_t index = 0; index < mapping.text_offsets.size(); ++index) {
        if (mapping.text_offsets[index] != clamped_text_offset) {
            if (mapping.text_offsets[index] > clamped_text_offset) {
                break;
            }
            continue;
        }
        if (!first_match) {
            first_match = mapping.display_boundaries[index];
        }
        last_match = mapping.display_boundaries[index];
    }

    if (clamped_text_offset == text.size() && first_match) {
        return *first_match;
    }
    if (last_match) {
        return *last_match;
    }

    for (std::size_t index = 0; index < mapping.text_offsets.size(); ++index) {
        if (mapping.text_offsets[index] > clamped_text_offset) {
            return mapping.display_boundaries[index];
        }
    }
    return mapping.display_boundaries.back();
}

[[nodiscard]] std::size_t
mapped_text_offset_for_display_offset(std::string_view display,
                                      const InputDisplayTextOffsetMapping& mapping,
                                      std::size_t display_offset) noexcept {
    if (mapping.display_boundaries.empty() || mapping.text_offsets.empty()) {
        return 0U;
    }

    const auto clamped_display_offset =
        rendering::clamp_utf8_boundary(display, std::min(display_offset, display.size()));
    const auto upper = std::upper_bound(mapping.display_boundaries.begin(),
                                        mapping.display_boundaries.end(), clamped_display_offset);
    const auto index = upper == mapping.display_boundaries.begin()
                           ? std::size_t{0}
                           : static_cast<std::size_t>(std::distance(
                                 mapping.display_boundaries.begin(), std::prev(upper)));
    return mapping.text_offsets[index];
}

[[nodiscard]] std::size_t
text_offset_for_display_code_point_offset(std::string_view text,
                                          std::size_t display_offset) noexcept {
    auto text_offset = std::size_t{0};
    for (auto index = std::size_t{0}; index < display_offset && text_offset < text.size();
         ++index) {
        text_offset = rendering::next_utf8_boundary(text, text_offset);
    }
    return text_offset;
}

[[nodiscard]] std::size_t newline_count(std::string_view text) noexcept {
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

[[nodiscard]] float clamp_non_negative(float value) noexcept {
    return std::max(value, 0.0F);
}

[[nodiscard]] bool has_active_composition(bool composition_active) noexcept {
    return composition_active;
}

[[nodiscard]] bool has_visible_composition_text(bool composition_active,
                                                std::string_view composition_text) noexcept {
    return composition_active && !composition_text.empty();
}

[[nodiscard]] layout::Rect inset_rect(layout::Rect rect, layout::EdgeInsets padding) noexcept {
    rect.x += padding.left;
    rect.y += padding.top;
    rect.width = clamp_non_negative(rect.width - padding.left - padding.right);
    rect.height = clamp_non_negative(rect.height - padding.top - padding.bottom);
    return rect;
}

[[nodiscard]] std::string truncate_by_code_points(std::string_view text, std::size_t max_length) {
    auto offset = std::size_t{0};
    auto count = std::size_t{0};
    while (offset < text.size() && count < max_length) {
        offset = rendering::next_utf8_boundary(text, offset);
        ++count;
    }
    return std::string(text.substr(0U, offset));
}

[[nodiscard]] std::string strip_single_line_breaks(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const auto value : text) {
        if (value == '\r' || value == '\n') {
            continue;
        }
        result.push_back(value);
    }
    return result;
}

[[nodiscard]] std::string normalize_textarea_breaks(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (auto index = std::size_t{0}; index < text.size(); ++index) {
        const auto value = text[index];
        if (value == '\r') {
            if (index + 1U < text.size() && text[index + 1U] == '\n') {
                ++index;
            }
            result.push_back('\n');
            continue;
        }
        result.push_back(value);
    }
    return result;
}

} // namespace

struct Input::EventState {
    TextEventHandler input_changed;
    TextEventHandler change_committed;
    VoidEventHandler cleared;
    VoidEventHandler focused;
    VoidEventHandler blurred;
    KeyEventHandler key_down;
    VoidEventHandler mouse_entered;
    VoidEventHandler mouse_left;
    VoidEventHandler composition_started;
    TextEventHandler composition_updated;
    TextEventHandler composition_ended;
};

class Input::TextInputHandlerAdapter final : public elements::TextInputHandler {
  public:
    explicit TextInputHandlerAdapter(Input& owner) noexcept : owner_(owner) {}

    [[nodiscard]] std::optional<layout::Rect> caret_rect() const override {
        return owner_.text_input_caret_rect();
    }

    [[nodiscard]] elements::TextInputEditCommandState edit_command_state() const override {
        return owner_.text_input_edit_command_state();
    }

    bool invoke_edit_command(elements::TextInputEditCommand command) override {
        return owner_.invoke_text_input_edit_command(command);
    }

    bool show_context_menu(layout::Point absolute_position) override {
        return owner_.show_text_input_context_menu(absolute_position);
    }

    void dismiss_context_menu() noexcept override {
        owner_.dismiss_text_input_context_menu();
    }

    [[nodiscard]] bool context_menu_open() const noexcept override {
        return owner_.text_input_context_menu_open();
    }

    [[nodiscard]] bool
    context_menu_hit_test(layout::Point absolute_position) const noexcept override {
        return owner_.text_input_context_menu_hit_test(absolute_position);
    }

    bool handle_context_menu_pointer(elements::PointerEvent& event) override {
        return owner_.handle_text_input_context_menu_pointer(event);
    }

  private:
    Input& owner_;
};

Input::Input() : Control() {
    apply_style_value(style::default_input_style(), true);
    set_theme_class(style::theme_class::input);
    set_focusable(true);
    initialize_icons();
    set_text_input_handler(std::make_unique<TextInputHandlerAdapter>(*this));
    update_measure_callback();
}

Input::~Input() {
    *lifetime_token_ = false;
    dismiss_text_input_context_menu();
    set_text_input_handler(nullptr);
}

Input& Input::set_text(std::string_view text) {
    if (text_storage() == text) {
        return *this;
    }

    text_storage() = std::string(text);
    committed_text_ = text_storage();
    caret_byte_offset_ = rendering::clamp_utf8_boundary(text_storage(), text_storage().size());
    selection_anchor_byte_offset_ = caret_byte_offset_;
    selection_active_byte_offset_ = caret_byte_offset_;
    undo_stack_.clear();
    redo_stack_.clear();
    mark_text_transform_generation_changed();
    restart_caret_blink();
    ensure_caret_visible();
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_placeholder(std::string_view placeholder) {
    set_property(property_keys::input_placeholder(), std::string{placeholder});
    return *this;
}

Input& Input::set_type(InputType type) noexcept {
    set_property(property_keys::input_type(), type);
    return *this;
}

Input& Input::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return *this;
    }

    UIElement::set_disabled(disabled);
    if (disabled_) {
        pointer_selecting_ = false;
        release_pointer_capture();
        dismiss_text_input_context_menu();
    }
    invalidate_paint();
    return *this;
}

Input& Input::set_read_only(bool read_only) noexcept {
    if (read_only_ == read_only) {
        return *this;
    }

    read_only_ = read_only;
    if (read_only_) {
        pointer_selecting_ = false;
        scrollbar_dragging_ = false;
        release_pointer_capture();
        dismiss_text_input_context_menu();
    }
    invalidate_paint();
    return *this;
}

Input& Input::set_password(bool password) noexcept {
    return set_type(password ? InputType::Password : InputType::Text);
}

Input& Input::set_show_password_toggle(bool show_password_toggle) noexcept {
    if (show_password_toggle_ == show_password_toggle) {
        return *this;
    }

    show_password_toggle_ = show_password_toggle;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_password_visible(bool password_visible) noexcept {
    if (password_visible_ == password_visible) {
        return *this;
    }

    password_visible_ = password_visible && password();
    mark_text_transform_generation_changed();
    invalidate_paint();
    return *this;
}

Input& Input::set_clearable(bool clearable) noexcept {
    if (clearable_ == clearable) {
        return *this;
    }

    clearable_ = clearable;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_size(InputSize size) noexcept {
    set_property(property_keys::input_size(), size);
    return *this;
}

Input& Input::set_status(InputStatus status) noexcept {
    set_property(property_keys::input_status(), status);
    return *this;
}

Input& Input::set_validate_event(bool validate_event) noexcept {
    validate_event_ = validate_event;
    return *this;
}

Input& Input::set_autocomplete(std::string_view autocomplete) {
    autocomplete_ = autocomplete;
    return *this;
}

Input& Input::set_input_mode(std::string_view input_mode) {
    input_mode_ = input_mode;
    return *this;
}

Input& Input::set_spellcheck(bool spellcheck) noexcept {
    spellcheck_ = spellcheck;
    return *this;
}

Input& Input::set_autofill_hint(std::string_view autofill_hint) {
    autofill_hint_ = autofill_hint;
    return *this;
}

Input& Input::set_prefix_text(std::string_view prefix_text) {
    if (prefix_text_ == prefix_text) {
        return *this;
    }

    prefix_text_ = prefix_text;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_suffix_text(std::string_view suffix_text) {
    if (suffix_text_ == suffix_text) {
        return *this;
    }

    suffix_text_ = suffix_text;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_prepend_text(std::string_view prepend_text) {
    if (prepend_text_ == prepend_text) {
        return *this;
    }

    prepend_text_ = prepend_text;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_append_text(std::string_view append_text) {
    if (append_text_ == append_text) {
        return *this;
    }

    append_text_ = append_text;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_prefix_icon_name(std::string_view icon_name) {
    prefix_icon_name_ = std::string(icon_name);
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_suffix_icon_name(std::string_view icon_name) {
    suffix_icon_name_ = std::string(icon_name);
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_clear_aria_label(std::string_view label) {
    clear_aria_label_ = std::string(label);
    return *this;
}

Input& Input::set_password_toggle_aria_label(std::string_view label) {
    password_toggle_aria_label_ = std::string(label);
    return *this;
}

Input& Input::set_max_length(std::optional<std::size_t> max_length) {
    if (max_length_ == max_length) {
        return *this;
    }

    max_length_ = max_length;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_min_length(std::optional<std::size_t> min_length) {
    if (min_length_ == min_length) {
        return *this;
    }

    min_length_ = min_length;
    invalidate_paint();
    return *this;
}

Input& Input::set_show_word_limit(bool show_word_limit) noexcept {
    if (show_word_limit_ == show_word_limit) {
        return *this;
    }

    show_word_limit_ = show_word_limit;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_word_limit_position(InputWordLimitPosition position) noexcept {
    if (word_limit_position_ == position) {
        return *this;
    }

    word_limit_position_ = position;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_count_graphemes(bool count_graphemes) noexcept {
    if (count_graphemes_ == count_graphemes) {
        return *this;
    }

    count_graphemes_ = count_graphemes;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_count_graphemes_handler(GraphemeCountHandler handler) {
    count_graphemes_handler_ = std::move(handler);
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_rows(std::size_t rows) {
    const auto next_rows = std::max(rows, std::size_t{1});
    if (rows_ == next_rows) {
        return *this;
    }

    rows_ = next_rows;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_autosize(bool autosize) noexcept {
    if (autosize_ == autosize) {
        return *this;
    }

    autosize_ = autosize;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_autosize_limits(std::size_t min_rows, std::size_t max_rows) {
    const auto next_min_rows = std::max(min_rows, std::size_t{1});
    const auto next_max_rows = max_rows == 0U ? 0U : std::max(max_rows, next_min_rows);
    if (autosize_min_rows_ == next_min_rows && autosize_max_rows_ == next_max_rows) {
        return *this;
    }

    autosize_min_rows_ = next_min_rows;
    autosize_max_rows_ = next_max_rows;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_formatter(TextTransform formatter) {
    formatter_ = std::move(formatter);
    mark_text_transform_generation_changed();
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Input& Input::set_parser(TextTransform parser) {
    parser_ = std::move(parser);
    mark_text_transform_generation_changed();
    return *this;
}

Input::TextEventHandler& Input::input_changed() noexcept {
    return ensure_event_state().input_changed;
}

Input::TextEventHandler& Input::change_committed() noexcept {
    return ensure_event_state().change_committed;
}

Input::VoidEventHandler& Input::cleared() noexcept {
    return ensure_event_state().cleared;
}

Input::VoidEventHandler& Input::focus_received() noexcept {
    return ensure_event_state().focused;
}

Input::VoidEventHandler& Input::focus_lost() noexcept {
    return ensure_event_state().blurred;
}

Input::KeyEventHandler& Input::key_down() noexcept {
    return ensure_event_state().key_down;
}

Input::VoidEventHandler& Input::mouse_entered() noexcept {
    return ensure_event_state().mouse_entered;
}

Input::VoidEventHandler& Input::mouse_left() noexcept {
    return ensure_event_state().mouse_left;
}

Input::VoidEventHandler& Input::composition_started() noexcept {
    return ensure_event_state().composition_started;
}

Input::TextEventHandler& Input::composition_updated() noexcept {
    return ensure_event_state().composition_updated;
}

Input::TextEventHandler& Input::composition_ended() noexcept {
    return ensure_event_state().composition_ended;
}

Input::EventState& Input::ensure_event_state() {
    if (event_state_ == nullptr) {
        event_state_ = std::make_unique<EventState>();
    }
    return *event_state_;
}

Input& Input::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    caret_byte_offset_ = rendering::clamp_utf8_boundary(text_storage(), caret_byte_offset_);
    mark_measure_dirty();
    return *this;
}

Input& Input::set_caret_byte_offset(std::size_t byte_offset) {
    const auto clamped_offset = rendering::clamp_utf8_boundary(text_storage(), byte_offset);
    if (caret_byte_offset_ == clamped_offset && !has_selection()) {
        return *this;
    }

    caret_byte_offset_ = clamped_offset;
    selection_anchor_byte_offset_ = clamped_offset;
    selection_active_byte_offset_ = clamped_offset;
    restart_caret_blink();
    ensure_caret_visible();
    invalidate_paint();
    return *this;
}

Input& Input::set_selection(std::size_t anchor_byte_offset, std::size_t active_byte_offset) {
    selection_anchor_byte_offset_ =
        rendering::clamp_utf8_boundary(text_storage(), anchor_byte_offset);
    selection_active_byte_offset_ =
        rendering::clamp_utf8_boundary(text_storage(), active_byte_offset);
    caret_byte_offset_ = selection_active_byte_offset_;
    restart_caret_blink();
    ensure_caret_visible();
    invalidate_paint();
    return *this;
}

Input& Input::clear() {
    if (!text_storage().empty()) {
        push_undo_state();
        text_storage().clear();
        caret_byte_offset_ = 0U;
        selection_anchor_byte_offset_ = 0U;
        selection_active_byte_offset_ = 0U;
        restart_caret_blink();
        ensure_caret_visible();
        mark_measure_dirty();
        invalidate_paint();
        emit_input();
        emit_change_if_needed();
    }
    if (event_state_ != nullptr && !event_state_->cleared.empty()) {
        event_state_->cleared.emit();
    }
    return *this;
}

Input& Input::select_all() {
    return set_selection(0U, text_storage().size());
}

Input& Input::toggle_password_visibility() noexcept {
    return set_password_visible(!password_visible_);
}

Input& Input::commit_change() {
    emit_change_if_needed();
    return *this;
}

const std::string& Input::text() const noexcept {
    return text_storage();
}

const std::string& Input::placeholder() const noexcept {
    return placeholder_;
}

InputType Input::type() const noexcept {
    return type_;
}

bool Input::disabled() const noexcept {
    return disabled_;
}

bool Input::read_only() const noexcept {
    return read_only_;
}

bool Input::password() const noexcept {
    return type_ == InputType::Password;
}

bool Input::show_password_toggle() const noexcept {
    return show_password_toggle_;
}

bool Input::password_visible() const noexcept {
    return password_visible_;
}

bool Input::clearable() const noexcept {
    return clearable_;
}

InputSize Input::size() const noexcept {
    return size_;
}

InputStatus Input::status() const noexcept {
    return status_;
}

bool Input::validate_event() const noexcept {
    return validate_event_;
}

const std::string& Input::autocomplete() const noexcept {
    return autocomplete_;
}

const std::string& Input::input_mode() const noexcept {
    return input_mode_;
}

bool Input::spellcheck() const noexcept {
    return spellcheck_;
}

const std::string& Input::autofill_hint() const noexcept {
    return autofill_hint_;
}

const std::string& Input::prefix_text() const noexcept {
    return prefix_text_;
}

const std::string& Input::suffix_text() const noexcept {
    return suffix_text_;
}

const std::string& Input::prepend_text() const noexcept {
    return prepend_text_;
}

const std::string& Input::append_text() const noexcept {
    return append_text_;
}

const std::string& Input::prefix_icon_name() const noexcept {
    return prefix_icon_name_;
}

const std::string& Input::suffix_icon_name() const noexcept {
    return suffix_icon_name_;
}

const std::string& Input::clear_aria_label() const noexcept {
    return clear_aria_label_;
}

const std::string& Input::password_toggle_aria_label() const noexcept {
    return password_toggle_aria_label_;
}

std::optional<std::size_t> Input::max_length() const noexcept {
    return max_length_;
}

std::optional<std::size_t> Input::min_length() const noexcept {
    return min_length_;
}

bool Input::show_word_limit() const noexcept {
    return show_word_limit_;
}

InputWordLimitPosition Input::word_limit_position() const noexcept {
    return word_limit_position_;
}

bool Input::count_graphemes() const noexcept {
    return count_graphemes_;
}

std::size_t Input::rows() const noexcept {
    return rows_;
}

bool Input::autosize() const noexcept {
    return autosize_;
}

std::size_t Input::autosize_min_rows() const noexcept {
    return autosize_min_rows_;
}

std::size_t Input::autosize_max_rows() const noexcept {
    return autosize_max_rows_;
}

std::size_t Input::text_length() const {
    return count_text_units(text_storage());
}

bool Input::length_valid() const {
    const auto length = text_length();
    if (min_length_ && length < *min_length_) {
        return false;
    }
    if (max_length_ && length > *max_length_) {
        return false;
    }
    return true;
}

bool Input::is_composing() const noexcept {
    return composition_active_;
}

std::size_t Input::caret_byte_offset() const noexcept {
    return caret_byte_offset_;
}

std::size_t Input::selection_anchor_byte_offset() const noexcept {
    return selection_anchor_byte_offset_;
}

std::size_t Input::selection_active_byte_offset() const noexcept {
    return selection_active_byte_offset_;
}

bool Input::has_selection() const noexcept {
    return selection_anchor_byte_offset_ != selection_active_byte_offset_;
}

float Input::vertical_scroll_offset() const noexcept {
    return vertical_scroll_y_;
}

bool Input::vertical_scrollbar_visible() const {
    if (!textarea() || frame().width <= 0.0F || frame().height <= 0.0F) {
        return false;
    }
    const auto geometry = create_geometry(layout::Rect{0.0F, 0.0F, frame().width, frame().height});
    const auto text_layout = create_text_layout(display_text(), geometry.text_rect.width);
    return vertical_scrollbar_visible_for(geometry, text_layout);
}

InputAccessibilitySnapshot Input::accessibility_snapshot() const {
    const auto [selection_start, selection_end] =
        ordered_range(selection_anchor_byte_offset_, selection_active_byte_offset_);
    return InputAccessibilitySnapshot{.text = password() && !password_visible_ ? std::string{}
                                                                               : text_storage(),
                                      .placeholder = placeholder_,
                                      .selected_text = selected_text(),
                                      .caret_byte_offset = caret_byte_offset_,
                                      .selection_start_byte_offset = selection_start,
                                      .selection_end_byte_offset = selection_end,
                                      .disabled = disabled_,
                                      .read_only = read_only_,
                                      .multiline = textarea(),
                                      .password = password()};
}

std::optional<layout::Rect> Input::text_input_caret_rect() const {
    if (disabled_ || !focused()) {
        return std::nullopt;
    }

    const auto geometry = create_geometry(absolute_frame());
    const auto rendered_text = display_text_for_rendering();
    const auto text_layout = textarea()
                                 ? create_text_layout(rendered_text, geometry.text_rect.width)
                                 : create_text_layout(rendered_text);
    const auto origin =
        layout::Point{text_origin_x(geometry, text_layout), text_origin_y(geometry, text_layout)};
    const auto caret_rect = caret_rect_in_content_space(geometry, text_layout);
    return layout::Rect{origin.x + caret_rect.x, origin.y + caret_rect.y, caret_rect.width,
                        caret_rect.height};
}

elements::TextInputEditCommandState Input::text_input_edit_command_state() const {
    const auto can_copy = !selected_text().empty();
    const auto [selection_start, selection_end] =
        ordered_range(selection_anchor_byte_offset_, selection_active_byte_offset_);
    return elements::TextInputEditCommandState{
        .can_cut = editable() && can_copy,
        .can_copy = can_copy,
        .can_paste = editable() && text_clipboard_service().has_text(),
        .can_select_all = !text_storage().empty() &&
                          (selection_start != 0U || selection_end != text_storage().size())};
}

bool Input::invoke_text_input_edit_command(elements::TextInputEditCommand command) {
    const auto state = text_input_edit_command_state();
    switch (command) {
    case elements::TextInputEditCommand::Cut:
        if (!state.can_cut) {
            return false;
        }
        copy_selection_to_clipboard();
        push_undo_state();
        delete_selection();
        emit_input();
        return true;
    case elements::TextInputEditCommand::Copy:
        if (!state.can_copy) {
            return false;
        }
        copy_selection_to_clipboard();
        return true;
    case elements::TextInputEditCommand::Paste:
        if (!state.can_paste) {
            return false;
        }
        paste_from_clipboard();
        return true;
    case elements::TextInputEditCommand::SelectAll:
        if (!state.can_select_all) {
            return false;
        }
        select_all();
        return true;
    }

    return false;
}

bool Input::show_text_input_context_menu(layout::Point absolute_position) {
    const auto state = text_input_edit_command_state();
    if (disabled_ || !has_context_menu_commands(state)) {
        dismiss_text_input_context_menu();
        return false;
    }

    dismiss_text_input_context_menu();

    auto items = std::vector<ContextMenuItem>{};
    items.reserve(4U);
    for (const auto command :
         {elements::TextInputEditCommand::Cut, elements::TextInputEditCommand::Copy,
          elements::TextInputEditCommand::Paste, elements::TextInputEditCommand::SelectAll}) {
        items.push_back(ContextMenuItem{.text = std::string(context_menu_label(command)),
                                        .enabled = context_menu_command_enabled(command, state),
                                        .id = std::string(context_menu_command_id(command))});
    }

    auto menu = make_child<ContextMenu>();
    menu->set_items(std::move(items));
    auto weak_lifetime = std::weak_ptr<bool>{lifetime_token_};
    auto* owner = this;
    menu->selected() += [weak_lifetime, owner](const ContextMenu::SelectEvent& event) {
        const auto alive = weak_lifetime.lock();
        if (alive == nullptr || !*alive) {
            return;
        }
        if (const auto command = owner->context_menu_command_for_id(event.item.id)) {
            static_cast<void>(owner->invoke_text_input_edit_command(*command));
        }
        owner->dismiss_text_input_context_menu();
    };
    menu->dismissed() += [weak_lifetime, owner]() {
        const auto alive = weak_lifetime.lock();
        if (alive != nullptr && *alive) {
            owner->dismiss_text_input_context_menu();
        }
    };

    const auto popup_size = menu->preferred_size();
    elements::PopupManager popup_manager(*this);
    const auto result = popup_manager.open(
        std::move(menu),
        elements::PopupOptions{
            .anchor_rect = layout::Rect{absolute_position.x, absolute_position.y, 1.0F, 1.0F},
            .size = popup_size,
            .placement = elements::PopupPlacement::BottomStart,
            .gap = 0.0F,
            .viewport_margin = 4.0F,
            .light_dismiss = true,
            .preserve_focus = true});
    if (auto* popup = popup_manager.element(result.handle); popup != nullptr) {
        popup->dismissed_event() += [weak_lifetime, owner]() {
            const auto alive = weak_lifetime.lock();
            if (alive != nullptr && *alive) {
                owner->context_menu_open_ = false;
            }
        };
    }
    context_menu_rect_ = result.bounds;
    context_menu_popup_ = result.handle;
    context_menu_open_ = true;
    invalidate_paint();
    return true;
}

void Input::dismiss_text_input_context_menu() noexcept {
    const auto was_open = context_menu_open_;
    if (context_menu_popup_.valid()) {
        try {
            elements::PopupManager popup_manager(*this);
            static_cast<void>(popup_manager.close(context_menu_popup_));
            context_menu_popup_.reset();
            context_menu_open_ = false;
        } catch (...) {
            context_menu_open_ = was_open;
            return;
        }
    } else {
        context_menu_open_ = false;
    }
    if (was_open) {
        invalidate_paint();
    }
}

bool Input::text_input_context_menu_open() const noexcept {
    return context_menu_open_;
}

bool Input::text_input_context_menu_hit_test(layout::Point absolute_position) const noexcept {
    return context_menu_open_ && contains_local_point(context_menu_rect_, absolute_position);
}

bool Input::handle_text_input_context_menu_pointer(elements::PointerEvent& event) {
    static_cast<void>(event);
    return false;
}

void Input::apply_property_change(const core::PropertyChange& change) {
    if (!change.changed) {
        return;
    }

    const auto id = change.metadata->id;

    if (id == property_keys::input_type().id()) {
        auto* v = properties().local_value<InputType>(property_keys::input_type());
        type_ = v ? *v : InputType::Text;
        if (type_ != InputType::Password) {
            password_visible_ = false;
        }
        if (!textarea()) {
            vertical_scroll_y_ = 0.0F;
        }
        caret_byte_offset_ = rendering::clamp_utf8_boundary(text_storage(), caret_byte_offset_);
        selection_anchor_byte_offset_ =
            rendering::clamp_utf8_boundary(text_storage(), selection_anchor_byte_offset_);
        selection_active_byte_offset_ =
            rendering::clamp_utf8_boundary(text_storage(), selection_active_byte_offset_);
        mark_text_transform_generation_changed();
        mark_measure_dirty();
        invalidate_paint();
        return;
    }
    if (id == property_keys::input_size().id()) {
        auto* v = properties().local_value<InputSize>(property_keys::input_size());
        size_ = v ? *v : InputSize::Default;
        mark_measure_dirty();
        invalidate_paint();
        return;
    }
    if (id == property_keys::input_status().id()) {
        auto* v = properties().local_value<InputStatus>(property_keys::input_status());
        status_ = v ? *v : InputStatus::Default;
        invalidate_paint();
        return;
    }
    if (id == property_keys::input_placeholder().id()) {
        auto* v = properties().local_value<std::string>(property_keys::input_placeholder());
        placeholder_ = v ? *v : std::string{};
        mark_measure_dirty();
        invalidate_paint();
        return;
    }

    UIElement::apply_property_change(change);
}

void Input::on_pointer_event(elements::PointerEvent& event) {
    if (event.kind == elements::PointerEventKind::Enter) {
        hovered_ = true;
        animate_hover(1.0F);
        if (event_state_ != nullptr && !event_state_->mouse_entered.empty()) {
            event_state_->mouse_entered.emit();
        }
        invalidate_paint();
        return;
    }

    if (event.kind == elements::PointerEventKind::Leave ||
        event.kind == elements::PointerEventKind::Cancel) {
        hovered_ = false;
        animate_hover(0.0F);
        if (pointer_selecting_) {
            pointer_selecting_ = false;
            pointer_selection_anchor_byte_offset_ = caret_byte_offset_;
            release_pointer_capture();
        }
        if (scrollbar_dragging_) {
            scrollbar_dragging_ = false;
            animate_scrollbar(0.0F);
            release_pointer_capture();
        }
        if (event.kind == elements::PointerEventKind::Leave && event_state_ != nullptr &&
            !event_state_->mouse_left.empty()) {
            event_state_->mouse_left.emit();
        }
        invalidate_paint();
        return;
    }

    if (event.kind == elements::PointerEventKind::Wheel && textarea() && !disabled_) {
        event.handled = scroll_textarea_by(-event.wheel_delta.y * row_height() * wheel_scroll_rows);
        return;
    }

    if (event.kind == elements::PointerEventKind::Move && pointer_selecting_) {
        update_pointer_selection(event.local_position);
        event.handled = true;
        return;
    }

    if (event.kind == elements::PointerEventKind::Move && scrollbar_dragging_) {
        event.handled = set_vertical_scroll_from_pointer(event.local_position);
        return;
    }

    if (event.kind == elements::PointerEventKind::Up && pointer_selecting_) {
        update_pointer_selection(event.local_position);
        pointer_selecting_ = false;
        release_pointer_capture();
        event.handled = true;
        return;
    }

    if (event.kind == elements::PointerEventKind::Up && scrollbar_dragging_) {
        scrollbar_dragging_ = false;
        animate_scrollbar(0.0F);
        release_pointer_capture();
        event.handled = set_vertical_scroll_from_pointer(event.local_position);
        return;
    }

    if (event.kind == elements::PointerEventKind::Up &&
        event.button == elements::PointerButton::Secondary) {
        event.handled = true;
        if (!disabled_) {
            if (!has_selection()) {
                update_pointer_selection(event.local_position);
            }
            static_cast<void>(show_text_input_context_menu(event.position));
        }
        return;
    }

    if (event.kind != elements::PointerEventKind::Down &&
        event.kind != elements::PointerEventKind::DoubleClick) {
        return;
    }
    if (event.button == elements::PointerButton::Secondary) {
        event.handled = true;
        if (context_menu_open_) {
            dismiss_text_input_context_menu();
        }
        return;
    }
    if (event.button != elements::PointerButton::Primary) {
        return;
    }

    event.handled = true;
    if (disabled_) {
        return;
    }

    const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    const auto geometry = create_geometry(local_frame);
    if (geometry.has_clear_button &&
        contains_centered_circle(geometry.clear_button_rect, input_icon_draw_size,
                                 event.local_position)) {
        clear();
        return;
    }
    if (geometry.has_password_button &&
        contains_local_point(geometry.password_button_rect, event.local_position)) {
        toggle_password_visibility();
        return;
    }

    if (textarea()) {
        const auto text_layout =
            create_text_layout(display_text_for_rendering(), geometry.text_rect.width);
        if (vertical_scrollbar_visible_for(geometry, text_layout) &&
            contains_local_point(vertical_scrollbar_track_rect(geometry), event.local_position)) {
            scrollbar_dragging_ = true;
            animate_scrollbar(1.0F);
            static_cast<void>(capture_pointer());
            static_cast<void>(set_vertical_scroll_from_pointer(event.local_position));
            return;
        }
    }

    if (event.kind == elements::PointerEventKind::DoubleClick || event.click_count >= 2U) {
        if (event.click_count >= 3U) {
            select_line_at(event.local_position);
        } else {
            select_word_at(event.local_position);
        }
        return;
    }

    update_pointer_selection(event.local_position);
    pointer_selection_anchor_byte_offset_ = caret_byte_offset_;
    selection_anchor_byte_offset_ = pointer_selection_anchor_byte_offset_;
    selection_active_byte_offset_ = caret_byte_offset_;
    pointer_selecting_ = true;
    static_cast<void>(capture_pointer());
}

void Input::on_key_event(elements::KeyEvent& event) {
    struct KeyDownCallbackOnExit final {
        Input& owner;
        elements::KeyEvent& event;

        ~KeyDownCallbackOnExit() {
            if (event.kind == elements::KeyEventKind::Down && owner.event_state_ != nullptr &&
                !owner.event_state_->key_down.empty()) {
                owner.event_state_->key_down.emit(event);
            }
        }
    };

    if (disabled_) {
        return;
    }

    KeyDownCallbackOnExit key_down_callback{*this, event};

    if (event.kind == elements::KeyEventKind::CompositionStart) {
        composition_active_ = true;
        composition_deleted_selection_ = false;
        if (!read_only_ && has_selection()) {
            push_undo_state();
            delete_selection();
            composition_deleted_selection_ = true;
        }
        composition_text_.clear();
        restart_caret_blink();
        ensure_caret_visible();
        if (event_state_ != nullptr && !event_state_->composition_started.empty()) {
            event_state_->composition_started.emit();
        }
        event.handled = true;
        invalidate_paint();
        return;
    }

    if (event.kind == elements::KeyEventKind::CompositionUpdate) {
        composition_active_ = true;
        composition_text_ = event.text;
        restart_caret_blink();
        ensure_caret_visible();
        if (event_state_ != nullptr && !event_state_->composition_updated.empty()) {
            event_state_->composition_updated.emit(composition_text_);
        }
        event.handled = true;
        invalidate_paint();
        return;
    }

    if (event.kind == elements::KeyEventKind::CompositionEnd) {
        composition_active_ = false;
        composition_text_.clear();
        if (!read_only_ && !event.text.empty()) {
            replace_selection_with_text(event.text, !composition_deleted_selection_);
        } else if (composition_deleted_selection_ && !undo_stack_.empty()) {
            restore_undo_state(undo_stack_.back());
            undo_stack_.pop_back();
        } else {
            restart_caret_blink();
            ensure_caret_visible();
        }
        composition_deleted_selection_ = false;
        if (event_state_ != nullptr && !event_state_->composition_ended.empty()) {
            event_state_->composition_ended.emit(event.text);
        }
        event.handled = true;
        invalidate_paint();
        return;
    }

    if (event.kind == elements::KeyEventKind::TextInput && !event.text.empty()) {
        if (!read_only_) {
            replace_selection_with_text(event.text);
        }
        event.handled = true;
        return;
    }

    if (event.kind != elements::KeyEventKind::Down) {
        return;
    }

    switch (event.key) {
    case elements::Key::Enter:
        if (textarea() && !read_only_) {
            replace_selection_with_text("\n");
        } else {
            emit_change_if_needed();
        }
        event.handled = true;
        break;
    case elements::Key::A:
        if (event.modifiers.control) {
            set_selection(0U, text_storage().size());
            event.handled = true;
        }
        break;
    case elements::Key::C:
        if (event.modifiers.control) {
            event.handled = invoke_text_input_edit_command(elements::TextInputEditCommand::Copy);
        }
        break;
    case elements::Key::X:
        if (event.modifiers.control) {
            copy_selection_to_clipboard();
            if (!read_only_ && has_selection()) {
                push_undo_state();
                delete_selection();
                emit_input();
            }
            event.handled = true;
        }
        break;
    case elements::Key::V:
        if (event.modifiers.control) {
            event.handled = invoke_text_input_edit_command(elements::TextInputEditCommand::Paste);
        }
        break;
    case elements::Key::Z:
        if (event.modifiers.control) {
            if (!read_only_) {
                if (event.modifiers.shift) {
                    redo();
                } else {
                    undo();
                }
            }
            event.handled = true;
        }
        break;
    case elements::Key::Backspace:
        if (!read_only_ && has_selection()) {
            push_undo_state();
            delete_selection();
            emit_input();
        } else if (!read_only_ && caret_byte_offset_ > 0U) {
            push_undo_state();
            const auto previous =
                rendering::previous_utf8_boundary(text_storage(), caret_byte_offset_);
            text_storage().erase(previous, caret_byte_offset_ - previous);
            caret_byte_offset_ = previous;
            selection_anchor_byte_offset_ = caret_byte_offset_;
            selection_active_byte_offset_ = caret_byte_offset_;
            mark_text_transform_generation_changed();
            mark_measure_dirty();
            invalidate_paint();
            emit_input();
        }
        event.handled = true;
        break;
    case elements::Key::Delete:
        if (!read_only_ && has_selection()) {
            push_undo_state();
            delete_selection();
            emit_input();
        } else if (!read_only_ && caret_byte_offset_ < text_storage().size()) {
            push_undo_state();
            const auto next = rendering::next_utf8_boundary(text_storage(), caret_byte_offset_);
            text_storage().erase(caret_byte_offset_, next - caret_byte_offset_);
            selection_anchor_byte_offset_ = caret_byte_offset_;
            selection_active_byte_offset_ = caret_byte_offset_;
            mark_text_transform_generation_changed();
            mark_measure_dirty();
            invalidate_paint();
            emit_input();
        }
        event.handled = true;
        break;
    case elements::Key::Left:
        if (event.modifiers.shift) {
            const auto anchor =
                has_selection() ? selection_anchor_byte_offset_ : caret_byte_offset_;
            move_caret_visually_left();
            selection_anchor_byte_offset_ = anchor;
            selection_active_byte_offset_ = caret_byte_offset_;
        } else {
            move_caret_visually_left();
        }
        event.handled = true;
        break;
    case elements::Key::Right:
        if (event.modifiers.shift) {
            const auto anchor =
                has_selection() ? selection_anchor_byte_offset_ : caret_byte_offset_;
            move_caret_visually_right();
            selection_anchor_byte_offset_ = anchor;
            selection_active_byte_offset_ = caret_byte_offset_;
        } else {
            move_caret_visually_right();
        }
        event.handled = true;
        break;
    case elements::Key::Up:
        if (event.modifiers.shift) {
            const auto anchor =
                has_selection() ? selection_anchor_byte_offset_ : caret_byte_offset_;
            move_caret_visually_up();
            selection_anchor_byte_offset_ = anchor;
            selection_active_byte_offset_ = caret_byte_offset_;
        } else {
            move_caret_visually_up();
        }
        event.handled = true;
        break;
    case elements::Key::Down:
        if (event.modifiers.shift) {
            const auto anchor =
                has_selection() ? selection_anchor_byte_offset_ : caret_byte_offset_;
            move_caret_visually_down();
            selection_anchor_byte_offset_ = anchor;
            selection_active_byte_offset_ = caret_byte_offset_;
        } else {
            move_caret_visually_down();
        }
        event.handled = true;
        break;
    case elements::Key::Home:
        if (event.modifiers.shift) {
            caret_byte_offset_ = 0U;
            selection_active_byte_offset_ = caret_byte_offset_;
            invalidate_paint();
        } else {
            set_caret_byte_offset(0U);
        }
        event.handled = true;
        break;
    case elements::Key::End:
        if (event.modifiers.shift) {
            caret_byte_offset_ = text_storage().size();
            selection_active_byte_offset_ = caret_byte_offset_;
            invalidate_paint();
        } else {
            set_caret_byte_offset(text_storage().size());
        }
        event.handled = true;
        break;
    default:
        break;
    }
    if (event.handled) {
        restart_caret_blink();
        ensure_caret_visible();
    }
}

elements::PointerCursor Input::cursor_for_local_point(layout::Point local_position) const noexcept {
    if (disabled_) {
        return elements::PointerCursor::NotAllowed;
    }
    const auto geometry = create_geometry(layout::Rect{0.0F, 0.0F, frame().width, frame().height});
    if (geometry.has_clear_button &&
        contains_local_point(geometry.clear_button_rect, local_position)) {
        return contains_centered_circle(geometry.clear_button_rect, input_icon_draw_size,
                                        local_position)
                   ? elements::PointerCursor::Hand
                   : elements::PointerCursor::Default;
    }
    if (geometry.has_password_button &&
        contains_local_point(geometry.password_button_rect, local_position)) {
        return elements::PointerCursor::Hand;
    }
    if (textarea()) {
        const auto text_layout =
            create_text_layout(display_text_for_rendering(), geometry.text_rect.width);
        if (vertical_scrollbar_visible_for(geometry, text_layout) &&
            contains_local_point(vertical_scrollbar_track_rect(geometry), local_position)) {
            return elements::PointerCursor::Hand;
        }
    }
    if (contains_local_point(geometry.control_rect, local_position)) {
        return elements::PointerCursor::IBeam;
    }
    return elements::PointerCursor::Default;
}

void Input::on_focus_changed(const elements::FocusChangeEvent& event) {
    if (event.focused) {
        committed_text_ = text_storage();
        restart_caret_blink();
        ensure_caret_visible();
        if (event_state_ != nullptr && !event_state_->focused.empty()) {
            event_state_->focused.emit();
        }
    } else {
        pointer_selecting_ = false;
        release_pointer_capture();
        dismiss_text_input_context_menu();
        composition_active_ = false;
        composition_deleted_selection_ = false;
        composition_text_.clear();
        emit_change_if_needed();
        if (event_state_ != nullptr && !event_state_->blurred.empty()) {
            event_state_->blurred.emit();
        }
    }
}

bool Input::on_animation_frame(animation::AnimationTimePoint now) {
    auto active = hover_progress_.tick(now);
    active = scrollbar_progress_.tick(now) || active;
    if (active) {
        invalidate_paint();
    }
    return active;
}

void Input::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    const auto style = resolved_style();
    const auto& semantic = style.semantic;
    const auto geometry = create_geometry(absolute_frame);
    const auto invalid = status_ == InputStatus::Error ||
                         (max_length_ && show_word_limit_ && text_length() > *max_length_);
    const auto background = disabled_ || read_only_ ? style.read_only_background : style.background;
    const auto status_border = [&]() noexcept {
        switch (status_) {
        case InputStatus::Success:
            return semantic.success;
        case InputStatus::Warning:
            return semantic.warning;
        case InputStatus::Error:
            return semantic.danger;
        case InputStatus::Default:
            return style.border_color;
        }
        return style.border_color;
    }();
    const auto hover_border = animation::interpolate_value(
        style.border_color, semantic.hover_border, animated_hover_progress());
    const auto border = invalid                           ? semantic.danger
                        : status_ != InputStatus::Default ? status_border
                        : focused()                       ? style.focus_border_color
                                                          : hover_border;

    if (geometry.has_prepend) {
        context.fill_rect(geometry.prepend_rect, semantic.surface_subtle);
        context.stroke_rect(geometry.prepend_rect, style.border_color, style.border_width);
        context.draw_text(
            prepend_text_, geometry.prepend_rect,
            rendering::TextStyle{.font_size = style.font_size,
                                 .color = semantic.secondary_text,
                                 .alignment = rendering::TextAlignment::Center,
                                 .vertical_alignment = rendering::TextVerticalAlignment::Center});
    }

    winelement::style::paint_rectangle(
        context, geometry.control_rect,
        winelement::style::rectangle_style_from(style, background, border));

    if (geometry.has_append) {
        context.fill_rect(geometry.append_rect, semantic.surface_subtle);
        context.stroke_rect(geometry.append_rect, style.border_color, style.border_width);
        context.draw_text(
            append_text_, geometry.append_rect,
            rendering::TextStyle{.font_size = style.font_size,
                                 .color = semantic.secondary_text,
                                 .alignment = rendering::TextAlignment::Center,
                                 .vertical_alignment = rendering::TextVerticalAlignment::Center});
    }

    const auto affix_text_style =
        rendering::TextStyle{.font_size = style.font_size,
                             .color = disabled_ ? semantic.disabled_text : semantic.secondary_text,
                             .alignment = rendering::TextAlignment::Center,
                             .vertical_alignment = rendering::TextVerticalAlignment::Center};
    if (geometry.has_prefix) {
        context.draw_text(prefix_text_, geometry.prefix_rect, affix_text_style);
    }
    if (geometry.has_suffix) {
        context.draw_text(suffix_text_, geometry.suffix_rect, affix_text_style);
    }

    const auto base_rendered_text = display_text();
    const auto rendered_text = display_text_for_rendering();
    const auto has_composition = has_active_composition(composition_active_);
    const auto& text_to_draw =
        rendered_text.empty() && !has_composition ? placeholder_ : rendered_text;
    auto text_layout = textarea() ? create_text_layout(text_to_draw, geometry.text_rect.width)
                                  : create_text_layout(text_to_draw);
    text_layout.style.color = disabled_ ? semantic.disabled_text
                              : base_rendered_text.empty() && !has_composition
                                  ? style.placeholder_color
                                  : style.text_color;
    const auto origin =
        layout::Point{text_origin_x(geometry, text_layout), text_origin_y(geometry, text_layout)};

    context.save();
    context.push_clip(geometry.text_rect);
    if (focused() && has_selection()) {
        const auto [start, end] =
            ordered_range(selection_anchor_byte_offset_, selection_active_byte_offset_);
        const auto display_start = display_offset_for_text_offset(start);
        const auto display_end = display_offset_for_text_offset(end);
        for (auto rect : text_engine().selection_rects(
                 text_layout, rendering::TextSelectionRange{display_start, display_end})) {
            rect.x += origin.x;
            rect.y += origin.y;
            context.fill_rect(rect, style.hover_background);
        }
    }
    context.draw_text_layout(text_layout, origin);

    const auto caret_rect = caret_rect_in_content_space(geometry, text_layout);

    if (focused() && editable() && caret_blink_visible()) {
        context.fill_pixel_snapped_rect(layout::Rect{origin.x + caret_rect.x,
                                                     origin.y + caret_rect.y, caret_rect.width,
                                                     caret_rect.height},
                                        style.caret_color);
    }
    context.pop_clip();
    context.restore();

    if (textarea() && vertical_scrollbar_visible_for(geometry, text_layout)) {
        const auto track = vertical_scrollbar_track_rect(geometry);
        const auto thumb = vertical_scrollbar_thumb_rect(geometry, text_layout);
        const auto scrollbar_progress = animated_scrollbar_progress();
        context.fill_rect(track, rendering::Color::rgba(245, 247, 250,
                                                        static_cast<std::uint8_t>(std::round(
                                                            120.0F + 60.0F * scrollbar_progress))));
        context.fill_rounded_rect(
            thumb, rendering::CornerRadius::uniform(3.0F),
            animation::interpolate_value(rendering::Color::rgba(192, 196, 204, 180),
                                         style.focus_border_color, scrollbar_progress));
    }

    if (geometry.has_clear_button) {
        const auto clear_color = animation::interpolate_value(
            semantic.secondary_text, style.focus_border_color, animated_hover_progress());
        clear_icon_.paint_icon(
            context, centered_square_rect(geometry.clear_button_rect, input_icon_draw_size),
            clear_color);
    }
    if (geometry.has_password_button) {
        const auto& icon = password_visible_ ? password_visible_icon_ : password_hidden_icon_;
        const auto password_color = animation::interpolate_value(
            semantic.secondary_text, style.focus_border_color, animated_hover_progress());
        icon.paint_icon(context,
                        centered_square_rect(geometry.password_button_rect, input_icon_draw_size),
                        password_color);
    }
    if ((geometry.has_inside_word_limit ||
         word_limit_position_ == InputWordLimitPosition::Outside) &&
        geometry.word_limit_rect.width > 0.0F && geometry.word_limit_rect.height > 0.0F &&
        show_word_limit_ && max_length_ && !password()) {
        context.draw_text(
            word_limit_text(), geometry.word_limit_rect,
            rendering::TextStyle{.font_size = 12.0F,
                                 .color = invalid ? semantic.danger : semantic.secondary_text,
                                 .alignment = rendering::TextAlignment::End,
                                 .vertical_alignment = rendering::TextVerticalAlignment::Center});
    }
}

void Input::on_paint_overlay(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    static_cast<void>(absolute_frame);
    static_cast<void>(context);
}

void Input::update_measure_callback() {
    set_measure_callback([this](const layout::MeasureInput& input) {
        const auto style = resolved_style();
        const auto rendered_text = display_text_for_rendering();
        const auto has_composition = has_active_composition(composition_active_);
        const auto content =
            rendered_text.empty()
                ? has_composition ? std::string_view{} : std::string_view(placeholder_)
                : std::string_view(rendered_text);
        const auto has_outside_word_limit =
            show_word_limit_ && max_length_ &&
            word_limit_position_ == InputWordLimitPosition::Outside && !password();
        const auto outside_height = has_outside_word_limit ? outside_word_limit_height : 0.0F;

        if (textarea()) {
            auto width = style.min_width;
            if (input.width_mode != layout::MeasureMode::Undefined &&
                input.available_width > 0.0F) {
                width = input.available_width;
            }
            const auto content_width =
                std::max(width - style.padding.left - style.padding.right, 1.0F);
            const auto layout = create_text_layout(content, content_width);
            auto desired_rows = rows_;
            if (autosize_) {
                desired_rows =
                    std::max(autosize_min_rows_,
                             std::max(newline_count(text_storage()) + 1U, layout.lines.size()));
                if (autosize_max_rows_ > 0U) {
                    desired_rows = std::min(desired_rows, autosize_max_rows_);
                }
            }
            const auto height = std::max(row_height() * static_cast<float>(desired_rows) +
                                             style.padding.top + style.padding.bottom,
                                         style.min_height) +
                                outside_height;
            return layout::Size{std::max(width, style.min_width), height};
        }

        const auto text_size = text_engine().measure_single_line(
            content, rendering::TextStyle{.font_size = style.font_size,
                                          .color = style.text_color,
                                          .alignment = rendering::TextAlignment::Start});
        auto width =
            std::max(text_size.width + style.padding.left + style.padding.right, style.min_width);
        if (!prefix_text_.empty()) {
            width += measure_affix_text(prefix_text_).width + default_affix_gap;
        }
        if (!suffix_text_.empty()) {
            width += measure_affix_text(suffix_text_).width + default_affix_gap;
        }
        if (clearable_) {
            width += default_icon_extent;
        }
        if (show_password_toggle_ && password()) {
            width += default_icon_extent;
        }
        if (show_word_limit_ && max_length_ && !password() &&
            word_limit_position_ == InputWordLimitPosition::Inside) {
            width += measure_affix_text(word_limit_text()).width + default_affix_gap;
        }
        if (!prepend_text_.empty()) {
            width +=
                measure_affix_text(prepend_text_).width + group_segment_horizontal_padding * 2.0F;
        }
        if (!append_text_.empty()) {
            width +=
                measure_affix_text(append_text_).width + group_segment_horizontal_padding * 2.0F;
        }
        if (input.width_mode == layout::MeasureMode::Exactly && input.available_width > 0.0F) {
            width = input.available_width;
        } else if (input.width_mode == layout::MeasureMode::AtMost &&
                   input.available_width > 0.0F) {
            width = std::min(width, input.available_width);
        }
        return layout::Size{std::max(width, style.min_width), style.min_height + outside_height};
    });
}

void Input::restart_caret_blink() {
    caret_blink_epoch_ = std::chrono::steady_clock::now();
}

void Input::ensure_caret_visible() {
    if (frame().width <= 0.0F || frame().height <= 0.0F) {
        horizontal_scroll_x_ = 0.0F;
        vertical_scroll_y_ = 0.0F;
        return;
    }

    const auto geometry = create_geometry(layout::Rect{0.0F, 0.0F, frame().width, frame().height});
    if (geometry.text_rect.height <= 0.0F || geometry.text_rect.width <= 0.0F) {
        horizontal_scroll_x_ = 0.0F;
        vertical_scroll_y_ = 0.0F;
        return;
    }

    const auto rendered_text = display_text_for_rendering();
    const auto text_layout = textarea()
                                 ? create_text_layout(rendered_text, geometry.text_rect.width)
                                 : create_text_layout(rendered_text);
    const auto caret_rect = caret_rect_in_content_space(geometry, text_layout);
    if (textarea()) {
        horizontal_scroll_x_ = 0.0F;
        const auto max_scroll = max_vertical_scroll_y(text_layout, geometry);
        const auto caret_top = caret_rect.y;
        const auto caret_bottom = caret_rect.y + caret_rect.height;
        const auto view_top = vertical_scroll_y_;
        const auto view_bottom = view_top + geometry.text_rect.height;
        auto next_scroll = vertical_scroll_y_;
        if (caret_bottom > view_bottom) {
            next_scroll = caret_bottom - geometry.text_rect.height;
        } else if (caret_top < view_top) {
            next_scroll = caret_top;
        }
        vertical_scroll_y_ = std::clamp(next_scroll, 0.0F, max_scroll);
        return;
    }

    vertical_scroll_y_ = 0.0F;
    const auto max_scroll = max_horizontal_scroll_x(text_layout, geometry);
    const auto caret_left = caret_rect.x;
    const auto caret_right = caret_rect.x + caret_rect.width;
    const auto view_left = horizontal_scroll_x_;
    const auto view_right = view_left + geometry.text_rect.width;
    auto next_scroll = horizontal_scroll_x_;
    if (caret_right > view_right) {
        next_scroll = caret_right - geometry.text_rect.width;
    } else if (caret_left < view_left) {
        next_scroll = caret_left;
    }
    horizontal_scroll_x_ = std::clamp(next_scroll, 0.0F, max_scroll);
}

bool Input::scroll_textarea_by(float delta_y) {
    if (!textarea() || frame().width <= 0.0F || frame().height <= 0.0F) {
        return false;
    }

    const auto geometry = create_geometry(layout::Rect{0.0F, 0.0F, frame().width, frame().height});
    if (geometry.text_rect.height <= 0.0F) {
        return false;
    }

    const auto text_layout =
        create_text_layout(display_text_for_rendering(), geometry.text_rect.width);
    const auto max_scroll = max_vertical_scroll_y(text_layout, geometry);
    if (max_scroll <= 0.0F) {
        vertical_scroll_y_ = 0.0F;
        return false;
    }

    const auto next_scroll = std::clamp(vertical_scroll_y_ + delta_y, 0.0F, max_scroll);
    if (std::abs(next_scroll - vertical_scroll_y_) < 0.01F) {
        return false;
    }

    vertical_scroll_y_ = next_scroll;
    invalidate_paint();
    return true;
}

void Input::scroll_textarea_for_pointer(layout::Point local_position, const Geometry& geometry) {
    if (!textarea() || !pointer_selecting_) {
        return;
    }

    const auto top = geometry.text_rect.y;
    const auto bottom = geometry.text_rect.y + geometry.text_rect.height;
    auto delta_y = 0.0F;
    if (local_position.y < top) {
        delta_y = local_position.y - top;
    } else if (local_position.y > bottom) {
        delta_y = local_position.y - bottom;
    }
    if (std::abs(delta_y) < 0.01F) {
        return;
    }

    const auto direction = delta_y < 0.0F ? -1.0F : 1.0F;
    scroll_textarea_by(direction * std::max(std::abs(delta_y), row_height()));
}

void Input::update_pointer_selection(layout::Point local_position) {
    const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    auto geometry = create_geometry(local_frame);
    scroll_textarea_for_pointer(local_position, geometry);
    geometry = create_geometry(local_frame);
    const auto rendered_text = display_text();
    auto text_layout = textarea() ? create_text_layout(rendered_text, geometry.text_rect.width)
                                  : create_text_layout(rendered_text);
    const auto point = layout::Point{local_position.x - text_origin_x(geometry, text_layout),
                                     local_position.y - text_origin_y(geometry, text_layout)};
    const auto hit_offset = textarea() ? text_engine().hit_test_byte_offset(text_layout, point)
                                       : text_engine().hit_test_byte_offset(text_layout, point.x);
    const auto text_offset = text_offset_for_display_offset(hit_offset);
    if (!pointer_selecting_) {
        set_caret_byte_offset(text_offset);
        pointer_selection_anchor_byte_offset_ = caret_byte_offset_;
        return;
    }

    set_selection(pointer_selection_anchor_byte_offset_, text_offset);
}

void Input::select_word_at(layout::Point local_position) {
    update_pointer_selection(local_position);
    auto offset = caret_byte_offset_;
    if (offset > 0U && (offset == text_storage().size() || !is_word_byte(text_storage(), offset))) {
        offset = rendering::previous_utf8_boundary(text_storage(), offset);
    }
    if (!is_word_byte(text_storage(), offset)) {
        set_selection(caret_byte_offset_, caret_byte_offset_);
        return;
    }

    auto start = offset;
    while (start > 0U) {
        const auto previous = rendering::previous_utf8_boundary(text_storage(), start);
        if (!is_word_byte(text_storage(), previous) ||
            is_line_break_byte(text_storage(), previous)) {
            break;
        }
        start = previous;
    }

    auto end = rendering::next_utf8_boundary(text_storage(), offset);
    while (end < text_storage().size()) {
        if (!is_word_byte(text_storage(), end) || is_line_break_byte(text_storage(), end)) {
            break;
        }
        end = rendering::next_utf8_boundary(text_storage(), end);
    }

    set_selection(start, end);
    pointer_selection_anchor_byte_offset_ = start;
    caret_byte_offset_ = end;
    restart_caret_blink();
    ensure_caret_visible();
}

void Input::select_line_at(layout::Point local_position) {
    update_pointer_selection(local_position);
    auto start = caret_byte_offset_;
    while (start > 0U && text_storage()[start - 1U] != '\n') {
        start = rendering::previous_utf8_boundary(text_storage(), start);
    }

    auto end = caret_byte_offset_;
    while (end < text_storage().size() && text_storage()[end] != '\n') {
        end = rendering::next_utf8_boundary(text_storage(), end);
    }

    set_selection(start, end);
    pointer_selection_anchor_byte_offset_ = start;
    caret_byte_offset_ = end;
    restart_caret_blink();
    ensure_caret_visible();
}

std::optional<elements::TextInputEditCommand>
Input::context_menu_command_for_id(std::string_view id) const noexcept {
    if (id == "cut") {
        return elements::TextInputEditCommand::Cut;
    }
    if (id == "copy") {
        return elements::TextInputEditCommand::Copy;
    }
    if (id == "paste") {
        return elements::TextInputEditCommand::Paste;
    }
    if (id == "select_all") {
        return elements::TextInputEditCommand::SelectAll;
    }
    return std::nullopt;
}

void Input::move_caret_visually_left() {
    const auto text_layout = create_text_layout(display_text());
    const auto current_display_offset = display_offset_for_text_offset(caret_byte_offset_);
    const auto next_display_offset =
        password() && !password_visible_
            ? text_engine().previous_cluster_boundary(text_layout, current_display_offset)
            : text_engine().caret_offset_for_visual_left(text_layout, current_display_offset);
    set_caret_byte_offset(text_offset_for_display_offset(next_display_offset));
}

void Input::move_caret_visually_right() {
    const auto text_layout = create_text_layout(display_text());
    const auto current_display_offset = display_offset_for_text_offset(caret_byte_offset_);
    const auto next_display_offset =
        password() && !password_visible_
            ? text_engine().next_cluster_boundary(text_layout, current_display_offset)
            : text_engine().caret_offset_for_visual_right(text_layout, current_display_offset);
    set_caret_byte_offset(text_offset_for_display_offset(next_display_offset));
}

void Input::move_caret_visually_up() {
    if (!textarea()) {
        set_caret_byte_offset(0U);
        return;
    }

    const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    const auto geometry = create_geometry(local_frame);
    const auto text_layout =
        create_text_layout(display_text_for_rendering(), geometry.text_rect.width);
    const auto current_display_offset = display_offset_for_text_offset(caret_byte_offset_);
    const auto metrics =
        text_engine().caret_metrics_for_byte_offset(text_layout, current_display_offset);
    if (metrics.line_index == 0U || text_layout.lines.empty()) {
        set_caret_byte_offset(0U);
        return;
    }

    const auto target_line = text_layout.lines[metrics.line_index - 1U];
    const auto point =
        layout::Point{metrics.rect.x, target_line.rect.y + target_line.rect.height * 0.5F};
    const auto next_display_offset = text_engine().hit_test_byte_offset(text_layout, point);
    set_caret_byte_offset(text_offset_for_display_offset(next_display_offset));
}

void Input::move_caret_visually_down() {
    if (!textarea()) {
        set_caret_byte_offset(text_storage().size());
        return;
    }

    const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    const auto geometry = create_geometry(local_frame);
    const auto text_layout =
        create_text_layout(display_text_for_rendering(), geometry.text_rect.width);
    const auto current_display_offset = display_offset_for_text_offset(caret_byte_offset_);
    const auto metrics =
        text_engine().caret_metrics_for_byte_offset(text_layout, current_display_offset);
    if (text_layout.lines.empty() || metrics.line_index + 1U >= text_layout.lines.size()) {
        set_caret_byte_offset(text_storage().size());
        return;
    }

    const auto target_line = text_layout.lines[metrics.line_index + 1U];
    const auto point =
        layout::Point{metrics.rect.x, target_line.rect.y + target_line.rect.height * 0.5F};
    const auto next_display_offset = text_engine().hit_test_byte_offset(text_layout, point);
    set_caret_byte_offset(text_offset_for_display_offset(next_display_offset));
}

void Input::delete_selection() {
    if (!has_selection()) {
        return;
    }

    const auto [start, end] =
        ordered_range(selection_anchor_byte_offset_, selection_active_byte_offset_);
    text_storage().erase(start, end - start);
    caret_byte_offset_ = start;
    selection_anchor_byte_offset_ = caret_byte_offset_;
    selection_active_byte_offset_ = caret_byte_offset_;
    mark_text_transform_generation_changed();
    restart_caret_blink();
    ensure_caret_visible();
    mark_measure_dirty();
    invalidate_paint();
}

void Input::insert_text_at_caret(std::string_view text) {
    replace_selection_with_text(text);
}

void Input::replace_selection_with_text(std::string_view text, bool record_undo) {
    if (!editable()) {
        return;
    }

    const auto insert_text = sanitized_input(text);
    if (insert_text.empty()) {
        return;
    }

    const auto [start, end] =
        ordered_range(selection_anchor_byte_offset_, selection_active_byte_offset_);
    auto next_text = text_storage();
    next_text.erase(start, end - start);
    next_text.insert(start, insert_text);
    if (record_undo) {
        push_undo_state();
    }
    apply_user_text(std::move(next_text), start + insert_text.size());
}

void Input::apply_user_text(std::string text, std::size_t preferred_caret_offset) {
    auto next_text = truncated_to_max_length(parsed_text(text));
    const auto next_caret = rendering::clamp_utf8_boundary(
        next_text, std::min(preferred_caret_offset, next_text.size()));
    if (text_storage() == next_text && caret_byte_offset_ == next_caret && !has_selection()) {
        return;
    }

    text_storage() = std::move(next_text);
    caret_byte_offset_ = next_caret;
    selection_anchor_byte_offset_ = caret_byte_offset_;
    selection_active_byte_offset_ = caret_byte_offset_;
    mark_text_transform_generation_changed();
    restart_caret_blink();
    ensure_caret_visible();
    mark_measure_dirty();
    invalidate_paint();
    emit_input();
}

void Input::push_undo_state() {
    const auto state = current_undo_state();
    const auto same_as_last =
        !undo_stack_.empty() && undo_stack_.back().text == state.text &&
        undo_stack_.back().caret_byte_offset == state.caret_byte_offset &&
        undo_stack_.back().selection_anchor_byte_offset == state.selection_anchor_byte_offset &&
        undo_stack_.back().selection_active_byte_offset == state.selection_active_byte_offset;
    if (!same_as_last) {
        undo_stack_.push_back(state);
        if (undo_stack_.size() > 64U) {
            undo_stack_.erase(undo_stack_.begin());
        }
    }
    redo_stack_.clear();
}

void Input::undo() {
    if (undo_stack_.empty()) {
        return;
    }

    redo_stack_.push_back(current_undo_state());
    restore_undo_state(undo_stack_.back());
    undo_stack_.pop_back();
    emit_input();
}

void Input::redo() {
    if (redo_stack_.empty()) {
        return;
    }

    undo_stack_.push_back(current_undo_state());
    restore_undo_state(redo_stack_.back());
    redo_stack_.pop_back();
    emit_input();
}

void Input::restore_undo_state(const UndoState& state) {
    text_storage() = state.text;
    caret_byte_offset_ = rendering::clamp_utf8_boundary(
        text_storage(), std::min(state.caret_byte_offset, text_storage().size()));
    selection_anchor_byte_offset_ = rendering::clamp_utf8_boundary(
        text_storage(), std::min(state.selection_anchor_byte_offset, text_storage().size()));
    selection_active_byte_offset_ = rendering::clamp_utf8_boundary(
        text_storage(), std::min(state.selection_active_byte_offset, text_storage().size()));
    mark_text_transform_generation_changed();
    restart_caret_blink();
    ensure_caret_visible();
    mark_measure_dirty();
    invalidate_paint();
}

Input::UndoState Input::current_undo_state() const {
    return UndoState{.text = text_storage(),
                     .caret_byte_offset = caret_byte_offset_,
                     .selection_anchor_byte_offset = selection_anchor_byte_offset_,
                     .selection_active_byte_offset = selection_active_byte_offset_};
}

void Input::copy_selection_to_clipboard() const {
    text_clipboard_service().copy_text(selected_text());
}

void Input::paste_from_clipboard() {
    if (const auto clipboard_text = text_clipboard_service().text(); !clipboard_text.empty()) {
        replace_selection_with_text(clipboard_text);
    }
}

void Input::emit_input() {
    if (event_state_ != nullptr && !event_state_->input_changed.empty()) {
        event_state_->input_changed.emit(text_storage());
    }
}

void Input::emit_change_if_needed() {
    if (committed_text_ == text_storage()) {
        return;
    }

    committed_text_ = text_storage();
    if (event_state_ != nullptr && !event_state_->change_committed.empty()) {
        event_state_->change_committed.emit(text_storage());
    }
}

std::string Input::selected_text() const {
    if (!has_selection() || (password() && !password_visible_)) {
        return {};
    }

    const auto [start, end] =
        ordered_range(selection_anchor_byte_offset_, selection_active_byte_offset_);
    return text_storage().substr(start, end - start);
}

std::string Input::sanitized_input(std::string_view text) const {
    return textarea() ? normalize_textarea_breaks(text) : strip_single_line_breaks(text);
}

std::string Input::parsed_text(std::string_view text) const {
    if (parser_ && !textarea() && !password()) {
        return parser_(text);
    }
    return std::string(text);
}

std::string Input::truncated_to_max_length(std::string_view text) const {
    if (!max_length_) {
        return std::string(text);
    }
    if (count_text_units(text) <= *max_length_) {
        return std::string(text);
    }

    if (count_graphemes_) {
        const auto style = resolved_style();
        const auto layout = text_engine().shape_single_line(
            text, rendering::TextStyle{.font_size = style.font_size, .color = style.text_color});
        std::vector<rendering::TextCluster> clusters = layout.clusters;
        std::sort(clusters.begin(), clusters.end(), [](const auto& left, const auto& right) {
            return left.byte_offset < right.byte_offset;
        });
        auto count = std::size_t{0};
        auto end_offset = std::size_t{0};
        for (const auto& cluster : clusters) {
            if (cluster.byte_length == 0U || cluster.is_newline) {
                continue;
            }
            if (count >= *max_length_) {
                break;
            }
            ++count;
            end_offset = cluster.byte_offset + cluster.byte_length;
        }
        return std::string(text.substr(0U, end_offset));
    }

    return truncate_by_code_points(text, *max_length_);
}

std::string Input::display_text() const {
    if (password() && !password_visible_) {
        return std::string(rendering::utf8_code_point_count(text_storage()), '*');
    }
    if (formatter_ && !textarea() && !password()) {
        return formatter_(text_storage());
    }
    return text_storage();
}

std::string Input::display_text_for_rendering() const {
    auto rendered_text = display_text();
    if (!has_visible_composition_text(composition_active_, composition_text_)) {
        return rendered_text;
    }

    const auto insert_offset = rendering::clamp_utf8_boundary(
        rendered_text,
        std::min(display_offset_for_text_offset(caret_byte_offset_), rendered_text.size()));
    rendered_text.insert(insert_offset, composition_text_);
    return rendered_text;
}

std::size_t Input::display_caret_offset_for_rendering() const {
    const auto display_offset = display_offset_for_text_offset(caret_byte_offset_);
    if (!has_visible_composition_text(composition_active_, composition_text_)) {
        return display_offset;
    }

    const auto rendered_text = display_text();
    const auto insert_offset = rendering::clamp_utf8_boundary(
        rendered_text, std::min(display_offset, rendered_text.size()));
    return insert_offset + composition_text_.size();
}

std::string Input::word_limit_text() const {
    if (!max_length_) {
        return std::to_string(text_length());
    }
    return std::to_string(text_length()) + " / " + std::to_string(*max_length_);
}

std::size_t Input::display_offset_for_text_offset(std::size_t byte_offset) const {
    const auto clamped_text_offset = rendering::clamp_utf8_boundary(
        text_storage(), std::min(byte_offset, text_storage().size()));
    if (password() && !password_visible_) {
        return rendering::utf8_code_point_count(
            std::string_view(text_storage()).substr(0U, clamped_text_offset));
    }

    const auto rendered_text = display_text();
    if (formatter_ && !textarea() && !password() && rendered_text != text_storage()) {
        const auto& mapping = display_text_offset_mapping(rendered_text);
        return mapped_display_offset_for_text_offset(text_storage(), mapping, clamped_text_offset);
    }

    return rendering::clamp_utf8_boundary(rendered_text, clamped_text_offset);
}

std::size_t Input::text_offset_for_display_offset(std::size_t display_offset) const {
    if (password() && !password_visible_) {
        return text_offset_for_display_code_point_offset(text_storage(), display_offset);
    }

    const auto rendered_text = display_text();
    if (formatter_ && !textarea() && !password() && rendered_text != text_storage()) {
        const auto& mapping = display_text_offset_mapping(rendered_text);
        return mapped_text_offset_for_display_offset(rendered_text, mapping, display_offset);
    }

    return rendering::clamp_utf8_boundary(text_storage(),
                                          std::min(display_offset, text_storage().size()));
}

const InputDisplayTextOffsetMapping&
Input::display_text_offset_mapping(std::string_view rendered_text) const {
    if (!display_offset_mapping_cache_.valid ||
        display_offset_mapping_cache_.generation != text_transform_generation_ ||
        display_offset_mapping_cache_.source_text != text_storage() ||
        display_offset_mapping_cache_.rendered_text != rendered_text) {
        display_offset_mapping_cache_.source_text = text_storage();
        display_offset_mapping_cache_.rendered_text = std::string(rendered_text);
        display_offset_mapping_cache_.generation = text_transform_generation_;
        display_offset_mapping_cache_.mapping = build_display_offset_mapping(
            text_storage(), rendered_text, parser_ ? &parser_ : nullptr);
        display_offset_mapping_cache_.valid = true;
    }
    return display_offset_mapping_cache_.mapping;
}

void Input::invalidate_display_text_offset_mapping() const noexcept {
    display_offset_mapping_cache_.valid = false;
}

rendering::TextLayout Input::create_text_layout(std::string_view text, float max_width) const {
    const auto style = resolved_style();
    auto text_style = rendering::TextStyle{
        .font_size = style.font_size,
        .color = style.text_color,
        .alignment = rendering::TextAlignment::Start,
        .vertical_alignment = rendering::TextVerticalAlignment::Top,
        .wrapping = textarea() ? rendering::TextWrapping::Wrap : rendering::TextWrapping::NoWrap};
    if (max_width > 0.0F) {
        return text_engine().layout_text(text, text_style,
                                         rendering::TextLayoutOptions{.max_width = max_width});
    }
    return text_engine().layout_text(text, text_style);
}

style::UIElementStyle Input::resolved_style() const noexcept {
    auto style = style_storage();
    if (textarea()) {
        style.min_height = std::max(style.min_height, row_height() * static_cast<float>(rows_) +
                                                          style.padding.top + style.padding.bottom);
        return style;
    }

    switch (size_) {
    case InputSize::Large:
        style.padding = layout::EdgeInsets{16.0F, 7.0F, 16.0F, 7.0F};
        style.min_height = 40.0F;
        break;
    case InputSize::Small:
        style.padding = layout::EdgeInsets{8.0F, 3.0F, 8.0F, 3.0F};
        style.font_size = 12.0F;
        style.min_height = 24.0F;
        break;
    case InputSize::Default:
    default:
        break;
    }
    return style;
}

float Input::animated_hover_progress() const {
    return std::clamp(hover_progress_.value(), 0.0F, 1.0F);
}

float Input::animated_scrollbar_progress() const {
    return std::clamp(scrollbar_progress_.value(), 0.0F, 1.0F);
}

void Input::animate_hover(float target) {
    hover_progress_.animate_to(target);
}

void Input::animate_scrollbar(float target) {
    scrollbar_progress_.animate_to(target, animation::AnimationDuration{0.1F});
}

Input::Geometry Input::create_geometry(layout::Rect frame) const {
    const auto style = resolved_style();
    Geometry geometry;
    geometry.frame = frame;
    const auto outside_word_limit = show_word_limit_ && max_length_ && !password() &&
                                    word_limit_position_ == InputWordLimitPosition::Outside;
    const auto input_height =
        clamp_non_negative(frame.height - (outside_word_limit ? outside_word_limit_height : 0.0F));
    const auto input_frame = layout::Rect{frame.x, frame.y, frame.width, input_height};
    geometry.word_limit_rect = layout::Rect{frame.x, frame.y + input_height, frame.width,
                                            outside_word_limit ? outside_word_limit_height : 0.0F};

    geometry.has_prepend = can_show_inline_affixes() && !prepend_text_.empty();
    geometry.has_append = can_show_inline_affixes() && !append_text_.empty();
    const auto prepend_width = geometry.has_prepend ? measure_affix_text(prepend_text_).width +
                                                          group_segment_horizontal_padding * 2.0F
                                                    : 0.0F;
    const auto append_width = geometry.has_append ? measure_affix_text(append_text_).width +
                                                        group_segment_horizontal_padding * 2.0F
                                                  : 0.0F;
    geometry.prepend_rect =
        layout::Rect{input_frame.x, input_frame.y, std::min(prepend_width, input_frame.width),
                     input_frame.height};
    geometry.append_rect =
        layout::Rect{input_frame.x + std::max(input_frame.width - append_width, 0.0F),
                     input_frame.y, std::min(append_width, input_frame.width), input_frame.height};
    geometry.control_rect =
        layout::Rect{input_frame.x + geometry.prepend_rect.width, input_frame.y,
                     clamp_non_negative(input_frame.width - geometry.prepend_rect.width -
                                        geometry.append_rect.width),
                     input_frame.height};
    geometry.text_rect = inset_rect(geometry.control_rect, style.padding);

    geometry.has_prefix = can_show_inline_affixes() && !prefix_text_.empty();
    if (geometry.has_prefix) {
        const auto prefix_width = measure_affix_text(prefix_text_).width + default_affix_gap;
        geometry.prefix_rect = layout::Rect{geometry.text_rect.x, geometry.text_rect.y,
                                            std::min(prefix_width, geometry.text_rect.width),
                                            geometry.text_rect.height};
        geometry.text_rect.x += geometry.prefix_rect.width;
        geometry.text_rect.width =
            clamp_non_negative(geometry.text_rect.width - geometry.prefix_rect.width);
    }

    geometry.has_clear_button = can_show_clear_button();
    geometry.has_password_button = can_show_password_button();
    geometry.has_inside_word_limit = show_word_limit_ && max_length_ && !password() &&
                                     word_limit_position_ == InputWordLimitPosition::Inside;
    auto suffix_reserved_width = 0.0F;
    if (!suffix_text_.empty() && can_show_inline_affixes()) {
        suffix_reserved_width += measure_affix_text(suffix_text_).width + default_affix_gap;
    }
    if (geometry.has_clear_button) {
        suffix_reserved_width += default_icon_extent;
    }
    if (geometry.has_password_button) {
        suffix_reserved_width += default_icon_extent;
    }
    if (geometry.has_inside_word_limit) {
        suffix_reserved_width += measure_affix_text(word_limit_text()).width + default_affix_gap;
    }

    auto suffix_x =
        geometry.text_rect.x + std::max(geometry.text_rect.width - suffix_reserved_width, 0.0F);
    geometry.text_rect.width = clamp_non_negative(geometry.text_rect.width - suffix_reserved_width);
    geometry.has_suffix = can_show_inline_affixes() && !suffix_text_.empty();
    if (geometry.has_suffix) {
        const auto width = measure_affix_text(suffix_text_).width + default_affix_gap;
        geometry.suffix_rect =
            layout::Rect{suffix_x, geometry.text_rect.y, width, geometry.text_rect.height};
        suffix_x += width;
    }
    if (geometry.has_clear_button) {
        geometry.clear_button_rect = layout::Rect{
            suffix_x, geometry.control_rect.y, default_icon_extent, geometry.control_rect.height};
        suffix_x += default_icon_extent;
    }
    if (geometry.has_password_button) {
        geometry.password_button_rect = layout::Rect{
            suffix_x, geometry.control_rect.y, default_icon_extent, geometry.control_rect.height};
        suffix_x += default_icon_extent;
    }
    if (geometry.has_inside_word_limit) {
        geometry.word_limit_rect =
            layout::Rect{suffix_x, geometry.text_rect.y,
                         measure_affix_text(word_limit_text()).width + default_affix_gap,
                         geometry.text_rect.height};
    }

    return geometry;
}

layout::Size Input::measure_affix_text(std::string_view text) const {
    if (text.empty()) {
        return {};
    }
    const auto style = resolved_style();
    return text_engine().measure_single_line(
        text, rendering::TextStyle{.font_size = style.font_size,
                                   .color = style.semantic.secondary_text,
                                   .alignment = rendering::TextAlignment::Start});
}

std::size_t Input::count_text_units(std::string_view text) const {
    if (count_graphemes_handler_) {
        return count_graphemes_handler_(text);
    }
    if (count_graphemes_) {
        if (text.empty()) {
            return 0U;
        }
        const auto style = resolved_style();
        const auto layout = text_engine().shape_single_line(
            text, rendering::TextStyle{.font_size = style.font_size, .color = style.text_color});
        auto count = std::size_t{0};
        for (const auto& cluster : layout.clusters) {
            if (cluster.byte_length > 0U && !cluster.is_newline) {
                ++count;
            }
        }
        return count;
    }
    return rendering::utf8_code_point_count(text);
}

float Input::row_height() const {
    return std::max(style_storage().font_size * 1.5F, style_storage().font_size + 4.0F);
}

void Input::initialize_icons() {
    clear_icon_.set_icon_paths(elements::icons::CircleClose);
    password_visible_icon_.set_icon_paths(elements::icons::View);
    password_hidden_icon_.set_icon_paths(elements::icons::Hide);
}

layout::Rect Input::caret_rect_in_content_space(const Geometry& geometry,
                                                const rendering::TextLayout& text_layout) const {
    static_cast<void>(geometry);
    const auto style = resolved_style();
    const auto caret_metrics = text_engine().caret_metrics_for_byte_offset(
        text_layout, display_caret_offset_for_rendering());
    return layout::Rect{caret_metrics.rect.x, caret_metrics.rect.y, style.caret_width,
                        caret_metrics.rect.height};
}

float Input::max_horizontal_scroll_x(const rendering::TextLayout& text_layout,
                                     const Geometry& geometry) const {
    if (textarea()) {
        return 0.0F;
    }
    const auto caret_rect = caret_rect_in_content_space(geometry, text_layout);
    const auto content_width = std::max(text_layout.size.width, caret_rect.x + caret_rect.width);
    return std::max(content_width - geometry.text_rect.width, 0.0F);
}

float Input::clamped_horizontal_scroll_x(const rendering::TextLayout& text_layout,
                                         const Geometry& geometry) const {
    if (textarea()) {
        return 0.0F;
    }
    return std::clamp(horizontal_scroll_x_, 0.0F, max_horizontal_scroll_x(text_layout, geometry));
}

float Input::max_vertical_scroll_y(const rendering::TextLayout& text_layout,
                                   const Geometry& geometry) const noexcept {
    return textarea() ? std::max(text_layout.size.height - geometry.text_rect.height, 0.0F) : 0.0F;
}

float Input::clamped_vertical_scroll_y(const rendering::TextLayout& text_layout,
                                       const Geometry& geometry) const noexcept {
    if (!textarea()) {
        return 0.0F;
    }
    const auto max_scroll = max_vertical_scroll_y(text_layout, geometry);
    return std::clamp(vertical_scroll_y_, 0.0F, max_scroll);
}

float Input::text_origin_x(const Geometry& geometry,
                           const rendering::TextLayout& text_layout) const {
    return textarea() ? geometry.text_rect.x
                      : geometry.text_rect.x - clamped_horizontal_scroll_x(text_layout, geometry);
}

float Input::text_origin_y(const Geometry& geometry,
                           const rendering::TextLayout& text_layout) const noexcept {
    if (textarea()) {
        return geometry.text_rect.y - clamped_vertical_scroll_y(text_layout, geometry);
    }
    if (password() && !password_visible_ && !text_storage().empty() && !text_layout.lines.empty()) {
        const auto& line = text_layout.lines.front();
        const auto visual_center_offset =
            std::max(text_layout.style.font_size * password_mask_visual_center_factor, 1.0F);
        const auto centered_origin = geometry.text_rect.y + geometry.text_rect.height * 0.5F -
                                     line.baseline + visual_center_offset;
        const auto max_origin =
            geometry.text_rect.y +
            std::max(geometry.text_rect.height - line.rect.height * 0.25F, 0.0F);
        return std::clamp(centered_origin, geometry.text_rect.y, max_origin);
    }
    return geometry.text_rect.y +
           std::max((geometry.text_rect.height - text_layout.size.height) * 0.5F, 0.0F);
}

bool Input::caret_blink_visible() const {
    const auto elapsed = std::chrono::steady_clock::now() - caret_blink_epoch_;
    const auto interval_count = elapsed / caret_blink_interval;
    return interval_count % 2 == 0;
}

layout::Rect Input::vertical_scrollbar_track_rect(const Geometry& geometry) const noexcept {
    constexpr auto scrollbar_width = 6.0F;
    constexpr auto scrollbar_inset = 2.0F;
    if (!textarea() || geometry.text_rect.height <= scrollbar_inset * 2.0F) {
        return {};
    }
    return layout::Rect{geometry.text_rect.x + geometry.text_rect.width - scrollbar_width,
                        geometry.text_rect.y + scrollbar_inset, scrollbar_width,
                        std::max(geometry.text_rect.height - scrollbar_inset * 2.0F, 0.0F)};
}

layout::Rect
Input::vertical_scrollbar_thumb_rect(const Geometry& geometry,
                                     const rendering::TextLayout& text_layout) const noexcept {
    const auto track = vertical_scrollbar_track_rect(geometry);
    const auto max_scroll = max_vertical_scroll_y(text_layout, geometry);
    if (track.height <= 0.0F || max_scroll <= 0.0F || text_layout.size.height <= 0.0F) {
        return {};
    }

    const auto viewport_ratio =
        std::clamp(geometry.text_rect.height / text_layout.size.height, 0.0F, 1.0F);
    const auto thumb_height =
        std::clamp(track.height * viewport_ratio, std::min(track.height, 24.0F), track.height);
    const auto travel = std::max(track.height - thumb_height, 0.0F);
    const auto scroll_ratio = clamped_vertical_scroll_y(text_layout, geometry) / max_scroll;
    return layout::Rect{track.x, track.y + travel * scroll_ratio, track.width, thumb_height};
}

bool Input::vertical_scrollbar_visible_for(
    const Geometry& geometry, const rendering::TextLayout& text_layout) const noexcept {
    return textarea() && geometry.text_rect.width > 0.0F && geometry.text_rect.height > 0.0F &&
           max_vertical_scroll_y(text_layout, geometry) > 0.5F;
}

bool Input::set_vertical_scroll_from_pointer(layout::Point local_position) {
    if (!textarea()) {
        return false;
    }
    const auto geometry = create_geometry(layout::Rect{0.0F, 0.0F, frame().width, frame().height});
    const auto text_layout = create_text_layout(display_text(), geometry.text_rect.width);
    const auto max_scroll = max_vertical_scroll_y(text_layout, geometry);
    const auto track = vertical_scrollbar_track_rect(geometry);
    if (max_scroll <= 0.0F || track.height <= 0.0F) {
        vertical_scroll_y_ = 0.0F;
        invalidate_paint();
        return false;
    }

    const auto thumb = vertical_scrollbar_thumb_rect(geometry, text_layout);
    const auto travel = std::max(track.height - thumb.height, 1.0F);
    const auto thumb_center_y =
        std::clamp(local_position.y - track.y - thumb.height * 0.5F, 0.0F, travel);
    const auto next_scroll = std::clamp(thumb_center_y / travel * max_scroll, 0.0F, max_scroll);
    if (std::abs(next_scroll - vertical_scroll_y_) < 0.01F) {
        return true;
    }
    vertical_scroll_y_ = next_scroll;
    invalidate_paint();
    return true;
}

bool Input::textarea() const noexcept {
    return type_ == InputType::Textarea;
}

bool Input::editable() const noexcept {
    return !disabled_ && !read_only_;
}

bool Input::can_show_inline_affixes() const noexcept {
    return !textarea();
}

bool Input::can_show_clear_button() const noexcept {
    return clearable_ && editable() && !text_storage().empty() && (focused() || hovered_);
}

bool Input::can_show_password_button() const noexcept {
    return show_password_toggle_ && password() && !disabled_ && !text_storage().empty();
}

void Input::mark_text_transform_generation_changed() noexcept {
    ++text_transform_generation_;
    invalidate_display_text_offset_mapping();
}

} // namespace winelement::controls
