#include <winelement/rendering.hpp>
#include <winelement/rendering/svg_path_parser.hpp>

#include "d3d11_display_list_renderer.hpp"
#include "d3d11_render_device.hpp"
#include "d3d11_render_resource_cache.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <combaseapi.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace core = winelement::core;
namespace layout = winelement::rendering::layout;
namespace rendering = winelement::rendering;
namespace win32 = winelement::platform::win32;

constexpr std::uint32_t canvas_width = 1280U;
constexpr std::uint32_t canvas_height = 3380U;
constexpr float dpi = 96.0F;
constexpr auto demo_texture_id = rendering::RenderResourceId{1001U};
constexpr auto demo_circle_texture_id = rendering::RenderResourceId{1002U};
constexpr float page_margin = 40.0F;
constexpr float panel_gap = 28.0F;
constexpr float panel_width = static_cast<float>(canvas_width) - page_margin * 2.0F;

constexpr std::string_view message_icon_outline_svg =
    "M128 224v512a64 64 0 0 0 64 64h640a64 64 0 0 0 64-64V224zm0-64h768a64 64 0 0 1 64 "
    "64v512a128 128 0 0 1-128 128H192A128 128 0 0 1 64 736V224a64 64 0 0 1 64-64";
constexpr std::string_view message_icon_flap_svg =
    "M904 224 656.512 506.88a192 192 0 0 1-289.024 0L120 224zm-698.944 0 210.56 "
    "240.704a128 128 0 0 0 192.704 0L818.944 224z";
constexpr std::array<std::string_view, 2> message_svg = {message_icon_outline_svg,
                                                         message_icon_flap_svg};
constexpr std::array<std::string_view, 1> info_filled_svg = {
    "M512 64a448 448 0 1 1 0 896.064A448 448 0 0 1 512 64m67.2 275.072c33.28 0 60.288-23.104 "
    "60.288-57.344s-27.072-57.344-60.288-57.344c-33.28 0-60.16 23.104-60.16 57.344s26.88 "
    "57.344 60.16 57.344M590.912 699.2c0-6.848 2.368-24.64 1.024-34.752l-52.608 60.544c-10.88 "
    "11.456-24.512 19.392-30.912 17.28a12.99 12.99 0 0 1-8.256-14.72l87.68-276.992c7.168-35.136"
    "-12.544-67.2-54.336-71.296-44.096 0-108.992 44.736-148.48 101.504 0 6.784-1.28 23.68.064 "
    "33.792l52.544-60.608c10.88-11.328 23.552-19.328 29.952-17.152a12.8 12.8 0 0 1 7.808 "
    "16.128L388.48 728.576c-10.048 32.256 8.96 63.872 55.04 71.04 67.84 0 107.904-43.648 "
    "147.456-100.416z"};
constexpr std::array<std::string_view, 1> success_filled_svg = {
    "M512 64a448 448 0 1 1 0 896 448 448 0 0 1 0-896m-55.808 536.384-99.52-99.584a38.4 38.4 0 "
    "1 0-54.336 54.336l126.72 126.72a38.27 38.27 0 0 0 54.336 0l262.4-262.464a38.4 38.4 0 1 "
    "0-54.272-54.336z"};
constexpr std::array<std::string_view, 1> warning_filled_svg = {
    "M512 64a448 448 0 1 1 0 896 448 448 0 0 1 0-896m0 192a58.43 58.43 0 0 0-58.24 "
    "63.744l23.36 256.384a35.072 35.072 0 0 0 69.76 0l23.296-256.384A58.43 58.43 0 0 0 512 "
    "256m0 512a51.2 51.2 0 1 0 0-102.4 51.2 51.2 0 0 0 0 102.4"};
constexpr std::array<std::string_view, 1> circle_close_filled_svg = {
    "M512 64a448 448 0 1 1 0 896 448 448 0 0 1 0-896m0 393.664L407.936 353.6a38.4 38.4 0 1 "
    "0-54.336 54.336L457.664 512 353.6 616.064a38.4 38.4 0 1 0 54.336 54.336L512 566.336 "
    "616.064 670.4a38.4 38.4 0 1 0 54.336-54.336L566.336 512 670.4 407.936a38.4 38.4 0 1 "
    "0-54.336-54.336z"};
constexpr std::array<std::string_view, 1> close_svg = {
    "M764.288 214.592 512 466.88 259.712 214.592a31.936 31.936 0 0 0-45.12 45.12L466.752 512 "
    "214.528 764.224a31.936 31.936 0 1 0 45.12 45.184L512 557.184l252.288 252.288a31.936 "
    "31.936 0 0 0 45.12-45.12L557.12 512.064l252.288-252.352a31.936 31.936 0 1 "
    "0-45.12-45.184z"};
constexpr std::string_view svg_straight_line_path = "M0 60 L220 60";
constexpr std::string_view svg_polyline_path = "M0 58 L48 18 L104 98 L168 36 L236 68";
constexpr std::string_view svg_quadratic_path = "M0 92 Q110 4 220 92";
constexpr std::string_view svg_cubic_path = "M0 82 C44 6 182 122 236 84";
constexpr std::string_view svg_arc_path = "M8 94 A92 54 18 1 1 228 94";
constexpr std::string_view svg_circle_path =
    "M50 0 A50 50 0 1 1 50 100 A50 50 0 1 1 50 0";
constexpr std::string_view svg_dense_diagonal_path =
    "M8 10 L112 78 M8 34 L128 118 M20 8 L140 92 M144 10 L44 78 M144 34 L20 110 M132 8 L8 92";
constexpr std::string_view svg_sharp_join_path =
    "M8 104 L52 18 L88 104 L122 32 L156 104 L192 14 L228 104";

class ScopedComApartment final {
  public:
    ScopedComApartment() {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized_ = SUCCEEDED(result_);
        if (result_ == RPC_E_CHANGED_MODE) {
            initialized_ = false;
            result_ = S_OK;
        }
        if (FAILED(result_)) {
            throw std::runtime_error("failed to initialize COM apartment");
        }
    }

    ~ScopedComApartment() {
        if (initialized_) {
            CoUninitialize();
        }
    }

    ScopedComApartment(const ScopedComApartment&) = delete;
    ScopedComApartment& operator=(const ScopedComApartment&) = delete;

  private:
    HRESULT result_ = S_OK;
    bool initialized_ = false;
};

struct RenderTargetBundle {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_view;
};

struct DemoVisualRegions {
    layout::Rect full_canvas{};
    layout::Rect header{};
    layout::Rect primitives_panel{};
    layout::Rect primitive_shapes{};
    layout::Rect primitive_edges{};
    layout::Rect primitive_curves{};
    layout::Rect geometry_panel{};
    layout::Rect geometry_feature_mix{};
    layout::Rect geometry_clip_image{};
    layout::Rect geometry_large_message{};
    layout::Rect geometry_message_board{};
    layout::Rect svg_panel{};
    layout::Rect svg_detail_board{};
    layout::Rect svg_scale_board{};
    layout::Rect text_panel{};
    layout::Rect text_content{};
    layout::Rect text_selection{};
    layout::Rect text_image{};
    layout::Rect text_layer{};
};

struct DemoArtifacts {
    rendering::RenderCommandList commands;
    rendering::RenderScene scene;
    rendering::DirtyRegion dirty_region;
    rendering::RenderFrameGraph frame_graph;
    rendering::CompositorPromotionPlan promotion_plan;
    rendering::CompositorFrame compositor_frame;
    rendering::RenderResourceUpload image_upload;
    rendering::RenderResourceUpload circle_image_upload;
    rendering::TextLayout text_layout;
    std::vector<layout::Rect> selection_rects;
    rendering::TextHitTestResult hit_test;
    rendering::TextCaretMetrics caret_metrics;
    DemoVisualRegions visual_regions;
    rendering::Color clear_color = rendering::Color::rgba(245, 247, 250);
};

[[nodiscard]] std::runtime_error make_hresult_error(std::string_view message, HRESULT result) {
    std::ostringstream stream;
    stream << message << " HRESULT=0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result);
    return std::runtime_error(stream.str());
}

[[nodiscard]] core::Point polar_point(core::Point center, float radius, float angle) {
    return core::Point{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
}

[[nodiscard]] rendering::Geometry transform_geometry(const rendering::Geometry& source, float scale,
                                                     core::Point offset) {
    auto geometry = source;
    for (auto& figure : geometry.figures) {
        figure.start = {figure.start.x * scale + offset.x, figure.start.y * scale + offset.y};
        for (auto& segment : figure.segments) {
            segment.point = {segment.point.x * scale + offset.x, segment.point.y * scale + offset.y};
            segment.control_point1 = {segment.control_point1.x * scale + offset.x,
                                      segment.control_point1.y * scale + offset.y};
            segment.control_point2 = {segment.control_point2.x * scale + offset.x,
                                      segment.control_point2.y * scale + offset.y};
            segment.radius = {segment.radius.width * scale, segment.radius.height * scale};
        }
    }
    return geometry;
}

[[nodiscard]] rendering::Geometry make_open_quadratic_curve() {
    rendering::Geometry geometry;
    rendering::GeometryFigure figure;
    figure.start = {0.0F, 58.0F};
    figure.begin = rendering::GeometryFigureBegin::Hollow;
    figure.end = rendering::GeometryFigureEnd::Open;
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::QuadraticBezier,
        .point = {204.0F, 58.0F},
        .control_point1 = {102.0F, -22.0F}});
    geometry.figures.push_back(std::move(figure));
    return geometry;
}

[[nodiscard]] rendering::Geometry make_open_cubic_curve() {
    rendering::Geometry geometry;
    rendering::GeometryFigure figure;
    figure.start = {0.0F, 82.0F};
    figure.begin = rendering::GeometryFigureBegin::Hollow;
    figure.end = rendering::GeometryFigureEnd::Open;
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::CubicBezier,
        .point = {236.0F, 84.0F},
        .control_point1 = {44.0F, -30.0F},
        .control_point2 = {182.0F, 192.0F}});
    geometry.figures.push_back(std::move(figure));
    return geometry;
}

[[nodiscard]] rendering::Geometry make_open_arc_curve() {
    rendering::Geometry geometry;
    rendering::GeometryFigure figure;
    figure.start = {0.0F, 90.0F};
    figure.begin = rendering::GeometryFigureBegin::Hollow;
    figure.end = rendering::GeometryFigureEnd::Open;
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Arc,
        .point = {220.0F, 90.0F},
        .radius = {86.0F, 48.0F},
        .rotation_angle = 18.0F,
        .arc_size = rendering::GeometryArcSize::Large,
        .sweep_direction = rendering::GeometryArcSweepDirection::Clockwise});
    geometry.figures.push_back(std::move(figure));
    return geometry;
}

[[nodiscard]] rendering::Geometry make_open_polyline_path() {
    rendering::Geometry geometry;
    rendering::GeometryFigure figure;
    figure.start = {0.0F, 52.0F};
    figure.begin = rendering::GeometryFigureBegin::Hollow;
    figure.end = rendering::GeometryFigureEnd::Open;
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Line,
        .point = {48.0F, 12.0F}});
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Line,
        .point = {104.0F, 92.0F}});
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Line,
        .point = {168.0F, 30.0F}});
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Line,
        .point = {236.0F, 62.0F}});
    geometry.figures.push_back(std::move(figure));
    return geometry;
}

[[nodiscard]] rendering::Geometry make_message_icon_outline_geometry(float scale,
                                                                     core::Point offset) {
    return transform_geometry(rendering::parse_svg_path(message_icon_outline_svg), scale, offset);
}

[[nodiscard]] rendering::Geometry make_message_icon_flap_geometry(float scale, core::Point offset) {
    return transform_geometry(rendering::parse_svg_path(message_icon_flap_svg), scale, offset);
}

struct MessageDemoPalette {
    rendering::Color accent{};
    rendering::Color background{};
    rendering::Color border{};
    rendering::Color text{};
};

struct MessageDemoIconSpec {
    std::string_view label;
    std::span<const std::string_view> paths;
    MessageDemoPalette palette;
};

struct SvgPathStressSpec {
    std::string_view label;
    std::string_view path;
    rendering::Color color;
    float view_width;
    float view_height;
};

void draw_svg_icon(rendering::RenderCommandRecorder& recorder,
                   std::span<const std::string_view> paths, core::Point offset, float size,
                   rendering::Color color) {
    const auto scale = size / 1024.0F;
    for (const auto path : paths) {
        recorder.fill_geometry(transform_geometry(rendering::parse_svg_path(path), scale, offset), color);
    }
}

void draw_svg_icon_in_box(rendering::RenderCommandRecorder& recorder,
                          std::span<const std::string_view> paths, layout::Rect rect,
                          rendering::Color color, float content_scale = 1.0F) {
    const auto size = std::min(rect.width, rect.height) * content_scale;
    draw_svg_icon(recorder, paths,
                  {rect.x + (rect.width - size) * 0.5F, rect.y + (rect.height - size) * 0.5F},
                  size, color);
}

void draw_svg_path_in_rect(rendering::RenderCommandRecorder& recorder, std::string_view path,
                           layout::Rect rect, float source_width, float source_height,
                           rendering::Color color,
                           const rendering::GeometryStrokeStyle& stroke_style) {
    const auto scale = std::min(rect.width / source_width, rect.height / source_height);
    const auto offset = core::Point{rect.x + (rect.width - source_width * scale) * 0.5F,
                                    rect.y + (rect.height - source_height * scale) * 0.5F};
    recorder.stroke_geometry(transform_geometry(rendering::parse_svg_path(path), scale, offset),
                             color, stroke_style);
}

void draw_svg_path_probe(rendering::RenderCommandRecorder& recorder, std::string_view path,
                         layout::Rect container, float source_width, float source_height,
                         float probe_size, rendering::Color color,
                         const rendering::GeometryStrokeStyle& stroke_style) {
    const auto scale = std::min(probe_size / source_width, probe_size / source_height);
    const auto probe_rect = layout::Rect{
        container.x + (container.width - source_width * scale) * 0.5F,
        container.y + (container.height - source_height * scale) * 0.5F,
        source_width * scale,
        source_height * scale};
    draw_svg_path_in_rect(recorder, path, probe_rect, source_width, source_height, color, stroke_style);
}

void draw_svg_stress_cell(rendering::RenderCommandRecorder& recorder, layout::Rect rect,
                          const SvgPathStressSpec& spec,
                          const rendering::GeometryStrokeStyle& stroke_style,
                          std::string_view width_label) {
    recorder.fill_rounded_rect(rect, core::CornerRadius::uniform(14.0F),
                               rendering::Color::rgba(250, 251, 253));
    recorder.stroke_rounded_rect(rect, core::CornerRadius::uniform(14.0F),
                                 rendering::Color::rgba(226, 231, 238), 1.0F);
    recorder.draw_text(std::string(spec.label), {rect.x + 14.0F, rect.y + 12.0F, rect.width - 110.0F, 20.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 14.0F,
                                            .color = rendering::Color::rgba(58, 70, 90)});
    recorder.draw_text(std::string(width_label), {rect.x + rect.width - 80.0F, rect.y + 12.0F, 64.0F, 20.0F},
                       rendering::TextStyle{.font_family = "Segoe UI",
                                            .locale = "en-us",
                                            .font_size = 12.0F,
                                            .color = rendering::Color::rgba(116, 126, 142),
                                            .alignment = rendering::TextAlignment::End});
    draw_svg_path_in_rect(recorder, spec.path,
                          {rect.x + 16.0F, rect.y + 42.0F, rect.width - 32.0F, rect.height - 58.0F},
                          spec.view_width, spec.view_height, spec.color, stroke_style);
}

void draw_svg_scale_probe_tile(rendering::RenderCommandRecorder& recorder, layout::Rect rect,
                               const SvgPathStressSpec& spec,
                               const rendering::GeometryStrokeStyle& stroke_style) {
    recorder.fill_rounded_rect(rect, core::CornerRadius::uniform(14.0F),
                               rendering::Color::rgba(249, 250, 252));
    recorder.stroke_rounded_rect(rect, core::CornerRadius::uniform(14.0F),
                                 rendering::Color::rgba(226, 231, 238), 1.0F);
    recorder.draw_text(std::string(spec.label), {rect.x + 14.0F, rect.y + 12.0F, rect.width - 28.0F, 20.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 14.0F,
                                            .color = rendering::Color::rgba(58, 70, 90)});
    constexpr std::array<float, 3> probe_sizes = {16.0F, 20.0F, 24.0F};
    constexpr float probe_box_size = 52.0F;
    constexpr float probe_gap = 12.0F;
    const auto total_width = probe_box_size * static_cast<float>(probe_sizes.size()) +
                             probe_gap * static_cast<float>(probe_sizes.size() - 1U);
    const auto start_x = rect.x + (rect.width - total_width) * 0.5F;
    for (std::size_t index = 0; index < probe_sizes.size(); ++index) {
        const auto cell_x = start_x + static_cast<float>(index) * (probe_box_size + probe_gap);
        const auto box = layout::Rect{cell_x, rect.y + 42.0F, probe_box_size, probe_box_size};
        recorder.fill_rounded_rect(box, core::CornerRadius::uniform(10.0F),
                                   rendering::Color::rgba(255, 255, 255, 232));
        recorder.stroke_rounded_rect(box, core::CornerRadius::uniform(10.0F),
                                     rendering::Color::rgba(223, 228, 236), 1.0F);
        draw_svg_path_probe(recorder, spec.path, box, spec.view_width, spec.view_height,
                            probe_sizes[index], spec.color, stroke_style);
        recorder.draw_text(std::to_string(static_cast<int>(probe_sizes[index])) + "px",
                           {cell_x, rect.y + 100.0F, probe_box_size, 16.0F},
                           rendering::TextStyle{.font_family = "Segoe UI",
                                                .locale = "en-us",
                                                .font_size = 11.0F,
                                                .color = rendering::Color::rgba(118, 128, 145),
                                                .alignment = rendering::TextAlignment::Center});
    }
}

void draw_svg_probe_tile(rendering::RenderCommandRecorder& recorder, layout::Rect rect,
                         const MessageDemoIconSpec& spec) {
    recorder.fill_rounded_rect(rect, core::CornerRadius::uniform(14.0F), spec.palette.background);
    recorder.stroke_rounded_rect(rect, core::CornerRadius::uniform(14.0F), spec.palette.border, 1.0F);
    recorder.draw_text(std::string(spec.label), {rect.x + 16.0F, rect.y + 12.0F, rect.width - 32.0F, 20.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 15.0F,
                                            .color = spec.palette.text});
    constexpr std::array<float, 4> probe_sizes = {16.0F, 20.0F, 24.0F, 32.0F};
    constexpr float cell_size = 40.0F;
    constexpr float cell_gap = 12.0F;
    for (std::size_t index = 0; index < probe_sizes.size(); ++index) {
        const auto cell_x = rect.x + 16.0F + static_cast<float>(index) * (cell_size + cell_gap);
        const auto cell_rect = layout::Rect{cell_x, rect.y + 42.0F, cell_size, cell_size};
        recorder.fill_rounded_rect(cell_rect, core::CornerRadius::uniform(10.0F),
                                   rendering::Color::rgba(255, 255, 255, 232));
        recorder.stroke_rounded_rect(cell_rect, core::CornerRadius::uniform(10.0F),
                                     rendering::Color::rgba(223, 228, 236), 1.0F);
        draw_svg_icon_in_box(recorder, spec.paths, cell_rect, spec.palette.accent,
                             probe_sizes[index] <= 20.0F ? 0.875F : 0.92F);
        recorder.draw_text(std::to_string(static_cast<int>(probe_sizes[index])) + "px",
                           {cell_x, rect.y + 84.0F, cell_size, 16.0F},
                           rendering::TextStyle{.font_family = "Segoe UI",
                                                .locale = "en-us",
                                                .font_size = 11.0F,
                                                .color = rendering::Color::rgba(118, 128, 145),
                                                .alignment = rendering::TextAlignment::Center});
    }
}

void draw_panel(rendering::RenderCommandRecorder& recorder, layout::Rect rect,
                std::string_view title, std::string_view subtitle) {
    recorder.draw_box_shadow(rect,
                             rendering::ShadowStyle{.color = rendering::Color::rgba(18, 28, 45, 26),
                                                    .offset = {0.0F, 8.0F},
                                                    .blur_radius = 22.0F,
                                                    .spread = 0.0F});
    recorder.fill_rounded_rect(rect, core::CornerRadius::uniform(28.0F),
                               rendering::Color::rgba(255, 255, 255));
    recorder.stroke_rounded_rect(rect, core::CornerRadius::uniform(28.0F),
                                 rendering::Color::rgba(224, 229, 236), 1.0F);
    recorder.draw_text(std::string(title), {rect.x + 26.0F, rect.y + 20.0F, rect.width - 52.0F, 28.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 24.0F,
                                            .color = rendering::Color::rgba(25, 38, 59)});
    recorder.draw_text(std::string(subtitle), {rect.x + 26.0F, rect.y + 52.0F, rect.width - 52.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI",
                                            .locale = "en-us",
                                            .font_size = 16.0F,
                                            .color = rendering::Color::rgba(103, 116, 137)});
}

[[nodiscard]] rendering::Geometry make_feature_geometry(float offset_x, float offset_y) {
    rendering::Geometry geometry;
    geometry.fill_rule = rendering::GeometryFillRule::EvenOdd;

    rendering::GeometryFigure figure;
    figure.start = {offset_x + 30.0F, offset_y + 110.0F};
    figure.begin = rendering::GeometryFigureBegin::Filled;
    figure.end = rendering::GeometryFigureEnd::Closed;
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Line,
        .point = {offset_x + 115.0F, offset_y + 24.0F}});
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::QuadraticBezier,
        .point = {offset_x + 182.0F, offset_y + 110.0F},
        .control_point1 = {offset_x + 184.0F, offset_y - 4.0F}});
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::CubicBezier,
        .point = {offset_x + 52.0F, offset_y + 198.0F},
        .control_point1 = {offset_x + 225.0F, offset_y + 220.0F},
        .control_point2 = {offset_x + 120.0F, offset_y + 262.0F}});
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Arc,
        .point = {offset_x + 30.0F, offset_y + 110.0F},
        .radius = {44.0F, 62.0F},
        .rotation_angle = 22.0F,
        .arc_size = rendering::GeometryArcSize::Large,
        .sweep_direction = rendering::GeometryArcSweepDirection::CounterClockwise});
    geometry.figures.push_back(std::move(figure));

    rendering::GeometryFigure hollow;
    hollow.start = {offset_x + 92.0F, offset_y + 74.0F};
    hollow.begin = rendering::GeometryFigureBegin::Hollow;
    hollow.end = rendering::GeometryFigureEnd::Closed;
    hollow.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Arc,
        .point = {offset_x + 130.0F, offset_y + 74.0F},
        .radius = {18.0F, 18.0F},
        .arc_size = rendering::GeometryArcSize::Small,
        .sweep_direction = rendering::GeometryArcSweepDirection::Clockwise});
    hollow.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Arc,
        .point = {offset_x + 92.0F, offset_y + 74.0F},
        .radius = {18.0F, 18.0F},
        .arc_size = rendering::GeometryArcSize::Small,
        .sweep_direction = rendering::GeometryArcSweepDirection::Clockwise});
    geometry.figures.push_back(std::move(hollow));

    return geometry;
}

[[nodiscard]] rendering::Geometry make_clip_geometry(float offset_x, float offset_y) {
    rendering::Geometry geometry;

    rendering::GeometryFigure figure;
    figure.start = {offset_x + 70.0F, offset_y + 0.0F};
    figure.end = rendering::GeometryFigureEnd::Closed;
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Arc,
        .point = {offset_x + 140.0F, offset_y + 70.0F},
        .radius = {70.0F, 70.0F},
        .arc_size = rendering::GeometryArcSize::Small,
        .sweep_direction = rendering::GeometryArcSweepDirection::Clockwise});
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Arc,
        .point = {offset_x + 70.0F, offset_y + 140.0F},
        .radius = {70.0F, 70.0F},
        .arc_size = rendering::GeometryArcSize::Small,
        .sweep_direction = rendering::GeometryArcSweepDirection::Clockwise});
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Arc,
        .point = {offset_x + 0.0F, offset_y + 70.0F},
        .radius = {70.0F, 70.0F},
        .arc_size = rendering::GeometryArcSize::Small,
        .sweep_direction = rendering::GeometryArcSweepDirection::Clockwise});
    figure.segments.push_back(rendering::GeometrySegment{
        .type = rendering::GeometrySegmentType::Arc,
        .point = {offset_x + 70.0F, offset_y + 0.0F},
        .radius = {70.0F, 70.0F},
        .arc_size = rendering::GeometryArcSize::Small,
        .sweep_direction = rendering::GeometryArcSweepDirection::Clockwise});
    geometry.figures.push_back(std::move(figure));
    return geometry;
}

[[nodiscard]] std::vector<layout::Point> make_star_points(core::Point center, float outer_radius,
                                                          float inner_radius,
                                                          std::size_t point_count) {
    constexpr float pi = 3.14159265358979323846F;
    std::vector<layout::Point> points;
    points.reserve(point_count * 2U);
    for (std::size_t index = 0; index < point_count * 2U; ++index) {
        const auto angle = (static_cast<float>(index) * pi / static_cast<float>(point_count)) -
                           (pi / 2.0F);
        const auto radius = (index % 2U == 0U) ? outer_radius : inner_radius;
        points.push_back(polar_point(center, radius, angle));
    }
    return points;
}

[[nodiscard]] std::vector<layout::Point>
make_wave_points(float start_x, float baseline_y, float width, float amplitude,
                 std::size_t sample_count) {
    constexpr float tau = 6.28318530717958647692F;
    std::vector<layout::Point> points;
    points.reserve(sample_count);
    for (std::size_t index = 0; index < sample_count; ++index) {
        const auto t = static_cast<float>(index) / static_cast<float>(sample_count - 1U);
        const auto x = start_x + width * t;
        const auto y = baseline_y + std::sin(t * tau * 1.5F) * amplitude;
        points.push_back({x, y});
    }
    return points;
}

[[nodiscard]] rendering::RenderResourceUpload make_demo_texture(rendering::RenderResourceId id,
                                                                bool circular_mask = false) {
    constexpr std::uint32_t width = 160U;
    constexpr std::uint32_t height = 160U;
    constexpr std::uint32_t stride = width * 4U;

    rendering::RenderResourceUpload upload;
    upload.id = id;
    upload.action = rendering::RenderResourceAction::Upload;
    upload.kind = rendering::RenderResourceKind::Image;
    upload.format = rendering::RenderResourceFormat::Bgra8Premultiplied;
    upload.reference_count = 1U;
    upload.width = width;
    upload.height = height;
    upload.stride = stride;
    upload.payload.resize(static_cast<std::size_t>(stride) * height);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto checker = ((x / 16U) + (y / 16U)) % 2U;
            const auto blue = static_cast<std::uint8_t>(32U + (x * 180U) / width);
            const auto green = static_cast<std::uint8_t>(70U + (y * 150U) / height);
            const auto red = static_cast<std::uint8_t>(checker == 0U ? 230U : 120U);
            auto alpha = static_cast<float>(255U - ((x + y) % 28U));
            if (circular_mask) {
                const auto center_x = (static_cast<float>(width) - 1.0F) * 0.5F;
                const auto center_y = (static_cast<float>(height) - 1.0F) * 0.5F;
                const auto dx = static_cast<float>(x) - center_x;
                const auto dy = static_cast<float>(y) - center_y;
                const auto distance = std::sqrt(dx * dx + dy * dy);
                const auto radius = 71.0F;
                const auto feather = 2.25F;
                const auto coverage = std::clamp((radius + feather - distance) / feather, 0.0F, 1.0F);
                alpha *= coverage;
            }
            const auto offset = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4U;
            upload.payload[offset + 0U] = static_cast<std::byte>(blue);
            upload.payload[offset + 1U] = static_cast<std::byte>(green);
            upload.payload[offset + 2U] = static_cast<std::byte>(red);
            upload.payload[offset + 3U] = static_cast<std::byte>(static_cast<std::uint8_t>(alpha));
        }
    }

    return upload;
}

void fill_selection_rects(rendering::RenderCommandRecorder& recorder,
                          std::span<const layout::Rect> rects, rendering::Color color) {
    for (const auto rect : rects) {
        recorder.fill_rect(rect, color);
    }
}

[[nodiscard]] DemoArtifacts build_demo() {
    DemoArtifacts demo;
    demo.image_upload = make_demo_texture(demo_texture_id);
    demo.circle_image_upload = make_demo_texture(demo_circle_texture_id, true);

    rendering::TextEngine text_engine;
    rendering::TextStyle body_style;
    body_style.font_family = "Segoe UI";
    body_style.locale = "zh-cn";
    body_style.font_size = 24.0F;
    body_style.color = rendering::Color::rgba(33, 37, 41);
    body_style.wrapping = rendering::TextWrapping::Wrap;
    body_style.trimming = rendering::TextTrimming::WordEllipsis;
    body_style.decoration_line = rendering::TextDecorationLine::None;
    body_style.fallback_font_families = {"Segoe UI Emoji", "Microsoft YaHei UI"};
    body_style.features.push_back(
        rendering::OpenTypeFeature{rendering::make_opentype_tag('l', 'i', 'g', 'a'), 1U});

    demo.text_layout = text_engine.layout_text(
        "WinElement rendering pipeline demo\n"
        "text layout, selection, clip, image, shadow, geometry and offscreen D3D11 rendering. "
        "ASCII symbols: <> [] {} # @ * with fallback font coverage.",
        body_style,
        rendering::TextLayoutOptions{.max_width = 1000.0F,
                                     .max_height = 180.0F,
                                     .pixels_per_dip = 1.0F});

    const auto aligned_range = text_engine.cluster_aligned_range(
        demo.text_layout, rendering::TextSelectionRange{.start_byte_offset = 0U,
                                                         .end_byte_offset = 56U});
    demo.selection_rects = text_engine.selection_rects(demo.text_layout, aligned_range);
    demo.hit_test = text_engine.hit_test_point(demo.text_layout, {180.0F, 48.0F});
    demo.caret_metrics = text_engine.caret_metrics_for_byte_offset(
        demo.text_layout, aligned_range.end_byte_offset);

    rendering::RenderCommandRecorder recorder;
    const auto canvas = layout::Rect{0.0F, 0.0F, static_cast<float>(canvas_width),
                                     static_cast<float>(canvas_height)};
    demo.visual_regions.full_canvas = canvas;
    recorder.save();
    recorder.fill_rect(canvas, demo.clear_color);

    const auto header_rect = layout::Rect{page_margin, 28.0F, panel_width, 124.0F};
    demo.visual_regions.header = header_rect;
    recorder.draw_box_shadow(header_rect,
                             rendering::ShadowStyle{.color = rendering::Color::rgba(14, 30, 37, 42),
                                                    .offset = {0.0F, 10.0F},
                                                    .blur_radius = 20.0F,
                                                    .spread = 0.0F});
    recorder.fill_rounded_rect(header_rect, core::CornerRadius::uniform(28.0F),
                               rendering::Color::rgba(255, 255, 255));
    recorder.stroke_rounded_rect(header_rect, core::CornerRadius::uniform(28.0F),
                                 rendering::Color::rgba(214, 220, 228), 1.5F);
    recorder.draw_text("WinElement Render Pipeline Demo", {72.0F, 44.0F, 760.0F, 44.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 34.0F,
                                            .color = rendering::Color::rgba(19, 28, 44)});
    recorder.draw_text("vertical stress page for primitive coverage, curve paths, Message icon geometry and offscreen D3D pipeline diagnostics",
                       {72.0F, 90.0F, 1080.0F, 26.0F},
                       rendering::TextStyle{.font_family = "Segoe UI",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(96, 109, 129)});

    const auto primitives_panel = layout::Rect{page_margin, 180.0F, panel_width, 640.0F};
    demo.visual_regions.primitives_panel = primitives_panel;
    demo.visual_regions.primitive_shapes =
        layout::Rect{primitives_panel.x + 24.0F, primitives_panel.y + 94.0F, 672.0F, 434.0F};
    demo.visual_regions.primitive_edges =
        layout::Rect{primitives_panel.x + 748.0F, primitives_panel.y + 146.0F, 346.0F, 184.0F};
    demo.visual_regions.primitive_curves =
        layout::Rect{primitives_panel.x + 748.0F, primitives_panel.y + 348.0F, 360.0F, 338.0F};
    draw_panel(recorder, primitives_panel, "Primitive Commands",
               "all base primitives in one vertical stack, including 1px edge cases, open curve paths and varied stroke styles");
    recorder.push_clip({primitives_panel.x + 20.0F, primitives_panel.y + 96.0F,
                        primitives_panel.width - 40.0F, primitives_panel.height - 120.0F});

    recorder.draw_text("Lines + Rectangles", {primitives_panel.x + 28.0F, primitives_panel.y + 98.0F,
                                               260.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    recorder.draw_line({primitives_panel.x + 38.0F, primitives_panel.y + 148.0F},
                       {primitives_panel.x + 420.0F, primitives_panel.y + 148.0F},
                       rendering::Color::rgba(208, 73, 73), 5.0F);
    recorder.fill_rect({primitives_panel.x + 44.0F, primitives_panel.y + 176.0F, 110.0F, 54.0F},
                       rendering::Color::rgba(38, 122, 217));
    recorder.fill_pixel_snapped_rect(
        {primitives_panel.x + 182.5F, primitives_panel.y + 176.5F, 114.0F, 54.0F},
        rendering::Color::rgba(70, 166, 117));
    recorder.stroke_pixel_snapped_rect(
        {primitives_panel.x + 322.5F, primitives_panel.y + 176.5F, 116.0F, 54.0F},
        rendering::Color::rgba(83, 91, 242), 2.0F);
    recorder.stroke_rect({primitives_panel.x + 466.0F, primitives_panel.y + 176.0F, 116.0F, 54.0F},
                         rendering::Color::rgba(245, 150, 47), 3.0F);

    recorder.draw_text("Rounded + Ellipse", {primitives_panel.x + 28.0F, primitives_panel.y + 254.0F,
                                              240.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    recorder.fill_rounded_rect({primitives_panel.x + 44.0F, primitives_panel.y + 298.0F, 144.0F, 70.0F},
                               core::CornerRadius::uniform(22.0F),
                               rendering::Color::rgba(255, 211, 76));
    recorder.stroke_rounded_rect(
        {primitives_panel.x + 216.0F, primitives_panel.y + 298.0F, 144.0F, 70.0F},
        core::CornerRadius::uniform(22.0F), rendering::Color::rgba(76, 99, 123), 3.0F);
    recorder.fill_ellipse({primitives_panel.x + 402.0F, primitives_panel.y + 286.0F, 92.0F, 92.0F},
                          rendering::Color::rgba(244, 120, 98));
    recorder.stroke_ellipse({primitives_panel.x + 530.0F, primitives_panel.y + 286.0F, 92.0F, 92.0F},
                            rendering::Color::rgba(41, 76, 145), 4.0F);
    recorder.draw_point({primitives_panel.x + 680.0F, primitives_panel.y + 332.0F},
                        rendering::Color::rgba(15, 118, 110), 8.0F);

    recorder.draw_text("Polyline + Polygon", {primitives_panel.x + 28.0F, primitives_panel.y + 402.0F,
                                               240.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    const auto wave_points = make_wave_points(primitives_panel.x + 44.0F, primitives_panel.y + 470.0F,
                                              292.0F, 28.0F, 24U);
    rendering::GeometryStrokeStyle wave_style;
    wave_style.width = 3.0F;
    wave_style.start_cap = rendering::StrokeLineCap::Round;
    wave_style.end_cap = rendering::StrokeLineCap::Round;
    wave_style.line_join = rendering::StrokeLineJoin::Round;
    wave_style.dash_style = rendering::StrokeDashStyle::DashDot;
    recorder.draw_polyline(wave_points, rendering::Color::rgba(20, 112, 190), wave_style);
    const auto star_points = make_star_points({primitives_panel.x + 470.0F, primitives_panel.y + 474.0F},
                                              54.0F, 24.0F, 5U);
    recorder.fill_polygon(star_points, rendering::Color::rgba(145, 96, 245, 176),
                          rendering::GeometryFillRule::EvenOdd);
    rendering::GeometryStrokeStyle star_style;
    star_style.width = 2.5F;
    star_style.line_join = rendering::StrokeLineJoin::Round;
    recorder.stroke_polygon(star_points, rendering::Color::rgba(82, 42, 179), star_style,
                            rendering::GeometryFillRule::EvenOdd);

    recorder.draw_text("1px Edge Cases", {primitives_panel.x + 760.0F, primitives_panel.y + 98.0F,
                                           180.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    recorder.draw_text("pixel-snapped, raw and diagonal 1px primitives", {primitives_panel.x + 760.0F, primitives_panel.y + 126.0F,
                                                                            320.0F, 22.0F},
                       rendering::TextStyle{.font_family = "Segoe UI",
                                            .locale = "en-us",
                                            .font_size = 14.0F,
                                            .color = rendering::Color::rgba(111, 122, 140)});
    recorder.fill_rounded_rect({primitives_panel.x + 748.0F, primitives_panel.y + 146.0F, 346.0F, 184.0F},
                               core::CornerRadius::uniform(20.0F), rendering::Color::rgba(249, 250, 252));
    recorder.stroke_rounded_rect({primitives_panel.x + 748.0F, primitives_panel.y + 146.0F, 346.0F, 184.0F},
                                 core::CornerRadius::uniform(20.0F), rendering::Color::rgba(228, 233, 240), 1.0F);
    recorder.fill_pixel_snapped_rect(
        {primitives_panel.x + 774.5F, primitives_panel.y + 158.5F, 1.0F, 1.0F},
        rendering::Color::rgba(32, 44, 68));
    recorder.fill_pixel_snapped_rect(
        {primitives_panel.x + 782.5F, primitives_panel.y + 154.5F, 12.0F, 1.0F},
        rendering::Color::rgba(32, 44, 68));
    recorder.fill_pixel_snapped_rect(
        {primitives_panel.x + 800.5F, primitives_panel.y + 146.5F, 1.0F, 12.0F},
        rendering::Color::rgba(32, 44, 68));
    recorder.draw_line({primitives_panel.x + 774.5F, primitives_panel.y + 162.5F},
                       {primitives_panel.x + 1070.5F, primitives_panel.y + 162.5F},
                       rendering::Color::rgba(214, 61, 61), 1.0F);
    recorder.draw_line({primitives_panel.x + 804.5F, primitives_panel.y + 172.5F},
                       {primitives_panel.x + 804.5F, primitives_panel.y + 246.5F},
                       rendering::Color::rgba(31, 120, 180), 1.0F);
    recorder.draw_line({primitives_panel.x + 842.5F, primitives_panel.y + 176.5F},
                       {primitives_panel.x + 1096.5F, primitives_panel.y + 230.5F},
                       rendering::Color::rgba(84, 99, 214), 1.0F);
    recorder.stroke_pixel_snapped_rect(
        {primitives_panel.x + 874.5F, primitives_panel.y + 182.5F, 88.0F, 52.0F},
        rendering::Color::rgba(45, 145, 98), 1.0F);
    recorder.stroke_rect({primitives_panel.x + 986.0F, primitives_panel.y + 182.0F, 88.0F, 52.0F},
                         rendering::Color::rgba(245, 150, 47), 1.0F);
    recorder.stroke_ellipse({primitives_panel.x + 890.5F, primitives_panel.y + 260.5F, 71.0F, 71.0F},
                            rendering::Color::rgba(168, 85, 247, 184), 1.0F);
    recorder.stroke_geometry(transform_geometry(make_open_polyline_path(), 0.52F,
                                               {primitives_panel.x + 970.0F, primitives_panel.y + 272.0F}),
                             rendering::Color::rgba(19, 113, 95),
                             rendering::GeometryStrokeStyle{.width = 1.0F,
                                                            .start_cap = rendering::StrokeLineCap::Flat,
                                                            .end_cap = rendering::StrokeLineCap::Flat,
                                                            .line_join = rendering::StrokeLineJoin::Miter,
                                                            .dash_style = rendering::StrokeDashStyle::Solid});

    recorder.draw_text("Open Curves", {primitives_panel.x + 760.0F, primitives_panel.y + 348.0F,
                                        180.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    rendering::GeometryStrokeStyle quadratic_style;
    quadratic_style.width = 5.0F;
    quadratic_style.start_cap = rendering::StrokeLineCap::Round;
    quadratic_style.end_cap = rendering::StrokeLineCap::Round;
    quadratic_style.line_join = rendering::StrokeLineJoin::Round;
    quadratic_style.dash_style = rendering::StrokeDashStyle::Solid;
    recorder.stroke_geometry(transform_geometry(make_open_quadratic_curve(), 1.0F,
                                               {primitives_panel.x + 772.0F, primitives_panel.y + 386.0F}),
                             rendering::Color::rgba(217, 79, 70), quadratic_style);
    recorder.stroke_geometry(transform_geometry(make_open_cubic_curve(), 1.0F,
                                               {primitives_panel.x + 772.0F, primitives_panel.y + 482.0F}),
                             rendering::Color::rgba(63, 116, 231),
                             rendering::GeometryStrokeStyle{.width = 5.0F,
                                                            .start_cap = rendering::StrokeLineCap::Square,
                                                            .end_cap = rendering::StrokeLineCap::Triangle,
                                                            .line_join = rendering::StrokeLineJoin::Bevel,
                                                            .dash_style = rendering::StrokeDashStyle::Dash});
    recorder.stroke_geometry(transform_geometry(make_open_arc_curve(), 1.0F,
                                               {primitives_panel.x + 772.0F, primitives_panel.y + 580.0F}),
                             rendering::Color::rgba(45, 145, 98),
                             rendering::GeometryStrokeStyle{.width = 5.0F,
                                                            .start_cap = rendering::StrokeLineCap::Round,
                                                            .end_cap = rendering::StrokeLineCap::Round,
                                                            .line_join = rendering::StrokeLineJoin::Round,
                                                            .dash_style = rendering::StrokeDashStyle::Dot});
    recorder.stroke_geometry(transform_geometry(make_open_polyline_path(), 1.0F,
                                               {primitives_panel.x + 772.0F, primitives_panel.y + 678.0F}),
                             rendering::Color::rgba(120, 81, 169),
                             rendering::GeometryStrokeStyle{.width = 4.0F,
                                                            .start_cap = rendering::StrokeLineCap::Flat,
                                                            .end_cap = rendering::StrokeLineCap::Square,
                                                            .line_join = rendering::StrokeLineJoin::Miter,
                                                            .dash_style = rendering::StrokeDashStyle::DashDotDot});
    recorder.pop_clip();

    const auto geometry_panel = layout::Rect{page_margin, primitives_panel.y + primitives_panel.height + panel_gap,
                                             panel_width, 760.0F};
    demo.visual_regions.geometry_panel = geometry_panel;
    demo.visual_regions.geometry_feature_mix =
        layout::Rect{geometry_panel.x + 46.0F, geometry_panel.y + 94.0F, 252.0F, 226.0F};
    demo.visual_regions.geometry_clip_image =
        layout::Rect{geometry_panel.x + 316.0F, geometry_panel.y + 128.0F, 164.0F, 164.0F};
    demo.visual_regions.geometry_large_message =
        layout::Rect{geometry_panel.x + 44.0F, geometry_panel.y + 370.0F, 256.0F, 348.0F};
    demo.visual_regions.geometry_message_board =
        layout::Rect{geometry_panel.x + 492.0F, geometry_panel.y + 374.0F, 620.0F, 384.0F};
    draw_panel(recorder, geometry_panel, "Geometry Stress: WinMochi Message Icons",
               "large envelope geometry on the left, plus a pure SVG probe board on the right for small-icon fidelity and edge quality checks");
    const auto feature_geometry = make_feature_geometry(geometry_panel.x + 56.0F, geometry_panel.y + 128.0F);
    rendering::GeometryStrokeStyle geometry_style;
    geometry_style.width = 5.0F;
    geometry_style.start_cap = rendering::StrokeLineCap::Round;
    geometry_style.end_cap = rendering::StrokeLineCap::Triangle;
    geometry_style.dash_cap = rendering::StrokeLineCap::Round;
    geometry_style.line_join = rendering::StrokeLineJoin::Round;
    geometry_style.dash_style = rendering::StrokeDashStyle::Custom;
    geometry_style.dashes = {14.0F, 8.0F, 3.0F, 6.0F};
    geometry_style.dash_offset = 2.0F;
    recorder.draw_text("Mixed segment geometry", {geometry_panel.x + 56.0F, geometry_panel.y + 100.0F,
                                                   260.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    recorder.fill_geometry(feature_geometry, rendering::Color::rgba(74, 145, 226, 120));
    recorder.stroke_geometry(feature_geometry, rendering::Color::rgba(20, 48, 92), geometry_style);

    const auto clip_geometry = make_clip_geometry(geometry_panel.x + 326.0F, geometry_panel.y + 136.0F);
    recorder.push_geometry_clip(clip_geometry);
    recorder.draw_text("Geometry clip + image", {geometry_panel.x + 324.0F, geometry_panel.y + 100.0F,
                                                  220.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    recorder.draw_box_shadow({geometry_panel.x + 320.0F, geometry_panel.y + 132.0F, 152.0F, 152.0F},
                             rendering::ShadowStyle{.color = rendering::Color::rgba(18, 52, 86, 52),
                                                    .offset = {5.0F, 7.0F},
                                                    .blur_radius = 14.0F,
                                                    .spread = 2.0F});
    recorder.draw_image(demo_circle_texture_id,
                        rendering::RenderImageOptions{.destination = {geometry_panel.x + 316.0F, geometry_panel.y + 128.0F, 164.0F, 164.0F},
                                                      .source = {0.0F, 0.0F, 160.0F, 160.0F},
                                                      .opacity = 0.94F});
    recorder.pop_geometry_clip();

    recorder.draw_text("Message icon outline + flap", {geometry_panel.x + 56.0F, geometry_panel.y + 374.0F,
                                                        280.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    const auto message_outline_large = make_message_icon_outline_geometry(0.26F,
                                                                          {geometry_panel.x + 62.0F, geometry_panel.y + 432.0F});
    const auto message_flap_large = make_message_icon_flap_geometry(0.26F,
                                                                    {geometry_panel.x + 62.0F, geometry_panel.y + 432.0F});
    recorder.fill_geometry(message_outline_large, rendering::Color::rgba(243, 247, 252));
    recorder.stroke_geometry(message_outline_large, rendering::Color::rgba(52, 83, 142),
                             rendering::GeometryStrokeStyle{.width = 4.0F,
                                                            .start_cap = rendering::StrokeLineCap::Round,
                                                            .end_cap = rendering::StrokeLineCap::Round,
                                                            .line_join = rendering::StrokeLineJoin::Round,
                                                            .dash_style = rendering::StrokeDashStyle::Solid});
    recorder.fill_geometry(message_flap_large, rendering::Color::rgba(97, 156, 236, 170));

    recorder.draw_text("Message SVG probe board", {geometry_panel.x + 492.0F, geometry_panel.y + 374.0F,
                                                    320.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    recorder.draw_text("pure SVG rendering only: icon geometry, fit and small-size detail probes at 16/20/24/32 px.",
                       {geometry_panel.x + 492.0F, geometry_panel.y + 402.0F, 560.0F, 22.0F},
                       rendering::TextStyle{.font_family = "Segoe UI",
                                            .locale = "en-us",
                                            .font_size = 14.0F,
                                            .color = rendering::Color::rgba(111, 122, 140)});
    const std::array<MessageDemoIconSpec, 6> message_icon_specs = {{
        {.label = "InfoFilled", .paths = info_filled_svg,
         .palette = {.accent = rendering::Color::rgba(64, 158, 255),
                     .background = rendering::Color::rgba(236, 245, 255),
                     .border = rendering::Color::rgba(179, 216, 255),
                     .text = rendering::Color::rgba(64, 158, 255)}} ,
        {.label = "SuccessFilled", .paths = success_filled_svg,
         .palette = {.accent = rendering::Color::rgba(103, 194, 58),
                     .background = rendering::Color::rgba(240, 249, 235),
                     .border = rendering::Color::rgba(225, 243, 216),
                     .text = rendering::Color::rgba(82, 155, 46)}},
        {.label = "WarningFilled", .paths = warning_filled_svg,
         .palette = {.accent = rendering::Color::rgba(230, 162, 60),
                     .background = rendering::Color::rgba(253, 246, 236),
                     .border = rendering::Color::rgba(250, 236, 216),
                     .text = rendering::Color::rgba(179, 119, 27)}},
        {.label = "CircleCloseFilled", .paths = circle_close_filled_svg,
         .palette = {.accent = rendering::Color::rgba(245, 108, 108),
                     .background = rendering::Color::rgba(254, 240, 240),
                     .border = rendering::Color::rgba(253, 226, 226),
                     .text = rendering::Color::rgba(196, 86, 86)}},
        {.label = "Close glyph", .paths = close_svg,
         .palette = {.accent = rendering::Color::rgba(144, 147, 153),
                     .background = rendering::Color::rgba(247, 248, 250),
                     .border = rendering::Color::rgba(228, 231, 237),
                     .text = rendering::Color::rgba(96, 98, 102)}},
        {.label = "Message envelope", .paths = message_svg,
         .palette = {.accent = rendering::Color::rgba(121, 134, 203),
                     .background = rendering::Color::rgba(243, 245, 255),
                     .border = rendering::Color::rgba(223, 227, 252),
                     .text = rendering::Color::rgba(77, 89, 156)}},
    }};
    constexpr float message_tile_width = 300.0F;
    constexpr float message_tile_height = 100.0F;
    constexpr float message_tile_gap_x = 18.0F;
    constexpr float message_tile_gap_y = 12.0F;
    for (std::size_t index = 0; index < message_icon_specs.size(); ++index) {
        const auto column = static_cast<float>(index % 2U);
        const auto row = static_cast<float>(index / 2U);
        const auto tile = layout::Rect{geometry_panel.x + 506.0F + column * (message_tile_width + message_tile_gap_x),
                                       geometry_panel.y + 432.0F + row * (message_tile_height + message_tile_gap_y),
                                       message_tile_width,
                                       message_tile_height};
        draw_svg_probe_tile(recorder, tile, message_icon_specs[index]);
    }

    const auto svg_panel = layout::Rect{page_margin, geometry_panel.y + geometry_panel.height + panel_gap,
                                        panel_width, 800.0F};
    demo.visual_regions.svg_panel = svg_panel;
    demo.visual_regions.svg_detail_board =
        layout::Rect{svg_panel.x + 28.0F, svg_panel.y + 134.0F, 834.0F, 420.0F};
    demo.visual_regions.svg_scale_board =
        layout::Rect{svg_panel.x + 28.0F, svg_panel.y + 554.0F, 1092.0F, 192.0F};
    draw_panel(recorder, svg_panel, "SVG Path Stress",
               "parser-focused validation only: dense 1px diagonals, sharp miter joins, multi-pixel path primitives and 16/20/24 px scale probes");
    recorder.draw_text("1px detail cases", {svg_panel.x + 28.0F, svg_panel.y + 100.0F, 240.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    recorder.draw_text("4px-6px parser primitives", {svg_panel.x + 612.0F, svg_panel.y + 100.0F, 260.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    const std::array<SvgPathStressSpec, 4> svg_thin_detail_specs = {{
        {.label = "Line", .path = svg_straight_line_path, .color = rendering::Color::rgba(214, 61, 61), .view_width = 220.0F, .view_height = 120.0F},
        {.label = "Dense diagonals", .path = svg_dense_diagonal_path, .color = rendering::Color::rgba(45, 113, 198), .view_width = 152.0F, .view_height = 120.0F},
        {.label = "Sharp miter join", .path = svg_sharp_join_path, .color = rendering::Color::rgba(111, 85, 214), .view_width = 236.0F, .view_height = 120.0F},
        {.label = "Circle outline", .path = svg_circle_path, .color = rendering::Color::rgba(38, 154, 109), .view_width = 100.0F, .view_height = 100.0F},
    }};
    const std::array<SvgPathStressSpec, 6> svg_stress_specs = {{
        {.label = "Line", .path = svg_straight_line_path, .color = rendering::Color::rgba(214, 61, 61), .view_width = 220.0F, .view_height = 120.0F},
        {.label = "Polyline", .path = svg_polyline_path, .color = rendering::Color::rgba(36, 113, 196), .view_width = 236.0F, .view_height = 120.0F},
        {.label = "Quadratic", .path = svg_quadratic_path, .color = rendering::Color::rgba(235, 131, 46), .view_width = 220.0F, .view_height = 120.0F},
        {.label = "Cubic", .path = svg_cubic_path, .color = rendering::Color::rgba(104, 94, 219), .view_width = 236.0F, .view_height = 120.0F},
        {.label = "Arc", .path = svg_arc_path, .color = rendering::Color::rgba(38, 154, 109), .view_width = 236.0F, .view_height = 120.0F},
        {.label = "Circle", .path = svg_circle_path, .color = rendering::Color::rgba(60, 98, 173), .view_width = 100.0F, .view_height = 100.0F},
    }};
    const auto svg_stress_thin = rendering::GeometryStrokeStyle{.width = 1.0F,
                                                                .start_cap = rendering::StrokeLineCap::Flat,
                                                                .end_cap = rendering::StrokeLineCap::Flat,
                                                                .line_join = rendering::StrokeLineJoin::Miter,
                                                                .dash_style = rendering::StrokeDashStyle::Solid};
    const auto svg_stress_thick = rendering::GeometryStrokeStyle{.width = 4.5F,
                                                                 .start_cap = rendering::StrokeLineCap::Round,
                                                                 .end_cap = rendering::StrokeLineCap::Round,
                                                                 .line_join = rendering::StrokeLineJoin::Round,
                                                                 .dash_style = rendering::StrokeDashStyle::Solid};
    constexpr float svg_cell_width = 250.0F;
    constexpr float svg_cell_height = 120.0F;
    constexpr float svg_cell_gap_x = 18.0F;
    constexpr float svg_cell_gap_y = 16.0F;
    for (std::size_t index = 0; index < svg_thin_detail_specs.size(); ++index) {
        const auto row = static_cast<float>(index / 2U);
        const auto column = static_cast<float>(index % 2U);
        const auto thin_rect = layout::Rect{svg_panel.x + 28.0F + column * (svg_cell_width + svg_cell_gap_x),
                                            svg_panel.y + 134.0F + row * (svg_cell_height + svg_cell_gap_y),
                                            svg_cell_width,
                                            svg_cell_height};
        draw_svg_stress_cell(recorder, thin_rect, svg_thin_detail_specs[index], svg_stress_thin, "1px");
    }
    for (std::size_t index = 0; index < svg_stress_specs.size(); ++index) {
        const auto row = static_cast<float>(index / 2U);
        const auto column = static_cast<float>(index % 2U);
        const auto thick_rect = layout::Rect{svg_panel.x + 612.0F + column * (svg_cell_width + svg_cell_gap_x),
                                             svg_panel.y + 134.0F + row * (svg_cell_height + svg_cell_gap_y),
                                             svg_cell_width,
                                             svg_cell_height};
        draw_svg_stress_cell(recorder, thick_rect, svg_stress_specs[index], svg_stress_thick, "4.5px");
    }

    recorder.draw_text("16px / 20px / 24px scale probes",
                       {svg_panel.x + 28.0F, svg_panel.y + 554.0F, 340.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    recorder.draw_text("small SVG path scaling only: dense diagonals, acute joins, arcs and circle edges at icon-like sizes.",
                       {svg_panel.x + 28.0F, svg_panel.y + 582.0F, 700.0F, 22.0F},
                       rendering::TextStyle{.font_family = "Segoe UI",
                                            .locale = "en-us",
                                            .font_size = 14.0F,
                                            .color = rendering::Color::rgba(111, 122, 140)});
    const std::array<SvgPathStressSpec, 4> svg_scale_probe_specs = {{
        {.label = "Dense diagonals", .path = svg_dense_diagonal_path, .color = rendering::Color::rgba(45, 113, 198), .view_width = 152.0F, .view_height = 120.0F},
        {.label = "Sharp miter join", .path = svg_sharp_join_path, .color = rendering::Color::rgba(111, 85, 214), .view_width = 236.0F, .view_height = 120.0F},
        {.label = "Arc", .path = svg_arc_path, .color = rendering::Color::rgba(38, 154, 109), .view_width = 236.0F, .view_height = 120.0F},
        {.label = "Circle", .path = svg_circle_path, .color = rendering::Color::rgba(60, 98, 173), .view_width = 100.0F, .view_height = 100.0F},
    }};
    constexpr float svg_probe_tile_width = 270.0F;
    constexpr float svg_probe_tile_gap = 12.0F;
    for (std::size_t index = 0; index < svg_scale_probe_specs.size(); ++index) {
        const auto tile = layout::Rect{svg_panel.x + 28.0F + static_cast<float>(index) * (svg_probe_tile_width + svg_probe_tile_gap),
                                       svg_panel.y + 620.0F,
                                       svg_probe_tile_width,
                                       126.0F};
        draw_svg_scale_probe_tile(recorder, tile, svg_scale_probe_specs[index], svg_stress_thin);
    }

    const auto text_panel = layout::Rect{page_margin, svg_panel.y + svg_panel.height + panel_gap,
                                         panel_width, 740.0F};
    demo.visual_regions.text_panel = text_panel;
    draw_panel(recorder, text_panel, "Text, Image and Layering",
               "selection rectangles, text layout, image sampling, box shadows and an offset composited layer");

    recorder.push_layer(rendering::RenderLayerOptions{.bounds = {text_panel.x + 44.0F, text_panel.y + 112.0F,
                                                                 text_panel.width - 88.0F, 560.0F},
                                                      .opacity = 0.97F,
                                                      .transform = core::Transform2D::translation(8.0F, 10.0F),
                                                      .clips_to_bounds = false});
    const auto text_content_panel = layout::Rect{text_panel.x + 44.0F, text_panel.y + 112.0F,
                                                 text_panel.width - 88.0F, 560.0F};
    demo.visual_regions.text_content = text_content_panel;
    demo.visual_regions.text_selection =
        layout::Rect{text_content_panel.x + 18.0F, text_content_panel.y + 16.0F, 944.0F, 214.0F};
    demo.visual_regions.text_image =
        layout::Rect{text_content_panel.x + 26.0F, text_content_panel.y + 298.0F, 214.0F, 248.0F};
    demo.visual_regions.text_layer =
        layout::Rect{text_content_panel.x + 276.0F, text_content_panel.y + 330.0F, 338.0F, 154.0F};
    recorder.draw_box_shadow(text_content_panel,
                             rendering::ShadowStyle{.color = rendering::Color::rgba(22, 35, 54, 42),
                                                    .offset = {0.0F, 6.0F},
                                                    .blur_radius = 18.0F,
                                                    .spread = 0.0F});
    recorder.fill_rounded_rect(text_content_panel, core::CornerRadius::uniform(24.0F),
                               rendering::Color::rgba(255, 255, 255, 244));
    recorder.stroke_rounded_rect(text_content_panel, core::CornerRadius::uniform(24.0F),
                                 rendering::Color::rgba(213, 220, 228), 1.0F);
    recorder.draw_text("Text layout + selection", {text_content_panel.x + 24.0F, text_content_panel.y + 20.0F,
                                                    260.0F, 28.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 24.0F,
                                            .color = rendering::Color::rgba(32, 44, 68)});
    recorder.draw_text("Image texture + compositor candidate", {text_content_panel.x + 24.0F, text_content_panel.y + 264.0F,
                                                                 340.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    recorder.draw_text("underline + strikethrough sample", {text_content_panel.x + 24.0F, text_content_panel.y + 224.0F,
                                                             360.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(92, 105, 125),
                                            .decoration_line = rendering::TextDecorationLine::Underline |
                                                               rendering::TextDecorationLine::Strikethrough});

    std::vector<layout::Rect> translated_selection_rects;
    translated_selection_rects.reserve(demo.selection_rects.size());
    for (const auto rect : demo.selection_rects) {
        translated_selection_rects.push_back(
            core::offset_rect(rect, core::Point{text_content_panel.x + 26.0F, text_content_panel.y + 62.0F}));
    }
    fill_selection_rects(recorder, translated_selection_rects,
                         rendering::Color::rgba(140, 197, 255, 116));
    recorder.draw_text_layout(demo.text_layout, {text_content_panel.x + 26.0F, text_content_panel.y + 62.0F});
    recorder.fill_rect(core::offset_rect(demo.caret_metrics.rect,
                                         core::Point{text_content_panel.x + 26.0F, text_content_panel.y + 62.0F}),
                       rendering::Color::rgba(36, 91, 180));
    recorder.draw_image(demo_texture_id,
                        rendering::RenderImageOptions{.destination = {text_content_panel.x + 34.0F,
                                                                     text_content_panel.y + 338.0F,
                                                                     196.0F,
                                                                     196.0F},
                                                      .source = {0.0F, 0.0F, 160.0F, 160.0F},
                                                      .opacity = 0.96F});
    recorder.draw_box_shadow({text_content_panel.x + 292.0F, text_content_panel.y + 344.0F, 304.0F, 126.0F},
                             rendering::ShadowStyle{.color = rendering::Color::rgba(13, 24, 36, 28),
                                                    .offset = {0.0F, 4.0F},
                                                    .blur_radius = 12.0F,
                                                    .spread = 0.0F});
    recorder.fill_rounded_rect({text_content_panel.x + 292.0F, text_content_panel.y + 344.0F, 304.0F, 126.0F},
                               core::CornerRadius::uniform(18.0F), rendering::Color::rgba(248, 250, 253));
    recorder.stroke_rounded_rect({text_content_panel.x + 292.0F, text_content_panel.y + 344.0F, 304.0F, 126.0F},
                                 core::CornerRadius::uniform(18.0F), rendering::Color::rgba(205, 214, 224), 1.0F);
    recorder.draw_text("layer opacity + translation", {text_content_panel.x + 318.0F, text_content_panel.y + 372.0F, 236.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(32, 44, 68)});
    recorder.draw_text("this whole card is rendered inside a translated layer", {text_content_panel.x + 318.0F, text_content_panel.y + 406.0F, 236.0F, 52.0F},
                       rendering::TextStyle{.font_family = "Segoe UI",
                                            .locale = "en-us",
                                            .font_size = 16.0F,
                                            .color = rendering::Color::rgba(92, 105, 125),
                                            .wrapping = rendering::TextWrapping::Wrap});
    recorder.pop_layer();

    recorder.restore();

    demo.commands = recorder.take_command_list();
    demo.scene.update_from_commands(demo.commands, "render_pipeline_demo");
    demo.frame_graph = rendering::build_render_frame_graph(demo.scene);
    demo.promotion_plan = rendering::build_compositor_promotion_plan(
        demo.scene,
        rendering::CompositorPromotionOptions{.max_candidates = 8U,
                                              .minimum_area = 2048.0F,
                                              .include_stable_layers = true});

    demo.dirty_region.add(canvas);
    demo.dirty_region.add(primitives_panel);
    demo.dirty_region.add(geometry_panel);
    demo.dirty_region.add(svg_panel);
    demo.dirty_region.add(text_panel);
    demo.dirty_region.optimize(rendering::DirtyRegionOptimizeOptions{.max_rects = 6U,
                                                                     .merge_slop = 12.0F,
                                                                     .scanline_merge = true});

    demo.compositor_frame = rendering::CompositorFrameBuilder(1U)
                                .set_clear_color(demo.clear_color)
                                .set_dirty_region(demo.dirty_region)
                                .set_render_scene(demo.scene)
                                .add_layer(rendering::CompositorLayer{.id = 1U,
                                                                      .kind = rendering::CompositorLayerKind::Texture,
                                                                      .bounds = {text_panel.x + 78.0F, text_panel.y + 464.0F, 196.0F, 196.0F},
                                                                      .opacity = 0.94F,
                                                                      .resource_id = demo_texture_id,
                                                                      .debug_name = "demo texture"})
                                .build();
    return demo;
}

[[nodiscard]] RenderTargetBundle create_render_target(win32::D3D11RenderDevice& device,
                                                      std::uint32_t width,
                                                      std::uint32_t height) {
    RenderTargetBundle bundle;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    auto result = device.d3d_device().CreateTexture2D(&description, nullptr, &bundle.texture);
    if (FAILED(result)) {
        throw make_hresult_error("failed to create D3D11 render target texture", result);
    }

    result =
        device.d3d_device().CreateRenderTargetView(bundle.texture.Get(), nullptr, &bundle.target_view);
    if (FAILED(result)) {
        throw make_hresult_error("failed to create D3D11 render target view", result);
    }

    return bundle;
}

[[nodiscard]] std::vector<std::byte>
read_back_texture(win32::D3D11RenderDevice& device, ID3D11Texture2D& texture, std::uint32_t width,
                  std::uint32_t height, std::uint32_t& stride) {
    D3D11_TEXTURE2D_DESC description{};
    texture.GetDesc(&description);

    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0U;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0U;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture;
    auto result = device.d3d_device().CreateTexture2D(&description, nullptr, &staging_texture);
    if (FAILED(result)) {
        throw make_hresult_error("failed to create staging texture", result);
    }

    device.d3d_context().CopyResource(staging_texture.Get(), &texture);
    device.d3d_context().Flush();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = device.d3d_context().Map(staging_texture.Get(), 0U, D3D11_MAP_READ, 0U, &mapped);
    if (FAILED(result)) {
        throw make_hresult_error("failed to map staging texture", result);
    }

    stride = width * 4U;
    std::vector<std::byte> pixels(static_cast<std::size_t>(stride) * height);
    const auto* source = static_cast<const std::byte*>(mapped.pData);
    for (std::uint32_t row = 0; row < height; ++row) {
        const auto* source_row = source + static_cast<std::size_t>(mapped.RowPitch) * row;
        auto* destination_row = pixels.data() + static_cast<std::size_t>(stride) * row;
        std::copy_n(source_row, stride, destination_row);
    }

    device.d3d_context().Unmap(staging_texture.Get(), 0U);
    return pixels;
}

void save_png(const fs::path& output_path, std::uint32_t width, std::uint32_t height,
              std::uint32_t stride, const std::vector<std::byte>& pixels) {
    ScopedComApartment com;

    if (!output_path.parent_path().empty()) {
        fs::create_directories(output_path.parent_path());
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    auto result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        throw make_hresult_error("failed to create WIC imaging factory", result);
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    result = factory->CreateStream(&stream);
    if (FAILED(result)) {
        throw make_hresult_error("failed to create WIC stream", result);
    }

    result = stream->InitializeFromFilename(output_path.c_str(), GENERIC_WRITE);
    if (FAILED(result)) {
        throw make_hresult_error("failed to initialize WIC stream", result);
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(result)) {
        throw make_hresult_error("failed to create PNG encoder", result);
    }

    result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(result)) {
        throw make_hresult_error("failed to initialize PNG encoder", result);
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    Microsoft::WRL::ComPtr<IPropertyBag2> properties;
    result = encoder->CreateNewFrame(&frame, &properties);
    if (FAILED(result)) {
        throw make_hresult_error("failed to create PNG frame", result);
    }

    result = frame->Initialize(properties.Get());
    if (FAILED(result)) {
        throw make_hresult_error("failed to initialize PNG frame", result);
    }

    result = frame->SetSize(width, height);
    if (FAILED(result)) {
        throw make_hresult_error("failed to set PNG frame size", result);
    }

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppPBGRA;
    result = frame->SetPixelFormat(&pixel_format);
    if (FAILED(result)) {
        throw make_hresult_error("failed to set PNG pixel format", result);
    }

    result = frame->WritePixels(height, stride, static_cast<UINT>(pixels.size()),
                                reinterpret_cast<BYTE*>(const_cast<std::byte*>(pixels.data())));
    if (FAILED(result)) {
        throw make_hresult_error("failed to write PNG pixels", result);
    }

    result = frame->Commit();
    if (FAILED(result)) {
        throw make_hresult_error("failed to commit PNG frame", result);
    }

    result = encoder->Commit();
    if (FAILED(result)) {
        throw make_hresult_error("failed to commit PNG encoder", result);
    }
}

void print_demo_summary(const DemoArtifacts& demo) {
    std::array<std::size_t, 5U> batch_counts{};
    for (const auto& batch : demo.commands.draw_batches()) {
        batch_counts[static_cast<std::size_t>(batch.kind)] += 1U;
    }

    std::cout << "Render pipeline demo summary\n";
    std::cout << "  commands: " << demo.commands.command_count() << "\n";
    std::cout << "  serialized opcode bytes: " << demo.commands.serialized_opcodes().size() << "\n";
    std::cout << "  dirty rects: " << demo.dirty_region.rects().size() << "\n";
    std::cout << "  dirty tree nodes: " << demo.dirty_region.node_count() << "\n";
    std::cout << "  frame graph passes: " << demo.frame_graph.passes.size() << "\n";
    std::cout << "  frame graph draw calls: " << demo.frame_graph.estimated_draw_call_count << "\n";
    std::cout << "  compositor promotion candidates: " << demo.promotion_plan.candidates.size()
              << "\n";
    std::cout << "  compositor frame layers: " << demo.compositor_frame.layers.size() << "\n";
    std::cout << "  text glyphs: " << demo.text_layout.glyphs.size() << "\n";
    std::cout << "  text lines: " << demo.text_layout.lines.size() << "\n";
    std::cout << "  text selection rects: " << demo.selection_rects.size() << "\n";
    std::cout << "  hit test byte offset: " << demo.hit_test.byte_offset << "\n";
    std::cout << "  caret line index: " << demo.caret_metrics.line_index << "\n";
    std::cout << "  draw batch counts [state, geometry, text, image, effect]: "
              << batch_counts[0] << ", " << batch_counts[1] << ", " << batch_counts[2] << ", "
              << batch_counts[3] << ", " << batch_counts[4] << "\n";

    for (std::size_t index = 0; index < demo.frame_graph.passes.size(); ++index) {
        const auto& pass = demo.frame_graph.passes[index];
        std::cout << "    pass[" << index << "] kind=" << static_cast<int>(pass.kind)
                  << " commands=" << pass.command_count
                  << " draws=" << pass.estimated_draw_call_count
                  << " stencil=" << pass.requires_stencil << "\n";
    }
}

[[nodiscard]] std::vector<std::byte> render_demo_frame(DemoArtifacts& demo, std::uint32_t& stride) {
    demo = build_demo();

    win32::D3D11RenderDevice device;
    win32::D3D11RenderResourceCache resource_cache;
    resource_cache.upload(device.d3d_device(), demo.image_upload);
    resource_cache.upload(device.d3d_device(), demo.circle_image_upload);

    auto render_target = create_render_target(device, canvas_width, canvas_height);
    win32::D3D11DisplayListRenderer renderer(device.d3d_device());
    renderer.render(device.d3d_context(), *render_target.target_view.Get(), demo.clear_color,
                    demo.scene.empty() ? nullptr : &demo.scene, demo.dirty_region, dpi,
                    canvas_width, canvas_height, resource_cache, &demo.frame_graph);

    auto pixels =
        read_back_texture(device, *render_target.texture.Get(), canvas_width, canvas_height, stride);
    resource_cache.clear();
    return pixels;
}

} // namespace

#ifndef WINELEMENT_RENDER_PIPELINE_DEMO_AS_LIBRARY
int main(int argc, char** argv) {
    try {
        const fs::path output_path =
            argc > 1 ? fs::path(argv[1]) : fs::current_path() / "render_pipeline_demo.png";

        DemoArtifacts demo;
        std::uint32_t stride = 0U;
        const auto pixels = render_demo_frame(demo, stride);
        save_png(output_path, canvas_width, canvas_height, stride, pixels);

        print_demo_summary(demo);
        std::cout << "  output: " << output_path.string() << "\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "render_pipeline_demo failed: " << exception.what() << "\n";
        return 1;
    }
}
#endif