#include <winelement/controls/select.hpp>

#include <winelement/controls/property_keys.hpp>

#include "control_style.hpp"
#include "popup_menu_surface.hpp"

#include <winelement/elements/all_icons.hpp>
#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>

namespace winelement::controls {

namespace {
constexpr auto arrow_width = 22.0F;
constexpr auto filter_height = 36.0F;
constexpr auto filter_text_padding = 8.0F;
constexpr auto select_icon_size = 14.0F;
constexpr auto tag_gap = 4.0F;
constexpr auto tag_horizontal_padding = 8.0F;
constexpr auto tag_close_slot_width = 16.0F;
constexpr auto tag_close_icon_size = 10.0F;
constexpr auto tag_close_hit_extent = 18.0F;
constexpr auto dropdown_open_start_progress = 0.82F;
constexpr auto select_caret_blink_interval = std::chrono::milliseconds{500};
constexpr auto pi = 3.14159265358979323846F;

[[nodiscard]] detail::PopupListMetrics select_popup_metrics() noexcept {
    return detail::PopupListMetrics{.min_width = 160.0F,
                                    .max_width = 360.0F,
                                    .item_height = 30.0F,
                                    .vertical_padding = 4.0F,
                                    .text_padding = 12.0F,
                                    .font_size = 13.0F};
}

[[nodiscard]] bool contains_local_point(layout::Rect rect, layout::Point point) noexcept {
    return rect.width > 0.0F && rect.height > 0.0F && point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

[[nodiscard]] char ascii_lower(char value) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

[[nodiscard]] bool contains_case_insensitive(std::string_view text, std::string_view query) {
    if (query.empty()) {
        return true;
    }
    if (query.size() > text.size()) {
        return false;
    }
    const auto match = std::search(
        text.begin(), text.end(), query.begin(), query.end(),
        [](char lhs, char rhs) noexcept { return ascii_lower(lhs) == ascii_lower(rhs); });
    return match != text.end();
}

[[nodiscard]] char32_t lower_filter_code_point(char32_t value) noexcept {
    if (value >= U'A' && value <= U'Z') {
        return value + 0x20U;
    }
    if ((value >= 0x00C0U && value <= 0x00D6U) || (value >= 0x00D8U && value <= 0x00DEU) ||
        (value >= 0x0391U && value <= 0x03A1U) || (value >= 0x03A3U && value <= 0x03ABU) ||
        (value >= 0x0410U && value <= 0x042FU)) {
        return value + 0x20U;
    }
    if (value == 0x0401U) {
        return 0x0451U;
    }
    return value;
}

[[nodiscard]] std::vector<char32_t> folded_filter_text(std::string_view text) {
    std::vector<char32_t> result;
    result.reserve(text.size());
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = static_cast<unsigned char>(text[offset]);
        char32_t value = first;
        auto width = std::size_t{1U};
        if ((first & 0xE0U) == 0xC0U && offset + 1U < text.size()) {
            value =
                ((first & 0x1FU) << 6U) | (static_cast<unsigned char>(text[offset + 1U]) & 0x3FU);
            width = 2U;
        } else if ((first & 0xF0U) == 0xE0U && offset + 2U < text.size()) {
            value = ((first & 0x0FU) << 12U) |
                    ((static_cast<unsigned char>(text[offset + 1U]) & 0x3FU) << 6U) |
                    (static_cast<unsigned char>(text[offset + 2U]) & 0x3FU);
            width = 3U;
        } else if ((first & 0xF8U) == 0xF0U && offset + 3U < text.size()) {
            value = ((first & 0x07U) << 18U) |
                    ((static_cast<unsigned char>(text[offset + 1U]) & 0x3FU) << 12U) |
                    ((static_cast<unsigned char>(text[offset + 2U]) & 0x3FU) << 6U) |
                    (static_cast<unsigned char>(text[offset + 3U]) & 0x3FU);
            width = 4U;
        }
        result.push_back(lower_filter_code_point(value));
        offset += width;
    }
    return result;
}

[[nodiscard]] bool contains_folded_text(const std::vector<char32_t>& text,
                                        const std::vector<char32_t>& query) {
    if (query.empty()) {
        return true;
    }
    if (query.size() > text.size()) {
        return false;
    }
    return std::search(text.begin(), text.end(), query.begin(), query.end()) != text.end();
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

[[nodiscard]] rendering::Transform2D rotation_transform(float radians) noexcept {
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    return rendering::Transform2D{.m11 = cosine, .m12 = sine, .m21 = -sine, .m22 = cosine};
}

} // namespace

struct Select::EventState {
    ChangeEventHandler selection_changed;
    MultiChangeEventHandler multi_selection_changed;
    RemoteSearchEventHandler remote_search_requested;
};

class SelectDropdown final : public elements::UIElement {
  public:
    explicit SelectDropdown(Select& owner) : owner_(owner) {
        apply_style_value(style::default_select_option_style(), true);
        set_theme_class(style::theme_class::select_option);
        set_focusable(true);
        refresh_from_owner();
        start_open_animation();
    }

    void refresh_from_owner() {
        filtered_indices_ = owner_.filtered_indices_;
        filter_text_ = owner_.filter_text_;
        restart_filter_caret_blink();
        rebuild_rows();
        hovered_visible_index_ = next_enabled_item(1);
        configure_layout([this](layout::LayoutElement& item) {
            const auto size = preferred_size();
            item.set_size(layout::Length::points(size.width), layout::Length::points(size.height));
        });
        invalidate_paint();
    }

    [[nodiscard]] layout::Size preferred_size() const noexcept {
        const auto metrics = select_popup_metrics();
        const auto row_count = std::max<std::size_t>(rows_.size(), 1U);
        const auto filter_extra = owner_.filterable_ ? filter_height : 0.0F;
        auto labels = std::vector<std::string_view>{};
        labels.reserve(row_count + (owner_.filterable_ ? 1U : 0U));
        rendered_labels_.clear();
        rendered_labels_.reserve(rows_.size());
        if (owner_.filterable_) {
            labels.push_back(filter_text_.empty() ? std::string_view{"Filter options"}
                                                  : std::string_view{filter_text_});
        }
        if (rows_.empty()) {
            labels.push_back("No data");
        } else {
            for (const auto& row : rows_) {
                if (row.group_header) {
                    labels.push_back(row.label);
                } else {
                    rendered_labels_.push_back(owner_.option_label(row.option_index));
                    labels.push_back(rendered_labels_.back());
                }
            }
        }
        const auto width = detail::preferred_popup_width(metrics, labels);
        return detail::preferred_popup_list_size(metrics, row_count, width, filter_extra);
    }

  protected:
    void on_pointer_event(elements::PointerEvent& event) override {
        const auto index = item_at(event.local_position);
        switch (event.kind) {
        case elements::PointerEventKind::Move:
            if (hovered_visible_index_ == index) {
                event.handled = true;
                return;
            }
            hovered_visible_index_ = index;
            event.handled = true;
            invalidate_paint();
            return;
        case elements::PointerEventKind::Down:
        case elements::PointerEventKind::DoubleClick:
            pressed_visible_index_ = index;
            event.handled = true;
            invalidate_paint();
            return;
        case elements::PointerEventKind::Up:
            if (event.button == elements::PointerButton::Primary && index &&
                pressed_visible_index_ == index) {
                choose_visible_index(*index);
            }
            pressed_visible_index_.reset();
            event.handled = true;
            invalidate_paint();
            return;
        case elements::PointerEventKind::Cancel:
            pressed_visible_index_.reset();
            event.handled = true;
            return;
        case elements::PointerEventKind::Leave:
            if (!hovered_visible_index_ && !pressed_visible_index_) {
                event.handled = true;
                return;
            }
            hovered_visible_index_.reset();
            pressed_visible_index_.reset();
            event.handled = true;
            invalidate_paint();
            return;
        case elements::PointerEventKind::Wheel:
        case elements::PointerEventKind::HorizontalWheel:
            event.handled = true;
            return;
        case elements::PointerEventKind::Click:
        case elements::PointerEventKind::Enter:
            return;
        }
    }

    void on_key_event(elements::KeyEvent& event) override {
        if (owner_.handle_filter_key_event(event)) {
            return;
        }
        if (event.kind != elements::KeyEventKind::Down) {
            return;
        }
        switch (event.key) {
        case elements::Key::Escape:
            owner_.dismiss_popup();
            event.handled = true;
            return;
        case elements::Key::Up:
            hovered_visible_index_ = next_enabled_item(-1);
            event.handled = true;
            invalidate_paint();
            return;
        case elements::Key::Down:
            hovered_visible_index_ = next_enabled_item(1);
            event.handled = true;
            invalidate_paint();
            return;
        case elements::Key::Enter:
        case elements::Key::Space:
            if (hovered_visible_index_) {
                choose_visible_index(*hovered_visible_index_);
            }
            event.handled = true;
            return;
        default:
            return;
        }
    }

    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override {
        auto active = false;
        if (!open_animation_.empty()) {
            active = open_animation_.tick(now);
            if (active) {
                invalidate_paint();
            }
        }
        if (filter_caret_active()) {
            const auto next_caret_visible = filter_caret_visible_for(now);
            if (filter_caret_visible_ != next_caret_visible) {
                filter_caret_visible_ = next_caret_visible;
                invalidate_paint();
            }
            active = true;
        } else if (!filter_caret_visible_) {
            filter_caret_visible_ = true;
            invalidate_paint();
        }
        return active;
    }

    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override {
        const auto& option_style = style_storage();
        const auto metrics = select_popup_metrics();
        detail::paint_popup_surface(context, absolute_frame, option_style,
                                    animated_open_progress());

        auto item_y = absolute_frame.y + metrics.vertical_padding;
        if (owner_.filterable_) {
            const auto filter_active = owner_.popup_open_ && (owner_.focused() || focused());
            const auto filter_rect =
                layout::Rect{absolute_frame.x + 8.0F, item_y + 4.0F,
                             std::max(0.0F, absolute_frame.width - 16.0F), filter_height - 8.0F};
            context.fill_rounded_rect(filter_rect, option_style.corner_radius,
                                      filter_active ? option_style.background
                                                    : option_style.semantic.surface_subtle);
            context.stroke_pixel_snapped_rect(
                filter_rect,
                filter_active ? option_style.focus_border_color : option_style.border_color, 1.0F);
            const auto filter_text_rect =
                layout::Rect{filter_rect.x + filter_text_padding, filter_rect.y,
                             std::max(0.0F, filter_rect.width - filter_text_padding * 2.0F - 8.0F),
                             filter_rect.height};
            context.draw_text(
                filter_text_.empty() ? "Filter options" : filter_text_, filter_text_rect,
                rendering::TextStyle{.font_size = 12.0F,
                                     .color = filter_text_.empty() ? option_style.placeholder_color
                                                                   : option_style.text_color,
                                     .vertical_alignment = rendering::TextVerticalAlignment::Center,
                                     .wrapping = rendering::TextWrapping::NoWrap,
                                     .trimming = rendering::TextTrimming::CharacterEllipsis});
            if (filter_active && filter_caret_visible_) {
                auto caret_x = filter_text_rect.x + 1.0F;
                if (!filter_text_.empty()) {
                    caret_x += std::min(detail::estimated_popup_text_width(filter_text_, 12.0F),
                                        filter_text_rect.width);
                }
                caret_x = std::clamp(caret_x, filter_text_rect.x + 1.0F,
                                     filter_text_rect.x + filter_text_rect.width + 1.0F);
                context.fill_pixel_snapped_rect(
                    layout::Rect{caret_x, filter_rect.y + 7.0F, 1.0F,
                                 std::max(0.0F, filter_rect.height - 14.0F)},
                    option_style.focus_border_color);
            }
            item_y += filter_height;
        }

        if (rows_.empty()) {
            const auto empty_rect =
                layout::Rect{absolute_frame.x, item_y, absolute_frame.width, metrics.item_height};
            context.draw_text("No data", empty_rect,
                              rendering::TextStyle{.font_size = 13.0F,
                                                   .color = option_style.semantic.disabled_text,
                                                   .alignment = rendering::TextAlignment::Center,
                                                   .vertical_alignment =
                                                       rendering::TextVerticalAlignment::Center});
            context.pop_layer();
            return;
        }

        for (std::size_t visible_index = 0; visible_index < rows_.size(); ++visible_index) {
            const auto& row = rows_[visible_index];
            const auto item_rect =
                layout::Rect{absolute_frame.x, item_y, absolute_frame.width, metrics.item_height};
            if (row.group_header) {
                const auto header_rect =
                    layout::Rect{item_rect.x + metrics.text_padding, item_rect.y,
                                 std::max(0.0F, item_rect.width - metrics.text_padding * 2.0F),
                                 item_rect.height};
                context.draw_text(
                    row.label, header_rect,
                    rendering::TextStyle{
                        .font_size = 12.0F,
                        .color = row.group_disabled ? option_style.semantic.disabled_text
                                                    : option_style.semantic.secondary_text,
                        .alignment = rendering::TextAlignment::Start,
                        .vertical_alignment = rendering::TextVerticalAlignment::Center,
                        .wrapping = rendering::TextWrapping::NoWrap,
                        .trimming = rendering::TextTrimming::CharacterEllipsis});
            } else {
                const auto& option = owner_.options_[row.option_index];
                const auto label = owner_.option_label(row.option_index);
                detail::paint_popup_item(context, item_rect, label, option_style, metrics,
                                         detail::PopupItemPaintState{
                                             .hovered = hovered_visible_index_ == visible_index,
                                             .pressed = pressed_visible_index_ == visible_index,
                                             .selected = owner_.option_selected(row.option_index),
                                             .disabled = option.disabled || row.group_disabled});
            }
            item_y += metrics.item_height;
        }
        context.pop_layer();
    }

  private:
    struct DropdownRow {
        std::size_t option_index = 0;
        std::string label;
        bool group_header = false;
        bool group_disabled = false;
    };

    [[nodiscard]] std::pair<std::string, bool> group_for_option(std::size_t option_index) const {
        for (const auto& group : owner_.option_groups_) {
            if (option_index >= group.start_index &&
                option_index < group.start_index + group.count) {
                return {group.label, group.disabled};
            }
        }
        const auto& option_group = owner_.options_[option_index].group;
        return {option_group, false};
    }

    void rebuild_rows() {
        rows_.clear();
        rows_.reserve(filtered_indices_.size() + owner_.option_groups_.size());
        auto current_group = std::string{};
        for (const auto option_index : filtered_indices_) {
            auto [group_label, group_disabled] = group_for_option(option_index);
            if (!group_label.empty() && group_label != current_group) {
                rows_.push_back(DropdownRow{
                    .label = group_label, .group_header = true, .group_disabled = group_disabled});
                current_group = std::move(group_label);
            }
            rows_.push_back(
                DropdownRow{.option_index = option_index, .group_disabled = group_disabled});
        }
    }

    [[nodiscard]] float animated_open_progress() const {
        if (open_animation_.empty()) {
            return open_progress_;
        }
        static_cast<void>(open_animation_.tick());
        return std::clamp(open_progress_, 0.0F, 1.0F);
    }

    void start_open_animation() {
        open_animation_.clear();
        open_progress_ = dropdown_open_start_progress;
        open_animation_.animate<float>(
            dropdown_open_start_progress, 1.0F,
            animation::make_transition_timing(animation::AnimationDuration{0.16F}),
            [this](const float& value) { open_progress_ = value; });
        open_animation_.play(animation::AnimationClockType::now());
    }

    [[nodiscard]] std::optional<std::size_t> item_at(layout::Point local_position) const noexcept {
        if (rows_.empty()) {
            return std::nullopt;
        }
        const auto index =
            detail::popup_item_at(local_position, select_popup_metrics(), rows_.size(),
                                  owner_.filterable_ ? filter_height : 0.0F);
        if (!index) {
            return std::nullopt;
        }
        const auto& row = rows_[*index];
        if (row.group_header || row.group_disabled || owner_.options_[row.option_index].disabled) {
            return std::nullopt;
        }
        return index;
    }

    [[nodiscard]] std::optional<std::size_t> next_enabled_item(int direction) const noexcept {
        return detail::next_enabled_popup_item(
            rows_.size(), hovered_visible_index_, direction, [this](std::size_t index) noexcept {
                const auto& row = rows_[index];
                return !row.group_header && !row.group_disabled &&
                       !owner_.options_[row.option_index].disabled;
            });
    }

    void choose_visible_index(std::size_t visible_index) {
        if (visible_index >= rows_.size()) {
            return;
        }
        const auto& row = rows_[visible_index];
        if (row.group_header || row.group_disabled) {
            return;
        }
        owner_.choose_index(row.option_index);
        if (!owner_.multiple_) {
            owner_.dismiss_popup();
        }
    }

    [[nodiscard]] bool filter_caret_active() const noexcept {
        return owner_.filterable_ && owner_.popup_open_ && (owner_.focused() || focused());
    }

    [[nodiscard]] bool filter_caret_visible_for(animation::AnimationTimePoint now) const noexcept {
        const auto elapsed = now - filter_caret_epoch_;
        const auto interval_count = elapsed / select_caret_blink_interval;
        return interval_count % 2 == 0;
    }

    void restart_filter_caret_blink() noexcept {
        filter_caret_epoch_ = animation::AnimationClockType::now();
        filter_caret_visible_ = true;
    }

    Select& owner_;
    std::vector<std::size_t> filtered_indices_;
    std::vector<DropdownRow> rows_;
    mutable std::vector<std::string> rendered_labels_;
    std::string filter_text_;
    std::optional<std::size_t> hovered_visible_index_;
    std::optional<std::size_t> pressed_visible_index_;
    mutable animation::Storyboard open_animation_;
    mutable float open_progress_ = 1.0F;
    animation::AnimationTimePoint filter_caret_epoch_ = animation::AnimationClockType::now();
    bool filter_caret_visible_ = true;
};

Select::Select() : Control() {
    apply_style_value(style::default_select_style(), true);
    set_theme_class(style::theme_class::select);
    set_focusable(true);
    arrow_icon_.set_icon_paths(elements::icons::ArrowDown);
    clear_icon_.set_icon_paths(elements::icons::CircleClose);
    loading_icon_.set_icon_paths(elements::icons::Loading);
    update_measure_callback();
}

Select::~Select() {
    *lifetime_token_ = false;
    hover_progress_.clear();
    arrow_progress_.clear();
    loading_progress_.clear();
    dismiss_popup();
}

Select& Select::set_options(std::vector<SelectOption> options) {
    options_ = std::move(options);
    rebuild_option_cache();
    if (selected_index_ && *selected_index_ >= options_.size()) {
        selected_index_.reset();
    }
    selected_indices_.erase(
        std::remove_if(selected_indices_.begin(), selected_indices_.end(),
                       [this](std::size_t index) { return index >= options_.size(); }),
        selected_indices_.end());
    refresh_filter();
    refresh_popup_items();
    invalidate_paint();
    return *this;
}

Select& Select::set_placeholder(std::string_view placeholder) {
    placeholder_ = placeholder;
    invalidate_paint();
    return *this;
}

Select& Select::set_selected_index(std::optional<std::size_t> index) {
    if (index && *index >= options_.size()) {
        index.reset();
    }
    if (selected_index_ == index) {
        return *this;
    }
    selected_index_ = index;
    if (!multiple_) {
        selected_indices_.clear();
        if (selected_index_) {
            selected_indices_.push_back(*selected_index_);
        }
    }
    invalidate_paint();
    refresh_popup_items();
    if (event_state_ != nullptr && !event_state_->selection_changed.empty()) {
        event_state_->selection_changed.emit(selected_index_);
    }
    return *this;
}

Select& Select::set_selected_indices(std::vector<std::size_t> indices) {
    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [this](std::size_t index) {
                                     return index >= options_.size() || options_[index].disabled;
                                 }),
                  indices.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (!multiple_ && !indices.empty()) {
        return set_selected_index(indices.front());
    }
    if (selected_indices_ == indices) {
        return *this;
    }
    selected_indices_ = std::move(indices);
    selected_index_ = selected_indices_.empty()
                          ? std::optional<std::size_t>{}
                          : std::optional<std::size_t>{selected_indices_.front()};
    invalidate_paint();
    refresh_popup_items();
    if (event_state_ != nullptr && !event_state_->multi_selection_changed.empty()) {
        event_state_->multi_selection_changed.emit(selected_indices_);
    }
    if (event_state_ != nullptr && !event_state_->selection_changed.empty()) {
        event_state_->selection_changed.emit(selected_index_);
    }
    return *this;
}

Select& Select::set_multiple(bool multiple) noexcept {
    set_property(property_keys::select_multiple(), multiple);
    return *this;
}

Select& Select::set_tags_visible(bool visible) noexcept {
    tags_visible_ = visible;
    invalidate_paint();
    return *this;
}

Select& Select::set_clearable(bool clearable) noexcept {
    clearable_ = clearable;
    invalidate_paint();
    return *this;
}

Select& Select::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return *this;
    }

    UIElement::set_disabled(disabled);
    if (disabled_) {
        dismiss_popup();
        filter_text_.clear();
        refresh_filter();
    }
    invalidate_paint();
    return *this;
}

Select& Select::set_filterable(bool filterable) {
    filterable_ = filterable;
    if (!filterable_) {
        filter_text_.clear();
    }
    refresh_filter();
    refresh_popup_items();
    return *this;
}

Select& Select::set_filter_text(std::string_view filter_text) {
    if (filter_text_ == filter_text) {
        return *this;
    }
    filter_text_ = filter_text;
    if (remote_search_ && event_state_ != nullptr && !event_state_->remote_search_requested.empty()) {
        event_state_->remote_search_requested.emit(filter_text_);
    }
    refresh_filter();
    refresh_popup_items();
    invalidate_paint();
    return *this;
}

Select& Select::set_remote_search(bool remote) noexcept {
    remote_search_ = remote;
    return *this;
}

Select& Select::set_option_groups(std::vector<SelectOptionGroup> groups) {
    option_groups_ = std::move(groups);
    std::sort(option_groups_.begin(), option_groups_.end(),
              [](const SelectOptionGroup& lhs, const SelectOptionGroup& rhs) {
                  return lhs.start_index < rhs.start_index;
              });
    refresh_popup_items();
    return *this;
}

Select& Select::set_label_formatter(LabelFormatter formatter) {
    label_formatter_ = std::move(formatter);
    rebuild_option_cache();
    refresh_filter();
    refresh_popup_items();
    invalidate_paint();
    return *this;
}

Select& Select::set_filter_mode(SelectFilterMode mode) {
    if (filter_mode_ == mode) {
        return *this;
    }
    filter_mode_ = mode;
    refresh_filter();
    refresh_popup_items();
    invalidate_paint();
    return *this;
}

Select& Select::set_filter_predicate(FilterPredicate predicate) {
    filter_predicate_ = std::move(predicate);
    if (filter_predicate_) {
        filter_mode_ = SelectFilterMode::Custom;
    } else if (filter_mode_ == SelectFilterMode::Custom) {
        filter_mode_ = SelectFilterMode::UnicodeCaseInsensitive;
    }
    refresh_filter();
    refresh_popup_items();
    invalidate_paint();
    return *this;
}

Select& Select::set_loading(bool loading) noexcept {
    if (loading_ == loading) {
        return *this;
    }
    loading_ = loading;
    if (loading_) {
        loading_progress_.animate_loop(animation::AnimationDuration{0.9F});
    } else {
        loading_progress_.set(0.0F);
    }
    invalidate_paint();
    return *this;
}

Select& Select::set_size(SelectSize size) {
    set_property(property_keys::select_size(), size);
    return *this;
}

Select::ChangeEventHandler& Select::selection_changed() noexcept {
    return ensure_event_state().selection_changed;
}

Select::MultiChangeEventHandler& Select::multi_selection_changed() noexcept {
    return ensure_event_state().multi_selection_changed;
}

Select::RemoteSearchEventHandler& Select::remote_search_requested() noexcept {
    return ensure_event_state().remote_search_requested;
}

Select::EventState& Select::ensure_event_state() {
    if (event_state_ == nullptr) {
        event_state_ = std::make_unique<EventState>();
    }
    return *event_state_;
}

Select& Select::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    update_measure_callback();
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

const std::vector<SelectOption>& Select::options() const noexcept {
    return options_;
}

const std::vector<SelectOptionGroup>& Select::option_groups() const noexcept {
    return option_groups_;
}

std::optional<std::size_t> Select::selected_index() const noexcept {
    return selected_index_;
}

std::vector<std::size_t> Select::selected_indices() const {
    return selected_indices_;
}

std::string Select::selected_label() const {
    if (multiple_) {
        auto label = std::string{};
        for (const auto index : selected_indices_) {
            if (index >= options_.size()) {
                continue;
            }
            if (!label.empty()) {
                label += ", ";
            }
            label += option_label(index);
        }
        return label;
    }
    return selected_index_ && *selected_index_ < options_.size() ? option_label(*selected_index_)
                                                                 : std::string{};
}

std::vector<std::string> Select::selected_tags() const {
    auto tags = std::vector<std::string>{};
    tags.reserve(selected_indices_.size());
    for (const auto index : selected_indices_) {
        if (index < options_.size()) {
            tags.push_back(option_label(index));
        }
    }
    return tags;
}

bool Select::popup_open() const noexcept {
    return popup_open_;
}

bool Select::clearable() const noexcept {
    return clearable_;
}

bool Select::disabled() const noexcept {
    return disabled_;
}

bool Select::filterable() const noexcept {
    return filterable_;
}

bool Select::multiple() const noexcept {
    return multiple_;
}

bool Select::tags_visible() const noexcept {
    return tags_visible_;
}

bool Select::remote_search() const noexcept {
    return remote_search_;
}

const std::string& Select::filter_text() const noexcept {
    return filter_text_;
}

SelectFilterMode Select::filter_mode() const noexcept {
    return filter_mode_;
}

std::size_t Select::filtered_option_count() const noexcept {
    return filtered_indices_.size();
}

bool Select::loading() const noexcept {
    return loading_;
}

SelectSize Select::size() const noexcept {
    return size_;
}

void Select::apply_property_change(const core::PropertyChange& change) {
    if (!change.changed) {
        return;
    }

    const auto id = change.metadata->id;

    if (id == property_keys::select_multiple().id()) {
        auto* v = properties().local_value<bool>(property_keys::select_multiple());
        multiple_ = v ? *v : false;
        if (multiple_ && selected_index_ && selected_indices_.empty()) {
            selected_indices_.push_back(*selected_index_);
        }
        if (!multiple_ && selected_indices_.size() > 1U) {
            selected_indices_.erase(selected_indices_.begin() + 1, selected_indices_.end());
            selected_index_ = selected_indices_.empty()
                                  ? std::optional<std::size_t>{}
                                  : std::optional<std::size_t>{selected_indices_.front()};
        }
        refresh_popup_items();
        invalidate_paint();
        return;
    }
    if (id == property_keys::select_size().id()) {
        auto* v = properties().local_value<SelectSize>(property_keys::select_size());
        size_ = v ? *v : SelectSize::Default;
        update_measure_callback();
        mark_measure_dirty();
        invalidate_paint();
        return;
    }

    UIElement::apply_property_change(change);
}

void Select::on_pointer_event(elements::PointerEvent& event) {
    const auto update_hovered_tag_close = [this](std::optional<std::size_t> index) {
        if (hovered_tag_close_index_ == index) {
            return;
        }
        hovered_tag_close_index_ = index;
        invalidate_paint();
    };
    const auto clear_tag_close_interaction = [this]() {
        if (!hovered_tag_close_index_) {
            return;
        }
        hovered_tag_close_index_.reset();
        invalidate_paint();
    };

    if (event.kind == elements::PointerEventKind::Enter) {
        hovered_ = true;
        update_hovered_tag_close(tag_close_index_at(event.local_position));
        animate_hover(1.0F);
        invalidate_paint();
        return;
    }
    if (event.kind == elements::PointerEventKind::Move) {
        if (disabled_ || loading_) {
            clear_tag_close_interaction();
            return;
        }
        update_hovered_tag_close(tag_close_index_at(event.local_position));
        return;
    }
    if (event.kind == elements::PointerEventKind::Leave) {
        hovered_ = false;
        primary_pressed_ = false;
        clear_tag_close_interaction();
        animate_hover(0.0F);
        invalidate_paint();
        return;
    }
    if (disabled_ || loading_ || event.button != elements::PointerButton::Primary) {
        return;
    }
    const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    const auto arrow_rect =
        layout::Rect{local_frame.width - resolved_style().padding.right - arrow_width, 0.0F,
                     arrow_width, local_frame.height};
    const auto clear_rect =
        layout::Rect{arrow_rect.x - arrow_width, 0.0F, arrow_width, local_frame.height};
    if (event.kind == elements::PointerEventKind::Down ||
        event.kind == elements::PointerEventKind::DoubleClick) {
        primary_pressed_ = true;
        const auto pressed_tag_close = tag_close_index_at(event.local_position);
        if (pressed_tag_close) {
            update_hovered_tag_close(pressed_tag_close);
        }
        event.handled = true;
        return;
    }
    if (event.kind == elements::PointerEventKind::Cancel) {
        primary_pressed_ = false;
        event.handled = true;
        return;
    }
    if (event.kind == elements::PointerEventKind::Up) {
        primary_pressed_ = false;
        event.handled = true;
        return;
    }
    if (event.kind == elements::PointerEventKind::Click) {
        if (const auto tag_index = tag_close_index_at(event.local_position)) {
            remove_selected_index(*tag_index);
            hovered_tag_close_index_.reset();
        } else if (clearable_ && (selected_index_.has_value() || !selected_indices_.empty()) &&
                   contains_local_point(clear_rect, event.local_position)) {
            if (contains_centered_circle(clear_rect, select_icon_size, event.local_position)) {
                if (multiple_) {
                    set_selected_indices({});
                } else {
                    set_selected_index(std::nullopt);
                }
            }
        } else if (popup_open_) {
            dismiss_popup();
        } else {
            open_popup();
        }
        event.handled = true;
    }
}

elements::PointerCursor
Select::cursor_for_local_point(layout::Point local_position) const noexcept {
    if (disabled_ || loading_) {
        return elements::PointerCursor::Default;
    }
    if (tag_close_index_at(local_position)) {
        return elements::PointerCursor::Hand;
    }
    const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    const auto arrow_rect =
        layout::Rect{local_frame.width - resolved_style().padding.right - arrow_width, 0.0F,
                     arrow_width, local_frame.height};
    const auto clear_rect =
        layout::Rect{arrow_rect.x - arrow_width, 0.0F, arrow_width, local_frame.height};
    const auto has_value = selected_index_.has_value() || !selected_indices_.empty();
    if (clearable_ && has_value && contains_local_point(clear_rect, local_position)) {
        return contains_centered_circle(clear_rect, select_icon_size, local_position)
                   ? elements::PointerCursor::Hand
                   : elements::PointerCursor::Default;
    }
    return contains_local_point(local_frame, local_position) && !filterable_
               ? elements::PointerCursor::Hand
               : elements::PointerCursor::Default;
}

void Select::on_key_event(elements::KeyEvent& event) {
    if (disabled_ || loading_) {
        return;
    }

    if (handle_filter_key_event(event)) {
        return;
    }

    if (event.kind != elements::KeyEventKind::Down) {
        return;
    }
    if (event.key == elements::Key::Enter || event.key == elements::Key::Space ||
        event.key == elements::Key::Down) {
        open_popup();
        event.handled = true;
    } else if (event.key == elements::Key::Escape && popup_open_) {
        dismiss_popup();
        event.handled = true;
    }
}

bool Select::handle_filter_key_event(elements::KeyEvent& event) {
    if (!filterable_ || disabled_ || loading_) {
        return false;
    }

    if (event.kind == elements::KeyEventKind::TextInput) {
        if (!popup_open_) {
            open_popup();
        }
        if (!event.text.empty()) {
            set_filter_text(filter_text_ + event.text);
            event.handled = true;
            return true;
        }
        return false;
    }

    if (event.kind != elements::KeyEventKind::Down || !popup_open_) {
        return false;
    }

    if (event.key == elements::Key::Space) {
        event.handled = true;
        return true;
    }

    if (event.key == elements::Key::Backspace) {
        if (!filter_text_.empty()) {
            const auto previous =
                rendering::previous_utf8_boundary(filter_text_, filter_text_.size());
            set_filter_text(filter_text_.substr(0U, previous));
        }
        event.handled = true;
        return true;
    }

    return false;
}

void Select::on_focus_changed(const elements::FocusChangeEvent& event) {
    if (!event.focused && !event.focus_within) {
        dismiss_popup();
    }
}

bool Select::on_animation_frame(animation::AnimationTimePoint now) {
    auto active = hover_progress_.tick(now);
    active = arrow_progress_.tick(now) || active;
    active = loading_progress_.tick(now) || active;
    if (active) {
        invalidate_paint();
    }
    return active;
}

void Select::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    const auto style = resolved_style();
    const auto hover_border = animation::interpolate_value(
        style.border_color, style.semantic.hover_border, animated_hover_progress());
    const auto border = focused() || popup_open_ ? style.focus_border_color : hover_border;
    const auto background = disabled_ ? style.read_only_background : style.background;
    winelement::style::paint_rectangle(
        context, absolute_frame,
        winelement::style::rectangle_style_from(style, background, border));

    auto content = detail::inset_rect(absolute_frame, style.padding);
    const auto has_value = selected_index_.has_value() || !selected_indices_.empty();
    const auto clear_visible = clearable_ && has_value && hovered_ && !disabled_ && !loading_;
    content.width = std::max(0.0F, content.width - arrow_width * (clear_visible ? 2.0F : 1.0F));
    const auto draw_plain_label = [&]() {
        const auto label = loading_          ? std::string("Loading")
                           : selected_index_ ? selected_label()
                                             : placeholder_;
        context.draw_text(
            label, content,
            rendering::TextStyle{.font_size = style.font_size,
                                 .color = disabled_         ? style.semantic.disabled_text
                                          : selected_index_ ? style.text_color
                                                            : style.placeholder_color,
                                 .vertical_alignment = rendering::TextVerticalAlignment::Center,
                                 .wrapping = rendering::TextWrapping::NoWrap,
                                 .trimming = rendering::TextTrimming::CharacterEllipsis});
    };
    if (multiple_ && tags_visible_ && !selected_indices_.empty() && !loading_) {
        auto tag_x = content.x;
        const auto tag_height = std::max(20.0F, std::min(content.height - 2.0F, 24.0F));
        const auto tag_y = content.y + (content.height - tag_height) * 0.5F;
        context.save();
        context.push_clip(content);
        for (const auto index : selected_indices_) {
            if (index >= options_.size()) {
                continue;
            }
            const auto tag = option_label(index);
            const auto label_width = detail::estimated_popup_text_width(tag, style.font_size);
            const auto tag_width =
                std::min(label_width + tag_horizontal_padding * 2.0F + tag_close_slot_width,
                         std::max(0.0F, content.x + content.width - tag_x));
            if (tag_width <= tag_horizontal_padding * 2.0F + tag_close_slot_width) {
                break;
            }
            const auto tag_rect = layout::Rect{tag_x, tag_y, tag_width, tag_height};
            const auto close_slot_rect = layout::Rect{
                tag_rect.x + tag_rect.width - tag_horizontal_padding - tag_close_slot_width,
                tag_rect.y, tag_close_slot_width, tag_rect.height};
            const auto close_hovered =
                hovered_tag_close_index_ && *hovered_tag_close_index_ == index;
            context.fill_rounded_rect(tag_rect, rendering::CornerRadius::uniform(4.0F),
                                      style.semantic.surface_subtle);
            context.stroke_pixel_snapped_rect(tag_rect, style.border_color, 1.0F);
            context.draw_text(
                tag,
                layout::Rect{tag_rect.x + tag_horizontal_padding, tag_rect.y + 1.0F,
                             std::max(0.0F, tag_rect.width - tag_horizontal_padding * 2.0F -
                                                tag_close_slot_width),
                             std::max(0.0F, tag_rect.height - 2.0F)},
                rendering::TextStyle{.font_size = style.font_size,
                                     .color = disabled_ ? style.semantic.disabled_text
                                                        : style.text_color,
                                     .vertical_alignment = rendering::TextVerticalAlignment::Center,
                                     .wrapping = rendering::TextWrapping::NoWrap,
                                     .trimming = rendering::TextTrimming::CharacterEllipsis});
            const auto close_color = disabled_       ? style.semantic.disabled_text
                                     : close_hovered ? style.focus_border_color
                                                     : style.semantic.secondary_text;
            clear_icon_.paint_icon(
                context, centered_square_rect(close_slot_rect, tag_close_icon_size), close_color);
            tag_x += tag_width + tag_gap;
        }
        context.pop_clip();
        context.restore();
    } else {
        draw_plain_label();
    }
    const auto arrow_rect =
        layout::Rect{absolute_frame.x + absolute_frame.width - style.padding.right - arrow_width,
                     absolute_frame.y, arrow_width, absolute_frame.height};
    if (clear_visible) {
        const auto clear_rect =
            layout::Rect{arrow_rect.x - arrow_width, arrow_rect.y, arrow_width, arrow_rect.height};
        const auto clear_progress = animated_hover_progress();
        const auto clear_color = animation::interpolate_value(
            style.semantic.secondary_text, style.focus_border_color, clear_progress);
        clear_icon_.paint_icon(context, centered_square_rect(clear_rect, select_icon_size),
                               clear_color);
    }
    const auto arrow_progress = animated_popup_indicator_progress();
    const auto icon_rect = centered_square_rect(arrow_rect, select_icon_size);
    const auto icon_color =
        disabled_ ? style.semantic.disabled_text : style.semantic.secondary_text;
    if (loading_) {
        const auto center = layout::Point{icon_rect.x + icon_rect.width * 0.5F,
                                          icon_rect.y + icon_rect.height * 0.5F};
        context.push_layer(rendering::RenderLayerOptions{
            .bounds = arrow_rect,
            .transform = rendering::transform_around_point(
                rotation_transform(pi * 2.0F * animated_loading_progress()), center),
            .clips_to_bounds = false});
        loading_icon_.paint_icon(context, icon_rect, icon_color);
        context.pop_layer();
    } else if (arrow_progress > 0.0001F) {
        const auto center = layout::Point{icon_rect.x + icon_rect.width * 0.5F,
                                          icon_rect.y + icon_rect.height * 0.5F};
        context.push_layer(rendering::RenderLayerOptions{
            .bounds = arrow_rect,
            .transform =
                rendering::transform_around_point(rotation_transform(pi * arrow_progress), center),
            .clips_to_bounds = false});
        arrow_icon_.paint_icon(context, icon_rect, icon_color);
        context.pop_layer();
    } else {
        arrow_icon_.paint_icon(context, icon_rect, icon_color);
    }
}

void Select::update_measure_callback() {
    set_measure_callback([this](const layout::MeasureInput&) {
        const auto style = resolved_style();
        return layout::Size{style.min_width, style.min_height};
    });
}

void Select::open_popup() {
    if (disabled_ || loading_ || popup_open_) {
        return;
    }

    if (filterable_) {
        filter_text_.clear();
    }
    refresh_filter();
    auto dropdown = make_child<SelectDropdown>(*this);
    const auto popup_size = dropdown->preferred_size();
    elements::PopupManager popup_manager(*this);
    auto weak_lifetime = std::weak_ptr<bool>{lifetime_token_};
    auto* owner = this;
    const auto result = popup_manager.open_for_anchor(
        *this, std::move(dropdown),
        elements::PopupOptions{.size = popup_size,
                               .placement = elements::PopupPlacement::BottomStart,
                               .gap = 4.0F,
                               .viewport_margin = 4.0F,
                               .match_anchor_width = true,
                               .light_dismiss = true,
                               .preserve_focus = true});
    if (auto* popup = popup_manager.element(result.handle); popup != nullptr) {
        popup->dismissed_event() += [weak_lifetime, owner]() {
            const auto alive = weak_lifetime.lock();
            if (alive != nullptr && *alive) {
                owner->popup_open_ = false;
                owner->animate_popup_indicator(0.0F);
                owner->invalidate_paint();
            }
        };
    }
    popup_handle_ = result.handle;
    popup_open_ = popup_handle_.valid();
    animate_popup_indicator(popup_open_ ? 1.0F : 0.0F);
    invalidate_paint();
}

void Select::dismiss_popup() noexcept {
    const auto was_open = popup_open_;
    if (popup_handle_.valid()) {
        try {
            elements::PopupManager popup_manager(*this);
            static_cast<void>(popup_manager.close(popup_handle_));
            popup_handle_.reset();
            popup_open_ = false;
        } catch (...) {
            popup_open_ = was_open;
            return;
        }
    } else {
        popup_open_ = false;
    }
    animate_popup_indicator(popup_open_ ? 1.0F : 0.0F);
    invalidate_paint();
}

void Select::choose_index(std::size_t index) {
    if (index >= options_.size() || options_[index].disabled) {
        return;
    }
    if (!multiple_) {
        set_selected_index(index);
        return;
    }
    auto indices = selected_indices_;
    const auto iterator = std::find(indices.begin(), indices.end(), index);
    if (iterator == indices.end()) {
        indices.push_back(index);
    } else {
        indices.erase(iterator);
    }
    set_selected_indices(std::move(indices));
}

void Select::remove_selected_index(std::size_t index) {
    if (!multiple_) {
        if (selected_index_ && *selected_index_ == index) {
            set_selected_index(std::nullopt);
        }
        return;
    }

    auto indices = selected_indices_;
    const auto iterator = std::find(indices.begin(), indices.end(), index);
    if (iterator == indices.end()) {
        return;
    }
    indices.erase(iterator);
    set_selected_indices(std::move(indices));
}

bool Select::option_selected(std::size_t index) const noexcept {
    if (multiple_) {
        return std::find(selected_indices_.begin(), selected_indices_.end(), index) !=
               selected_indices_.end();
    }
    return selected_index_ == index;
}

std::string Select::option_label(std::size_t index) const {
    if (index >= options_.size()) {
        return {};
    }
    if (option_render_cache_.size() == options_.size()) {
        return option_render_cache_[index].label;
    }
    return label_formatter_ ? label_formatter_(options_[index], index) : options_[index].label;
}

std::optional<std::size_t> Select::tag_close_index_at(layout::Point local_position) const {
    if (!multiple_ || !tags_visible_ || selected_indices_.empty() || disabled_ || loading_) {
        return std::nullopt;
    }

    const auto style = resolved_style();
    const auto local_frame = layout::Rect{0.0F, 0.0F, frame().width, frame().height};
    auto content = detail::inset_rect(local_frame, style.padding);
    const auto has_value = selected_index_.has_value() || !selected_indices_.empty();
    const auto clear_visible = clearable_ && has_value && hovered_;
    content.width = std::max(0.0F, content.width - arrow_width * (clear_visible ? 2.0F : 1.0F));
    if (!contains_local_point(content, local_position)) {
        return std::nullopt;
    }

    auto tag_x = content.x;
    const auto tag_height = std::max(20.0F, std::min(content.height - 2.0F, 24.0F));
    const auto tag_y = content.y + (content.height - tag_height) * 0.5F;
    for (const auto index : selected_indices_) {
        if (index >= options_.size()) {
            continue;
        }
        const auto label = option_label(index);
        const auto label_width = detail::estimated_popup_text_width(label, style.font_size);
        const auto tag_width =
            std::min(label_width + tag_horizontal_padding * 2.0F + tag_close_slot_width,
                     std::max(0.0F, content.x + content.width - tag_x));
        if (tag_width <= tag_horizontal_padding * 2.0F + tag_close_slot_width) {
            break;
        }
        const auto tag_rect = layout::Rect{tag_x, tag_y, tag_width, tag_height};
        const auto close_center = layout::Point{
            tag_rect.x + tag_rect.width - tag_horizontal_padding - tag_close_slot_width * 0.5F,
            tag_rect.y + tag_rect.height * 0.5F};
        const auto hit_extent = std::min(tag_close_hit_extent, tag_rect.height);
        const auto close_hit_rect =
            layout::Rect{close_center.x - hit_extent * 0.5F, close_center.y - hit_extent * 0.5F,
                         hit_extent, hit_extent};
        if (contains_local_point(close_hit_rect, local_position)) {
            return index;
        }
        tag_x += tag_width + tag_gap;
    }
    return std::nullopt;
}

void Select::rebuild_option_cache() {
    option_render_cache_.clear();
    option_render_cache_.reserve(options_.size());
    for (std::size_t index = 0; index < options_.size(); ++index) {
        auto label =
            label_formatter_ ? label_formatter_(options_[index], index) : options_[index].label;
        auto folded_label = folded_filter_text(label);
        auto folded_value = folded_filter_text(options_[index].value);
        option_render_cache_.push_back(OptionRenderCache{.label = std::move(label),
                                                         .folded_label = std::move(folded_label),
                                                         .folded_value = std::move(folded_value)});
    }
}

void Select::refresh_filter() {
    if (option_render_cache_.size() != options_.size()) {
        rebuild_option_cache();
    }

    filtered_indices_.clear();
    filtered_indices_.reserve(options_.size());
    const auto folded_query = filter_mode_ == SelectFilterMode::UnicodeCaseInsensitive &&
                                      filterable_ && !filter_text_.empty()
                                  ? folded_filter_text(filter_text_)
                                  : std::vector<char32_t>{};
    for (std::size_t index = 0; index < options_.size(); ++index) {
        const auto& option = options_[index];
        const auto& cache = option_render_cache_[index];
        const auto matches_filter = [this, &folded_query](std::string_view text,
                                                          const std::vector<char32_t>& folded) {
            switch (filter_mode_) {
            case SelectFilterMode::AsciiCaseInsensitive:
                return contains_case_insensitive(text, filter_text_);
            case SelectFilterMode::UnicodeCaseInsensitive:
                return contains_folded_text(folded, folded_query);
            case SelectFilterMode::Custom:
                return filter_predicate_ ? filter_predicate_(text, filter_text_) : true;
            }
            return true;
        };
        if (!filterable_ || filter_text_.empty() ||
            matches_filter(cache.label, cache.folded_label) ||
            matches_filter(option.value, cache.folded_value)) {
            filtered_indices_.push_back(index);
        }
    }
}

void Select::refresh_popup_items() {
    if (!popup_handle_.valid()) {
        return;
    }
    elements::PopupManager popup_manager(*this);
    auto* dropdown = dynamic_cast<SelectDropdown*>(popup_manager.element(popup_handle_));
    if (dropdown == nullptr) {
        return;
    }
    dropdown->refresh_from_owner();
    const auto popup_size = dropdown->preferred_size();
    static_cast<void>(popup_manager.update_placement_for_anchor(
        popup_handle_, *this,
        elements::PopupOptions{.size = popup_size,
                               .placement = elements::PopupPlacement::BottomStart,
                               .gap = 4.0F,
                               .viewport_margin = 4.0F,
                               .match_anchor_width = true,
                               .light_dismiss = true,
                               .preserve_focus = true}));
}

style::UIElementStyle Select::resolved_style() const noexcept {
    auto style = style_storage();
    switch (size_) {
    case SelectSize::Large:
        style.min_height = 40.0F;
        style.padding = layout::EdgeInsets{14.0F, 8.0F, 14.0F, 8.0F};
        break;
    case SelectSize::Small:
        style.min_height = 24.0F;
        style.font_size = 12.0F;
        style.padding = layout::EdgeInsets{8.0F, 4.0F, 8.0F, 4.0F};
        break;
    case SelectSize::Default:
        break;
    }
    return style;
}

float Select::animated_hover_progress() const {
    return std::clamp(hover_progress_.value(), 0.0F, 1.0F);
}

float Select::animated_popup_indicator_progress() const {
    return std::clamp(arrow_progress_.value(), 0.0F, 1.0F);
}

float Select::animated_loading_progress() const {
    return loading_progress_.value();
}

void Select::animate_hover(float target) {
    hover_progress_.animate_to(target);
}

void Select::animate_popup_indicator(float target) {
    arrow_progress_.animate_to(target, animation::AnimationDuration{0.14F});
}

} // namespace winelement::controls

