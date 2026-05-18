#include <winelement/layout/layout_engine.hpp>

#include <winelement/layout/layout_element.hpp>

#include "detail/yoga_handles.hpp"

#include <memory>

namespace winelement::layout {

LayoutEngine::LayoutEngine(LayoutEngineOptions options)
    : config_(std::make_shared<detail::YogaConfigHandle>(options)) {}

LayoutEngine::~LayoutEngine() = default;

std::unique_ptr<LayoutElement> LayoutEngine::create_element() const {
    return std::unique_ptr<LayoutElement>(new LayoutElement(config_));
}

void LayoutEngine::set_point_scale_factor(float point_scale_factor) {
    config_->set_point_scale_factor(point_scale_factor);
}

float LayoutEngine::point_scale_factor() const noexcept {
    return config_->point_scale_factor();
}

bool LayoutEngine::use_web_defaults() const noexcept {
    return config_->use_web_defaults();
}

void LayoutEngine::set_errata(Errata errata) {
    config_->set_errata(errata);
}

Errata LayoutEngine::errata() const noexcept {
    return config_->errata();
}

void LayoutEngine::set_experimental_feature_enabled(ExperimentalFeature feature, bool enabled) {
    config_->set_experimental_feature_enabled(feature, enabled);
}

bool LayoutEngine::is_experimental_feature_enabled(ExperimentalFeature feature) const noexcept {
    return config_->is_experimental_feature_enabled(feature);
}

} // namespace winelement::layout
