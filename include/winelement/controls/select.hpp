#pragma once

#include <winelement/animation.hpp>
#include <winelement/controls/control.hpp>
#include <winelement/controls/control_animation.hpp>
#include <winelement/elements/popup_manager.hpp>
#include <winelement/elements/svg_icon.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winelement::controls {

struct SelectOption {
    std::string label;
    std::string value;
    std::string group;
    bool disabled = false;
};

struct SelectOptionGroup {
    std::string label;
    std::size_t start_index = 0;
    std::size_t count = 0;
    bool disabled = false;
};

enum class SelectSize { Default, Large, Small };
enum class SelectFilterMode { AsciiCaseInsensitive, UnicodeCaseInsensitive, Custom };

class Select : public Control {
  public:
    using FilterPredicate = std::function<bool(std::string_view, std::string_view)>;
    using LabelFormatter = std::function<std::string(const SelectOption&, std::size_t)>;
    using ChangeEventHandler = core::EventHandler<std::optional<std::size_t>>;
    using MultiChangeEventHandler = core::EventHandler<const std::vector<std::size_t>&>;
    using RemoteSearchEventHandler = core::EventHandler<std::string_view>;

    Select();
    ~Select() override;

    Select& set_options(std::vector<SelectOption> options);
    Select& set_placeholder(std::string_view placeholder);
    Select& set_selected_index(std::optional<std::size_t> index);
    Select& set_selected_indices(std::vector<std::size_t> indices);
    Select& set_multiple(bool multiple) noexcept;
    Select& set_tags_visible(bool visible) noexcept;
    Select& set_clearable(bool clearable) noexcept;
    Select& set_disabled(bool disabled) noexcept;
    Select& set_filterable(bool filterable);
    Select& set_filter_text(std::string_view filter_text);
    Select& set_filter_mode(SelectFilterMode mode);
    Select& set_filter_predicate(FilterPredicate predicate);
    Select& set_remote_search(bool remote) noexcept;
    Select& set_option_groups(std::vector<SelectOptionGroup> groups);
    Select& set_label_formatter(LabelFormatter formatter);
    Select& set_loading(bool loading) noexcept;
    Select& set_size(SelectSize size);
    [[nodiscard]] ChangeEventHandler& selection_changed() noexcept;
    [[nodiscard]] MultiChangeEventHandler& multi_selection_changed() noexcept;
    [[nodiscard]] RemoteSearchEventHandler& remote_search_requested() noexcept;
    Select& set_style(style::UIElementStyle style) override;
    [[nodiscard]] const std::vector<SelectOption>& options() const noexcept;
    [[nodiscard]] const std::vector<SelectOptionGroup>& option_groups() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
    [[nodiscard]] std::vector<std::size_t> selected_indices() const;
    [[nodiscard]] std::string selected_label() const;
    [[nodiscard]] std::vector<std::string> selected_tags() const;
    [[nodiscard]] bool popup_open() const noexcept;
    [[nodiscard]] bool clearable() const noexcept;
    [[nodiscard]] bool disabled() const noexcept;
    [[nodiscard]] bool filterable() const noexcept;
    [[nodiscard]] bool multiple() const noexcept;
    [[nodiscard]] bool tags_visible() const noexcept;
    [[nodiscard]] bool remote_search() const noexcept;
    [[nodiscard]] const std::string& filter_text() const noexcept;
    [[nodiscard]] SelectFilterMode filter_mode() const noexcept;
    [[nodiscard]] std::size_t filtered_option_count() const noexcept;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] SelectSize size() const noexcept;

  protected:
    void on_pointer_event(elements::PointerEvent& event) override;
    void on_key_event(elements::KeyEvent& event) override;
    void on_focus_changed(const elements::FocusChangeEvent& event) override;
    [[nodiscard]] elements::PointerCursor
    cursor_for_local_point(layout::Point local_position) const noexcept override;
    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override;
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;
    void apply_property_change(const core::PropertyChange& change) override;

  private:
    friend class SelectDropdown;

    struct OptionRenderCache {
        std::string label;
        std::vector<char32_t> folded_label;
        std::vector<char32_t> folded_value;
    };

    void update_measure_callback();
    void open_popup();
    void dismiss_popup() noexcept;
    void choose_index(std::size_t index);
    void remove_selected_index(std::size_t index);
    bool handle_filter_key_event(elements::KeyEvent& event);
    [[nodiscard]] bool option_selected(std::size_t index) const noexcept;
    [[nodiscard]] std::string option_label(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> tag_close_index_at(layout::Point local_position) const;
    void rebuild_option_cache();
    void refresh_filter();
    void refresh_popup_items();
    [[nodiscard]] style::UIElementStyle resolved_style() const noexcept;
    [[nodiscard]] float animated_hover_progress() const;
    [[nodiscard]] float animated_popup_indicator_progress() const;
    [[nodiscard]] float animated_loading_progress() const;
    void animate_hover(float target);
    void animate_popup_indicator(float target);

    struct EventState;
    [[nodiscard]] EventState& ensure_event_state();

    std::vector<SelectOption> options_;
    std::vector<SelectOptionGroup> option_groups_;
    std::vector<OptionRenderCache> option_render_cache_;
    std::vector<std::size_t> filtered_indices_;
    std::string placeholder_ = "Select";
    std::string filter_text_;
    FilterPredicate filter_predicate_;
    LabelFormatter label_formatter_;
    std::optional<std::size_t> selected_index_;
    std::vector<std::size_t> selected_indices_;
    elements::PopupHandle popup_handle_{};
    elements::SvgIcon arrow_icon_;
    elements::SvgIcon clear_icon_;
    elements::SvgIcon loading_icon_;
    AnimatedFloat hover_progress_{0.0F};
    AnimatedFloat arrow_progress_{0.0F};
    AnimatedFloat loading_progress_{0.0F};
    SelectSize size_ = SelectSize::Default;
    SelectFilterMode filter_mode_ = SelectFilterMode::UnicodeCaseInsensitive;
    bool clearable_ = false;
    bool multiple_ = false;
    bool tags_visible_ = false;
    bool remote_search_ = false;
    bool filterable_ = false;
    bool loading_ = false;
    bool hovered_ = false;
    bool primary_pressed_ = false;
    bool popup_open_ = false;
    std::optional<std::size_t> hovered_tag_close_index_;
    std::shared_ptr<bool> lifetime_token_ = std::make_shared<bool>(true);
    std::unique_ptr<EventState> event_state_;
};

} // namespace winelement::controls

