#pragma once

#include <winelement/rendering/render_scene.hpp>
#include <winelement/rendering/render_types.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace winelement::rendering {

enum class RenderFramePassKind {
    State,
    Geometry,
    Text,
    Image,
    Effect,
    Composite,
    Present,
};

struct RenderFramePass {
    RenderFramePassKind kind = RenderFramePassKind::State;
    layout::Rect bounds{};
    std::uint32_t first_command_index = 0U;
    std::uint32_t command_count = 0U;
    std::uint32_t estimated_draw_call_count = 0U;
    bool uses_texture = false;
    bool requires_stencil = false;
};

struct RenderFrameGraph {
    std::vector<RenderFramePass> passes;
    layout::Rect bounds{};
    std::uint64_t fingerprint = 0U;
    std::uint32_t command_count = 0U;
    std::uint32_t estimated_draw_call_count = 0U;

    [[nodiscard]] bool empty() const noexcept {
        return passes.empty();
    }
};

[[nodiscard]] RenderFrameGraph build_render_frame_graph(const RenderCommandList& command_list);
[[nodiscard]] RenderFrameGraph build_render_frame_graph(const RenderScene& scene);

} // namespace winelement::rendering