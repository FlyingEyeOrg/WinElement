#pragma once

#include <winelement/core/core_types.hpp>
#include <winelement/rendering/render_frame_graph.hpp>

#include "d3d11_render_resource_cache.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>
#ifdef DrawText
#undef DrawText
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

struct IDWriteFontFace;

namespace winelement::rendering {
class DirtyRegion;
struct Geometry;
struct GeometryStrokeStyle;
enum class GeometryFillRule;
struct FillGeometryCommand;
struct StrokeGeometryCommand;
struct PushGeometryClipCommand;
struct DrawTextLayoutCommand;
struct PreparedGeometryFill;
struct PreparedTextGlyphCoverage;
struct PreparedTextGlyphCoverageList;
class RenderCommandList;
struct RenderFrameGraph;
class RenderScene;
struct RenderImageOptions;
struct RenderLayerOptions;
struct RenderNode;
struct RenderResourceId;
struct ShadowStyle;
struct TextGlyph;
struct TextLayout;
struct TextStyle;
} // namespace winelement::rendering

namespace winelement::platform::win32 {

struct D3D11RenderDirtyClip {
    core::Rect device_clip{};
    core::Rect cull_clip{};
};

class D3D11DisplayListRenderer final {
  public:
    struct RenderTimingMetrics {
        double task_split_ms = 0.0;
        double resource_sync_ms = 0.0;
        double worker_record_ms = 0.0;
        double command_submit_ms = 0.0;
        std::size_t work_item_count = 0U;
        std::size_t command_list_count = 0U;
        bool parallel_recording = false;
    };

    explicit D3D11DisplayListRenderer(ID3D11Device& device);
    ~D3D11DisplayListRenderer();

    D3D11DisplayListRenderer(const D3D11DisplayListRenderer&) = delete;
    D3D11DisplayListRenderer& operator=(const D3D11DisplayListRenderer&) = delete;

    void render(ID3D11DeviceContext& context, ID3D11RenderTargetView& target,
                core::Color clear_color, const rendering::RenderScene* scene,
                const rendering::DirtyRegion& dirty_region, float dpi,
                std::uint32_t target_pixel_width, std::uint32_t target_pixel_height,
                const D3D11RenderResourceCache& resource_cache,
                const rendering::RenderFrameGraph* frame_graph = nullptr);
    void set_parallel_recording_enabled(bool enabled) noexcept;
    [[nodiscard]] bool parallel_recording_enabled() const noexcept;
    [[nodiscard]] RenderTimingMetrics last_timing_metrics() const noexcept;
    void trim_parallel_recording_resources() noexcept;
    void trim_idle_resources() noexcept;

  private:
    enum class StateKind : std::uint8_t { Base, Save, RectClip, GeometryClip, Layer };
    enum class TextureSamplingMode : std::uint8_t {
        None,
        Bgra,
        AlphaCoverage,
        RgbSubpixelCoverage,
        SignedDistance
    };
    enum class PipelineVariant : std::uint8_t {
        AlphaBlend,
        SubpixelText,
        StencilAlphaBlend,
        StencilSubpixelText,
        Count
    };
    enum class StencilUpdateOp : std::uint8_t { Increment, Decrement };

    struct Vertex {
        float x = 0.0F;
        float y = 0.0F;
        float red = 0.0F;
        float green = 0.0F;
        float blue = 0.0F;
        float alpha = 1.0F;
        float u = 0.0F;
        float v = 0.0F;
    };
    struct DrawState {
        rendering::Transform2D transform{};
        float opacity = 1.0F;
        D3D11_RECT clip{};
        std::uint8_t stencil_depth = 0U;
        StateKind kind = StateKind::Base;
        std::shared_ptr<const rendering::Geometry> geometry_clip;
        std::shared_ptr<const rendering::PreparedGeometryFill> prepared_geometry_clip;
    };
    struct GlyphAtlasKey {
        std::string font_family;
        std::uint32_t glyph_index = 0U;
        std::uint32_t font_size_key = 0U;
        std::uint16_t font_weight = 400U;
        std::uint16_t font_stretch = 5U;
        std::uint8_t font_style = 0U;
        bool is_right_to_left = false;

        [[nodiscard]] friend bool operator==(const GlyphAtlasKey&,
                                             const GlyphAtlasKey&) noexcept = default;
    };
    struct GlyphAtlasKeyHash {
        [[nodiscard]] std::size_t operator()(const GlyphAtlasKey& key) const noexcept;
    };
    struct TextFaceKey {
        std::string font_family;
        std::uint16_t font_weight = 400U;
        std::uint16_t font_stretch = 5U;
        std::uint8_t font_style = 0U;

        [[nodiscard]] friend bool operator==(const TextFaceKey&,
                                             const TextFaceKey&) noexcept = default;
    };
    struct TextFaceKeyHash {
        [[nodiscard]] std::size_t operator()(const TextFaceKey& key) const noexcept;
    };
    struct GlyphAtlasEntry {
        float left = 0.0F;
        float top = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        float u0 = 0.0F;
        float v0 = 0.0F;
        float u1 = 0.0F;
        float v1 = 0.0F;
        std::uint32_t generation = 0U;
    };
    struct PositionedTextGlyph {
        std::size_t glyph_index = std::numeric_limits<std::size_t>::max();
        GlyphAtlasEntry entry{};
    };
    struct RecorderState {
        std::vector<DrawState> state_stack;
        std::vector<Vertex> batch_vertices;
        const rendering::RenderFrameGraph* active_frame_graph = nullptr;
        ID3D11ShaderResourceView* batch_texture = nullptr;
        TextureSamplingMode batch_texture_mode = TextureSamplingMode::None;
        std::uint8_t batch_stencil_depth = 0U;
        bool batch_active = false;
        ID3D11BlendState* bound_blend_state = nullptr;
        ID3D11DepthStencilState* bound_depth_stencil_state = nullptr;
        std::uint8_t bound_stencil_reference = 0U;
        ID3D11ShaderResourceView* bound_shader_resource = nullptr;
        std::vector<Vertex> primitive_vertices;
        std::vector<PositionedTextGlyph> positioned_text_glyphs;
        std::vector<std::size_t> outline_text_glyph_indices;
        std::vector<std::uint32_t> light_plan_command_indices;
        bool constant_buffer_state_valid = false;
        float uploaded_constant_target_width = 0.0F;
        float uploaded_constant_target_height = 0.0F;
        float uploaded_constant_textured = -1.0F;
        float uploaded_constant_texture_mode = -1.0F;
    };
    struct GlyphBitmap {
        int left = 0;
        int top = 0;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::vector<std::byte> pixels;
    };
    struct GeometryBoundaryEdge {
        core::Point start{};
        core::Point end{};
        core::Point direction{};
        core::Point outward{};
        bool visible = false;
    };
    struct GeometryFringeContour {
        std::vector<GeometryBoundaryEdge> edges;
        std::vector<core::Point> outer_points;
        std::vector<std::uint8_t> outer_point_valid;
    };
    struct GeometryFringeQuad {
        core::Point a{};
        core::Point b{};
        core::Point c{};
        core::Point d{};
    };
    struct UnitArcPoint {
        float cosine = 1.0F;
        float sine = 0.0F;
    };
    struct PrimitivePointCache {
        std::unordered_map<std::uint32_t, std::vector<core::Point>> unit_circle_points;
        std::unordered_map<std::uint32_t, std::vector<UnitArcPoint>> quarter_arc_points;
    };
    struct TransformedGeometryFill {
        const rendering::PreparedGeometryFill* prepared_fill = nullptr;
        std::size_t prepared_signature = 0U;
        core::Transform2D transform{};
        std::uint64_t last_used_frame = 0U;
        std::vector<std::vector<core::Point>> filled_contours;
        std::vector<core::Point> tessellated_vertices;
        std::vector<GeometryFringeContour> fringe_contours;
        std::vector<GeometryFringeQuad> fringe_quads;
        rendering::GeometryFillRule fringe_fill_rule{};
        float fringe_width = 0.0F;
        bool fringe_valid = false;
    };
    struct CachedTextGlyphRun {
        const rendering::TextLayout* layout = nullptr;
        const rendering::PreparedTextGlyphCoverageList* prepared_glyphs = nullptr;
        std::size_t fingerprint = 0U;
        std::size_t prepared_glyphs_signature = 0U;
        std::uint32_t generation = 0U;
        std::uint64_t last_used_frame = 0U;
        std::vector<PositionedTextGlyph> positioned_glyphs;
        std::vector<std::size_t> outline_glyph_indices;
    };
    struct CachedDrawPipeline {
        ID3D11BlendState* blend_state = nullptr;
        ID3D11DepthStencilState* depth_stencil_state = nullptr;
    };
    struct RenderPlanCacheState;
    enum class RendererInstanceKind : std::uint8_t { Primary, Worker };
    enum class RenderWorkKind : std::uint8_t { Clear, NodeCommandsOnly, NodeSubtree };

    struct RenderWorkSortKey {
        std::uint32_t pass_order = 0U;
        std::uint32_t scene_order = 0U;
        std::uint32_t layer_depth = 0U;
        std::uint32_t z_order = 0U;
        std::uint32_t dirty_bucket_order = 0U;
    };

    struct RenderFrameGraphSnapshot {
        rendering::RenderFrameGraph graph{};
    };

    struct PreparedDrawTextLayoutSnapshot {
        const rendering::RenderCommandList* commands = nullptr;
        std::size_t opcode_index = std::numeric_limits<std::size_t>::max();
        std::shared_ptr<const rendering::TextLayout> layout;
    };

    struct GlyphAtlasSnapshot {
        std::unordered_map<GlyphAtlasKey, GlyphAtlasEntry, GlyphAtlasKeyHash> entries;
        std::vector<PreparedDrawTextLayoutSnapshot> prepared_text_layouts;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::uint32_t generation = 0U;
    };

    struct RenderWorkItem {
        RenderWorkKind kind = RenderWorkKind::NodeSubtree;
        const rendering::RenderNode* scene_subtree = nullptr;
        std::vector<D3D11RenderDirtyClip> dirty_clips;
        std::shared_ptr<const RenderFrameGraphSnapshot> frame_graph_snapshot;
        std::shared_ptr<const D3D11RenderResourceCache::Snapshot> resource_snapshot;
        std::shared_ptr<const GlyphAtlasSnapshot> glyph_atlas_snapshot;
        RenderWorkSortKey sort_key{};
        std::uint64_t dependency_key = 0U;
        std::size_t estimated_command_count = 0U;
        std::size_t estimated_vertex_budget = 0U;
        core::Color clear_color{};
        float dpi = 96.0F;
        std::uint32_t target_pixel_width = 1U;
        std::uint32_t target_pixel_height = 1U;
        bool barrier = false;
        bool may_record_parallel = true;
        Microsoft::WRL::ComPtr<ID3D11CommandList> command_list;
        std::shared_ptr<D3D11DisplayListRenderer> recorder_lease;
    };

    struct RenderFrameAnalysis {
        std::vector<D3D11RenderDirtyClip> dirty_clips;
        std::vector<D3D11RenderDirtyClip> dirty_tiles;
        std::shared_ptr<const RenderFrameGraphSnapshot> frame_graph_snapshot;
        std::shared_ptr<const D3D11RenderResourceCache::Snapshot> resource_snapshot;
        std::shared_ptr<const GlyphAtlasSnapshot> glyph_atlas_snapshot;
        std::vector<RenderWorkItem> work_items;
        std::uint32_t target_pixel_width = 1U;
        std::uint32_t target_pixel_height = 1U;
        float dpi = 96.0F;
        float target_dip_width = 1.0F;
        float target_dip_height = 1.0F;
        bool use_parallel_recording = false;
    };

    D3D11DisplayListRenderer(ID3D11Device& device, RendererInstanceKind instance_kind);

    void render_to_context(ID3D11DeviceContext& context, ID3D11RenderTargetView& target,
                           core::Color clear_color, const rendering::RenderScene* scene,
                           const rendering::DirtyRegion& dirty_region, float dpi,
                           std::uint32_t target_pixel_width, std::uint32_t target_pixel_height,
                           const D3D11RenderResourceCache::Snapshot& resource_snapshot,
                           const rendering::RenderFrameGraph* frame_graph);
    [[nodiscard]] RenderFrameAnalysis
    analyze_render_frame(core::Color clear_color, const rendering::RenderScene* scene,
                         const rendering::DirtyRegion& dirty_region, float dpi,
                         std::uint32_t target_pixel_width, std::uint32_t target_pixel_height,
                         const D3D11RenderResourceCache& resource_cache,
                         const rendering::RenderFrameGraph* frame_graph);
    void prepare_text_resources_for_scene(const rendering::RenderScene* scene,
                                          std::span<const D3D11RenderDirtyClip> dirty_clips);
    void prepare_text_resources_for_node(const rendering::RenderNode& node,
                                         std::span<const D3D11RenderDirtyClip> dirty_clips);
    void prepare_text_resources_for_command_list(const rendering::RenderCommandList& commands);
    void prepare_text_layout_resources(const rendering::TextLayout& layout,
                                       const rendering::PreparedTextGlyphCoverageList* prepared);
    [[nodiscard]] std::shared_ptr<const GlyphAtlasSnapshot> snapshot_glyph_atlas() const;
    void assign_glyph_snapshot(RenderFrameAnalysis& analysis,
                               std::shared_ptr<const GlyphAtlasSnapshot> snapshot) const;
    void record_render_work_items(RenderFrameAnalysis& analysis, ID3D11RenderTargetView& target);
    void record_work_item_to_command_list(RenderWorkItem& work_item,
                                          ID3D11RenderTargetView& target);
    void render_work_item_to_context(ID3D11DeviceContext& context, ID3D11RenderTargetView& target,
                                     const RenderWorkItem& work_item, float dpi,
                                     std::uint32_t target_pixel_width,
                                     std::uint32_t target_pixel_height);
    void submit_recorded_work_items(ID3D11DeviceContext& context, RenderFrameAnalysis& analysis);
    [[nodiscard]] std::shared_ptr<D3D11DisplayListRenderer> acquire_worker_recorder();
    void release_worker_recorder(std::shared_ptr<D3D11DisplayListRenderer> recorder);
    void ensure_worker_recorder_pool(std::size_t recorder_count);
    void adopt_glyph_atlas_snapshot(const GlyphAtlasSnapshot& snapshot);

    void ensure_primary_deferred_context();
    void create_pipeline(ID3D11Device& device);
    void rebuild_pipeline_cache() noexcept;
    [[nodiscard]] const CachedDrawPipeline&
    cached_pipeline_for(TextureSamplingMode texture_mode,
                        std::uint8_t stencil_depth) const noexcept;
    void apply_frame_graph_plan(const rendering::RenderFrameGraph* frame_graph);
    void ensure_stencil_target();
    [[nodiscard]] bool activate_stencil_target();
    void release_stale_stencil_target() noexcept;
    void begin_frame(ID3D11DeviceContext& context, ID3D11RenderTargetView& target, float dpi,
                     std::uint32_t target_pixel_width, std::uint32_t target_pixel_height);
    void end_frame();
    void render_node(const rendering::RenderNode& node,
                     std::span<const D3D11RenderDirtyClip> dirty_clips,
                     const D3D11RenderResourceCache::Snapshot& resource_snapshot,
                     bool force_commands = false);
    void render_command_list(const rendering::RenderCommandList& commands,
                             std::span<const D3D11RenderDirtyClip> dirty_clips,
                             const D3D11RenderResourceCache::Snapshot& resource_snapshot,
                             bool force_commands);
    void clear_dirty_region(core::Color clear_color,
                            std::span<const D3D11RenderDirtyClip> dirty_clips);
    void render_command(const rendering::RenderCommandList& commands, std::size_t opcode_index,
                        const D3D11RenderResourceCache::Snapshot& resource_snapshot);
    void push_clip(core::Rect rect);
    void push_device_clip(core::Rect rect);
    void pop_clip();
    void push_layer(const rendering::RenderLayerOptions& options);
    void pop_layer();
    void apply_current_scissor();
    void bind_blend_state(ID3D11BlendState* blend_state) noexcept;
    void bind_depth_stencil_state(ID3D11DepthStencilState* depth_stencil_state,
                                  std::uint8_t stencil_reference) noexcept;
    void bind_shader_resource(ID3D11ShaderResourceView* texture_view) noexcept;
    void draw_solid_rect(core::Rect rect, core::Color color);
    void draw_stroke_rect(core::Rect rect, core::Color color, float stroke_width);
    void draw_line(core::Point start, core::Point end, core::Color color, float stroke_width);
    void draw_line_segment(core::Point start, core::Point end, core::Color color,
                           float stroke_width, bool antialias_start_cap, bool antialias_end_cap);
    void draw_ellipse(core::Rect rect, core::Color color);
    void draw_stroke_ellipse(core::Rect rect, core::Color color, float stroke_width);
    void fill_rounded_rect(core::Rect rect, core::CornerRadius radius, core::Color color);
    void stroke_rounded_rect(core::Rect rect, core::CornerRadius radius, core::Color color,
                             float stroke_width);
    void fill_geometry(const rendering::Geometry& geometry, core::Color color);
    void fill_geometry(const rendering::FillGeometryCommand& command);
    void draw_prepared_geometry_fill(const rendering::PreparedGeometryFill& prepared_fill,
                                     rendering::GeometryFillRule fill_rule, core::Color color);
    void stroke_geometry(const rendering::Geometry& geometry, core::Color color,
                         const rendering::GeometryStrokeStyle& style);
    void stroke_geometry(const rendering::StrokeGeometryCommand& command);
    void push_geometry_clip(const rendering::Geometry& geometry);
    void push_geometry_clip(const rendering::PushGeometryClipCommand& command);
    void pop_geometry_clip();
    void pop_state();
    void draw_text(std::string_view text, core::Rect rect, const rendering::TextStyle& style);
    void draw_text_layout(const rendering::TextLayout& layout, core::Point origin);
    void draw_text_layout(const rendering::TextLayout& layout, core::Point origin,
                          const rendering::PreparedTextGlyphCoverageList* prepared_glyphs);
    void draw_text_layout(const rendering::DrawTextLayoutCommand& command);
    void draw_box_shadow(core::Rect rect, const rendering::ShadowStyle& style);
    void fill_polygon(const std::vector<core::Point>& points, core::Color color);
    void stroke_polyline(const std::vector<core::Point>& points, core::Color color,
                         const rendering::GeometryStrokeStyle& style, bool closed);
    void draw_image(rendering::RenderResourceId resource_id,
                    const rendering::RenderImageOptions& options,
                    const D3D11RenderResourceCache::Snapshot& resource_snapshot);
    void append_batch_vertices(std::span<const Vertex> vertices);
    void submit_vertices(std::span<const Vertex> vertices, ID3D11ShaderResourceView* texture,
                         TextureSamplingMode texture_mode = TextureSamplingMode::None);
    void flush_batch();
    void draw_vertices_now(std::span<const Vertex> vertices, ID3D11ShaderResourceView* texture,
                           TextureSamplingMode texture_mode);
    void render_stencil_clip(const rendering::Geometry& geometry, std::uint8_t reference,
                             StencilUpdateOp op);
    void render_stencil_clip(const rendering::PreparedGeometryFill& prepared_fill,
                             std::uint8_t reference, StencilUpdateOp op);
    [[nodiscard]] const GlyphAtlasEntry*
    glyph_atlas_entry(IDWriteFontFace& face, const rendering::TextGlyph& glyph,
                      const rendering::TextStyle& style,
                      const rendering::PreparedTextGlyphCoverageList* prepared_glyphs = nullptr);
    [[nodiscard]] Microsoft::WRL::ComPtr<IDWriteFontFace>
    cached_font_face(const rendering::TextStyle& style);
    [[nodiscard]] const GlyphAtlasEntry*
    prepared_glyph_atlas_entry(const rendering::TextGlyph& glyph, const rendering::TextStyle& style,
                               const rendering::PreparedTextGlyphCoverageList* prepared_glyphs);
    [[nodiscard]] const GlyphAtlasEntry*
    prepared_glyph_atlas_entry(const rendering::PreparedTextGlyphCoverage& coverage);
    [[nodiscard]] GlyphBitmap rasterize_glyph_coverage(IDWriteFontFace& face,
                                                       const rendering::TextGlyph& glyph,
                                                       float font_size) const;
    [[nodiscard]] const std::vector<core::Point>& cached_unit_circle_points(std::uint32_t segments);
    [[nodiscard]] const std::vector<UnitArcPoint>&
    cached_quarter_arc_points(std::uint32_t segments);
    [[nodiscard]] std::vector<core::Point>
    rounded_rect_outline_from_cache(core::Rect rect, core::CornerRadius radius,
                                    std::uint32_t segments);
    [[nodiscard]] std::vector<Vertex>& prepare_primitive_vertices(std::size_t reserve_count);
    [[nodiscard]] TransformedGeometryFill&
    transformed_geometry_fill_for(const rendering::PreparedGeometryFill& prepared_fill,
                                  core::Transform2D transform);
    void ensure_glyph_atlas_texture();
    void upload_glyph_atlas_if_dirty();
    void upload_glyph_atlas_if_dirty(ID3D11DeviceContext& context, bool flush_pending_batch);
    [[nodiscard]] bool ensure_glyph_atlas_can_fit(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] bool advance_glyph_atlas_cursor(std::uint32_t width, std::uint32_t height);
    void mark_glyph_atlas_dirty(std::uint32_t left, std::uint32_t top, std::uint32_t width,
                                 std::uint32_t height) noexcept;
    void clear_glyph_atlas_dirty() noexcept;
    void reset_glyph_atlas();
    void reset_glyph_atlas(std::uint32_t width, std::uint32_t height);
    void prune_frame_caches_if_needed();
    [[nodiscard]] const rendering::TextLayout*
    prepared_draw_text_layout_for(const rendering::RenderCommandList& commands,
                                  std::size_t opcode_index) const noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_state_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> subpixel_text_blend_state_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> stencil_mask_blend_state_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_state_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_state_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stencil_texture_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> stencil_view_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> stencil_disabled_state_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> stencil_test_state_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> stencil_increment_state_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> stencil_decrement_state_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> glyph_atlas_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> glyph_atlas_view_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> primary_deferred_context_;
    std::array<CachedDrawPipeline, static_cast<std::size_t>(PipelineVariant::Count)>
        draw_pipeline_cache_{};

    ID3D11DeviceContext* context_ = nullptr;
    ID3D11RenderTargetView* render_target_ = nullptr;
    float dpi_ = 96.0F;
    float target_dip_width_ = 1.0F;
    float target_dip_height_ = 1.0F;
    std::uint32_t target_pixel_width_ = 1U;
    std::uint32_t target_pixel_height_ = 1U;
    bool stencil_used_this_frame_ = false;
    std::uint32_t stencil_idle_frame_count_ = 0U;
    RecorderState recorder_state_{};
    std::uint64_t frame_sequence_ = 0U;
    std::uint64_t next_cache_prune_frame_ = 32U;
    PrimitivePointCache primitive_point_cache_;
    std::vector<TransformedGeometryFill> transformed_geometry_fill_cache_;
    std::vector<CachedTextGlyphRun> text_glyph_run_cache_;
    std::vector<D3D11RenderDirtyClip> frame_dirty_clips_;
    std::unique_ptr<RenderPlanCacheState> render_plan_cache_;
    std::size_t applied_frame_graph_plan_key_ = 0U;
    std::size_t applied_frame_graph_plan_vertices_ = 0U;
    std::unordered_map<GlyphAtlasKey, GlyphAtlasEntry, GlyphAtlasKeyHash> glyph_atlas_entries_;
    std::unordered_map<TextFaceKey, Microsoft::WRL::ComPtr<IDWriteFontFace>, TextFaceKeyHash>
        text_face_cache_;
    std::vector<PreparedDrawTextLayoutSnapshot> prepared_draw_text_layouts_;
    std::unordered_set<std::size_t> prepared_text_layout_resource_keys_;
    std::vector<std::byte> glyph_atlas_pixels_;
    std::uint32_t glyph_atlas_width_ = 0U;
    std::uint32_t glyph_atlas_height_ = 0U;
    std::uint32_t glyph_atlas_cursor_x_ = 0U;
    std::uint32_t glyph_atlas_cursor_y_ = 0U;
    std::uint32_t glyph_atlas_row_height_ = 0U;
    std::uint32_t glyph_atlas_generation_ = 0U;
    bool glyph_atlas_dirty_ = false;
    std::uint32_t glyph_atlas_dirty_left_ = 0U;
    std::uint32_t glyph_atlas_dirty_top_ = 0U;
    std::uint32_t glyph_atlas_dirty_right_ = 0U;
    std::uint32_t glyph_atlas_dirty_bottom_ = 0U;
    std::vector<std::shared_ptr<D3D11DisplayListRenderer>> worker_recorders_;
    mutable std::mutex worker_recorder_mutex_;
    RenderTimingMetrics last_timing_metrics_{};
    bool parallel_recording_enabled_ = false;
    bool resource_updates_allowed_ = true;
};

} // namespace winelement::platform::win32
