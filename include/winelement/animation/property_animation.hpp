#pragma once

#include <winelement/animation/timeline.hpp>
#include <winelement/core/property.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace winelement::animation {

using PropertyInvalidationHandler = std::function<void(const core::PropertyChange& change)>;

template <typename T>
[[nodiscard]] std::unique_ptr<KeyframeAnimation<T>>
make_property_animation(core::PropertyStore& store, const core::PropertyMetadata& metadata,
                        KeyframeTrack<T> track, AnimationTiming timing,
                        PropertyInvalidationHandler invalidation_handler = {}) {
    return make_keyframe_animation<T>(
        std::move(track), timing,
        [&store, &metadata,
         invalidation_handler = std::move(invalidation_handler)](const T& value) {
            const auto change = store.set_value<T>(metadata, value);
            if (invalidation_handler) {
                invalidation_handler(change);
            }
        });
}

} // namespace winelement::animation