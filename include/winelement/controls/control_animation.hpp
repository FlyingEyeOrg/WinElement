#pragma once

#include <winelement/animation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace winelement::controls {

class AnimatedFloat final {
  public:
    explicit AnimatedFloat(float value = 0.0F) noexcept : value_(sanitize(value)) {}

    [[nodiscard]] float value() const noexcept {
        static_cast<void>(tick(animation::AnimationClockType::now()));
        return value_;
    }

    [[nodiscard]] bool running() const noexcept {
        return !animation_.empty() && animation_.state() == animation::AnimationPlayState::Running;
    }

    void set(float value) noexcept {
        animation_.clear();
        value_ = sanitize(value);
    }

    void animate_to(
        float target, animation::AnimationDuration duration = animation::AnimationDuration{0.15F},
        animation::EasingFunction easing = animation::EasingFunction::ease_out_cubic()) noexcept {
        const auto now = animation::AnimationClockType::now();
        static_cast<void>(tick(now));
        const auto sanitized_target = sanitize(target);
        if (std::abs(value_ - sanitized_target) <= 0.0001F) {
            set(sanitized_target);
            return;
        }

        try {
            animation_.clear();
            animation_.animate<float>(value_, sanitized_target,
                                      animation::make_transition_timing(duration, easing),
                                      [this](const float& next) { value_ = sanitize(next); });
            animation_.play(now);
        } catch (...) {
            set(sanitized_target);
        }
    }

    void
    animate_loop(animation::AnimationDuration duration,
                 animation::EasingFunction easing = animation::EasingFunction::linear()) noexcept {
        try {
            animation_.clear();
            value_ = 0.0F;
            auto timing =
                animation::make_transition_timing(duration, easing, animation::FillMode::None);
            timing.iteration_count = std::numeric_limits<float>::infinity();
            animation_.animate<float>(0.0F, 1.0F, timing,
                                      [this](const float& next) { value_ = sanitize(next); });
            animation_.play(animation::AnimationClockType::now());
        } catch (...) {
            set(0.0F);
        }
    }

    [[nodiscard]] bool tick(animation::AnimationTimePoint now) const noexcept {
        if (animation_.empty()) {
            return false;
        }

        const auto previous = value_;
        const auto active = animation_.tick(now);
        return active || std::abs(value_ - previous) > 0.0001F;
    }

    void clear() noexcept {
        animation_.clear();
    }

  private:
    [[nodiscard]] static float sanitize(float value) noexcept {
        return std::isfinite(value) ? value : 0.0F;
    }

    mutable animation::Storyboard animation_;
    mutable float value_ = 0.0F;
};

} // namespace winelement::controls