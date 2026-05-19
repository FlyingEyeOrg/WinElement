#include "d3d11_display_list_renderer.hpp"

#ifdef DrawText
#undef DrawText
#endif

#include "d3d11_display_list_ps.hpp"
#include "d3d11_display_list_vs.hpp"

#include <winelement/core/core_types.hpp>
#include <winelement/platform/render_thread_pool.hpp>
#include <winelement/rendering/render_command_list.hpp>
#include <winelement/rendering/render_frame_graph.hpp>
#include <winelement/rendering/render_scene.hpp>
#include <winelement/rendering/render_types.hpp>
#include <winelement/rendering/text_engine.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#ifdef DrawText
#undef DrawText
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace winelement::platform::win32 {
namespace {

constexpr auto default_dpi = 96.0F;
constexpr auto triangle_vertex_count = 3U;
constexpr auto max_vertices = 16383U;
constexpr auto pi = 3.14159265358979323846F;
constexpr auto glyph_atlas_width = 1024U;
constexpr auto glyph_atlas_height = 1024U;
constexpr auto glyph_atlas_padding = 2U;
constexpr auto glyph_atlas_bytes_per_pixel = 4U;
constexpr auto geometry_epsilon = 0.0001F;
constexpr auto contour_cleanup_epsilon = 0.001F;
constexpr auto geometry_flattening_tolerance = 0.01F;
constexpr auto max_curve_flattening_depth = 12U;
constexpr auto smooth_stroke_join_cosine = 0.94F;
constexpr auto continuous_join_cover_cosine = 0.997F;

struct FrameConstants {
    float target_width = 1.0F;
    float target_height = 1.0F;
    float textured = 0.0F;
    float texture_mode = 0.0F;
};

struct PreparedRenderTile {
    rendering::layout::Rect device_clip{};
    rendering::layout::Rect cull_clip{};
    std::vector<std::uint32_t> command_indices;
    bool uses_shared_command_indices = false;
};

[[nodiscard]] const std::vector<std::vector<rendering::layout::Point>>&
prepared_geometry_figures(const rendering::PreparedGeometryFill& prepared_fill) noexcept {
    static const std::vector<std::vector<rendering::layout::Point>> empty_figures;
    return prepared_fill.flatten != nullptr ? prepared_fill.flatten->figures : empty_figures;
}

[[nodiscard]] const std::vector<std::vector<rendering::layout::Point>>&
prepared_geometry_figures(const rendering::PreparedGeometryStroke& prepared_stroke) noexcept {
    static const std::vector<std::vector<rendering::layout::Point>> empty_figures;
    return prepared_stroke.flatten != nullptr ? prepared_stroke.flatten->figures : empty_figures;
}

[[nodiscard]] bool
has_prepared_geometry_fill(const rendering::PreparedGeometryFill* prepared_fill) noexcept {
    return prepared_fill != nullptr && (!prepared_geometry_figures(*prepared_fill).empty() ||
                                        !prepared_fill->filled_contours.empty() ||
                                        !prepared_fill->tessellated_vertices.empty());
}

struct PreparedRenderPassMetrics {
    rendering::RenderFramePassKind kind = rendering::RenderFramePassKind::State;
    std::uint32_t first_command_index = 0U;
    std::uint32_t command_count = 0U;
    std::uint32_t visible_command_count = 0U;
};

struct PreparedRenderPlan {
    std::vector<PreparedRenderTile> tiles;
    std::vector<PreparedRenderPassMetrics> passes;
    std::vector<std::uint32_t> shared_command_indices;
    std::uint32_t visible_command_count = 0U;
    bool prepared_in_parallel = false;
};

template <typename Value> void cache_hash_combine(std::size_t& seed, const Value& value) noexcept {
    seed ^= std::hash<Value>{}(value) + 0x9E3779B97F4A7C15ULL + (seed << 6U) + (seed >> 2U);
}

struct QuantizedRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
    rendering::layout::Rect rect{};
};

[[nodiscard]] QuantizedRect quantize_rect_outward(rendering::layout::Rect rect) noexcept {
    constexpr auto bucket_size = 64.0F;
    const auto left = static_cast<std::int32_t>(std::floor(rect.x / bucket_size));
    const auto top = static_cast<std::int32_t>(std::floor(rect.y / bucket_size));
    const auto right = static_cast<std::int32_t>(std::ceil((rect.x + rect.width) / bucket_size));
    const auto bottom = static_cast<std::int32_t>(std::ceil((rect.y + rect.height) / bucket_size));
    return QuantizedRect{
        .left = left,
        .top = top,
        .right = right,
        .bottom = bottom,
        .rect = rendering::layout::Rect{static_cast<float>(left) * bucket_size,
                                        static_cast<float>(top) * bucket_size,
                                        static_cast<float>(right - left) * bucket_size,
                                        static_cast<float>(bottom - top) * bucket_size}};
}

void cache_hash_quantized_rect(std::size_t& seed, QuantizedRect rect) noexcept {
    cache_hash_combine(seed, rect.left);
    cache_hash_combine(seed, rect.top);
    cache_hash_combine(seed, rect.right);
    cache_hash_combine(seed, rect.bottom);
}

void cache_hash_rect_exact(std::size_t& seed, rendering::layout::Rect rect) noexcept {
    cache_hash_combine(seed, rect.x);
    cache_hash_combine(seed, rect.y);
    cache_hash_combine(seed, rect.width);
    cache_hash_combine(seed, rect.height);
}

[[nodiscard]] std::uint64_t visibility_tile_cache_key(const rendering::RenderCommandList& commands,
                                                      rendering::layout::Rect cull_clip,
                                                      bool force_all_commands) noexcept {
    auto seed = static_cast<std::size_t>(0x71ECA11U);
    cache_hash_combine(seed, commands.fingerprint());
    cache_hash_combine(seed, commands.command_count());
    cache_hash_combine(seed, force_all_commands);
    if (!force_all_commands) {
        cache_hash_quantized_rect(seed, quantize_rect_outward(cull_clip));
    }
    return static_cast<std::uint64_t>(seed);
}

[[nodiscard]] std::uint64_t pass_metrics_cache_key(const rendering::RenderCommandList& commands,
                                                   const rendering::RenderFrameGraph& frame_graph,
                                                   rendering::layout::Rect cull_bounds,
                                                   bool force_all_commands) noexcept {
    auto seed = static_cast<std::size_t>(0x6A551E7U);
    cache_hash_combine(seed, commands.fingerprint());
    cache_hash_combine(seed, commands.command_count());
    cache_hash_combine(seed, frame_graph.fingerprint);
    cache_hash_combine(seed, frame_graph.command_count);
    cache_hash_combine(seed, force_all_commands);
    if (!force_all_commands) {
        cache_hash_quantized_rect(seed, quantize_rect_outward(cull_bounds));
    }
    return static_cast<std::uint64_t>(seed);
}

[[nodiscard]] std::uint64_t render_plan_cache_key(const rendering::RenderCommandList& commands,
                                                  std::span<const D3D11RenderDirtyClip> dirty_clips,
                                                  const rendering::RenderFrameGraph* frame_graph,
                                                  float target_dip_width, float target_dip_height,
                                                  bool force_all_commands) noexcept {
    auto seed = static_cast<std::size_t>(0x51A7E91U);
    cache_hash_combine(seed, commands.fingerprint());
    cache_hash_combine(seed, commands.command_count());
    cache_hash_combine(seed, static_cast<std::int32_t>(std::round(target_dip_width * 8.0F)));
    cache_hash_combine(seed, static_cast<std::int32_t>(std::round(target_dip_height * 8.0F)));
    cache_hash_combine(seed, force_all_commands);
    if (frame_graph != nullptr) {
        cache_hash_combine(seed, frame_graph->fingerprint);
        cache_hash_combine(seed, frame_graph->command_count);
        cache_hash_combine(seed, frame_graph->passes.size());
    }
    cache_hash_combine(seed, dirty_clips.size());
    for (const auto clip : dirty_clips) {
        cache_hash_rect_exact(seed, clip.device_clip);
        cache_hash_rect_exact(seed, clip.cull_clip);
    }
    return static_cast<std::uint64_t>(seed);
}

[[nodiscard]] std::size_t
prepared_geometry_fill_signature(const rendering::PreparedGeometryFill& prepared_fill) noexcept {
    auto seed = std::size_t{};
    cache_hash_combine(seed, prepared_fill.flatten.get());
    cache_hash_combine(seed, prepared_fill.filled_contours.size());
    cache_hash_combine(seed, prepared_fill.tessellated_vertices.size());
    cache_hash_combine(seed, prepared_fill.bounds.x);
    cache_hash_combine(seed, prepared_fill.bounds.y);
    cache_hash_combine(seed, prepared_fill.bounds.width);
    cache_hash_combine(seed, prepared_fill.bounds.height);
    for (const auto& contour : prepared_fill.filled_contours) {
        cache_hash_combine(seed, contour.data());
        cache_hash_combine(seed, contour.size());
    }
    return seed;
}

[[nodiscard]] std::size_t prepared_text_glyphs_signature(
    const rendering::PreparedTextGlyphCoverageList* prepared_glyphs) noexcept {
    if (prepared_glyphs == nullptr) {
        return 0U;
    }
    auto seed = std::size_t{};
    cache_hash_combine(seed, prepared_glyphs->glyphs.size());
    for (const auto& glyph : prepared_glyphs->glyphs) {
        cache_hash_combine(seed, glyph.get());
    }
    return seed;
}

[[nodiscard]] bool multiplied_at_least(std::size_t left, std::size_t right,
                                       std::size_t threshold) noexcept {
    return right != 0U && left >= (threshold + right - 1U) / right;
}

[[nodiscard]] bool should_try_parallel_render_plan(std::size_t command_count,
                                                   std::size_t tile_count,
                                                   std::size_t pass_count) noexcept {
    constexpr auto tile_work_threshold = 16384U;
    constexpr auto pass_command_threshold = 4096U;
    if (command_count < 1024U) {
        return false;
    }

    const auto has_tile_work =
        tile_count >= 4U && multiplied_at_least(command_count, tile_count, tile_work_threshold);
    const auto has_pass_work = pass_count >= 4U && command_count >= pass_command_threshold;
    return has_tile_work || has_pass_work;
}

[[nodiscard]] std::runtime_error make_hresult_error(std::string_view message, HRESULT result) {
    auto text = std::string(message);
    text += " HRESULT=0x";

    constexpr auto digits = "0123456789ABCDEF";
    for (auto shift = 28; shift >= 0; shift -= 4) {
        text += digits[(static_cast<unsigned long>(result) >> shift) & 0x0F];
    }

    return std::runtime_error(text);
}

void throw_if_failed(HRESULT result, std::string_view message) {
    if (FAILED(result)) {
        throw make_hresult_error(message, result);
    }
}

[[nodiscard]] bool is_draw_command(rendering::RenderCommandType type) noexcept {
    switch (type) {
    case rendering::RenderCommandType::DrawLine:
    case rendering::RenderCommandType::FillRect:
    case rendering::RenderCommandType::FillPixelSnappedRect:
    case rendering::RenderCommandType::StrokePixelSnappedRect:
    case rendering::RenderCommandType::StrokeRect:
    case rendering::RenderCommandType::FillRoundedRect:
    case rendering::RenderCommandType::StrokeRoundedRect:
    case rendering::RenderCommandType::FillEllipse:
    case rendering::RenderCommandType::StrokeEllipse:
    case rendering::RenderCommandType::FillGeometry:
    case rendering::RenderCommandType::StrokeGeometry:
    case rendering::RenderCommandType::DrawImage:
    case rendering::RenderCommandType::DrawText:
    case rendering::RenderCommandType::DrawTextLayout:
    case rendering::RenderCommandType::DrawBoxShadow:
        return true;
    case rendering::RenderCommandType::Save:
    case rendering::RenderCommandType::Restore:
    case rendering::RenderCommandType::PushClip:
    case rendering::RenderCommandType::PopClip:
    case rendering::RenderCommandType::PushGeometryClip:
    case rendering::RenderCommandType::PopGeometryClip:
    case rendering::RenderCommandType::PushLayer:
    case rendering::RenderCommandType::PopLayer:
    default:
        return false;
    }
}

[[nodiscard]] rendering::layout::Rect
visual_bounds_for_node(const rendering::RenderNode& node) noexcept {
    if (node.kind == rendering::RenderNodeKind::Layer) {
        return rendering::transform_rect(node.bounds, node.transform);
    }
    return node.bounds;
}

[[nodiscard]] bool dirty_clips_intersect_node(std::span<const D3D11RenderDirtyClip> dirty_clips,
                                              const rendering::RenderNode& node) noexcept {
    if (dirty_clips.empty()) {
        return false;
    }

    const auto bounds = visual_bounds_for_node(node);
    if (!rendering::layout::is_visible_rect(bounds)) {
        return true;
    }

    for (const auto clip : dirty_clips) {
        if (rendering::layout::rects_intersect(bounds, clip.cull_clip)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool dirty_clips_intersect_rect(std::span<const D3D11RenderDirtyClip> dirty_clips,
                                              rendering::layout::Rect bounds) noexcept {
    if (!rendering::layout::is_visible_rect(bounds)) {
        return true;
    }

    for (const auto clip : dirty_clips) {
        if (rendering::layout::rects_intersect(bounds, clip.cull_clip)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool rect_contains_rect(rendering::layout::Rect outer,
                                      rendering::layout::Rect inner) noexcept {
    return outer.x <= inner.x && outer.y <= inner.y &&
           outer.x + outer.width >= inner.x + inner.width &&
           outer.y + outer.height >= inner.y + inner.height;
}

[[nodiscard]] const D3D11RenderDirtyClip*
covering_dirty_clip(std::span<const D3D11RenderDirtyClip> dirty_clips,
                    rendering::layout::Rect bounds) noexcept {
    if (!rendering::layout::is_visible_rect(bounds)) {
        return nullptr;
    }

    const auto* best = static_cast<const D3D11RenderDirtyClip*>(nullptr);
    auto best_area = std::numeric_limits<float>::max();
    for (const auto& dirty_clip : dirty_clips) {
        if (!rect_contains_rect(dirty_clip.cull_clip, bounds)) {
            continue;
        }

        const auto area = dirty_clip.cull_clip.width * dirty_clip.cull_clip.height;
        if (best == nullptr || area < best_area) {
            best = &dirty_clip;
            best_area = area;
        }
    }
    return best;
}

[[nodiscard]] std::optional<rendering::Transform2D>
inverse_transform(rendering::Transform2D transform) noexcept {
    const auto determinant = transform.m11 * transform.m22 - transform.m12 * transform.m21;
    if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001F) {
        return std::nullopt;
    }

    const auto inverse_det = 1.0F / determinant;
    auto inverse = rendering::Transform2D{.m11 = transform.m22 * inverse_det,
                                          .m12 = -transform.m12 * inverse_det,
                                          .m21 = -transform.m21 * inverse_det,
                                          .m22 = transform.m11 * inverse_det};
    inverse.dx = -(transform.dx * inverse.m11 + transform.dy * inverse.m21);
    inverse.dy = -(transform.dx * inverse.m12 + transform.dy * inverse.m22);
    return inverse;
}

[[nodiscard]] float rect_area(rendering::layout::Rect rect) noexcept {
    return std::max(rect.width, 0.0F) * std::max(rect.height, 0.0F);
}

void dirty_clips_from_region(const rendering::DirtyRegion& dirty_region,
                             rendering::layout::Size target_size,
                             std::vector<D3D11RenderDirtyClip>& clips) {
    clips.clear();
    const auto& rects = dirty_region.rects();
    if (rects.empty()) {
        return;
    }

    const auto target_bounds = rendering::layout::Rect{
        0.0F, 0.0F, std::max(target_size.width, 1.0F), std::max(target_size.height, 1.0F)};
    auto union_rect = rendering::layout::Rect{};
    auto dirty_area = 0.0F;
    auto visible_count = std::size_t{0U};
    for (const auto rect : rects) {
        auto clipped = rendering::layout::intersect_rects(rect, target_bounds);
        if (!rendering::layout::is_visible_rect(clipped)) {
            continue;
        }
        union_rect =
            visible_count == 0U ? clipped : rendering::layout::union_rects(union_rect, clipped);
        dirty_area += rect_area(clipped);
        ++visible_count;
    }

    if (visible_count == 0U || !rendering::layout::is_visible_rect(union_rect)) {
        return;
    }

    const auto union_area = rect_area(union_rect);
    const auto target_area = rect_area(target_bounds);
    const auto should_coalesce =
        visible_count > 96U || dirty_area >= target_area * 0.65F ||
        (visible_count > 24U && union_area > 0.0F && dirty_area >= union_area * 0.55F);
    if (should_coalesce) {
        clips.push_back(D3D11RenderDirtyClip{.device_clip = union_rect, .cull_clip = union_rect});
        return;
    }

    clips.reserve(visible_count);
    for (const auto rect : rects) {
        const auto clipped = rendering::layout::intersect_rects(rect, target_bounds);
        if (rendering::layout::is_visible_rect(clipped)) {
            clips.push_back(D3D11RenderDirtyClip{.device_clip = clipped, .cull_clip = clipped});
        }
    }
}

[[nodiscard]] std::optional<std::vector<D3D11RenderDirtyClip>>
dirty_clips_for_layer(std::span<const D3D11RenderDirtyClip> parent_clips,
                      const rendering::RenderNode& layer) {
    if (layer.kind != rendering::RenderNodeKind::Layer) {
        return std::nullopt;
    }

    const auto inverse = inverse_transform(layer.transform);
    if (!inverse.has_value()) {
        return std::nullopt;
    }

    auto clips = std::vector<D3D11RenderDirtyClip>{};
    clips.reserve(parent_clips.size());
    for (const auto parent_clip : parent_clips) {
        auto cull_clip = rendering::transform_rect(parent_clip.cull_clip, *inverse);
        if (layer.clips_to_bounds) {
            cull_clip = rendering::layout::intersect_rects(cull_clip, layer.bounds);
        }
        if (!rendering::layout::is_visible_rect(cull_clip)) {
            continue;
        }
        clips.push_back(
            D3D11RenderDirtyClip{.device_clip = parent_clip.device_clip, .cull_clip = cull_clip});
    }
    return clips;
}

[[nodiscard]] rendering::TextEngine& render_worker_text_engine() {
    thread_local rendering::TextEngine engine;
    thread_local bool configured = false;
    if (!configured) {
        engine.set_max_cached_layouts(12U);
        configured = true;
    }
    return engine;
}

[[nodiscard]] std::vector<D3D11RenderDirtyClip>
dirty_clips_for_clip_rect(std::span<const D3D11RenderDirtyClip> parent_clips,
                          rendering::layout::Rect clip_rect) {
    auto clips = std::vector<D3D11RenderDirtyClip>{};
    clips.reserve(parent_clips.size());
    for (const auto parent_clip : parent_clips) {
        const auto cull_clip = rendering::layout::intersect_rects(parent_clip.cull_clip, clip_rect);
        if (rendering::layout::is_visible_rect(cull_clip)) {
            clips.push_back(D3D11RenderDirtyClip{.device_clip = parent_clip.device_clip,
                                                 .cull_clip = cull_clip});
        }
    }
    return clips;
}

void append_opcode_index(std::vector<std::uint32_t>& indices, std::size_t index) {
    if (index <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        indices.push_back(static_cast<std::uint32_t>(index));
    }
}

[[nodiscard]] std::vector<D3D11RenderDirtyClip>
build_render_tiles(std::span<const D3D11RenderDirtyClip> dirty_clips, float target_dip_width,
                   float target_dip_height) {
    constexpr auto tile_extent = 384.0F;
    constexpr auto max_tiles_per_rect = 16U;
    auto tiles = std::vector<D3D11RenderDirtyClip>{};
    tiles.reserve(dirty_clips.size());

    const auto target_bounds = rendering::layout::Rect{0.0F, 0.0F, std::max(target_dip_width, 1.0F),
                                                       std::max(target_dip_height, 1.0F)};
    for (const auto dirty_clip : dirty_clips) {
        auto rect = dirty_clip.device_clip;
        if (!rendering::layout::rects_intersect(rect, target_bounds)) {
            continue;
        }

        const auto right = std::min(rect.x + rect.width, target_bounds.x + target_bounds.width);
        const auto bottom = std::min(rect.y + rect.height, target_bounds.y + target_bounds.height);
        rect.x = std::max(rect.x, target_bounds.x);
        rect.y = std::max(rect.y, target_bounds.y);
        rect.width = std::max(right - rect.x, 0.0F);
        rect.height = std::max(bottom - rect.y, 0.0F);
        if (!rendering::layout::is_visible_rect(rect)) {
            continue;
        }
        if (dirty_clip.device_clip != dirty_clip.cull_clip) {
            tiles.push_back(
                D3D11RenderDirtyClip{.device_clip = rect, .cull_clip = dirty_clip.cull_clip});
            continue;
        }

        auto columns = static_cast<std::uint32_t>(std::ceil(rect.width / tile_extent));
        auto rows = static_cast<std::uint32_t>(std::ceil(rect.height / tile_extent));
        columns = std::clamp(columns, 1U, 8U);
        rows = std::clamp(rows, 1U, 8U);
        while (columns * rows > max_tiles_per_rect) {
            if (columns >= rows && columns > 1U) {
                --columns;
            } else if (rows > 1U) {
                --rows;
            } else {
                break;
            }
        }

        const auto tile_width = std::ceil(rect.width / static_cast<float>(columns));
        const auto tile_height = std::ceil(rect.height / static_cast<float>(rows));
        for (std::uint32_t row = 0U; row < rows; ++row) {
            for (std::uint32_t column = 0U; column < columns; ++column) {
                const auto x = column == 0U
                                   ? rect.x
                                   : std::min(rect.x + rect.width,
                                              rect.x + tile_width * static_cast<float>(column));
                const auto y = row == 0U
                                   ? rect.y
                                   : std::min(rect.y + rect.height,
                                              rect.y + tile_height * static_cast<float>(row));
                const auto tile_right = column + 1U == columns
                                            ? rect.x + rect.width
                                            : std::min(rect.x + rect.width, x + tile_width);
                const auto tile_bottom = row + 1U == rows
                                             ? rect.y + rect.height
                                             : std::min(rect.y + rect.height, y + tile_height);
                const auto device_tile =
                    rendering::layout::Rect{x, y, tile_right - x, tile_bottom - y};
                const auto cull_tile = dirty_clip.device_clip == dirty_clip.cull_clip
                                           ? device_tile
                                           : dirty_clip.cull_clip;
                if (rendering::layout::is_visible_rect(cull_tile)) {
                    tiles.push_back(
                        D3D11RenderDirtyClip{.device_clip = device_tile, .cull_clip = cull_tile});
                }
            }
        }
    }
    return tiles;
}

[[nodiscard]] bool dirty_clips_overlap(std::span<const D3D11RenderDirtyClip> dirty_clips) noexcept {
    for (std::size_t left = 0U; left < dirty_clips.size(); ++left) {
        for (std::size_t right = left + 1U; right < dirty_clips.size(); ++right) {
            if (rendering::layout::rects_intersect(dirty_clips[left].device_clip,
                                                   dirty_clips[right].device_clip)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::vector<D3D11RenderDirtyClip>
slice_dirty_clips_for_parallel_recording(std::span<const D3D11RenderDirtyClip> dirty_clips,
                                         float target_dip_width, float target_dip_height) {
    if (dirty_clips.empty()) {
        return {};
    }

    if (!dirty_clips_overlap(dirty_clips)) {
        return build_render_tiles(dirty_clips, target_dip_width, target_dip_height);
    }

    auto union_clip = dirty_clips.front().device_clip;
    for (std::size_t index = 1U; index < dirty_clips.size(); ++index) {
        union_clip = rendering::layout::union_rects(union_clip, dirty_clips[index].device_clip);
    }
    const auto coalesced = std::array<D3D11RenderDirtyClip, 1U>{
        D3D11RenderDirtyClip{.device_clip = union_clip, .cull_clip = union_clip}};
    return build_render_tiles(coalesced, target_dip_width, target_dip_height);
}

[[nodiscard]] std::size_t command_count_for_subtree(const rendering::RenderNode& node) noexcept {
    auto count = node.commands.command_count();
    for (const auto& child : node.children) {
        count += command_count_for_subtree(child);
    }
    return count;
}

[[nodiscard]] bool command_list_has_serial_barrier(
    const rendering::RenderCommandList& commands) noexcept {
    for (const auto& opcode : commands.opcodes()) {
        switch (opcode.opcode) {
        case rendering::RenderCommandType::PushGeometryClip:
        case rendering::RenderCommandType::PopGeometryClip:
        case rendering::RenderCommandType::PushLayer:
        case rendering::RenderCommandType::PopLayer:
            return true;
        default:
            break;
        }
    }
    return false;
}

[[nodiscard]] bool subtree_has_serial_barrier(const rendering::RenderNode& node) noexcept {
    if (node.kind == rendering::RenderNodeKind::Clip ||
        (node.kind == rendering::RenderNodeKind::Layer &&
         (node.opacity < 1.0F || !rendering::is_identity_transform(node.transform)))) {
        return true;
    }
    if (command_list_has_serial_barrier(node.commands)) {
        return true;
    }
    for (const auto& child : node.children) {
        if (subtree_has_serial_barrier(child)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::size_t estimate_vertex_budget_for_command_count(
    std::size_t command_count) noexcept {
    return std::min<std::size_t>(command_count * 24U, max_vertices);
}

void collect_all_opcode_indices(const std::vector<rendering::RenderOpcodeRecord>& opcodes,
                                std::vector<std::uint32_t>& output) {
    output.clear();
    const auto count = std::min<std::size_t>(
        opcodes.size(), static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1U);
    output.resize(count);
    std::iota(output.begin(), output.end(), 0U);
}

void add_visible_command_count(std::uint32_t& total, std::size_t count) noexcept {
    const auto remaining = std::numeric_limits<std::uint32_t>::max() - total;
    total += static_cast<std::uint32_t>(std::min<std::size_t>(count, remaining));
}

void collect_visible_opcode_indices(const rendering::RenderCommandList& commands,
                                    rendering::layout::Rect dirty_rect,
                                    std::vector<std::uint32_t>& output) {
    const auto& opcodes = commands.opcodes();
    output.clear();
    output.reserve(opcodes.size());

    struct LayerCullState {
        rendering::layout::Rect dirty_rect{};
        bool force_all = false;
    };

    constexpr auto max_layer_stack_depth = 16U;
    std::array<LayerCullState, max_layer_stack_depth> layer_stack{};
    std::vector<LayerCullState> overflow_layer_stack;
    overflow_layer_stack.reserve(4U);
    auto layer_stack_size = std::size_t{0U};
    auto skipped_layer_depth = 0U;
    const auto current_layer_state = [&]() -> const LayerCullState* {
        if (layer_stack_size == 0U) {
            return nullptr;
        }
        if (layer_stack_size <= layer_stack.size()) {
            return &layer_stack[layer_stack_size - 1U];
        }
        return &overflow_layer_stack[layer_stack_size - layer_stack.size() - 1U];
    };
    const auto push_layer_state = [&](LayerCullState state) {
        if (layer_stack_size < layer_stack.size()) {
            layer_stack[layer_stack_size++] = state;
        } else {
            overflow_layer_stack.push_back(state);
            ++layer_stack_size;
        }
    };
    const auto pop_layer_state = [&]() {
        if (layer_stack_size == 0U) {
            return;
        }
        if (layer_stack_size > layer_stack.size()) {
            overflow_layer_stack.pop_back();
        }
        --layer_stack_size;
    };

    for (std::size_t index = 0; index < opcodes.size(); ++index) {
        const auto& opcode = opcodes[index];
        if (skipped_layer_depth > 0U) {
            if (opcode.opcode == rendering::RenderCommandType::PushLayer) {
                ++skipped_layer_depth;
            } else if (opcode.opcode == rendering::RenderCommandType::PopLayer &&
                       skipped_layer_depth > 0U) {
                --skipped_layer_depth;
            }
            continue;
        }

        const auto* current_layer = current_layer_state();
        const auto current_force_all = current_layer != nullptr && current_layer->force_all;
        const auto current_dirty_rect = current_layer == nullptr ? dirty_rect
                                                                 : current_layer->dirty_rect;

        if (current_force_all) {
            append_opcode_index(output, index);
            if (opcode.opcode == rendering::RenderCommandType::PushLayer) {
                push_layer_state(LayerCullState{.dirty_rect = current_dirty_rect,
                                                .force_all = true});
            } else if (opcode.opcode == rendering::RenderCommandType::PopLayer) {
                pop_layer_state();
            }
            continue;
        }

        if (opcode.opcode == rendering::RenderCommandType::PushLayer) {
            if (!rendering::layout::rects_intersect(opcode.bounds, current_dirty_rect)) {
                skipped_layer_depth = 1U;
                continue;
            }

            const auto& layer = commands.payload<rendering::PushLayerCommand>(index).options;
            auto layer_dirty_rect = current_dirty_rect;
            auto force_layer_commands = false;
            if (!rendering::is_identity_transform(layer.transform)) {
                if (const auto inverse = inverse_transform(layer.transform)) {
                    layer_dirty_rect = rendering::transform_rect(current_dirty_rect, *inverse);
                } else {
                    force_layer_commands = true;
                }
            }
            if (!force_layer_commands && layer.clips_to_bounds) {
                layer_dirty_rect =
                    rendering::layout::intersect_rects(layer_dirty_rect, layer.bounds);
            }
            if (!force_layer_commands && !rendering::layout::is_visible_rect(layer_dirty_rect)) {
                skipped_layer_depth = 1U;
                continue;
            }

            append_opcode_index(output, index);
            push_layer_state(LayerCullState{.dirty_rect = layer_dirty_rect,
                                            .force_all = force_layer_commands});
            continue;
        }

        if (opcode.opcode == rendering::RenderCommandType::PopLayer) {
            append_opcode_index(output, index);
            pop_layer_state();
            continue;
        }

        if (is_draw_command(opcode.opcode) &&
            !rendering::layout::rects_intersect(opcode.bounds, current_dirty_rect)) {
            continue;
        }
        append_opcode_index(output, index);
    }
}

void collect_visible_opcode_indices_for_tiles(
    const rendering::RenderCommandList& commands,
    std::span<PreparedRenderTile> tiles) {
    const auto& opcodes = commands.opcodes();
    if (opcodes.empty() || tiles.empty()) {
        return;
    }

    const auto reserve_count =
        std::min<std::size_t>(opcodes.size(),
                              std::max<std::size_t>(32U, opcodes.size() / tiles.size()));
    for (auto& tile : tiles) {
        tile.command_indices.clear();
        tile.command_indices.reserve(reserve_count);
        tile.uses_shared_command_indices = false;
    }

    for (std::size_t opcode_index = 0U; opcode_index < opcodes.size(); ++opcode_index) {
        const auto& opcode = opcodes[opcode_index];
        if (!is_draw_command(opcode.opcode)) {
            for (auto& tile : tiles) {
                append_opcode_index(tile.command_indices, opcode_index);
            }
            continue;
        }

        for (auto& tile : tiles) {
            if (rendering::layout::rects_intersect(opcode.bounds, tile.cull_clip)) {
                append_opcode_index(tile.command_indices, opcode_index);
            }
        }
    }
}

[[nodiscard]] std::uint32_t
count_pass_visible_commands(const std::vector<rendering::RenderOpcodeRecord>& opcodes,
                            const rendering::RenderFramePass& pass,
                            rendering::layout::Rect dirty_bounds,
                            bool force_all_commands) noexcept {
    const auto first = std::min<std::size_t>(pass.first_command_index, opcodes.size());
    const auto last = std::min<std::size_t>(first + pass.command_count, opcodes.size());
    if (force_all_commands) {
        return static_cast<std::uint32_t>(
            std::min<std::size_t>(last - first, std::numeric_limits<std::uint32_t>::max()));
    }

    auto visible_count = 0U;
    for (auto index = first; index < last; ++index) {
        const auto& opcode = opcodes[index];
        if (!is_draw_command(opcode.opcode) ||
            rendering::layout::rects_intersect(opcode.bounds, dirty_bounds)) {
            ++visible_count;
        }
    }
    return visible_count;
}

[[nodiscard]] std::size_t estimate_vertex_budget_for_pass(rendering::RenderFramePassKind kind,
                                                          std::uint32_t command_count) noexcept {
    switch (kind) {
    case rendering::RenderFramePassKind::Geometry:
        return static_cast<std::size_t>(command_count) * 24U;
    case rendering::RenderFramePassKind::Text:
    case rendering::RenderFramePassKind::Image:
        return static_cast<std::size_t>(command_count) * 6U;
    case rendering::RenderFramePassKind::Effect:
        return static_cast<std::size_t>(command_count) * 60U;
    case rendering::RenderFramePassKind::State:
    case rendering::RenderFramePassKind::Composite:
    case rendering::RenderFramePassKind::Present:
    default:
        return 0U;
    }
}

[[nodiscard]] PreparedRenderPlan
prepare_render_plan(const rendering::RenderCommandList& commands,
                    std::span<const D3D11RenderDirtyClip> dirty_clips,
                    const rendering::RenderFrameGraph* frame_graph, float target_dip_width,
                    float target_dip_height, bool force_all_commands) {
    PreparedRenderPlan plan;
    const auto& opcodes = commands.opcodes();
    auto tile_rects = build_render_tiles(dirty_clips, target_dip_width, target_dip_height);
    if (opcodes.empty() || tile_rects.empty()) {
        return plan;
    }

    plan.tiles.resize(tile_rects.size());
    for (std::size_t index = 0U; index < tile_rects.size(); ++index) {
        plan.tiles[index].device_clip = tile_rects[index].device_clip;
        plan.tiles[index].cull_clip = tile_rects[index].cull_clip;
    }

    auto dirty_bounds = rendering::layout::Rect{};
    for (const auto tile : tile_rects) {
        dirty_bounds = rendering::layout::union_rects(dirty_bounds, tile.cull_clip);
    }

    if (frame_graph != nullptr && !frame_graph->empty() &&
        frame_graph->command_count == opcodes.size()) {
        plan.passes.resize(frame_graph->passes.size());
        for (std::size_t index = 0U; index < frame_graph->passes.size(); ++index) {
            const auto& pass = frame_graph->passes[index];
            plan.passes[index] =
                PreparedRenderPassMetrics{.kind = pass.kind,
                                          .first_command_index = pass.first_command_index,
                                          .command_count = pass.command_count};
        }
    }

    auto* pool = static_cast<RenderThreadPoolService*>(nullptr);
    if (should_try_parallel_render_plan(opcodes.size(), plan.tiles.size(), plan.passes.size())) {
        auto& shared_pool = shared_render_thread_pool();
        if (shared_pool.worker_count() > 1U) {
            pool = &shared_pool;
        }
    }

    if (force_all_commands) {
        collect_all_opcode_indices(opcodes, plan.shared_command_indices);
        for (auto& tile : plan.tiles) {
            tile.uses_shared_command_indices = true;
        }
    } else if (plan.tiles.size() == 1U) {
        collect_visible_opcode_indices(commands, plan.tiles.front().cull_clip,
                                       plan.shared_command_indices);
        plan.tiles.front().uses_shared_command_indices = true;
    } else if (!command_list_has_serial_barrier(commands)) {
        collect_visible_opcode_indices_for_tiles(commands, plan.tiles);
    } else if (pool != nullptr && plan.tiles.size() >= 4U) {
        pool->parallel_for(
            plan.tiles.size(), 1U, [&plan, &commands](std::size_t first, std::size_t last) {
                for (auto index = first; index < last; ++index) {
                    collect_visible_opcode_indices(commands, plan.tiles[index].cull_clip,
                                                   plan.tiles[index].command_indices);
                }
            });
        plan.prepared_in_parallel = true;
    } else {
        for (auto& tile : plan.tiles) {
            collect_visible_opcode_indices(commands, tile.cull_clip, tile.command_indices);
        }
    }

    if (!plan.passes.empty()) {
        const auto should_parallelize_passes =
            pool != nullptr && plan.passes.size() >= 4U && opcodes.size() >= 4096U;
        const auto fill_passes = [&plan, &opcodes, dirty_bounds,
                                  force_all_commands](std::size_t first, std::size_t last) {
            for (auto index = first; index < last; ++index) {
                const auto pass = rendering::RenderFramePass{
                    .kind = plan.passes[index].kind,
                    .first_command_index = plan.passes[index].first_command_index,
                    .command_count = plan.passes[index].command_count};
                plan.passes[index].visible_command_count =
                    count_pass_visible_commands(opcodes, pass, dirty_bounds, force_all_commands);
            }
        };
        if (should_parallelize_passes) {
            pool->parallel_for(plan.passes.size(), 1U, fill_passes);
            plan.prepared_in_parallel = true;
        } else {
            fill_passes(0U, plan.passes.size());
        }
    }

    for (const auto& tile : plan.tiles) {
        add_visible_command_count(plan.visible_command_count,
                                  tile.uses_shared_command_indices
                                      ? plan.shared_command_indices.size()
                                      : tile.command_indices.size());
    }
    return plan;
}

[[nodiscard]] D3D11_RECT empty_scissor() noexcept {
    return D3D11_RECT{0, 0, 0, 0};
}

[[nodiscard]] bool is_visible_scissor(const D3D11_RECT& rect) noexcept {
    return rect.right > rect.left && rect.bottom > rect.top;
}

[[nodiscard]] D3D11_RECT intersect_scissor(D3D11_RECT left, D3D11_RECT right) noexcept {
    const auto result =
        D3D11_RECT{std::max(left.left, right.left), std::max(left.top, right.top),
                   std::min(left.right, right.right), std::min(left.bottom, right.bottom)};
    return is_visible_scissor(result) ? result : empty_scissor();
}

[[nodiscard]] D3D11_RECT to_scissor(rendering::layout::Rect rect, float dpi,
                                    std::uint32_t target_width,
                                    std::uint32_t target_height) noexcept {
    const auto scale = std::max(dpi, 1.0F) / default_dpi;
    auto left = static_cast<LONG>(std::floor(rect.x * scale));
    auto top = static_cast<LONG>(std::floor(rect.y * scale));
    auto right = static_cast<LONG>(std::ceil((rect.x + rect.width) * scale));
    auto bottom = static_cast<LONG>(std::ceil((rect.y + rect.height) * scale));

    left = std::clamp<LONG>(left, 0, static_cast<LONG>(target_width));
    top = std::clamp<LONG>(top, 0, static_cast<LONG>(target_height));
    right = std::clamp<LONG>(right, left, static_cast<LONG>(target_width));
    bottom = std::clamp<LONG>(bottom, top, static_cast<LONG>(target_height));
    return D3D11_RECT{left, top, right, bottom};
}

[[nodiscard]] rendering::Color multiply_alpha(rendering::Color color, float opacity) noexcept {
    const auto alpha = std::clamp(static_cast<int>(std::round(color.alpha * opacity)), 0, 255);
    return rendering::Color::rgba(color.red, color.green, color.blue,
                                  static_cast<std::uint8_t>(alpha));
}

[[nodiscard]] std::array<float, 4U> premultiplied_color(rendering::Color color,
                                                        float opacity) noexcept {
    const auto draw_color = multiply_alpha(color, opacity);
    const auto alpha = static_cast<float>(draw_color.alpha) / 255.0F;
    return {static_cast<float>(draw_color.red) / 255.0F * alpha,
            static_cast<float>(draw_color.green) / 255.0F * alpha,
            static_cast<float>(draw_color.blue) / 255.0F * alpha, alpha};
}

[[nodiscard]] float physical_pixel_size(float dpi) noexcept {
    return default_dpi / std::max(dpi, 1.0F);
}

[[nodiscard]] std::uint32_t stroke_pixel_width(float width, float dpi) noexcept {
    const auto scale = std::max(dpi, 1.0F) / default_dpi;
    return std::max(1U, static_cast<std::uint32_t>(std::round(std::max(width, 0.0F) * scale)));
}

[[nodiscard]] float snap_stroke_center(float value, std::uint32_t pixel_width, float dpi) noexcept {
    const auto scale = dpi / default_dpi;
    const auto scaled = value * scale;
    if ((pixel_width % 2U) == 0U) {
        return std::round(scaled) / scale;
    }
    return (std::floor(scaled) + 0.5F) / scale;
}

[[nodiscard]] std::array<float, 4U> with_coverage(std::array<float, 4U> color,
                                                  float coverage) noexcept {
    const auto clipped = std::clamp(coverage, 0.0F, 1.0F);
    return {color[0] * clipped, color[1] * clipped, color[2] * clipped, color[3] * clipped};
}

template <typename Value> void hash_combine(std::size_t& seed, const Value& value) noexcept {
    seed ^= std::hash<Value>{}(value) + 0x9E3779B97F4A7C15ULL + (seed << 6U) + (seed >> 2U);
}

[[nodiscard]] std::size_t text_atlas_run_fingerprint(const rendering::TextLayout& layout) noexcept {
    auto seed = std::size_t{};
    hash_combine(seed, std::string_view{layout.style.font_family});
    hash_combine(seed, layout.style.font_size);
    hash_combine(seed, static_cast<std::uint16_t>(layout.style.font_weight));
    hash_combine(seed, static_cast<std::uint16_t>(layout.style.font_stretch));
    hash_combine(seed, static_cast<std::uint8_t>(layout.style.font_style));
    hash_combine(seed, layout.glyphs.size());
    for (const auto& glyph : layout.glyphs) {
        hash_combine(seed, std::string_view{glyph.font_family});
        hash_combine(seed, glyph.glyph_index);
        hash_combine(seed, glyph.is_right_to_left);
        hash_combine(seed, glyph.origin.x);
        hash_combine(seed, glyph.origin.y);
    }
    return seed;
}

[[nodiscard]] std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const auto required =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        required);
    return result;
}

[[nodiscard]] Microsoft::WRL::ComPtr<IDWriteFactory> create_dwrite_factory() {
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    const auto result =
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &factory);
    if (FAILED(result)) {
        return {};
    }
    return factory;
}

[[nodiscard]] IDWriteFactory* shared_dwrite_factory() noexcept {
    static auto factory = create_dwrite_factory();
    return factory.Get();
}

[[nodiscard]] DWRITE_FONT_STYLE to_dwrite_font_style(rendering::FontStyle style) noexcept {
    switch (style) {
    case rendering::FontStyle::Italic:
        return DWRITE_FONT_STYLE_ITALIC;
    case rendering::FontStyle::Oblique:
        return DWRITE_FONT_STYLE_OBLIQUE;
    case rendering::FontStyle::Normal:
    default:
        return DWRITE_FONT_STYLE_NORMAL;
    }
}

[[nodiscard]] Microsoft::WRL::ComPtr<IDWriteFontFace>
resolve_font_face(const rendering::TextStyle& style) noexcept {
    auto* factory = shared_dwrite_factory();
    if (factory == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
    if (FAILED(factory->GetSystemFontCollection(&collection)) || collection == nullptr) {
        return {};
    }

    auto family_name = utf8_to_wide(style.font_family);
    if (family_name.empty()) {
        family_name = L"Segoe UI";
    }

    UINT32 family_index = 0;
    BOOL exists = FALSE;
    auto result = collection->FindFamilyName(family_name.c_str(), &family_index, &exists);
    if (FAILED(result) || !exists) {
        result = collection->FindFamilyName(L"Segoe UI", &family_index, &exists);
    }
    if (FAILED(result) || !exists) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
    if (FAILED(collection->GetFontFamily(family_index, &family)) || family == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFont> font;
    if (FAILED(family->GetFirstMatchingFont(static_cast<DWRITE_FONT_WEIGHT>(style.font_weight),
                                            static_cast<DWRITE_FONT_STRETCH>(style.font_stretch),
                                            to_dwrite_font_style(style.font_style), &font)) ||
        font == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontFace> face;
    if (FAILED(font->CreateFontFace(&face))) {
        return {};
    }
    return face;
}

class GlyphGeometrySink final : public ID2D1SimplifiedGeometrySink {
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(ID2D1SimplifiedGeometrySink)) {
            *object = static_cast<ID2D1SimplifiedGeometrySink*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++ref_count_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --ref_count_;
        return count;
    }

    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE fill_mode) override {
        geometry_.fill_rule = fill_mode == D2D1_FILL_MODE_ALTERNATE
                                  ? rendering::GeometryFillRule::EvenOdd
                                  : rendering::GeometryFillRule::NonZero;
    }

    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}

    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start_point,
                                       D2D1_FIGURE_BEGIN figure_begin) override {
        current_ = rendering::GeometryFigure{
            .start = rendering::layout::Point{start_point.x, start_point.y},
            .begin = figure_begin == D2D1_FIGURE_BEGIN_FILLED
                         ? rendering::GeometryFigureBegin::Filled
                         : rendering::GeometryFigureBegin::Hollow};
        has_current_ = true;
    }

    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 points_count) override {
        if (!has_current_ || points == nullptr) {
            return;
        }
        for (UINT32 index = 0; index < points_count; ++index) {
            current_.segments.push_back(rendering::GeometrySegment{
                .type = rendering::GeometrySegmentType::Line,
                .point = rendering::layout::Point{points[index].x, points[index].y}});
        }
    }

    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* beziers,
                                      UINT32 beziers_count) override {
        if (!has_current_ || beziers == nullptr) {
            return;
        }
        for (UINT32 index = 0; index < beziers_count; ++index) {
            current_.segments.push_back(rendering::GeometrySegment{
                .type = rendering::GeometrySegmentType::CubicBezier,
                .point = rendering::layout::Point{beziers[index].point3.x, beziers[index].point3.y},
                .control_point1 =
                    rendering::layout::Point{beziers[index].point1.x, beziers[index].point1.y},
                .control_point2 =
                    rendering::layout::Point{beziers[index].point2.x, beziers[index].point2.y}});
        }
    }

    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END figure_end) override {
        if (!has_current_) {
            return;
        }
        current_.end = figure_end == D2D1_FIGURE_END_CLOSED ? rendering::GeometryFigureEnd::Closed
                                                            : rendering::GeometryFigureEnd::Open;
        geometry_.figures.push_back(std::move(current_));
        current_ = {};
        has_current_ = false;
    }

    HRESULT STDMETHODCALLTYPE Close() override {
        return S_OK;
    }

    [[nodiscard]] const rendering::Geometry& geometry() const noexcept {
        return geometry_;
    }

  private:
    ULONG ref_count_ = 1U;
    rendering::Geometry geometry_;
    rendering::GeometryFigure current_;
    bool has_current_ = false;
};

[[nodiscard]] float cross(rendering::layout::Point a, rendering::layout::Point b,
                          rendering::layout::Point c) noexcept {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

[[nodiscard]] float polygon_area(const std::vector<rendering::layout::Point>& points) noexcept {
    if (points.size() < 3U) {
        return 0.0F;
    }
    auto area = 0.0F;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& current = points[index];
        const auto& next = points[(index + 1U) % points.size()];
        area += current.x * next.y - next.x * current.y;
    }
    return area * 0.5F;
}

[[nodiscard]] bool point_in_triangle(rendering::layout::Point point, rendering::layout::Point a,
                                     rendering::layout::Point b,
                                     rendering::layout::Point c) noexcept {
    const auto ab = cross(a, b, point);
    const auto bc = cross(b, c, point);
    const auto ca = cross(c, a, point);
    return (ab >= 0.0F && bc >= 0.0F && ca >= 0.0F) || (ab <= 0.0F && bc <= 0.0F && ca <= 0.0F);
}

[[nodiscard]] std::vector<std::uint32_t>
triangulate_polygon(const std::vector<rendering::layout::Point>& points) {
    std::vector<std::uint32_t> triangles;
    if (points.size() < 3U) {
        return triangles;
    }

    std::vector<std::uint32_t> indices(points.size());
    for (std::uint32_t index = 0; index < indices.size(); ++index) {
        indices[index] = index;
    }
    if (polygon_area(points) < 0.0F) {
        std::reverse(indices.begin(), indices.end());
    }

    auto guard = points.size() * points.size();
    while (indices.size() > 3U && guard-- > 0U) {
        auto clipped = false;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const auto previous = indices[(i + indices.size() - 1U) % indices.size()];
            const auto current = indices[i];
            const auto next = indices[(i + 1U) % indices.size()];
            if (cross(points[previous], points[current], points[next]) <= 0.0F) {
                continue;
            }

            auto contains_point = false;
            for (const auto candidate : indices) {
                if (candidate == previous || candidate == current || candidate == next) {
                    continue;
                }
                if (point_in_triangle(points[candidate], points[previous], points[current],
                                      points[next])) {
                    contains_point = true;
                    break;
                }
            }
            if (contains_point) {
                continue;
            }

            triangles.push_back(previous);
            triangles.push_back(current);
            triangles.push_back(next);
            indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            break;
        }
    }

    if (indices.size() == 3U) {
        triangles.push_back(indices[0]);
        triangles.push_back(indices[1]);
        triangles.push_back(indices[2]);
    }
    return triangles;
}

[[nodiscard]] rendering::layout::Point lerp(rendering::layout::Point a, rendering::layout::Point b,
                                            float t) noexcept {
    return rendering::layout::Point{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

[[nodiscard]] rendering::layout::Point point_add(rendering::layout::Point left,
                                                 rendering::layout::Point right) noexcept {
    return rendering::layout::Point{left.x + right.x, left.y + right.y};
}

[[nodiscard]] rendering::layout::Point point_subtract(rendering::layout::Point left,
                                                      rendering::layout::Point right) noexcept {
    return rendering::layout::Point{left.x - right.x, left.y - right.y};
}

[[nodiscard]] rendering::layout::Point point_scale(rendering::layout::Point point,
                                                   float scale) noexcept {
    return rendering::layout::Point{point.x * scale, point.y * scale};
}

[[nodiscard]] float vector_cross(rendering::layout::Point left,
                                 rendering::layout::Point right) noexcept {
    return left.x * right.y - left.y * right.x;
}

[[nodiscard]] float vector_dot(rendering::layout::Point left,
                               rendering::layout::Point right) noexcept {
    return left.x * right.x + left.y * right.y;
}

[[nodiscard]] float vector_length(rendering::layout::Point vector) noexcept {
    return std::sqrt(vector.x * vector.x + vector.y * vector.y);
}

[[nodiscard]] float point_distance(rendering::layout::Point left,
                                   rendering::layout::Point right) noexcept {
    return vector_length(point_subtract(right, left));
}

[[nodiscard]] float point_line_distance(rendering::layout::Point point,
                                        rendering::layout::Point line_start,
                                        rendering::layout::Point line_end) noexcept {
    const auto line = point_subtract(line_end, line_start);
    const auto length = vector_length(line);
    if (length <= geometry_epsilon) {
        return point_distance(point, line_start);
    }
    return std::abs(vector_cross(point_subtract(point, line_start), line)) / length;
}

[[nodiscard]] bool nearly_same_point(rendering::layout::Point left,
                                     rendering::layout::Point right) noexcept {
    return point_distance(left, right) <= contour_cleanup_epsilon;
}

[[nodiscard]] bool is_redundant_collinear_point(rendering::layout::Point previous,
                                                rendering::layout::Point current,
                                                rendering::layout::Point next) noexcept {
    if (nearly_same_point(previous, current) || nearly_same_point(current, next)) {
        return true;
    }
    const auto before = point_subtract(current, previous);
    const auto after = point_subtract(next, current);
    if (vector_dot(before, after) < -contour_cleanup_epsilon) {
        return false;
    }
    return point_line_distance(current, previous, next) <= contour_cleanup_epsilon;
}

[[nodiscard]] std::vector<rendering::layout::Point>
clean_closed_contour(std::vector<rendering::layout::Point> points) {
    if (points.size() > 1U && nearly_same_point(points.front(), points.back())) {
        points.pop_back();
    }
    if (points.size() < 3U) {
        return {};
    }

    std::vector<rendering::layout::Point> compacted;
    compacted.reserve(points.size());
    for (const auto point : points) {
        if (!compacted.empty() && nearly_same_point(compacted.back(), point)) {
            continue;
        }
        compacted.push_back(point);
    }
    if (compacted.size() > 1U && nearly_same_point(compacted.front(), compacted.back())) {
        compacted.pop_back();
    }
    if (compacted.size() < 3U) {
        return {};
    }

    auto changed = true;
    while (changed && compacted.size() >= 3U) {
        changed = false;
        std::vector<rendering::layout::Point> simplified;
        simplified.reserve(compacted.size());
        for (std::size_t index = 0U; index < compacted.size(); ++index) {
            const auto previous = compacted[(index + compacted.size() - 1U) % compacted.size()];
            const auto current = compacted[index];
            const auto next = compacted[(index + 1U) % compacted.size()];
            if (is_redundant_collinear_point(previous, current, next)) {
                changed = true;
                continue;
            }
            simplified.push_back(current);
        }
        compacted = std::move(simplified);
    }

    return compacted.size() >= 3U ? compacted : std::vector<rendering::layout::Point>{};
}

[[nodiscard]] float degrees_to_radians(float degrees) noexcept {
    return degrees * pi / 180.0F;
}

[[nodiscard]] rendering::layout::Point normalize_vector(rendering::layout::Point vector) noexcept {
    const auto length = vector_length(vector);
    if (length <= geometry_epsilon) {
        return rendering::layout::Point{};
    }
    return point_scale(vector, 1.0F / length);
}

[[nodiscard]] rendering::layout::Point left_normal(rendering::layout::Point direction) noexcept {
    return rendering::layout::Point{-direction.y, direction.x};
}

[[nodiscard]] bool line_intersection(rendering::layout::Point origin,
                                     rendering::layout::Point direction,
                                     rendering::layout::Point other_origin,
                                     rendering::layout::Point other_direction,
                                     rendering::layout::Point& intersection) noexcept {
    const auto denominator = vector_cross(direction, other_direction);
    if (std::abs(denominator) <= geometry_epsilon) {
        return false;
    }
    const auto t =
        vector_cross(point_subtract(other_origin, origin), other_direction) / denominator;
    intersection = point_add(origin, point_scale(direction, t));
    return true;
}

[[nodiscard]] rendering::layout::Point
joined_offset_point(rendering::layout::Point point, rendering::layout::Point previous_direction,
                    rendering::layout::Point previous_normal,
                    rendering::layout::Point next_direction, rendering::layout::Point next_normal,
                    float distance, float limit) noexcept {
    const auto previous_origin = point_add(point, point_scale(previous_normal, distance));
    const auto next_origin = point_add(point, point_scale(next_normal, distance));
    auto intersection = rendering::layout::Point{};
    if (line_intersection(previous_origin, previous_direction, next_origin, next_direction,
                          intersection) &&
        point_distance(point, intersection) <= std::max(limit, distance)) {
        return intersection;
    }

    const auto blended_normal = point_add(previous_normal, next_normal);
    if (vector_length(blended_normal) > geometry_epsilon) {
        return point_add(point, point_scale(normalize_vector(blended_normal), distance));
    }
    return next_origin;
}

[[nodiscard]] bool transform_has_axis_aligned_basis(core::Transform2D transform) noexcept {
    const auto x_axis_aligned = std::abs(transform.m12) <= geometry_epsilon;
    const auto y_axis_aligned = std::abs(transform.m21) <= geometry_epsilon;
    const auto x_axis_vertical = std::abs(transform.m11) <= geometry_epsilon;
    const auto y_axis_horizontal = std::abs(transform.m22) <= geometry_epsilon;
    return (x_axis_aligned && y_axis_aligned) || (x_axis_vertical && y_axis_horizontal);
}

[[nodiscard]] std::uint32_t adaptive_curve_segments(float length, std::uint32_t minimum,
                                                    std::uint32_t maximum) noexcept {
    return std::clamp(static_cast<std::uint32_t>(std::ceil(std::max(length, 0.0F) / 0.75F)),
                      minimum, maximum);
}

[[nodiscard]] std::uint32_t adaptive_arc_segments(float radius_x, float radius_y,
                                                  float sweep_radians) noexcept {
    const auto radius = std::max(std::max(radius_x, radius_y), 0.0F);
    if (radius <= geometry_epsilon || std::abs(sweep_radians) <= geometry_epsilon) {
        return 1U;
    }
    const auto ratio = std::clamp(geometry_flattening_tolerance / radius, 0.0F, 1.0F);
    const auto max_angle_step = std::max(2.0F * std::acos(1.0F - ratio), pi / 96.0F);
    return std::clamp(
        static_cast<std::uint32_t>(std::ceil(std::abs(sweep_radians) / max_angle_step)), 8U, 256U);
}

[[nodiscard]] std::uint32_t adaptive_ellipse_segments(float radius_x, float radius_y) noexcept {
    const auto a = std::max(radius_x, 0.0F);
    const auto b = std::max(radius_y, 0.0F);
    const auto circumference = pi * (3.0F * (a + b) - std::sqrt((3.0F * a + b) * (a + 3.0F * b)));
    return adaptive_curve_segments(circumference, 64U, 192U);
}

struct ArcFlatteningParameters {
    rendering::layout::Point center{};
    float radius_x = 0.0F;
    float radius_y = 0.0F;
    float rotation = 0.0F;
    float start_angle = 0.0F;
    float sweep_angle = 0.0F;
};

[[nodiscard]] std::vector<rendering::layout::Point>
flatten_figure(const rendering::GeometryFigure& figure);

[[nodiscard]] std::optional<ArcFlatteningParameters>
resolve_arc(rendering::layout::Point start, const rendering::GeometrySegment& segment) noexcept {
    auto radius_x = std::abs(segment.radius.width);
    auto radius_y = std::abs(segment.radius.height);
    if (radius_x <= geometry_epsilon || radius_y <= geometry_epsilon ||
        point_distance(start, segment.point) <= geometry_epsilon) {
        return std::nullopt;
    }

    const auto rotation = degrees_to_radians(segment.rotation_angle);
    const auto cos_rotation = std::cos(rotation);
    const auto sin_rotation = std::sin(rotation);
    const auto half_delta_x = (start.x - segment.point.x) * 0.5F;
    const auto half_delta_y = (start.y - segment.point.y) * 0.5F;
    const auto transformed_start_x = cos_rotation * half_delta_x + sin_rotation * half_delta_y;
    const auto transformed_start_y = -sin_rotation * half_delta_x + cos_rotation * half_delta_y;

    auto radius_x_squared = radius_x * radius_x;
    auto radius_y_squared = radius_y * radius_y;
    const auto transformed_start_x_squared = transformed_start_x * transformed_start_x;
    const auto transformed_start_y_squared = transformed_start_y * transformed_start_y;
    const auto radius_scale = transformed_start_x_squared / radius_x_squared +
                              transformed_start_y_squared / radius_y_squared;
    if (radius_scale > 1.0F) {
        const auto scale = std::sqrt(radius_scale);
        radius_x *= scale;
        radius_y *= scale;
        radius_x_squared = radius_x * radius_x;
        radius_y_squared = radius_y * radius_y;
    }

    const auto denominator = radius_x_squared * transformed_start_y_squared +
                             radius_y_squared * transformed_start_x_squared;
    if (denominator <= geometry_epsilon) {
        return std::nullopt;
    }

    const auto numerator = radius_x_squared * radius_y_squared - denominator;
    auto factor = std::sqrt(std::max(0.0F, numerator / denominator));
    const auto large_arc = segment.arc_size == rendering::GeometryArcSize::Large;
    const auto clockwise =
        segment.sweep_direction == rendering::GeometryArcSweepDirection::Clockwise;
    if (large_arc == clockwise) {
        factor = -factor;
    }

    const auto center_x_prime = factor * radius_x * transformed_start_y / radius_y;
    const auto center_y_prime = -factor * radius_y * transformed_start_x / radius_x;
    const auto center =
        rendering::layout::Point{cos_rotation * center_x_prime - sin_rotation * center_y_prime +
                                     (start.x + segment.point.x) * 0.5F,
                                 sin_rotation * center_x_prime + cos_rotation * center_y_prime +
                                     (start.y + segment.point.y) * 0.5F};

    const auto start_angle = std::atan2((transformed_start_y - center_y_prime) / radius_y,
                                        (transformed_start_x - center_x_prime) / radius_x);
    const auto end_angle = std::atan2((-transformed_start_y - center_y_prime) / radius_y,
                                      (-transformed_start_x - center_x_prime) / radius_x);
    auto sweep_angle = end_angle - start_angle;
    if (clockwise && sweep_angle < 0.0F) {
        sweep_angle += pi * 2.0F;
    } else if (!clockwise && sweep_angle > 0.0F) {
        sweep_angle -= pi * 2.0F;
    }

    return ArcFlatteningParameters{.center = center,
                                   .radius_x = radius_x,
                                   .radius_y = radius_y,
                                   .rotation = rotation,
                                   .start_angle = start_angle,
                                   .sweep_angle = sweep_angle};
}

[[nodiscard]] rendering::layout::Point evaluate_arc_point(const ArcFlatteningParameters& arc,
                                                          float angle) noexcept {
    const auto cos_rotation = std::cos(arc.rotation);
    const auto sin_rotation = std::sin(arc.rotation);
    const auto cos_angle = std::cos(angle);
    const auto sin_angle = std::sin(angle);
    return rendering::layout::Point{arc.center.x + arc.radius_x * cos_angle * cos_rotation -
                                        arc.radius_y * sin_angle * sin_rotation,
                                    arc.center.y + arc.radius_x * cos_angle * sin_rotation +
                                        arc.radius_y * sin_angle * cos_rotation};
}

[[nodiscard]] std::vector<std::vector<rendering::layout::Point>>
flatten_filled_contours(const rendering::Geometry& geometry) {
    std::vector<std::vector<rendering::layout::Point>> contours;
    contours.reserve(geometry.figures.size());
    for (const auto& figure : geometry.figures) {
        if (figure.begin != rendering::GeometryFigureBegin::Filled) {
            continue;
        }
        auto points = clean_closed_contour(flatten_figure(figure));
        if (points.size() >= 3U) {
            contours.push_back(std::move(points));
        }
    }
    return contours;
}

[[nodiscard]] bool
point_inside_fill(const std::vector<std::vector<rendering::layout::Point>>& contours,
                  rendering::GeometryFillRule fill_rule, rendering::layout::Point point) noexcept {
    auto winding = 0;
    auto inside_even_odd = false;
    for (const auto& contour : contours) {
        for (std::size_t index = 0U; index < contour.size(); ++index) {
            const auto start = contour[index];
            const auto end = contour[(index + 1U) % contour.size()];
            const auto crosses =
                (start.y <= point.y && end.y > point.y) || (start.y > point.y && end.y <= point.y);
            if (!crosses) {
                continue;
            }
            const auto t = (point.y - start.y) / (end.y - start.y);
            const auto x = start.x + (end.x - start.x) * t;
            if (x <= point.x + geometry_epsilon) {
                continue;
            }
            if (fill_rule == rendering::GeometryFillRule::EvenOdd) {
                inside_even_odd = !inside_even_odd;
            } else {
                winding += end.y > start.y ? 1 : -1;
            }
        }
    }
    return fill_rule == rendering::GeometryFillRule::EvenOdd ? inside_even_odd : winding != 0;
}

void append_quadratic(std::vector<rendering::layout::Point>& points, rendering::layout::Point start,
                      rendering::layout::Point control, rendering::layout::Point end) {
    const auto append_recursive = [&](const auto& self, rendering::layout::Point current_start,
                                      rendering::layout::Point current_control,
                                      rendering::layout::Point current_end,
                                      std::uint32_t depth) -> void {
        if (depth >= max_curve_flattening_depth ||
            point_line_distance(current_control, current_start, current_end) <=
                geometry_flattening_tolerance) {
            points.push_back(current_end);
            return;
        }

        const auto start_control = lerp(current_start, current_control, 0.5F);
        const auto control_end = lerp(current_control, current_end, 0.5F);
        const auto middle = lerp(start_control, control_end, 0.5F);
        self(self, current_start, start_control, middle, depth + 1U);
        self(self, middle, control_end, current_end, depth + 1U);
    };
    append_recursive(append_recursive, start, control, end, 0U);
}

void append_cubic(std::vector<rendering::layout::Point>& points, rendering::layout::Point start,
                  rendering::layout::Point control1, rendering::layout::Point control2,
                  rendering::layout::Point end) {
    const auto append_recursive =
        [&](const auto& self, rendering::layout::Point current_start,
            rendering::layout::Point current_control1, rendering::layout::Point current_control2,
            rendering::layout::Point current_end, std::uint32_t depth) -> void {
        const auto flatness =
            std::max(point_line_distance(current_control1, current_start, current_end),
                     point_line_distance(current_control2, current_start, current_end));
        if (depth >= max_curve_flattening_depth || flatness <= geometry_flattening_tolerance) {
            points.push_back(current_end);
            return;
        }

        const auto start_control = lerp(current_start, current_control1, 0.5F);
        const auto middle_control = lerp(current_control1, current_control2, 0.5F);
        const auto control_end = lerp(current_control2, current_end, 0.5F);
        const auto left_control = lerp(start_control, middle_control, 0.5F);
        const auto right_control = lerp(middle_control, control_end, 0.5F);
        const auto middle = lerp(left_control, right_control, 0.5F);
        self(self, current_start, start_control, left_control, middle, depth + 1U);
        self(self, middle, right_control, control_end, current_end, depth + 1U);
    };
    append_recursive(append_recursive, start, control1, control2, end, 0U);
}

void append_arc(std::vector<rendering::layout::Point>& points, rendering::layout::Point start,
                const rendering::GeometrySegment& segment) {
    const auto arc = resolve_arc(start, segment);
    if (!arc.has_value()) {
        points.push_back(segment.point);
        return;
    }
    const auto segments = adaptive_arc_segments(arc->radius_x, arc->radius_y, arc->sweep_angle);
    for (auto step = 1U; step <= segments; ++step) {
        const auto t = static_cast<float>(step) / static_cast<float>(segments);
        const auto angle = arc->start_angle + arc->sweep_angle * t;
        points.push_back(evaluate_arc_point(*arc, angle));
    }
}

[[nodiscard]] std::vector<rendering::layout::Point>
flatten_figure(const rendering::GeometryFigure& figure) {
    std::vector<rendering::layout::Point> points;
    points.push_back(figure.start);
    auto current = figure.start;
    for (const auto& segment : figure.segments) {
        switch (segment.type) {
        case rendering::GeometrySegmentType::Line:
            points.push_back(segment.point);
            break;
        case rendering::GeometrySegmentType::QuadraticBezier:
            append_quadratic(points, current, segment.control_point1, segment.point);
            break;
        case rendering::GeometrySegmentType::CubicBezier:
            append_cubic(points, current, segment.control_point1, segment.control_point2,
                         segment.point);
            break;
        case rendering::GeometrySegmentType::Arc:
            append_arc(points, current, segment);
            break;
        }
        current = segment.point;
    }
    if (figure.end == rendering::GeometryFigureEnd::Closed && !points.empty() &&
        (points.front().x != points.back().x || points.front().y != points.back().y)) {
        points.push_back(points.front());
    }
    return points;
}

[[nodiscard]] std::vector<std::vector<rendering::layout::Point>>
flatten_geometry(const rendering::Geometry& geometry) {
    std::vector<std::vector<rendering::layout::Point>> figures;
    figures.reserve(geometry.figures.size());
    for (const auto& figure : geometry.figures) {
        figures.push_back(flatten_figure(figure));
    }
    return figures;
}

[[nodiscard]] rendering::Geometry offset_geometry(rendering::Geometry geometry,
                                                  rendering::layout::Point offset) {
    for (auto& figure : geometry.figures) {
        figure.start.x += offset.x;
        figure.start.y += offset.y;
        for (auto& segment : figure.segments) {
            segment.point.x += offset.x;
            segment.point.y += offset.y;
            segment.control_point1.x += offset.x;
            segment.control_point1.y += offset.y;
            segment.control_point2.x += offset.x;
            segment.control_point2.y += offset.y;
        }
    }
    return geometry;
}

[[nodiscard]] rendering::Geometry
glyph_outline_geometry(IDWriteFontFace& face, const rendering::TextGlyph& glyph, float font_size) {
    GlyphGeometrySink sink;
    const auto glyph_index = static_cast<UINT16>(glyph.glyph_index);
    const auto advance = glyph.advance;
    if (FAILED(face.GetGlyphRunOutline(font_size, &glyph_index, &advance, nullptr, 1, FALSE,
                                       glyph.is_right_to_left ? TRUE : FALSE, &sink))) {
        return {};
    }
    return sink.geometry();
}

[[nodiscard]] std::vector<rendering::layout::Point>
rounded_rect_outline_with_segments(rendering::layout::Rect rect, rendering::CornerRadius radius,
                                   std::uint32_t segments) {
    const auto max_x = std::max(rect.width * 0.5F, 0.0F);
    const auto max_y = std::max(rect.height * 0.5F, 0.0F);
    const auto rx = std::clamp(radius.x, 0.0F, max_x);
    const auto ry = std::clamp(radius.y, 0.0F, max_y);
    segments = std::max(segments, 1U);

    std::vector<rendering::layout::Point> points;
    points.reserve((segments + 1U) * 4U);
    const std::array<rendering::layout::Point, 4U> centers{
        rendering::layout::Point{rect.x + rect.width - rx, rect.y + ry},
        rendering::layout::Point{rect.x + rect.width - rx, rect.y + rect.height - ry},
        rendering::layout::Point{rect.x + rx, rect.y + rect.height - ry},
        rendering::layout::Point{rect.x + rx, rect.y + ry}};
    const std::array<float, 4U> starts{-pi * 0.5F, 0.0F, pi * 0.5F, pi};
    for (std::size_t corner = 0; corner < centers.size(); ++corner) {
        for (auto step = 0U; step <= segments; ++step) {
            const auto angle = starts[corner] + (static_cast<float>(step) / segments) * pi * 0.5F;
            points.push_back(rendering::layout::Point{centers[corner].x + std::cos(angle) * rx,
                                                      centers[corner].y + std::sin(angle) * ry});
        }
    }
    return points;
}

[[nodiscard]] rendering::layout::Rect inset_rect(rendering::layout::Rect rect,
                                                 float inset) noexcept {
    const auto amount = std::max(inset, 0.0F);
    const auto clamped_x = std::min(amount, std::max(rect.width * 0.5F, 0.0F));
    const auto clamped_y = std::min(amount, std::max(rect.height * 0.5F, 0.0F));
    return rendering::layout::Rect{rect.x + clamped_x, rect.y + clamped_y,
                                   std::max(0.0F, rect.width - clamped_x * 2.0F),
                                   std::max(0.0F, rect.height - clamped_y * 2.0F)};
}

[[nodiscard]] rendering::layout::Rect outset_rect(rendering::layout::Rect rect,
                                                  float outset) noexcept {
    const auto amount = std::max(outset, 0.0F);
    return rendering::layout::Rect{rect.x - amount, rect.y - amount, rect.width + amount * 2.0F,
                                   rect.height + amount * 2.0F};
}

[[nodiscard]] rendering::CornerRadius clamp_corner_radius(rendering::layout::Rect rect,
                                                          rendering::CornerRadius radius) noexcept {
    return rendering::CornerRadius{std::clamp(radius.x, 0.0F, std::max(rect.width * 0.5F, 0.0F)),
                                   std::clamp(radius.y, 0.0F, std::max(rect.height * 0.5F, 0.0F))};
}

[[nodiscard]] rendering::CornerRadius inset_corner_radius(rendering::CornerRadius radius,
                                                          float inset,
                                                          rendering::layout::Rect rect) noexcept {
    const auto amount = std::max(inset, 0.0F);
    return clamp_corner_radius(rect, rendering::CornerRadius{std::max(0.0F, radius.x - amount),
                                                             std::max(0.0F, radius.y - amount)});
}

[[nodiscard]] std::vector<rendering::layout::Point>
rounded_rect_outline(rendering::layout::Rect rect, rendering::CornerRadius radius) {
    const auto clamped_radius = clamp_corner_radius(rect, radius);
    if (clamped_radius.x <= geometry_epsilon || clamped_radius.y <= geometry_epsilon) {
        return {rendering::layout::Point{rect.x, rect.y},
                rendering::layout::Point{rect.x + rect.width, rect.y},
                rendering::layout::Point{rect.x + rect.width, rect.y + rect.height},
                rendering::layout::Point{rect.x, rect.y + rect.height}};
    }
    return rounded_rect_outline_with_segments(
        rect, clamped_radius, adaptive_arc_segments(clamped_radius.x, clamped_radius.y, pi * 0.5F));
}

[[nodiscard]] rendering::layout::Rect geometry_bounds(const rendering::Geometry& geometry) {
    auto left = std::numeric_limits<float>::max();
    auto top = std::numeric_limits<float>::max();
    auto right = std::numeric_limits<float>::lowest();
    auto bottom = std::numeric_limits<float>::lowest();
    auto has_point = false;
    for (const auto& figure : flatten_geometry(geometry)) {
        for (const auto point : figure) {
            left = std::min(left, point.x);
            top = std::min(top, point.y);
            right = std::max(right, point.x);
            bottom = std::max(bottom, point.y);
            has_point = true;
        }
    }
    return has_point ? rendering::layout::Rect{left, top, right - left, bottom - top}
                     : rendering::layout::Rect{};
}

struct FillEdge {
    rendering::layout::Point start{};
    rendering::layout::Point end{};
    float min_y = 0.0F;
    float max_y = 0.0F;
    int winding = 0;
};

struct ActiveFillEdge {
    const FillEdge* edge = nullptr;
    float x = 0.0F;
};

[[nodiscard]] float x_at_y(const FillEdge& edge, float y) noexcept {
    const auto dy = edge.end.y - edge.start.y;
    if (std::abs(dy) <= geometry_epsilon) {
        return edge.start.x;
    }
    const auto t = (y - edge.start.y) / dy;
    return edge.start.x + (edge.end.x - edge.start.x) * t;
}

void add_sorted_y(std::vector<float>& values, float y) {
    if (!std::isfinite(y)) {
        return;
    }
    values.push_back(y);
}

[[nodiscard]] bool edge_intersection_y(const FillEdge& first, const FillEdge& second,
                                       float& y) noexcept {
    const auto x1 = first.start.x;
    const auto y1 = first.start.y;
    const auto x2 = first.end.x;
    const auto y2 = first.end.y;
    const auto x3 = second.start.x;
    const auto y3 = second.start.y;
    const auto x4 = second.end.x;
    const auto y4 = second.end.y;
    const auto denominator = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(denominator) <= geometry_epsilon) {
        return false;
    }

    const auto determinant1 = x1 * y2 - y1 * x2;
    const auto determinant2 = x3 * y4 - y3 * x4;
    const auto intersection_y = (determinant1 * (y3 - y4) - (y1 - y2) * determinant2) / denominator;
    const auto first_min = std::max(std::min(y1, y2), std::min(y3, y4));
    const auto first_max = std::min(std::max(y1, y2), std::max(y3, y4));
    if (intersection_y <= first_min + geometry_epsilon ||
        intersection_y >= first_max - geometry_epsilon) {
        return false;
    }
    const auto intersection_x = x_at_y(first, intersection_y);
    const auto second_x = x_at_y(second, intersection_y);
    if (std::abs(intersection_x - second_x) > 0.01F) {
        return false;
    }
    y = intersection_y;
    return true;
}

void append_trapezoid(std::vector<rendering::layout::Point>& vertices, const FillEdge& left,
                      const FillEdge& right, float top, float bottom) {
    if (bottom - top <= geometry_epsilon) {
        return;
    }
    const auto top_left = rendering::layout::Point{x_at_y(left, top), top};
    const auto bottom_left = rendering::layout::Point{x_at_y(left, bottom), bottom};
    const auto top_right = rendering::layout::Point{x_at_y(right, top), top};
    const auto bottom_right = rendering::layout::Point{x_at_y(right, bottom), bottom};
    if (std::abs(top_right.x - top_left.x) <= geometry_epsilon &&
        std::abs(bottom_right.x - bottom_left.x) <= geometry_epsilon) {
        return;
    }

    vertices.push_back(top_left);
    vertices.push_back(top_right);
    vertices.push_back(bottom_right);
    vertices.push_back(top_left);
    vertices.push_back(bottom_right);
    vertices.push_back(bottom_left);
}

[[nodiscard]] std::vector<rendering::layout::Point>
tessellate_geometry_fill(const std::vector<std::vector<rendering::layout::Point>>& contours,
                         rendering::GeometryFillRule fill_rule) {
    std::vector<FillEdge> edges;
    std::vector<float> y_values;
    for (const auto& points : contours) {
        if (points.size() < 3U) {
            continue;
        }
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto start = points[index];
            const auto end = points[(index + 1U) % points.size()];
            if (std::abs(start.y - end.y) <= geometry_epsilon) {
                continue;
            }
            edges.push_back(FillEdge{.start = start,
                                     .end = end,
                                     .min_y = std::min(start.y, end.y),
                                     .max_y = std::max(start.y, end.y),
                                     .winding = start.y < end.y ? 1 : -1});
            add_sorted_y(y_values, start.y);
            add_sorted_y(y_values, end.y);
        }
    }

    if (edges.empty() || y_values.size() < 2U) {
        return {};
    }

    for (std::size_t first = 0; first < edges.size(); ++first) {
        for (std::size_t second = first + 1U; second < edges.size(); ++second) {
            auto y = 0.0F;
            if (edge_intersection_y(edges[first], edges[second], y)) {
                add_sorted_y(y_values, y);
            }
        }
    }

    std::sort(y_values.begin(), y_values.end());
    y_values.erase(std::unique(y_values.begin(), y_values.end(),
                               [](float left, float right) {
                                   return std::abs(left - right) <= geometry_epsilon;
                               }),
                   y_values.end());

    std::vector<rendering::layout::Point> vertices;
    for (std::size_t band = 1U; band < y_values.size(); ++band) {
        const auto top = y_values[band - 1U];
        const auto bottom = y_values[band];
        if (bottom - top <= geometry_epsilon) {
            continue;
        }
        const auto mid_y = (top + bottom) * 0.5F;
        std::vector<ActiveFillEdge> active_edges;
        for (const auto& edge : edges) {
            if (mid_y > edge.min_y + geometry_epsilon && mid_y < edge.max_y - geometry_epsilon) {
                active_edges.push_back(ActiveFillEdge{.edge = &edge, .x = x_at_y(edge, mid_y)});
            }
        }
        if (active_edges.size() < 2U) {
            continue;
        }
        std::sort(active_edges.begin(), active_edges.end(),
                  [](const auto& left, const auto& right) { return left.x < right.x; });

        auto winding = 0;
        const FillEdge* span_start = nullptr;
        for (const auto& active : active_edges) {
            const auto filled_before = fill_rule == rendering::GeometryFillRule::EvenOdd
                                           ? (winding % 2) != 0
                                           : winding != 0;
            winding += fill_rule == rendering::GeometryFillRule::EvenOdd ? 1 : active.edge->winding;
            const auto filled_after = fill_rule == rendering::GeometryFillRule::EvenOdd
                                          ? (winding % 2) != 0
                                          : winding != 0;
            if (!filled_before && filled_after) {
                span_start = active.edge;
            } else if (filled_before && !filled_after && span_start != nullptr) {
                append_trapezoid(vertices, *span_start, *active.edge, top, bottom);
                span_start = nullptr;
            }
        }
    }
    return vertices;
}

} // namespace

struct D3D11DisplayListRenderer::RenderPlanCacheState {
    struct CachedFrameGraph {
        std::uint64_t fingerprint = 0U;
        std::size_t command_count = 0U;
        rendering::RenderFrameGraph frame_graph{};
        std::uint64_t last_used_frame = 0U;
    };

    struct CachedVisibilityTile {
        std::uint64_t key = 0U;
        std::vector<std::uint32_t> command_indices;
        std::uint64_t last_used_frame = 0U;
    };

    struct CachedPassMetrics {
        std::uint64_t key = 0U;
        std::vector<PreparedRenderPassMetrics> passes;
        std::uint64_t last_used_frame = 0U;
    };

    struct CachedRenderPlan {
        std::uint64_t key = 0U;
        PreparedRenderPlan plan{};
        std::uint64_t last_used_frame = 0U;
    };

    [[nodiscard]] const rendering::RenderFrameGraph*
    frame_graph_for(const rendering::RenderCommandList& commands,
                    const rendering::RenderFrameGraph* preferred, std::uint64_t frame_sequence) {
        const auto command_count = commands.command_count();
        if (preferred != nullptr && preferred->command_count == command_count &&
            preferred->fingerprint == commands.fingerprint()) {
            return preferred;
        }

        const auto fingerprint = commands.fingerprint();
        if (fingerprint == 0U || command_count == 0U) {
            return nullptr;
        }

        for (auto& cached : frame_graphs) {
            if (cached.fingerprint == fingerprint && cached.command_count == command_count) {
                cached.last_used_frame = frame_sequence;
                return &cached.frame_graph;
            }
        }

        auto* slot = static_cast<CachedFrameGraph*>(nullptr);
        if (frame_graphs.size() >= max_frame_graphs) {
            const auto iterator =
                std::min_element(frame_graphs.begin(), frame_graphs.end(),
                                 [](const CachedFrameGraph& left, const CachedFrameGraph& right) {
                                     return left.last_used_frame < right.last_used_frame;
                                 });
            if (iterator != frame_graphs.end()) {
                slot = &*iterator;
            }
        } else {
            frame_graphs.push_back(CachedFrameGraph{});
            slot = &frame_graphs.back();
        }

        auto& cached = slot != nullptr ? *slot : frame_graphs.back();
        cached = CachedFrameGraph{};
        cached.fingerprint = fingerprint;
        cached.command_count = command_count;
        cached.frame_graph = rendering::build_render_frame_graph(commands);
        cached.last_used_frame = frame_sequence;
        return &cached.frame_graph;
    }

    [[nodiscard]] const PreparedRenderPlan&
    render_plan_for(const rendering::RenderCommandList& commands,
                    std::span<const D3D11RenderDirtyClip> dirty_clips,
                    const rendering::RenderFrameGraph* frame_graph, float target_dip_width,
                    float target_dip_height, bool force_all_commands,
                    std::uint64_t frame_sequence) {
        const auto key = render_plan_cache_key(commands, dirty_clips, frame_graph, target_dip_width,
                                               target_dip_height, force_all_commands);
        for (auto& cached : render_plans) {
            if (cached.key == key) {
                cached.last_used_frame = frame_sequence;
                return cached.plan;
            }
        }

        auto* slot = static_cast<CachedRenderPlan*>(nullptr);
        if (render_plans.size() >= max_render_plans) {
            const auto iterator =
                std::min_element(render_plans.begin(), render_plans.end(),
                                 [](const CachedRenderPlan& left, const CachedRenderPlan& right) {
                                     return left.last_used_frame < right.last_used_frame;
                                 });
            if (iterator != render_plans.end()) {
                slot = &*iterator;
            }
        } else {
            render_plans.push_back(CachedRenderPlan{});
            slot = &render_plans.back();
        }

        auto& cached = slot != nullptr ? *slot : render_plans.back();
        cached = CachedRenderPlan{.key = key, .last_used_frame = frame_sequence};
        auto& plan = cached.plan;
        const auto& opcodes = commands.opcodes();
        auto tile_rects = build_render_tiles(dirty_clips, target_dip_width, target_dip_height);
        if (opcodes.empty() || tile_rects.empty()) {
            return cached.plan;
        }

        plan.tiles.resize(tile_rects.size());
        if (force_all_commands) {
            collect_all_opcode_indices(opcodes, plan.shared_command_indices);
        }
        auto dirty_bounds = rendering::layout::Rect{};
        for (std::size_t index = 0U; index < tile_rects.size(); ++index) {
            auto& tile = plan.tiles[index];
            tile.device_clip = tile_rects[index].device_clip;
            tile.cull_clip = tile_rects[index].cull_clip;
            dirty_bounds = rendering::layout::union_rects(dirty_bounds, tile.cull_clip);
        }

        if (force_all_commands) {
            for (auto& tile : plan.tiles) {
                tile.uses_shared_command_indices = true;
            }
            add_visible_command_count(plan.visible_command_count,
                                      plan.shared_command_indices.size() * plan.tiles.size());
        } else if (plan.tiles.size() == 1U) {
            plan.shared_command_indices =
                visibility_indices_for(commands, plan.tiles.front().cull_clip, false,
                                       frame_sequence);
            plan.tiles.front().uses_shared_command_indices = true;
            add_visible_command_count(plan.visible_command_count,
                                      plan.shared_command_indices.size());
        } else if (!command_list_has_serial_barrier(commands)) {
            collect_visible_opcode_indices_for_tiles(commands, plan.tiles);
            for (const auto& tile : plan.tiles) {
                add_visible_command_count(plan.visible_command_count,
                                          tile.command_indices.size());
            }
        } else {
            for (auto& tile : plan.tiles) {
                tile.command_indices =
                    visibility_indices_for(commands, tile.cull_clip, false, frame_sequence);
                add_visible_command_count(plan.visible_command_count,
                                          tile.command_indices.size());
            }
        }

        if (frame_graph != nullptr && !frame_graph->empty() &&
            frame_graph->command_count == opcodes.size()) {
            plan.passes = pass_metrics_for(commands, *frame_graph, dirty_bounds, force_all_commands,
                                           frame_sequence);
        }

        return cached.plan;
    }

    [[nodiscard]] const std::vector<std::uint32_t>&
    visibility_indices_for(const rendering::RenderCommandList& commands,
                           rendering::layout::Rect cull_clip, bool force_all_commands,
                           std::uint64_t frame_sequence) {
        const auto key = visibility_tile_cache_key(commands, cull_clip, force_all_commands);
        for (auto& cached : visibility_tiles) {
            if (cached.key == key) {
                cached.last_used_frame = frame_sequence;
                return cached.command_indices;
            }
        }

        auto* slot = static_cast<CachedVisibilityTile*>(nullptr);
        if (visibility_tiles.size() >= max_visibility_tiles) {
            const auto iterator = std::min_element(
                visibility_tiles.begin(), visibility_tiles.end(),
                [](const CachedVisibilityTile& left, const CachedVisibilityTile& right) {
                    return left.last_used_frame < right.last_used_frame;
                });
            if (iterator != visibility_tiles.end()) {
                slot = &*iterator;
            }
        } else {
            visibility_tiles.push_back(CachedVisibilityTile{});
            slot = &visibility_tiles.back();
        }

        auto& cached = slot != nullptr ? *slot : visibility_tiles.back();
        cached = CachedVisibilityTile{};
        cached.key = key;
        cached.last_used_frame = frame_sequence;
        if (force_all_commands) {
            collect_all_opcode_indices(commands.opcodes(), cached.command_indices);
        } else {
            collect_visible_opcode_indices(commands, quantize_rect_outward(cull_clip).rect,
                                           cached.command_indices);
        }
        return cached.command_indices;
    }

    [[nodiscard]] std::vector<PreparedRenderPassMetrics>
    pass_metrics_for(const rendering::RenderCommandList& commands,
                     const rendering::RenderFrameGraph& frame_graph,
                     rendering::layout::Rect cull_bounds, bool force_all_commands,
                     std::uint64_t frame_sequence) {
        const auto key =
            pass_metrics_cache_key(commands, frame_graph, cull_bounds, force_all_commands);
        for (auto& cached : pass_metrics) {
            if (cached.key == key) {
                cached.last_used_frame = frame_sequence;
                return cached.passes;
            }
        }

        auto* slot = static_cast<CachedPassMetrics*>(nullptr);
        if (pass_metrics.size() >= max_pass_metrics) {
            const auto iterator =
                std::min_element(pass_metrics.begin(), pass_metrics.end(),
                                 [](const CachedPassMetrics& left, const CachedPassMetrics& right) {
                                     return left.last_used_frame < right.last_used_frame;
                                 });
            if (iterator != pass_metrics.end()) {
                slot = &*iterator;
            }
        } else {
            pass_metrics.push_back(CachedPassMetrics{});
            slot = &pass_metrics.back();
        }

        auto& cached = slot != nullptr ? *slot : pass_metrics.back();
        cached = CachedPassMetrics{};
        cached.key = key;
        cached.last_used_frame = frame_sequence;
        cached.passes.resize(frame_graph.passes.size());
        const auto query_bounds =
            force_all_commands ? cull_bounds : quantize_rect_outward(cull_bounds).rect;
        const auto& opcodes = commands.opcodes();
        for (std::size_t index = 0U; index < frame_graph.passes.size(); ++index) {
            const auto& pass = frame_graph.passes[index];
            cached.passes[index] =
                PreparedRenderPassMetrics{.kind = pass.kind,
                                          .first_command_index = pass.first_command_index,
                                          .command_count = pass.command_count,
                                          .visible_command_count = count_pass_visible_commands(
                                              opcodes, pass, query_bounds, force_all_commands)};
        }
        return cached.passes;
    }

    void prune_unused(std::uint64_t frame_sequence, std::uint64_t max_idle_frames) {
        const auto prune_entries =
            [frame_sequence, max_idle_frames](auto& entries) {
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                                             [frame_sequence, max_idle_frames](const auto& cached) {
                                                 return frame_sequence > cached.last_used_frame &&
                                                        frame_sequence - cached.last_used_frame >
                                                            max_idle_frames;
                                             }),
                              entries.end());
            };

        prune_entries(frame_graphs);
        prune_entries(visibility_tiles);
        prune_entries(pass_metrics);
        prune_entries(render_plans);
    }

    static constexpr auto max_frame_graphs = 32U;
    static constexpr auto max_visibility_tiles = 96U;
    static constexpr auto max_pass_metrics = 32U;
    static constexpr auto max_render_plans = 32U;
    std::vector<CachedFrameGraph> frame_graphs;
    std::vector<CachedVisibilityTile> visibility_tiles;
    std::vector<CachedPassMetrics> pass_metrics;
    std::vector<CachedRenderPlan> render_plans;
};

D3D11DisplayListRenderer::D3D11DisplayListRenderer(ID3D11Device& device)
    : D3D11DisplayListRenderer(device, RendererInstanceKind::Primary) {}

D3D11DisplayListRenderer::D3D11DisplayListRenderer(ID3D11Device& device,
                                                   RendererInstanceKind instance_kind)
    : device_(&device),
      render_plan_cache_(std::make_unique<RenderPlanCacheState>()),
      resource_updates_allowed_(instance_kind == RendererInstanceKind::Primary) {
    create_pipeline(device);
    throw_if_failed(device.CreateDeferredContext(0U, &primary_deferred_context_),
                    "failed to create display-list deferred context");
}

D3D11DisplayListRenderer::~D3D11DisplayListRenderer() = default;

void D3D11DisplayListRenderer::set_parallel_recording_enabled(bool enabled) noexcept {
    parallel_recording_enabled_ = enabled;
}

bool D3D11DisplayListRenderer::parallel_recording_enabled() const noexcept {
    return parallel_recording_enabled_;
}

D3D11DisplayListRenderer::RenderTimingMetrics
D3D11DisplayListRenderer::last_timing_metrics() const noexcept {
    return last_timing_metrics_;
}

void D3D11DisplayListRenderer::prune_frame_caches_if_needed() {
    if (frame_sequence_ < next_cache_prune_frame_) {
        return;
    }

    next_cache_prune_frame_ = frame_sequence_ + 32U;

    constexpr auto transformed_geometry_max_idle_frames = 120U;
    transformed_geometry_fill_cache_.erase(
        std::remove_if(transformed_geometry_fill_cache_.begin(),
                       transformed_geometry_fill_cache_.end(),
                       [this](const TransformedGeometryFill& cached) {
                           return frame_sequence_ > cached.last_used_frame &&
                                  frame_sequence_ - cached.last_used_frame >
                                      transformed_geometry_max_idle_frames;
                       }),
        transformed_geometry_fill_cache_.end());

    constexpr auto text_glyph_run_max_idle_frames = 120U;
    text_glyph_run_cache_.erase(
        std::remove_if(text_glyph_run_cache_.begin(), text_glyph_run_cache_.end(),
                       [this](const CachedTextGlyphRun& cached) {
                           const auto atlas_generation_changed =
                               cached.generation != glyph_atlas_generation_ &&
                               frame_sequence_ > cached.last_used_frame;
                           const auto expired_by_idle_time =
                               frame_sequence_ > cached.last_used_frame &&
                               frame_sequence_ - cached.last_used_frame >
                                   text_glyph_run_max_idle_frames;
                           return atlas_generation_changed || expired_by_idle_time;
                       }),
        text_glyph_run_cache_.end());

    constexpr auto render_plan_cache_max_idle_frames = 120U;
    if (render_plan_cache_ != nullptr) {
        render_plan_cache_->prune_unused(frame_sequence_, render_plan_cache_max_idle_frames);
    }
}

std::size_t
D3D11DisplayListRenderer::GlyphAtlasKeyHash::operator()(const GlyphAtlasKey& key) const noexcept {
    auto seed = std::size_t{};
    hash_combine(seed, std::string_view{key.font_family});
    hash_combine(seed, key.glyph_index);
    hash_combine(seed, key.font_size_key);
    hash_combine(seed, key.font_weight);
    hash_combine(seed, key.font_stretch);
    hash_combine(seed, key.font_style);
    hash_combine(seed, key.is_right_to_left);
    return seed;
}

std::size_t
D3D11DisplayListRenderer::TextFaceKeyHash::operator()(const TextFaceKey& key) const noexcept {
    auto seed = std::size_t{};
    hash_combine(seed, std::string_view{key.font_family});
    hash_combine(seed, key.font_weight);
    hash_combine(seed, key.font_stretch);
    hash_combine(seed, key.font_style);
    return seed;
}

Microsoft::WRL::ComPtr<IDWriteFontFace>
D3D11DisplayListRenderer::cached_font_face(const rendering::TextStyle& style) {
    auto family = style.font_family.empty() ? std::string{"Segoe UI"} : style.font_family;
    auto key = TextFaceKey{.font_family = std::move(family),
                           .font_weight = static_cast<std::uint16_t>(style.font_weight),
                           .font_stretch = static_cast<std::uint16_t>(style.font_stretch),
                           .font_style = static_cast<std::uint8_t>(style.font_style)};
    if (const auto iterator = text_face_cache_.find(key); iterator != text_face_cache_.end()) {
        return iterator->second;
    }

    auto face = resolve_font_face(style);
    if (text_face_cache_.size() >= 64U) {
        text_face_cache_.clear();
    }
    const auto [iterator, inserted] = text_face_cache_.emplace(std::move(key), face);
    (void)inserted;
    return iterator->second;
}

const std::vector<core::Point>&
D3D11DisplayListRenderer::cached_unit_circle_points(std::uint32_t segments) {
    segments = std::clamp(segments, 3U, 256U);
    auto [iterator, inserted] = primitive_point_cache_.unit_circle_points.try_emplace(segments);
    if (inserted) {
        auto& points = iterator->second;
        points.reserve(segments);
        for (auto index = 0U; index < segments; ++index) {
            const auto angle = static_cast<float>(index) / static_cast<float>(segments) * pi * 2.0F;
            points.push_back(core::Point{std::cos(angle), std::sin(angle)});
        }
    }
    return iterator->second;
}

const std::vector<D3D11DisplayListRenderer::UnitArcPoint>&
D3D11DisplayListRenderer::cached_quarter_arc_points(std::uint32_t segments) {
    segments = std::clamp(segments, 1U, 256U);
    auto [iterator, inserted] = primitive_point_cache_.quarter_arc_points.try_emplace(segments);
    if (inserted) {
        auto& points = iterator->second;
        points.reserve(static_cast<std::size_t>(segments) + 1U);
        for (auto step = 0U; step <= segments; ++step) {
            const auto angle = static_cast<float>(step) / static_cast<float>(segments) * pi * 0.5F;
            points.push_back(UnitArcPoint{.cosine = std::cos(angle), .sine = std::sin(angle)});
        }
    }
    return iterator->second;
}

std::vector<core::Point> D3D11DisplayListRenderer::rounded_rect_outline_from_cache(
    core::Rect rect, core::CornerRadius radius, std::uint32_t segments) {
    const auto max_x = std::max(rect.width * 0.5F, 0.0F);
    const auto max_y = std::max(rect.height * 0.5F, 0.0F);
    const auto rx = std::clamp(radius.x, 0.0F, max_x);
    const auto ry = std::clamp(radius.y, 0.0F, max_y);
    const auto& arc = cached_quarter_arc_points(segments);

    std::vector<core::Point> points;
    points.reserve(arc.size() * 4U);
    const std::array<core::Point, 4U> centers{
        core::Point{rect.x + rect.width - rx, rect.y + ry},
        core::Point{rect.x + rect.width - rx, rect.y + rect.height - ry},
        core::Point{rect.x + rx, rect.y + rect.height - ry}, core::Point{rect.x + rx, rect.y + ry}};
    for (const auto unit : arc) {
        points.push_back(
            core::Point{centers[0].x + unit.sine * rx, centers[0].y - unit.cosine * ry});
    }
    for (const auto unit : arc) {
        points.push_back(
            core::Point{centers[1].x + unit.cosine * rx, centers[1].y + unit.sine * ry});
    }
    for (const auto unit : arc) {
        points.push_back(
            core::Point{centers[2].x - unit.sine * rx, centers[2].y + unit.cosine * ry});
    }
    for (const auto unit : arc) {
        points.push_back(
            core::Point{centers[3].x - unit.cosine * rx, centers[3].y - unit.sine * ry});
    }
    return points;
}

std::vector<D3D11DisplayListRenderer::Vertex>&
D3D11DisplayListRenderer::prepare_primitive_vertices(std::size_t reserve_count) {
    recorder_state_.primitive_vertices.clear();
    if (recorder_state_.primitive_vertices.capacity() < reserve_count) {
        recorder_state_.primitive_vertices.reserve(reserve_count);
    }
    return recorder_state_.primitive_vertices;
}

D3D11DisplayListRenderer::TransformedGeometryFill&
D3D11DisplayListRenderer::transformed_geometry_fill_for(
    const rendering::PreparedGeometryFill& prepared_fill, core::Transform2D transform) {
    const auto prepared_signature = prepared_geometry_fill_signature(prepared_fill);
    for (auto& cached : transformed_geometry_fill_cache_) {
        if (cached.prepared_fill == &prepared_fill &&
            cached.prepared_signature == prepared_signature && cached.transform == transform) {
            cached.last_used_frame = frame_sequence_;
            return cached;
        }
    }

    auto* slot = static_cast<TransformedGeometryFill*>(nullptr);
    constexpr auto max_transformed_geometry_cache_entries = 64U;
    if (transformed_geometry_fill_cache_.size() >= max_transformed_geometry_cache_entries) {
        const auto iterator = std::min_element(
            transformed_geometry_fill_cache_.begin(), transformed_geometry_fill_cache_.end(),
            [](const TransformedGeometryFill& left, const TransformedGeometryFill& right) {
                return left.last_used_frame < right.last_used_frame;
            });
        if (iterator != transformed_geometry_fill_cache_.end()) {
            slot = &*iterator;
        }
    } else {
        transformed_geometry_fill_cache_.push_back(TransformedGeometryFill{});
        slot = &transformed_geometry_fill_cache_.back();
    }

    auto& cached = slot != nullptr ? *slot : transformed_geometry_fill_cache_.back();
    cached = TransformedGeometryFill{.prepared_fill = &prepared_fill,
                                     .prepared_signature = prepared_signature,
                                     .transform = transform,
                                     .last_used_frame = frame_sequence_};
    cached.filled_contours.resize(prepared_fill.filled_contours.size());
    for (std::size_t contour_index = 0U; contour_index < prepared_fill.filled_contours.size();
         ++contour_index) {
        const auto& contour = prepared_fill.filled_contours[contour_index];
        auto& transformed_contour = cached.filled_contours[contour_index];
        transformed_contour.reserve(contour.size());
        for (const auto point : contour) {
            transformed_contour.push_back(rendering::transform_point(point, transform));
        }
    }

    cached.tessellated_vertices.reserve(prepared_fill.tessellated_vertices.size());
    for (const auto point : prepared_fill.tessellated_vertices) {
        cached.tessellated_vertices.push_back(rendering::transform_point(point, transform));
    }
    return cached;
}

void D3D11DisplayListRenderer::render(ID3D11DeviceContext& context, ID3D11RenderTargetView& target,
                                      core::Color clear_color, const rendering::RenderScene* scene,
                                      const rendering::DirtyRegion& dirty_region, float dpi,
                                      std::uint32_t target_pixel_width,
                                      std::uint32_t target_pixel_height,
                                      const D3D11RenderResourceCache& resource_cache,
                                      const rendering::RenderFrameGraph* frame_graph) {
    if (primary_deferred_context_ == nullptr) {
        throw std::runtime_error("display-list deferred context is not available");
    }

    using Clock = std::chrono::steady_clock;
    const auto elapsed_ms = [](Clock::time_point first, Clock::time_point last) {
        return std::chrono::duration<double, std::milli>(last - first).count();
    };

    ++frame_sequence_;
    prune_frame_caches_if_needed();
    last_timing_metrics_ = {};

    if (!parallel_recording_enabled_) {
        const auto record_start = Clock::now();
        const auto resource_snapshot = resource_cache.snapshot();
        render_to_context(context, target, clear_color, scene, dirty_region, dpi,
                          target_pixel_width, target_pixel_height, resource_snapshot,
                          frame_graph);
        const auto record_finish = Clock::now();
        last_timing_metrics_.worker_record_ms = elapsed_ms(record_start, record_finish);
        last_timing_metrics_.work_item_count = 1U;
        last_timing_metrics_.command_list_count = 0U;
        last_timing_metrics_.parallel_recording = false;
        return;
    }

    // Architecture boundary: parallelism here is CPU recording of D3D11 deferred command lists.
    // The immediate context remains the only GPU submission path and ExecuteCommandList is always
    // called later on this caller thread in a stable sort order.
    const auto split_start = Clock::now();
    auto analysis = analyze_render_frame(clear_color, scene, dirty_region, dpi, target_pixel_width,
                                         target_pixel_height, resource_cache, frame_graph);
    const auto split_finish = Clock::now();
    last_timing_metrics_.task_split_ms = elapsed_ms(split_start, split_finish);

    const auto resource_start = Clock::now();
    if (analysis.use_parallel_recording) {
        prepare_text_resources_for_scene(scene, analysis.dirty_clips);
        upload_glyph_atlas_if_dirty(context, false);
        assign_glyph_snapshot(analysis, snapshot_glyph_atlas());
    }
    const auto resource_finish = Clock::now();
    last_timing_metrics_.resource_sync_ms = elapsed_ms(resource_start, resource_finish);

    const auto record_start = Clock::now();
    if (analysis.use_parallel_recording) {
        record_render_work_items(analysis, target);
    } else {
        render_to_context(*primary_deferred_context_.Get(), target, clear_color, scene,
                          dirty_region, dpi, target_pixel_width, target_pixel_height,
                          *analysis.resource_snapshot, frame_graph);

        analysis.work_items.clear();
        analysis.work_items.push_back(RenderWorkItem{.kind = RenderWorkKind::NodeSubtree,
                                                     .frame_graph_snapshot =
                                                         analysis.frame_graph_snapshot,
                                                     .resource_snapshot =
                                                         analysis.resource_snapshot,
                                                     .sort_key = RenderWorkSortKey{}});
        throw_if_failed(primary_deferred_context_->FinishCommandList(
                            FALSE, &analysis.work_items.back().command_list),
                        "failed to finish display-list deferred command list");
    }
    const auto record_finish = Clock::now();
    last_timing_metrics_.worker_record_ms = elapsed_ms(record_start, record_finish);

    const auto submit_start = Clock::now();
    if (!analysis.use_parallel_recording) {
        upload_glyph_atlas_if_dirty(context, false);
    }
    submit_recorded_work_items(context, analysis);
    const auto submit_finish = Clock::now();
    last_timing_metrics_.command_submit_ms = elapsed_ms(submit_start, submit_finish);
    last_timing_metrics_.parallel_recording = analysis.use_parallel_recording;
    last_timing_metrics_.work_item_count = analysis.work_items.size();
    last_timing_metrics_.command_list_count = 0U;
    for (const auto& item : analysis.work_items) {
        if (item.command_list != nullptr) {
            ++last_timing_metrics_.command_list_count;
        }
    }
}

void D3D11DisplayListRenderer::render_to_context(
    ID3D11DeviceContext& context, ID3D11RenderTargetView& target, core::Color clear_color,
    const rendering::RenderScene* scene, const rendering::DirtyRegion& dirty_region, float dpi,
    std::uint32_t target_pixel_width, std::uint32_t target_pixel_height,
    const D3D11RenderResourceCache::Snapshot& resource_snapshot,
    const rendering::RenderFrameGraph* frame_graph) {
    recorder_state_.active_frame_graph = frame_graph;
    apply_frame_graph_plan(frame_graph);
    begin_frame(context, target, dpi, target_pixel_width, target_pixel_height);

    dirty_clips_from_region(dirty_region,
                            rendering::layout::Size{target_dip_width_, target_dip_height_},
                            frame_dirty_clips_);
    clear_dirty_region(clear_color, frame_dirty_clips_);

    if (scene != nullptr && scene->root() != nullptr) {
        render_node(*scene->root(), frame_dirty_clips_, resource_snapshot);
    }

    end_frame();
}

D3D11DisplayListRenderer::RenderFrameAnalysis D3D11DisplayListRenderer::analyze_render_frame(
    core::Color clear_color, const rendering::RenderScene* scene,
    const rendering::DirtyRegion& dirty_region, float dpi, std::uint32_t target_pixel_width,
    std::uint32_t target_pixel_height, const D3D11RenderResourceCache& resource_cache,
    const rendering::RenderFrameGraph* frame_graph) {
    RenderFrameAnalysis analysis;
    analysis.dpi = std::max(dpi, 1.0F);
    analysis.target_pixel_width = std::max(target_pixel_width, 1U);
    analysis.target_pixel_height = std::max(target_pixel_height, 1U);
    const auto dip_scale = default_dpi / analysis.dpi;
    analysis.target_dip_width = static_cast<float>(analysis.target_pixel_width) * dip_scale;
    analysis.target_dip_height = static_cast<float>(analysis.target_pixel_height) * dip_scale;
    analysis.resource_snapshot =
        std::make_shared<D3D11RenderResourceCache::Snapshot>(resource_cache.snapshot());

    auto graph_snapshot = std::make_shared<RenderFrameGraphSnapshot>();
    if (frame_graph != nullptr) {
        graph_snapshot->graph = *frame_graph;
    } else if (scene != nullptr) {
        graph_snapshot->graph = rendering::build_render_frame_graph(*scene);
    }
    analysis.frame_graph_snapshot = graph_snapshot;

    dirty_clips_from_region(
        dirty_region,
        rendering::layout::Size{analysis.target_dip_width, analysis.target_dip_height},
        analysis.dirty_clips);
    analysis.dirty_tiles = slice_dirty_clips_for_parallel_recording(
        analysis.dirty_clips, analysis.target_dip_width, analysis.target_dip_height);
    if (analysis.dirty_tiles.empty()) {
        analysis.dirty_tiles = analysis.dirty_clips;
    }

    if (!analysis.dirty_clips.empty()) {
        analysis.work_items.push_back(RenderWorkItem{
            .kind = RenderWorkKind::Clear,
            .dirty_clips = analysis.dirty_clips,
            .frame_graph_snapshot = analysis.frame_graph_snapshot,
            .resource_snapshot = analysis.resource_snapshot,
            .sort_key = RenderWorkSortKey{.pass_order = 0U},
            .dependency_key = 0U,
            .clear_color = clear_color,
            .dpi = analysis.dpi,
            .target_pixel_width = analysis.target_pixel_width,
            .target_pixel_height = analysis.target_pixel_height,
            .barrier = true,
            .may_record_parallel = false});
    }

    const auto* root = scene != nullptr ? scene->root() : nullptr;
    if (root != nullptr && !analysis.dirty_tiles.empty()) {
        const auto can_split_root =
            (root->kind == rendering::RenderNodeKind::Picture ||
             root->kind == rendering::RenderNodeKind::Surface) &&
            !command_list_has_serial_barrier(root->commands);
        auto scene_order = 1U;
        const auto append_item = [&](RenderWorkKind kind, const rendering::RenderNode& node,
                                     std::span<const D3D11RenderDirtyClip> clips,
                                     std::uint32_t order, bool barrier) {
            auto item = RenderWorkItem{
                .kind = kind,
                .scene_subtree = &node,
                .dirty_clips = std::vector<D3D11RenderDirtyClip>{clips.begin(), clips.end()},
                .frame_graph_snapshot = analysis.frame_graph_snapshot,
                .resource_snapshot = analysis.resource_snapshot,
                .sort_key = RenderWorkSortKey{.pass_order = 1U, .scene_order = order},
                .dependency_key = barrier ? static_cast<std::uint64_t>(order) : 0U,
                .estimated_command_count =
                    kind == RenderWorkKind::NodeCommandsOnly ? node.commands.command_count()
                                                             : command_count_for_subtree(node),
                .dpi = analysis.dpi,
                .target_pixel_width = analysis.target_pixel_width,
                .target_pixel_height = analysis.target_pixel_height,
                .barrier = barrier,
                .may_record_parallel = !barrier};
            item.estimated_vertex_budget =
                estimate_vertex_budget_for_command_count(item.estimated_command_count);
            analysis.work_items.push_back(std::move(item));
        };

        if (can_split_root && (!root->children.empty() || !root->commands.empty())) {
            if (!root->commands.empty()) {
                for (std::size_t tile_index = 0U; tile_index < analysis.dirty_tiles.size();
                     ++tile_index) {
                    const auto clip = std::array<D3D11RenderDirtyClip, 1U>{
                        analysis.dirty_tiles[tile_index]};
                    append_item(RenderWorkKind::NodeCommandsOnly, *root, clip, scene_order++,
                                command_list_has_serial_barrier(root->commands));
                    analysis.work_items.back().sort_key.dirty_bucket_order =
                        static_cast<std::uint32_t>(tile_index);
                }
            }

            for (const auto& child : root->children) {
                if (!dirty_clips_intersect_node(analysis.dirty_tiles, child)) {
                    continue;
                }
                const auto barrier = subtree_has_serial_barrier(child);
                const auto split_child_by_tile = !barrier && analysis.dirty_tiles.size() > 1U;
                if (split_child_by_tile) {
                    for (std::size_t tile_index = 0U; tile_index < analysis.dirty_tiles.size();
                         ++tile_index) {
                        const auto clip = std::array<D3D11RenderDirtyClip, 1U>{
                            analysis.dirty_tiles[tile_index]};
                        if (!dirty_clips_intersect_node(clip, child)) {
                            continue;
                        }
                        append_item(RenderWorkKind::NodeSubtree, child, clip, scene_order,
                                    false);
                        analysis.work_items.back().sort_key.dirty_bucket_order =
                            static_cast<std::uint32_t>(tile_index);
                    }
                    ++scene_order;
                } else {
                    append_item(RenderWorkKind::NodeSubtree, child, analysis.dirty_clips,
                                scene_order++, barrier);
                }
            }
        } else {
            append_item(RenderWorkKind::NodeSubtree, *root, analysis.dirty_clips, scene_order++,
                        true);
        }
    }

    auto recordable_items = std::size_t{0U};
    auto parallel_items = std::size_t{0U};
    auto estimated_commands = std::size_t{0U};
    for (const auto& item : analysis.work_items) {
        if (item.kind == RenderWorkKind::Clear) {
            continue;
        }
        ++recordable_items;
        estimated_commands += item.estimated_command_count;
        if (item.may_record_parallel) {
            ++parallel_items;
        }
    }

    constexpr auto min_parallel_work_items = 2U;
    const auto pass_count =
        analysis.frame_graph_snapshot != nullptr ? analysis.frame_graph_snapshot->graph.passes.size()
                                                 : std::size_t{};
    const auto dirty_area = std::accumulate(
        analysis.dirty_clips.begin(), analysis.dirty_clips.end(), 0.0F,
        [](float total, const D3D11RenderDirtyClip& clip) {
            return total + rect_area(clip.cull_clip);
        });
    constexpr auto min_large_dirty_parallel_area = 16'384.0F;
    const auto large_dirty_scene =
        estimated_commands >= 128U && dirty_area >= min_large_dirty_parallel_area;
    analysis.use_parallel_recording =
        parallel_recording_enabled_ && resource_updates_allowed_ && recordable_items >= 2U &&
        parallel_items >= min_parallel_work_items &&
        (should_try_parallel_render_plan(estimated_commands, analysis.dirty_tiles.size(),
                                         pass_count) ||
         large_dirty_scene) &&
        shared_render_thread_pool().worker_count() > 1U;
    return analysis;
}

std::shared_ptr<const D3D11DisplayListRenderer::GlyphAtlasSnapshot>
D3D11DisplayListRenderer::snapshot_glyph_atlas() const {
    auto snapshot = std::make_shared<GlyphAtlasSnapshot>();
    snapshot->entries = glyph_atlas_entries_;
    snapshot->prepared_text_layouts = prepared_draw_text_layouts_;
    snapshot->texture = glyph_atlas_texture_;
    snapshot->view = glyph_atlas_view_;
    snapshot->generation = glyph_atlas_generation_;
    return snapshot;
}

void D3D11DisplayListRenderer::assign_glyph_snapshot(
    RenderFrameAnalysis& analysis, std::shared_ptr<const GlyphAtlasSnapshot> snapshot) const {
    analysis.glyph_atlas_snapshot = std::move(snapshot);
    for (auto& item : analysis.work_items) {
        item.glyph_atlas_snapshot = analysis.glyph_atlas_snapshot;
    }
}

void D3D11DisplayListRenderer::adopt_glyph_atlas_snapshot(
    const GlyphAtlasSnapshot& snapshot) {
    glyph_atlas_entries_ = snapshot.entries;
    prepared_draw_text_layouts_ = snapshot.prepared_text_layouts;
    glyph_atlas_texture_ = snapshot.texture;
    glyph_atlas_view_ = snapshot.view;
    glyph_atlas_generation_ = snapshot.generation;
    glyph_atlas_pixels_.clear();
    clear_glyph_atlas_dirty();
}

std::shared_ptr<D3D11DisplayListRenderer>
D3D11DisplayListRenderer::acquire_worker_recorder() {
    if (device_ == nullptr) {
        return {};
    }

    const std::scoped_lock lock(worker_recorder_mutex_);
    if (!worker_recorders_.empty()) {
        auto recorder = std::move(worker_recorders_.back());
        worker_recorders_.pop_back();
        return recorder;
    }
    return std::shared_ptr<D3D11DisplayListRenderer>(
        new D3D11DisplayListRenderer(*device_.Get(), RendererInstanceKind::Worker));
}

void D3D11DisplayListRenderer::release_worker_recorder(
    std::shared_ptr<D3D11DisplayListRenderer> recorder) {
    if (recorder == nullptr) {
        return;
    }

    const std::scoped_lock lock(worker_recorder_mutex_);
    worker_recorders_.push_back(std::move(recorder));
}

void D3D11DisplayListRenderer::ensure_worker_recorder_pool(std::size_t recorder_count) {
    if (device_ == nullptr || recorder_count == 0U) {
        return;
    }

    const std::scoped_lock lock(worker_recorder_mutex_);
    while (worker_recorders_.size() < recorder_count) {
        worker_recorders_.push_back(std::shared_ptr<D3D11DisplayListRenderer>(
            new D3D11DisplayListRenderer(*device_.Get(), RendererInstanceKind::Worker)));
    }
}

void D3D11DisplayListRenderer::record_render_work_items(RenderFrameAnalysis& analysis,
                                                        ID3D11RenderTargetView& target) {
    auto worker_item_indices = std::vector<std::size_t>{};
    worker_item_indices.reserve(analysis.work_items.size());
    for (std::size_t index = 0U; index < analysis.work_items.size(); ++index) {
        auto& item = analysis.work_items[index];
        if (item.kind == RenderWorkKind::Clear) {
            record_work_item_to_command_list(item, target);
            continue;
        }
        worker_item_indices.push_back(index);
    }

    if (worker_item_indices.empty()) {
        return;
    }

    auto& pool = shared_render_thread_pool();
    // A small UI frame usually benefits more from cache locality than from spawning one recorder
    // per hardware worker. Two deferred recorders keep parallelism useful without retaining a
    // large set of D3D contexts and glyph snapshots for showcase-sized scenes.
    constexpr auto max_parallel_recorders_per_frame = 2U;
    const auto task_count = std::min({worker_item_indices.size(), pool.worker_count(),
                                      static_cast<std::size_t>(max_parallel_recorders_per_frame)});
    ensure_worker_recorder_pool(task_count);

    auto futures = std::vector<std::future<void>>{};
    futures.reserve(task_count);
    auto next_item = std::atomic_size_t{0U};
    for (std::size_t task_index = 0U; task_index < task_count; ++task_index) {
        futures.push_back(pool.submit([this, &analysis, &target, &worker_item_indices,
                                       &next_item]() {
            auto recorder = acquire_worker_recorder();
            if (recorder == nullptr) {
                return;
            }
            if (analysis.glyph_atlas_snapshot != nullptr) {
                recorder->adopt_glyph_atlas_snapshot(*analysis.glyph_atlas_snapshot);
            }
            recorder->frame_sequence_ = frame_sequence_;

            auto first_recorded_item = std::numeric_limits<std::size_t>::max();
            for (;;) {
                const auto worker_item_offset = next_item.fetch_add(1U, std::memory_order_relaxed);
                if (worker_item_offset >= worker_item_indices.size()) {
                    break;
                }

                const auto item_index = worker_item_indices[worker_item_offset];
                if (first_recorded_item == std::numeric_limits<std::size_t>::max()) {
                    first_recorded_item = item_index;
                }
                recorder->record_work_item_to_command_list(analysis.work_items[item_index], target);
            }

            if (first_recorded_item != std::numeric_limits<std::size_t>::max()) {
                analysis.work_items[first_recorded_item].recorder_lease = std::move(recorder);
            } else {
                release_worker_recorder(std::move(recorder));
            }
        }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

void D3D11DisplayListRenderer::record_work_item_to_command_list(
    RenderWorkItem& work_item, ID3D11RenderTargetView& target) {
    if (primary_deferred_context_ == nullptr) {
        return;
    }

    render_work_item_to_context(*primary_deferred_context_.Get(), target, work_item,
                                work_item.dpi, work_item.target_pixel_width,
                                work_item.target_pixel_height);
    throw_if_failed(primary_deferred_context_->FinishCommandList(FALSE, &work_item.command_list),
                    "failed to finish display-list work item command list");
}

void D3D11DisplayListRenderer::render_work_item_to_context(
    ID3D11DeviceContext& context, ID3D11RenderTargetView& target,
    const RenderWorkItem& work_item, float dpi, std::uint32_t target_pixel_width,
    std::uint32_t target_pixel_height) {
    const auto* graph = work_item.frame_graph_snapshot != nullptr
                            ? &work_item.frame_graph_snapshot->graph
                            : static_cast<const rendering::RenderFrameGraph*>(nullptr);
    recorder_state_.active_frame_graph = graph;
    apply_frame_graph_plan(graph);
    if (work_item.estimated_vertex_budget > 0U) {
        recorder_state_.batch_vertices.reserve(
            std::min<std::size_t>(work_item.estimated_vertex_budget, max_vertices));
        recorder_state_.primitive_vertices.reserve(
            std::min<std::size_t>(work_item.estimated_vertex_budget, max_vertices));
    }

    begin_frame(context, target, dpi, target_pixel_width, target_pixel_height);
    switch (work_item.kind) {
    case RenderWorkKind::Clear:
        clear_dirty_region(work_item.clear_color, work_item.dirty_clips);
        break;
    case RenderWorkKind::NodeCommandsOnly:
        if (work_item.scene_subtree != nullptr && work_item.resource_snapshot != nullptr) {
            render_command_list(work_item.scene_subtree->commands, work_item.dirty_clips,
                                *work_item.resource_snapshot, false);
        }
        break;
    case RenderWorkKind::NodeSubtree:
        if (work_item.scene_subtree != nullptr && work_item.resource_snapshot != nullptr) {
            render_node(*work_item.scene_subtree, work_item.dirty_clips,
                        *work_item.resource_snapshot);
        }
        break;
    }
    end_frame();
}

void D3D11DisplayListRenderer::submit_recorded_work_items(
    ID3D11DeviceContext& context, RenderFrameAnalysis& analysis) {
    const auto work_item_less = [](const RenderWorkItem& left, const RenderWorkItem& right) {
        const auto& a = left.sort_key;
        const auto& b = right.sort_key;
        if (a.pass_order != b.pass_order) {
            return a.pass_order < b.pass_order;
        }
        if (a.scene_order != b.scene_order) {
            return a.scene_order < b.scene_order;
        }
        if (a.layer_depth != b.layer_depth) {
            return a.layer_depth < b.layer_depth;
        }
        if (a.z_order != b.z_order) {
            return a.z_order < b.z_order;
        }
        return a.dirty_bucket_order < b.dirty_bucket_order;
    };
    if (!std::is_sorted(analysis.work_items.begin(), analysis.work_items.end(),
                        work_item_less)) {
        std::stable_sort(analysis.work_items.begin(), analysis.work_items.end(),
                         work_item_less);
    }

    for (const auto& item : analysis.work_items) {
        if (item.command_list != nullptr) {
            context.ExecuteCommandList(item.command_list.Get(), FALSE);
        }
    }

    for (auto& item : analysis.work_items) {
        if (item.recorder_lease != nullptr) {
            release_worker_recorder(std::move(item.recorder_lease));
        }
    }
}

void D3D11DisplayListRenderer::create_pipeline(ID3D11Device& device) {
    throw_if_failed(device.CreateVertexShader(d3d11_display_list_vs, sizeof(d3d11_display_list_vs),
                                              nullptr, &vertex_shader_),
                    "failed to create display-list vertex shader");
    throw_if_failed(device.CreatePixelShader(d3d11_display_list_ps, sizeof(d3d11_display_list_ps),
                                             nullptr, &pixel_shader_),
                    "failed to create display-list pixel shader");

    const std::array<D3D11_INPUT_ELEMENT_DESC, 3U> input_elements{
        D3D11_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                                 D3D11_INPUT_PER_VERTEX_DATA, 0},
        D3D11_INPUT_ELEMENT_DESC{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, sizeof(float) * 2U,
                                 D3D11_INPUT_PER_VERTEX_DATA, 0},
        D3D11_INPUT_ELEMENT_DESC{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(float) * 6U,
                                 D3D11_INPUT_PER_VERTEX_DATA, 0}};
    throw_if_failed(device.CreateInputLayout(
                        input_elements.data(), static_cast<UINT>(input_elements.size()),
                        d3d11_display_list_vs, sizeof(d3d11_display_list_vs), &input_layout_),
                    "failed to create display-list input layout");

    D3D11_BUFFER_DESC vertex_buffer_description{};
    vertex_buffer_description.ByteWidth = sizeof(Vertex) * max_vertices;
    vertex_buffer_description.Usage = D3D11_USAGE_DYNAMIC;
    vertex_buffer_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertex_buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    throw_if_failed(device.CreateBuffer(&vertex_buffer_description, nullptr, &vertex_buffer_),
                    "failed to create display-list vertex buffer");

    D3D11_BUFFER_DESC constant_buffer_description{};
    constant_buffer_description.ByteWidth = sizeof(FrameConstants);
    constant_buffer_description.Usage = D3D11_USAGE_DYNAMIC;
    constant_buffer_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constant_buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    throw_if_failed(device.CreateBuffer(&constant_buffer_description, nullptr, &constant_buffer_),
                    "failed to create display-list constant buffer");

    D3D11_BLEND_DESC blend_description{};
    blend_description.RenderTarget[0].BlendEnable = TRUE;
    blend_description.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend_description.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_description.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_description.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_description.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_description.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_description.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    throw_if_failed(device.CreateBlendState(&blend_description, &blend_state_),
                    "failed to create display-list blend state");

    auto subpixel_blend_description = blend_description;
    subpixel_blend_description.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC1_COLOR;
    subpixel_blend_description.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC1_ALPHA;
    if (FAILED(device.CreateBlendState(&subpixel_blend_description, &subpixel_text_blend_state_))) {
        subpixel_text_blend_state_ = blend_state_;
    }

    auto stencil_blend_description = blend_description;
    stencil_blend_description.RenderTarget[0].BlendEnable = FALSE;
    stencil_blend_description.RenderTarget[0].RenderTargetWriteMask = 0U;
    throw_if_failed(device.CreateBlendState(&stencil_blend_description, &stencil_mask_blend_state_),
                    "failed to create display-list stencil mask blend state");

    D3D11_DEPTH_STENCIL_DESC stencil_disabled_description{};
    stencil_disabled_description.DepthEnable = FALSE;
    stencil_disabled_description.StencilEnable = FALSE;
    throw_if_failed(
        device.CreateDepthStencilState(&stencil_disabled_description, &stencil_disabled_state_),
        "failed to create display-list stencil disabled state");

    D3D11_DEPTH_STENCIL_DESC stencil_test_description{};
    stencil_test_description.DepthEnable = FALSE;
    stencil_test_description.StencilEnable = TRUE;
    stencil_test_description.StencilReadMask = 0xFFU;
    stencil_test_description.StencilWriteMask = 0U;
    stencil_test_description.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
    stencil_test_description.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    stencil_test_description.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    stencil_test_description.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    stencil_test_description.BackFace = stencil_test_description.FrontFace;
    throw_if_failed(device.CreateDepthStencilState(&stencil_test_description, &stencil_test_state_),
                    "failed to create display-list stencil test state");

    auto stencil_increment_description = stencil_test_description;
    stencil_increment_description.StencilWriteMask = 0xFFU;
    stencil_increment_description.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR_SAT;
    stencil_increment_description.BackFace = stencil_increment_description.FrontFace;
    throw_if_failed(
        device.CreateDepthStencilState(&stencil_increment_description, &stencil_increment_state_),
        "failed to create display-list stencil increment state");

    auto stencil_decrement_description = stencil_increment_description;
    stencil_decrement_description.FrontFace.StencilPassOp = D3D11_STENCIL_OP_DECR_SAT;
    stencil_decrement_description.BackFace = stencil_decrement_description.FrontFace;
    throw_if_failed(
        device.CreateDepthStencilState(&stencil_decrement_description, &stencil_decrement_state_),
        "failed to create display-list stencil decrement state");

    D3D11_RASTERIZER_DESC rasterizer_description{};
    rasterizer_description.FillMode = D3D11_FILL_SOLID;
    rasterizer_description.CullMode = D3D11_CULL_NONE;
    rasterizer_description.ScissorEnable = TRUE;
    rasterizer_description.DepthClipEnable = TRUE;
    throw_if_failed(device.CreateRasterizerState(&rasterizer_description, &rasterizer_state_),
                    "failed to create display-list rasterizer state");

    D3D11_SAMPLER_DESC sampler_description{};
    sampler_description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
    throw_if_failed(device.CreateSamplerState(&sampler_description, &sampler_state_),
                    "failed to create display-list sampler state");

    rebuild_pipeline_cache();
}

void D3D11DisplayListRenderer::rebuild_pipeline_cache() noexcept {
    const auto alpha_blend = blend_state_.Get();
    const auto subpixel_blend =
        subpixel_text_blend_state_ != nullptr ? subpixel_text_blend_state_.Get() : alpha_blend;
    draw_pipeline_cache_[static_cast<std::size_t>(PipelineVariant::AlphaBlend)] =
        CachedDrawPipeline{.blend_state = alpha_blend,
                           .depth_stencil_state = stencil_disabled_state_.Get()};
    draw_pipeline_cache_[static_cast<std::size_t>(PipelineVariant::SubpixelText)] =
        CachedDrawPipeline{.blend_state = subpixel_blend,
                           .depth_stencil_state = stencil_disabled_state_.Get()};
    draw_pipeline_cache_[static_cast<std::size_t>(PipelineVariant::StencilAlphaBlend)] =
        CachedDrawPipeline{.blend_state = alpha_blend,
                           .depth_stencil_state = stencil_test_state_.Get()};
    draw_pipeline_cache_[static_cast<std::size_t>(PipelineVariant::StencilSubpixelText)] =
        CachedDrawPipeline{.blend_state = subpixel_blend,
                           .depth_stencil_state = stencil_test_state_.Get()};
}

const D3D11DisplayListRenderer::CachedDrawPipeline&
D3D11DisplayListRenderer::cached_pipeline_for(TextureSamplingMode texture_mode,
                                              std::uint8_t stencil_depth) const noexcept {
    const auto subpixel = texture_mode == TextureSamplingMode::RgbSubpixelCoverage;
    const auto stencil = stencil_depth > 0U;
    const auto variant =
        stencil
            ? (subpixel ? PipelineVariant::StencilSubpixelText : PipelineVariant::StencilAlphaBlend)
            : (subpixel ? PipelineVariant::SubpixelText : PipelineVariant::AlphaBlend);
    return draw_pipeline_cache_[static_cast<std::size_t>(variant)];
}

void D3D11DisplayListRenderer::apply_frame_graph_plan(
    const rendering::RenderFrameGraph* frame_graph) {
    if (frame_graph == nullptr || frame_graph->empty()) {
        return;
    }

    auto plan_key = static_cast<std::size_t>(frame_graph->fingerprint);
    hash_combine(plan_key, frame_graph->command_count);
    hash_combine(plan_key, frame_graph->passes.size());
    if (plan_key == applied_frame_graph_plan_key_ && applied_frame_graph_plan_vertices_ > 0U) {
        return;
    }

    auto estimated_vertices = std::size_t{0U};
    for (const auto& pass : frame_graph->passes) {
        estimated_vertices += estimate_vertex_budget_for_pass(pass.kind, pass.command_count);
    }

    if (estimated_vertices > 0U) {
        recorder_state_.batch_vertices.reserve(std::min<std::size_t>(estimated_vertices, max_vertices));
        recorder_state_.primitive_vertices.reserve(std::min<std::size_t>(estimated_vertices, max_vertices));
    }
    applied_frame_graph_plan_key_ = plan_key;
    applied_frame_graph_plan_vertices_ = estimated_vertices;
}

void D3D11DisplayListRenderer::ensure_stencil_target() {
    if (device_ == nullptr) {
        return;
    }

    if (stencil_texture_ != nullptr) {
        D3D11_TEXTURE2D_DESC current_description{};
        stencil_texture_->GetDesc(&current_description);
        if (current_description.Width == target_pixel_width_ &&
            current_description.Height == target_pixel_height_) {
            return;
        }
    }

    stencil_view_.Reset();
    stencil_texture_.Reset();

    D3D11_TEXTURE2D_DESC stencil_description{};
    stencil_description.Width = target_pixel_width_;
    stencil_description.Height = target_pixel_height_;
    stencil_description.MipLevels = 1U;
    stencil_description.ArraySize = 1U;
    stencil_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    stencil_description.SampleDesc.Count = 1U;
    stencil_description.Usage = D3D11_USAGE_DEFAULT;
    stencil_description.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    throw_if_failed(device_->CreateTexture2D(&stencil_description, nullptr, &stencil_texture_),
                    "failed to create display-list stencil texture");
    throw_if_failed(
        device_->CreateDepthStencilView(stencil_texture_.Get(), nullptr, &stencil_view_),
        "failed to create display-list stencil view");
}

bool D3D11DisplayListRenderer::activate_stencil_target() {
    if (context_ == nullptr || render_target_ == nullptr) {
        return false;
    }

    ensure_stencil_target();
    if (stencil_view_ == nullptr) {
        return false;
    }

    ID3D11RenderTargetView* targets[] = {render_target_};
    context_->OMSetRenderTargets(1, targets, stencil_view_.Get());
    if (!stencil_used_this_frame_) {
        context_->ClearDepthStencilView(stencil_view_.Get(),
                                        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0F, 0U);
        stencil_used_this_frame_ = true;
        stencil_idle_frame_count_ = 0U;
    }
    return true;
}

void D3D11DisplayListRenderer::release_stale_stencil_target() noexcept {
    if (stencil_texture_ == nullptr) {
        stencil_idle_frame_count_ = 0U;
        return;
    }

    D3D11_TEXTURE2D_DESC current_description{};
    stencil_texture_->GetDesc(&current_description);
    if (current_description.Width != target_pixel_width_ ||
        current_description.Height != target_pixel_height_) {
        stencil_view_.Reset();
        stencil_texture_.Reset();
        stencil_idle_frame_count_ = 0U;
    }
}

void D3D11DisplayListRenderer::begin_frame(ID3D11DeviceContext& context,
                                           ID3D11RenderTargetView& target, float dpi,
                                           std::uint32_t target_pixel_width,
                                           std::uint32_t target_pixel_height) {
    context_ = &context;
    render_target_ = &target;
    dpi_ = std::max(dpi, 1.0F);
    target_pixel_width_ = std::max(target_pixel_width, 1U);
    target_pixel_height_ = std::max(target_pixel_height, 1U);
    const auto dip_scale = default_dpi / dpi_;
    target_dip_width_ = static_cast<float>(target_pixel_width_) * dip_scale;
    target_dip_height_ = static_cast<float>(target_pixel_height_) * dip_scale;
    stencil_used_this_frame_ = false;
    release_stale_stencil_target();

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0F;
    viewport.TopLeftY = 0.0F;
    viewport.Width = static_cast<float>(target_pixel_width_);
    viewport.Height = static_cast<float>(target_pixel_height_);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    context.RSSetViewports(1, &viewport);
    ID3D11RenderTargetView* targets[] = {&target};
    context.OMSetRenderTargets(1, targets, nullptr);
    context.IASetInputLayout(input_layout_.Get());
    context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context.PSSetShader(pixel_shader_.Get(), nullptr, 0);
    context.RSSetState(rasterizer_state_.Get());
    context.PSSetSamplers(0, 1, sampler_state_.GetAddressOf());

    const auto stride = static_cast<UINT>(sizeof(Vertex));
    const auto offset = 0U;
    ID3D11Buffer* vertex_buffer = vertex_buffer_.Get();
    context.IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);

    ID3D11Buffer* constant_buffer = constant_buffer_.Get();
    context.VSSetConstantBuffers(0, 1, &constant_buffer);
    context.PSSetConstantBuffers(0, 1, &constant_buffer);
    recorder_state_.bound_blend_state = nullptr;
    recorder_state_.bound_depth_stencil_state = nullptr;
    recorder_state_.bound_stencil_reference = 0U;
    recorder_state_.bound_shader_resource = nullptr;
    bind_blend_state(blend_state_.Get());
    bind_depth_stencil_state(stencil_disabled_state_.Get(), 0U);

    recorder_state_.state_stack.clear();
    recorder_state_.state_stack.push_back(
        DrawState{.clip = D3D11_RECT{0, 0, static_cast<LONG>(target_pixel_width_),
                                     static_cast<LONG>(target_pixel_height_)},
                  .kind = StateKind::Base});
    recorder_state_.batch_vertices.clear();
    if (recorder_state_.batch_vertices.capacity() < max_vertices) {
        recorder_state_.batch_vertices.reserve(max_vertices);
    }
    recorder_state_.batch_active = false;
    recorder_state_.constant_buffer_state_valid = false;
    apply_current_scissor();
}

void D3D11DisplayListRenderer::end_frame() {
    flush_batch();
    if (context_ != nullptr) {
        bind_shader_resource(nullptr);
        bind_depth_stencil_state(stencil_disabled_state_.Get(), 0U);
    }
    if (!stencil_used_this_frame_ && stencil_texture_ != nullptr) {
        constexpr auto max_stencil_idle_frames = 120U;
        if (++stencil_idle_frame_count_ > max_stencil_idle_frames) {
            stencil_view_.Reset();
            stencil_texture_.Reset();
            stencil_idle_frame_count_ = 0U;
        }
    }
    recorder_state_.state_stack.clear();
    recorder_state_.active_frame_graph = nullptr;
    recorder_state_.constant_buffer_state_valid = false;
    context_ = nullptr;
    render_target_ = nullptr;
}

void D3D11DisplayListRenderer::clear_dirty_region(
    core::Color clear_color, std::span<const D3D11RenderDirtyClip> dirty_clips) {
    if (context_ == nullptr || dirty_clips.empty()) {
        return;
    }

    const auto components = premultiplied_color(clear_color, 1.0F);
    if (render_target_ != nullptr && dirty_clips.size() == 1U) {
        const auto rect = dirty_clips.front().device_clip;
        const auto covers_target = rect.x <= 0.0F && rect.y <= 0.0F &&
                                   rect.x + rect.width >= target_dip_width_ &&
                                   rect.y + rect.height >= target_dip_height_;
        if (covers_target) {
            flush_batch();
            context_->ClearRenderTargetView(render_target_, components.data());
            return;
        }
    }

    auto& vertices = prepare_primitive_vertices(dirty_clips.size() * 6U);
    for (const auto dirty_clip : dirty_clips) {
        const auto rect = dirty_clip.device_clip;
        if (!rendering::layout::is_visible_rect(rect)) {
            continue;
        }

        const auto left = rect.x;
        const auto top = rect.y;
        const auto right = rect.x + rect.width;
        const auto bottom = rect.y + rect.height;
        vertices.push_back(Vertex{left, top, components[0], components[1], components[2],
                                  components[3], 0.0F, 0.0F});
        vertices.push_back(Vertex{right, top, components[0], components[1], components[2],
                                  components[3], 0.0F, 0.0F});
        vertices.push_back(Vertex{right, bottom, components[0], components[1], components[2],
                                  components[3], 0.0F, 0.0F});
        vertices.push_back(Vertex{left, top, components[0], components[1], components[2],
                                  components[3], 0.0F, 0.0F});
        vertices.push_back(Vertex{right, bottom, components[0], components[1], components[2],
                                  components[3], 0.0F, 0.0F});
        vertices.push_back(Vertex{left, bottom, components[0], components[1], components[2],
                                  components[3], 0.0F, 0.0F});
    }

    if (vertices.empty()) {
        return;
    }

    flush_batch();
    bind_blend_state(nullptr);
    bind_depth_stencil_state(stencil_disabled_state_.Get(), 0U);
    draw_vertices_now(vertices, nullptr, TextureSamplingMode::None);
    bind_blend_state(blend_state_.Get());
    bind_depth_stencil_state(stencil_disabled_state_.Get(), 0U);
}

void D3D11DisplayListRenderer::bind_blend_state(ID3D11BlendState* blend_state) noexcept {
    if (context_ == nullptr || recorder_state_.bound_blend_state == blend_state) {
        return;
    }
    context_->OMSetBlendState(blend_state, nullptr, 0xFFFFFFFFU);
    recorder_state_.bound_blend_state = blend_state;
}

void D3D11DisplayListRenderer::bind_depth_stencil_state(
    ID3D11DepthStencilState* depth_stencil_state, std::uint8_t stencil_reference) noexcept {
    if (context_ == nullptr) {
        return;
    }
    if (recorder_state_.bound_depth_stencil_state == depth_stencil_state &&
        recorder_state_.bound_stencil_reference == stencil_reference) {
        return;
    }
    context_->OMSetDepthStencilState(depth_stencil_state, stencil_reference);
    recorder_state_.bound_depth_stencil_state = depth_stencil_state;
    recorder_state_.bound_stencil_reference = stencil_reference;
}

void D3D11DisplayListRenderer::bind_shader_resource(
    ID3D11ShaderResourceView* texture_view) noexcept {
    if (context_ == nullptr || recorder_state_.bound_shader_resource == texture_view) {
        return;
    }
    context_->PSSetShaderResources(0, 1, &texture_view);
    recorder_state_.bound_shader_resource = texture_view;
}

void D3D11DisplayListRenderer::render_node(const rendering::RenderNode& node,
                                           std::span<const D3D11RenderDirtyClip> dirty_clips,
                                           const D3D11RenderResourceCache::Snapshot& resource_snapshot,
                                           bool force_commands) {
    if (!force_commands && !dirty_clips_intersect_node(dirty_clips, node)) {
        return;
    }

    if (node.commands.empty() && node.children.empty()) {
        return;
    }

    auto single_mapped_dirty_clip = std::array<D3D11RenderDirtyClip, 1U>{};
    auto single_command_dirty_clip = std::array<D3D11RenderDirtyClip, 1U>{};
    auto stack_mapped_dirty_clips = std::array<D3D11RenderDirtyClip, 32U>{};
    auto mapped_dirty_clips = std::vector<D3D11RenderDirtyClip>{};
    auto active_dirty_clips = dirty_clips;
    auto force_descendant_commands = force_commands;
    if (!force_commands) {
        if (const auto* covered_clip =
                covering_dirty_clip(dirty_clips, visual_bounds_for_node(node));
            covered_clip != nullptr) {
            single_mapped_dirty_clip[0] = *covered_clip;
            active_dirty_clips = std::span<const D3D11RenderDirtyClip>{
                single_mapped_dirty_clip.data(), single_mapped_dirty_clip.size()};
            force_descendant_commands = true;
        } else if (node.kind == rendering::RenderNodeKind::Layer) {
        if (rendering::is_identity_transform(node.transform)) {
            if (!node.clips_to_bounds) {
                active_dirty_clips = dirty_clips;
            } else if (dirty_clips.size() == 1U) {
                const auto cull_clip =
                    rendering::layout::intersect_rects(dirty_clips.front().cull_clip, node.bounds);
                if (!rendering::layout::is_visible_rect(cull_clip)) {
                    return;
                }
                single_mapped_dirty_clip[0] = D3D11RenderDirtyClip{
                    .device_clip = dirty_clips.front().device_clip, .cull_clip = cull_clip};
                active_dirty_clips = std::span<const D3D11RenderDirtyClip>{
                    single_mapped_dirty_clip.data(), single_mapped_dirty_clip.size()};
            } else if (dirty_clips.size() <= stack_mapped_dirty_clips.size()) {
                auto mapped_count = std::size_t{0U};
                for (const auto dirty_clip : dirty_clips) {
                    const auto cull_clip =
                        rendering::layout::intersect_rects(dirty_clip.cull_clip, node.bounds);
                    if (!rendering::layout::is_visible_rect(cull_clip)) {
                        continue;
                    }
                    stack_mapped_dirty_clips[mapped_count++] = D3D11RenderDirtyClip{
                        .device_clip = dirty_clip.device_clip, .cull_clip = cull_clip};
                }
                if (mapped_count == 0U) {
                    return;
                }
                active_dirty_clips = std::span<const D3D11RenderDirtyClip>{
                    stack_mapped_dirty_clips.data(), mapped_count};
            } else {
                mapped_dirty_clips = dirty_clips_for_clip_rect(dirty_clips, node.bounds);
                if (mapped_dirty_clips.empty()) {
                    return;
                }
                active_dirty_clips = mapped_dirty_clips;
            }
        } else if (dirty_clips.size() == 1U) {
            const auto inverse = inverse_transform(node.transform);
            if (!inverse.has_value()) {
                force_descendant_commands = true;
            } else {
                auto cull_clip = rendering::transform_rect(dirty_clips.front().cull_clip, *inverse);
                if (node.clips_to_bounds) {
                    cull_clip = rendering::layout::intersect_rects(cull_clip, node.bounds);
                }
                if (!rendering::layout::is_visible_rect(cull_clip)) {
                    return;
                }
                single_mapped_dirty_clip[0] = D3D11RenderDirtyClip{
                    .device_clip = dirty_clips.front().device_clip, .cull_clip = cull_clip};
                active_dirty_clips = std::span<const D3D11RenderDirtyClip>{
                    single_mapped_dirty_clip.data(), single_mapped_dirty_clip.size()};
            }
        } else if (dirty_clips.size() <= stack_mapped_dirty_clips.size()) {
            const auto inverse = inverse_transform(node.transform);
            if (!inverse.has_value()) {
                force_descendant_commands = true;
            } else {
                auto mapped_count = std::size_t{0U};
                for (const auto dirty_clip : dirty_clips) {
                    auto cull_clip = rendering::transform_rect(dirty_clip.cull_clip, *inverse);
                    if (node.clips_to_bounds) {
                        cull_clip = rendering::layout::intersect_rects(cull_clip, node.bounds);
                    }
                    if (!rendering::layout::is_visible_rect(cull_clip)) {
                        continue;
                    }
                    stack_mapped_dirty_clips[mapped_count++] = D3D11RenderDirtyClip{
                        .device_clip = dirty_clip.device_clip, .cull_clip = cull_clip};
                }
                if (mapped_count == 0U) {
                    return;
                }
                active_dirty_clips = std::span<const D3D11RenderDirtyClip>{
                    stack_mapped_dirty_clips.data(), mapped_count};
            }
        } else if (auto layer_clips = dirty_clips_for_layer(dirty_clips, node)) {
            if (layer_clips->empty()) {
                return;
            }
            mapped_dirty_clips = std::move(*layer_clips);
            active_dirty_clips = mapped_dirty_clips;
        } else {
            force_descendant_commands = true;
        }
        } else {
            if (dirty_clips.size() == 1U) {
                const auto cull_clip =
                    rendering::layout::intersect_rects(dirty_clips.front().cull_clip, node.bounds);
                if (!rendering::layout::is_visible_rect(cull_clip)) {
                    return;
                }
                single_mapped_dirty_clip[0] = D3D11RenderDirtyClip{
                    .device_clip = dirty_clips.front().device_clip, .cull_clip = cull_clip};
                active_dirty_clips = std::span<const D3D11RenderDirtyClip>{
                    single_mapped_dirty_clip.data(), single_mapped_dirty_clip.size()};
            } else if (dirty_clips.size() <= stack_mapped_dirty_clips.size()) {
                auto mapped_count = std::size_t{0U};
                for (const auto dirty_clip : dirty_clips) {
                    const auto cull_clip =
                        rendering::layout::intersect_rects(dirty_clip.cull_clip, node.bounds);
                    if (!rendering::layout::is_visible_rect(cull_clip)) {
                        continue;
                    }
                    stack_mapped_dirty_clips[mapped_count++] = D3D11RenderDirtyClip{
                        .device_clip = dirty_clip.device_clip, .cull_clip = cull_clip};
                }
                if (mapped_count == 0U) {
                    return;
                }
                active_dirty_clips = std::span<const D3D11RenderDirtyClip>{
                    stack_mapped_dirty_clips.data(), mapped_count};
            } else {
                mapped_dirty_clips = dirty_clips_for_clip_rect(dirty_clips, node.bounds);
                if (mapped_dirty_clips.empty()) {
                    return;
                }
                active_dirty_clips = mapped_dirty_clips;
            }
        }
    }

    switch (node.kind) {
    case rendering::RenderNodeKind::Layer:
        push_layer(rendering::RenderLayerOptions{.bounds = node.bounds,
                                                 .opacity = node.opacity,
                                                 .transform = node.transform,
                                                 .clips_to_bounds = node.clips_to_bounds});
        break;
    case rendering::RenderNodeKind::Clip:
        push_clip(node.bounds);
        break;
    case rendering::RenderNodeKind::Picture:
    case rendering::RenderNodeKind::Surface:
        break;
    }

    auto command_dirty_clips = active_dirty_clips;
    auto force_command_list = force_descendant_commands;
    if (!force_command_list && !node.commands.empty()) {
        if (const auto* covered_clip =
                covering_dirty_clip(active_dirty_clips, node.commands.bounds());
            covered_clip != nullptr) {
            single_command_dirty_clip[0] = *covered_clip;
            command_dirty_clips = std::span<const D3D11RenderDirtyClip>{
                single_command_dirty_clip.data(), single_command_dirty_clip.size()};
            force_command_list = true;
        }
    }

    if (!node.commands.empty() &&
        (force_command_list ||
         dirty_clips_intersect_rect(command_dirty_clips, node.commands.bounds()))) {
        render_command_list(node.commands, command_dirty_clips, resource_snapshot,
                            force_command_list);
    }
    for (const auto& child : node.children) {
        render_node(child, active_dirty_clips, resource_snapshot, force_descendant_commands);
    }

    switch (node.kind) {
    case rendering::RenderNodeKind::Layer:
        pop_layer();
        break;
    case rendering::RenderNodeKind::Clip:
        pop_clip();
        break;
    case rendering::RenderNodeKind::Picture:
    case rendering::RenderNodeKind::Surface:
        break;
    }
}

void D3D11DisplayListRenderer::render_command_list(
    const rendering::RenderCommandList& commands, std::span<const D3D11RenderDirtyClip> dirty_clips,
    const D3D11RenderResourceCache::Snapshot& resource_snapshot, bool force_commands) {
    const auto& opcodes = commands.opcodes();
    if (opcodes.empty() || dirty_clips.empty()) {
        return;
    }

    if (force_commands && dirty_clips.size() == 1U) {
        const auto visible_vertex_budget =
            std::min<std::size_t>(static_cast<std::size_t>(opcodes.size()) * 24U, max_vertices);
        if (visible_vertex_budget > 0U) {
            recorder_state_.batch_vertices.reserve(visible_vertex_budget);
            recorder_state_.primitive_vertices.reserve(visible_vertex_budget);
        }

        push_device_clip(dirty_clips.front().device_clip);
        for (std::size_t opcode_index = 0U; opcode_index < opcodes.size(); ++opcode_index) {
            render_command(commands, opcode_index, resource_snapshot);
        }
        pop_clip();
        return;
    }

    if (!force_commands && dirty_clips.size() == 1U) {
        const auto* visible_indices = static_cast<const std::vector<std::uint32_t>*>(nullptr);
        recorder_state_.light_plan_command_indices.clear();
        if (render_plan_cache_ != nullptr) {
            visible_indices = &render_plan_cache_->visibility_indices_for(
                commands, dirty_clips.front().cull_clip, false, frame_sequence_);
        } else {
            collect_visible_opcode_indices(commands, dirty_clips.front().cull_clip,
                                           recorder_state_.light_plan_command_indices);
            visible_indices = &recorder_state_.light_plan_command_indices;
        }
        if (visible_indices == nullptr || visible_indices->empty()) {
            return;
        }
        const auto visible_vertex_budget = std::min<std::size_t>(
            static_cast<std::size_t>(visible_indices->size()) * 24U, max_vertices);
        recorder_state_.batch_vertices.reserve(visible_vertex_budget);
        recorder_state_.primitive_vertices.reserve(visible_vertex_budget);
        push_device_clip(dirty_clips.front().device_clip);
        for (const auto opcode_index : *visible_indices) {
            render_command(commands, opcode_index, resource_snapshot);
        }
        pop_clip();
        return;
    }

    const auto* command_frame_graph =
        render_plan_cache_ != nullptr
            ? render_plan_cache_->frame_graph_for(commands, recorder_state_.active_frame_graph, frame_sequence_)
            : recorder_state_.active_frame_graph;
    auto fallback_plan = PreparedRenderPlan{};
    const auto* plan = static_cast<const PreparedRenderPlan*>(nullptr);
    if (render_plan_cache_ != nullptr) {
        plan = &render_plan_cache_->render_plan_for(commands, dirty_clips, command_frame_graph,
                                                    target_dip_width_, target_dip_height_,
                                                    force_commands, frame_sequence_);
    } else {
        fallback_plan = prepare_render_plan(commands, dirty_clips, command_frame_graph,
                                            target_dip_width_, target_dip_height_, force_commands);
        plan = &fallback_plan;
    }
    if (plan == nullptr || plan->visible_command_count == 0U) {
        return;
    }

    if (plan->tiles.size() == 1U) {
        const auto& tile = plan->tiles.front();
        const auto& command_indices = tile.uses_shared_command_indices ? plan->shared_command_indices
                                                                       : tile.command_indices;
        if (command_indices.empty()) {
            return;
        }

        push_device_clip(tile.device_clip);
        for (const auto opcode_index : command_indices) {
            render_command(commands, opcode_index, resource_snapshot);
        }
        pop_clip();
        return;
    }

    auto visible_vertex_budget = std::size_t{0U};
    for (const auto& pass : plan->passes) {
        visible_vertex_budget +=
            estimate_vertex_budget_for_pass(pass.kind, pass.visible_command_count);
    }
    if (visible_vertex_budget > 0U) {
        recorder_state_.batch_vertices.reserve(std::min<std::size_t>(visible_vertex_budget, max_vertices));
        recorder_state_.primitive_vertices.reserve(std::min<std::size_t>(visible_vertex_budget, max_vertices));
    }

    for (const auto& tile : plan->tiles) {
        const auto& command_indices =
            tile.uses_shared_command_indices ? plan->shared_command_indices : tile.command_indices;
        if (command_indices.empty()) {
            continue;
        }
        push_device_clip(tile.device_clip);
        for (const auto opcode_index : command_indices) {
            render_command(commands, opcode_index, resource_snapshot);
        }
        pop_clip();
    }
}

template <typename Payload>
[[nodiscard]] const Payload* payload_at(const std::vector<Payload>& payloads,
                                        const std::vector<std::uint32_t>& indices,
                                        std::size_t opcode_index) noexcept {
    if (opcode_index >= indices.size()) {
        return nullptr;
    }
    const auto payload_index = indices[opcode_index];
    return payload_index < payloads.size() ? &payloads[payload_index] : nullptr;
}

void D3D11DisplayListRenderer::prepare_text_resources_for_scene(
    const rendering::RenderScene* scene,
    std::span<const D3D11RenderDirtyClip> dirty_clips) {
    prepared_draw_text_layouts_.clear();
    prepared_text_layout_resource_keys_.clear();
    if (scene == nullptr || scene->root() == nullptr || dirty_clips.empty()) {
        return;
    }
    prepare_text_resources_for_node(*scene->root(), dirty_clips);
}

void D3D11DisplayListRenderer::prepare_text_resources_for_node(
    const rendering::RenderNode& node,
    std::span<const D3D11RenderDirtyClip> dirty_clips) {
    if (!dirty_clips_intersect_node(dirty_clips, node)) {
        return;
    }

    prepare_text_resources_for_command_list(node.commands);
    for (const auto& child : node.children) {
        prepare_text_resources_for_node(child, dirty_clips);
    }
}

void D3D11DisplayListRenderer::prepare_text_resources_for_command_list(
    const rendering::RenderCommandList& commands) {
    const auto& opcodes = commands.opcodes();
    const auto& indices = commands.opcode_payload_indices();
    for (std::size_t opcode_index = 0U; opcode_index < opcodes.size(); ++opcode_index) {
        switch (opcodes[opcode_index].opcode) {
        case rendering::RenderCommandType::DrawText:
            if (const auto* payload =
                    payload_at(commands.draw_text_payloads(), indices, opcode_index)) {
                if (payload->text_view().empty() ||
                    !rendering::layout::is_visible_rect(payload->rect)) {
                    break;
                }
                const auto layout = render_worker_text_engine().layout_text(
                    payload->text_view(), payload->style,
                    rendering::TextLayoutOptions{.max_width = payload->rect.width,
                                                 .max_height = payload->rect.height});
                prepared_draw_text_layouts_.push_back(
                    PreparedDrawTextLayoutSnapshot{.commands = &commands,
                                                   .opcode_index = opcode_index,
                                                   .layout = std::make_shared<rendering::TextLayout>(
                                                       layout)});
                prepare_text_layout_resources(layout, nullptr);
            }
            break;
        case rendering::RenderCommandType::DrawTextLayout:
            if (const auto* payload =
                    payload_at(commands.draw_text_layout_payloads(), indices, opcode_index)) {
                if (const auto* layout = payload->layout_value()) {
                    prepare_text_layout_resources(*layout, payload->prepared_glyphs.get());
                }
            }
            break;
        default:
            break;
        }
    }
}

void D3D11DisplayListRenderer::prepare_text_layout_resources(
    const rendering::TextLayout& layout,
    const rendering::PreparedTextGlyphCoverageList* prepared) {
    if (layout.text.empty()) {
        return;
    }

    const auto resource_key = text_atlas_run_fingerprint(layout);
    if (!prepared_text_layout_resource_keys_.insert(resource_key).second) {
        return;
    }

    auto default_face = cached_font_face(layout.style);
    if (default_face == nullptr) {
        return;
    }

    const auto face_for_glyph = [&](const rendering::TextGlyph& glyph) {
        const auto& font_family =
            glyph.font_family.empty() ? layout.style.font_family : glyph.font_family;
        if (font_family.empty() || font_family == layout.style.font_family) {
            return default_face;
        }

        auto glyph_style = layout.style;
        glyph_style.font_family = font_family;
        auto face = cached_font_face(glyph_style);
        return face != nullptr ? face : default_face;
    };

    for (std::size_t glyph_index = 0U; glyph_index < layout.glyphs.size(); ++glyph_index) {
        const auto& glyph = layout.glyphs[glyph_index];
        if (glyph.glyph_index == 0U) {
            continue;
        }
        if (prepared != nullptr && glyph_index < prepared->glyphs_by_layout_index.size()) {
            if (const auto& coverage = prepared->glyphs_by_layout_index[glyph_index]) {
                (void)prepared_glyph_atlas_entry(*coverage);
                continue;
            }
        }

        auto glyph_face = face_for_glyph(glyph);
        if (glyph_face != nullptr) {
            (void)glyph_atlas_entry(*glyph_face.Get(), glyph, layout.style, prepared);
        }
    }
}

const rendering::TextLayout* D3D11DisplayListRenderer::prepared_draw_text_layout_for(
    const rendering::RenderCommandList& commands, std::size_t opcode_index) const noexcept {
    if (resource_updates_allowed_) {
        return nullptr;
    }

    for (const auto& prepared : prepared_draw_text_layouts_) {
        if (prepared.commands == &commands && prepared.opcode_index == opcode_index &&
            prepared.layout != nullptr) {
            return prepared.layout.get();
        }
    }
    return nullptr;
}

void D3D11DisplayListRenderer::render_command(const rendering::RenderCommandList& commands,
                                              std::size_t opcode_index,
                                              const D3D11RenderResourceCache::Snapshot& resource_snapshot) {
    const auto& opcodes = commands.opcodes();
    if (opcode_index >= opcodes.size()) {
        return;
    }

    const auto& indices = commands.opcode_payload_indices();
    switch (opcodes[opcode_index].opcode) {
    case rendering::RenderCommandType::Save: {
        auto state = recorder_state_.state_stack.back();
        state.kind = StateKind::Save;
        state.geometry_clip.reset();
        state.prepared_geometry_clip.reset();
        recorder_state_.state_stack.push_back(std::move(state));
    }
        apply_current_scissor();
        break;
    case rendering::RenderCommandType::Restore:
        pop_state();
        break;
    case rendering::RenderCommandType::PushClip:
        if (const auto* payload =
                payload_at(commands.push_clip_payloads(), indices, opcode_index)) {
            push_clip(payload->rect);
        }
        break;
    case rendering::RenderCommandType::PushGeometryClip:
        if (const auto* payload =
                payload_at(commands.push_geometry_clip_payloads(), indices, opcode_index)) {
            push_geometry_clip(*payload);
        }
        break;
    case rendering::RenderCommandType::PopClip:
        pop_clip();
        break;
    case rendering::RenderCommandType::PopGeometryClip:
        pop_geometry_clip();
        break;
    case rendering::RenderCommandType::PushLayer:
        if (const auto* payload =
                payload_at(commands.push_layer_payloads(), indices, opcode_index)) {
            push_layer(payload->options);
        }
        break;
    case rendering::RenderCommandType::PopLayer:
        pop_layer();
        break;
    case rendering::RenderCommandType::DrawLine:
        if (const auto* payload =
                payload_at(commands.draw_line_payloads(), indices, opcode_index)) {
            draw_line(payload->start, payload->end, payload->color, payload->stroke_width);
        }
        break;
    case rendering::RenderCommandType::FillRect:
        if (const auto* payload =
                payload_at(commands.fill_rect_payloads(), indices, opcode_index)) {
            draw_solid_rect(payload->rect, payload->color);
        }
        break;
    case rendering::RenderCommandType::FillPixelSnappedRect:
        if (const auto* payload =
                payload_at(commands.fill_pixel_snapped_rect_payloads(), indices, opcode_index)) {
            draw_solid_rect(payload->rect, payload->color);
        }
        break;
    case rendering::RenderCommandType::StrokePixelSnappedRect:
        if (const auto* payload =
                payload_at(commands.stroke_pixel_snapped_rect_payloads(), indices, opcode_index)) {
            draw_stroke_rect(payload->rect, payload->color, payload->stroke_width);
        }
        break;
    case rendering::RenderCommandType::StrokeRect:
        if (const auto* payload =
                payload_at(commands.stroke_rect_payloads(), indices, opcode_index)) {
            draw_stroke_rect(payload->rect, payload->color, payload->stroke_width);
        }
        break;
    case rendering::RenderCommandType::FillRoundedRect:
        if (const auto* payload =
                payload_at(commands.fill_rounded_rect_payloads(), indices, opcode_index)) {
            fill_rounded_rect(payload->rect, payload->radius, payload->color);
        }
        break;
    case rendering::RenderCommandType::StrokeRoundedRect:
        if (const auto* payload =
                payload_at(commands.stroke_rounded_rect_payloads(), indices, opcode_index)) {
            stroke_rounded_rect(payload->rect, payload->radius, payload->color,
                                payload->stroke_width);
        }
        break;
    case rendering::RenderCommandType::FillEllipse:
        if (const auto* payload =
                payload_at(commands.fill_ellipse_payloads(), indices, opcode_index)) {
            draw_ellipse(payload->rect, payload->color);
        }
        break;
    case rendering::RenderCommandType::StrokeEllipse:
        if (const auto* payload =
                payload_at(commands.stroke_ellipse_payloads(), indices, opcode_index)) {
            draw_stroke_ellipse(payload->rect, payload->color, payload->stroke_width);
        }
        break;
    case rendering::RenderCommandType::FillGeometry:
        if (const auto* payload =
                payload_at(commands.fill_geometry_payloads(), indices, opcode_index)) {
            fill_geometry(*payload);
        }
        break;
    case rendering::RenderCommandType::StrokeGeometry:
        if (const auto* payload =
                payload_at(commands.stroke_geometry_payloads(), indices, opcode_index)) {
            stroke_geometry(*payload);
        }
        break;
    case rendering::RenderCommandType::DrawImage:
        if (const auto* payload =
                payload_at(commands.draw_image_payloads(), indices, opcode_index)) {
            draw_image(payload->resource_id, payload->options, resource_snapshot);
        }
        break;
    case rendering::RenderCommandType::DrawBoxShadow:
        if (const auto* payload =
                payload_at(commands.draw_box_shadow_payloads(), indices, opcode_index)) {
            draw_box_shadow(payload->rect, payload->style);
        }
        break;
    case rendering::RenderCommandType::DrawText:
        if (const auto* payload =
                payload_at(commands.draw_text_payloads(), indices, opcode_index)) {
            if (const auto* prepared_layout =
                    prepared_draw_text_layout_for(commands, opcode_index);
                prepared_layout != nullptr) {
                draw_text_layout(*prepared_layout,
                                 rendering::layout::Point{payload->rect.x, payload->rect.y});
            } else {
                draw_text(payload->text_view(), payload->rect, payload->style);
            }
        }
        break;
    case rendering::RenderCommandType::DrawTextLayout:
        if (const auto* payload =
                payload_at(commands.draw_text_layout_payloads(), indices, opcode_index)) {
            draw_text_layout(*payload);
        }
        break;
    }
}

void D3D11DisplayListRenderer::push_clip(core::Rect rect) {
    flush_batch();
    auto state = recorder_state_.state_stack.back();
    const auto clip_rect = rendering::transform_rect(rect, state.transform);
    state.clip = intersect_scissor(
        state.clip, to_scissor(clip_rect, dpi_, target_pixel_width_, target_pixel_height_));
    state.kind = StateKind::RectClip;
    state.geometry_clip.reset();
    state.prepared_geometry_clip.reset();
    recorder_state_.state_stack.push_back(state);
    apply_current_scissor();
}

void D3D11DisplayListRenderer::push_device_clip(core::Rect rect) {
    flush_batch();
    auto state = recorder_state_.state_stack.back();
    state.clip = intersect_scissor(
        state.clip, to_scissor(rect, dpi_, target_pixel_width_, target_pixel_height_));
    state.kind = StateKind::RectClip;
    state.geometry_clip.reset();
    state.prepared_geometry_clip.reset();
    recorder_state_.state_stack.push_back(state);
    apply_current_scissor();
}

void D3D11DisplayListRenderer::pop_clip() {
    pop_state();
}

void D3D11DisplayListRenderer::push_layer(const rendering::RenderLayerOptions& options) {
    flush_batch();
    auto state = recorder_state_.state_stack.back();
    state.opacity *= std::clamp(options.opacity, 0.0F, 1.0F);
    state.transform = rendering::multiply_transforms(state.transform, options.transform);
    if (options.clips_to_bounds) {
        const auto clip_rect = rendering::transform_rect(options.bounds, state.transform);
        state.clip = intersect_scissor(
            state.clip, to_scissor(clip_rect, dpi_, target_pixel_width_, target_pixel_height_));
    }
    state.kind = StateKind::Layer;
    state.geometry_clip.reset();
    state.prepared_geometry_clip.reset();
    recorder_state_.state_stack.push_back(state);
    apply_current_scissor();
}

void D3D11DisplayListRenderer::pop_layer() {
    pop_state();
}

void D3D11DisplayListRenderer::pop_geometry_clip() {
    pop_state();
}

void D3D11DisplayListRenderer::pop_state() {
    if (recorder_state_.state_stack.size() > 1U) {
        flush_batch();
        const auto& state = recorder_state_.state_stack.back();
        if (state.kind == StateKind::GeometryClip && state.geometry_clip != nullptr &&
            state.stencil_depth > 0U) {
            if (state.prepared_geometry_clip != nullptr &&
                !state.prepared_geometry_clip->tessellated_vertices.empty()) {
                render_stencil_clip(*state.prepared_geometry_clip, state.stencil_depth,
                                    StencilUpdateOp::Decrement);
            } else {
                render_stencil_clip(*state.geometry_clip, state.stencil_depth,
                                    StencilUpdateOp::Decrement);
            }
        }
        recorder_state_.state_stack.pop_back();
    }
    apply_current_scissor();
}

void D3D11DisplayListRenderer::apply_current_scissor() {
    if (context_ == nullptr || recorder_state_.state_stack.empty()) {
        return;
    }
    auto clip = recorder_state_.state_stack.back().clip;
    context_->RSSetScissorRects(1, &clip);
}

void D3D11DisplayListRenderer::draw_solid_rect(core::Rect rect, core::Color color) {
    if (!rendering::layout::is_visible_rect(rect)) {
        return;
    }
    const auto& state = recorder_state_.state_stack.back();
    if (!transform_has_axis_aligned_basis(state.transform)) {
        fill_polygon(
            std::vector<core::Point>{core::Point{rect.x, rect.y},
                                     core::Point{rect.x + rect.width, rect.y},
                                     core::Point{rect.x + rect.width, rect.y + rect.height},
                                     core::Point{rect.x, rect.y + rect.height}},
            color);
        return;
    }

    const auto draw_color = multiply_alpha(color, state.opacity);
    const auto alpha = static_cast<float>(draw_color.alpha) / 255.0F;
    const auto red = (static_cast<float>(draw_color.red) / 255.0F) * alpha;
    const auto green = (static_cast<float>(draw_color.green) / 255.0F) * alpha;
    const auto blue = (static_cast<float>(draw_color.blue) / 255.0F) * alpha;

    const auto top_left =
        rendering::transform_point(rendering::layout::Point{rect.x, rect.y}, state.transform);
    const auto top_right = rendering::transform_point(
        rendering::layout::Point{rect.x + rect.width, rect.y}, state.transform);
    const auto bottom_right = rendering::transform_point(
        rendering::layout::Point{rect.x + rect.width, rect.y + rect.height}, state.transform);
    const auto bottom_left = rendering::transform_point(
        rendering::layout::Point{rect.x, rect.y + rect.height}, state.transform);

    const std::array<Vertex, 6U> vertices{
        {{top_left.x, top_left.y, red, green, blue, alpha, 0.0F, 0.0F},
         {top_right.x, top_right.y, red, green, blue, alpha, 1.0F, 0.0F},
         {bottom_right.x, bottom_right.y, red, green, blue, alpha, 1.0F, 1.0F},
         {top_left.x, top_left.y, red, green, blue, alpha, 0.0F, 0.0F},
         {bottom_right.x, bottom_right.y, red, green, blue, alpha, 1.0F, 1.0F},
         {bottom_left.x, bottom_left.y, red, green, blue, alpha, 0.0F, 1.0F}}};
    submit_vertices(vertices, nullptr);
}

void D3D11DisplayListRenderer::draw_stroke_rect(core::Rect rect, core::Color color,
                                                float stroke_width) {
    if (!rendering::layout::is_visible_rect(rect)) {
        return;
    }
    const auto width = std::max(stroke_width, 1.0F);
    draw_solid_rect(rendering::layout::Rect{rect.x, rect.y, rect.width, width}, color);
    draw_solid_rect(
        rendering::layout::Rect{rect.x, rect.y + rect.height - width, rect.width, width}, color);
    draw_solid_rect(rendering::layout::Rect{rect.x, rect.y, width, rect.height}, color);
    draw_solid_rect(
        rendering::layout::Rect{rect.x + rect.width - width, rect.y, width, rect.height}, color);
}

void D3D11DisplayListRenderer::draw_line(core::Point start, core::Point end, core::Color color,
                                         float stroke_width) {
    draw_line_segment(start, end, color, stroke_width, true, true);
}

void D3D11DisplayListRenderer::draw_line_segment(core::Point start, core::Point end,
                                                 core::Color color, float stroke_width,
                                                 bool antialias_start_cap, bool antialias_end_cap) {
    const auto delta_x = end.x - start.x;
    const auto delta_y = end.y - start.y;
    const auto length = std::sqrt(delta_x * delta_x + delta_y * delta_y);
    if (length <= 0.0001F) {
        return;
    }

    const auto scale = std::max(dpi_, 1.0F) / default_dpi;
    const auto pixel_width = stroke_pixel_width(stroke_width, dpi_);
    const auto snapped_width = static_cast<float>(pixel_width) / scale;
    const auto half_width = snapped_width * 0.5F;
    if (std::abs(delta_y) <= geometry_epsilon) {
        const auto center_y = snap_stroke_center((start.y + end.y) * 0.5F, pixel_width, dpi_);
        draw_solid_rect(rendering::layout::Rect{std::min(start.x, end.x), center_y - half_width,
                                                std::abs(delta_x), snapped_width},
                        color);
        return;
    }
    if (std::abs(delta_x) <= geometry_epsilon) {
        const auto center_x = snap_stroke_center((start.x + end.x) * 0.5F, pixel_width, dpi_);
        draw_solid_rect(rendering::layout::Rect{center_x - half_width, std::min(start.y, end.y),
                                                snapped_width, std::abs(delta_y)},
                        color);
        return;
    }

    const auto& state = recorder_state_.state_stack.back();
    const auto color_components = premultiplied_color(color, state.opacity);
    const auto unit_x = delta_x / length;
    const auto unit_y = delta_y / length;
    const auto normal_x = -unit_y;
    const auto normal_y = unit_x;
    const auto fringe = physical_pixel_size(dpi_) * 0.5F;
    const auto solid_half_width = std::max(0.0F, half_width - fringe);
    const auto outer_half_width = half_width + fringe;
    const auto start_outer = antialias_start_cap ? -fringe : 0.0F;
    const auto end_outer = antialias_end_cap ? length + fringe : length;

    const std::array<float, 4U> longitudinal_offsets{start_outer, 0.0F, length, end_outer};
    const std::array<float, 4U> longitudinal_coverage{antialias_start_cap ? 0.0F : 1.0F, 1.0F, 1.0F,
                                                      antialias_end_cap ? 0.0F : 1.0F};
    const std::array<float, 4U> lateral_offsets{-outer_half_width, -solid_half_width,
                                                solid_half_width, outer_half_width};
    const std::array<float, 4U> lateral_coverage{0.0F, 1.0F, 1.0F, 0.0F};

    std::array<std::array<Vertex, 4U>, 4U> grid{};
    for (std::size_t longitudinal = 0U; longitudinal < longitudinal_offsets.size();
         ++longitudinal) {
        for (std::size_t lateral = 0U; lateral < lateral_offsets.size(); ++lateral) {
            const auto along = longitudinal_offsets[longitudinal];
            const auto across = lateral_offsets[lateral];
            const auto point =
                rendering::layout::Point{start.x + unit_x * along + normal_x * across,
                                         start.y + unit_y * along + normal_y * across};
            const auto transformed = rendering::transform_point(point, state.transform);
            const auto coverage =
                std::min(longitudinal_coverage[longitudinal], lateral_coverage[lateral]);
            const auto covered_color = with_coverage(color_components, coverage);
            grid[longitudinal][lateral] = Vertex{transformed.x,
                                                 transformed.y,
                                                 covered_color[0],
                                                 covered_color[1],
                                                 covered_color[2],
                                                 covered_color[3],
                                                 0.0F,
                                                 0.0F};
        }
    }

    std::vector<Vertex> vertices;
    vertices.reserve(54U);
    for (std::size_t longitudinal = 0U; longitudinal + 1U < longitudinal_offsets.size();
         ++longitudinal) {
        for (std::size_t lateral = 0U; lateral + 1U < lateral_offsets.size(); ++lateral) {
            const auto top_left = grid[longitudinal][lateral];
            const auto top_right = grid[longitudinal + 1U][lateral];
            const auto bottom_right = grid[longitudinal + 1U][lateral + 1U];
            const auto bottom_left = grid[longitudinal][lateral + 1U];
            vertices.push_back(top_left);
            vertices.push_back(top_right);
            vertices.push_back(bottom_right);
            vertices.push_back(top_left);
            vertices.push_back(bottom_right);
            vertices.push_back(bottom_left);
        }
    }
    submit_vertices(vertices, nullptr);
}

void D3D11DisplayListRenderer::draw_ellipse(core::Rect rect, core::Color color) {
    if (!rendering::layout::is_visible_rect(rect)) {
        return;
    }
    const auto& state = recorder_state_.state_stack.back();
    const auto components = premultiplied_color(color, state.opacity);
    const auto transparent = with_coverage(components, 0.0F);
    const auto fringe = physical_pixel_size(dpi_);
    const auto center =
        rendering::layout::Point{rect.x + rect.width * 0.5F, rect.y + rect.height * 0.5F};
    const auto radius_x = rect.width * 0.5F;
    const auto radius_y = rect.height * 0.5F;
    if (radius_x <= geometry_epsilon || radius_y <= geometry_epsilon) {
        return;
    }
    const auto segments = adaptive_ellipse_segments(radius_x, radius_y);
    const auto& unit_points = cached_unit_circle_points(segments);

    auto& vertices = prepare_primitive_vertices(static_cast<std::size_t>(segments) * 9U);
    const auto append_vertex = [&](rendering::layout::Point point,
                                   const std::array<float, 4U>& vertex_color) {
        const auto transformed = rendering::transform_point(point, state.transform);
        vertices.push_back(Vertex{transformed.x, transformed.y, vertex_color[0], vertex_color[1],
                                  vertex_color[2], vertex_color[3], 0.0F, 0.0F});
    };
    const auto append_triangle =
        [&](rendering::layout::Point a, rendering::layout::Point b, rendering::layout::Point c,
            const std::array<float, 4U>& a_color, const std::array<float, 4U>& b_color,
            const std::array<float, 4U>& c_color) {
            append_vertex(a, a_color);
            append_vertex(b, b_color);
            append_vertex(c, c_color);
        };
    const auto append_quad =
        [&](rendering::layout::Point a, rendering::layout::Point b, rendering::layout::Point c,
            rendering::layout::Point d, const std::array<float, 4U>& a_color,
            const std::array<float, 4U>& b_color, const std::array<float, 4U>& c_color,
            const std::array<float, 4U>& d_color) {
            append_triangle(a, b, c, a_color, b_color, c_color);
            append_triangle(a, c, d, a_color, c_color, d_color);
        };
    const auto ellipse_point = [&](core::Point unit, float point_radius_x, float point_radius_y) {
        return rendering::layout::Point{center.x + unit.x * point_radius_x,
                                        center.y + unit.y * point_radius_y};
    };
    for (auto index = 0U; index < segments; ++index) {
        const auto unit = unit_points[index];
        const auto next_unit = unit_points[(index + 1U) % unit_points.size()];
        const auto edge = ellipse_point(unit, radius_x, radius_y);
        const auto next_edge = ellipse_point(next_unit, radius_x, radius_y);
        const auto outer = ellipse_point(unit, radius_x + fringe, radius_y + fringe);
        const auto next_outer = ellipse_point(next_unit, radius_x + fringe, radius_y + fringe);
        append_triangle(center, edge, next_edge, components, components, components);
        append_quad(edge, next_edge, next_outer, outer, components, components, transparent,
                    transparent);
    }
    submit_vertices(vertices, nullptr);
}

void D3D11DisplayListRenderer::append_batch_vertices(std::span<const Vertex> vertices) {
    if (vertices.empty()) {
        return;
    }

    const auto offset = recorder_state_.batch_vertices.size();
    recorder_state_.batch_vertices.resize(offset + vertices.size());
    std::memcpy(recorder_state_.batch_vertices.data() + offset, vertices.data(), vertices.size_bytes());
}

void D3D11DisplayListRenderer::draw_stroke_ellipse(core::Rect rect, core::Color color,
                                                   float stroke_width) {
    if (!rendering::layout::is_visible_rect(rect)) {
        return;
    }

    const auto width = std::max(stroke_width, physical_pixel_size(dpi_));
    const auto inner = inset_rect(rect, width);
    if (!rendering::layout::is_visible_rect(inner)) {
        draw_ellipse(rect, color);
        return;
    }

    const auto& state = recorder_state_.state_stack.back();
    const auto components = premultiplied_color(color, state.opacity);
    const auto transparent = with_coverage(components, 0.0F);
    const auto fringe = std::min(physical_pixel_size(dpi_), width);
    const auto center =
        rendering::layout::Point{rect.x + rect.width * 0.5F, rect.y + rect.height * 0.5F};
    const auto radius_x = rect.width * 0.5F;
    const auto radius_y = rect.height * 0.5F;
    const auto segments = adaptive_ellipse_segments(radius_x, radius_y);
    const auto& unit_points = cached_unit_circle_points(segments);

    const auto outer_aa_radius_x = radius_x + fringe;
    const auto outer_aa_radius_y = radius_y + fringe;
    const auto inner_radius_x = std::max(0.0F, radius_x - width);
    const auto inner_radius_y = std::max(0.0F, radius_y - width);
    const auto inner_aa_radius_x = std::max(0.0F, inner_radius_x - fringe);
    const auto inner_aa_radius_y = std::max(0.0F, inner_radius_y - fringe);

    auto& vertices = prepare_primitive_vertices(static_cast<std::size_t>(segments) * 18U);
    const auto append_vertex = [&](rendering::layout::Point point,
                                   const std::array<float, 4U>& vertex_color) {
        const auto transformed = rendering::transform_point(point, state.transform);
        vertices.push_back(Vertex{transformed.x, transformed.y, vertex_color[0], vertex_color[1],
                                  vertex_color[2], vertex_color[3], 0.0F, 0.0F});
    };
    const auto append_quad =
        [&](rendering::layout::Point a, rendering::layout::Point b, rendering::layout::Point c,
            rendering::layout::Point d, const std::array<float, 4U>& a_color,
            const std::array<float, 4U>& b_color, const std::array<float, 4U>& c_color,
            const std::array<float, 4U>& d_color) {
            append_vertex(a, a_color);
            append_vertex(b, b_color);
            append_vertex(c, c_color);
            append_vertex(a, a_color);
            append_vertex(c, c_color);
            append_vertex(d, d_color);
        };
    const auto ellipse_point = [&](core::Point unit, float radius_x_value, float radius_y_value) {
        return rendering::layout::Point{center.x + unit.x * radius_x_value,
                                        center.y + unit.y * radius_y_value};
    };

    for (auto index = 0U; index < segments; ++index) {
        const auto unit = unit_points[index];
        const auto next_unit = unit_points[(index + 1U) % unit_points.size()];
        append_quad(ellipse_point(unit, outer_aa_radius_x, outer_aa_radius_y),
                    ellipse_point(next_unit, outer_aa_radius_x, outer_aa_radius_y),
                    ellipse_point(next_unit, radius_x, radius_y),
                    ellipse_point(unit, radius_x, radius_y), transparent, transparent, components,
                    components);
        append_quad(ellipse_point(unit, radius_x, radius_y),
                    ellipse_point(next_unit, radius_x, radius_y),
                    ellipse_point(next_unit, inner_radius_x, inner_radius_y),
                    ellipse_point(unit, inner_radius_x, inner_radius_y), components, components,
                    components, components);
        append_quad(ellipse_point(unit, inner_radius_x, inner_radius_y),
                    ellipse_point(next_unit, inner_radius_x, inner_radius_y),
                    ellipse_point(next_unit, inner_aa_radius_x, inner_aa_radius_y),
                    ellipse_point(unit, inner_aa_radius_x, inner_aa_radius_y), components,
                    components, transparent, transparent);
    }
    submit_vertices(vertices, nullptr);
}

void D3D11DisplayListRenderer::fill_rounded_rect(core::Rect rect, core::CornerRadius radius,
                                                 core::Color color) {
    if (!rendering::layout::is_visible_rect(rect)) {
        return;
    }
    const auto clamped_radius = clamp_corner_radius(rect, radius);
    if (clamped_radius.x <= geometry_epsilon || clamped_radius.y <= geometry_epsilon) {
        draw_solid_rect(rect, color);
        return;
    }

    const auto& state = recorder_state_.state_stack.back();
    const auto components = premultiplied_color(color, state.opacity);
    const auto transparent = with_coverage(components, 0.0F);
    const auto fringe = physical_pixel_size(dpi_);
    const auto segments = adaptive_arc_segments(clamped_radius.x, clamped_radius.y, pi * 0.5F);
    const auto outer_aa = outset_rect(rect, fringe);
    const auto outer_aa_radius = clamp_corner_radius(
        outer_aa, rendering::CornerRadius{clamped_radius.x + fringe, clamped_radius.y + fringe});
    const auto points = rounded_rect_outline_from_cache(rect, clamped_radius, segments);
    const auto outer_points = rounded_rect_outline_from_cache(outer_aa, outer_aa_radius, segments);
    const auto center =
        rendering::layout::Point{rect.x + rect.width * 0.5F, rect.y + rect.height * 0.5F};

    auto& vertices = prepare_primitive_vertices(points.size() * 9U);
    const auto append_vertex = [&](rendering::layout::Point point,
                                   const std::array<float, 4U>& vertex_color) {
        const auto transformed = rendering::transform_point(point, state.transform);
        vertices.push_back(Vertex{transformed.x, transformed.y, vertex_color[0], vertex_color[1],
                                  vertex_color[2], vertex_color[3], 0.0F, 0.0F});
    };
    const auto append_triangle =
        [&](rendering::layout::Point a, rendering::layout::Point b, rendering::layout::Point c,
            const std::array<float, 4U>& a_color, const std::array<float, 4U>& b_color,
            const std::array<float, 4U>& c_color) {
            append_vertex(a, a_color);
            append_vertex(b, b_color);
            append_vertex(c, c_color);
        };
    const auto append_quad =
        [&](rendering::layout::Point a, rendering::layout::Point b, rendering::layout::Point c,
            rendering::layout::Point d, const std::array<float, 4U>& a_color,
            const std::array<float, 4U>& b_color, const std::array<float, 4U>& c_color,
            const std::array<float, 4U>& d_color) {
            append_triangle(a, b, c, a_color, b_color, c_color);
            append_triangle(a, c, d, a_color, c_color, d_color);
        };

    for (std::size_t index = 0U; index < points.size(); ++index) {
        const auto next_index = (index + 1U) % points.size();
        append_triangle(center, points[index], points[next_index], components, components,
                        components);
        append_quad(points[index], points[next_index], outer_points[next_index],
                    outer_points[index], components, components, transparent, transparent);
    }
    submit_vertices(vertices, nullptr);
}

void D3D11DisplayListRenderer::stroke_rounded_rect(core::Rect rect, core::CornerRadius radius,
                                                   core::Color color, float stroke_width) {
    if (!rendering::layout::is_visible_rect(rect)) {
        return;
    }

    const auto clamped_radius = clamp_corner_radius(rect, radius);
    if (clamped_radius.x <= geometry_epsilon || clamped_radius.y <= geometry_epsilon) {
        draw_stroke_rect(rect, color, stroke_width);
        return;
    }

    const auto width = std::max(stroke_width, physical_pixel_size(dpi_));
    const auto inner = inset_rect(rect, width);
    if (!rendering::layout::is_visible_rect(inner)) {
        fill_rounded_rect(rect, clamped_radius, color);
        return;
    }

    const auto& state = recorder_state_.state_stack.back();
    const auto components = premultiplied_color(color, state.opacity);
    const auto transparent = with_coverage(components, 0.0F);
    const auto fringe = std::min(physical_pixel_size(dpi_), width);
    const auto segments = adaptive_arc_segments(clamped_radius.x, clamped_radius.y, pi * 0.5F);
    const auto outer_aa = outset_rect(rect, fringe);
    const auto inner_aa = inset_rect(rect, width + fringe);
    const auto outer_aa_radius = clamp_corner_radius(
        outer_aa, rendering::CornerRadius{clamped_radius.x + fringe, clamped_radius.y + fringe});
    const auto inner_radius = inset_corner_radius(clamped_radius, width, inner);
    const auto inner_aa_radius = inset_corner_radius(clamped_radius, width + fringe, inner_aa);
    const auto outer_aa_points =
        rounded_rect_outline_from_cache(outer_aa, outer_aa_radius, segments);
    const auto outer_points = rounded_rect_outline_from_cache(rect, clamped_radius, segments);
    const auto inner_points = rounded_rect_outline_from_cache(inner, inner_radius, segments);
    const auto inner_aa_points =
        rounded_rect_outline_from_cache(inner_aa, inner_aa_radius, segments);

    auto& vertices = prepare_primitive_vertices(outer_points.size() * 18U);
    const auto append_vertex = [&](rendering::layout::Point point,
                                   const std::array<float, 4U>& vertex_color) {
        const auto transformed = rendering::transform_point(point, state.transform);
        vertices.push_back(Vertex{transformed.x, transformed.y, vertex_color[0], vertex_color[1],
                                  vertex_color[2], vertex_color[3], 0.0F, 0.0F});
    };
    const auto append_quad =
        [&](rendering::layout::Point a, rendering::layout::Point b, rendering::layout::Point c,
            rendering::layout::Point d, const std::array<float, 4U>& a_color,
            const std::array<float, 4U>& b_color, const std::array<float, 4U>& c_color,
            const std::array<float, 4U>& d_color) {
            append_vertex(a, a_color);
            append_vertex(b, b_color);
            append_vertex(c, c_color);
            append_vertex(a, a_color);
            append_vertex(c, c_color);
            append_vertex(d, d_color);
        };

    for (std::size_t index = 0U; index < outer_points.size(); ++index) {
        const auto next_index = (index + 1U) % outer_points.size();
        append_quad(outer_aa_points[index], outer_aa_points[next_index], outer_points[next_index],
                    outer_points[index], transparent, transparent, components, components);
        append_quad(outer_points[index], outer_points[next_index], inner_points[next_index],
                    inner_points[index], components, components, components, components);
        append_quad(inner_points[index], inner_points[next_index], inner_aa_points[next_index],
                    inner_aa_points[index], components, components, transparent, transparent);
    }
    submit_vertices(vertices, nullptr);
}

void D3D11DisplayListRenderer::fill_polygon(const std::vector<core::Point>& source_points,
                                            core::Color color) {
    if (source_points.size() < 3U) {
        return;
    }

    auto points = source_points;
    if (points.size() > 1U && points.front().x == points.back().x &&
        points.front().y == points.back().y) {
        points.pop_back();
    }
    if (points.size() < 3U) {
        return;
    }

    std::vector<Vertex> vertices;
    const auto& state = recorder_state_.state_stack.back();
    const auto components = premultiplied_color(color, state.opacity);
    const auto transparent = with_coverage(components, 0.0F);
    const auto fringe = physical_pixel_size(dpi_) * 0.5F;
    std::vector<rendering::layout::Point> transformed_points;
    transformed_points.reserve(points.size());
    for (const auto point : points) {
        transformed_points.push_back(rendering::transform_point(point, state.transform));
    }

    const auto triangles = triangulate_polygon(points);
    vertices.reserve(triangles.size() + points.size() * 6U);
    for (const auto point_index : triangles) {
        const auto point = transformed_points[point_index];
        vertices.push_back(Vertex{point.x, point.y, components[0], components[1], components[2],
                                  components[3], 0.0F, 0.0F});
    }

    const auto normal_sign = polygon_area(transformed_points) >= 0.0F ? -1.0F : 1.0F;
    std::vector<rendering::layout::Point> outer_points(transformed_points.size());
    for (std::size_t index = 0; index < transformed_points.size(); ++index) {
        const auto previous = transformed_points[(index + transformed_points.size() - 1U) %
                                                 transformed_points.size()];
        const auto current = transformed_points[index];
        const auto next = transformed_points[(index + 1U) % transformed_points.size()];
        const auto previous_direction = normalize_vector(point_subtract(current, previous));
        const auto next_direction = normalize_vector(point_subtract(next, current));
        if (vector_length(previous_direction) <= geometry_epsilon ||
            vector_length(next_direction) <= geometry_epsilon) {
            outer_points[index] = current;
            continue;
        }
        const auto previous_outward = point_scale(left_normal(previous_direction), normal_sign);
        const auto next_outward = point_scale(left_normal(next_direction), normal_sign);
        outer_points[index] =
            joined_offset_point(current, previous_direction, previous_outward, next_direction,
                                next_outward, fringe, fringe * 4.0F);
    }

    const auto append_vertex = [&](rendering::layout::Point point,
                                   const std::array<float, 4U>& vertex_color) {
        vertices.push_back(Vertex{point.x, point.y, vertex_color[0], vertex_color[1],
                                  vertex_color[2], vertex_color[3], 0.0F, 0.0F});
    };

    for (std::size_t index = 0; index < transformed_points.size(); ++index) {
        const auto start = transformed_points[index];
        const auto end = transformed_points[(index + 1U) % transformed_points.size()];
        const auto direction = normalize_vector(point_subtract(end, start));
        if (vector_length(direction) <= geometry_epsilon) {
            continue;
        }
        const auto outer_start = outer_points[index];
        const auto outer_end = outer_points[(index + 1U) % transformed_points.size()];

        append_vertex(start, components);
        append_vertex(end, components);
        append_vertex(outer_end, transparent);
        append_vertex(start, components);
        append_vertex(outer_end, transparent);
        append_vertex(outer_start, transparent);
    }
    submit_vertices(vertices, nullptr);
}

void D3D11DisplayListRenderer::stroke_polyline(const std::vector<core::Point>& source_points,
                                               core::Color color,
                                               const rendering::GeometryStrokeStyle& style,
                                               bool closed) {
    std::vector<rendering::layout::Point> points;
    points.reserve(source_points.size());
    for (const auto point : source_points) {
        if (!points.empty() && point_distance(points.back(), point) <= geometry_epsilon) {
            continue;
        }
        points.push_back(point);
    }
    if (closed && points.size() > 1U &&
        point_distance(points.front(), points.back()) <= geometry_epsilon) {
        points.pop_back();
    }
    if (points.size() < 2U || (closed && points.size() < 3U)) {
        return;
    }

    if (style.dash_style != rendering::StrokeDashStyle::Solid) {
        auto dash_pattern = std::vector<float>{};
        const auto unit = std::max(style.width, physical_pixel_size(dpi_));
        switch (style.dash_style) {
        case rendering::StrokeDashStyle::Dash:
            dash_pattern = {3.0F * unit, 3.0F * unit};
            break;
        case rendering::StrokeDashStyle::Dot:
            dash_pattern = {unit, 2.0F * unit};
            break;
        case rendering::StrokeDashStyle::DashDot:
            dash_pattern = {3.0F * unit, 2.0F * unit, unit, 2.0F * unit};
            break;
        case rendering::StrokeDashStyle::DashDotDot:
            dash_pattern = {3.0F * unit, 2.0F * unit, unit, 2.0F * unit, unit, 2.0F * unit};
            break;
        case rendering::StrokeDashStyle::Custom:
            for (const auto dash : style.dashes) {
                dash_pattern.push_back(std::max(dash * unit, geometry_epsilon));
            }
            break;
        case rendering::StrokeDashStyle::Solid:
        default:
            break;
        }
        if (dash_pattern.empty()) {
            return;
        }
        if ((dash_pattern.size() % 2U) != 0U) {
            const auto original_size = dash_pattern.size();
            dash_pattern.reserve(original_size * 2U);
            for (std::size_t index = 0U; index < original_size; ++index) {
                dash_pattern.push_back(dash_pattern[index]);
            }
        }

        auto solid_style = style;
        solid_style.dash_style = rendering::StrokeDashStyle::Solid;
        solid_style.dashes.clear();
        solid_style.start_cap = style.dash_cap;
        solid_style.end_cap = style.dash_cap;

        auto pattern_length = 0.0F;
        for (const auto dash : dash_pattern) {
            pattern_length += dash;
        }
        auto pattern_index = std::size_t{0U};
        auto pattern_offset = std::fmod(std::max(style.dash_offset, 0.0F), pattern_length);
        while (pattern_offset >= dash_pattern[pattern_index] - geometry_epsilon) {
            pattern_offset -= dash_pattern[pattern_index];
            pattern_index = (pattern_index + 1U) % dash_pattern.size();
        }
        auto pattern_remaining = dash_pattern[pattern_index] - pattern_offset;
        auto drawing = (pattern_index % 2U) == 0U;
        auto active_dash = std::vector<rendering::layout::Point>{};
        const auto flush_dash = [&]() {
            if (active_dash.size() >= 2U) {
                stroke_polyline(active_dash, color, solid_style, false);
            }
            active_dash.clear();
        };
        const auto append_dash_point = [&](rendering::layout::Point point) {
            if (active_dash.empty() ||
                point_distance(active_dash.back(), point) > geometry_epsilon) {
                active_dash.push_back(point);
            }
        };

        const auto segment_count = closed ? points.size() : points.size() - 1U;
        for (std::size_t segment_index = 0U; segment_index < segment_count; ++segment_index) {
            const auto start = points[segment_index];
            const auto end = points[(segment_index + 1U) % points.size()];
            const auto delta = point_subtract(end, start);
            const auto length = vector_length(delta);
            if (length <= geometry_epsilon) {
                continue;
            }
            const auto direction = point_scale(delta, 1.0F / length);
            auto consumed = 0.0F;
            while (consumed < length - geometry_epsilon) {
                const auto step = std::min(pattern_remaining, length - consumed);
                const auto dash_start = point_add(start, point_scale(direction, consumed));
                const auto dash_end = point_add(start, point_scale(direction, consumed + step));
                if (drawing) {
                    append_dash_point(dash_start);
                    append_dash_point(dash_end);
                }
                consumed += step;
                pattern_remaining -= step;
                if (pattern_remaining <= geometry_epsilon) {
                    if (drawing) {
                        flush_dash();
                    }
                    pattern_index = (pattern_index + 1U) % dash_pattern.size();
                    pattern_remaining = dash_pattern[pattern_index];
                    drawing = (pattern_index % 2U) == 0U;
                }
            }
        }
        flush_dash();
        return;
    }

    struct StrokeSegment {
        rendering::layout::Point start{};
        rendering::layout::Point end{};
        rendering::layout::Point direction{};
        rendering::layout::Point normal{};
        float length = 0.0F;
    };

    std::vector<StrokeSegment> segments;
    const auto segment_count = closed ? points.size() : points.size() - 1U;
    segments.reserve(segment_count);
    for (std::size_t index = 0U; index < segment_count; ++index) {
        const auto start = points[index];
        const auto end = points[(index + 1U) % points.size()];
        const auto delta = point_subtract(end, start);
        const auto length = vector_length(delta);
        if (length <= geometry_epsilon) {
            continue;
        }
        const auto direction = point_scale(delta, 1.0F / length);
        segments.push_back(StrokeSegment{.start = start,
                                         .end = end,
                                         .direction = direction,
                                         .normal = left_normal(direction),
                                         .length = length});
    }
    if (segments.empty()) {
        return;
    }

    const auto& state = recorder_state_.state_stack.back();
    const auto components = premultiplied_color(color, state.opacity);
    const auto transparent = with_coverage(components, 0.0F);
    const auto half_width = std::max(style.width, physical_pixel_size(dpi_)) * 0.5F;
    const auto fringe = physical_pixel_size(dpi_) * 0.5F;
    const auto solid_half_width = std::max(0.0F, half_width - fringe);
    const auto outer_half_width = half_width + fringe;
    const auto round_disk_segments =
        adaptive_curve_segments(outer_half_width * pi * 2.0F, 12U, 64U);
    const auto miter_limit = std::max(style.miter_limit, 1.0F) * half_width;

    std::vector<Vertex> vertices;

    const auto append_vertex = [&](rendering::layout::Point point,
                                   const std::array<float, 4U>& vertex_color) {
        const auto transformed = rendering::transform_point(point, state.transform);
        vertices.push_back(Vertex{transformed.x, transformed.y, vertex_color[0], vertex_color[1],
                                  vertex_color[2], vertex_color[3], 0.0F, 0.0F});
    };
    const auto append_triangle =
        [&](rendering::layout::Point a, rendering::layout::Point b, rendering::layout::Point c,
            const std::array<float, 4U>& a_color, const std::array<float, 4U>& b_color,
            const std::array<float, 4U>& c_color) {
            append_vertex(a, a_color);
            append_vertex(b, b_color);
            append_vertex(c, c_color);
        };
    const auto append_solid_triangle = [&](rendering::layout::Point a, rendering::layout::Point b,
                                           rendering::layout::Point c) {
        append_triangle(a, b, c, components, components, components);
    };
    const auto append_quad =
        [&](rendering::layout::Point a, rendering::layout::Point b, rendering::layout::Point c,
            rendering::layout::Point d, const std::array<float, 4U>& a_color,
            const std::array<float, 4U>& b_color, const std::array<float, 4U>& c_color,
            const std::array<float, 4U>& d_color) {
            append_triangle(a, b, c, a_color, b_color, c_color);
            append_triangle(a, c, d, a_color, c_color, d_color);
        };
    const auto append_solid_quad = [&](rendering::layout::Point a, rendering::layout::Point b,
                                       rendering::layout::Point c, rendering::layout::Point d) {
        append_quad(a, b, c, d, components, components, components, components);
    };
    const auto append_strip = [&](rendering::layout::Point start, rendering::layout::Point end,
                                  rendering::layout::Point normal) {
        const std::array<float, 4U> offsets{
            -outer_half_width,
            -solid_half_width,
            solid_half_width,
            outer_half_width,
        };
        const std::array<const std::array<float, 4U>*, 4U> colors{&transparent, &components,
                                                                  &components, &transparent};
        std::array<rendering::layout::Point, 4U> start_points{};
        std::array<rendering::layout::Point, 4U> end_points{};
        for (std::size_t index = 0U; index < offsets.size(); ++index) {
            start_points[index] = point_add(start, point_scale(normal, offsets[index]));
            end_points[index] = point_add(end, point_scale(normal, offsets[index]));
        }
        for (std::size_t index = 0U; index + 1U < offsets.size(); ++index) {
            append_quad(start_points[index], end_points[index], end_points[index + 1U],
                        start_points[index + 1U], *colors[index], *colors[index],
                        *colors[index + 1U], *colors[index + 1U]);
        }
    };
    const auto append_join_cover_disk = [&](rendering::layout::Point center) {
        if (half_width <= fringe + geometry_epsilon) {
            return;
        }
        const auto steps = round_disk_segments;
        const auto point_at = [&](float angle) {
            return rendering::layout::Point{center.x + std::cos(angle) * half_width,
                                            center.y + std::sin(angle) * half_width};
        };
        for (auto step = 0U; step < steps; ++step) {
            const auto angle = static_cast<float>(step) / static_cast<float>(steps) * pi * 2.0F;
            const auto next_angle =
                static_cast<float>(step + 1U) / static_cast<float>(steps) * pi * 2.0F;
            append_solid_triangle(center, point_at(angle), point_at(next_angle));
        }
    };
    const auto append_round_disk = [&](rendering::layout::Point center) {
        const auto steps = round_disk_segments;
        const auto point_at = [&](float angle, float radius) {
            return rendering::layout::Point{center.x + std::cos(angle) * radius,
                                            center.y + std::sin(angle) * radius};
        };
        for (auto step = 0U; step < steps; ++step) {
            const auto angle = static_cast<float>(step) / static_cast<float>(steps) * pi * 2.0F;
            const auto next_angle =
                static_cast<float>(step + 1U) / static_cast<float>(steps) * pi * 2.0F;
            const auto edge = point_at(angle, solid_half_width);
            const auto next_edge = point_at(next_angle, solid_half_width);
            const auto outer = point_at(angle, outer_half_width);
            const auto next_outer = point_at(next_angle, outer_half_width);
            append_solid_triangle(center, edge, next_edge);
            append_quad(edge, next_edge, next_outer, outer, components, components, transparent,
                        transparent);
        }
    };
    const auto append_round_join = [&](rendering::layout::Point point,
                                       rendering::layout::Point previous_normal,
                                       rendering::layout::Point next_normal, float side) {
        const auto previous_outer = point_scale(previous_normal, side);
        const auto next_outer = point_scale(next_normal, side);
        auto start_angle = std::atan2(previous_outer.y, previous_outer.x);
        auto end_angle = std::atan2(next_outer.y, next_outer.x);
        if (side < 0.0F) {
            if (end_angle < start_angle) {
                end_angle += pi * 2.0F;
            }
        } else if (end_angle > start_angle) {
            end_angle -= pi * 2.0F;
        }

        const auto sweep = end_angle - start_angle;
        if (std::abs(sweep) <= geometry_epsilon) {
            return;
        }
        const auto steps = std::clamp(
            static_cast<std::uint32_t>(std::ceil(std::abs(sweep) * outer_half_width / 0.75F)), 4U,
            round_disk_segments);
        const auto point_at = [&](float angle, float radius) {
            return rendering::layout::Point{point.x + std::cos(angle) * radius,
                                            point.y + std::sin(angle) * radius};
        };
        for (auto step = 0U; step < steps; ++step) {
            const auto angle =
                start_angle + sweep * static_cast<float>(step) / static_cast<float>(steps);
            const auto next_angle =
                start_angle + sweep * static_cast<float>(step + 1U) / static_cast<float>(steps);
            const auto edge = point_at(angle, solid_half_width);
            const auto next_edge = point_at(next_angle, solid_half_width);
            const auto outer = point_at(angle, outer_half_width);
            const auto next_outer_edge = point_at(next_angle, outer_half_width);
            append_solid_triangle(point, edge, next_edge);
            append_quad(edge, next_edge, next_outer_edge, outer, components, components,
                        transparent, transparent);
        }
    };
    const auto append_cap = [&](rendering::layout::Point point, rendering::layout::Point direction,
                                rendering::layout::Point normal, rendering::StrokeLineCap cap,
                                bool start_cap) {
        const auto outward = start_cap ? point_scale(direction, -1.0F) : direction;
        const auto edge_center = cap == rendering::StrokeLineCap::Square
                                     ? point_add(point, point_scale(outward, half_width))
                                     : point;
        if (cap == rendering::StrokeLineCap::Round) {
            append_round_disk(point);
            return;
        }
        if (cap == rendering::StrokeLineCap::Triangle) {
            const auto left = point_add(point, point_scale(normal, solid_half_width));
            const auto right = point_add(point, point_scale(normal, -solid_half_width));
            const auto tip = point_add(point, point_scale(outward, half_width));
            const auto outer_tip = point_add(tip, point_scale(outward, fringe));
            append_solid_triangle(left, tip, right);
            append_triangle(tip, outer_tip, right, components, transparent, components);
            append_triangle(left, outer_tip, tip, components, transparent, components);
            return;
        }

        const auto left = point_add(edge_center, point_scale(normal, solid_half_width));
        const auto right = point_add(edge_center, point_scale(normal, -solid_half_width));
        const auto outer_left = point_add(left, point_scale(outward, fringe));
        const auto outer_right = point_add(right, point_scale(outward, fringe));
        append_quad(left, outer_left, outer_right, right, components, transparent, transparent,
                    components);
    };
    const auto append_bevel_join = [&](rendering::layout::Point point,
                                       rendering::layout::Point previous_normal,
                                       rendering::layout::Point next_normal, float side) {
        const auto previous_outer =
            point_add(point, point_scale(previous_normal, side * solid_half_width));
        const auto next_outer = point_add(point, point_scale(next_normal, side * solid_half_width));
        const auto previous_outer_aa =
            point_add(point, point_scale(previous_normal, side * outer_half_width));
        const auto next_outer_aa =
            point_add(point, point_scale(next_normal, side * outer_half_width));
        append_solid_triangle(point, previous_outer, next_outer);
        append_quad(previous_outer, previous_outer_aa, next_outer_aa, next_outer, components,
                    transparent, transparent, components);
    };
    const auto append_miter_join = [&](rendering::layout::Point point,
                                       const StrokeSegment& previous, const StrokeSegment& next,
                                       float side, float limit) {
        const auto previous_outer =
            point_add(point, point_scale(previous.normal, side * solid_half_width));
        const auto next_outer = point_add(point, point_scale(next.normal, side * solid_half_width));
        auto miter = rendering::layout::Point{};
        if (!line_intersection(previous_outer, previous.direction, next_outer, next.direction,
                               miter) ||
            point_distance(point, miter) > limit) {
            return false;
        }

        append_solid_triangle(previous_outer, miter, next_outer);
        auto previous_aa = point_add(point, point_scale(previous.normal, side * outer_half_width));
        auto next_aa = point_add(point, point_scale(next.normal, side * outer_half_width));
        auto miter_aa = miter;
        const auto aa_limit = std::max(limit + fringe * 2.0F, outer_half_width * 2.0F);
        if (line_intersection(previous_aa, previous.direction, next_aa, next.direction, miter_aa) &&
            point_distance(point, miter_aa) <= aa_limit) {
            append_triangle(previous_outer, previous_aa, miter_aa, components, transparent,
                            transparent);
            append_triangle(previous_outer, miter_aa, miter, components, transparent, components);
            append_triangle(miter, miter_aa, next_outer, components, transparent, components);
            append_triangle(next_outer, miter_aa, next_aa, components, transparent, transparent);
        }
        return true;
    };
    const auto append_join = [&](std::size_t point_index, const StrokeSegment& previous,
                                 const StrokeSegment& next) {
        const auto turn = vector_cross(previous.direction, next.direction);
        if (std::abs(turn) <= geometry_epsilon) {
            return;
        }
        const auto side = turn > 0.0F ? -1.0F : 1.0F;
        const auto point = points[point_index];
        const auto direction_dot =
            std::clamp(vector_dot(previous.direction, next.direction), -1.0F, 1.0F);
        if (direction_dot >= smooth_stroke_join_cosine &&
            append_miter_join(point, previous, next, side, half_width * 8.0F)) {
            return;
        }
        if (style.line_join == rendering::StrokeLineJoin::Round) {
            append_round_join(point, previous.normal, next.normal, side);
            return;
        }
        if (style.line_join == rendering::StrokeLineJoin::Miter) {
            if (append_miter_join(point, previous, next, side, miter_limit)) {
                return;
            }
        }
        append_bevel_join(point, previous.normal, next.normal, side);
    };

    const auto can_use_continuous_mesh = [&]() noexcept {
        if (style.line_join == rendering::StrokeLineJoin::Bevel) {
            return false;
        }
        if (segments.size() != segment_count || points.size() < 3U) {
            return false;
        }
        if (!closed && (style.start_cap == rendering::StrokeLineCap::Square ||
                        style.start_cap == rendering::StrokeLineCap::Triangle ||
                        style.end_cap == rendering::StrokeLineCap::Square ||
                        style.end_cap == rendering::StrokeLineCap::Triangle)) {
            return false;
        }
        const auto join_can_use_continuous_mesh = [&](std::size_t point_index,
                                                      const StrokeSegment& previous,
                                                      const StrokeSegment& next) noexcept {
            const auto direction_dot =
                std::clamp(vector_dot(previous.direction, next.direction), -1.0F, 1.0F);
            if (style.line_join != rendering::StrokeLineJoin::Miter) {
                return direction_dot >= smooth_stroke_join_cosine;
            }

            const auto turn = vector_cross(previous.direction, next.direction);
            if (std::abs(turn) <= geometry_epsilon) {
                return true;
            }
            const auto side = turn > 0.0F ? -1.0F : 1.0F;
            const auto point = points[point_index];
            const auto previous_outer =
                point_add(point, point_scale(previous.normal, side * solid_half_width));
            const auto next_outer =
                point_add(point, point_scale(next.normal, side * solid_half_width));
            auto miter = rendering::layout::Point{};
            return line_intersection(previous_outer, previous.direction, next_outer, next.direction,
                                     miter) &&
                   point_distance(point, miter) <= miter_limit;
        };
        if (closed) {
            for (std::size_t index = 0U; index < segments.size(); ++index) {
                const auto& previous = segments[(index + segments.size() - 1U) % segments.size()];
                const auto& next = segments[index];
                if (!join_can_use_continuous_mesh(index, previous, next)) {
                    return false;
                }
            }
            return true;
        }
        for (std::size_t index = 1U; index < segments.size(); ++index) {
            if (!join_can_use_continuous_mesh(index, segments[index - 1U], segments[index])) {
                return false;
            }
        }
        return true;
    };

    const auto append_continuous_stroke_mesh = [&]() {
        struct StrokeRingPoint {
            rendering::layout::Point outer_left{};
            rendering::layout::Point left{};
            rendering::layout::Point right{};
            rendering::layout::Point outer_right{};
        };

        std::vector<StrokeRingPoint> ring(points.size());
        const auto offset_limit = std::max(miter_limit + fringe * 4.0F, outer_half_width * 4.0F);
        const auto make_endpoint = [&](rendering::layout::Point point,
                                       const StrokeSegment& segment) {
            return StrokeRingPoint{
                .outer_left = point_add(point, point_scale(segment.normal, outer_half_width)),
                .left = point_add(point, point_scale(segment.normal, solid_half_width)),
                .right = point_add(point, point_scale(segment.normal, -solid_half_width)),
                .outer_right = point_add(point, point_scale(segment.normal, -outer_half_width))};
        };
        const auto make_join = [&](rendering::layout::Point point, const StrokeSegment& previous,
                                   const StrokeSegment& next) {
            return StrokeRingPoint{
                .outer_left =
                    joined_offset_point(point, previous.direction, previous.normal, next.direction,
                                        next.normal, outer_half_width, offset_limit),
                .left =
                    joined_offset_point(point, previous.direction, previous.normal, next.direction,
                                        next.normal, solid_half_width, miter_limit),
                .right = joined_offset_point(
                    point, previous.direction, point_scale(previous.normal, -1.0F), next.direction,
                    point_scale(next.normal, -1.0F), solid_half_width, miter_limit),
                .outer_right = joined_offset_point(
                    point, previous.direction, point_scale(previous.normal, -1.0F), next.direction,
                    point_scale(next.normal, -1.0F), outer_half_width, offset_limit)};
        };

        if (closed) {
            for (std::size_t index = 0U; index < points.size(); ++index) {
                ring[index] = make_join(points[index],
                                        segments[(index + segments.size() - 1U) % segments.size()],
                                        segments[index % segments.size()]);
            }
        } else {
            ring.front() = make_endpoint(points.front(), segments.front());
            ring.back() = make_endpoint(points.back(), segments.back());
            for (std::size_t index = 1U; index + 1U < points.size(); ++index) {
                ring[index] = make_join(points[index], segments[index - 1U], segments[index]);
            }
        }

        const auto append_ring_segment = [&](std::size_t index, std::size_t next_index) {
            const auto& start = ring[index];
            const auto& end = ring[next_index];
            append_quad(start.outer_left, end.outer_left, end.left, start.left, transparent,
                        transparent, components, components);
            append_solid_quad(start.left, end.left, end.right, start.right);
            append_quad(start.right, end.right, end.outer_right, start.outer_right, components,
                        components, transparent, transparent);
        };

        const auto mesh_segment_count = closed ? points.size() : points.size() - 1U;
        for (std::size_t index = 0U; index < mesh_segment_count; ++index) {
            append_ring_segment(index, (index + 1U) % points.size());
        }
        if (style.line_join == rendering::StrokeLineJoin::Round) {
            const auto append_cover_if_needed =
                [&](std::size_t index, const StrokeSegment& previous, const StrokeSegment& next) {
                    const auto direction_dot =
                        std::clamp(vector_dot(previous.direction, next.direction), -1.0F, 1.0F);
                    if (direction_dot < continuous_join_cover_cosine) {
                        append_join_cover_disk(points[index]);
                    }
                };
            if (closed) {
                for (std::size_t index = 0U; index < points.size(); ++index) {
                    append_cover_if_needed(
                        index, segments[(index + segments.size() - 1U) % segments.size()],
                        segments[index % segments.size()]);
                }
            } else {
                for (std::size_t index = 1U; index + 1U < points.size(); ++index) {
                    append_cover_if_needed(index, segments[index - 1U], segments[index]);
                }
            }
        }
        if (!closed) {
            append_cap(points.front(), segments.front().direction, segments.front().normal,
                       style.start_cap, true);
            append_cap(points.back(), segments.back().direction, segments.back().normal,
                       style.end_cap, false);
        }
    };

    if (can_use_continuous_mesh()) {
        vertices.reserve(segments.size() * 18U + points.size() * round_disk_segments * 9U +
                         (closed ? 0U : 160U));
        append_continuous_stroke_mesh();
        submit_vertices(vertices, nullptr);
        return;
    }

    vertices.reserve(segments.size() * 18U + points.size() * round_disk_segments * 9U);

    for (std::size_t index = 0U; index < segments.size(); ++index) {
        const auto& segment = segments[index];
        auto start = segment.start;
        auto end = segment.end;
        if (!closed && index == 0U && style.start_cap == rendering::StrokeLineCap::Square) {
            start = point_add(start, point_scale(segment.direction, -half_width));
        }
        if (!closed && index + 1U == segments.size() &&
            style.end_cap == rendering::StrokeLineCap::Square) {
            end = point_add(end, point_scale(segment.direction, half_width));
        }
        append_strip(start, end, segment.normal);
    }

    if (closed) {
        for (std::size_t index = 0U; index < points.size(); ++index) {
            append_join(index, segments[(index + segments.size() - 1U) % segments.size()],
                        segments[index % segments.size()]);
        }
    } else {
        for (std::size_t index = 1U; index + 1U < points.size(); ++index) {
            append_join(index, segments[index - 1U], segments[index]);
        }
        append_cap(points.front(), segments.front().direction, segments.front().normal,
                   style.start_cap, true);
        append_cap(points.back(), segments.back().direction, segments.back().normal, style.end_cap,
                   false);
    }

    submit_vertices(vertices, nullptr);
}

void D3D11DisplayListRenderer::fill_geometry(const rendering::Geometry& geometry,
                                             core::Color color) {
    auto prepared = rendering::PreparedGeometryFill{};
    auto flatten = std::make_shared<rendering::PreparedGeometryFlatten>();
    flatten->figures = flatten_geometry(geometry);
    flatten->bounds = geometry_bounds(geometry);
    prepared.flatten = std::move(flatten);
    prepared.filled_contours = flatten_filled_contours(geometry);
    prepared.tessellated_vertices =
        tessellate_geometry_fill(prepared.filled_contours, geometry.fill_rule);
    prepared.bounds =
        prepared.flatten != nullptr ? prepared.flatten->bounds : rendering::layout::Rect{};
    draw_prepared_geometry_fill(prepared, geometry.fill_rule, color);
}

void D3D11DisplayListRenderer::fill_geometry(const rendering::FillGeometryCommand& command) {
    if (has_prepared_geometry_fill(command.prepared_fill.get())) {
        draw_prepared_geometry_fill(*command.prepared_fill, command.geometry.fill_rule,
                                    command.color);
        return;
    }
    fill_geometry(command.geometry, command.color);
}

void D3D11DisplayListRenderer::draw_prepared_geometry_fill(
    const rendering::PreparedGeometryFill& prepared_fill, rendering::GeometryFillRule fill_rule,
    core::Color color) {
    if (!prepared_fill.tessellated_vertices.empty()) {
        const auto& state = recorder_state_.state_stack.back();
        const auto components = premultiplied_color(color, state.opacity);
        const auto transparent = with_coverage(components, 0.0F);
        // Geometry fill AA is emitted outside the solid contour. Use half a physical pixel for
        // crispness, especially avoiding fringe overlap that closes small SVG icon counters.
        const auto fringe = physical_pixel_size(dpi_) * 0.5F;
        const auto fringe_edge = with_coverage(components, 0.55F);
        auto& transformed = transformed_geometry_fill_for(prepared_fill, state.transform);
        const auto& transformed_geometry_contours = transformed.filled_contours;
        auto& vertices = prepare_primitive_vertices(transformed.tessellated_vertices.size() +
                                                    transformed.fringe_quads.size() * 6U);
        const auto append_transformed_vertex = [&](rendering::layout::Point point,
                                                   const std::array<float, 4U>& vertex_color) {
            vertices.push_back(Vertex{point.x, point.y, vertex_color[0], vertex_color[1],
                                      vertex_color[2], vertex_color[3], 0.0F, 0.0F});
        };
        const auto append_quad =
            [&](rendering::layout::Point a, rendering::layout::Point b, rendering::layout::Point c,
                rendering::layout::Point d, const std::array<float, 4U>& a_color,
                const std::array<float, 4U>& b_color, const std::array<float, 4U>& c_color,
                const std::array<float, 4U>& d_color) {
                append_transformed_vertex(a, a_color);
                append_transformed_vertex(b, b_color);
                append_transformed_vertex(c, c_color);
                append_transformed_vertex(a, a_color);
                append_transformed_vertex(c, c_color);
                append_transformed_vertex(d, d_color);
            };
        for (const auto point : transformed.tessellated_vertices) {
            append_transformed_vertex(point, components);
        }

        if (!transformed.fringe_valid || transformed.fringe_fill_rule != fill_rule ||
            transformed.fringe_width != fringe) {
            transformed.fringe_contours.clear();
            transformed.fringe_contours.resize(transformed_geometry_contours.size());
            transformed.fringe_quads.clear();
            for (std::size_t contour_index = 0U;
                 contour_index < transformed_geometry_contours.size(); ++contour_index) {
                const auto& contour = transformed_geometry_contours[contour_index];
                auto& cached_contour = transformed.fringe_contours[contour_index];
                auto& edges = cached_contour.edges;
                auto& outer_points = cached_contour.outer_points;
                auto& outer_point_valid = cached_contour.outer_point_valid;
                edges.clear();
                outer_points.clear();
                outer_point_valid.clear();
                edges.resize(contour.size());
                for (std::size_t index = 0U; index < contour.size(); ++index) {
                    const auto start = contour[index];
                    const auto end = contour[(index + 1U) % contour.size()];
                    const auto direction = normalize_vector(point_subtract(end, start));
                    if (vector_length(direction) <= geometry_epsilon) {
                        continue;
                    }
                    const auto normal = left_normal(direction);
                    const auto midpoint = point_scale(point_add(start, end), 0.5F);
                    const auto sample_distance = std::max(fringe * 0.25F, geometry_epsilon * 4.0F);
                    const auto left_inside = point_inside_fill(
                        transformed_geometry_contours, fill_rule,
                        point_add(midpoint, point_scale(normal, sample_distance)));
                    const auto right_inside = point_inside_fill(
                        transformed_geometry_contours, fill_rule,
                        point_add(midpoint, point_scale(normal, -sample_distance)));
                    if (left_inside == right_inside) {
                        continue;
                    }
                    edges[index] = GeometryBoundaryEdge{
                        .start = start,
                        .end = end,
                        .direction = direction,
                        .outward = point_scale(normal, left_inside ? -1.0F : 1.0F),
                        .visible = true};
                }

                outer_points.resize(contour.size());
                outer_point_valid.assign(contour.size(), std::uint8_t{0U});
                for (std::size_t index = 0U; index < contour.size(); ++index) {
                    const auto& previous_edge = edges[(index + edges.size() - 1U) % edges.size()];
                    const auto& next_edge = edges[index];
                    const auto point = contour[index];
                    if (previous_edge.visible && next_edge.visible) {
                        outer_points[index] = joined_offset_point(
                            point, previous_edge.direction, previous_edge.outward,
                            next_edge.direction, next_edge.outward, fringe, fringe * 4.0F);
                        outer_point_valid[index] = 1U;
                    } else if (previous_edge.visible) {
                        outer_points[index] =
                            point_add(point, point_scale(previous_edge.outward, fringe));
                        outer_point_valid[index] = 1U;
                    } else if (next_edge.visible) {
                        outer_points[index] =
                            point_add(point, point_scale(next_edge.outward, fringe));
                        outer_point_valid[index] = 1U;
                    }
                }

                transformed.fringe_quads.reserve(transformed.fringe_quads.size() + edges.size());
                for (std::size_t index = 0U; index < edges.size(); ++index) {
                    const auto next_index = (index + 1U) % edges.size();
                    if (!edges[index].visible || outer_point_valid[index] == 0U ||
                        outer_point_valid[next_index] == 0U) {
                        continue;
                    }
                    transformed.fringe_quads.push_back(
                        GeometryFringeQuad{.a = edges[index].start,
                                           .b = edges[index].end,
                                           .c = outer_points[next_index],
                                           .d = outer_points[index]});
                }
            }
            transformed.fringe_fill_rule = fill_rule;
            transformed.fringe_width = fringe;
            transformed.fringe_valid = true;
        }

        for (const auto& quad : transformed.fringe_quads) {
            append_quad(quad.a, quad.b, quad.c, quad.d, fringe_edge, fringe_edge, transparent,
                        transparent);
        }
        submit_vertices(vertices, nullptr);
        return;
    }

    for (const auto& figure : prepared_geometry_figures(prepared_fill)) {
        fill_polygon(figure, color);
    }
}

void D3D11DisplayListRenderer::stroke_geometry(const rendering::Geometry& geometry,
                                               core::Color color,
                                               const rendering::GeometryStrokeStyle& style) {
    for (const auto& figure : flatten_geometry(geometry)) {
        const auto closed = figure.size() > 2U && figure.front().x == figure.back().x &&
                            figure.front().y == figure.back().y;
        stroke_polyline(figure, color, style, closed);
    }
}

void D3D11DisplayListRenderer::stroke_geometry(const rendering::StrokeGeometryCommand& command) {
    if (command.prepared_stroke == nullptr ||
        prepared_geometry_figures(*command.prepared_stroke).empty()) {
        stroke_geometry(command.geometry, command.color, command.style);
        return;
    }

    for (const auto& figure : prepared_geometry_figures(*command.prepared_stroke)) {
        const auto closed = figure.size() > 2U && figure.front().x == figure.back().x &&
                            figure.front().y == figure.back().y;
        stroke_polyline(figure, command.color, command.style, closed);
    }
}

void D3D11DisplayListRenderer::push_geometry_clip(const rendering::Geometry& geometry) {
    if (recorder_state_.state_stack.empty()) {
        return;
    }

    flush_batch();
    const auto& current = recorder_state_.state_stack.back();
    if (current.stencil_depth == 0xFFU || !activate_stencil_target()) {
        push_device_clip(rendering::transform_rect(geometry_bounds(geometry), current.transform));
        return;
    }

    render_stencil_clip(geometry, current.stencil_depth, StencilUpdateOp::Increment);

    auto state = current;
    state.stencil_depth = static_cast<std::uint8_t>(current.stencil_depth + 1U);
    state.kind = StateKind::GeometryClip;
    state.geometry_clip = std::make_shared<rendering::Geometry>(geometry);
    state.prepared_geometry_clip.reset();
    const auto transformed_bounds =
        rendering::transform_rect(geometry_bounds(geometry), current.transform);
    state.clip =
        intersect_scissor(state.clip, to_scissor(transformed_bounds, dpi_, target_pixel_width_,
                                                 target_pixel_height_));
    recorder_state_.state_stack.push_back(std::move(state));
    apply_current_scissor();
}

void D3D11DisplayListRenderer::push_geometry_clip(
    const rendering::PushGeometryClipCommand& command) {
    if (recorder_state_.state_stack.empty()) {
        return;
    }

    flush_batch();
    const auto& current = recorder_state_.state_stack.back();
    const auto* prepared_fill = command.prepared_fill.get();
    const auto bounds =
        prepared_fill != nullptr && rendering::layout::is_visible_rect(prepared_fill->bounds)
            ? prepared_fill->bounds
            : geometry_bounds(command.geometry);
    if (current.stencil_depth == 0xFFU || !activate_stencil_target()) {
        push_device_clip(rendering::transform_rect(bounds, current.transform));
        return;
    }

    if (prepared_fill != nullptr && !prepared_fill->tessellated_vertices.empty()) {
        render_stencil_clip(*prepared_fill, current.stencil_depth, StencilUpdateOp::Increment);
    } else {
        render_stencil_clip(command.geometry, current.stencil_depth, StencilUpdateOp::Increment);
    }

    auto state = current;
    state.stencil_depth = static_cast<std::uint8_t>(current.stencil_depth + 1U);
    state.kind = StateKind::GeometryClip;
    state.geometry_clip = std::make_shared<rendering::Geometry>(command.geometry);
    state.prepared_geometry_clip = command.prepared_fill;
    const auto transformed_bounds = rendering::transform_rect(bounds, current.transform);
    state.clip =
        intersect_scissor(state.clip, to_scissor(transformed_bounds, dpi_, target_pixel_width_,
                                                 target_pixel_height_));
    recorder_state_.state_stack.push_back(std::move(state));
    apply_current_scissor();
}

void D3D11DisplayListRenderer::render_stencil_clip(const rendering::Geometry& geometry,
                                                   std::uint8_t reference, StencilUpdateOp op) {
    auto prepared = rendering::PreparedGeometryFill{};
    prepared.filled_contours = flatten_filled_contours(geometry);
    prepared.tessellated_vertices =
        tessellate_geometry_fill(prepared.filled_contours, geometry.fill_rule);
    render_stencil_clip(prepared, reference, op);
}

void D3D11DisplayListRenderer::render_stencil_clip(
    const rendering::PreparedGeometryFill& prepared_fill, std::uint8_t reference,
    StencilUpdateOp op) {
    if (context_ == nullptr || recorder_state_.state_stack.empty() || !activate_stencil_target()) {
        return;
    }

    flush_batch();
    const auto& state = recorder_state_.state_stack.back();
    std::vector<Vertex> vertices;
    vertices.reserve(prepared_fill.tessellated_vertices.size());
    for (const auto point : prepared_fill.tessellated_vertices) {
        const auto transformed = rendering::transform_point(point, state.transform);
        vertices.push_back(
            Vertex{transformed.x, transformed.y, 1.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F});
    }
    if (vertices.empty()) {
        return;
    }

    bind_blend_state(stencil_mask_blend_state_.Get());
    bind_depth_stencil_state(op == StencilUpdateOp::Increment
                                 ? stencil_increment_state_.Get()
                                 : stencil_decrement_state_.Get(),
                             reference);
    draw_vertices_now(vertices, nullptr, TextureSamplingMode::None);
    bind_blend_state(blend_state_.Get());
    const auto draw_depth = recorder_state_.state_stack.empty()
                                ? std::uint8_t{0U}
                                : recorder_state_.state_stack.back().stencil_depth;
    bind_depth_stencil_state(draw_depth > 0U ? stencil_test_state_.Get()
                                             : stencil_disabled_state_.Get(),
                             draw_depth);
}

D3D11DisplayListRenderer::GlyphBitmap D3D11DisplayListRenderer::rasterize_glyph_coverage(
    IDWriteFontFace& face, const rendering::TextGlyph& glyph, float font_size) const {
    auto* factory = shared_dwrite_factory();
    if (factory == nullptr || glyph.glyph_index == 0U || font_size <= 0.0F) {
        return {};
    }

    const auto glyph_index = static_cast<UINT16>(glyph.glyph_index);
    const auto advance = glyph.advance;
    const DWRITE_GLYPH_OFFSET offset{};
    DWRITE_GLYPH_RUN glyph_run{};
    glyph_run.fontFace = &face;
    glyph_run.fontEmSize = font_size;
    glyph_run.glyphCount = 1U;
    glyph_run.glyphIndices = &glyph_index;
    glyph_run.glyphAdvances = &advance;
    glyph_run.glyphOffsets = &offset;
    glyph_run.isSideways = FALSE;
    glyph_run.bidiLevel = glyph.is_right_to_left ? 1U : 0U;

    Microsoft::WRL::ComPtr<IDWriteGlyphRunAnalysis> analysis;
    auto result = factory->CreateGlyphRunAnalysis(
        &glyph_run, 1.0F, nullptr, DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
        DWRITE_MEASURING_MODE_NATURAL, 0.0F, 0.0F, &analysis);
    auto texture_type = DWRITE_TEXTURE_CLEARTYPE_3x1;
    if (FAILED(result) || analysis == nullptr) {
        result = factory->CreateGlyphRunAnalysis(
            &glyph_run, 1.0F, nullptr, DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
            0.0F, 0.0F, &analysis);
        texture_type = DWRITE_TEXTURE_ALIASED_1x1;
    }
    if (FAILED(result) || analysis == nullptr) {
        return {};
    }

    RECT bounds{};
    if (FAILED(analysis->GetAlphaTextureBounds(texture_type, &bounds)) ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return {};
    }

    const auto width = static_cast<std::uint32_t>(bounds.right - bounds.left);
    const auto height = static_cast<std::uint32_t>(bounds.bottom - bounds.top);
    const auto bytes_per_pixel = texture_type == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3U : 1U;
    std::vector<std::uint8_t> texture(static_cast<std::size_t>(width) * height * bytes_per_pixel);
    if (FAILED(analysis->CreateAlphaTexture(texture_type, &bounds, texture.data(),
                                            static_cast<UINT32>(texture.size())))) {
        return {};
    }

    std::vector<std::byte> padded_coverage(
        static_cast<std::size_t>(width + glyph_atlas_padding * 2U) *
            (height + glyph_atlas_padding * 2U) * glyph_atlas_bytes_per_pixel,
        std::byte{0});
    const auto padded_width = width + glyph_atlas_padding * 2U;
    for (std::uint32_t y = 0U; y < height; ++y) {
        const auto destination =
            static_cast<std::size_t>(y + glyph_atlas_padding) * padded_width + glyph_atlas_padding;
        for (std::uint32_t x = 0U; x < width; ++x) {
            const auto source = static_cast<std::size_t>(y) * width + x;
            const auto target = (destination + x) * glyph_atlas_bytes_per_pixel;
            if (bytes_per_pixel == 1U) {
                const auto coverage = texture[source];
                padded_coverage[target] = static_cast<std::byte>(coverage);
                padded_coverage[target + 1U] = static_cast<std::byte>(coverage);
                padded_coverage[target + 2U] = static_cast<std::byte>(coverage);
                padded_coverage[target + 3U] = static_cast<std::byte>(coverage);
            } else {
                const auto base = source * 3U;
                const auto red_coverage = texture[base];
                const auto green_coverage = texture[base + 1U];
                const auto blue_coverage = texture[base + 2U];
                padded_coverage[target] = static_cast<std::byte>(red_coverage);
                padded_coverage[target + 1U] = static_cast<std::byte>(green_coverage);
                padded_coverage[target + 2U] = static_cast<std::byte>(blue_coverage);
                padded_coverage[target + 3U] =
                    static_cast<std::byte>(std::max({red_coverage, green_coverage, blue_coverage}));
            }
        }
    }

    return GlyphBitmap{.left = bounds.left - static_cast<int>(glyph_atlas_padding),
                       .top = bounds.top - static_cast<int>(glyph_atlas_padding),
                       .width = width + glyph_atlas_padding * 2U,
                       .height = height + glyph_atlas_padding * 2U,
                       .pixels = std::move(padded_coverage)};
}

void D3D11DisplayListRenderer::ensure_glyph_atlas_texture() {
    if (device_ == nullptr) {
        return;
    }
    if (glyph_atlas_texture_ == nullptr) {
        if (glyph_atlas_pixels_.empty()) {
            glyph_atlas_pixels_.assign(static_cast<std::size_t>(glyph_atlas_width) *
                                           glyph_atlas_height * glyph_atlas_bytes_per_pixel,
                                       std::byte{0});
        }

        D3D11_TEXTURE2D_DESC texture_description{};
        texture_description.Width = glyph_atlas_width;
        texture_description.Height = glyph_atlas_height;
        texture_description.MipLevels = 1U;
        texture_description.ArraySize = 1U;
        texture_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_description.SampleDesc.Count = 1U;
        texture_description.Usage = D3D11_USAGE_DEFAULT;
        texture_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initial_data{};
        initial_data.pSysMem = glyph_atlas_pixels_.data();
        initial_data.SysMemPitch = glyph_atlas_width * glyph_atlas_bytes_per_pixel;
        throw_if_failed(
            device_->CreateTexture2D(&texture_description, &initial_data, &glyph_atlas_texture_),
            "failed to create display-list glyph atlas texture");
    }
    if (glyph_atlas_view_ != nullptr) {
        return;
    }

    D3D11_TEXTURE2D_DESC texture_description{};
    glyph_atlas_texture_->GetDesc(&texture_description);

    D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
    view_description.Format = texture_description.Format;
    view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view_description.Texture2D.MipLevels = 1U;
    throw_if_failed(device_->CreateShaderResourceView(glyph_atlas_texture_.Get(), &view_description,
                                                      &glyph_atlas_view_),
                    "failed to create display-list glyph atlas view");
}

void D3D11DisplayListRenderer::upload_glyph_atlas_if_dirty() {
    if (!glyph_atlas_dirty_) {
        return;
    }
    if (!resource_updates_allowed_) {
        return;
    }
    if (context_ == nullptr || context_ == primary_deferred_context_.Get()) {
        ensure_glyph_atlas_texture();
        return;
    }
    upload_glyph_atlas_if_dirty(*context_, true);
}

void D3D11DisplayListRenderer::upload_glyph_atlas_if_dirty(ID3D11DeviceContext& context,
                                                           bool flush_pending_batch) {
    if (!glyph_atlas_dirty_) {
        return;
    }
    if (!resource_updates_allowed_) {
        return;
    }
    const auto texture_existed = glyph_atlas_texture_ != nullptr;
    ensure_glyph_atlas_texture();
    if (glyph_atlas_texture_ == nullptr || glyph_atlas_pixels_.empty()) {
        return;
    }
    if (!texture_existed) {
        clear_glyph_atlas_dirty();
        return;
    }
    const auto left = std::min(glyph_atlas_dirty_left_, glyph_atlas_width);
    const auto top = std::min(glyph_atlas_dirty_top_, glyph_atlas_height);
    const auto right = std::min(glyph_atlas_dirty_right_, glyph_atlas_width);
    const auto bottom = std::min(glyph_atlas_dirty_bottom_, glyph_atlas_height);
    if (right <= left || bottom <= top) {
        clear_glyph_atlas_dirty();
        return;
    }

    if (flush_pending_batch) {
        flush_batch();
    }
    const auto row_pitch = glyph_atlas_width * glyph_atlas_bytes_per_pixel;
    const auto dirty_area = static_cast<std::size_t>(right - left) * (bottom - top);
    constexpr auto full_upload_threshold =
        static_cast<std::size_t>(glyph_atlas_width) * glyph_atlas_height / 2U;
    if (dirty_area >= full_upload_threshold) {
        context.UpdateSubresource(glyph_atlas_texture_.Get(), 0U, nullptr,
                                  glyph_atlas_pixels_.data(), row_pitch, 0U);
    } else {
        const auto source_offset = (static_cast<std::size_t>(top) * glyph_atlas_width + left) *
                                   glyph_atlas_bytes_per_pixel;
        const D3D11_BOX box{left, top, 0U, right, bottom, 1U};
        context.UpdateSubresource(glyph_atlas_texture_.Get(), 0U, &box,
                                  glyph_atlas_pixels_.data() + source_offset, row_pitch, 0U);
    }
    clear_glyph_atlas_dirty();
}

void D3D11DisplayListRenderer::mark_glyph_atlas_dirty(std::uint32_t left, std::uint32_t top,
                                                      std::uint32_t width,
                                                      std::uint32_t height) noexcept {
    const auto right = std::min(left + width, glyph_atlas_width);
    const auto bottom = std::min(top + height, glyph_atlas_height);
    if (right <= left || bottom <= top) {
        return;
    }

    if (!glyph_atlas_dirty_) {
        glyph_atlas_dirty_left_ = left;
        glyph_atlas_dirty_top_ = top;
        glyph_atlas_dirty_right_ = right;
        glyph_atlas_dirty_bottom_ = bottom;
        glyph_atlas_dirty_ = true;
        return;
    }

    glyph_atlas_dirty_left_ = std::min(glyph_atlas_dirty_left_, left);
    glyph_atlas_dirty_top_ = std::min(glyph_atlas_dirty_top_, top);
    glyph_atlas_dirty_right_ = std::max(glyph_atlas_dirty_right_, right);
    glyph_atlas_dirty_bottom_ = std::max(glyph_atlas_dirty_bottom_, bottom);
}

void D3D11DisplayListRenderer::clear_glyph_atlas_dirty() noexcept {
    glyph_atlas_dirty_ = false;
    glyph_atlas_dirty_left_ = 0U;
    glyph_atlas_dirty_top_ = 0U;
    glyph_atlas_dirty_right_ = 0U;
    glyph_atlas_dirty_bottom_ = 0U;
}

void D3D11DisplayListRenderer::reset_glyph_atlas() {
    flush_batch();
    glyph_atlas_entries_.clear();
    glyph_atlas_pixels_.assign(static_cast<std::size_t>(glyph_atlas_width) * glyph_atlas_height *
                                   glyph_atlas_bytes_per_pixel,
                               std::byte{0});
    glyph_atlas_cursor_x_ = 0U;
    glyph_atlas_cursor_y_ = 0U;
    glyph_atlas_row_height_ = 0U;
    ++glyph_atlas_generation_;
    clear_glyph_atlas_dirty();
}

const D3D11DisplayListRenderer::GlyphAtlasEntry* D3D11DisplayListRenderer::glyph_atlas_entry(
    IDWriteFontFace& face, const rendering::TextGlyph& glyph, const rendering::TextStyle& style,
    const rendering::PreparedTextGlyphCoverageList* prepared_glyphs) {
    if (glyph.glyph_index == 0U) {
        return nullptr;
    }

    const auto key = GlyphAtlasKey{
        .font_family = glyph.font_family.empty() ? style.font_family : glyph.font_family,
        .glyph_index = glyph.glyph_index,
        .font_size_key = static_cast<std::uint32_t>(std::round(style.font_size * 64.0F)),
        .font_weight = static_cast<std::uint16_t>(style.font_weight),
        .font_stretch = static_cast<std::uint16_t>(style.font_stretch),
        .font_style = static_cast<std::uint8_t>(style.font_style),
        .is_right_to_left = glyph.is_right_to_left};
    if (const auto iterator = glyph_atlas_entries_.find(key);
        iterator != glyph_atlas_entries_.end()) {
        return &iterator->second;
    }

    if (prepared_glyphs != nullptr) {
        const auto key_hash = GlyphAtlasKeyHash{}(key);
        if (const auto iterator = prepared_glyphs->glyphs_by_hash.find(key_hash);
            iterator != prepared_glyphs->glyphs_by_hash.end()) {
            for (const auto& coverage : iterator->second) {
                if (coverage != nullptr && !coverage->pixels.empty() &&
                    coverage->font_family == key.font_family &&
                    coverage->glyph_index == key.glyph_index &&
                    coverage->font_size_key == key.font_size_key &&
                    coverage->font_weight == key.font_weight &&
                    coverage->font_stretch == key.font_stretch &&
                    coverage->font_style == key.font_style &&
                    coverage->is_right_to_left == key.is_right_to_left) {
                    return prepared_glyph_atlas_entry(*coverage);
                }
            }
        }
    }

    if (!resource_updates_allowed_) {
        return nullptr;
    }

    auto bitmap = rasterize_glyph_coverage(face, glyph, style.font_size);
    if (bitmap.pixels.empty() || bitmap.width == 0U || bitmap.height == 0U ||
        bitmap.width > glyph_atlas_width || bitmap.height > glyph_atlas_height) {
        return nullptr;
    }

    if (glyph_atlas_pixels_.empty()) {
        reset_glyph_atlas();
    }

    if (glyph_atlas_cursor_x_ + bitmap.width > glyph_atlas_width) {
        glyph_atlas_cursor_x_ = 0U;
        glyph_atlas_cursor_y_ += glyph_atlas_row_height_;
        glyph_atlas_row_height_ = 0U;
    }
    if (glyph_atlas_cursor_y_ + bitmap.height > glyph_atlas_height) {
        reset_glyph_atlas();
    }

    const auto atlas_x = glyph_atlas_cursor_x_;
    const auto atlas_y = glyph_atlas_cursor_y_;
    for (std::uint32_t row = 0U; row < bitmap.height; ++row) {
        const auto destination =
            (static_cast<std::size_t>(atlas_y + row) * glyph_atlas_width + atlas_x) *
            glyph_atlas_bytes_per_pixel;
        const auto source =
            static_cast<std::size_t>(row) * bitmap.width * glyph_atlas_bytes_per_pixel;
        const auto row_bytes = static_cast<std::size_t>(bitmap.width) * glyph_atlas_bytes_per_pixel;
        std::copy(bitmap.pixels.begin() + static_cast<std::ptrdiff_t>(source),
                  bitmap.pixels.begin() + static_cast<std::ptrdiff_t>(source + row_bytes),
                  glyph_atlas_pixels_.begin() + static_cast<std::ptrdiff_t>(destination));
    }

    glyph_atlas_cursor_x_ += bitmap.width;
    glyph_atlas_row_height_ = std::max(glyph_atlas_row_height_, bitmap.height);
    mark_glyph_atlas_dirty(atlas_x, atlas_y, bitmap.width, bitmap.height);

    const auto entry = GlyphAtlasEntry{
        .left = static_cast<float>(bitmap.left),
        .top = static_cast<float>(bitmap.top),
        .width = static_cast<float>(bitmap.width),
        .height = static_cast<float>(bitmap.height),
        .u0 = static_cast<float>(atlas_x) / static_cast<float>(glyph_atlas_width),
        .v0 = static_cast<float>(atlas_y) / static_cast<float>(glyph_atlas_height),
        .u1 = static_cast<float>(atlas_x + bitmap.width) / static_cast<float>(glyph_atlas_width),
        .v1 = static_cast<float>(atlas_y + bitmap.height) / static_cast<float>(glyph_atlas_height),
        .generation = glyph_atlas_generation_};
    const auto [iterator, inserted] = glyph_atlas_entries_.emplace(key, entry);
    (void)inserted;
    return &iterator->second;
}

const D3D11DisplayListRenderer::GlyphAtlasEntry*
D3D11DisplayListRenderer::prepared_glyph_atlas_entry(
    const rendering::TextGlyph& glyph, const rendering::TextStyle& style,
    const rendering::PreparedTextGlyphCoverageList* prepared_glyphs) {
    if (prepared_glyphs == nullptr || glyph.glyph_index == 0U) {
        return nullptr;
    }

    const auto key = GlyphAtlasKey{
        .font_family = glyph.font_family.empty() ? style.font_family : glyph.font_family,
        .glyph_index = glyph.glyph_index,
        .font_size_key = static_cast<std::uint32_t>(std::round(style.font_size * 64.0F)),
        .font_weight = static_cast<std::uint16_t>(style.font_weight),
        .font_stretch = static_cast<std::uint16_t>(style.font_stretch),
        .font_style = static_cast<std::uint8_t>(style.font_style),
        .is_right_to_left = glyph.is_right_to_left};
    if (const auto iterator = glyph_atlas_entries_.find(key);
        iterator != glyph_atlas_entries_.end()) {
        return &iterator->second;
    }

    if (!resource_updates_allowed_) {
        return nullptr;
    }

    const auto key_hash = GlyphAtlasKeyHash{}(key);
    if (const auto iterator = prepared_glyphs->glyphs_by_hash.find(key_hash);
        iterator != prepared_glyphs->glyphs_by_hash.end()) {
        for (const auto& coverage : iterator->second) {
            if (coverage != nullptr && !coverage->pixels.empty() &&
                coverage->font_family == key.font_family &&
                coverage->glyph_index == key.glyph_index &&
                coverage->font_size_key == key.font_size_key &&
                coverage->font_weight == key.font_weight &&
                coverage->font_stretch == key.font_stretch &&
                coverage->font_style == key.font_style &&
                coverage->is_right_to_left == key.is_right_to_left) {
                return prepared_glyph_atlas_entry(*coverage);
            }
        }
    }
    return nullptr;
}

const D3D11DisplayListRenderer::GlyphAtlasEntry*
D3D11DisplayListRenderer::prepared_glyph_atlas_entry(
    const rendering::PreparedTextGlyphCoverage& coverage) {
    const auto key = GlyphAtlasKey{.font_family = coverage.font_family,
                                   .glyph_index = coverage.glyph_index,
                                   .font_size_key = coverage.font_size_key,
                                   .font_weight = coverage.font_weight,
                                   .font_stretch = coverage.font_stretch,
                                   .font_style = coverage.font_style,
                                   .is_right_to_left = coverage.is_right_to_left};
    if (const auto iterator = glyph_atlas_entries_.find(key);
        iterator != glyph_atlas_entries_.end()) {
        return &iterator->second;
    }
    if (!resource_updates_allowed_) {
        return nullptr;
    }
    if (coverage.pixels.empty() || coverage.width == 0U || coverage.height == 0U ||
        coverage.width > glyph_atlas_width || coverage.height > glyph_atlas_height) {
        return nullptr;
    }

    if (glyph_atlas_pixels_.empty()) {
        reset_glyph_atlas();
    }

    if (glyph_atlas_cursor_x_ + coverage.width > glyph_atlas_width) {
        glyph_atlas_cursor_x_ = 0U;
        glyph_atlas_cursor_y_ += glyph_atlas_row_height_;
        glyph_atlas_row_height_ = 0U;
    }
    if (glyph_atlas_cursor_y_ + coverage.height > glyph_atlas_height) {
        reset_glyph_atlas();
    }

    const auto atlas_x = glyph_atlas_cursor_x_;
    const auto atlas_y = glyph_atlas_cursor_y_;
    for (std::uint32_t row = 0U; row < coverage.height; ++row) {
        const auto destination =
            (static_cast<std::size_t>(atlas_y + row) * glyph_atlas_width + atlas_x) *
            glyph_atlas_bytes_per_pixel;
        const auto source =
            static_cast<std::size_t>(row) * coverage.width * glyph_atlas_bytes_per_pixel;
        const auto row_bytes =
            static_cast<std::size_t>(coverage.width) * glyph_atlas_bytes_per_pixel;
        std::copy(coverage.pixels.begin() + static_cast<std::ptrdiff_t>(source),
                  coverage.pixels.begin() + static_cast<std::ptrdiff_t>(source + row_bytes),
                  glyph_atlas_pixels_.begin() + static_cast<std::ptrdiff_t>(destination));
    }

    glyph_atlas_cursor_x_ += coverage.width;
    glyph_atlas_row_height_ = std::max(glyph_atlas_row_height_, coverage.height);
    mark_glyph_atlas_dirty(atlas_x, atlas_y, coverage.width, coverage.height);

    const auto entry = GlyphAtlasEntry{
        .left = static_cast<float>(coverage.left),
        .top = static_cast<float>(coverage.top),
        .width = static_cast<float>(coverage.width),
        .height = static_cast<float>(coverage.height),
        .u0 = static_cast<float>(atlas_x) / static_cast<float>(glyph_atlas_width),
        .v0 = static_cast<float>(atlas_y) / static_cast<float>(glyph_atlas_height),
        .u1 = static_cast<float>(atlas_x + coverage.width) / static_cast<float>(glyph_atlas_width),
        .v1 =
            static_cast<float>(atlas_y + coverage.height) / static_cast<float>(glyph_atlas_height),
        .generation = glyph_atlas_generation_};
    const auto [iterator, inserted] = glyph_atlas_entries_.emplace(key, entry);
    (void)inserted;
    return &iterator->second;
}

void D3D11DisplayListRenderer::draw_text(std::string_view text, core::Rect rect,
                                         const rendering::TextStyle& style) {
    if (text.empty() || !rendering::layout::is_visible_rect(rect)) {
        return;
    }

    const auto layout = render_worker_text_engine().layout_text(
        text, style,
        rendering::TextLayoutOptions{.max_width = rect.width, .max_height = rect.height});
    draw_text_layout(layout, rendering::layout::Point{rect.x, rect.y});
}

void D3D11DisplayListRenderer::draw_text_layout(const rendering::TextLayout& layout,
                                                core::Point origin) {
    draw_text_layout(layout, origin, nullptr);
}

void D3D11DisplayListRenderer::draw_text_layout(const rendering::DrawTextLayoutCommand& command) {
    if (const auto* layout = command.layout_value()) {
        draw_text_layout(*layout, command.origin, command.prepared_glyphs.get());
    }
}

void D3D11DisplayListRenderer::draw_text_layout(
    const rendering::TextLayout& layout, core::Point origin,
    const rendering::PreparedTextGlyphCoverageList* prepared_glyphs) {
    if (layout.text.empty()) {
        return;
    }

    auto default_face = cached_font_face(layout.style);
    if (default_face != nullptr) {
        const auto face_for_glyph = [&](const rendering::TextGlyph& glyph) {
            const auto& font_family =
                glyph.font_family.empty() ? layout.style.font_family : glyph.font_family;
            if (font_family.empty() || font_family == layout.style.font_family) {
                return default_face;
            }

            auto glyph_style = layout.style;
            glyph_style.font_family = font_family;
            auto face = cached_font_face(glyph_style);
            if (face == nullptr) {
                face = default_face;
            }
            return face;
        };

        const auto glyph_run_fingerprint = text_atlas_run_fingerprint(layout);
        const auto glyphs_signature = prepared_text_glyphs_signature(prepared_glyphs);
        auto* cached_glyph_run = static_cast<CachedTextGlyphRun*>(nullptr);
        for (auto& cached : text_glyph_run_cache_) {
            if (cached.fingerprint == glyph_run_fingerprint &&
                (cached.prepared_glyphs_signature == glyphs_signature ||
                 cached.positioned_glyphs.size() + cached.outline_glyph_indices.size() ==
                     layout.glyphs.size()) &&
                cached.generation == glyph_atlas_generation_) {
                cached.last_used_frame = frame_sequence_;
                cached_glyph_run = &cached;
                break;
            }
        }

        auto& positioned_glyphs = recorder_state_.positioned_text_glyphs;
        auto& outline_fallbacks = recorder_state_.outline_text_glyph_indices;
        auto* positioned_glyphs_to_draw = &positioned_glyphs;
        auto* outline_fallbacks_to_draw = &outline_fallbacks;
        if (cached_glyph_run != nullptr) {
            positioned_glyphs_to_draw = &cached_glyph_run->positioned_glyphs;
            outline_fallbacks_to_draw = &cached_glyph_run->outline_glyph_indices;
        } else {
            positioned_glyphs.clear();
            positioned_glyphs.reserve(layout.glyphs.size());
            outline_fallbacks.clear();
            outline_fallbacks.reserve(layout.glyphs.size());
            auto collected_atlas_glyphs = false;
            for (auto attempt = 0U; attempt < 2U; ++attempt) {
                positioned_glyphs.clear();
                outline_fallbacks.clear();
                const auto generation = glyph_atlas_generation_;
                auto restarted = false;
                for (std::size_t glyph_index = 0U; glyph_index < layout.glyphs.size();
                     ++glyph_index) {
                    const auto& glyph = layout.glyphs[glyph_index];
                    if (glyph.glyph_index == 0U) {
                        continue;
                    }
                    auto* entry = static_cast<const GlyphAtlasEntry*>(nullptr);
                    if (prepared_glyphs != nullptr &&
                        glyph_index < prepared_glyphs->glyphs_by_layout_index.size()) {
                        if (const auto& coverage =
                                prepared_glyphs->glyphs_by_layout_index[glyph_index]) {
                            entry = prepared_glyph_atlas_entry(*coverage);
                        }
                    }
                    if (entry == nullptr) {
                        auto glyph_face = face_for_glyph(glyph);
                        if (glyph_face == nullptr) {
                            continue;
                        }
                        entry = glyph_atlas_entry(*glyph_face.Get(), glyph, layout.style,
                                                  prepared_glyphs);
                    }
                    if (entry != nullptr) {
                        if (entry->generation != glyph_atlas_generation_ ||
                            generation != glyph_atlas_generation_) {
                            restarted = true;
                            break;
                        }
                        positioned_glyphs.push_back(
                            PositionedTextGlyph{.glyph_index = glyph_index, .entry = *entry});
                    } else {
                        outline_fallbacks.push_back(glyph_index);
                    }
                }
                if (!restarted) {
                    collected_atlas_glyphs = true;
                    break;
                }
            }
            if (!collected_atlas_glyphs) {
                positioned_glyphs.clear();
                outline_fallbacks.clear();
                outline_fallbacks.reserve(layout.glyphs.size());
                for (std::size_t glyph_index = 0U; glyph_index < layout.glyphs.size();
                     ++glyph_index) {
                    outline_fallbacks.push_back(glyph_index);
                }
            }

            constexpr auto max_text_glyph_run_cache_entries = 48U;
            auto* cache_slot = static_cast<CachedTextGlyphRun*>(nullptr);
            if (text_glyph_run_cache_.size() >= max_text_glyph_run_cache_entries) {
                const auto iterator = std::min_element(
                    text_glyph_run_cache_.begin(), text_glyph_run_cache_.end(),
                    [](const CachedTextGlyphRun& left, const CachedTextGlyphRun& right) {
                        return left.last_used_frame < right.last_used_frame;
                    });
                if (iterator != text_glyph_run_cache_.end()) {
                    cache_slot = &*iterator;
                }
            } else {
                text_glyph_run_cache_.push_back(CachedTextGlyphRun{});
                cache_slot = &text_glyph_run_cache_.back();
            }
            if (cache_slot != nullptr) {
                *cache_slot = CachedTextGlyphRun{.layout = &layout,
                                                 .prepared_glyphs = prepared_glyphs,
                                                 .fingerprint = glyph_run_fingerprint,
                                                 .prepared_glyphs_signature = glyphs_signature,
                                                 .generation = glyph_atlas_generation_,
                                                 .last_used_frame = frame_sequence_,
                                                 .positioned_glyphs = positioned_glyphs,
                                                 .outline_glyph_indices = outline_fallbacks};
            }
        }

        upload_glyph_atlas_if_dirty();
        if (!positioned_glyphs_to_draw->empty() && glyph_atlas_view_ != nullptr) {
            const auto& state = recorder_state_.state_stack.back();
            const auto color = premultiplied_color(layout.style.color, state.opacity);
            auto& vertices = prepare_primitive_vertices(positioned_glyphs_to_draw->size() * 6U);
            for (const auto& positioned : *positioned_glyphs_to_draw) {
                if (positioned.glyph_index >= layout.glyphs.size()) {
                    continue;
                }
                const auto& glyph = layout.glyphs[positioned.glyph_index];
                const auto& entry = positioned.entry;
                const auto left = origin.x + glyph.origin.x + entry.left;
                const auto top = origin.y + glyph.origin.y + entry.top;
                const auto right = left + entry.width;
                const auto bottom = top + entry.height;
                const auto top_left = rendering::transform_point(
                    rendering::layout::Point{left, top}, state.transform);
                const auto top_right = rendering::transform_point(
                    rendering::layout::Point{right, top}, state.transform);
                const auto bottom_right = rendering::transform_point(
                    rendering::layout::Point{right, bottom}, state.transform);
                const auto bottom_left = rendering::transform_point(
                    rendering::layout::Point{left, bottom}, state.transform);
                vertices.push_back(Vertex{top_left.x, top_left.y, color[0], color[1], color[2],
                                          color[3], entry.u0, entry.v0});
                vertices.push_back(Vertex{top_right.x, top_right.y, color[0], color[1], color[2],
                                          color[3], entry.u1, entry.v0});
                vertices.push_back(Vertex{bottom_right.x, bottom_right.y, color[0], color[1],
                                          color[2], color[3], entry.u1, entry.v1});
                vertices.push_back(Vertex{top_left.x, top_left.y, color[0], color[1], color[2],
                                          color[3], entry.u0, entry.v0});
                vertices.push_back(Vertex{bottom_right.x, bottom_right.y, color[0], color[1],
                                          color[2], color[3], entry.u1, entry.v1});
                vertices.push_back(Vertex{bottom_left.x, bottom_left.y, color[0], color[1],
                                          color[2], color[3], entry.u0, entry.v1});
            }
            submit_vertices(vertices, glyph_atlas_view_.Get(),
                            TextureSamplingMode::RgbSubpixelCoverage);
        }

        for (const auto fallback_glyph_index : *outline_fallbacks_to_draw) {
            if (fallback_glyph_index >= layout.glyphs.size()) {
                continue;
            }
            const auto& glyph = layout.glyphs[fallback_glyph_index];
            if (glyph.glyph_index == 0U) {
                continue;
            }
            auto glyph_face = face_for_glyph(glyph);
            if (glyph_face == nullptr) {
                continue;
            }
            auto geometry =
                glyph_outline_geometry(*glyph_face.Get(), glyph, layout.style.font_size);
            geometry = offset_geometry(
                std::move(geometry),
                rendering::layout::Point{origin.x + glyph.origin.x, origin.y + glyph.origin.y});
            fill_geometry(geometry, layout.style.color);
        }
    } else {
        for (const auto& cluster : layout.clusters) {
            draw_solid_rect(rendering::layout::Rect{origin.x + cluster.origin.x,
                                                    origin.y + cluster.origin.y,
                                                    std::max(cluster.size.width, 1.0F),
                                                    std::max(cluster.size.height, 1.0F)},
                            layout.style.color);
        }
    }

    for (const auto& decoration : layout.decorations) {
        draw_solid_rect(rendering::layout::Rect{origin.x + decoration.rect.x,
                                                origin.y + decoration.rect.y, decoration.rect.width,
                                                decoration.rect.height},
                        layout.style.color);
    }
}

void D3D11DisplayListRenderer::draw_box_shadow(core::Rect rect,
                                               const rendering::ShadowStyle& style) {
    if (!rendering::layout::is_visible_rect(rect) || style.color.alpha == 0U) {
        return;
    }

    const auto base_rect = rendering::layout::offset_rect(
        rendering::layout::inflate_rect(rect, std::max(style.spread, 0.0F)), style.offset);
    const auto blur = std::max(style.blur_radius, 0.0F);
    const auto steps = std::clamp(static_cast<int>(std::ceil(blur * 0.5F)), 1, 16);
    for (auto step = steps; step >= 1; --step) {
        const auto t = static_cast<float>(step) / static_cast<float>(steps);
        const auto alpha_scale = (1.0F - t * 0.75F) / static_cast<float>(steps);
        const auto shadow_rect = rendering::layout::inflate_rect(base_rect, blur * t);
        draw_solid_rect(shadow_rect, multiply_alpha(style.color, alpha_scale));
    }
    draw_solid_rect(base_rect, multiply_alpha(style.color, 0.18F));
}

void D3D11DisplayListRenderer::draw_image(rendering::RenderResourceId resource_id,
                                          const rendering::RenderImageOptions& options,
                                          const D3D11RenderResourceCache::Snapshot& resource_snapshot) {
    const auto* texture = resource_snapshot.texture(resource_id);
    if (texture == nullptr || texture->view == nullptr ||
        !rendering::layout::is_visible_rect(options.destination)) {
        return;
    }

    const auto& state = recorder_state_.state_stack.back();
    const auto alpha = std::clamp(options.opacity * state.opacity, 0.0F, 1.0F);
    const auto top_left = rendering::transform_point(
        rendering::layout::Point{options.destination.x, options.destination.y}, state.transform);
    const auto top_right = rendering::transform_point(
        rendering::layout::Point{options.destination.x + options.destination.width,
                                 options.destination.y},
        state.transform);
    const auto bottom_right = rendering::transform_point(
        rendering::layout::Point{options.destination.x + options.destination.width,
                                 options.destination.y + options.destination.height},
        state.transform);
    const auto bottom_left = rendering::transform_point(
        rendering::layout::Point{options.destination.x,
                                 options.destination.y + options.destination.height},
        state.transform);

    auto source = options.source;
    if (!rendering::layout::is_visible_rect(source)) {
        source = rendering::layout::Rect{0.0F, 0.0F, static_cast<float>(texture->width),
                                         static_cast<float>(texture->height)};
    }
    const auto u0 = source.x / static_cast<float>(std::max(texture->width, 1U));
    const auto v0 = source.y / static_cast<float>(std::max(texture->height, 1U));
    const auto u1 = (source.x + source.width) / static_cast<float>(std::max(texture->width, 1U));
    const auto v1 = (source.y + source.height) / static_cast<float>(std::max(texture->height, 1U));
    const std::vector<Vertex> vertices{
        {top_left.x, top_left.y, 1.0F, 1.0F, 1.0F, alpha, u0, v0},
        {top_right.x, top_right.y, 1.0F, 1.0F, 1.0F, alpha, u1, v0},
        {bottom_right.x, bottom_right.y, 1.0F, 1.0F, 1.0F, alpha, u1, v1},
        {top_left.x, top_left.y, 1.0F, 1.0F, 1.0F, alpha, u0, v0},
        {bottom_right.x, bottom_right.y, 1.0F, 1.0F, 1.0F, alpha, u1, v1},
        {bottom_left.x, bottom_left.y, 1.0F, 1.0F, 1.0F, alpha, u0, v1}};
    submit_vertices(vertices, texture->view.Get(),
                    texture->format == rendering::RenderResourceFormat::Alpha8
                        ? TextureSamplingMode::AlphaCoverage
                        : TextureSamplingMode::Bgra);
}

void D3D11DisplayListRenderer::submit_vertices(std::span<const Vertex> vertices,
                                               ID3D11ShaderResourceView* texture,
                                               TextureSamplingMode texture_mode) {
    if (context_ == nullptr || vertices.empty() || recorder_state_.state_stack.empty() ||
        !is_visible_scissor(recorder_state_.state_stack.back().clip)) {
        return;
    }

    const auto stencil_depth = recorder_state_.state_stack.back().stencil_depth;
    const auto mode = texture == nullptr ? TextureSamplingMode::None : texture_mode;
    auto first = std::size_t{0U};
    const auto aligned_vertex_count = vertices.size() - vertices.size() % triangle_vertex_count;
    while (first < aligned_vertex_count) {
        if (recorder_state_.batch_active && (recorder_state_.batch_texture != texture || recorder_state_.batch_texture_mode != mode ||
                              recorder_state_.batch_stencil_depth != stencil_depth ||
                              recorder_state_.batch_vertices.size() + triangle_vertex_count > max_vertices)) {
            flush_batch();
        }

        if (!recorder_state_.batch_active) {
            recorder_state_.batch_texture = texture;
            recorder_state_.batch_texture_mode = mode;
            recorder_state_.batch_stencil_depth = stencil_depth;
            recorder_state_.batch_active = true;
        }

        const auto capacity = max_vertices - recorder_state_.batch_vertices.size();
        auto count = std::min<std::size_t>(capacity, aligned_vertex_count - first);
        count -= count % triangle_vertex_count;
        if (count == 0U) {
            flush_batch();
            continue;
        }
        append_batch_vertices(vertices.subspan(first, count));
        first += count;
        if (recorder_state_.batch_vertices.size() >= max_vertices) {
            flush_batch();
        }
    }
}

void D3D11DisplayListRenderer::flush_batch() {
    if (context_ == nullptr || !recorder_state_.batch_active || recorder_state_.batch_vertices.empty()) {
        recorder_state_.batch_vertices.clear();
        recorder_state_.batch_active = false;
        return;
    }

    const auto& pipeline = cached_pipeline_for(recorder_state_.batch_texture_mode, recorder_state_.batch_stencil_depth);
    bind_blend_state(pipeline.blend_state);
    bind_depth_stencil_state(pipeline.depth_stencil_state, recorder_state_.batch_stencil_depth);
    draw_vertices_now(recorder_state_.batch_vertices, recorder_state_.batch_texture, recorder_state_.batch_texture_mode);
    recorder_state_.batch_vertices.clear();
    recorder_state_.batch_texture = nullptr;
    recorder_state_.batch_texture_mode = TextureSamplingMode::None;
    recorder_state_.batch_stencil_depth = 0U;
    recorder_state_.batch_active = false;
}

void D3D11DisplayListRenderer::draw_vertices_now(std::span<const Vertex> vertices,
                                                 ID3D11ShaderResourceView* texture,
                                                 TextureSamplingMode texture_mode) {
    if (context_ == nullptr || vertices.empty()) {
        return;
    }

    auto mode_value = 0.0F;
    switch (texture_mode) {
    case TextureSamplingMode::Bgra:
        mode_value = 1.0F;
        break;
    case TextureSamplingMode::AlphaCoverage:
        mode_value = 2.0F;
        break;
    case TextureSamplingMode::RgbSubpixelCoverage:
        mode_value = 3.0F;
        break;
    case TextureSamplingMode::SignedDistance:
        mode_value = 4.0F;
        break;
    case TextureSamplingMode::None:
    default:
        mode_value = 0.0F;
        break;
    }

    auto constants = FrameConstants{.target_width = target_dip_width_,
                                    .target_height = target_dip_height_,
                                    .textured = texture == nullptr ? 0.0F : 1.0F,
                                    .texture_mode = mode_value};
    const auto constants_changed = !recorder_state_.constant_buffer_state_valid ||
                                   recorder_state_.uploaded_constant_target_width != constants.target_width ||
                                   recorder_state_.uploaded_constant_target_height != constants.target_height ||
                                   recorder_state_.uploaded_constant_textured != constants.textured ||
                                   recorder_state_.uploaded_constant_texture_mode != constants.texture_mode;
    if (constants_changed) {
        D3D11_MAPPED_SUBRESOURCE constants_resource{};
        if (SUCCEEDED(context_->Map(constant_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                    &constants_resource))) {
            *static_cast<FrameConstants*>(constants_resource.pData) = constants;
            context_->Unmap(constant_buffer_.Get(), 0);
            recorder_state_.uploaded_constant_target_width = constants.target_width;
            recorder_state_.uploaded_constant_target_height = constants.target_height;
            recorder_state_.uploaded_constant_textured = constants.textured;
            recorder_state_.uploaded_constant_texture_mode = constants.texture_mode;
            recorder_state_.constant_buffer_state_valid = true;
        }
    }

    bind_shader_resource(texture);

    for (std::size_t first = 0; first < vertices.size(); first += max_vertices) {
        const auto count = std::min<std::size_t>(max_vertices, vertices.size() - first);
        D3D11_MAPPED_SUBRESOURCE vertex_resource{};
        if (FAILED(context_->Map(vertex_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                 &vertex_resource))) {
            return;
        }
        std::memcpy(vertex_resource.pData, vertices.data() + first, count * sizeof(Vertex));
        context_->Unmap(vertex_buffer_.Get(), 0);
        context_->Draw(static_cast<UINT>(count), 0);
    }
}
} // namespace winelement::platform::win32
