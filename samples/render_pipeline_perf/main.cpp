#define WINELEMENT_RENDER_PIPELINE_DEMO_AS_LIBRARY 1
#include "../render_pipeline_demo/main.cpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct PerfOptions {
    fs::path report_path = fs::current_path() / "render_pipeline_perf_report.json";
    fs::path png_path = fs::current_path() / "render_pipeline_perf_frame.png";
    std::size_t warmup_iterations = 3U;
    std::size_t measured_iterations = 12U;
    bool write_png = true;
};

struct FrameSample {
    double render_ms = 0.0;
    double task_split_ms = 0.0;
    double resource_sync_ms = 0.0;
    double worker_record_ms = 0.0;
    double command_submit_ms = 0.0;
    double readback_ms = 0.0;
    double total_ms = 0.0;
    std::uint64_t pixel_hash = 0U;
    std::size_t work_item_count = 0U;
    std::size_t command_list_count = 0U;
    bool parallel_recording = false;
};

struct SummaryStats {
    double min = 0.0;
    double mean = 0.0;
    double median = 0.0;
    double max = 0.0;
};

template <typename Func>
[[nodiscard]] double measure_milliseconds(Func&& func) {
    const auto start = Clock::now();
    std::forward<Func>(func)();
    const auto finish = Clock::now();
    return std::chrono::duration<double, std::milli>(finish - start).count();
}

[[nodiscard]] std::uint64_t hash_pixels(std::span<const std::byte> bytes) noexcept {
    constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
    constexpr std::uint64_t fnv_prime = 1099511628211ULL;
    auto hash = fnv_offset;
    for (const auto byte : bytes) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(byte));
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] SummaryStats summarize(std::span<const double> values) {
    if (values.empty()) {
        return {};
    }

    auto sorted = std::vector<double>{values.begin(), values.end()};
    std::sort(sorted.begin(), sorted.end());

    const auto sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    const auto mid = sorted.size() / 2U;
    const auto median = sorted.size() % 2U == 0U
                            ? (sorted[mid - 1U] + sorted[mid]) * 0.5
                            : sorted[mid];

    return SummaryStats{.min = sorted.front(),
                        .mean = sum / static_cast<double>(sorted.size()),
                        .median = median,
                        .max = sorted.back()};
}

[[nodiscard]] std::string format_hex(std::uint64_t value) {
    return fmt::format("0x{:016X}", value);
}

void print_usage() {
    fmt::print("Usage: render_pipeline_perf [report.json] [--png frame.png] [--no-png] "
               "[--iterations N] [--warmup N]\n");
}

[[nodiscard]] PerfOptions parse_options(int argc, char** argv) {
    PerfOptions options;
    bool report_path_set = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            print_usage();
            std::exit(0);
        }
        if (argument == "--no-png") {
            options.write_png = false;
            continue;
        }
        if (argument == "--png" && index + 1 < argc) {
            options.png_path = fs::path(argv[++index]);
            options.write_png = true;
            continue;
        }
        if (argument == "--iterations" && index + 1 < argc) {
            options.measured_iterations = static_cast<std::size_t>(std::stoull(argv[++index]));
            continue;
        }
        if (argument == "--warmup" && index + 1 < argc) {
            options.warmup_iterations = static_cast<std::size_t>(std::stoull(argv[++index]));
            continue;
        }
        if (!report_path_set && !argument.starts_with("--")) {
            options.report_path = fs::path(argument);
            report_path_set = true;
            continue;
        }

        throw std::runtime_error(fmt::format("unknown argument: {}", argument));
    }

    if (options.write_png && options.png_path == fs::path{}) {
        options.png_path = options.report_path;
        options.png_path.replace_extension(".png");
    }

    return options;
}

void write_json_report(const fs::path& report_path, const PerfOptions& options,
                       double build_demo_ms, double setup_ms,
                       std::span<const FrameSample> samples) {
    if (!report_path.parent_path().empty()) {
        fs::create_directories(report_path.parent_path());
    }

    const auto collect = [&](auto member) {
        auto values = std::vector<double>{};
        values.reserve(samples.size());
        for (const auto& sample : samples) {
            values.push_back(sample.*member);
        }
        return values;
    };

    const auto render_stats = summarize(collect(&FrameSample::render_ms));
    const auto task_split_stats = summarize(collect(&FrameSample::task_split_ms));
    const auto resource_sync_stats = summarize(collect(&FrameSample::resource_sync_ms));
    const auto worker_record_stats = summarize(collect(&FrameSample::worker_record_ms));
    const auto command_submit_stats = summarize(collect(&FrameSample::command_submit_ms));
    const auto readback_stats = summarize(collect(&FrameSample::readback_ms));
    const auto total_stats = summarize(collect(&FrameSample::total_ms));

    std::ofstream report(report_path, std::ios::out | std::ios::trunc);
    if (!report) {
        throw std::runtime_error(fmt::format("failed to open report file: {}", report_path.string()));
    }

    report << std::fixed << std::setprecision(6);
    report << "{\n";
    report << "  \"sample\": \"render_pipeline_perf\",\n";
    report << "  \"report_path\": " << std::quoted(report_path.string()) << ",\n";
    report << "  \"png_path\": " << std::quoted(options.png_path.string()) << ",\n";
    report << "  \"canvas_width\": " << canvas_width << ",\n";
    report << "  \"canvas_height\": " << canvas_height << ",\n";
    report << "  \"dpi\": " << dpi << ",\n";
    report << "  \"warmup_iterations\": " << options.warmup_iterations << ",\n";
    report << "  \"measured_iterations\": " << options.measured_iterations << ",\n";
    report << "  \"build_demo_ms\": " << build_demo_ms << ",\n";
    report << "  \"setup_ms\": " << setup_ms << ",\n";

    const auto write_stats = [&](std::string_view name, const SummaryStats& stats) {
        report << "  \"" << name << "\": {\n";
        report << "    \"min_ms\": " << stats.min << ",\n";
        report << "    \"mean_ms\": " << stats.mean << ",\n";
        report << "    \"median_ms\": " << stats.median << ",\n";
        report << "    \"max_ms\": " << stats.max << "\n";
        report << "  },\n";
    };

    write_stats("render_ms", render_stats);
    write_stats("task_split_ms", task_split_stats);
    write_stats("resource_sync_ms", resource_sync_stats);
    write_stats("worker_record_ms", worker_record_stats);
    write_stats("command_submit_ms", command_submit_stats);
    write_stats("readback_ms", readback_stats);
    write_stats("total_ms", total_stats);

    report << "  \"frames\": [\n";
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        report << "    {\n";
        report << "      \"index\": " << index << ",\n";
        report << "      \"render_ms\": " << sample.render_ms << ",\n";
        report << "      \"task_split_ms\": " << sample.task_split_ms << ",\n";
        report << "      \"resource_sync_ms\": " << sample.resource_sync_ms << ",\n";
        report << "      \"worker_record_ms\": " << sample.worker_record_ms << ",\n";
        report << "      \"command_submit_ms\": " << sample.command_submit_ms << ",\n";
        report << "      \"readback_ms\": " << sample.readback_ms << ",\n";
        report << "      \"total_ms\": " << sample.total_ms << ",\n";
        report << "      \"pixel_hash\": " << std::quoted(format_hex(sample.pixel_hash)) << ",\n";
        report << "      \"work_item_count\": " << sample.work_item_count << ",\n";
        report << "      \"command_list_count\": " << sample.command_list_count << ",\n";
        report << "      \"parallel_recording\": " << (sample.parallel_recording ? "true" : "false")
               << "\n";
        report << "    }";
        report << (index + 1U == samples.size() ? "\n" : ",\n");
    }
    report << "  ]\n";
    report << "}\n";
}

[[nodiscard]] FrameSample render_frame(win32::D3D11DisplayListRenderer& renderer,
                                       win32::D3D11RenderDevice& device,
                                       const RenderTargetBundle& target,
                                       const DemoArtifacts& demo,
                                       const win32::D3D11RenderResourceCache& resource_cache,
                                       std::uint32_t width, std::uint32_t height,
                                       std::uint32_t& stride, std::vector<std::byte>& pixels) {
    FrameSample sample;
    sample.render_ms = measure_milliseconds([&] {
        renderer.render(device.d3d_context(), *target.target_view.Get(), demo.clear_color,
                        demo.scene.empty() ? nullptr : &demo.scene, demo.dirty_region, dpi,
                        width, height, resource_cache, &demo.frame_graph);
    });
    const auto renderer_metrics = renderer.last_timing_metrics();
    sample.task_split_ms = renderer_metrics.task_split_ms;
    sample.resource_sync_ms = renderer_metrics.resource_sync_ms;
    sample.worker_record_ms = renderer_metrics.worker_record_ms;
    sample.command_submit_ms = renderer_metrics.command_submit_ms;
    sample.work_item_count = renderer_metrics.work_item_count;
    sample.command_list_count = renderer_metrics.command_list_count;
    sample.parallel_recording = renderer_metrics.parallel_recording;

    sample.readback_ms = measure_milliseconds([&] {
        pixels = read_back_texture(device, *target.texture.Get(), width, height, stride);
    });
    sample.total_ms = sample.render_ms + sample.readback_ms;
    sample.pixel_hash = hash_pixels(pixels);
    return sample;
}

[[nodiscard]] DemoArtifacts build_mixed_perf_demo(std::uint32_t, std::uint32_t) {
    return build_demo();
}

[[nodiscard]] DemoArtifacts build_text_perf_demo(std::uint32_t width,
                                                  std::uint32_t height) {
    DemoArtifacts demo;

    rendering::TextEngine text_engine;
    rendering::TextStyle body_style;
    body_style.font_family = "Segoe UI";
    body_style.locale = "en-us";
    body_style.font_size = 26.0F;
    body_style.color = rendering::Color::rgba(34, 40, 52);
    body_style.wrapping = rendering::TextWrapping::Wrap;
    body_style.trimming = rendering::TextTrimming::WordEllipsis;
    body_style.fallback_font_families = {"Segoe UI Emoji", "Microsoft YaHei UI"};

    demo.text_layout = text_engine.layout_text(
        "Text benchmark. The goal is to stress glyph shaping, wrapping, selection rectangles, "
        "and layered text composition without mixing in image or SVG work. The same paragraph "
        "is repeated to amplify glyph atlas reuse and line layout overhead.",
        body_style,
        rendering::TextLayoutOptions{.max_width = static_cast<float>(width) - 120.0F,
                                     .max_height = 240.0F,
                                     .pixels_per_dip = 1.0F});

    const auto selection_range = text_engine.cluster_aligned_range(
        demo.text_layout, rendering::TextSelectionRange{.start_byte_offset = 0U,
                                                         .end_byte_offset = 92U});
    demo.selection_rects = text_engine.selection_rects(demo.text_layout, selection_range);
    demo.hit_test = text_engine.hit_test_point(demo.text_layout, {180.0F, 58.0F});
    demo.caret_metrics =
        text_engine.caret_metrics_for_byte_offset(demo.text_layout, selection_range.end_byte_offset);

    rendering::RenderCommandRecorder recorder;
    const auto canvas = layout::Rect{0.0F, 0.0F, static_cast<float>(width),
                                     static_cast<float>(height)};
    demo.visual_regions.full_canvas = canvas;
    demo.visual_regions.text_panel = layout::Rect{page_margin, page_margin,
                                                  static_cast<float>(width) - page_margin * 2.0F,
                                                  static_cast<float>(height) - page_margin * 2.0F};

    recorder.save();
    recorder.fill_rect(canvas, rendering::Color::rgba(245, 247, 250));
    draw_panel(recorder, demo.visual_regions.text_panel, "Text Benchmark",
               "pure glyph layout, selection rectangles and layered text");

    const auto content = layout::Rect{demo.visual_regions.text_panel.x + 28.0F,
                                      demo.visual_regions.text_panel.y + 92.0F,
                                      demo.visual_regions.text_panel.width - 56.0F,
                                      demo.visual_regions.text_panel.height - 136.0F};
    demo.visual_regions.text_content = content;
    demo.visual_regions.text_selection = content;

    recorder.draw_text("Text-heavy workload", {content.x, content.y - 34.0F, 240.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});

    std::vector<layout::Rect> translated_selection_rects;
    translated_selection_rects.reserve(demo.selection_rects.size());
    for (const auto rect : demo.selection_rects) {
        translated_selection_rects.push_back(core::offset_rect(rect, core::Point{content.x, content.y}));
    }
    fill_selection_rects(recorder, translated_selection_rects,
                         rendering::Color::rgba(140, 197, 255, 116));
    recorder.draw_text_layout(demo.text_layout, {content.x, content.y});
    recorder.fill_rect(core::offset_rect(demo.caret_metrics.rect, core::Point{content.x, content.y}),
                       rendering::Color::rgba(36, 91, 180));

    recorder.draw_text("Repeated paragraph", {content.x, content.y + 280.0F, 260.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    auto repeated_style = body_style;
    repeated_style.font_size = 22.0F;
    repeated_style.color = rendering::Color::rgba(58, 70, 90);
    const auto repeated_layout = text_engine.layout_text(
        "One more paragraph to keep the glyph atlas warm and to force a second text layout pass "
        "inside the same frame. This helps separate shaping cost from later image or SVG work.",
        repeated_style,
        rendering::TextLayoutOptions{.max_width = content.width,
                                     .max_height = 140.0F,
                                     .pixels_per_dip = 1.0F});
    recorder.draw_text_layout(repeated_layout, {content.x, content.y + 316.0F});

    recorder.restore();
    demo.commands = recorder.take_command_list();
    demo.scene.update_from_commands(demo.commands, "render_pipeline_perf_text");
    demo.frame_graph = rendering::build_render_frame_graph(demo.scene);
    demo.dirty_region.add(canvas);
    demo.dirty_region.add(demo.visual_regions.text_panel);
    demo.dirty_region.add(content);
    return demo;
}

[[nodiscard]] DemoArtifacts build_svg_perf_demo(std::uint32_t width,
                                                std::uint32_t height) {
    DemoArtifacts demo;
    rendering::RenderCommandRecorder recorder;

    const auto canvas = layout::Rect{0.0F, 0.0F, static_cast<float>(width),
                                     static_cast<float>(height)};
    demo.visual_regions.full_canvas = canvas;
    const auto panel = layout::Rect{page_margin, page_margin,
                                    static_cast<float>(width) - page_margin * 2.0F,
                                    static_cast<float>(height) - page_margin * 2.0F};
    demo.visual_regions.svg_panel = panel;
    recorder.save();
    recorder.fill_rect(canvas, rendering::Color::rgba(245, 247, 250));
    draw_panel(recorder, panel, "SVG Benchmark",
               "pure SVG path parsing, curve flattening and small-icon scaling");

    demo.visual_regions.svg_detail_board = layout::Rect{panel.x + 28.0F, panel.y + 112.0F, 814.0F, 286.0F};
    demo.visual_regions.svg_scale_board = layout::Rect{panel.x + 28.0F, panel.y + 418.0F, 814.0F, 214.0F};

    recorder.draw_text("Detail probes", {panel.x + 28.0F, panel.y + 82.0F, 220.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    const std::array<SvgPathStressSpec, 4> thin_specs = {{
        {.label = "Line", .path = svg_straight_line_path, .color = rendering::Color::rgba(214, 61, 61), .view_width = 220.0F, .view_height = 120.0F},
        {.label = "Dense", .path = svg_dense_diagonal_path, .color = rendering::Color::rgba(45, 113, 198), .view_width = 152.0F, .view_height = 120.0F},
        {.label = "Join", .path = svg_sharp_join_path, .color = rendering::Color::rgba(111, 85, 214), .view_width = 236.0F, .view_height = 120.0F},
        {.label = "Circle", .path = svg_circle_path, .color = rendering::Color::rgba(38, 154, 109), .view_width = 100.0F, .view_height = 100.0F},
    }};
    const auto svg_stress_thin = rendering::GeometryStrokeStyle{.width = 1.0F,
                                                                .start_cap = rendering::StrokeLineCap::Flat,
                                                                .end_cap = rendering::StrokeLineCap::Flat,
                                                                .line_join = rendering::StrokeLineJoin::Miter,
                                                                .dash_style = rendering::StrokeDashStyle::Solid};
    constexpr float svg_cell_width = 250.0F;
    constexpr float svg_cell_height = 120.0F;
    constexpr float svg_cell_gap_x = 18.0F;
    constexpr float svg_cell_gap_y = 14.0F;
    for (std::size_t index = 0; index < thin_specs.size(); ++index) {
        const auto row = static_cast<float>(index / 2U);
        const auto column = static_cast<float>(index % 2U);
        const auto rect = layout::Rect{panel.x + 28.0F + column * (svg_cell_width + svg_cell_gap_x),
                                       panel.y + 112.0F + row * (svg_cell_height + svg_cell_gap_y),
                                       svg_cell_width,
                                       svg_cell_height};
        draw_svg_stress_cell(recorder, rect, thin_specs[index], svg_stress_thin, "1px");
    }

    recorder.draw_text("Scale probes", {panel.x + 28.0F, panel.y + 418.0F, 220.0F, 24.0F},
                       rendering::TextStyle{.font_family = "Segoe UI Semibold",
                                            .locale = "en-us",
                                            .font_size = 18.0F,
                                            .color = rendering::Color::rgba(68, 80, 101)});
    const std::array<SvgPathStressSpec, 4> scale_specs = {{
        {.label = "Dense diagonals", .path = svg_dense_diagonal_path, .color = rendering::Color::rgba(45, 113, 198), .view_width = 152.0F, .view_height = 120.0F},
        {.label = "Sharp miter join", .path = svg_sharp_join_path, .color = rendering::Color::rgba(111, 85, 214), .view_width = 236.0F, .view_height = 120.0F},
        {.label = "Arc", .path = svg_arc_path, .color = rendering::Color::rgba(38, 154, 109), .view_width = 236.0F, .view_height = 120.0F},
        {.label = "Circle", .path = svg_circle_path, .color = rendering::Color::rgba(60, 98, 173), .view_width = 100.0F, .view_height = 100.0F},
    }};
    const auto svg_stress_thick = rendering::GeometryStrokeStyle{.width = 4.5F,
                                                                 .start_cap = rendering::StrokeLineCap::Round,
                                                                 .end_cap = rendering::StrokeLineCap::Round,
                                                                 .line_join = rendering::StrokeLineJoin::Round,
                                                                 .dash_style = rendering::StrokeDashStyle::Solid};
    for (std::size_t index = 0; index < scale_specs.size(); ++index) {
        const auto column = static_cast<float>(index % 2U);
        const auto row = static_cast<float>(index / 2U);
        const auto rect = layout::Rect{panel.x + 28.0F + column * (svg_cell_width + svg_cell_gap_x),
                                       panel.y + 418.0F + row * (svg_cell_height + svg_cell_gap_y),
                                       svg_cell_width,
                                       svg_cell_height};
        draw_svg_stress_cell(recorder, rect, scale_specs[index], svg_stress_thick, "4.5px");
    }

    recorder.restore();
    demo.commands = recorder.take_command_list();
    demo.scene.update_from_commands(demo.commands, "render_pipeline_perf_svg");
    demo.frame_graph = rendering::build_render_frame_graph(demo.scene);
    demo.dirty_region.add(canvas);
    demo.dirty_region.add(panel);
    demo.dirty_region.add(demo.visual_regions.svg_detail_board);
    demo.dirty_region.add(demo.visual_regions.svg_scale_board);
    return demo;
}

[[nodiscard]] DemoArtifacts build_image_perf_demo(std::uint32_t width,
                                                  std::uint32_t height) {
    DemoArtifacts demo;
    demo.image_upload = make_demo_texture(demo_texture_id);
    demo.circle_image_upload = make_demo_texture(demo_circle_texture_id, true);

    rendering::RenderCommandRecorder recorder;
    const auto canvas = layout::Rect{0.0F, 0.0F, static_cast<float>(width),
                                     static_cast<float>(height)};
    demo.visual_regions.full_canvas = canvas;
    const auto panel = layout::Rect{page_margin, page_margin,
                                    static_cast<float>(width) - page_margin * 2.0F,
                                    static_cast<float>(height) - page_margin * 2.0F};
    recorder.save();
    recorder.fill_rect(canvas, rendering::Color::rgba(245, 247, 250));
    draw_panel(recorder, panel, "Image Benchmark",
               "pure image sampling, opacity and clipping with repeated texture draws");

    constexpr std::array<layout::Rect, 6U> tiles = {
        layout::Rect{0.0F, 0.0F, 160.0F, 160.0F}, layout::Rect{0.0F, 0.0F, 140.0F, 120.0F},
        layout::Rect{0.0F, 0.0F, 180.0F, 100.0F}, layout::Rect{0.0F, 0.0F, 120.0F, 180.0F},
        layout::Rect{0.0F, 0.0F, 150.0F, 150.0F}, layout::Rect{0.0F, 0.0F, 196.0F, 132.0F}};
    const auto tile_origin = core::Point{panel.x + 30.0F, panel.y + 120.0F};
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        const auto column = static_cast<float>(index % 3U);
        const auto row = static_cast<float>(index / 3U);
        const auto destination = layout::Rect{tile_origin.x + column * 204.0F,
                                               tile_origin.y + row * 214.0F,
                                               tiles[index].width,
                                               tiles[index].height};
        recorder.fill_rounded_rect(destination, core::CornerRadius::uniform(18.0F),
                                   rendering::Color::rgba(255, 255, 255, 236));
        recorder.stroke_rounded_rect(destination, core::CornerRadius::uniform(18.0F),
                                     rendering::Color::rgba(224, 229, 236), 1.0F);
        recorder.draw_image(index % 2U == 0U ? demo_texture_id : demo_circle_texture_id,
                            rendering::RenderImageOptions{.destination = destination,
                                                          .source = {0.0F, 0.0F, 160.0F, 160.0F},
                                                          .opacity = 0.90F});
    }

    recorder.restore();
    demo.commands = recorder.take_command_list();
    demo.scene.update_from_commands(demo.commands, "render_pipeline_perf_image");
    demo.frame_graph = rendering::build_render_frame_graph(demo.scene);
    demo.dirty_region.add(canvas);
    demo.dirty_region.add(panel);
    return demo;
}

[[nodiscard]] DemoArtifacts build_dense_dirty_perf_demo(std::uint32_t width,
                                                        std::uint32_t height) {
    DemoArtifacts demo;

    rendering::RenderCommandRecorder recorder;
    const auto canvas = layout::Rect{0.0F, 0.0F, static_cast<float>(width),
                                     static_cast<float>(height)};
    demo.visual_regions.full_canvas = canvas;
    recorder.save();
    recorder.fill_rect(canvas, rendering::Color::rgba(245, 247, 250));

    const auto columns = 32U;
    const auto rows = 18U;
    const auto cell_width = static_cast<float>(width) / static_cast<float>(columns);
    const auto cell_height = static_cast<float>(height) / static_cast<float>(rows);
    for (std::uint32_t row = 0U; row < rows; ++row) {
        for (std::uint32_t column = 0U; column < columns; ++column) {
            const auto x = cell_width * static_cast<float>(column);
            const auto y = cell_height * static_cast<float>(row);
            const auto rect = layout::Rect{x + 1.0F, y + 1.0F, cell_width - 2.0F, cell_height - 2.0F};
            const auto red = static_cast<std::uint8_t>(64U + (column * 173U) / columns);
            const auto green = static_cast<std::uint8_t>(72U + (row * 139U) / rows);
            const auto blue = static_cast<std::uint8_t>(136U + ((column + row) * 59U) % 96U);
            recorder.fill_rect(rect, rendering::Color::rgba(red, green, blue, 255));
            demo.dirty_region.add(rect);
        }
    }

    recorder.restore();
    demo.commands = recorder.take_command_list();
    demo.scene.update_from_commands(demo.commands, "render_pipeline_perf_dense_dirty");
    demo.frame_graph = rendering::build_render_frame_graph(demo.scene);
    demo.dirty_region.add(canvas);
    return demo;
}

struct PerfScenarioSpec {
    std::string_view name;
    std::uint32_t canvas_width = 0U;
    std::uint32_t canvas_height = 0U;
    DemoArtifacts (*build)(std::uint32_t, std::uint32_t) = nullptr;
};

struct PerfScenarioResult {
    std::string_view name;
    fs::path png_path;
    std::uint32_t canvas_width = 0U;
    std::uint32_t canvas_height = 0U;
    double build_demo_ms = 0.0;
    double setup_ms = 0.0;
    std::size_t command_count = 0U;
    std::size_t dirty_rect_count = 0U;
    std::size_t dirty_tree_nodes = 0U;
    std::size_t frame_graph_pass_count = 0U;
    std::size_t frame_graph_draw_calls = 0U;
    std::size_t text_glyph_count = 0U;
    std::size_t text_line_count = 0U;
    std::vector<FrameSample> frames;
};

[[nodiscard]] fs::path scenario_png_path(const fs::path& base_path, std::string_view name) {
    if (base_path.empty()) {
        return {};
    }

    const auto directory = base_path.parent_path();
    const auto stem = base_path.stem().string();
    const auto extension = base_path.extension().string();
    return directory /
           fmt::format("{}_{}{}", stem, name, extension.empty() ? std::string_view{".png"}
                                                                       : std::string_view{extension});
}

void upload_render_resources(win32::D3D11RenderDevice& device,
                             win32::D3D11RenderResourceCache& resource_cache,
                             const DemoArtifacts& demo) {
    const auto upload_if_needed = [&](const rendering::RenderResourceUpload& upload) {
        if (upload.action != rendering::RenderResourceAction::Upload || upload.id.value == 0U ||
            upload.width == 0U || upload.height == 0U || upload.payload.empty()) {
            return;
        }
        resource_cache.upload(device.d3d_device(), upload);
    };

    upload_if_needed(demo.image_upload);
    upload_if_needed(demo.circle_image_upload);
}

[[nodiscard]] PerfScenarioResult run_scenario(const PerfScenarioSpec& spec,
                                              const PerfOptions& options) {
    PerfScenarioResult result;
    result.name = spec.name;
    result.canvas_width = spec.canvas_width;
    result.canvas_height = spec.canvas_height;
    result.png_path = scenario_png_path(options.png_path, spec.name);

    fmt::print("scenario {}\n", spec.name);

    const auto build_start = Clock::now();
    auto demo = spec.build(spec.canvas_width, spec.canvas_height);
    const auto build_finish = Clock::now();
    result.build_demo_ms =
        std::chrono::duration<double, std::milli>(build_finish - build_start).count();

    const auto setup_start = Clock::now();
    win32::D3D11RenderDevice device;
    win32::D3D11RenderResourceCache resource_cache;
    upload_render_resources(device, resource_cache, demo);
    auto render_target = create_render_target(device, spec.canvas_width, spec.canvas_height);
    win32::D3D11DisplayListRenderer renderer(device.d3d_device());
    const auto setup_finish = Clock::now();
    result.setup_ms =
        std::chrono::duration<double, std::milli>(setup_finish - setup_start).count();

    result.command_count = demo.commands.command_count();
    result.dirty_rect_count = demo.dirty_region.rects().size();
    result.dirty_tree_nodes = demo.dirty_region.node_count();
    result.frame_graph_pass_count = demo.frame_graph.passes.size();
    result.frame_graph_draw_calls = demo.frame_graph.estimated_draw_call_count;
    result.text_glyph_count = demo.text_layout.glyphs.size();
    result.text_line_count = demo.text_layout.lines.size();

    std::vector<std::byte> pixels;
    std::uint32_t stride = 0U;
    result.frames.reserve(options.measured_iterations);

    for (std::size_t index = 0; index < options.warmup_iterations; ++index) {
        (void)render_frame(renderer, device, render_target, demo, resource_cache,
                           spec.canvas_width, spec.canvas_height, stride, pixels);
    }

    for (std::size_t index = 0; index < options.measured_iterations; ++index) {
        result.frames.push_back(
            render_frame(renderer, device, render_target, demo, resource_cache, spec.canvas_width,
                         spec.canvas_height, stride, pixels));
        const auto& sample = result.frames.back();
        fmt::print("  frame {:02}: render {:8.3f} ms (split {:6.3f}, record {:6.3f}, "
                   "submit {:6.3f}, sync {:6.3f}), readback {:8.3f} ms, total {:8.3f} ms, "
                   "lists {:02}, hash {}\n",
                   index, sample.render_ms, sample.task_split_ms, sample.worker_record_ms,
                   sample.command_submit_ms, sample.resource_sync_ms, sample.readback_ms,
                   sample.total_ms, sample.command_list_count, format_hex(sample.pixel_hash));
    }

    if (options.write_png && !result.png_path.empty()) {
        if (!result.png_path.parent_path().empty()) {
            fs::create_directories(result.png_path.parent_path());
        }
        save_png(result.png_path, spec.canvas_width, spec.canvas_height, stride, pixels);
    }

    resource_cache.clear();
    return result;
}

[[nodiscard]] SummaryStats summarize_frames(std::span<const FrameSample> samples,
                                            double FrameSample::*member) {
    auto values = std::vector<double>{};
    values.reserve(samples.size());
    for (const auto& sample : samples) {
        values.push_back(sample.*member);
    }
    return summarize(values);
}

void write_json_report(const fs::path& report_path, std::span<const PerfScenarioResult> scenarios) {
    if (!report_path.parent_path().empty()) {
        fs::create_directories(report_path.parent_path());
    }

    std::ofstream report(report_path, std::ios::out | std::ios::trunc);
    if (!report) {
        throw std::runtime_error(fmt::format("failed to open report file: {}", report_path.string()));
    }

    report << std::fixed << std::setprecision(6);
    report << "{\n";
    report << "  \"sample\": \"render_pipeline_perf\",\n";
    report << "  \"scenarios\": [\n";
    for (std::size_t scenario_index = 0; scenario_index < scenarios.size(); ++scenario_index) {
        const auto& scenario = scenarios[scenario_index];
        const auto render_stats = summarize_frames(scenario.frames, &FrameSample::render_ms);
        const auto task_split_stats =
            summarize_frames(scenario.frames, &FrameSample::task_split_ms);
        const auto resource_sync_stats =
            summarize_frames(scenario.frames, &FrameSample::resource_sync_ms);
        const auto worker_record_stats =
            summarize_frames(scenario.frames, &FrameSample::worker_record_ms);
        const auto command_submit_stats =
            summarize_frames(scenario.frames, &FrameSample::command_submit_ms);
        const auto readback_stats = summarize_frames(scenario.frames, &FrameSample::readback_ms);
        const auto total_stats = summarize_frames(scenario.frames, &FrameSample::total_ms);

        report << "    {\n";
        report << "      \"name\": " << std::quoted(std::string(scenario.name)) << ",\n";
        report << "      \"canvas_width\": " << scenario.canvas_width << ",\n";
        report << "      \"canvas_height\": " << scenario.canvas_height << ",\n";
        report << "      \"png_path\": " << std::quoted(scenario.png_path.string()) << ",\n";
        report << "      \"build_demo_ms\": " << scenario.build_demo_ms << ",\n";
        report << "      \"setup_ms\": " << scenario.setup_ms << ",\n";
        report << "      \"command_count\": " << scenario.command_count << ",\n";
        report << "      \"dirty_rect_count\": " << scenario.dirty_rect_count << ",\n";
        report << "      \"dirty_tree_nodes\": " << scenario.dirty_tree_nodes << ",\n";
        report << "      \"frame_graph_pass_count\": " << scenario.frame_graph_pass_count << ",\n";
        report << "      \"frame_graph_draw_calls\": " << scenario.frame_graph_draw_calls << ",\n";
        report << "      \"text_glyph_count\": " << scenario.text_glyph_count << ",\n";
        report << "      \"text_line_count\": " << scenario.text_line_count << ",\n";
        report << "      \"render_ms\": {\n";
        report << "        \"min_ms\": " << render_stats.min << ",\n";
        report << "        \"mean_ms\": " << render_stats.mean << ",\n";
        report << "        \"median_ms\": " << render_stats.median << ",\n";
        report << "        \"max_ms\": " << render_stats.max << "\n";
        report << "      },\n";
        const auto write_metric_stats = [&](std::string_view key, const SummaryStats& stats) {
            report << "      \"" << key << "\": {\n";
            report << "        \"min_ms\": " << stats.min << ",\n";
            report << "        \"mean_ms\": " << stats.mean << ",\n";
            report << "        \"median_ms\": " << stats.median << ",\n";
            report << "        \"max_ms\": " << stats.max << "\n";
            report << "      },\n";
        };
        write_metric_stats("task_split_ms", task_split_stats);
        write_metric_stats("resource_sync_ms", resource_sync_stats);
        write_metric_stats("worker_record_ms", worker_record_stats);
        write_metric_stats("command_submit_ms", command_submit_stats);
        report << "      \"readback_ms\": {\n";
        report << "        \"min_ms\": " << readback_stats.min << ",\n";
        report << "        \"mean_ms\": " << readback_stats.mean << ",\n";
        report << "        \"median_ms\": " << readback_stats.median << ",\n";
        report << "        \"max_ms\": " << readback_stats.max << "\n";
        report << "      },\n";
        report << "      \"total_ms\": {\n";
        report << "        \"min_ms\": " << total_stats.min << ",\n";
        report << "        \"mean_ms\": " << total_stats.mean << ",\n";
        report << "        \"median_ms\": " << total_stats.median << ",\n";
        report << "        \"max_ms\": " << total_stats.max << "\n";
        report << "      },\n";
        report << "      \"frames\": [\n";
        for (std::size_t frame_index = 0; frame_index < scenario.frames.size(); ++frame_index) {
            const auto& frame = scenario.frames[frame_index];
            report << "        {\n";
            report << "          \"index\": " << frame_index << ",\n";
            report << "          \"render_ms\": " << frame.render_ms << ",\n";
            report << "          \"task_split_ms\": " << frame.task_split_ms << ",\n";
            report << "          \"resource_sync_ms\": " << frame.resource_sync_ms << ",\n";
            report << "          \"worker_record_ms\": " << frame.worker_record_ms << ",\n";
            report << "          \"command_submit_ms\": " << frame.command_submit_ms << ",\n";
            report << "          \"readback_ms\": " << frame.readback_ms << ",\n";
            report << "          \"total_ms\": " << frame.total_ms << ",\n";
            report << "          \"pixel_hash\": " << std::quoted(format_hex(frame.pixel_hash))
                   << ",\n";
            report << "          \"work_item_count\": " << frame.work_item_count << ",\n";
            report << "          \"command_list_count\": " << frame.command_list_count << ",\n";
            report << "          \"parallel_recording\": "
                   << (frame.parallel_recording ? "true" : "false") << "\n";
            report << "        }";
            report << (frame_index + 1U == scenario.frames.size() ? "\n" : ",\n");
        }
        report << "      ]\n";
        report << "    }";
        report << (scenario_index + 1U == scenarios.size() ? "\n" : ",\n");
    }
    report << "  ]\n";
    report << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);

        fmt::print("render_pipeline_perf\n");
        fmt::print("  report: {}\n", options.report_path.string());
        if (options.write_png) {
            fmt::print("  png base: {}\n", options.png_path.string());
        }
        fmt::print("  warmup iterations: {}\n", options.warmup_iterations);
        fmt::print("  measured iterations: {}\n", options.measured_iterations);

        const std::array<PerfScenarioSpec, 5U> scenarios = {{
            PerfScenarioSpec{"mixed", canvas_width, canvas_height, &build_mixed_perf_demo},
            PerfScenarioSpec{"text", 1280U, 760U, &build_text_perf_demo},
            PerfScenarioSpec{"svg", 1280U, 760U, &build_svg_perf_demo},
            PerfScenarioSpec{"image", 1280U, 760U, &build_image_perf_demo},
            PerfScenarioSpec{"dense_dirty", 1280U, 720U, &build_dense_dirty_perf_demo},
        }};

        auto results = std::vector<PerfScenarioResult>{};
        results.reserve(scenarios.size());
        for (const auto& scenario : scenarios) {
            results.push_back(run_scenario(scenario, options));
            const auto& result = results.back();
            const auto render_stats = summarize_frames(result.frames, &FrameSample::render_ms);
            const auto total_stats = summarize_frames(result.frames, &FrameSample::total_ms);
            fmt::print("  scenario {} summary: render avg {:8.3f} ms, total avg {:8.3f} ms\n",
                       result.name, render_stats.mean, total_stats.mean);
        }

        write_json_report(options.report_path, results);

        fmt::print("  report written: {}\n", options.report_path.string());
        if (options.write_png) {
            fmt::print("  png files written under: {}\n", options.png_path.string());
        }
        return 0;
    } catch (const std::exception& exception) {
        fmt::print(stderr, "render_pipeline_perf failed: {}\n", exception.what());
        return 1;
    }
}
