#pragma once

#include <winelement/animation/timeline.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace winelement::animation {

struct PhysicsSample {
    float value = 0.0F;
    float velocity = 0.0F;
    bool done = false;
};

class PhysicsSimulation {
  public:
    virtual ~PhysicsSimulation();

    PhysicsSimulation(const PhysicsSimulation&) = delete;
    PhysicsSimulation& operator=(const PhysicsSimulation&) = delete;
    PhysicsSimulation(PhysicsSimulation&&) = delete;
    PhysicsSimulation& operator=(PhysicsSimulation&&) = delete;

    [[nodiscard]] virtual PhysicsSample sample(AnimationDuration elapsed) const noexcept = 0;

  protected:
    PhysicsSimulation() = default;
};

class SpringSimulation final : public PhysicsSimulation {
  public:
    SpringSimulation(float from, float to, float initial_velocity = 0.0F, float stiffness = 220.0F,
                     float damping = 22.0F, float epsilon = 0.001F) noexcept;

    [[nodiscard]] PhysicsSample sample(AnimationDuration elapsed) const noexcept override;

  private:
    float from_ = 0.0F;
    float to_ = 0.0F;
    float initial_velocity_ = 0.0F;
    float stiffness_ = 220.0F;
    float damping_ = 22.0F;
    float epsilon_ = 0.001F;
};

class FrictionSimulation final : public PhysicsSimulation {
  public:
    FrictionSimulation(float position, float velocity, float drag = 4.2F,
                       float epsilon = 0.01F) noexcept;

    [[nodiscard]] PhysicsSample sample(AnimationDuration elapsed) const noexcept override;

  private:
    float position_ = 0.0F;
    float velocity_ = 0.0F;
    float drag_ = 4.2F;
    float epsilon_ = 0.01F;
};

class PhysicsAnimation final : public AnimationChannel {
  public:
    using Setter = std::function<void(float)>;

    PhysicsAnimation(std::unique_ptr<PhysicsSimulation> simulation, Setter setter);

    [[nodiscard]] bool apply(AnimationDuration elapsed) override;
    [[nodiscard]] bool is_finished(AnimationDuration elapsed) const noexcept override;

  private:
    std::unique_ptr<PhysicsSimulation> simulation_;
    Setter setter_;
};

[[nodiscard]] std::unique_ptr<PhysicsAnimation>
make_physics_animation(std::unique_ptr<PhysicsSimulation> simulation,
                       PhysicsAnimation::Setter setter);

} // namespace winelement::animation