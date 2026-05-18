#pragma once

#include <winelement/rendering/render_resource_queue.hpp>
#include <winelement/rendering/render_scene.hpp>
#include <winelement/rendering/render_types.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winelement::rendering {

enum class CompositorLayerKind { Picture, Texture, PlatformSurface };

enum class CompositorPromotionReason {
    StableLayer,
    AnimatedOpacity,
    AnimatedTransform,
    VideoOrTexture,
};

struct CompositorLayer {
    std::uint64_t id = 0U;
    CompositorLayerKind kind = CompositorLayerKind::Picture;
    layout::Rect bounds{};
    Transform2D transform{};
    float opacity = 1.0F;
    RenderResourceId resource_id{};
    std::string debug_name;
};

struct CompositorPromotionCandidate {
    std::uint64_t element_id = 0U;
    layout::Rect bounds{};
    Transform2D transform{};
    float opacity = 1.0F;
    CompositorPromotionReason reason = CompositorPromotionReason::StableLayer;
    bool clips_to_bounds = true;
    bool animates_on_compositor = false;
};

struct CompositorPromotionPlan {
    std::vector<CompositorPromotionCandidate> candidates;

    [[nodiscard]] bool empty() const noexcept {
        return candidates.empty();
    }
};

struct CompositorPromotionOptions {
    std::size_t max_candidates = 32U;
    float minimum_area = 4096.0F;
    bool include_stable_layers = true;
};

[[nodiscard]] CompositorPromotionPlan
build_compositor_promotion_plan(const RenderCommandList& command_list,
                                CompositorPromotionOptions options = {});
[[nodiscard]] CompositorPromotionPlan
build_compositor_promotion_plan(const RenderScene& scene, CompositorPromotionOptions options = {});

struct CompositorFrame {
    std::uint64_t frame_id = 0U;
    Color clear_color{};
    DirtyRegion dirty_region{};
    RenderScene render_scene{};
    std::vector<CompositorLayer> layers;
};

class CompositorFrameBuilder final {
  public:
    explicit CompositorFrameBuilder(std::uint64_t frame_id = 0U);

    CompositorFrameBuilder& set_clear_color(Color color) noexcept;
    CompositorFrameBuilder& set_dirty_region(DirtyRegion dirty_region);
    CompositorFrameBuilder& set_render_scene(RenderScene render_scene);
    CompositorFrameBuilder& add_layer(CompositorLayer layer);
    [[nodiscard]] CompositorFrame build();

  private:
    CompositorFrame frame_{};
};

} // namespace winelement::rendering