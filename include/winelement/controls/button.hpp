#pragma once

#include <winelement/animation.hpp>
#include <winelement/controls/control.hpp>
#include <winelement/controls/control_animation.hpp>
#include <winelement/core/event.hpp>
#include <winelement/elements/svg_icon.hpp>
#include <winelement/rendering/render_types.hpp>
#include <winelement/rendering/text_engine.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace winelement::rendering {
class RenderContext;
}

namespace winelement::elements::icons {
struct IconPathsBase;
} // namespace winelement::elements::icons

namespace winelement::controls {

enum class ButtonType { Default, Primary, Success, Warning, Danger, Info, Text };
enum class ButtonSize { Default, Large, Small };
enum class ButtonRole { Button, Submit, Reset, Menu, SplitPrimary, SplitSecondary };

class Button;

struct ButtonClickEvent {
    Button& sender;
    ButtonRole role = ButtonRole::Button;
    bool from_keyboard = false;
};

enum class ButtonFlag : std::uint16_t {
    None = 0,
    Pressed = 1 << 0,
    Loading = 1 << 1,
    Plain = 1 << 2,
    Round = 1 << 3,
    Circle = 1 << 4,
    Dashed = 1 << 5,
    TextVariant = 1 << 6,
    BackgroundAlways = 1 << 7,
    LinkVariant = 1 << 8,
    Autofocus = 1 << 9,
    DarkMode = 1 << 10,
    AutoInsertSpace = 1 << 11,
};

[[nodiscard]] constexpr ButtonFlag operator|(ButtonFlag left, ButtonFlag right) noexcept {
    return static_cast<ButtonFlag>(static_cast<std::uint16_t>(left) |
                                   static_cast<std::uint16_t>(right));
}

[[nodiscard]] constexpr ButtonFlag operator&(ButtonFlag left, ButtonFlag right) noexcept {
    return static_cast<ButtonFlag>(static_cast<std::uint16_t>(left) &
                                   static_cast<std::uint16_t>(right));
}

[[nodiscard]] constexpr ButtonFlag operator~(ButtonFlag value) noexcept {
    return static_cast<ButtonFlag>(~static_cast<std::uint16_t>(value));
}

constexpr ButtonFlag& operator|=(ButtonFlag& left, ButtonFlag right) noexcept {
    left = left | right;
    return left;
}

constexpr ButtonFlag& operator&=(ButtonFlag& left, ButtonFlag right) noexcept {
    left = left & right;
    return left;
}

[[nodiscard]] constexpr bool has_flag(ButtonFlag value, ButtonFlag flag) noexcept {
    return (static_cast<std::uint16_t>(value) & static_cast<std::uint16_t>(flag)) != 0U;
}

class Button : public Control {
  public:
    using ClickHandler = std::function<void()>;
    using ClickEventSignal = core::EventSignal<const ButtonClickEvent&>;

    Button();
    ~Button() override;

    Button& set_text(std::string_view text);
    Button& set_type(ButtonType type);
    Button& set_size(ButtonSize size);
    Button& set_disabled(bool disabled);
    Button& set_loading(bool loading);
    Button& set_plain(bool plain);
    Button& set_round(bool round);
    Button& set_circle(bool circle);
    Button& set_dashed(bool dashed);
    Button& set_text_variant(bool text_variant);
    Button& set_background_always(bool bg);
    Button& set_link_variant(bool link);
    Button& set_icon_geometry(rendering::Geometry geometry);
    Button& set_icon_data(std::string_view svg_path_data);
    Button& set_icon_paths(const elements::icons::IconPathsBase& icon_data);
    Button& clear_icon();
    Button& set_role(ButtonRole role);
    Button& set_group_name(std::string_view group_name);
    Button& set_menu_indicator_visible(bool visible);
    Button& set_split(bool split);
    Button& set_autofocus(bool autofocus);
    Button& set_custom_color(rendering::Color color);
    Button& clear_custom_color();
    Button& set_dark_mode(bool dark);
    Button& set_auto_insert_space(bool auto_space);
    Button& set_on_click(ClickHandler handler);
    [[nodiscard]] ClickEventSignal& clicked() noexcept;
    Button& set_style(style::UIElementStyle style) override;
    [[nodiscard]] ButtonType type() const noexcept;
    [[nodiscard]] ButtonSize size() const noexcept;
    [[nodiscard]] bool disabled() const noexcept;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] bool plain() const noexcept;
    [[nodiscard]] bool round() const noexcept;
    [[nodiscard]] bool circle() const noexcept;
    [[nodiscard]] bool dashed() const noexcept;
    [[nodiscard]] bool text_variant() const noexcept;
    [[nodiscard]] bool background_always() const noexcept;
    [[nodiscard]] bool link_variant() const noexcept;
    [[nodiscard]] bool pressed() const noexcept;
    [[nodiscard]] bool has_icon() const noexcept;
    [[nodiscard]] ButtonRole role() const noexcept;
    [[nodiscard]] const std::string& group_name() const noexcept;
    [[nodiscard]] bool menu_indicator_visible() const noexcept;
    [[nodiscard]] bool split() const noexcept;
    [[nodiscard]] bool autofocus() const noexcept;
    [[nodiscard]] const std::optional<rendering::Color>& custom_color() const noexcept;
    [[nodiscard]] bool dark_mode() const noexcept;
    [[nodiscard]] bool auto_insert_space() const noexcept;

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
    void update_measure_callback();
    void apply_semantic_style();
    void apply_type_style(style::UIElementStyle& style) const;
    void apply_shape_style(style::UIElementStyle& style) const;
    void apply_variant_style(style::UIElementStyle& style) const;
    void apply_custom_color_style(style::UIElementStyle& style) const;
    void apply_dark_mode_style(style::UIElementStyle& style) const;
    [[nodiscard]] rendering::TextLayout create_text_layout(layout::Rect content_rect) const;
    [[nodiscard]] rendering::Color interactive_text_color() const noexcept;
    [[nodiscard]] std::string_view display_text() const;
    void invalidate_display_text_cache() const noexcept;
    [[nodiscard]] bool icon_visible() const noexcept;
    [[nodiscard]] bool menu_indicator_displayed() const noexcept;
    [[nodiscard]] float animated_hover_progress() const;
    [[nodiscard]] float animated_pressed_progress() const;
    [[nodiscard]] float animated_click_progress() const;
    [[nodiscard]] float animated_loading_progress() const;
    void animate_hover(float target);
    void animate_pressed(float target);
    void animate_click();
    void set_pressed(bool pressed);
    void click(bool from_keyboard = false);
    struct ClickEventState;
    [[nodiscard]] ClickEventState& ensure_click_event_state();

    ClickHandler click_handler_;
    ButtonType type_ = ButtonType::Default;
    ButtonSize size_ = ButtonSize::Default;
    ButtonRole role_ = ButtonRole::Button;
    ButtonFlag flags_ = ButtonFlag::None;
    elements::SvgIcon icon_;
    elements::SvgIcon loading_icon_;
    std::optional<rendering::Color> custom_color_;
    std::string group_name_;
    bool menu_indicator_visible_ = false;
    bool split_ = false;
    bool focus_visible_ = false;
    std::unique_ptr<ClickEventState> click_event_state_;
    mutable std::string display_text_cache_source_;
    mutable std::string display_text_cache_;
    mutable bool display_text_cache_valid_ = false;
    AnimatedFloat hover_progress_{0.0F};
    AnimatedFloat pressed_progress_{0.0F};
    AnimatedFloat click_progress_{0.0F};
    AnimatedFloat loading_progress_{0.0F};
};

} // namespace winelement::controls
