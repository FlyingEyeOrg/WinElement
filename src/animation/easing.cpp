#include <winelement/animation/easing.hpp>

#include <algorithm>
#include <cmath>

namespace winelement::animation {
namespace {

constexpr auto pi = 3.14159265358979323846F;

[[nodiscard]] float normalized(float progress) noexcept {
    return std::isfinite(progress) ? std::clamp(progress, 0.0F, 1.0F) : 0.0F;
}

} // namespace

float EasingFunction::evaluate(float progress) const noexcept {
    return apply_easing(curve, progress, overshoot);
}

float apply_easing(EasingCurve curve, float progress, float overshoot) noexcept {
    const auto value = normalized(progress);
    switch (curve) {
    case EasingCurve::Linear:
        return value;
    case EasingCurve::StepStart:
        return value <= 0.0F ? 0.0F : 1.0F;
    case EasingCurve::StepEnd:
        return value >= 1.0F ? 1.0F : 0.0F;
    case EasingCurve::EaseInSine:
        return 1.0F - std::cos((value * pi) * 0.5F);
    case EasingCurve::EaseOutSine:
        return std::sin((value * pi) * 0.5F);
    case EasingCurve::EaseInOutSine:
        return -(std::cos(pi * value) - 1.0F) * 0.5F;
    case EasingCurve::EaseInQuad:
        return value * value;
    case EasingCurve::EaseOutQuad:
        return 1.0F - (1.0F - value) * (1.0F - value);
    case EasingCurve::EaseInOutQuad:
        if (value < 0.5F) {
            return 2.0F * value * value;
        }
        {
            const auto shifted = -2.0F * value + 2.0F;
            return 1.0F - shifted * shifted * 0.5F;
        }
    case EasingCurve::EaseInCubic:
        return value * value * value;
    case EasingCurve::EaseOutCubic: {
        const auto shifted = 1.0F - value;
        return 1.0F - shifted * shifted * shifted;
    }
    case EasingCurve::EaseInOutCubic:
        if (value < 0.5F) {
            return 4.0F * value * value * value;
        }
        {
            const auto shifted = -2.0F * value + 2.0F;
            return 1.0F - shifted * shifted * shifted * 0.5F;
        }
    case EasingCurve::EaseOutBack: {
        const auto amount = std::isfinite(overshoot) ? overshoot : 1.70158F;
        const auto shifted = value - 1.0F;
        return 1.0F + (amount + 1.0F) * shifted * shifted * shifted + amount * shifted * shifted;
    }
    case EasingCurve::EaseInOutBack: {
        const auto amount = (std::isfinite(overshoot) ? overshoot : 1.70158F) * 1.525F;
        if (value < 0.5F) {
            const auto scaled = value * 2.0F;
            return (scaled * scaled * ((amount + 1.0F) * scaled - amount)) * 0.5F;
        }
        const auto scaled = value * 2.0F - 2.0F;
        return (scaled * scaled * ((amount + 1.0F) * scaled + amount) + 2.0F) * 0.5F;
    }
    }

    return value;
}

} // namespace winelement::animation
