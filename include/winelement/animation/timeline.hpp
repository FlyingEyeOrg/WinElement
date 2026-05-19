#pragma once

#include <winelement/animation/easing.hpp>
#include <winelement/core/core_types.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace winelement::animation {

using AnimationClockType = std::chrono::steady_clock;
using AnimationTimePoint = AnimationClockType::time_point;
using AnimationDuration = std::chrono::duration<float>;

enum class FillMode { None, Forwards, Backwards, Both };
enum class PlaybackDirection { Normal, Reverse, Alternate, AlternateReverse };
enum class AnimationPlayState { Idle, Running, Paused, Finished, Canceled };

struct AnimationTiming {
    AnimationDuration delay{0.0F};
    AnimationDuration duration{1.0F};
    float iteration_count = 1.0F;
    PlaybackDirection direction = PlaybackDirection::Normal;
    FillMode fill_mode = FillMode::Forwards;
    EasingFunction easing{};
};

[[nodiscard]] AnimationTiming
make_transition_timing(AnimationDuration duration = AnimationDuration{0.15F},
                       EasingFunction easing = EasingFunction::ease_out_cubic(),
                       FillMode fill_mode = FillMode::Forwards) noexcept;

struct TimelineSample {
    AnimationDuration elapsed{0.0F};
    std::uint32_t iteration = 0;
    float progress = 0.0F;
    float eased_progress = 0.0F;
    bool active = false;
    bool before_start = false;
    bool after_end = false;
    bool finished = false;
};

class Timeline final {
  public:
    explicit Timeline(AnimationTiming timing = {});

    [[nodiscard]] const AnimationTiming& timing() const noexcept;
    [[nodiscard]] std::optional<AnimationDuration> total_duration() const noexcept;
    [[nodiscard]] TimelineSample sample(AnimationDuration elapsed) const noexcept;

  private:
    AnimationTiming timing_{};
};

class AnimationClock final {
  public:
    [[nodiscard]] AnimationPlayState state() const noexcept;
    [[nodiscard]] AnimationDuration elapsed(AnimationTimePoint now) const noexcept;

    void play(AnimationTimePoint now = AnimationClockType::now()) noexcept;
    void pause(AnimationTimePoint now = AnimationClockType::now()) noexcept;
    void resume(AnimationTimePoint now = AnimationClockType::now()) noexcept;
    void finish(AnimationTimePoint now = AnimationClockType::now()) noexcept;
    void cancel() noexcept;

  private:
    AnimationTimePoint start_time_{};
    AnimationDuration held_elapsed_{0.0F};
    AnimationPlayState state_ = AnimationPlayState::Idle;
};

class AnimationChannel {
  public:
    virtual ~AnimationChannel();

    AnimationChannel(const AnimationChannel&) = delete;
    AnimationChannel& operator=(const AnimationChannel&) = delete;
    AnimationChannel(AnimationChannel&&) = delete;
    AnimationChannel& operator=(AnimationChannel&&) = delete;

    [[nodiscard]] virtual bool apply(AnimationDuration elapsed) = 0;
    [[nodiscard]] virtual bool is_finished(AnimationDuration elapsed) const noexcept = 0;

  protected:
    AnimationChannel() = default;
};

[[nodiscard]] float interpolate_value(float from, float to, float progress) noexcept;
[[nodiscard]] core::Point interpolate_value(core::Point from, core::Point to,
                                            float progress) noexcept;
[[nodiscard]] core::Size interpolate_value(core::Size from, core::Size to, float progress) noexcept;
[[nodiscard]] core::Rect interpolate_value(core::Rect from, core::Rect to, float progress) noexcept;
[[nodiscard]] core::Color interpolate_value(core::Color from, core::Color to,
                                            float progress) noexcept;
[[nodiscard]] core::Transform2D interpolate_value(core::Transform2D from, core::Transform2D to,
                                                  float progress) noexcept;

template <typename T> struct Keyframe {
    float offset = 0.0F;
    T value{};
    EasingFunction easing{};
};

template <typename T> class KeyframeTrack final {
  public:
    KeyframeTrack() = default;

    explicit KeyframeTrack(std::vector<Keyframe<T>> keyframes) {
        set_keyframes(std::move(keyframes));
    }

    void add_keyframe(Keyframe<T> keyframe) {
        keyframe.offset = normalized_offset(keyframe.offset);
        keyframes_.push_back(std::move(keyframe));
        sort_keyframes();
    }

    void set_keyframes(std::vector<Keyframe<T>> keyframes) {
        keyframes_ = std::move(keyframes);
        for (auto& keyframe : keyframes_) {
            keyframe.offset = normalized_offset(keyframe.offset);
        }
        sort_keyframes();
    }

    [[nodiscard]] bool empty() const noexcept {
        return keyframes_.empty();
    }

    [[nodiscard]] const std::vector<Keyframe<T>>& keyframes() const noexcept {
        return keyframes_;
    }

    [[nodiscard]] T sample(float progress) const {
        if (keyframes_.empty()) {
            throw std::logic_error("animation keyframe track has no keyframes");
        }

        const auto normalized_progress = std::clamp(progress, 0.0F, 1.0F);
        if (normalized_progress <= keyframes_.front().offset) {
            return keyframes_.front().value;
        }
        if (normalized_progress >= keyframes_.back().offset) {
            return keyframes_.back().value;
        }

        const auto right_index = segment_index_for(normalized_progress);
        const auto right = keyframes_.begin() + static_cast<std::ptrdiff_t>(right_index);
        const auto left = std::prev(right);
        const auto span = right->offset - left->offset;
        if (std::abs(span) <= 0.000001F) {
            return right->value;
        }

        const auto segment_progress =
            std::clamp((normalized_progress - left->offset) / span, 0.0F, 1.0F);
        return interpolate_value(left->value, right->value,
                                 right->easing.evaluate(segment_progress));
    }

  private:
    [[nodiscard]] static float normalized_offset(float offset) noexcept {
        return std::isfinite(offset) ? std::clamp(offset, 0.0F, 1.0F) : 0.0F;
    }

    void sort_keyframes() {
        std::stable_sort(
            keyframes_.begin(), keyframes_.end(),
            [](const auto& left, const auto& right) { return left.offset < right.offset; });
        cached_segment_index_ = 1U;
    }

    [[nodiscard]] std::size_t segment_index_for(float progress) const {
        if (cached_segment_index_ > 0U && cached_segment_index_ < keyframes_.size()) {
            const auto& left = keyframes_[cached_segment_index_ - 1U];
            const auto& right = keyframes_[cached_segment_index_];
            if (progress >= left.offset && progress <= right.offset) {
                return cached_segment_index_;
            }
        }

        const auto right = std::lower_bound(
            keyframes_.begin(), keyframes_.end(), progress,
            [](const Keyframe<T>& keyframe, float value) { return keyframe.offset < value; });
        const auto index = right == keyframes_.begin()
                               ? std::size_t{1U}
                               : static_cast<std::size_t>(std::distance(keyframes_.begin(), right));
        cached_segment_index_ = std::min(index, keyframes_.size() - 1U);
        return cached_segment_index_;
    }

    std::vector<Keyframe<T>> keyframes_;
    mutable std::size_t cached_segment_index_ = 1U;
};

template <typename T>
[[nodiscard]] KeyframeTrack<T> make_from_to_track(T from, T to, EasingFunction easing = {}) {
    return KeyframeTrack<T>(
        {Keyframe<T>{.offset = 0.0F, .value = std::move(from)},
         Keyframe<T>{.offset = 1.0F, .value = std::move(to), .easing = easing}});
}

template <typename T> class KeyframeAnimation final : public AnimationChannel {
  public:
    using Setter = std::function<void(const T&)>;

    KeyframeAnimation(KeyframeTrack<T> track, AnimationTiming timing, Setter setter)
        : timeline_(timing), track_(std::move(track)), setter_(std::move(setter)) {}

    [[nodiscard]] const Timeline& timeline() const noexcept {
        return timeline_;
    }

    [[nodiscard]] const KeyframeTrack<T>& track() const noexcept {
        return track_;
    }

    [[nodiscard]] bool apply(AnimationDuration elapsed) override {
        const auto sample = timeline_.sample(elapsed);
        if (sample.active && setter_ != nullptr && !track_.empty()) {
            setter_(track_.sample(sample.eased_progress));
        }
        return !sample.finished;
    }

    [[nodiscard]] bool is_finished(AnimationDuration elapsed) const noexcept override {
        return timeline_.sample(elapsed).finished;
    }

  private:
    Timeline timeline_;
    KeyframeTrack<T> track_;
    Setter setter_;
};

template <typename T>
[[nodiscard]] std::unique_ptr<KeyframeAnimation<T>>
make_keyframe_animation(KeyframeTrack<T> track, AnimationTiming timing,
                        typename KeyframeAnimation<T>::Setter setter) {
    return std::make_unique<KeyframeAnimation<T>>(std::move(track), timing, std::move(setter));
}

class Storyboard final {
  public:
    void add(std::unique_ptr<AnimationChannel> channel);
    void reserve(std::size_t channel_count);
    void clear() noexcept;

    template <typename T>
    Storyboard& animate(T from, T to, AnimationTiming timing,
                        typename KeyframeAnimation<T>::Setter setter) {
        add(make_keyframe_animation<T>(make_from_to_track<T>(std::move(from), std::move(to)),
                                       timing, std::move(setter)));
        return *this;
    }

    [[nodiscard]] AnimationPlayState state() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t channel_count() const noexcept;

    void play(AnimationTimePoint now = AnimationClockType::now()) noexcept;
    void pause(AnimationTimePoint now = AnimationClockType::now()) noexcept;
    void resume(AnimationTimePoint now = AnimationClockType::now()) noexcept;
    void cancel() noexcept;

    [[nodiscard]] bool seek(AnimationDuration elapsed);
    [[nodiscard]] bool tick(AnimationTimePoint now = AnimationClockType::now());

  private:
    void release_idle_channel_storage() noexcept;

    AnimationClock clock_;
    std::vector<std::unique_ptr<AnimationChannel>> channels_;
};

} // namespace winelement::animation
