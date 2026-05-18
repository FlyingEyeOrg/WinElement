#include <winelement/rendering/render_frame_graph.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace winelement::rendering {
namespace {

[[nodiscard]] RenderFramePassKind pass_kind_for(RenderCommandType type) noexcept {
    switch (type) {
    case RenderCommandType::DrawText:
    case RenderCommandType::DrawTextLayout:
        return RenderFramePassKind::Text;
    case RenderCommandType::DrawImage:
        return RenderFramePassKind::Image;
    case RenderCommandType::DrawBoxShadow:
        return RenderFramePassKind::Effect;
    case RenderCommandType::DrawLine:
    case RenderCommandType::FillRect:
    case RenderCommandType::FillPixelSnappedRect:
    case RenderCommandType::StrokePixelSnappedRect:
    case RenderCommandType::StrokeRect:
    case RenderCommandType::FillRoundedRect:
    case RenderCommandType::StrokeRoundedRect:
    case RenderCommandType::FillEllipse:
    case RenderCommandType::StrokeEllipse:
    case RenderCommandType::FillGeometry:
    case RenderCommandType::StrokeGeometry:
        return RenderFramePassKind::Geometry;
    case RenderCommandType::PushLayer:
    case RenderCommandType::PopLayer:
        return RenderFramePassKind::Composite;
    case RenderCommandType::Save:
    case RenderCommandType::Restore:
    case RenderCommandType::PushClip:
    case RenderCommandType::PopClip:
    case RenderCommandType::PushGeometryClip:
    case RenderCommandType::PopGeometryClip:
    default:
        return RenderFramePassKind::State;
    }
}

[[nodiscard]] bool uses_texture(RenderFramePassKind kind) noexcept {
    return kind == RenderFramePassKind::Image || kind == RenderFramePassKind::Text;
}

[[nodiscard]] bool requires_stencil(RenderCommandType type) noexcept {
    return type == RenderCommandType::PushGeometryClip ||
           type == RenderCommandType::PopGeometryClip;
}

[[nodiscard]] bool pass_is_order_barrier(const RenderFramePass& pass) noexcept {
    return pass.kind == RenderFramePassKind::State || pass.kind == RenderFramePassKind::Composite ||
           pass.requires_stencil;
}

[[nodiscard]] bool pass_can_record_parallel(const RenderFramePass& pass) noexcept {
    switch (pass.kind) {
    case RenderFramePassKind::Geometry:
    case RenderFramePassKind::Text:
    case RenderFramePassKind::Image:
    case RenderFramePassKind::Effect:
        return !pass.requires_stencil;
    case RenderFramePassKind::State:
    case RenderFramePassKind::Composite:
    case RenderFramePassKind::Present:
    default:
        return false;
    }
}

[[nodiscard]] std::uint32_t estimated_draw_calls(RenderFramePassKind kind,
                                                 std::uint32_t command_count) noexcept {
    switch (kind) {
    case RenderFramePassKind::Geometry:
    case RenderFramePassKind::Text:
    case RenderFramePassKind::Image:
    case RenderFramePassKind::Effect:
        return std::max(command_count, 1U);
    case RenderFramePassKind::Composite:
    case RenderFramePassKind::Present:
    case RenderFramePassKind::State:
    default:
        return 0U;
    }
}

void add_command_to_pass(RenderFramePass& pass, const RenderOpcodeRecord& opcode) noexcept {
    ++pass.command_count;
    pass.bounds = layout::union_rects(pass.bounds, opcode.bounds);
    pass.uses_texture = pass.uses_texture || uses_texture(pass.kind);
    pass.requires_stencil = pass.requires_stencil || requires_stencil(opcode.opcode);
    pass.estimated_draw_call_count = estimated_draw_calls(pass.kind, pass.command_count);
}

void finalize_pass_dependencies(RenderFrameGraph& graph) {
    graph.pass_groups.clear();
    if (graph.passes.empty()) {
        return;
    }

    auto dependency_key = 0U;
    for (auto& pass : graph.passes) {
        const auto barrier = pass_is_order_barrier(pass);
        if (barrier) {
            ++dependency_key;
        }
        pass.dependency_key = dependency_key;
        pass.starts_barrier = barrier;
        pass.ends_barrier = barrier;
        pass.dependency_kind = barrier ? RenderFramePassDependencyKind::Barrier
                                       : (pass_can_record_parallel(pass)
                                              ? RenderFramePassDependencyKind::Parallel
                                              : RenderFramePassDependencyKind::Ordered);
        if (barrier) {
            ++dependency_key;
        }
    }

    auto group = RenderFramePassGroup{};
    auto have_group = false;
    const auto flush_group = [&]() {
        if (have_group && group.pass_count > 0U) {
            graph.pass_groups.push_back(group);
        }
        group = RenderFramePassGroup{};
        have_group = false;
    };

    for (std::size_t index = 0U; index < graph.passes.size(); ++index) {
        const auto& pass = graph.passes[index];
        const auto can_record_parallel = pass_can_record_parallel(pass);
        const auto barrier = pass_is_order_barrier(pass);
        if (!have_group || group.dependency_key != pass.dependency_key ||
            group.can_record_parallel != can_record_parallel || group.barrier_before != barrier ||
            group.barrier_after != barrier) {
            flush_group();
            group = RenderFramePassGroup{
                .first_pass_index = static_cast<std::uint32_t>(index),
                .pass_count = 0U,
                .dependency_key = pass.dependency_key,
                .can_record_parallel = can_record_parallel,
                .barrier_before = barrier,
                .barrier_after = barrier};
            have_group = true;
        }
        ++group.pass_count;
    }
    flush_group();
}

void append_child_graph(RenderFrameGraph& graph, const RenderFrameGraph& child_graph) {
    graph.command_count += child_graph.command_count;
    graph.estimated_draw_call_count += child_graph.estimated_draw_call_count;
    graph.bounds = layout::union_rects(graph.bounds, child_graph.bounds);
    graph.passes.insert(graph.passes.end(), child_graph.passes.begin(), child_graph.passes.end());
    finalize_pass_dependencies(graph);
}

struct CachedNodeFrameGraph {
    std::uint64_t fingerprint = 0U;
    RenderNodeKind kind = RenderNodeKind::Picture;
    layout::Rect bounds{};
    RenderFrameGraph graph{};
};

[[nodiscard]] std::vector<CachedNodeFrameGraph>& node_graph_cache() {
    thread_local auto cache = std::vector<CachedNodeFrameGraph>{};
    return cache;
}

[[nodiscard]] bool cache_entry_matches(const CachedNodeFrameGraph& entry,
                                       const RenderNode& node) noexcept {
    return entry.fingerprint == node.fingerprint && entry.kind == node.kind &&
           entry.bounds == node.bounds;
}

void remember_node_graph(const RenderNode& node, RenderFrameGraph graph) {
    if (node.fingerprint == 0U) {
        return;
    }

    auto& cache = node_graph_cache();
    for (auto& entry : cache) {
        if (cache_entry_matches(entry, node)) {
            entry.graph = std::move(graph);
            return;
        }
    }

    constexpr auto max_cached_node_graphs = 256U;
    if (cache.size() >= max_cached_node_graphs) {
        cache.erase(cache.begin());
    }
    cache.push_back(CachedNodeFrameGraph{.fingerprint = node.fingerprint,
                                         .kind = node.kind,
                                         .bounds = node.bounds,
                                         .graph = std::move(graph)});
}

[[nodiscard]] RenderFrameGraph build_render_frame_graph_for_node(const RenderNode& node) {
    for (const auto& entry : node_graph_cache()) {
        if (cache_entry_matches(entry, node)) {
            return entry.graph;
        }
    }

    RenderFrameGraph graph;
    graph.bounds = node.bounds;
    graph.fingerprint = node.fingerprint;
    if (node.kind == RenderNodeKind::Layer) {
        graph.passes.push_back(RenderFramePass{.kind = RenderFramePassKind::Composite,
                                               .bounds = node.bounds,
                                               .first_command_index = graph.command_count});
    }
    if (!node.commands.empty()) {
        append_child_graph(graph, build_render_frame_graph(node.commands));
    }

    for (const auto& child : node.children) {
        append_child_graph(graph, build_render_frame_graph_for_node(child));
    }

    remember_node_graph(node, graph);
    return graph;
}

} // namespace

RenderFrameGraph build_render_frame_graph(const RenderCommandList& command_list) {
    RenderFrameGraph graph;
    graph.bounds = command_list.bounds();
    graph.fingerprint = command_list.fingerprint();
    graph.command_count = static_cast<std::uint32_t>(command_list.command_count());

    const auto& opcodes = command_list.opcodes();
    graph.passes.reserve(opcodes.size());
    for (std::size_t index = 0; index < opcodes.size(); ++index) {
        const auto& opcode = opcodes[index];
        const auto kind = pass_kind_for(opcode.opcode);
        if (graph.passes.empty() || graph.passes.back().kind != kind) {
            graph.passes.push_back(RenderFramePass{
                .kind = kind, .first_command_index = static_cast<std::uint32_t>(index)});
        }
        add_command_to_pass(graph.passes.back(), opcode);
    }

    for (const auto& pass : graph.passes) {
        graph.estimated_draw_call_count += pass.estimated_draw_call_count;
    }
    finalize_pass_dependencies(graph);
    return graph;
}

RenderFrameGraph build_render_frame_graph(const RenderScene& scene) {
    RenderFrameGraph graph;
    graph.bounds = scene.bounds();
    graph.fingerprint = scene.fingerprint();
    if (scene.root() != nullptr) {
        append_child_graph(graph, build_render_frame_graph_for_node(*scene.root()));
    }
    finalize_pass_dependencies(graph);
    return graph;
}

} // namespace winelement::rendering
