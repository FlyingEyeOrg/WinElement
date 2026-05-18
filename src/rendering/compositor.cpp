#include <winelement/rendering/compositor.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <utility>

namespace winelement::rendering {
namespace {

[[nodiscard]] bool should_promote_layer(const RenderLayerOptions& options,
                                        CompositorPromotionOptions promotion_options) noexcept {
    if (!layout::is_visible_rect(options.bounds)) {
        return false;
    }

    const auto area = options.bounds.width * options.bounds.height;
    if (!std::isfinite(area) || area < promotion_options.minimum_area) {
        return false;
    }

    return promotion_options.include_stable_layers || options.opacity < 1.0F ||
           !is_identity_transform(options.transform);
}

[[nodiscard]] CompositorPromotionReason
promotion_reason_for(const RenderLayerOptions& options) noexcept {
    if (!is_identity_transform(options.transform)) {
        return CompositorPromotionReason::AnimatedTransform;
    }
    if (options.opacity < 1.0F) {
        return CompositorPromotionReason::AnimatedOpacity;
    }
    return CompositorPromotionReason::StableLayer;
}

void hash_combine(std::size_t& seed, std::size_t value) noexcept {
    seed ^= value + 0x9E3779B9U + (seed << 6U) + (seed >> 2U);
}

void hash_float_bucket(std::size_t& seed, float value) noexcept {
    const auto bucket = static_cast<std::int64_t>(std::round(value * 16.0F));
    hash_combine(seed, std::hash<std::int64_t>{}(bucket));
}

[[nodiscard]] std::uint64_t stable_layer_id(const RenderCommandList& command_list,
                                            std::size_t opcode_index,
                                            const RenderLayerOptions& options) noexcept {
    auto seed = static_cast<std::size_t>(0xC0DEC0DEU);
    hash_combine(seed, static_cast<std::size_t>(command_list.command_count()));
    hash_combine(seed, opcode_index);
    hash_float_bucket(seed, options.bounds.x);
    hash_float_bucket(seed, options.bounds.y);
    hash_float_bucket(seed, options.bounds.width);
    hash_float_bucket(seed, options.bounds.height);
    return static_cast<std::uint64_t>(seed == 0U ? opcode_index + 1U : seed);
}

[[nodiscard]] std::uint64_t stable_node_layer_id(const RenderNode& node,
                                                 std::size_t sibling_index) noexcept {
    auto seed = static_cast<std::size_t>(node.fingerprint == 0U ? 0xC0DEC0DEU : node.fingerprint);
    hash_combine(seed, sibling_index);
    hash_float_bucket(seed, node.bounds.x);
    hash_float_bucket(seed, node.bounds.y);
    hash_float_bucket(seed, node.bounds.width);
    hash_float_bucket(seed, node.bounds.height);
    return static_cast<std::uint64_t>(seed == 0U ? sibling_index + 1U : seed);
}

void append_scene_promotions(CompositorPromotionPlan& plan, const RenderNode& node,
                             CompositorPromotionOptions options, std::size_t sibling_index = 0U) {
    if (node.kind == RenderNodeKind::Layer) {
        const auto layer_options = RenderLayerOptions{.bounds = node.bounds,
                                                      .opacity = node.opacity,
                                                      .transform = node.transform,
                                                      .clips_to_bounds = node.clips_to_bounds};
        if (should_promote_layer(layer_options, options)) {
            const auto reason = promotion_reason_for(layer_options);
            plan.candidates.push_back(CompositorPromotionCandidate{
                .element_id = stable_node_layer_id(node, sibling_index),
                .bounds = layer_options.bounds,
                .transform = layer_options.transform,
                .opacity = std::clamp(layer_options.opacity, 0.0F, 1.0F),
                .reason = reason,
                .clips_to_bounds = layer_options.clips_to_bounds,
                .animates_on_compositor = reason != CompositorPromotionReason::StableLayer});
            if (plan.candidates.size() >= options.max_candidates) {
                return;
            }
        }
    }

    if (!node.commands.empty()) {
        auto command_plan = build_compositor_promotion_plan(node.commands, options);
        for (auto& candidate : command_plan.candidates) {
            if (plan.candidates.size() >= options.max_candidates) {
                return;
            }
            plan.candidates.push_back(std::move(candidate));
        }
    }

    for (std::size_t index = 0; index < node.children.size(); ++index) {
        if (plan.candidates.size() >= options.max_candidates) {
            return;
        }
        append_scene_promotions(plan, node.children[index], options, index);
    }
}

} // namespace

CompositorPromotionPlan build_compositor_promotion_plan(const RenderCommandList& command_list,
                                                        CompositorPromotionOptions options) {
    CompositorPromotionPlan plan;
    if (options.max_candidates == 0U) {
        return plan;
    }

    const auto& opcodes = command_list.opcodes();
    plan.candidates.reserve(std::min(options.max_candidates, opcodes.size()));
    for (std::size_t index = 0; index < opcodes.size(); ++index) {
        const auto& opcode = opcodes[index];
        if (opcode.opcode != RenderCommandType::PushLayer) {
            continue;
        }

        const auto& layer = command_list.payload<PushLayerCommand>(index).options;
        if (!should_promote_layer(layer, options)) {
            continue;
        }

        const auto reason = promotion_reason_for(layer);
        plan.candidates.push_back(CompositorPromotionCandidate{
            .element_id = stable_layer_id(command_list, index, layer),
            .bounds = layer.bounds,
            .transform = layer.transform,
            .opacity = std::clamp(layer.opacity, 0.0F, 1.0F),
            .reason = reason,
            .clips_to_bounds = layer.clips_to_bounds,
            .animates_on_compositor = reason != CompositorPromotionReason::StableLayer});
        if (plan.candidates.size() >= options.max_candidates) {
            break;
        }
    }

    return plan;
}

CompositorPromotionPlan build_compositor_promotion_plan(const RenderScene& scene,
                                                        CompositorPromotionOptions options) {
    CompositorPromotionPlan plan;
    if (scene.root() == nullptr || options.max_candidates == 0U) {
        return plan;
    }

    append_scene_promotions(plan, *scene.root(), options);
    if (plan.candidates.size() > options.max_candidates) {
        plan.candidates.resize(options.max_candidates);
    }
    return plan;
}

CompositorFrameBuilder::CompositorFrameBuilder(std::uint64_t frame_id) {
    frame_.frame_id = frame_id;
}

CompositorFrameBuilder& CompositorFrameBuilder::set_clear_color(Color color) noexcept {
    frame_.clear_color = color;
    return *this;
}

CompositorFrameBuilder& CompositorFrameBuilder::set_dirty_region(DirtyRegion dirty_region) {
    frame_.dirty_region = std::move(dirty_region);
    return *this;
}

CompositorFrameBuilder& CompositorFrameBuilder::set_render_scene(RenderScene render_scene) {
    frame_.render_scene = std::move(render_scene);
    return *this;
}

CompositorFrameBuilder& CompositorFrameBuilder::add_layer(CompositorLayer layer) {
    frame_.layers.push_back(std::move(layer));
    return *this;
}

CompositorFrame CompositorFrameBuilder::build() {
    return std::move(frame_);
}

} // namespace winelement::rendering
