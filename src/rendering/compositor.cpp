#include <winelement/rendering/compositor.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <utility>

namespace winelement::rendering {
namespace {

struct LayerPromotionMetadata {
    RenderLayerOptions options{};
    float area = 0.0F;
    bool visible = false;
    bool identity_transform = true;
    bool should_promote = false;
    CompositorPromotionReason reason = CompositorPromotionReason::StableLayer;
};

[[nodiscard]] LayerPromotionMetadata
layer_promotion_metadata(const RenderLayerOptions& options,
                         CompositorPromotionOptions promotion_options) noexcept {
    auto metadata = LayerPromotionMetadata{.options = options,
                                           .area = options.bounds.width * options.bounds.height,
                                           .visible = layout::is_visible_rect(options.bounds),
                                           .identity_transform =
                                               is_identity_transform(options.transform)};
    if (!metadata.visible) {
        return metadata;
    }
    if (!std::isfinite(metadata.area) || metadata.area < promotion_options.minimum_area) {
        return metadata;
    }
    metadata.should_promote = promotion_options.include_stable_layers || options.opacity < 1.0F ||
                              !metadata.identity_transform;
    if (!metadata.identity_transform) {
        metadata.reason = CompositorPromotionReason::AnimatedTransform;
    } else if (options.opacity < 1.0F) {
        metadata.reason = CompositorPromotionReason::AnimatedOpacity;
    }
    return metadata;
}

void append_candidate(CompositorPromotionPlan& plan, std::uint64_t element_id,
                      const LayerPromotionMetadata& metadata) {
    if (!metadata.should_promote) {
        return;
    }
    const auto& layer_options = metadata.options;
    plan.candidates.push_back(CompositorPromotionCandidate{
        .element_id = element_id,
        .bounds = layer_options.bounds,
        .transform = layer_options.transform,
        .opacity = std::clamp(layer_options.opacity, 0.0F, 1.0F),
        .reason = metadata.reason,
        .clips_to_bounds = layer_options.clips_to_bounds,
        .animates_on_compositor = metadata.reason != CompositorPromotionReason::StableLayer});
}

[[nodiscard]] bool should_stop_promotions(const CompositorPromotionPlan& plan,
                                          CompositorPromotionOptions options) noexcept {
    return plan.candidates.size() >= options.max_candidates;
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

void append_command_promotions(CompositorPromotionPlan& plan,
                               const RenderCommandList& command_list,
                               CompositorPromotionOptions options) {
    const auto& opcodes = command_list.opcodes();
    for (std::size_t index = 0; index < opcodes.size(); ++index) {
        if (should_stop_promotions(plan, options)) {
            return;
        }
        const auto& opcode = opcodes[index];
        if (opcode.opcode != RenderCommandType::PushLayer) {
            continue;
        }

        const auto& layer = command_list.payload<PushLayerCommand>(index).options;
        const auto metadata = layer_promotion_metadata(layer, options);
        append_candidate(plan, stable_layer_id(command_list, index, layer), metadata);
    }
}

void append_scene_promotions(CompositorPromotionPlan& plan, const RenderNode& node,
                             CompositorPromotionOptions options, std::size_t sibling_index = 0U) {
    if (node.kind == RenderNodeKind::Layer) {
        const auto layer_options = RenderLayerOptions{.bounds = node.bounds,
                                                      .opacity = node.opacity,
                                                      .transform = node.transform,
                                                      .clips_to_bounds = node.clips_to_bounds};
        append_candidate(plan, stable_node_layer_id(node, sibling_index),
                         layer_promotion_metadata(layer_options, options));
        if (should_stop_promotions(plan, options)) {
            return;
        }
    }

    if (!node.commands.empty()) {
        append_command_promotions(plan, node.commands, options);
    }

    for (std::size_t index = 0; index < node.children.size(); ++index) {
        if (should_stop_promotions(plan, options)) {
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
    append_command_promotions(plan, command_list, options);

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
