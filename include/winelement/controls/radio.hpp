#pragma once

#include <winelement/animation.hpp>
#include <winelement/controls/control.hpp>
#include <winelement/controls/control_animation.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace winelement::controls {

class Radio;

class RadioGroupContext final {
  public:
    using ChangeHandler = std::function<void(std::string_view)>;
    using ChangeEventSignal = core::EventSignal<std::string_view>;

    RadioGroupContext();
    ~RadioGroupContext();

    RadioGroupContext(const RadioGroupContext&) = delete;
    RadioGroupContext& operator=(const RadioGroupContext&) = delete;

    RadioGroupContext& set_value(std::string_view value);
    RadioGroupContext& clear_value();
    RadioGroupContext& set_on_change(ChangeHandler handler);
    [[nodiscard]] ChangeEventSignal& changed() noexcept;
    [[nodiscard]] const std::string& value() const noexcept;
    [[nodiscard]] bool has_value() const noexcept;

    void register_radio(Radio& radio);
    void unregister_radio(Radio& radio) noexcept;
    bool select(std::string_view value);
    bool move_selection(const Radio& current, int direction);

  private:
    void sync_radios(bool notify_radios);

    struct EventState;
    [[nodiscard]] EventState& ensure_event_state();

    std::vector<Radio*> radios_;
    std::string value_;
    std::unique_ptr<EventState> event_state_;
    bool has_value_ = false;
};

class Radio : public Control {
  public:
    using ChangeHandler = std::function<void(bool)>;
    using ChangeEventSignal = core::EventSignal<bool>;

    Radio();
    ~Radio() override;

    Radio& set_text(std::string_view text);
    Radio& set_value(std::string_view value);
    Radio& set_group(std::shared_ptr<RadioGroupContext> group);
    Radio& set_checked(bool checked);
    Radio& set_disabled(bool disabled) noexcept;
    Radio& set_on_change(ChangeHandler handler);
    [[nodiscard]] ChangeEventSignal& changed() noexcept;
    Radio& set_style(style::UIElementStyle style) override;
    [[nodiscard]] bool checked() const noexcept;
    [[nodiscard]] bool disabled() const noexcept;
    [[nodiscard]] const std::string& value() const noexcept;
    [[nodiscard]] std::shared_ptr<RadioGroupContext> group() const noexcept;

  protected:
    void on_pointer_event(elements::PointerEvent& event) override;
    void on_key_event(elements::KeyEvent& event) override;
    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override;
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;
    void apply_property_change(const core::PropertyChange& change) override;

  private:
    friend class RadioGroupContext;

    void update_measure_callback();
    [[nodiscard]] float animated_checked_progress() const;
    [[nodiscard]] float animated_hover_progress() const;
    void animate_checked(float target);
    void animate_hover(float target);
    void activate();
    void set_checked_from_group(bool checked, bool notify);

    struct EventState;
    [[nodiscard]] EventState& ensure_event_state();

    std::shared_ptr<RadioGroupContext> group_;
    std::string value_;
    bool checked_ = false;
    std::unique_ptr<EventState> event_state_;
    AnimatedFloat checked_progress_{0.0F};
    AnimatedFloat hover_progress_{0.0F};
};

} // namespace winelement::controls
