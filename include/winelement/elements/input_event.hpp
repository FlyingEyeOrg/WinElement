#pragma once

#include <winelement/layout/layout_types.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace winelement::elements {

class UIElement;

enum class PointerEventKind {
    Move,
    Down,
    Up,
    Click,
    DoubleClick,
    Wheel,
    HorizontalWheel,
    Cancel,
    Enter,
    Leave
};
enum class PointerButton { None, Primary, Secondary, Middle, X1, X2 };
enum class PointerCursor { Default, Arrow, Move, Hand };
enum class EventRoutePhase { Tunnel, Bubble };
enum class KeyEventKind {
    Down,
    Up,
    TextInput,
    CompositionStart,
    CompositionUpdate,
    CompositionEnd
};
enum class Key {
    Unknown,
    Tab,
    Enter,
    Space,
    Escape,
    Backspace,
    Delete,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    A,
    C,
    V,
    X,
    Z
};

struct KeyModifiers {
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool meta = false;
};

struct PointerEvent {
    PointerEventKind kind = PointerEventKind::Move;
    EventRoutePhase phase = EventRoutePhase::Bubble;
    layout::Point position{};
    layout::Point local_position{};
    layout::Point wheel_delta{};
    PointerButton button = PointerButton::None;
    std::uint8_t click_count = 0;
    bool primary_button_down = false;
    bool secondary_button_down = false;
    bool middle_button_down = false;
    bool x1_button_down = false;
    bool x2_button_down = false;
    KeyModifiers modifiers{};
    UIElement* target = nullptr;
    UIElement* current_target = nullptr;
    bool handled = false;
};

struct KeyEvent {
    KeyEventKind kind = KeyEventKind::Down;
    Key key = Key::Unknown;
    std::string text;
    KeyModifiers modifiers{};
    UIElement* target = nullptr;
    UIElement* current_target = nullptr;
    bool handled = false;
};

struct FocusChangeEvent {
    bool focused = false;
    bool focus_visible = false;
    bool focus_within = false;
};

struct RoutedEventResult {
    UIElement* target = nullptr;
    UIElement* handled_by = nullptr;
    EventRoutePhase handled_phase = EventRoutePhase::Bubble;
    bool handled = false;
};

enum class GestureDisposition { Pending, Accepted, Rejected };

class GestureRecognizer {
  public:
    virtual ~GestureRecognizer() = default;

    [[nodiscard]] virtual GestureDisposition route_pointer_event(const PointerEvent& event) = 0;
};

class TapGestureRecognizer final : public GestureRecognizer {
  public:
    using Handler = std::function<void(const PointerEvent& event)>;

    explicit TapGestureRecognizer(Handler handler) : handler_(std::move(handler)) {}

    [[nodiscard]] GestureDisposition route_pointer_event(const PointerEvent& event) override {
        if (event.kind == PointerEventKind::Down) {
            active_ = true;
            return GestureDisposition::Pending;
        }

        if (!active_) {
            return GestureDisposition::Pending;
        }

        if (event.kind == PointerEventKind::Up) {
            active_ = false;
            if (handler_) {
                handler_(event);
            }
            return GestureDisposition::Accepted;
        }

        if (event.kind == PointerEventKind::Cancel) {
            active_ = false;
            return GestureDisposition::Rejected;
        }

        return GestureDisposition::Pending;
    }

  private:
    Handler handler_;
    bool active_ = false;
};

class DragGestureRecognizer final : public GestureRecognizer {
  public:
    using Handler = std::function<void(const PointerEvent& event)>;

    explicit DragGestureRecognizer(Handler handler, float threshold = 4.0F)
        : handler_(std::move(handler)), threshold_(std::max(threshold, 0.0F)) {}

    [[nodiscard]] GestureDisposition route_pointer_event(const PointerEvent& event) override {
        if (event.kind == PointerEventKind::Down) {
            active_ = true;
            accepted_ = false;
            start_ = event.position;
            return GestureDisposition::Pending;
        }

        if (!active_) {
            return GestureDisposition::Pending;
        }

        if (event.kind == PointerEventKind::Move) {
            const auto dx = event.position.x - start_.x;
            const auto dy = event.position.y - start_.y;
            if (!accepted_ && dx * dx + dy * dy >= threshold_ * threshold_) {
                accepted_ = true;
                if (handler_) {
                    handler_(event);
                }
                return GestureDisposition::Accepted;
            }
            return accepted_ ? GestureDisposition::Accepted : GestureDisposition::Pending;
        }

        if (event.kind == PointerEventKind::Up) {
            const auto disposition =
                accepted_ ? GestureDisposition::Accepted : GestureDisposition::Rejected;
            active_ = false;
            accepted_ = false;
            return disposition;
        }

        if (event.kind == PointerEventKind::Cancel) {
            active_ = false;
            accepted_ = false;
            return GestureDisposition::Rejected;
        }

        return accepted_ ? GestureDisposition::Accepted : GestureDisposition::Pending;
    }

  private:
    Handler handler_;
    layout::Point start_{};
    float threshold_ = 4.0F;
    bool active_ = false;
    bool accepted_ = false;
};

class GestureArena final {
  public:
    void add(std::unique_ptr<GestureRecognizer> recognizer) {
        if (!recognizer) {
            return;
        }

        recognizers_.push_back(std::move(recognizer));
        dispositions_.push_back(GestureDisposition::Pending);
    }

    void route_pointer_event(const PointerEvent& event) {
        if (event.kind == PointerEventKind::Down) {
            accepted_recognizer_.reset();
            std::fill(dispositions_.begin(), dispositions_.end(), GestureDisposition::Pending);
        }

        if (accepted_recognizer_.has_value()) {
            static_cast<void>(recognizers_[*accepted_recognizer_]->route_pointer_event(event));
            return;
        }

        for (auto index = std::size_t{0}; index < recognizers_.size(); ++index) {
            if (dispositions_[index] != GestureDisposition::Pending) {
                continue;
            }

            const auto disposition = recognizers_[index]->route_pointer_event(event);
            if (disposition == GestureDisposition::Pending) {
                continue;
            }

            dispositions_[index] = disposition;
            if (disposition == GestureDisposition::Accepted) {
                accepted_recognizer_ = index;
                reject_pending_except(index);
                return;
            }
        }

        if (event.kind == PointerEventKind::Cancel) {
            reject_pending_except(std::nullopt);
        }
    }

    [[nodiscard]] std::optional<std::size_t> accepted_recognizer() const noexcept {
        return accepted_recognizer_;
    }

    [[nodiscard]] GestureDisposition disposition(std::size_t index) const noexcept {
        return index < dispositions_.size() ? dispositions_[index] : GestureDisposition::Rejected;
    }

  private:
    void reject_pending_except(std::optional<std::size_t> accepted_index) noexcept {
        for (auto index = std::size_t{0}; index < dispositions_.size(); ++index) {
            if (accepted_index.has_value() && index == *accepted_index) {
                continue;
            }
            if (dispositions_[index] == GestureDisposition::Pending) {
                dispositions_[index] = GestureDisposition::Rejected;
            }
        }
    }

    std::vector<std::unique_ptr<GestureRecognizer>> recognizers_;
    std::vector<GestureDisposition> dispositions_;
    std::optional<std::size_t> accepted_recognizer_;
};

} // namespace winelement::elements
