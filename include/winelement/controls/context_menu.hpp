#pragma once

#include <winelement/animation.hpp>
#include <winelement/controls/control.hpp>
#include <winelement/controls/control_animation.hpp>
#include <winelement/elements/svg_icon.hpp>
#include <winelement/layout/layout_types.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winelement::controls {

struct ContextMenuItem {
    std::string text;
    bool enabled = true;
    std::string id;
    std::string icon_name;
    std::string shortcut_text;
    std::string group;
    std::vector<ContextMenuItem> submenu;
    bool checkable = false;
    bool checked = false;
    bool radio = false;
    bool separator = false;
};

struct ContextMenuMetrics {
    float min_width = 136.0F;
    float max_width = 320.0F;
    float item_height = 28.0F;
    float vertical_padding = 4.0F;
    float text_padding = 12.0F;
    float icon_column_width = 24.0F;
    float shortcut_padding = 24.0F;
    float font_size = 13.0F;
    std::size_t max_visible_items = 0;
};

class ContextMenu final : public Control {
  public:
    using SelectHandler = std::function<void(const ContextMenuItem& item, std::size_t index)>;
    using IndexSelectHandler = std::function<void(std::size_t index)>;
    using DismissHandler = std::function<void()>;
    struct SelectEvent {
        const ContextMenuItem& item;
        std::size_t index = 0;
    };
    using SelectEventSignal = core::EventSignal<const SelectEvent&>;
    using DismissEventSignal = core::EventSignal<>;

    ContextMenu();
    ~ContextMenu() override;

    ContextMenu& set_items(std::vector<ContextMenuItem> items);
    ContextMenu& set_on_select(SelectHandler handler);
    ContextMenu& set_on_select(IndexSelectHandler handler);
    ContextMenu& set_on_dismiss(DismissHandler handler);
    [[nodiscard]] SelectEventSignal& selected() noexcept;
    [[nodiscard]] DismissEventSignal& dismissed() noexcept;
    ContextMenu& set_metrics(ContextMenuMetrics metrics);

    [[nodiscard]] const std::vector<ContextMenuItem>& items() const noexcept;
    [[nodiscard]] const ContextMenuMetrics& metrics() const noexcept;
    [[nodiscard]] layout::Size preferred_size() const noexcept;
    [[nodiscard]] std::optional<std::size_t> item_at(layout::Point local_position) const noexcept;
    [[nodiscard]] bool has_submenu(std::size_t index) const noexcept;

  protected:
    void on_pointer_event(elements::PointerEvent& event) override;
    void on_key_event(elements::KeyEvent& event) override;
    [[nodiscard]] bool on_animation_frame(animation::AnimationTimePoint now) override;
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;

  private:
    [[nodiscard]] std::optional<std::size_t> first_enabled_item() const noexcept;
    [[nodiscard]] std::optional<std::size_t> next_enabled_item(int direction) const noexcept;
    [[nodiscard]] layout::Size current_size() const noexcept;
    [[nodiscard]] layout::Rect submenu_bounds_for(std::size_t index) const noexcept;
    void open_submenu(std::size_t index);
    void dismiss_submenu() noexcept;
    void select_item(std::size_t index);
    void request_dismiss();
    [[nodiscard]] float animated_open_progress() const;

    struct EventState;
    [[nodiscard]] EventState& ensure_event_state();

    std::vector<ContextMenuItem> items_;
    ContextMenuMetrics metrics_{};
    elements::SvgIcon check_icon_;
    elements::SvgIcon submenu_icon_;
    std::optional<std::size_t> hovered_index_;
    std::optional<std::size_t> pressed_index_;
    ContextMenu* submenu_menu_ = nullptr;
    std::optional<std::size_t> submenu_parent_index_;
    AnimatedFloat open_progress_{0.92F};
    std::shared_ptr<bool> lifetime_token_ = std::make_shared<bool>(true);
    std::unique_ptr<EventState> event_state_;
};

} // namespace winelement::controls
