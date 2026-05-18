#pragma once

#include <winelement/layout/layout_engine_options.hpp>
#include <winelement/layout/layout_types.hpp>

#include <memory>

namespace winelement::layout {

namespace detail {
class YogaConfigHandle;
}

class LayoutElement;
class LayoutElementPool;

class LayoutEngine final {
  public:
    explicit LayoutEngine(LayoutEngineOptions options = {});
    ~LayoutEngine();

    LayoutEngine(const LayoutEngine&) = delete;
    LayoutEngine& operator=(const LayoutEngine&) = delete;
    LayoutEngine(LayoutEngine&&) noexcept = default;
    LayoutEngine& operator=(LayoutEngine&&) noexcept = default;

    [[nodiscard]] std::unique_ptr<LayoutElement> create_element() const;

    void set_point_scale_factor(float point_scale_factor);
    [[nodiscard]] float point_scale_factor() const noexcept;
    [[nodiscard]] bool use_web_defaults() const noexcept;
    void set_errata(Errata errata);
    [[nodiscard]] Errata errata() const noexcept;
    void set_experimental_feature_enabled(ExperimentalFeature feature, bool enabled);
    [[nodiscard]] bool is_experimental_feature_enabled(ExperimentalFeature feature) const noexcept;

  private:
    std::shared_ptr<detail::YogaConfigHandle> config_;

    friend class LayoutElement;
    friend class LayoutElementPool;
};

} // namespace winelement::layout
