#include <winelement/controls/context_menu.hpp>

#include "control_style.hpp"
#include "popup_menu_surface.hpp"

#include <winelement/elements/all_icons.hpp>
#include <winelement/elements/svg_icon.hpp>
#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace winelement::controls {
namespace {

[[nodiscard]] detail::PopupListMetrics to_popup_metrics(ContextMenuMetrics metrics) noexcept {
    return detail::sanitize_popup_metrics(
        detail::PopupListMetrics{.min_width = metrics.min_width,
                                 .max_width = metrics.max_width,
                                 .item_height = metrics.item_height,
                                 .vertical_padding = metrics.vertical_padding,
                                 .text_padding = metrics.text_padding,
                                 .font_size = metrics.font_size});
}

[[nodiscard]] bool item_can_activate(const ContextMenuItem& item) noexcept {
    return item.enabled && !item.separator;
}

[[nodiscard]] layout::Rect
menu_item_highlight_rect(layout::Rect item_rect, const style::UIElementStyle& menu_style) noexcept {
    const auto inset = std::max(std::ceil(std::max(menu_style.border_width, 0.0F)), 1.0F);
    return layout::Rect{item_rect.x + inset, item_rect.y,
                        std::max(0.0F, item_rect.width - inset * 2.0F), item_rect.height};
}

void paint_menu_item_highlight(rendering::RenderContext& context, layout::Rect item_rect,
                               const style::UIElementStyle& menu_style, rendering::Color color) {
    if (color.alpha == 0U) {
        return;
    }
    context.fill_rounded_rect(menu_item_highlight_rect(item_rect, menu_style),
                              rendering::CornerRadius::uniform(2.0F), color);
}

[[nodiscard]] std::size_t visible_item_count(const ContextMenuMetrics& metrics,
                                             std::size_t total_count) noexcept {
    return metrics.max_visible_items == 0U ? total_count
                                           : std::min(total_count, metrics.max_visible_items);
}

[[nodiscard]] const elements::icons::IconPathsBase* find_menu_icon(std::string_view name) noexcept {
    if (name.empty()) {
        return nullptr;
    }

    using IconIndex = std::unordered_map<std::string_view, const elements::icons::IconPathsBase*>;
    static const auto index = [] {
        auto icons = IconIndex{};
        for (const auto& entry : elements::icons::all_icons()) {
            icons.emplace(entry.name, entry.data);
            icons.emplace(entry.kebab_name, entry.data);
        }
        return icons;
    }();

    const auto iterator = index.find(name);
    return iterator == index.end() ? nullptr : iterator->second;
}

[[nodiscard]] float preferred_context_menu_width(const ContextMenuMetrics& metrics,
                                                 const std::vector<ContextMenuItem>& items) {
    auto width = metrics.min_width;
    const auto submenu_column_width = 18.0F;
    for (const auto& item : items) {
        if (item.separator) {
            continue;
        }
        const auto label_width = detail::estimated_popup_text_width(item.text, metrics.font_size);
        const auto shortcut_width =
            item.shortcut_text.empty()
                ? 0.0F
                : detail::estimated_popup_text_width(item.shortcut_text, metrics.font_size);
        const auto shortcut_gap = item.shortcut_text.empty() ? 0.0F : metrics.shortcut_padding;
        const auto submenu_width = item.submenu.empty() ? 0.0F : submenu_column_width;
        width = std::max(width, metrics.text_padding * 2.0F + metrics.icon_column_width +
                                    label_width + shortcut_gap + shortcut_width + submenu_width);
    }
    return std::clamp(width, metrics.min_width, metrics.max_width);
}

void paint_menu_adornment(rendering::RenderContext& context, const ContextMenuItem& item,
                          layout::Rect icon_rect, const style::UIElementStyle& menu_style,
                          rendering::Color color, const elements::SvgIcon& check_icon) {
    if (item.checkable && item.checked) {
        const auto size = std::min({14.0F, icon_rect.width, icon_rect.height});
        const auto adornment =
            layout::Rect{icon_rect.x + (icon_rect.width - size) * 0.5F,
                         icon_rect.y + (icon_rect.height - size) * 0.5F, size, size};
        if (item.radio) {
            context.stroke_ellipse(adornment, color, 1.5F);
            const auto inset = 4.0F;
            context.fill_ellipse(layout::Rect{adornment.x + inset, adornment.y + inset,
                                              std::max(0.0F, adornment.width - inset * 2.0F),
                                              std::max(0.0F, adornment.height - inset * 2.0F)},
                                 color);
        } else {
            check_icon.paint_icon(context, adornment, color);
        }
        return;
    }

    if (const auto* icon_data = find_menu_icon(item.icon_name)) {
        elements::SvgIcon icon;
        icon.set_icon_paths(*icon_data);
        const auto size = std::min({14.0F, icon_rect.width, icon_rect.height});
        icon.paint_icon(context,
                        layout::Rect{icon_rect.x + (icon_rect.width - size) * 0.5F,
                                     icon_rect.y + (icon_rect.height - size) * 0.5F, size, size},
                        color);
        return;
    }

    if (!item.icon_name.empty()) {
        const auto size = std::min({8.0F, icon_rect.width, icon_rect.height});
        context.fill_rounded_rect(layout::Rect{icon_rect.x + (icon_rect.width - size) * 0.5F,
                                               icon_rect.y + (icon_rect.height - size) * 0.5F, size,
                                               size},
                                  menu_style.corner_radius, color);
    }
}

void paint_context_menu_item(rendering::RenderContext& context, layout::Rect item_rect,
                             const ContextMenuItem& item, const style::UIElementStyle& menu_style,
                             const ContextMenuMetrics& menu_metrics,
                             detail::PopupItemPaintState state, const elements::SvgIcon& check_icon,
                             const elements::SvgIcon& submenu_icon) {
    if (item.separator) {
        const auto y = std::floor(item_rect.y + item_rect.height * 0.5F) + 0.5F;
        context.draw_line(
            layout::Point{item_rect.x + menu_metrics.text_padding, y},
            layout::Point{item_rect.x + item_rect.width - menu_metrics.text_padding, y},
            menu_style.border_color, 1.0F);
        return;
    }

    if (state.hovered && !state.disabled) {
        paint_menu_item_highlight(context, item_rect, menu_style, menu_style.hover_background);
    }
    if (state.pressed && !state.disabled) {
        paint_menu_item_highlight(context, item_rect, menu_style,
                                  rendering::Color::rgba(menu_style.focus_border_color.red,
                                                         menu_style.focus_border_color.green,
                                                         menu_style.focus_border_color.blue, 28));
    }

    const auto text_color =
        state.disabled ? menu_style.semantic.disabled_text : menu_style.text_color;
    const auto adornment_color =
        state.disabled ? menu_style.semantic.disabled_text : menu_style.focus_border_color;
    const auto icon_rect = layout::Rect{item_rect.x + menu_metrics.text_padding * 0.5F, item_rect.y,
                                        menu_metrics.icon_column_width, item_rect.height};
    paint_menu_adornment(context, item, icon_rect, menu_style, adornment_color, check_icon);

    const auto chevron_width = item.submenu.empty() ? 0.0F : 18.0F;
    const auto shortcut_width = item.shortcut_text.empty()
                                    ? 0.0F
                                    : std::min(detail::estimated_popup_text_width(
                                                   item.shortcut_text, menu_metrics.font_size),
                                               item_rect.width * 0.35F);
    const auto shortcut_gap = item.shortcut_text.empty() ? 0.0F : menu_metrics.shortcut_padding;
    const auto text_x = item_rect.x + menu_metrics.text_padding + menu_metrics.icon_column_width;
    const auto reserved_right =
        menu_metrics.text_padding + chevron_width + shortcut_width + shortcut_gap;
    const auto text_rect = layout::Rect{
        text_x, item_rect.y,
        std::max(0.0F, item_rect.x + item_rect.width - text_x - reserved_right), item_rect.height};
    context.draw_text(
        item.text, text_rect,
        rendering::TextStyle{.font_size = menu_metrics.font_size,
                             .color = text_color,
                             .alignment = rendering::TextAlignment::Start,
                             .vertical_alignment = rendering::TextVerticalAlignment::Center,
                             .wrapping = rendering::TextWrapping::NoWrap,
                             .trimming = rendering::TextTrimming::CharacterEllipsis});

    if (!item.shortcut_text.empty()) {
        const auto shortcut_rect =
            layout::Rect{item_rect.x + item_rect.width - menu_metrics.text_padding - chevron_width -
                             shortcut_width,
                         item_rect.y, shortcut_width, item_rect.height};
        context.draw_text(
            item.shortcut_text, shortcut_rect,
            rendering::TextStyle{.font_size = menu_metrics.font_size,
                                 .color = state.disabled ? menu_style.semantic.disabled_text
                                                         : menu_style.semantic.secondary_text,
                                 .alignment = rendering::TextAlignment::End,
                                 .vertical_alignment = rendering::TextVerticalAlignment::Center,
                                 .wrapping = rendering::TextWrapping::NoWrap,
                                 .trimming = rendering::TextTrimming::CharacterEllipsis});
    }

    if (!item.submenu.empty()) {
        const auto chevron_rect =
            layout::Rect{item_rect.x + item_rect.width - menu_metrics.text_padding - chevron_width,
                         item_rect.y, chevron_width, item_rect.height};
        const auto icon_color =
            state.disabled ? menu_style.semantic.disabled_text : menu_style.semantic.secondary_text;
        const auto size = std::min({12.0F, chevron_rect.width, chevron_rect.height});
        submenu_icon.paint_icon(context,
                                layout::Rect{chevron_rect.x + (chevron_rect.width - size) * 0.5F,
                                             chevron_rect.y + (chevron_rect.height - size) * 0.5F,
                                             size, size},
                                icon_color);
    }
}

} // namespace

struct ContextMenu::EventState {
    SelectEventSignal selected;
    DismissEventSignal dismissed;
    core::EventToken legacy_select_token = 0U;
    core::EventToken legacy_dismiss_token = 0U;
};

ContextMenu::ContextMenu() : Control() {
    apply_style_value(style::default_context_menu_style(), true);
    set_theme_class(style::theme_class::context_menu);
    set_focusable(true);
    check_icon_.set_icon_paths(elements::icons::Check);
    submenu_icon_.set_icon_paths(elements::icons::ArrowRight);
    open_progress_.animate_to(1.0F, animation::AnimationDuration{0.12F});
}

ContextMenu::~ContextMenu() {
    *lifetime_token_ = false;
    dismiss_submenu();
}

ContextMenu& ContextMenu::set_items(std::vector<ContextMenuItem> items) {
    dismiss_submenu();
    items_ = std::move(items);
    hovered_index_ = first_enabled_item();
    pressed_index_.reset();
    configure_layout([this](layout::LayoutElement& layout) {
        const auto size = preferred_size();
        layout.set_size(layout::Length::points(size.width), layout::Length::points(size.height));
    });
    invalidate_paint();
    return *this;
}

ContextMenu& ContextMenu::set_on_select(SelectHandler handler) {
    auto& state = ensure_event_state();
    core::replace_handler_subscription(
        state.selected, state.legacy_select_token,
        handler ? SelectEventSignal::Handler{
                      [handler = std::move(handler)](const SelectEvent& event) {
                          handler(event.item, event.index);
                      }}
                : SelectEventSignal::Handler{});
    return *this;
}

ContextMenu& ContextMenu::set_on_select(IndexSelectHandler handler) {
    return set_on_select(handler ? SelectHandler{[handler = std::move(handler)](
                                                     const ContextMenuItem&, std::size_t index) {
                                      handler(index);
                                  }}
                                 : SelectHandler{});
}

ContextMenu& ContextMenu::set_on_dismiss(DismissHandler handler) {
    auto& state = ensure_event_state();
    core::replace_handler_subscription(
        state.dismissed, state.legacy_dismiss_token,
        handler ? DismissEventSignal::Handler{std::move(handler)} : DismissEventSignal::Handler{});
    return *this;
}

ContextMenu::SelectEventSignal& ContextMenu::selected() noexcept {
    return ensure_event_state().selected;
}

ContextMenu::DismissEventSignal& ContextMenu::dismissed() noexcept {
    return ensure_event_state().dismissed;
}

ContextMenu::EventState& ContextMenu::ensure_event_state() {
    if (event_state_ == nullptr) {
        event_state_ = std::make_unique<EventState>();
    }
    return *event_state_;
}

ContextMenu& ContextMenu::set_metrics(ContextMenuMetrics metrics) {
    dismiss_submenu();
    metrics_ = metrics;
    configure_layout([this](layout::LayoutElement& layout) {
        const auto size = preferred_size();
        layout.set_size(layout::Length::points(size.width), layout::Length::points(size.height));
    });
    invalidate_paint();
    return *this;
}

const std::vector<ContextMenuItem>& ContextMenu::items() const noexcept {
    return items_;
}

const ContextMenuMetrics& ContextMenu::metrics() const noexcept {
    return metrics_;
}

layout::Size ContextMenu::preferred_size() const noexcept {
    const auto metrics = to_popup_metrics(metrics_);
    const auto width = preferred_context_menu_width(metrics_, items_);
    const auto visible_count = visible_item_count(metrics_, items_.size());
    return detail::preferred_popup_list_size(metrics, visible_count, width);
}

layout::Size ContextMenu::current_size() const noexcept {
    const auto preferred = preferred_size();
    const auto current = frame();
    return layout::Size{current.width > 0.0F ? current.width : preferred.width,
                        current.height > 0.0F ? current.height : preferred.height};
}

std::optional<std::size_t> ContextMenu::item_at(layout::Point local_position) const noexcept {
    const auto size = current_size();
    const auto frame = layout::Rect{0.0F, 0.0F, size.width, size.height};
    if (!detail::popup_contains_local_point(frame, local_position)) {
        return std::nullopt;
    }
    const auto index = detail::popup_item_at(local_position, to_popup_metrics(metrics_),
                                             visible_item_count(metrics_, items_.size()));
    if (!index || !item_can_activate(items_[*index])) {
        return std::nullopt;
    }
    return index;
}

bool ContextMenu::has_submenu(std::size_t index) const noexcept {
    return index < items_.size() && !items_[index].submenu.empty();
}

layout::Rect ContextMenu::submenu_bounds_for(std::size_t index) const noexcept {
    const auto metrics = to_popup_metrics(metrics_);
    const auto size = current_size();
    const auto absolute = absolute_frame();
    const auto width = preferred_context_menu_width(metrics_, items_[index].submenu);
    const auto submenu_size = detail::preferred_popup_list_size(
        metrics, visible_item_count(metrics_, items_[index].submenu.size()), width);
    const auto item_top =
        metrics.vertical_padding + static_cast<float>(index) * metrics.item_height;
    return layout::Rect{absolute.x + size.width - 6.0F,
                        absolute.y + std::max(0.0F, item_top - metrics.vertical_padding),
                        submenu_size.width, submenu_size.height};
}

void ContextMenu::open_submenu(std::size_t index) {
    if (!has_submenu(index)) {
        dismiss_submenu();
        return;
    }

    if (submenu_parent_index_ == index && submenu_menu_ != nullptr) {
        set_top_layer_bounds(*submenu_menu_, submenu_bounds_for(index));
        bring_top_layer_to_front(*submenu_menu_);
        return;
    }

    dismiss_submenu();

    auto submenu = std::make_unique<ContextMenu>();
    submenu->set_metrics(metrics_).set_items(items_[index].submenu);

    auto weak_lifetime = std::weak_ptr<bool>{lifetime_token_};
    auto* owner = this;
    submenu->selected() += [weak_lifetime, owner](const ContextMenu::SelectEvent& event) {
            const auto alive = weak_lifetime.lock();
            if (alive == nullptr || !*alive) {
                return;
            }
            if (owner->event_state_ != nullptr && !owner->event_state_->selected.empty()) {
                owner->event_state_->selected.emit(event);
            }
            owner->request_dismiss();
        };
    submenu->dismissed() += [weak_lifetime, owner]() {
        const auto alive = weak_lifetime.lock();
        if (alive == nullptr || !*alive) {
            return;
        }
        owner->dismiss_submenu();
    };

    auto& submenu_ref = static_cast<ContextMenu&>(push_top_layer(
        std::move(submenu), elements::TopLayerOptions{.bounds = submenu_bounds_for(index),
                                                      .light_dismiss = true,
                                                      .preserve_focus = true,
                                                      .close_on_escape = false,
                                                      .on_dismissed =
                                                          [weak_lifetime, owner]() {
                                                              const auto alive =
                                                                  weak_lifetime.lock();
                                                              if (alive == nullptr || !*alive) {
                                                                  return;
                                                              }
                                                              owner->submenu_menu_ = nullptr;
                                                              owner->submenu_parent_index_.reset();
                                                              owner->invalidate_paint();
                                                          },
                                                      .logical_owner = this}));
    submenu_menu_ = &submenu_ref;
    submenu_parent_index_ = index;
    invalidate_paint();
}

void ContextMenu::dismiss_submenu() noexcept {
    if (submenu_menu_ == nullptr) {
        submenu_parent_index_.reset();
        return;
    }

    auto* submenu = submenu_menu_;
    submenu_menu_ = nullptr;
    submenu_parent_index_.reset();
    try {
        submenu->dismiss_own_top_layer();
    } catch (...) {
    }
    invalidate_paint();
}

void ContextMenu::on_pointer_event(elements::PointerEvent& event) {
    const auto index = item_at(event.local_position);
    switch (event.kind) {
    case elements::PointerEventKind::Move:
        if (index && has_submenu(*index)) {
            open_submenu(*index);
        } else if (submenu_menu_ != nullptr) {
            dismiss_submenu();
        }
        if (hovered_index_ == index) {
            event.handled = true;
            return;
        }
        hovered_index_ = index;
        event.handled = true;
        invalidate_paint();
        return;
    case elements::PointerEventKind::Down:
    case elements::PointerEventKind::DoubleClick:
        if (!index) {
            request_dismiss();
            event.handled = true;
            return;
        }
        hovered_index_ = index;
        if (has_submenu(*index)) {
            pressed_index_.reset();
            open_submenu(*index);
            event.handled = true;
            invalidate_paint();
            return;
        }
        pressed_index_ = event.button == elements::PointerButton::Primary && items_[*index].enabled
                             ? index
                             : std::nullopt;
        event.handled = true;
        invalidate_paint();
        return;
    case elements::PointerEventKind::Up:
        if (!index) {
            if (submenu_menu_ == nullptr) {
                request_dismiss();
            }
            event.handled = true;
            return;
        }
        if (has_submenu(*index)) {
            hovered_index_ = index;
            pressed_index_.reset();
            open_submenu(*index);
            event.handled = true;
            invalidate_paint();
            return;
        }
        if (event.button == elements::PointerButton::Primary && pressed_index_ == index &&
            items_[*index].enabled) {
            select_item(*index);
        }
        hovered_index_ = index;
        pressed_index_.reset();
        event.handled = true;
        invalidate_paint();
        return;
    case elements::PointerEventKind::Cancel:
        pressed_index_.reset();
        request_dismiss();
        event.handled = true;
        return;
    case elements::PointerEventKind::Leave:
        if (submenu_parent_index_) {
            hovered_index_ = submenu_parent_index_;
            pressed_index_.reset();
            event.handled = true;
            invalidate_paint();
            return;
        }
        if (!hovered_index_ && !pressed_index_) {
            event.handled = true;
            return;
        }
        hovered_index_.reset();
        pressed_index_.reset();
        event.handled = true;
        invalidate_paint();
        return;
    case elements::PointerEventKind::Wheel:
    case elements::PointerEventKind::HorizontalWheel:
        event.handled = detail::popup_contains_local_point(
            layout::Rect{0.0F, 0.0F, current_size().width, current_size().height},
            event.local_position);
        return;
    case elements::PointerEventKind::Click:
    case elements::PointerEventKind::Enter:
        return;
    }
}

void ContextMenu::on_key_event(elements::KeyEvent& event) {
    if (event.kind != elements::KeyEventKind::Down) {
        return;
    }

    switch (event.key) {
    case elements::Key::Escape:
        request_dismiss();
        event.handled = true;
        return;
    case elements::Key::Up:
        if (const auto next_index = next_enabled_item(-1); hovered_index_ != next_index) {
            hovered_index_ = next_index;
            invalidate_paint();
        }
        pressed_index_.reset();
        event.handled = true;
        return;
    case elements::Key::Down:
        if (const auto next_index = next_enabled_item(1); hovered_index_ != next_index) {
            hovered_index_ = next_index;
            invalidate_paint();
        }
        pressed_index_.reset();
        event.handled = true;
        return;
    case elements::Key::Enter:
    case elements::Key::Space:
        if (const auto index_to_select = hovered_index_ && items_[*hovered_index_].enabled
                                             ? hovered_index_
                                             : first_enabled_item()) {
            select_item(*index_to_select);
        }
        event.handled = true;
        return;
    case elements::Key::Right:
        if (hovered_index_ && has_submenu(*hovered_index_)) {
            open_submenu(*hovered_index_);
            event.handled = true;
        }
        return;
    case elements::Key::Left:
        if (submenu_menu_ != nullptr) {
            dismiss_submenu();
            event.handled = true;
        }
        return;
    case elements::Key::Unknown:
    case elements::Key::Tab:
    case elements::Key::Backspace:
    case elements::Key::Delete:
    case elements::Key::Home:
    case elements::Key::End:
    case elements::Key::A:
    case elements::Key::C:
    case elements::Key::V:
    case elements::Key::X:
    case elements::Key::Z:
        return;
    }
}

bool ContextMenu::on_animation_frame(animation::AnimationTimePoint now) {
    const auto active = open_progress_.tick(now);
    if (active) {
        invalidate_paint();
    }
    return active;
}

void ContextMenu::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    const auto& menu_style = style_storage();
    const auto metrics = to_popup_metrics(metrics_);
    detail::paint_popup_surface(context, absolute_frame, menu_style, animated_open_progress());

    auto item_y = absolute_frame.y + metrics.vertical_padding;
    const auto visible_count = visible_item_count(metrics_, items_.size());
    for (auto index = std::size_t{0}; index < visible_count; ++index) {
        const auto& item = items_[index];
        const auto item_rect =
            layout::Rect{absolute_frame.x, item_y, absolute_frame.width, metrics.item_height};
        paint_context_menu_item(
            context, item_rect, item, menu_style, metrics_,
            detail::PopupItemPaintState{.hovered = hovered_index_ == index ||
                                                   submenu_parent_index_ == index,
                                        .pressed = pressed_index_ == index,
                                        .selected = item.checked,
                                        .disabled = !item.enabled},
            check_icon_, submenu_icon_);
        item_y += metrics.item_height;
    }
    context.pop_layer();
}

std::optional<std::size_t> ContextMenu::first_enabled_item() const noexcept {
    return detail::next_enabled_popup_item(
        visible_item_count(metrics_, items_.size()), std::nullopt, 1,
        [this](std::size_t index) noexcept { return item_can_activate(items_[index]); });
}

std::optional<std::size_t> ContextMenu::next_enabled_item(int direction) const noexcept {
    return detail::next_enabled_popup_item(
        visible_item_count(metrics_, items_.size()), hovered_index_, direction,
        [this](std::size_t index) noexcept { return item_can_activate(items_[index]); });
}

void ContextMenu::select_item(std::size_t index) {
    if (index >= items_.size() || !item_can_activate(items_[index])) {
        return;
    }

    if (has_submenu(index)) {
        open_submenu(index);
        return;
    }

    if (event_state_ != nullptr && !event_state_->selected.empty()) {
        const auto event = SelectEvent{.item = items_[index], .index = index};
        event_state_->selected.emit(event);
    }
    request_dismiss();
}

void ContextMenu::request_dismiss() {
    dismiss_submenu();
    hovered_index_.reset();
    pressed_index_.reset();
    if (event_state_ != nullptr && !event_state_->dismissed.empty()) {
        event_state_->dismissed.emit();
    }
}

float ContextMenu::animated_open_progress() const {
    return std::clamp(open_progress_.value(), 0.0F, 1.0F);
}

} // namespace winelement::controls
