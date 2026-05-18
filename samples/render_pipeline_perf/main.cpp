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
    double readback_ms = 0.0;
    double total_ms = 0.0;
    std::uint64_t pixel_hash = 0U;
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
    write_stats("readback_ms", readback_stats);
    write_stats("total_ms", total_stats);

    report << "  \"frames\": [\n";
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        report << "    {\n";
        report << "      \"index\": " << index << ",\n";
        report << "      \"render_ms\": " << sample.render_ms << ",\n";
        report << "      \"readback_ms\": " << sample.readback_ms << ",\n";
        report << "      \"total_ms\": " << sample.total_ms << ",\n";
        report << "      \"pixel_hash\": " << std::quoted(format_hex(sample.pixel_hash)) << "\n";
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
                                       std::uint32_t& stride, std::vector<std::byte>& pixels) {
    FrameSample sample;
    sample.render_ms = measure_milliseconds([&] {
        renderer.render(device.d3d_context(), *target.target_view.Get(), demo.clear_color,
                        demo.scene.empty() ? nullptr : &demo.scene, demo.dirty_region, dpi,
                        canvas_width, canvas_height, resource_cache, &demo.frame_graph);
    });

    sample.readback_ms = measure_milliseconds([&] {
        pixels = read_back_texture(device, *target.texture.Get(), canvas_width, canvas_height,
                                   stride);
    });
    sample.total_ms = sample.render_ms + sample.readback_ms;
    sample.pixel_hash = hash_pixels(pixels);
    return sample;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);

        fmt::print("render_pipeline_perf\n");
        fmt::print("  report: {}\n", options.report_path.string());
        if (options.write_png) {
            fmt::print("  png: {}\n", options.png_path.string());
        }
        fmt::print("  warmup iterations: {}\n", options.warmup_iterations);
        fmt::print("  measured iterations: {}\n", options.measured_iterations);

        const auto build_start = Clock::now();
        auto demo = build_demo();
        const auto build_finish = Clock::now();
        const auto build_demo_ms =
            std::chrono::duration<double, std::milli>(build_finish - build_start).count();

        const auto setup_start = Clock::now();
        win32::D3D11RenderDevice device;
        win32::D3D11RenderResourceCache resource_cache;
        resource_cache.upload(device.d3d_device(), demo.image_upload);
        resource_cache.upload(device.d3d_device(), demo.circle_image_upload);
        auto render_target = create_render_target(device, canvas_width, canvas_height);
        win32::D3D11DisplayListRenderer renderer(device.d3d_device());
        const auto setup_finish = Clock::now();
        const auto setup_ms =
            std::chrono::duration<double, std::milli>(setup_finish - setup_start).count();

        std::vector<FrameSample> samples;
        samples.reserve(options.measured_iterations);
        std::vector<std::byte> pixels;
        std::uint32_t stride = 0U;

        for (std::size_t index = 0; index < options.warmup_iterations; ++index) {
            (void)render_frame(renderer, device, render_target, demo, resource_cache, stride,
                               pixels);
        }

        for (std::size_t index = 0; index < options.measured_iterations; ++index) {
            samples.push_back(
                render_frame(renderer, device, render_target, demo, resource_cache, stride, pixels));
            const auto& sample = samples.back();
            fmt::print("  frame {:02}: render {:8.3f} ms, readback {:8.3f} ms, total {:8.3f} ms, hash {}\n",
                       index, sample.render_ms, sample.readback_ms, sample.total_ms,
                       format_hex(sample.pixel_hash));
        }

        if (options.write_png) {
            save_png(options.png_path, canvas_width, canvas_height, stride, pixels);
        }

        write_json_report(options.report_path, options, build_demo_ms, setup_ms, samples);

        const auto render_values = [&] {
            auto values = std::vector<double>{};
            values.reserve(samples.size());
            for (const auto& sample : samples) {
                values.push_back(sample.total_ms);
            }
            return values;
        }();
        const auto total_stats = summarize(render_values);

        fmt::print("  summary: min {:8.3f} ms, avg {:8.3f} ms, median {:8.3f} ms, max {:8.3f} ms\n",
                   total_stats.min, total_stats.mean, total_stats.median, total_stats.max);
        fmt::print("  report written: {}\n", options.report_path.string());
        if (options.write_png) {
            fmt::print("  frame written: {}\n", options.png_path.string());
        }
        resource_cache.clear();
        return 0;
    } catch (const std::exception& exception) {
        fmt::print(stderr, "render_pipeline_perf failed: {}\n", exception.what());
        return 1;
    }
}