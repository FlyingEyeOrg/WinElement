#define WINELEMENT_RENDER_PIPELINE_DEMO_AS_LIBRARY
#include "../render_pipeline_demo/main.cpp"

#ifdef MessageBox
#undef MessageBox
#endif

#include <winelement/controls.hpp>
#include <winelement/elements.hpp>
#include <winelement/layout.hpp>
#include <winelement/rendering.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace winelement;
namespace fs = std::filesystem;

constexpr auto probe_width = 900U;
constexpr auto probe_height = 520U;
constexpr auto probe_dpi = 96.0F;

enum class ProbeKind { MessageBox, Dialog };
enum class ProbeBackground { Clean, Blocks, PathOverflow, Seams };

class ProbeSurface final : public controls::Panel {
  public:
    explicit ProbeSurface(ProbeBackground background) : background_(background) {}

  protected:
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override {
        context.fill_rect(absolute_frame, rendering::Color::rgba(246, 248, 252));

        if (background_ == ProbeBackground::Clean || background_ == ProbeBackground::PathOverflow) {
            return;
        }

        const auto card = layout::Rect{48.0F, 44.0F, probe_width - 96.0F, probe_height - 88.0F};
        context.fill_rounded_rect(card, rendering::CornerRadius::uniform(6.0F),
                                  rendering::Color::rgba(255, 255, 255));

        const auto inner = layout::Rect{72.0F, 84.0F, probe_width - 144.0F, 72.0F};
        context.fill_rounded_rect(inner, rendering::CornerRadius::uniform(4.0F),
                                  rendering::Color::rgba(250, 251, 253));

        if (background_ == ProbeBackground::Blocks) {
            return;
        }

        context.stroke_pixel_snapped_rect(card, rendering::Color::rgba(40, 40, 40, 210), 1.0F);
        context.stroke_pixel_snapped_rect(inner, rendering::Color::rgba(64, 158, 255, 190), 1.0F);

        for (const auto y : {104.0F, 156.0F, 364.0F}) {
            context.draw_line({0.0F, y}, {static_cast<float>(probe_width), y},
                              rendering::Color::rgba(0, 0, 0, 190), 1.0F);
        }
    }

  private:
    ProbeBackground background_ = ProbeBackground::Seams;
};

[[nodiscard]] rendering::RenderScene build_probe_scene(ProbeKind kind,
                                                       ProbeBackground background) {
    winelement::layout::LayoutEngineOptions layout_options;
    layout_options.point_scale_factor = 0.0F;
    winelement::layout::LayoutEngine engine(layout_options);

    ProbeSurface root(background);
    root.bind_layout_tree(engine);
    root.configure_layout([](winelement::layout::LayoutElement& item) {
        item.set_size(winelement::layout::Length::points(static_cast<float>(probe_width)),
                      winelement::layout::Length::points(static_cast<float>(probe_height)));
    });

    if (background == ProbeBackground::PathOverflow) {
        auto& path = root.append_new_child<controls::Path>();
        path.set_data("M -900 0 L 900 0")
            .clear_fill()
            .set_stroke(rendering::Color::rgba(0, 0, 0, 220))
            .set_stroke_width(2.0F)
            .set_stretch(controls::PathStretch::None);
        path.configure_layout([](winelement::layout::LayoutElement& item) {
            item.set_position_type(winelement::layout::PositionType::Absolute)
                .set_position(winelement::layout::Edge::Left,
                              winelement::layout::Length::points(438.0F))
                .set_position(winelement::layout::Edge::Top,
                              winelement::layout::Length::points(104.0F))
                .set_size(winelement::layout::Length::points(24.0F),
                          winelement::layout::Length::points(24.0F));
        });
    }

    root.calculate_layout(
        winelement::layout::LayoutConstraints{.width = static_cast<float>(probe_width),
                                              .height = static_cast<float>(probe_height)});

    if (kind == ProbeKind::MessageBox) {
        controls::MessageBox::show(
            root, controls::MessageBoxOptions{.title = "MessageBox mask probe",
                                              .message = "The mask should stay light while lines "
                                                         "behind it do not become modal seams.",
                                              .kind = controls::MessageBoxKind::Confirm,
                                              .show_cancel_button = true,
                                              .modal = true,
                                              .close_on_click_modal = false});
    } else {
        controls::Dialog::show(
            root, controls::DialogOptions{.title = "Dialog mask probe",
                                          .body = "The same backdrop is used by Dialog. The "
                                                  "background includes deliberate horizontal lines.",
                                          .show_cancel_button = true,
                                          .modal = true,
                                          .close_on_click_modal = false});
    }

    rendering::RenderScene scene;
    rendering::DirtyRegion dirty;
    root.commit_render_scene(scene, &dirty);
    return scene;
}

[[nodiscard]] std::vector<std::byte> render_probe(ProbeKind kind, ProbeBackground background,
                                                  std::uint32_t& stride) {
    const auto scene = build_probe_scene(kind, background);
    auto frame_graph = rendering::build_render_frame_graph(scene);
    rendering::DirtyRegion dirty;
    dirty.add(layout::Rect{0.0F, 0.0F, static_cast<float>(probe_width),
                           static_cast<float>(probe_height)});

    win32::D3D11RenderDevice device;
    win32::D3D11RenderResourceCache resource_cache;
    auto render_target = create_render_target(device, probe_width, probe_height);
    win32::D3D11DisplayListRenderer renderer(device.d3d_device());
    renderer.render(device.d3d_context(), *render_target.target_view.Get(),
                    rendering::Color::rgba(246, 248, 252), &scene, dirty, probe_dpi, probe_width,
                    probe_height, resource_cache, &frame_graph);

    auto pixels =
        read_back_texture(device, *render_target.texture.Get(), probe_width, probe_height, stride);
    resource_cache.clear();
    return pixels;
}

[[nodiscard]] float luma_at(const std::vector<std::byte>& pixels, std::uint32_t stride,
                            std::uint32_t x, std::uint32_t y) {
    const auto offset = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4U;
    const auto blue = static_cast<float>(std::to_integer<unsigned char>(pixels[offset]));
    const auto green = static_cast<float>(std::to_integer<unsigned char>(pixels[offset + 1U]));
    const auto red = static_cast<float>(std::to_integer<unsigned char>(pixels[offset + 2U]));
    return 0.2126F * red + 0.7152F * green + 0.0722F * blue;
}

[[nodiscard]] float row_average_luma(const std::vector<std::byte>& pixels, std::uint32_t stride,
                                     std::uint32_t y, std::uint32_t left,
                                     std::uint32_t right) {
    auto total = 0.0F;
    auto count = 0U;
    for (auto x = left; x < right; ++x) {
        total += luma_at(pixels, stride, x, y);
        ++count;
    }
    return count == 0U ? 0.0F : total / static_cast<float>(count);
}

[[nodiscard]] float line_contrast(const std::vector<std::byte>& pixels, std::uint32_t stride,
                                  std::uint32_t y) {
    constexpr auto left = 80U;
    constexpr auto right = probe_width - 80U;
    const auto line = row_average_luma(pixels, stride, y, left, right);
    const auto before = row_average_luma(pixels, stride, y - 4U, left, right);
    const auto after = row_average_luma(pixels, stride, y + 4U, left, right);
    return std::abs(line - ((before + after) * 0.5F));
}

[[nodiscard]] std::string_view background_label(ProbeBackground background) noexcept {
    switch (background) {
    case ProbeBackground::Clean:
        return "clean";
    case ProbeBackground::Blocks:
        return "blocks";
    case ProbeBackground::PathOverflow:
        return "path_overflow";
    case ProbeBackground::Seams:
    default:
        return "seams";
    }
}

void write_report(const fs::path& output_path, ProbeKind kind, ProbeBackground background,
                  const fs::path& png_path, const std::vector<std::byte>& pixels,
                  std::uint32_t stride) {
    std::ofstream report(output_path, std::ios::app);
    report << (kind == ProbeKind::MessageBox ? "messagebox" : "dialog")
           << "_" << background_label(background) << "\n";
    report << "  png: " << png_path.string() << "\n";
    for (const auto y : {104U, 156U, 364U}) {
        report << "  line_y_" << y << "_contrast: " << line_contrast(pixels, stride, y) << "\n";
    }
}

void render_probe_file(ProbeKind kind, ProbeBackground background, const fs::path& output_dir) {
    auto stem = std::string{kind == ProbeKind::MessageBox ? "messagebox" : "dialog"};
    stem += "_";
    stem += background_label(background);
    const auto png_path = output_dir / (std::string{stem} + ".png");
    std::uint32_t stride = 0U;
    const auto pixels = render_probe(kind, background, stride);
    save_png(png_path, probe_width, probe_height, stride, pixels);
    write_report(output_dir / "report.txt", kind, background, png_path, pixels, stride);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto output_dir =
            argc > 1 ? fs::path(argv[1]) : fs::current_path() / "modal_mask_probe";
        fs::create_directories(output_dir);
        std::ofstream(output_dir / "report.txt", std::ios::trunc);
        for (const auto background :
             {ProbeBackground::Clean, ProbeBackground::Blocks, ProbeBackground::PathOverflow,
              ProbeBackground::Seams}) {
            render_probe_file(ProbeKind::MessageBox, background, output_dir);
            render_probe_file(ProbeKind::Dialog, background, output_dir);
        }
        fmt::print("modal_mask_probe output: {}\n", output_dir.string());
        return 0;
    } catch (const std::exception& exception) {
        fmt::print(stderr, "modal_mask_probe failed: {}\n", exception.what());
        return 1;
    }
}
