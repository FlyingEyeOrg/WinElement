#include <winelement/animation/physics.hpp>

#include <stdexcept>
#include <utility>

namespace winelement::animation {
namespace {

[[nodiscard]] float finite_or(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

} // namespace

PhysicsSimulation::~PhysicsSimulation() = default;

SpringSimulation::SpringSimulation(float from, float to, float initial_velocity, float stiffness,
                                   float damping, float epsilon) noexcept
    : from_(finite_or(from, 0.0F)), to_(finite_or(to, 0.0F)),
      initial_velocity_(finite_or(initial_velocity, 0.0F)),
      stiffness_(std::max(finite_or(stiffness, 220.0F), 1.0F)),
      damping_(std::max(finite_or(damping, 22.0F), 0.0F)),
      epsilon_(std::max(finite_or(epsilon, 0.001F), 0.000001F)) {}

PhysicsSample SpringSimulation::sample(AnimationDuration elapsed) const noexcept {
    const auto t = std::max(finite_or(elapsed.count(), 0.0F), 0.0F);
    const auto displacement = from_ - to_;
    const auto omega = std::sqrt(stiffness_);
    const auto damping_ratio = damping_ / (2.0F * omega);

    float value = to_;
    float velocity = 0.0F;
    if (damping_ratio < 1.0F) {
        const auto damped_omega =
            omega * std::sqrt(std::max(0.0F, 1.0F - damping_ratio * damping_ratio));
        const auto envelope = std::exp(-damping_ratio * omega * t);
        const auto c2 = (initial_velocity_ + damping_ratio * omega * displacement) / damped_omega;
        const auto cos_term = std::cos(damped_omega * t);
        const auto sin_term = std::sin(damped_omega * t);
        value = to_ + envelope * (displacement * cos_term + c2 * sin_term);
        velocity =
            envelope * ((c2 * damped_omega - damping_ratio * omega * displacement) * cos_term +
                        (-displacement * damped_omega - damping_ratio * omega * c2) * sin_term);
    } else {
        const auto envelope = std::exp(-omega * t);
        const auto c2 = initial_velocity_ + omega * displacement;
        value = to_ + envelope * (displacement + c2 * t);
        velocity = envelope * (c2 - omega * (displacement + c2 * t));
    }

    const auto done = std::abs(value - to_) <= epsilon_ && std::abs(velocity) <= epsilon_;
    return PhysicsSample{.value = done ? to_ : value, .velocity = velocity, .done = done};
}

FrictionSimulation::FrictionSimulation(float position, float velocity, float drag,
                                       float epsilon) noexcept
    : position_(finite_or(position, 0.0F)), velocity_(finite_or(velocity, 0.0F)),
      drag_(std::max(finite_or(drag, 4.2F), 0.000001F)),
      epsilon_(std::max(finite_or(epsilon, 0.01F), 0.000001F)) {}

PhysicsSample FrictionSimulation::sample(AnimationDuration elapsed) const noexcept {
    const auto t = std::max(finite_or(elapsed.count(), 0.0F), 0.0F);
    const auto decay = std::exp(-drag_ * t);
    const auto velocity = velocity_ * decay;
    const auto value = position_ + (velocity_ / drag_) * (1.0F - decay);
    return PhysicsSample{
        .value = value, .velocity = velocity, .done = std::abs(velocity) <= epsilon_};
}

PhysicsAnimation::PhysicsAnimation(std::unique_ptr<PhysicsSimulation> simulation, Setter setter)
    : simulation_(std::move(simulation)), setter_(std::move(setter)) {
    if (simulation_ == nullptr) {
        throw std::invalid_argument("physics animation requires a simulation");
    }
}

bool PhysicsAnimation::apply(AnimationDuration elapsed) {
    const auto sample = simulation_->sample(elapsed);
    if (setter_) {
        setter_(sample.value);
    }
    return !sample.done;
}

bool PhysicsAnimation::is_finished(AnimationDuration elapsed) const noexcept {
    return simulation_->sample(elapsed).done;
}

std::unique_ptr<PhysicsAnimation>
make_physics_animation(std::unique_ptr<PhysicsSimulation> simulation,
                       PhysicsAnimation::Setter setter) {
    return std::make_unique<PhysicsAnimation>(std::move(simulation), std::move(setter));
}

} // namespace winelement::animation