#include <winelement/rendering/render_frame_graph.hpp>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <unordered_map>
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
        auto& pass = graph.passes[index];
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
        const auto can_record_parallel = pass_can_record_parallel(pass);
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
    graph.passes.reserve(graph.passes.size() + child_graph.passes.size());
    graph.passes.insert(graph.passes.end(), child_graph.passes.begin(), child_graph.passes.end());
}

struct CachedNodeFrameGraph {
    std::uint64_t fingerprint = 0U;
    RenderNodeKind kind = RenderNodeKind::Picture;
    layout::Rect bounds{};
    RenderFrameGraph graph{};
};

struct NodeGraphCache {
    std::unordered_map<std::size_t, CachedNodeFrameGraph> entries;
    std::deque<std::size_t> order;
};

void hash_combine(std::size_t& seed, std::size_t value) noexcept {
    seed ^= value + 0x9E3779B9U + (seed << 6U) + (seed >> 2U);
}

void hash_rect(std::size_t& seed, layout::Rect rect) noexcept {
    hash_combine(seed, std::hash<float>{}(rect.x));
    hash_combine(seed, std::hash<float>{}(rect.y));
    hash_combine(seed, std::hash<float>{}(rect.width));
    hash_combine(seed, std::hash<float>{}(rect.height));
}

[[nodiscard]] std::size_t node_graph_cache_key(const RenderNode& node) noexcept {
    auto seed = static_cast<std::size_t>(node.fingerprint);
    hash_combine(seed, static_cast<std::size_t>(node.kind));
    hash_rect(seed, node.bounds);
    return seed;
}

[[nodiscard]] NodeGraphCache& node_graph_cache() {
    thread_local auto cache = NodeGraphCache{};
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
    const auto key = node_graph_cache_key(node);
    if (const auto entry = cache.entries.find(key); entry != cache.entries.end() &&
                                               cache_entry_matches(entry->second, node)) {
        entry->second.graph = std::move(graph);
        return;
    }

    constexpr auto max_cached_node_graphs = 256U;
    if (cache.entries.size() >= max_cached_node_graphs && !cache.order.empty()) {
        cache.entries.erase(cache.order.front());
        cache.order.pop_front();
    }
    cache.entries[key] = CachedNodeFrameGraph{.fingerprint = node.fingerprint,
                                              .kind = node.kind,
                                              .bounds = node.bounds,
                                              .graph = std::move(graph)};
    cache.order.push_back(key);
}

[[nodiscard]] RenderFrameGraph build_render_frame_graph_for_node(const RenderNode& node) {
    auto& cache = node_graph_cache();
    const auto key = node_graph_cache_key(node);
    if (const auto entry = cache.entries.find(key);
        entry != cache.entries.end() && cache_entry_matches(entry->second, node)) {
        return entry->second.graph;
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
