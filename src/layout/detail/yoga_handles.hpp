#pragma once

#include <winelement/layout/layout_engine_options.hpp>

#include "yoga_conversions.hpp"

#include <cmath>
#include <memory>
#include <new>
#include <stdexcept>

#include <yoga/Yoga.h>

namespace winelement::layout::detail {

class YogaConfigHandle final {
  public:
    explicit YogaConfigHandle(LayoutEngineOptions options)
        : config_(YGConfigNew()), use_web_defaults_(options.use_web_defaults) {
        if (config_ == nullptr) {
            throw std::bad_alloc();
        }

        YGConfigSetUseWebDefaults(config_, use_web_defaults_);
        set_errata(options.errata);
        set_experimental_feature_enabled(ExperimentalFeature::WebFlexBasis,
                                         options.web_flex_basis_enabled);
        set_point_scale_factor(options.point_scale_factor);
    }

    ~YogaConfigHandle() {
        if (config_ != nullptr) {
            YGConfigFree(config_);
        }
    }

    YogaConfigHandle(const YogaConfigHandle&) = delete;
    YogaConfigHandle& operator=(const YogaConfigHandle&) = delete;

    [[nodiscard]] YGConfigRef get() const noexcept {
        return config_;
    }
    [[nodiscard]] float point_scale_factor() const noexcept {
        return point_scale_factor_;
    }
    [[nodiscard]] bool use_web_defaults() const noexcept {
        return use_web_defaults_;
    }
    [[nodiscard]] Errata errata() const noexcept {
        return errata_;
    }
    [[nodiscard]] bool is_experimental_feature_enabled(ExperimentalFeature feature) const noexcept {
        switch (feature) {
        case ExperimentalFeature::WebFlexBasis:
            return web_flex_basis_enabled_;
        }

        return false;
    }

    void set_point_scale_factor(float point_scale_factor) {
        if (!std::isfinite(point_scale_factor) || point_scale_factor < 0.0F) {
            throw std::invalid_argument("point scale factor must be finite and non-negative");
        }

        point_scale_factor_ = point_scale_factor;
        YGConfigSetPointScaleFactor(config_, point_scale_factor_);
    }

    void set_errata(Errata errata) noexcept {
        errata_ = errata;
        YGConfigSetErrata(config_, to_yoga(errata_));
    }

    void set_experimental_feature_enabled(ExperimentalFeature feature, bool enabled) {
        switch (feature) {
        case ExperimentalFeature::WebFlexBasis:
            web_flex_basis_enabled_ = enabled;
            break;
        }

        YGConfigSetExperimentalFeatureEnabled(config_, to_yoga(feature), enabled);
    }

  private:
    YGConfigRef config_ = nullptr;
    bool use_web_defaults_ = true;
    Errata errata_ = Errata::None;
    bool web_flex_basis_enabled_ = false;
    float point_scale_factor_ = 1.0F;
};

class YogaNodeHandle final {
  public:
    explicit YogaNodeHandle(YGConfigRef config) : node_(YGNodeNewWithConfig(config)) {
        if (node_ == nullptr) {
            throw std::bad_alloc();
        }
    }

    ~YogaNodeHandle() {
        if (node_ != nullptr) {
            YGNodeFree(node_);
        }
    }

    YogaNodeHandle(const YogaNodeHandle&) = delete;
    YogaNodeHandle& operator=(const YogaNodeHandle&) = delete;

    [[nodiscard]] YGNodeRef get() noexcept {
        return node_;
    }
    [[nodiscard]] YGNodeConstRef get() const noexcept {
        return node_;
    }

  private:
    YGNodeRef node_ = nullptr;
};

} // namespace winelement::layout::detail
