#pragma once

namespace winelement::animation {

enum class EasingCurve {
    Linear,
    StepStart,
    StepEnd,
    EaseInSine,
    EaseOutSine,
    EaseInOutSine,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseOutBack,
    EaseInOutBack
};

struct EasingFunction {
    EasingCurve curve = EasingCurve::Linear;
    float overshoot = 1.70158F;

    [[nodiscard]] friend constexpr bool operator==(EasingFunction,
                                                   EasingFunction) noexcept = default;

    [[nodiscard]] float evaluate(float progress) const noexcept;

    [[nodiscard]] static constexpr EasingFunction linear() noexcept {
        return EasingFunction{};
    }

    [[nodiscard]] static constexpr EasingFunction ease_out_cubic() noexcept {
        return EasingFunction{.curve = EasingCurve::EaseOutCubic};
    }

    [[nodiscard]] static constexpr EasingFunction ease_in_out_cubic() noexcept {
        return EasingFunction{.curve = EasingCurve::EaseInOutCubic};
    }

    [[nodiscard]] static constexpr EasingFunction
    ease_in_out_back(float overshoot_value = 1.70158F) noexcept {
        return EasingFunction{.curve = EasingCurve::EaseInOutBack, .overshoot = overshoot_value};
    }
};

[[nodiscard]] float apply_easing(EasingCurve curve, float progress,
                                 float overshoot = 1.70158F) noexcept;

} // namespace winelement::animation
