#include <winelement/animation.hpp>
#include <winelement/layout/layout_types.hpp>
#include <winelement/rendering/render_types.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <limits>

namespace {

using namespace winelement::animation;
using namespace winelement::layout;
using namespace winelement::rendering;

TEST(AnimationTests, EasingCurvesClampAndShapeProgress) {
    EXPECT_FLOAT_EQ(apply_easing(EasingCurve::Linear, -1.0F), 0.0F);
    EXPECT_FLOAT_EQ(apply_easing(EasingCurve::Linear, 2.0F), 1.0F);
    EXPECT_FLOAT_EQ(apply_easing(EasingCurve::StepStart, 0.25F), 1.0F);
    EXPECT_FLOAT_EQ(apply_easing(EasingCurve::StepEnd, 0.25F), 0.0F);
    EXPECT_GT(apply_easing(EasingCurve::EaseOutCubic, 0.5F), 0.5F);
    EXPECT_LT(apply_easing(EasingCurve::EaseInCubic, 0.5F), 0.5F);
}

TEST(AnimationTests, TimelineHandlesDelayDirectionIterationsAndFill) {
    const Timeline timeline(AnimationTiming{.delay = AnimationDuration{0.2F},
                                            .duration = AnimationDuration{1.0F},
                                            .iteration_count = 2.0F,
                                            .direction = PlaybackDirection::Alternate,
                                            .fill_mode = FillMode::Both});

    const auto before = timeline.sample(AnimationDuration{0.1F});
    EXPECT_TRUE(before.active);
    EXPECT_TRUE(before.before_start);
    EXPECT_FLOAT_EQ(before.progress, 0.0F);

    const auto first = timeline.sample(AnimationDuration{0.7F});
    EXPECT_TRUE(first.active);
    EXPECT_EQ(first.iteration, 0U);
    EXPECT_NEAR(first.progress, 0.5F, 0.001F);

    const auto second = timeline.sample(AnimationDuration{1.7F});
    EXPECT_TRUE(second.active);
    EXPECT_EQ(second.iteration, 1U);
    EXPECT_NEAR(second.progress, 0.5F, 0.001F);

    const auto after = timeline.sample(AnimationDuration{2.4F});
    EXPECT_TRUE(after.finished);
    EXPECT_TRUE(after.after_end);
    EXPECT_TRUE(after.active);
    EXPECT_FLOAT_EQ(after.progress, 0.0F);
    ASSERT_TRUE(timeline.total_duration());
    EXPECT_NEAR(timeline.total_duration()->count(), 2.2F, 0.001F);
}

TEST(AnimationTests, KeyframeTrackInterpolatesCommonValueTypes) {
    KeyframeTrack<float> opacity({Keyframe<float>{.offset = 0.0F, .value = 0.0F},
                                  Keyframe<float>{.offset = 1.0F, .value = 1.0F}});
    EXPECT_NEAR(opacity.sample(0.25F), 0.25F, 0.001F);

    KeyframeTrack<Color> color(
        {Keyframe<Color>{.offset = 0.0F, .value = Color::rgba(0, 0, 0, 0)},
         Keyframe<Color>{.offset = 1.0F, .value = Color::rgba(100, 50, 200, 255)}});
    const auto halfway = color.sample(0.5F);
    EXPECT_EQ(halfway.red, 50U);
    EXPECT_EQ(halfway.green, 25U);
    EXPECT_EQ(halfway.blue, 100U);
    EXPECT_EQ(halfway.alpha, 128U);

    KeyframeTrack<Transform2D> transform(
        {Keyframe<Transform2D>{.offset = 0.0F, .value = Transform2D::translation(0.0F, 0.0F)},
         Keyframe<Transform2D>{.offset = 1.0F, .value = Transform2D::translation(20.0F, 10.0F)}});
    const auto moved = transform.sample(0.5F);
    EXPECT_FLOAT_EQ(moved.dx, 10.0F);
    EXPECT_FLOAT_EQ(moved.dy, 5.0F);

    KeyframeTrack<Rect> rect(
        {Keyframe<Rect>{.offset = 0.0F, .value = Rect{0.0F, 0.0F, 10.0F, 10.0F}},
         Keyframe<Rect>{.offset = 1.0F, .value = Rect{10.0F, 20.0F, 30.0F, 40.0F}}});
    const auto sampled_rect = rect.sample(0.5F);
    EXPECT_FLOAT_EQ(sampled_rect.x, 5.0F);
    EXPECT_FLOAT_EQ(sampled_rect.y, 10.0F);
    EXPECT_FLOAT_EQ(sampled_rect.width, 20.0F);
    EXPECT_FLOAT_EQ(sampled_rect.height, 25.0F);
}

TEST(AnimationTests, TransitionHelpersCreateConciseFiniteAnimations) {
    const auto timing = make_transition_timing(AnimationDuration{0.25F}, EasingFunction::linear());
    EXPECT_FLOAT_EQ(timing.duration.count(), 0.25F);
    EXPECT_EQ(timing.fill_mode, FillMode::Forwards);
    EXPECT_EQ(timing.easing.curve, EasingCurve::Linear);

    const auto track = make_from_to_track<float>(2.0F, 6.0F, EasingFunction::ease_out_cubic());
    EXPECT_NEAR(track.sample(0.5F), 5.5F, 0.01F);

    auto value = 0.0F;
    Storyboard storyboard;
    storyboard.reserve(1U);
    storyboard.animate<float>(
        0.0F, 10.0F, make_transition_timing(AnimationDuration{1.0F}, EasingFunction::linear()),
        [&value](const float& next) { value = next; });
    EXPECT_EQ(storyboard.channel_count(), 1U);

    const auto start = AnimationClockType::now();
    storyboard.play(start);
    EXPECT_TRUE(storyboard.tick(start + std::chrono::milliseconds(500)));
    EXPECT_NEAR(value, 5.0F, 0.03F);
}

TEST(AnimationTests, StoryboardAppliesChannelsAndFinishesFiniteAnimations) {
    auto opacity = 0.0F;
    Storyboard storyboard;
    storyboard.add(make_keyframe_animation<float>(
        KeyframeTrack<float>({Keyframe<float>{.offset = 0.0F, .value = 0.0F},
                              Keyframe<float>{.offset = 1.0F, .value = 1.0F}}),
        AnimationTiming{.duration = AnimationDuration{1.0F}, .fill_mode = FillMode::Forwards},
        [&opacity](const float& value) { opacity = value; }));

    const auto start = AnimationClockType::now();
    storyboard.play(start);
    EXPECT_TRUE(storyboard.tick(start + std::chrono::milliseconds(500)));
    EXPECT_NEAR(opacity, 0.5F, 0.03F);

    EXPECT_FALSE(storyboard.tick(start + std::chrono::milliseconds(1200)));
    EXPECT_FLOAT_EQ(opacity, 1.0F);
    EXPECT_EQ(storyboard.state(), AnimationPlayState::Finished);
}

TEST(AnimationTests, StoryboardPrunesFinishedChannelsAfterTick) {
    auto opacity = 0.0F;
    Storyboard storyboard;
    storyboard.add(make_keyframe_animation<float>(
        KeyframeTrack<float>({Keyframe<float>{.offset = 0.0F, .value = 0.0F},
                              Keyframe<float>{.offset = 1.0F, .value = 1.0F}}),
        AnimationTiming{.duration = AnimationDuration{0.1F}, .fill_mode = FillMode::Forwards},
        [&opacity](const float& value) { opacity = value; }));

    const auto start = AnimationClockType::now();
    storyboard.play(start);
    EXPECT_FALSE(storyboard.tick(start + std::chrono::milliseconds(150)));
    EXPECT_FLOAT_EQ(opacity, 1.0F);
    EXPECT_EQ(storyboard.state(), AnimationPlayState::Finished);
    EXPECT_EQ(storyboard.channel_count(), 0U);
}

TEST(AnimationTests, PropertyAnimationsWriteThroughPropertyStore) {
    auto opacity_property = winelement::core::make_property_metadata<float>(
        "opacity", winelement::core::PropertyInvalidation::Paint);
    winelement::core::PropertyStore store;
    auto invalidated = false;

    Storyboard storyboard;
    storyboard.add(make_property_animation<float>(
        store, opacity_property,
        KeyframeTrack<float>({Keyframe<float>{.offset = 0.0F, .value = 0.0F},
                              Keyframe<float>{.offset = 1.0F, .value = 1.0F}}),
        AnimationTiming{.duration = AnimationDuration{1.0F}, .fill_mode = FillMode::Forwards},
        [&invalidated](const winelement::core::PropertyChange& change) {
            invalidated = change.changed &&
                          winelement::core::has_invalidation(
                              change.invalidation, winelement::core::PropertyInvalidation::Paint);
        }));

    const auto start = AnimationClockType::now();
    storyboard.play(start);
    EXPECT_TRUE(storyboard.tick(start + std::chrono::milliseconds(500)));
    EXPECT_TRUE(invalidated);
    EXPECT_NEAR(store.value(opacity_property, 0.0F), 0.5F, 0.03F);
}

TEST(AnimationTests, StoryboardKeepsInfiniteAnimationsRunning) {
    auto offset = 0.0F;
    Storyboard storyboard;
    storyboard.add(make_keyframe_animation<float>(
        KeyframeTrack<float>({Keyframe<float>{.offset = 0.0F, .value = 0.0F},
                              Keyframe<float>{.offset = 1.0F, .value = 10.0F}}),
        AnimationTiming{.duration = AnimationDuration{1.0F},
                        .iteration_count = std::numeric_limits<float>::infinity(),
                        .direction = PlaybackDirection::Alternate,
                        .fill_mode = FillMode::None},
        [&offset](const float& value) { offset = value; }));

    const auto start = AnimationClockType::now();
    storyboard.play(start);
    EXPECT_TRUE(storyboard.tick(start + std::chrono::milliseconds(1500)));
    EXPECT_NEAR(offset, 5.0F, 0.03F);
    EXPECT_EQ(storyboard.state(), AnimationPlayState::Running);
}

TEST(AnimationTests, PhysicsAnimationsDriveSpringAndFrictionValues) {
    SpringSimulation spring(0.0F, 1.0F);
    const auto spring_start = spring.sample(AnimationDuration{0.0F});
    const auto spring_late = spring.sample(AnimationDuration{1.0F});
    EXPECT_NEAR(spring_start.value, 0.0F, 0.001F);
    EXPECT_NEAR(spring_late.value, 1.0F, 0.08F);

    FrictionSimulation friction(0.0F, 100.0F, 6.0F);
    const auto friction_start = friction.sample(AnimationDuration{0.0F});
    const auto friction_late = friction.sample(AnimationDuration{1.0F});
    EXPECT_GT(friction_late.value, friction_start.value);
    EXPECT_LT(std::abs(friction_late.velocity), std::abs(friction_start.velocity));

    auto value = 0.0F;
    Storyboard storyboard;
    storyboard.add(make_physics_animation(std::make_unique<FrictionSimulation>(0.0F, 20.0F),
                                          [&value](float next) { value = next; }));
    const auto start = AnimationClockType::now();
    storyboard.play(start);
    EXPECT_TRUE(storyboard.tick(start + std::chrono::milliseconds(100)));
    EXPECT_GT(value, 0.0F);
}

} // namespace
