#include <winelement/rendering/render_scene.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace winelement::rendering {
namespace {

template <typename Value> void hash_combine(std::size_t& seed, const Value& value) noexcept {
    seed ^= std::hash<Value>{}(value) + 0x9E3779B9U + (seed << 6U) + (seed >> 2U);
}

void hash_rect(std::size_t& seed, layout::Rect rect) noexcept {
    hash_combine(seed, rect.x);
    hash_combine(seed, rect.y);
    hash_combine(seed, rect.width);
    hash_combine(seed, rect.height);
}

void hash_transform(std::size_t& seed, Transform2D transform) noexcept {
    hash_combine(seed, transform.m11);
    hash_combine(seed, transform.m12);
    hash_combine(seed, transform.m21);
    hash_combine(seed, transform.m22);
    hash_combine(seed, transform.dx);
    hash_combine(seed, transform.dy);
}

[[nodiscard]] layout::Rect visual_bounds_for(const RenderNode& node) noexcept {
    if (node.kind == RenderNodeKind::Layer) {
        return transform_rect(node.bounds, node.transform);
    }
    return node.bounds;
}

void append_command(RenderCommandRecorder& recorder, const RenderCommandList& source,
                    std::size_t opcode_index) {
    const auto& opcode = source.opcodes().at(opcode_index);
    switch (opcode.opcode) {
    case RenderCommandType::Save:
        recorder.save();
        break;
    case RenderCommandType::Restore:
        recorder.restore();
        break;
    case RenderCommandType::PushClip:
        recorder.push_clip(source.payload<PushClipCommand>(opcode_index).rect);
        break;
    case RenderCommandType::PopClip:
        recorder.pop_clip();
        break;
    case RenderCommandType::PushGeometryClip:
        recorder.push_geometry_clip(source.payload<PushGeometryClipCommand>(opcode_index).geometry);
        break;
    case RenderCommandType::PopGeometryClip:
        recorder.pop_geometry_clip();
        break;
    case RenderCommandType::PushLayer:
        recorder.push_layer(source.payload<PushLayerCommand>(opcode_index).options);
        break;
    case RenderCommandType::PopLayer:
        recorder.pop_layer();
        break;
    case RenderCommandType::DrawLine: {
        const auto& payload = source.payload<DrawLineCommand>(opcode_index);
        recorder.draw_line(payload.start, payload.end, payload.color, payload.stroke_width);
        break;
    }
    case RenderCommandType::FillRect: {
        const auto& payload = source.payload<FillRectCommand>(opcode_index);
        recorder.fill_rect(payload.rect, payload.color);
        break;
    }
    case RenderCommandType::FillPixelSnappedRect: {
        const auto& payload = source.payload<FillPixelSnappedRectCommand>(opcode_index);
        recorder.fill_pixel_snapped_rect(payload.rect, payload.color);
        break;
    }
    case RenderCommandType::StrokePixelSnappedRect: {
        const auto& payload = source.payload<StrokePixelSnappedRectCommand>(opcode_index);
        recorder.stroke_pixel_snapped_rect(payload.rect, payload.color, payload.stroke_width);
        break;
    }
    case RenderCommandType::StrokeRect: {
        const auto& payload = source.payload<StrokeRectCommand>(opcode_index);
        recorder.stroke_rect(payload.rect, payload.color, payload.stroke_width);
        break;
    }
    case RenderCommandType::FillRoundedRect: {
        const auto& payload = source.payload<FillRoundedRectCommand>(opcode_index);
        recorder.fill_rounded_rect(payload.rect, payload.radius, payload.color);
        break;
    }
    case RenderCommandType::StrokeRoundedRect: {
        const auto& payload = source.payload<StrokeRoundedRectCommand>(opcode_index);
        recorder.stroke_rounded_rect(payload.rect, payload.radius, payload.color,
                                     payload.stroke_width);
        break;
    }
    case RenderCommandType::FillEllipse: {
        const auto& payload = source.payload<FillEllipseCommand>(opcode_index);
        recorder.fill_ellipse(payload.rect, payload.color);
        break;
    }
    case RenderCommandType::StrokeEllipse: {
        const auto& payload = source.payload<StrokeEllipseCommand>(opcode_index);
        recorder.stroke_ellipse(payload.rect, payload.color, payload.stroke_width);
        break;
    }
    case RenderCommandType::FillGeometry: {
        const auto& payload = source.payload<FillGeometryCommand>(opcode_index);
        recorder.fill_geometry(payload.geometry, payload.color);
        break;
    }
    case RenderCommandType::StrokeGeometry: {
        const auto& payload = source.payload<StrokeGeometryCommand>(opcode_index);
        recorder.stroke_geometry(payload.geometry, payload.color, payload.style);
        break;
    }
    case RenderCommandType::DrawImage: {
        const auto& payload = source.payload<DrawImageCommand>(opcode_index);
        recorder.draw_image(payload.resource_id, payload.options);
        break;
    }
    case RenderCommandType::DrawText: {
        const auto& payload = source.payload<DrawTextCommand>(opcode_index);
        recorder.draw_text(payload.text, payload.rect, payload.style);
        break;
    }
    case RenderCommandType::DrawTextLayout: {
        const auto& payload = source.payload<DrawTextLayoutCommand>(opcode_index);
        recorder.draw_text_layout(payload.layout, payload.origin);
        break;
    }
    case RenderCommandType::DrawBoxShadow: {
        const auto& payload = source.payload<DrawBoxShadowCommand>(opcode_index);
        recorder.draw_box_shadow(payload.rect, payload.style);
        break;
    }
    }
}

[[nodiscard]] std::uint64_t layer_fingerprint(const RenderLayerOptions& options,
                                              std::uint64_t child_fingerprint,
                                              std::size_t order) noexcept {
    auto seed = static_cast<std::size_t>(0x51C3A11U);
    hash_rect(seed, options.bounds);
    hash_transform(seed, options.transform);
    hash_combine(seed, options.opacity);
    hash_combine(seed, options.clips_to_bounds);
    hash_combine(seed, child_fingerprint);
    hash_combine(seed, order);
    return static_cast<std::uint64_t>(seed);
}

[[nodiscard]] RenderNode parse_retained_node(const RenderCommandList& source, std::size_t& index,
                                             bool stop_on_pop_layer,
                                             const std::string& debug_name) {
    RenderNode node{.kind = RenderNodeKind::Picture, .debug_name = debug_name};
    RenderCommandRecorder picture_recorder(source.prepared_cache());
    auto picture_index = std::size_t{0};
    auto layer_index = std::size_t{0};

    const auto flush_picture = [&]() {
        auto commands = picture_recorder.take_command_list();
        if (commands.empty()) {
            return;
        }
        node.children.push_back(
            RenderNode{.kind = RenderNodeKind::Picture,
                       .bounds = commands.bounds(),
                       .debug_name = debug_name + ".picture." + std::to_string(picture_index++),
                       .fingerprint = commands.fingerprint(),
                       .commands = std::move(commands)});
    };

    const auto& opcodes = source.opcodes();
    while (index < opcodes.size()) {
        const auto type = opcodes[index].opcode;
        if (type == RenderCommandType::PushLayer) {
            flush_picture();
            const auto layer_order = layer_index++;
            const auto options = source.payload<PushLayerCommand>(index).options;
            ++index;
            auto layer = parse_retained_node(source, index, true,
                                             debug_name + ".layer." + std::to_string(layer_order));
            layer.kind = RenderNodeKind::Layer;
            layer.bounds = options.bounds;
            layer.transform = options.transform;
            layer.opacity = std::clamp(options.opacity, 0.0F, 1.0F);
            layer.clips_to_bounds = options.clips_to_bounds;
            layer.fingerprint = layer_fingerprint(options, layer.fingerprint, layer_order);
            node.children.push_back(std::move(layer));
            continue;
        }

        if (type == RenderCommandType::PopLayer && stop_on_pop_layer) {
            ++index;
            break;
        }

        append_command(picture_recorder, source, index);
        ++index;
    }

    flush_picture();

    for (const auto& child : node.children) {
        node.bounds = layout::union_rects(node.bounds, visual_bounds_for(child));
        auto seed = static_cast<std::size_t>(node.fingerprint);
        hash_combine(seed, child.fingerprint);
        hash_combine(seed, static_cast<int>(child.kind));
        node.fingerprint = static_cast<std::uint64_t>(seed);
    }

    return node;
}

void collect_reusable_nodes(
    const RenderNode& node,
    std::unordered_map<std::uint64_t, std::vector<const RenderNode*>>& nodes) {
    if (node.fingerprint != 0U) {
        nodes[node.fingerprint].push_back(&node);
    }
    for (const auto& child : node.children) {
        collect_reusable_nodes(child, nodes);
    }
}

[[nodiscard]] std::size_t count_render_nodes(const RenderNode& node) noexcept {
    auto count = std::size_t{1U};
    for (const auto& child : node.children) {
        count += count_render_nodes(child);
    }
    return count;
}

[[nodiscard]] bool reusable_node_matches(const RenderNode& candidate,
                                         const RenderNode& previous) noexcept {
    return candidate.kind == previous.kind && candidate.fingerprint == previous.fingerprint &&
           candidate.bounds == previous.bounds && candidate.transform == previous.transform &&
           candidate.opacity == previous.opacity &&
           candidate.clips_to_bounds == previous.clips_to_bounds;
}

void reuse_matching_nodes(
    RenderNode& node,
    const std::unordered_map<std::uint64_t, std::vector<const RenderNode*>>& previous_nodes) {
    if (const auto iterator = previous_nodes.find(node.fingerprint);
        iterator != previous_nodes.end()) {
        for (const auto* previous_node : iterator->second) {
            if (previous_node != nullptr && reusable_node_matches(node, *previous_node)) {
                node = *previous_node;
                return;
            }
        }
    }

    for (auto& child : node.children) {
        reuse_matching_nodes(child, previous_nodes);
    }
}

} // namespace

RenderScene::RenderScene() : prepared_cache_(std::make_shared<PreparedRenderCache>()) {}

RenderScene::RenderScene(std::shared_ptr<PreparedRenderCache> prepared_cache)
    : prepared_cache_(prepared_cache != nullptr ? std::move(prepared_cache)
                                                : std::make_shared<PreparedRenderCache>()) {}

RenderScene::RenderScene(const RenderScene& other) {
    prepared_cache_ = other.prepared_cache_ != nullptr ? other.prepared_cache_
                                                       : std::make_shared<PreparedRenderCache>();
    command_fingerprint_ = other.command_fingerprint_;
    command_count_ = other.command_count_;
    if (other.root_ != nullptr) {
        root_ = std::make_unique<RenderNode>(*other.root_);
    }
}

RenderScene& RenderScene::operator=(const RenderScene& other) {
    if (this == &other) {
        return *this;
    }
    prepared_cache_ = other.prepared_cache_ != nullptr ? other.prepared_cache_
                                                       : std::make_shared<PreparedRenderCache>();
    command_fingerprint_ = other.command_fingerprint_;
    command_count_ = other.command_count_;
    if (other.root_ == nullptr) {
        root_.reset();
    } else {
        root_ = std::make_unique<RenderNode>(*other.root_);
    }
    return *this;
}

RenderPictureCache::RenderPictureCache(std::size_t capacity) : cache_(capacity) {}

void RenderPictureCache::set_capacity(std::size_t capacity) {
    cache_.set_capacity(capacity);
}

std::size_t RenderPictureCache::capacity() const noexcept {
    return cache_.capacity();
}

std::size_t RenderPictureCache::size() const noexcept {
    return cache_.size();
}

void RenderPictureCache::clear() noexcept {
    cache_.clear();
}

void RenderPictureCache::store(std::uint64_t fingerprint, RenderCommandList commands) {
    if (fingerprint == 0U) {
        return;
    }
    cache_.put(fingerprint, std::move(commands));
}

const RenderCommandList* RenderPictureCache::find(std::uint64_t fingerprint) const noexcept {
    return cache_.get(fingerprint);
}

void RenderScene::clear() noexcept {
    root_.reset();
    command_fingerprint_ = 0U;
    command_count_ = 0U;
}

void RenderScene::set_root(RenderNode root) {
    command_fingerprint_ = root.fingerprint;
    command_count_ = root.commands.command_count();
    root_ = std::make_unique<RenderNode>(std::move(root));
}

RenderNode render_node_from_commands(RenderCommandList command_list, std::string debug_name) {
    if (command_list.empty()) {
        return RenderNode{.kind = RenderNodeKind::Picture, .debug_name = std::move(debug_name)};
    }

    const auto fingerprint = command_list.fingerprint();
    const auto bounds = command_list.bounds();
    if (debug_name.empty()) {
        debug_name = "scene.node";
    }

    auto index = std::size_t{0};
    auto root = parse_retained_node(command_list, index, false, debug_name);
    root.bounds = bounds;
    root.debug_name = debug_name;
    root.fingerprint = fingerprint;

    if (root.children.size() == 1U && root.children.front().kind == RenderNodeKind::Picture &&
        root.children.front().children.empty()) {
        auto only_picture = std::move(root.children.front());
        only_picture.bounds = bounds;
        only_picture.debug_name = std::move(debug_name);
        only_picture.fingerprint = fingerprint;
        return only_picture;
    }

    return root;
}

void RenderScene::update_from_commands(RenderCommandList command_list, std::string debug_name) {
    if (command_list.empty()) {
        clear();
        return;
    }

    const auto next_fingerprint = command_list.fingerprint();
    const auto next_command_count = command_list.command_count();
    if (root_ != nullptr && command_fingerprint_ == next_fingerprint &&
        command_count_ == next_command_count) {
        return;
    }

    if (debug_name.empty()) {
        debug_name = "scene.root";
    }
    auto previous_nodes = std::unordered_map<std::uint64_t, std::vector<const RenderNode*>>{};
    if (root_ != nullptr) {
        previous_nodes.reserve(count_render_nodes(*root_));
        collect_reusable_nodes(*root_, previous_nodes);
    }
    auto next_root = render_node_from_commands(std::move(command_list), std::move(debug_name));
    reuse_matching_nodes(next_root, previous_nodes);
    command_fingerprint_ = next_fingerprint;
    command_count_ = next_command_count;
    root_ = std::make_unique<RenderNode>(std::move(next_root));
}

bool RenderScene::empty() const noexcept {
    return root_ == nullptr;
}

const RenderNode* RenderScene::root() const noexcept {
    return root_.get();
}

layout::Rect RenderScene::bounds() const noexcept {
    return root_ == nullptr ? layout::Rect{} : root_->bounds;
}

std::uint64_t RenderScene::fingerprint() const noexcept {
    return root_ == nullptr ? 0U : root_->fingerprint;
}

std::shared_ptr<PreparedRenderCache> RenderScene::prepared_cache() const noexcept {
    return prepared_cache_;
}

} // namespace winelement::rendering
