#pragma once

#include <winelement/rendering/render_types.hpp>

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winelement::rendering {

struct TextGlyph {
    std::string font_family;
    std::size_t byte_offset = 0;
    std::size_t byte_length = 0;
    std::uint32_t glyph_index = 0;
    bool is_missing_glyph = false;
    std::uint32_t cluster_index = 0;
    layout::Point origin{};
    float advance = 0.0F;
    layout::Point advance_offset{};
    bool is_cluster_start = true;
    bool is_right_to_left = false;
};

struct TextCluster {
    std::size_t byte_offset = 0;
    std::size_t byte_length = 0;
    layout::Point origin{};
    layout::Size size{};
    float advance = 0.0F;
    std::uint32_t line_index = 0;
    bool is_whitespace = false;
    bool is_newline = false;
    bool is_right_to_left = false;
};

struct TextLine {
    std::size_t byte_offset = 0;
    std::size_t byte_length = 0;
    layout::Rect rect{};
    float baseline = 0.0F;
    bool has_trailing_newline = false;
};

struct TextLayoutOptions {
    float max_width = 0.0F;
    float max_height = 0.0F;
    std::size_t max_lines = 0;
    float pixels_per_dip = 1.0F;
    std::optional<layout::Rect> viewport;

    [[nodiscard]] friend bool operator==(const TextLayoutOptions& left,
                                         const TextLayoutOptions& right) noexcept {
        const auto same_viewport =
            (!left.viewport && !right.viewport) ||
            (left.viewport && right.viewport && left.viewport->x == right.viewport->x &&
             left.viewport->y == right.viewport->y &&
             left.viewport->width == right.viewport->width &&
             left.viewport->height == right.viewport->height);
        return left.max_width == right.max_width && left.max_height == right.max_height &&
               left.max_lines == right.max_lines && left.pixels_per_dip == right.pixels_per_dip &&
               same_viewport;
    }
};

struct TextSelectionRange {
    std::size_t start_byte_offset = 0;
    std::size_t end_byte_offset = 0;
};

struct TextLineRange {
    std::uint32_t start_line_index = 0;
    std::uint32_t end_line_index = 0;
};

struct TextHitTestResult {
    std::size_t byte_offset = 0;
    std::uint32_t line_index = 0;
    layout::Point point{};
    bool is_inside_text = false;
    bool is_trailing_hit = false;
};

struct TextCaretMetrics {
    std::size_t byte_offset = 0;
    std::uint32_t line_index = 0;
    layout::Rect rect{};
};

enum class TextNormalizationForm {
    CanonicalComposition,
    CanonicalDecomposition,
    CompatibilityComposition,
    CompatibilityDecomposition
};

struct TextFontFaceInfo {
    std::string family_name;
    std::string face_name;
    std::uint16_t weight = 400;
    std::uint16_t stretch = 5;
    FontStyle style = FontStyle::Normal;
    bool is_symbol_font = false;
    bool has_bold_simulation = false;
    bool has_oblique_simulation = false;
};

struct TextFontFamilyInfo {
    std::string family_name;
    std::vector<TextFontFaceInfo> faces;
};

struct TextFontMetrics {
    float em_size = 0.0F;
    float design_units_per_em = 0.0F;
    float ascent = 0.0F;
    float descent = 0.0F;
    float line_gap = 0.0F;
    float line_height = 0.0F;
    float cap_height = 0.0F;
    float x_height = 0.0F;
    float underline_position = 0.0F;
    float underline_thickness = 0.0F;
    float strikethrough_position = 0.0F;
    float strikethrough_thickness = 0.0F;
    bool has_bold_simulation = false;
    bool has_oblique_simulation = false;
};

struct TextCaretStop {
    std::size_t byte_offset = 0;
    std::uint32_t line_index = 0;
    layout::Point origin{};
    float height = 0.0F;
};

struct TextDecorationRun {
    TextDecorationLine line = TextDecorationLine::None;
    std::uint32_t line_index = 0;
    layout::Rect rect{};
    bool is_right_to_left = false;
};

struct TextLayout {
    std::string text;
    TextStyle style{};
    TextLayoutOptions options{};
    layout::Size size{};
    float baseline = 0.0F;
    std::vector<TextGlyph> glyphs;
    std::vector<TextCluster> clusters;
    std::vector<TextLine> lines;
    std::vector<TextCaretStop> caret_stops;
    std::vector<TextDecorationRun> decorations;
    std::size_t visible_byte_start = 0;
    std::size_t visible_byte_end = 0;
    TextLineRange visible_line_range{};
    bool truncated = false;
    bool has_missing_glyphs = false;
};

class TextEngine final {
  public:
    [[nodiscard]] TextLayout layout_text(std::string_view text, const TextStyle& style,
                                         TextLayoutOptions options = {}) const;
    [[nodiscard]] std::string
    normalize_text(std::string_view text,
                   TextNormalizationForm form = TextNormalizationForm::CanonicalComposition) const;
    [[nodiscard]] TextLayout shape_single_line(std::string_view text, const TextStyle& style) const;
    [[nodiscard]] layout::Size measure(std::string_view text, const TextStyle& style,
                                       TextLayoutOptions options = {}) const;
    [[nodiscard]] layout::Size measure_single_line(std::string_view text,
                                                   const TextStyle& style) const;
    [[nodiscard]] float caret_x_for_byte_offset(const TextLayout& layout,
                                                std::size_t byte_offset) const noexcept;
    [[nodiscard]] layout::Point
    caret_position_for_byte_offset(const TextLayout& layout,
                                   std::size_t byte_offset) const noexcept;
    [[nodiscard]] std::size_t hit_test_byte_offset(const TextLayout& layout,
                                                   float x) const noexcept;
    [[nodiscard]] std::size_t hit_test_byte_offset(const TextLayout& layout,
                                                   layout::Point point) const noexcept;
    [[nodiscard]] TextHitTestResult hit_test_point(const TextLayout& layout,
                                                   layout::Point point) const noexcept;
    [[nodiscard]] TextCaretMetrics
    caret_metrics_for_byte_offset(const TextLayout& layout, std::size_t byte_offset) const noexcept;
    [[nodiscard]] std::size_t caret_offset_for_visual_left(const TextLayout& layout,
                                                           std::size_t byte_offset) const noexcept;
    [[nodiscard]] std::size_t caret_offset_for_visual_right(const TextLayout& layout,
                                                            std::size_t byte_offset) const noexcept;
    [[nodiscard]] std::size_t previous_cluster_boundary(const TextLayout& layout,
                                                        std::size_t byte_offset) const noexcept;
    [[nodiscard]] std::size_t next_cluster_boundary(const TextLayout& layout,
                                                    std::size_t byte_offset) const noexcept;
    [[nodiscard]] TextSelectionRange cluster_aligned_range(const TextLayout& layout,
                                                           TextSelectionRange range) const noexcept;
    [[nodiscard]] std::uint32_t line_index_for_byte_offset(const TextLayout& layout,
                                                           std::size_t byte_offset) const noexcept;
    [[nodiscard]] TextSelectionRange visible_byte_range(const TextLayout& layout) const noexcept;
    [[nodiscard]] TextLineRange visible_line_range(const TextLayout& layout) const noexcept;
    [[nodiscard]] std::vector<layout::Rect>
    selection_rects(const TextLayout& layout, TextSelectionRange range,
                    std::optional<layout::Rect> viewport = std::nullopt) const;
    [[nodiscard]] std::vector<TextFontFamilyInfo> system_font_families() const;
    [[nodiscard]] std::optional<TextFontFaceInfo> match_font(const TextStyle& style) const;
    [[nodiscard]] TextFontMetrics font_metrics(const TextStyle& style) const;
    void clear_cache() const;
    void clear_font_cache() const;
    void set_max_cached_layouts(std::size_t max_cached_layouts);
    [[nodiscard]] std::size_t max_cached_layouts() const;
    [[nodiscard]] std::size_t cached_layout_count() const;

  private:
    struct CachedLayout {
        std::string text;
        TextStyle style{};
        TextLayoutOptions options{};
        TextLayout layout{};
    };

    [[nodiscard]] TextLayout layout_text_uncached(std::string_view text, const TextStyle& style,
                                                  TextLayoutOptions options) const;

    mutable std::vector<CachedLayout> cache_;
    mutable std::mutex cache_mutex_;
    mutable std::optional<std::vector<TextFontFamilyInfo>> system_font_cache_;
    mutable std::mutex font_cache_mutex_;
    std::size_t max_cached_layouts_ = 64;
};

[[nodiscard]] bool is_utf8_continuation_byte(unsigned char value) noexcept;
[[nodiscard]] std::size_t previous_utf8_boundary(std::string_view text,
                                                 std::size_t byte_offset) noexcept;
[[nodiscard]] std::size_t next_utf8_boundary(std::string_view text,
                                             std::size_t byte_offset) noexcept;
[[nodiscard]] std::size_t clamp_utf8_boundary(std::string_view text,
                                              std::size_t byte_offset) noexcept;
[[nodiscard]] std::size_t utf8_code_point_count(std::string_view text) noexcept;

} // namespace winelement::rendering
