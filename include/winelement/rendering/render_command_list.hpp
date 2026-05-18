#pragma once

#include <winelement/rendering/render_context.hpp>
#include <winelement/rendering/render_types.hpp>
#include <winelement/rendering/text_engine.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace winelement::rendering {

template <typename> inline constexpr bool dependent_false_v = false;

struct SaveCommand {};
struct RestoreCommand {};
struct PopClipCommand {};

struct PreparedGeometryFlatten {
    std::vector<std::vector<layout::Point>> figures;
    layout::Rect bounds{};
};

struct PreparedGeometryFill {
    std::shared_ptr<const PreparedGeometryFlatten> flatten;
    std::vector<std::vector<layout::Point>> filled_contours;
    std::vector<layout::Point> tessellated_vertices;
    layout::Rect bounds{};
};

struct PreparedGeometryStroke {
    std::shared_ptr<const PreparedGeometryFlatten> flatten;
    layout::Rect bounds{};
};

struct PreparedTextGlyphCoverage {
    std::string font_family;
    std::uint32_t glyph_index = 0U;
    std::uint32_t font_size_key = 0U;
    std::uint16_t font_weight = 400U;
    std::uint16_t font_stretch = 5U;
    std::uint8_t font_style = 0U;
    bool is_right_to_left = false;
    int left = 0;
    int top = 0;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::byte> pixels;
};

struct PreparedTextGlyphCoverageList {
    std::vector<std::shared_ptr<const PreparedTextGlyphCoverage>> glyphs;
    std::vector<std::shared_ptr<const PreparedTextGlyphCoverage>> glyphs_by_layout_index;
    std::unordered_map<std::size_t, std::vector<std::shared_ptr<const PreparedTextGlyphCoverage>>>
        glyphs_by_hash;
};

class PreparedRenderCache final {
  public:
    [[nodiscard]] std::shared_ptr<const PreparedGeometryFlatten>
    prepared_geometry_flatten(const Geometry& geometry);
    [[nodiscard]] std::shared_ptr<const PreparedGeometryFill>
    prepared_geometry_fill(const Geometry& geometry);
    [[nodiscard]] std::shared_ptr<const PreparedGeometryStroke>
    prepared_geometry_stroke(const Geometry& geometry);
    [[nodiscard]] std::shared_ptr<const PreparedTextGlyphCoverageList>
    prepared_text_glyph_coverages(const TextLayout& layout);
    void merge(const PreparedRenderCache& other);

  private:
    struct PreparedGeometryFlattenEntry {
        std::string canonical_key;
        Geometry geometry;
        std::shared_ptr<const PreparedGeometryFlatten> prepared;
    };

    struct PreparedGeometryFillEntry {
        std::string canonical_key;
        Geometry geometry;
        std::shared_ptr<const PreparedGeometryFill> prepared;
    };

    struct PreparedGeometryStrokeEntry {
        std::string canonical_key;
        Geometry geometry;
        std::shared_ptr<const PreparedGeometryStroke> prepared;
    };

    std::unordered_map<std::size_t, std::vector<PreparedGeometryFlattenEntry>>
        prepared_geometry_flatten_cache_;
    std::unordered_map<std::size_t, std::vector<PreparedGeometryFillEntry>>
        prepared_geometry_fill_cache_;
    std::unordered_map<std::size_t, std::vector<PreparedGeometryStrokeEntry>>
        prepared_geometry_stroke_cache_;
    std::unordered_map<std::size_t, std::vector<std::shared_ptr<const PreparedTextGlyphCoverage>>>
        prepared_text_glyph_cache_;
};

struct PushClipCommand {
    layout::Rect rect{};
};

struct PushGeometryClipCommand {
    Geometry geometry{};
    std::shared_ptr<const PreparedGeometryFill> prepared_fill;
};

struct PopGeometryClipCommand {};

struct PushLayerCommand {
    RenderLayerOptions options{};
};

struct PopLayerCommand {};

struct DrawLineCommand {
    layout::Point start{};
    layout::Point end{};
    Color color{};
    float stroke_width = 1.0F;
};

struct FillRectCommand {
    layout::Rect rect{};
    Color color{};
};

struct FillPixelSnappedRectCommand {
    layout::Rect rect{};
    Color color{};
};

struct StrokePixelSnappedRectCommand {
    layout::Rect rect{};
    Color color{};
    float stroke_width = 1.0F;
};

struct StrokeRectCommand {
    layout::Rect rect{};
    Color color{};
    float stroke_width = 1.0F;
};

struct FillRoundedRectCommand {
    layout::Rect rect{};
    CornerRadius radius{};
    Color color{};
};

struct StrokeRoundedRectCommand {
    layout::Rect rect{};
    CornerRadius radius{};
    Color color{};
    float stroke_width = 1.0F;
};

struct FillEllipseCommand {
    layout::Rect rect{};
    Color color{};
};

struct StrokeEllipseCommand {
    layout::Rect rect{};
    Color color{};
    float stroke_width = 1.0F;
};

struct FillGeometryCommand {
    Geometry geometry{};
    Color color{};
    std::shared_ptr<const PreparedGeometryFill> prepared_fill;
};

struct StrokeGeometryCommand {
    Geometry geometry{};
    Color color{};
    GeometryStrokeStyle style{};
    std::shared_ptr<const PreparedGeometryStroke> prepared_stroke;
};

struct DrawImageCommand {
    RenderResourceId resource_id{};
    RenderImageOptions options{};
};

struct DrawTextCommand {
    std::string text;
    layout::Rect rect{};
    TextStyle style{};
};

struct DrawTextLayoutCommand {
    TextLayout layout{};
    layout::Point origin{};
    std::shared_ptr<const PreparedTextGlyphCoverageList> prepared_glyphs;
};

struct DrawBoxShadowCommand {
    layout::Rect rect{};
    ShadowStyle style{};
};

struct DirtyRegionNode {
    layout::Rect bounds{};
    std::vector<DirtyRegionNode> children;
};

class RenderCommandList;

struct RenderOpcodeRecord {
    RenderCommandType opcode = RenderCommandType::Save;
    std::uint32_t payload_index = 0;
    layout::Rect bounds{};
    const RenderCommandList* owner = nullptr;

    [[nodiscard]] RenderCommandType type() const noexcept {
        return opcode;
    }

    template <typename Payload> [[nodiscard]] const Payload& payload() const;
};

enum class RenderBatchKind { State, Geometry, Text, Image, Effect };

struct RenderDrawBatch {
    RenderBatchKind kind = RenderBatchKind::State;
    layout::Rect bounds{};
    std::uint32_t command_count = 0;
};

struct DirtyRegionOptimizeOptions {
    std::size_t max_rects = 8U;
    float merge_slop = 0.0F;
    bool scanline_merge = true;
};

class DirtyRegion final {
  public:
    void add(layout::Rect rect);
    void add(const DirtyRegion& region);
    void clip(layout::Rect clip_rect);
    void optimize(DirtyRegionOptimizeOptions options = {});
    void cull_occluded(layout::Rect occluder);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const std::vector<layout::Rect>& rects() const noexcept;
    [[nodiscard]] const DirtyRegionNode& tree() const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] layout::Rect bounds() const noexcept;

  private:
    void rebuild_tree();

    std::vector<layout::Rect> rects_;
    DirtyRegionNode tree_;
};

class RenderCommandList final {
  public:
    RenderCommandList();
    explicit RenderCommandList(std::shared_ptr<PreparedRenderCache> prepared_cache);
    RenderCommandList(const RenderCommandList& other);
    RenderCommandList& operator=(const RenderCommandList& other);
    RenderCommandList(RenderCommandList&& other) noexcept;
    RenderCommandList& operator=(RenderCommandList&& other) noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t command_count() const noexcept;
    [[nodiscard]] const std::vector<RenderOpcodeRecord>& commands() const noexcept;
    [[nodiscard]] const std::vector<RenderOpcodeRecord>& opcodes() const noexcept;
    [[nodiscard]] const std::vector<std::uint32_t>& opcode_payload_indices() const noexcept;
    [[nodiscard]] const std::vector<PushClipCommand>& push_clip_payloads() const noexcept;
    [[nodiscard]] const std::vector<PushGeometryClipCommand>&
    push_geometry_clip_payloads() const noexcept;
    [[nodiscard]] const std::vector<PushLayerCommand>& push_layer_payloads() const noexcept;
    [[nodiscard]] const std::vector<DrawLineCommand>& draw_line_payloads() const noexcept;
    [[nodiscard]] const std::vector<FillRectCommand>& fill_rect_payloads() const noexcept;
    [[nodiscard]] const std::vector<FillPixelSnappedRectCommand>&
    fill_pixel_snapped_rect_payloads() const noexcept;
    [[nodiscard]] const std::vector<StrokePixelSnappedRectCommand>&
    stroke_pixel_snapped_rect_payloads() const noexcept;
    [[nodiscard]] const std::vector<StrokeRectCommand>& stroke_rect_payloads() const noexcept;
    [[nodiscard]] const std::vector<FillRoundedRectCommand>&
    fill_rounded_rect_payloads() const noexcept;
    [[nodiscard]] const std::vector<StrokeRoundedRectCommand>&
    stroke_rounded_rect_payloads() const noexcept;
    [[nodiscard]] const std::vector<FillEllipseCommand>& fill_ellipse_payloads() const noexcept;
    [[nodiscard]] const std::vector<StrokeEllipseCommand>& stroke_ellipse_payloads() const noexcept;
    [[nodiscard]] const std::vector<FillGeometryCommand>& fill_geometry_payloads() const noexcept;
    [[nodiscard]] const std::vector<StrokeGeometryCommand>&
    stroke_geometry_payloads() const noexcept;
    [[nodiscard]] const std::vector<DrawImageCommand>& draw_image_payloads() const noexcept;
    [[nodiscard]] const std::vector<DrawTextCommand>& draw_text_payloads() const noexcept;
    [[nodiscard]] const std::vector<DrawTextLayoutCommand>&
    draw_text_layout_payloads() const noexcept;
    [[nodiscard]] const std::vector<DrawBoxShadowCommand>&
    draw_box_shadow_payloads() const noexcept;
    template <typename Payload>
    [[nodiscard]] const Payload& payload(std::size_t opcode_index) const;
    template <typename Payload>
    [[nodiscard]] const Payload& payload_by_index(std::uint32_t payload_index) const;
    [[nodiscard]] std::vector<std::byte> serialized_opcodes() const;
    [[nodiscard]] std::vector<RenderDrawBatch> draw_batches() const;
    [[nodiscard]] layout::Rect bounds() const noexcept;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;
    [[nodiscard]] bool is_equivalent_to(const RenderCommandList& other) const noexcept;
    [[nodiscard]] std::shared_ptr<PreparedRenderCache> prepared_cache() const noexcept;

  private:
    friend class RenderCommandRecorder;

    void append(SaveCommand command);
    void append(RestoreCommand command);
    void append(PushClipCommand command);
    void append(PopClipCommand command);
    void append(PushGeometryClipCommand command);
    void append(PopGeometryClipCommand command);
    void append(PushLayerCommand command);
    void append(PopLayerCommand command);
    void append(DrawLineCommand command);
    void append(FillRectCommand command);
    void append(FillPixelSnappedRectCommand command);
    void append(StrokePixelSnappedRectCommand command);
    void append(StrokeRectCommand command);
    void append(FillRoundedRectCommand command);
    void append(StrokeRoundedRectCommand command);
    void append(FillEllipseCommand command);
    void append(StrokeEllipseCommand command);
    void append(FillGeometryCommand command);
    void append(StrokeGeometryCommand command);
    void append(DrawImageCommand command);
    void append(DrawTextCommand command);
    void append(DrawTextLayoutCommand command);
    void append(DrawBoxShadowCommand command);
    void append(const RenderCommandList& command_list);
    void append_opcode(RenderCommandType type, std::uint32_t payload_index, layout::Rect bounds,
                       std::size_t payload_hash);
    void rebind_opcode_owners() noexcept;
    [[nodiscard]] std::shared_ptr<const PreparedGeometryFlatten>
    cached_prepared_geometry_flatten(const Geometry& geometry);
    [[nodiscard]] std::shared_ptr<const PreparedGeometryFill>
    cached_prepared_geometry_fill(const Geometry& geometry);
    [[nodiscard]] std::shared_ptr<const PreparedGeometryStroke>
    cached_prepared_geometry_stroke(const Geometry& geometry);
    [[nodiscard]] std::shared_ptr<const PreparedTextGlyphCoverageList>
    cached_prepared_text_glyph_coverages(const TextLayout& layout);
    [[nodiscard]] PreparedRenderCache& ensure_prepared_cache();

    std::shared_ptr<PreparedRenderCache> prepared_cache_;
    std::vector<RenderOpcodeRecord> opcodes_;
    std::vector<std::uint32_t> opcode_payload_indices_;
    std::vector<PushClipCommand> push_clip_payloads_;
    std::vector<PushGeometryClipCommand> push_geometry_clip_payloads_;
    std::vector<PushLayerCommand> push_layer_payloads_;
    std::vector<DrawLineCommand> draw_line_payloads_;
    std::vector<FillRectCommand> fill_rect_payloads_;
    std::vector<FillPixelSnappedRectCommand> fill_pixel_snapped_rect_payloads_;
    std::vector<StrokePixelSnappedRectCommand> stroke_pixel_snapped_rect_payloads_;
    std::vector<StrokeRectCommand> stroke_rect_payloads_;
    std::vector<FillRoundedRectCommand> fill_rounded_rect_payloads_;
    std::vector<StrokeRoundedRectCommand> stroke_rounded_rect_payloads_;
    std::vector<FillEllipseCommand> fill_ellipse_payloads_;
    std::vector<StrokeEllipseCommand> stroke_ellipse_payloads_;
    std::vector<FillGeometryCommand> fill_geometry_payloads_;
    std::vector<StrokeGeometryCommand> stroke_geometry_payloads_;
    std::vector<DrawImageCommand> draw_image_payloads_;
    std::vector<DrawTextCommand> draw_text_payloads_;
    std::vector<DrawTextLayoutCommand> draw_text_layout_payloads_;
    std::vector<DrawBoxShadowCommand> draw_box_shadow_payloads_;
    layout::Rect bounds_{};
    std::uint64_t fingerprint_ = 0;
};

class RenderCommandRecorder final : public RenderContext {
  public:
    RenderCommandRecorder() = default;
    explicit RenderCommandRecorder(std::shared_ptr<PreparedRenderCache> prepared_cache);

    void save() override;
    void restore() override;
    void push_clip(layout::Rect rect) override;
    void pop_clip() override;
    void push_geometry_clip(const Geometry& geometry) override;
    void pop_geometry_clip() override;
    void push_layer(const RenderLayerOptions& options) override;
    void pop_layer() override;
    void draw_line(layout::Point start, layout::Point end, Color color,
                   float stroke_width) override;
    void fill_rect(layout::Rect rect, Color color) override;
    void fill_pixel_snapped_rect(layout::Rect rect, Color color) override;
    void stroke_pixel_snapped_rect(layout::Rect rect, Color color, float stroke_width) override;
    void stroke_rect(layout::Rect rect, Color color, float stroke_width) override;
    void fill_rounded_rect(layout::Rect rect, CornerRadius radius, Color color) override;
    void stroke_rounded_rect(layout::Rect rect, CornerRadius radius, Color color,
                             float stroke_width) override;
    void fill_ellipse(layout::Rect rect, Color color) override;
    void stroke_ellipse(layout::Rect rect, Color color, float stroke_width) override;
    void fill_geometry(const Geometry& geometry, Color color) override;
    void stroke_geometry(const Geometry& geometry, Color color,
                         const GeometryStrokeStyle& style) override;
    void draw_image(RenderResourceId resource_id, const RenderImageOptions& options) override;
    void draw_box_shadow(layout::Rect rect, const ShadowStyle& style) override;
    void draw_text(std::string_view text, layout::Rect rect, const TextStyle& style) override;
    void draw_text_layout(const TextLayout& layout, layout::Point origin) override;

    [[nodiscard]] const std::vector<RenderOpcodeRecord>& commands() const noexcept;
    [[nodiscard]] const RenderCommandList& command_list() const noexcept;
    template <typename Payload>
    [[nodiscard]] const Payload& payload(std::size_t opcode_index) const {
        return command_list_.payload<Payload>(opcode_index);
    }
    [[nodiscard]] RenderCommandList take_command_list() noexcept;
    void append(const RenderCommandList& command_list);

  private:
    RenderCommandList command_list_;
};

} // namespace winelement::rendering

namespace winelement::rendering {

template <typename Payload>
const Payload& RenderCommandList::payload(std::size_t opcode_index) const {
    const auto payload_index = opcodes_.at(opcode_index).payload_index;
    return payload_by_index<Payload>(payload_index);
}

template <typename Payload>
const Payload& RenderCommandList::payload_by_index(std::uint32_t payload_index) const {
    if constexpr (std::is_same_v<Payload, PushClipCommand>) {
        return push_clip_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, PushGeometryClipCommand>) {
        return push_geometry_clip_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, PushLayerCommand>) {
        return push_layer_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, DrawLineCommand>) {
        return draw_line_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, FillRectCommand>) {
        return fill_rect_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, FillPixelSnappedRectCommand>) {
        return fill_pixel_snapped_rect_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, StrokePixelSnappedRectCommand>) {
        return stroke_pixel_snapped_rect_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, StrokeRectCommand>) {
        return stroke_rect_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, FillRoundedRectCommand>) {
        return fill_rounded_rect_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, StrokeRoundedRectCommand>) {
        return stroke_rounded_rect_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, FillEllipseCommand>) {
        return fill_ellipse_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, StrokeEllipseCommand>) {
        return stroke_ellipse_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, FillGeometryCommand>) {
        return fill_geometry_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, StrokeGeometryCommand>) {
        return stroke_geometry_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, DrawImageCommand>) {
        return draw_image_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, DrawTextCommand>) {
        return draw_text_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, DrawTextLayoutCommand>) {
        return draw_text_layout_payloads_.at(payload_index);
    } else if constexpr (std::is_same_v<Payload, DrawBoxShadowCommand>) {
        return draw_box_shadow_payloads_.at(payload_index);
    } else {
        static_assert(dependent_false_v<Payload>, "Unsupported render command payload type");
    }
}

template <typename Payload> const Payload& RenderOpcodeRecord::payload() const {
    return owner->payload_by_index<Payload>(payload_index);
}

} // namespace winelement::rendering