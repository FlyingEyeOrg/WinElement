#include <winelement/animation/timeline.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace winelement::animation {
namespace {

[[nodiscard]] bool is_valid_duration(AnimationDuration duration) noexcept {
    return std::isfinite(duration.count()) && duration.count() >= 0.0F;
}

[[nodiscard]] bool is_infinite(float value) noexcept {
    return std::isinf(value) && value > 0.0F;
}

[[nodiscard]] bool is_valid_iteration_count(float iteration_count) noexcept {
    return (std::isfinite(iteration_count) && iteration_count > 0.0F) ||
           is_infinite(iteration_count);
}

void validate_timing(AnimationTiming timing) {
    if (!is_valid_duration(timing.delay)) {
        throw std::invalid_argument("animation delay must be finite and non-negative");
    }
    if (!is_valid_duration(timing.duration)) {
        throw std::invalid_argument("animation duration must be finite and non-negative");
    }
    if (!is_valid_iteration_count(timing.iteration_count)) {
        throw std::invalid_argument(
            "animation iteration count must be positive, finite, or positive infinity");
    }
}

[[nodiscard]] bool is_forward_iteration(PlaybackDirection direction,
                                        std::uint32_t iteration) noexcept {
    switch (direction) {
    case PlaybackDirection::Normal:
        return true;
    case PlaybackDirection::Reverse:
        return false;
    case PlaybackDirection::Alternate:
        return iteration % 2U == 0U;
    case PlaybackDirection::AlternateReverse:
        return iteration % 2U != 0U;
    }
    return true;
}

[[nodiscard]] float directed_progress(float progress, PlaybackDirection direction,
                                      std::uint32_t iteration) noexcept {
    const auto clamped = std::clamp(progress, 0.0F, 1.0F);
    return is_forward_iteration(direction, iteration) ? clamped : 1.0F - clamped;
}

[[nodiscard]] bool has_fill_before(FillMode fill_mode) noexcept {
    return fill_mode == FillMode::Backwards || fill_mode == FillMode::Both;
}

[[nodiscard]] bool has_fill_after(FillMode fill_mode) noexcept {
    return fill_mode == FillMode::Forwards || fill_mode == FillMode::Both;
}

[[nodiscard]] std::pair<std::uint32_t, float> terminal_iteration(float iteration_count) noexcept {
    if (is_infinite(iteration_count)) {
        return {0U, 1.0F};
    }

    const auto rounded = std::round(iteration_count);
    if (std::abs(iteration_count - rounded) <= 0.0001F) {
        const auto whole = std::max(1.0F, rounded);
        return {static_cast<std::uint32_t>(whole - 1.0F), 1.0F};
    }

    const auto whole = std::floor(iteration_count);
    return {static_cast<std::uint32_t>(std::max(0.0F, whole)), iteration_count - whole};
}

[[nodiscard]] AnimationClockType::duration to_clock_duration(AnimationDuration duration) noexcept {
    return std::chrono::duration_cast<AnimationClockType::duration>(duration);
}

} // namespace

AnimationTiming make_transition_timing(AnimationDuration duration, EasingFunction easing,
                                       FillMode fill_mode) noexcept {
    if (!is_valid_duration(duration)) {
        duration = AnimationDuration{0.15F};
    }
    return AnimationTiming{.duration = duration, .fill_mode = fill_mode, .easing = easing};
}

Timeline::Timeline(AnimationTiming timing) : timing_(timing) {
    validate_timing(timing_);
}

const AnimationTiming& Timeline::timing() const noexcept {
    return timing_;
}

std::optional<AnimationDuration> Timeline::total_duration() const noexcept {
    if (is_infinite(timing_.iteration_count)) {
        return std::nullopt;
    }
    return timing_.delay + timing_.duration * timing_.iteration_count;
}

TimelineSample Timeline::sample(AnimationDuration elapsed) const noexcept {
    TimelineSample sample;
    sample.elapsed = elapsed.count() < 0.0F ? AnimationDuration{0.0F} : elapsed;

    if (sample.elapsed < timing_.delay) {
        sample.before_start = true;
        sample.active = has_fill_before(timing_.fill_mode);
        sample.progress = directed_progress(0.0F, timing_.direction, 0U);
        sample.eased_progress = timing_.easing.evaluate(sample.progress);
        return sample;
    }

    const auto active_elapsed = sample.elapsed - timing_.delay;
    if (timing_.duration.count() <= 0.0F) {
        const auto [iteration, raw_progress] = terminal_iteration(timing_.iteration_count);
        sample.iteration = iteration;
        sample.progress = directed_progress(raw_progress, timing_.direction, iteration);
        sample.eased_progress = timing_.easing.evaluate(sample.progress);
        sample.active = has_fill_after(timing_.fill_mode);
        sample.after_end = true;
        sample.finished = true;
        return sample;
    }

    if (!is_infinite(timing_.iteration_count)) {
        const auto active_duration = timing_.duration * timing_.iteration_count;
        if (active_elapsed >= active_duration) {
            const auto [iteration, raw_progress] = terminal_iteration(timing_.iteration_count);
            sample.iteration = iteration;
            sample.progress = directed_progress(raw_progress, timing_.direction, iteration);
            sample.eased_progress = timing_.easing.evaluate(sample.progress);
            sample.active = has_fill_after(timing_.fill_mode);
            sample.after_end = true;
            sample.finished = true;
            return sample;
        }
    }

    const auto duration_seconds = timing_.duration.count();
    const auto active_seconds = std::max(0.0F, active_elapsed.count());
    const auto iteration_float = std::floor(active_seconds / duration_seconds);
    sample.iteration = static_cast<std::uint32_t>(std::max(0.0F, iteration_float));
    const auto iteration_start = duration_seconds * iteration_float;
    const auto raw_progress = (active_seconds - iteration_start) / duration_seconds;
    sample.progress = directed_progress(raw_progress, timing_.direction, sample.iteration);
    sample.eased_progress = timing_.easing.evaluate(sample.progress);
    sample.active = true;
    return sample;
}

AnimationPlayState AnimationClock::state() const noexcept {
    return state_;
}

AnimationDuration AnimationClock::elapsed(AnimationTimePoint now) const noexcept {
    if (state_ == AnimationPlayState::Running) {
        return std::chrono::duration_cast<AnimationDuration>(now - start_time_);
    }
    return held_elapsed_;
}

void AnimationClock::play(AnimationTimePoint now) noexcept {
    start_time_ = now;
    held_elapsed_ = AnimationDuration{0.0F};
    state_ = AnimationPlayState::Running;
}

void AnimationClock::pause(AnimationTimePoint now) noexcept {
    if (state_ != AnimationPlayState::Running) {
        return;
    }
    held_elapsed_ = elapsed(now);
    state_ = AnimationPlayState::Paused;
}

void AnimationClock::resume(AnimationTimePoint now) noexcept {
    if (state_ != AnimationPlayState::Paused) {
        return;
    }
    start_time_ = now - to_clock_duration(held_elapsed_);
    state_ = AnimationPlayState::Running;
}

void AnimationClock::finish(AnimationTimePoint now) noexcept {
    held_elapsed_ = elapsed(now);
    state_ = AnimationPlayState::Finished;
}

void AnimationClock::cancel() noexcept {
    held_elapsed_ = AnimationDuration{0.0F};
    state_ = AnimationPlayState::Canceled;
}

AnimationChannel::~AnimationChannel() = default;

float interpolate_value(float from, float to, float progress) noexcept {
    const auto value = std::isfinite(progress) ? std::clamp(progress, 0.0F, 1.0F) : 0.0F;
    return from + (to - from) * value;
}

core::Point interpolate_value(core::Point from, core::Point to, float progress) noexcept {
    return core::Point{interpolate_value(from.x, to.x, progress),
                       interpolate_value(from.y, to.y, progress)};
}

core::Size interpolate_value(core::Size from, core::Size to, float progress) noexcept {
    return core::Size{interpolate_value(from.width, to.width, progress),
                      interpolate_value(from.height, to.height, progress)};
}

core::Rect interpolate_value(core::Rect from, core::Rect to, float progress) noexcept {
    return core::Rect{interpolate_value(from.x, to.x, progress),
                      interpolate_value(from.y, to.y, progress),
                      interpolate_value(from.width, to.width, progress),
                      interpolate_value(from.height, to.height, progress)};
}

core::Color interpolate_value(core::Color from, core::Color to, float progress) noexcept {
    const auto channel = [progress](std::uint8_t left, std::uint8_t right) {
        const auto value =
            interpolate_value(static_cast<float>(left), static_cast<float>(right), progress);
        return static_cast<std::uint8_t>(std::clamp(std::round(value), 0.0F, 255.0F));
    };
    return core::Color::rgba(channel(from.red, to.red), channel(from.green, to.green),
                             channel(from.blue, to.blue), channel(from.alpha, to.alpha));
}

core::Transform2D interpolate_value(core::Transform2D from, core::Transform2D to,
                                    float progress) noexcept {
    return core::Transform2D{.m11 = interpolate_value(from.m11, to.m11, progress),
                             .m12 = interpolate_value(from.m12, to.m12, progress),
                             .m21 = interpolate_value(from.m21, to.m21, progress),
                             .m22 = interpolate_value(from.m22, to.m22, progress),
                             .dx = interpolate_value(from.dx, to.dx, progress),
                             .dy = interpolate_value(from.dy, to.dy, progress)};
}

void Storyboard::add(std::unique_ptr<AnimationChannel> channel) {
    if (!channel) {
        throw std::invalid_argument("storyboard animation channel must not be null");
    }
    channels_.push_back(std::move(channel));
}

void Storyboard::reserve(std::size_t channel_count) {
    channels_.reserve(channel_count);
}

void Storyboard::clear() noexcept {
    channels_.clear();
    clock_.cancel();
}

AnimationPlayState Storyboard::state() const noexcept {
    return clock_.state();
}

bool Storyboard::empty() const noexcept {
    return channels_.empty();
}

std::size_t Storyboard::channel_count() const noexcept {
    return channels_.size();
}

void Storyboard::play(AnimationTimePoint now) noexcept {
    clock_.play(now);
}

void Storyboard::pause(AnimationTimePoint now) noexcept {
    clock_.pause(now);
}

void Storyboard::resume(AnimationTimePoint now) noexcept {
    clock_.resume(now);
}

void Storyboard::cancel() noexcept {
    clock_.cancel();
}

bool Storyboard::seek(AnimationDuration elapsed) {
    auto has_active_channel = false;
    for (auto& channel : channels_) {
        has_active_channel = channel->apply(elapsed) || has_active_channel;
    }
    return has_active_channel;
}

bool Storyboard::tick(AnimationTimePoint now) {
    if (clock_.state() == AnimationPlayState::Idle) {
        clock_.play(now);
    }
    if (clock_.state() != AnimationPlayState::Running) {
        return false;
    }

    const auto elapsed_time = clock_.elapsed(now);
    const auto has_active_channel = seek(elapsed_time);
    if (!has_active_channel && !channels_.empty()) {
        clock_.finish(now);
    }
    return has_active_channel;
}

} // namespace winelement::animation
