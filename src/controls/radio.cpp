#include <winelement/controls/radio.hpp>

#include "control_style.hpp"

#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <utility>

namespace winelement::controls {

namespace {
constexpr auto indicator_extent = 14.0F;
constexpr auto indicator_gap = 8.0F;

[[nodiscard]] std::string next_radio_value() {
    static std::atomic_uint64_t next_value{1U};
    return "radio." + std::to_string(next_value.fetch_add(1U, std::memory_order_relaxed));
}
} // namespace

RadioGroupContext::~RadioGroupContext() {
    for (auto* radio : radios_) {
        if (radio != nullptr && radio->group_.get() == this) {
            radio->group_.reset();
        }
    }
}

RadioGroupContext& RadioGroupContext::set_value(std::string_view value) {
    if (has_value_ && value_ == value) {
        return *this;
    }
    has_value_ = true;
    value_ = std::string(value);
    sync_radios(true);
    if (change_handler_) {
        change_handler_(value_);
    }
    return *this;
}

RadioGroupContext& RadioGroupContext::clear_value() {
    if (!has_value_) {
        return *this;
    }
    has_value_ = false;
    value_.clear();
    sync_radios(true);
    if (change_handler_) {
        change_handler_(value_);
    }
    return *this;
}

RadioGroupContext& RadioGroupContext::set_on_change(ChangeHandler handler) {
    change_handler_ = std::move(handler);
    return *this;
}

const std::string& RadioGroupContext::value() const noexcept {
    return value_;
}

bool RadioGroupContext::has_value() const noexcept {
    return has_value_;
}

void RadioGroupContext::register_radio(Radio& radio) {
    if (std::find(radios_.begin(), radios_.end(), &radio) == radios_.end()) {
        radios_.push_back(&radio);
    }
    radio.set_checked_from_group(has_value_ && radio.value_ == value_, false);
}

void RadioGroupContext::unregister_radio(Radio& radio) noexcept {
    radios_.erase(std::remove(radios_.begin(), radios_.end(), &radio), radios_.end());
}

bool RadioGroupContext::select(std::string_view value) {
    const auto changed = !has_value_ || value_ != value;
    set_value(value);
    return changed;
}

bool RadioGroupContext::move_selection(const Radio& current, int direction) {
    if (radios_.empty() || direction == 0) {
        return false;
    }

    const auto iterator = std::find(radios_.begin(), radios_.end(), &current);
    const auto start_index =
        iterator == radios_.end()
            ? std::size_t{0U}
            : static_cast<std::size_t>(std::distance(radios_.begin(), iterator));
    for (std::size_t step = 1; step <= radios_.size(); ++step) {
        const auto offset = direction > 0 ? step : radios_.size() - step;
        const auto candidate_index = (start_index + offset) % radios_.size();
        auto* candidate = radios_[candidate_index];
        if (candidate != nullptr && !candidate->disabled_) {
            return select(candidate->value_);
        }
    }
    return false;
}

void RadioGroupContext::sync_radios(bool notify_radios) {
    for (auto* radio : radios_) {
        if (radio != nullptr) {
            radio->set_checked_from_group(has_value_ && radio->value_ == value_, notify_radios);
        }
    }
}

Radio::Radio() : Control(), value_(next_radio_value()) {
    apply_style_value(style::default_radio_style(), true);
    set_theme_class(style::theme_class::radio);
    set_focusable(true);
    update_measure_callback();
}

Radio::~Radio() {
    if (group_ != nullptr) {
        group_->unregister_radio(*this);
    }
}

Radio& Radio::set_text(std::string_view text) {
    UIElement::set_text(text);
    mark_measure_dirty();
    return *this;
}

Radio& Radio::set_value(std::string_view value) {
    if (value_ == value) {
        return *this;
    }
    value_ = std::string(value);
    if (group_ != nullptr) {
        if (checked_) {
            group_->select(value_);
        } else {
            set_checked_from_group(group_->has_value() && group_->value() == value_, false);
        }
    }
    return *this;
}

Radio& Radio::set_group(std::shared_ptr<RadioGroupContext> group) {
    if (group_ == group) {
        return *this;
    }
    if (group_ != nullptr) {
        group_->unregister_radio(*this);
    }
    group_ = std::move(group);
    if (group_ != nullptr) {
        group_->register_radio(*this);
    }
    return *this;
}

Radio& Radio::set_checked(bool checked) {
    if (group_ != nullptr) {
        if (checked) {
            group_->select(value_);
        } else if (checked_ && group_->has_value() && group_->value() == value_) {
            group_->clear_value();
        }
        return *this;
    }

    set_checked_from_group(checked, true);
    return *this;
}

void Radio::set_checked_from_group(bool checked, bool notify) {
    if (checked_ == checked) {
        return;
    }

    checked_ = checked;
    animate_checked(checked_ ? 1.0F : 0.0F);
    invalidate_paint();
    if (notify && change_handler_) {
        change_handler_(checked_);
    }
}

Radio& Radio::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return *this;
    }

    UIElement::set_disabled(disabled);
    invalidate_paint();
    return *this;
}

Radio& Radio::set_on_change(ChangeHandler handler) {
    change_handler_ = std::move(handler);
    return *this;
}

Radio& Radio::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    mark_measure_dirty();
    return *this;
}

bool Radio::checked() const noexcept {
    return checked_;
}

bool Radio::disabled() const noexcept {
    return disabled_;
}

const std::string& Radio::value() const noexcept {
    return value_;
}

std::shared_ptr<RadioGroupContext> Radio::group() const noexcept {
    return group_;
}

void Radio::on_pointer_event(elements::PointerEvent& event) {
    if (event.kind == elements::PointerEventKind::Enter) {
        hovered_ = true;
        animate_hover(1.0F);
        invalidate_paint();
        return;
    }
    if (event.kind == elements::PointerEventKind::Leave) {
        hovered_ = false;
        animate_hover(0.0F);
        invalidate_paint();
        return;
    }
    if (disabled_ || event.button != elements::PointerButton::Primary) {
        return;
    }
    if (event.kind == elements::PointerEventKind::Down ||
        event.kind == elements::PointerEventKind::DoubleClick) {
        event.handled = true;
        return;
    }
    if (event.kind == elements::PointerEventKind::Up) {
        activate();
        event.handled = true;
    }
}

void Radio::on_key_event(elements::KeyEvent& event) {
    if (disabled_ || event.kind != elements::KeyEventKind::Down) {
        return;
    }
    if (event.key == elements::Key::Space || event.key == elements::Key::Enter) {
        activate();
        event.handled = true;
    } else if (group_ != nullptr &&
               (event.key == elements::Key::Right || event.key == elements::Key::Down ||
                event.key == elements::Key::Left || event.key == elements::Key::Up)) {
        const auto direction =
            event.key == elements::Key::Right || event.key == elements::Key::Down ? 1 : -1;
        event.handled = group_->move_selection(*this, direction);
    }
}

bool Radio::on_animation_frame(animation::AnimationTimePoint now) {
    auto active = checked_progress_.tick(now);
    active = hover_progress_.tick(now) || active;
    if (active) {
        invalidate_paint();
    }
    return active;
}

void Radio::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    const auto center_y = absolute_frame.y + absolute_frame.height * 0.5F;
    const auto indicator = layout::Rect{absolute_frame.x, center_y - indicator_extent * 0.5F,
                                        indicator_extent, indicator_extent};
    const auto checked_progress = animated_checked_progress();
    const auto hover_progress = std::max(animated_hover_progress(), focused() ? 1.0F : 0.0F);
    const auto border = animation::interpolate_value(style_storage().border_color,
                                                     style_storage().focus_border_color,
                                                     std::max(checked_progress, hover_progress));
    const auto text_color =
        disabled_ ? style_storage().semantic.disabled_text : style_storage().text_color;

    context.fill_ellipse(indicator, disabled_ ? style_storage().read_only_background
                                              : style_storage().background);
    context.stroke_ellipse(indicator, border, style_storage().border_width);
    if (checked_progress > 0.0F) {
        const auto inset = 4.0F + (1.0F - checked_progress) * 3.0F;
        const auto inner = layout::Rect{indicator.x + inset, indicator.y + inset,
                                        std::max(0.0F, indicator.width - inset * 2.0F),
                                        std::max(0.0F, indicator.height - inset * 2.0F)};
        const auto active =
            disabled_ ? style_storage().semantic.disabled_text : style_storage().active_background;
        context.fill_ellipse(inner, rendering::Color::rgba(active.red, active.green, active.blue,
                                                           static_cast<std::uint8_t>(std::round(
                                                               active.alpha * checked_progress))));
    }

    const auto text_rect =
        layout::Rect{absolute_frame.x + indicator_extent + indicator_gap, absolute_frame.y,
                     std::max(0.0F, absolute_frame.width - indicator_extent - indicator_gap),
                     absolute_frame.height};
    context.draw_text(
        text_storage(), text_rect,
        rendering::TextStyle{.font_size = style_storage().font_size,
                             .color = text_color,
                             .vertical_alignment = rendering::TextVerticalAlignment::Center,
                             .wrapping = rendering::TextWrapping::NoWrap,
                             .trimming = rendering::TextTrimming::CharacterEllipsis});
}

void Radio::update_measure_callback() {
    set_measure_callback([this](const layout::MeasureInput&) {
        const auto text_size = text_engine().measure_single_line(
            text_storage(), rendering::TextStyle{.font_size = style_storage().font_size,
                                                 .color = style_storage().text_color});
        return layout::Size{
            indicator_extent + indicator_gap + text_size.width,
            std::max(style_storage().min_height, std::max(indicator_extent, text_size.height))};
    });
}

float Radio::animated_checked_progress() const {
    return std::clamp(checked_progress_.value(), 0.0F, 1.0F);
}

float Radio::animated_hover_progress() const {
    return std::clamp(hover_progress_.value(), 0.0F, 1.0F);
}

void Radio::animate_checked(float target) {
    checked_progress_.animate_to(target, animation::AnimationDuration{0.14F});
}

void Radio::animate_hover(float target) {
    hover_progress_.animate_to(target);
}

void Radio::activate() {
    if (group_ != nullptr) {
        if (checked_ && group_->has_value() && group_->value() == value_) {
            group_->clear_value();
        } else {
            group_->select(value_);
        }
    } else {
        set_checked_from_group(!checked_, true);
    }
}

} // namespace winelement::controls
