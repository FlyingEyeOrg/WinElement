#define WINELEMENT_RENDER_PIPELINE_DEMO_AS_LIBRARY
#include "../render_pipeline_demo/main.cpp"

#ifdef MessageBox
#undef MessageBox
#endif

#include <winelement/controls.hpp>
#include <winelement/layout.hpp>
#include <winelement/rendering.hpp>

#include <filesystem>

namespace {

namespace fs = std::filesystem;

constexpr auto probe_width = 1120U;
constexpr auto probe_height = 340U;
constexpr auto probe_dpi = 96.0F;

[[nodiscard]] winelement::rendering::RenderScene build_probe_scene() {
    winelement::layout::LayoutEngine engine;
    winelement::controls::Panel root;
    root.bind_layout_tree(engine);
    root.set_background(winelement::rendering::Color::rgba(245, 247, 250));
    root.configure_layout([](winelement::layout::LayoutElement& item) {
        item.set_size(winelement::layout::Length::points(static_cast<float>(probe_width)),
                      winelement::layout::Length::points(static_cast<float>(probe_height)));
    });

    auto& backdrop = root.append_new_child<winelement::controls::StackPanel>();
    backdrop.set_orientation(winelement::controls::Orientation::Horizontal).set_gap(12.0F);
    backdrop.configure_layout([](winelement::layout::LayoutElement& item) {
        item.set_position_type(winelement::layout::PositionType::Absolute)
            .set_position(winelement::layout::Edge::Left,
                          winelement::layout::Length::points(660.0F))
            .set_position(winelement::layout::Edge::Top,
                          winelement::layout::Length::points(154.0F))
            .set_width(winelement::layout::Length::points(320.0F));
    });
    for (const auto label :
         {std::string_view{"Open Message Box"}, std::string_view{"Open Confirm"},
          std::string_view{"Open Warning"}}) {
        auto& button = backdrop.append_new_child<winelement::controls::Button>();
        button.set_text(label).set_type(winelement::controls::ButtonType::Default);
    }

    root.calculate_layout(winelement::layout::LayoutConstraints{
        .width = static_cast<float>(probe_width), .height = static_cast<float>(probe_height)});

    winelement::controls::MessageBox::show(
        root, winelement::controls::MessageBoxOptions{
                  .title = "Warning",
                  .message = "This action will permanently remove the selected demo item.",
                  .kind = winelement::controls::MessageBoxKind::Confirm,
                  .type = winelement::controls::MessageType::Warning,
                  .show_cancel_button = true,
                  .distinguish_cancel_and_close = true,
                  .close_on_click_modal = false,
                  .close_on_press_escape = false});

    winelement::rendering::RenderScene scene;
    winelement::rendering::DirtyRegion dirty;
    root.commit_render_scene(scene, &dirty);
    return scene;
}

[[nodiscard]] std::vector<std::byte> render_probe(std::uint32_t& stride) {
    auto scene = build_probe_scene();
    auto frame_graph = winelement::rendering::build_render_frame_graph(scene);
    winelement::rendering::DirtyRegion dirty;
    dirty.add(winelement::layout::Rect{0.0F, 0.0F, static_cast<float>(probe_width),
                                       static_cast<float>(probe_height)});

    win32::D3D11RenderDevice device;
    win32::D3D11RenderResourceCache resource_cache;
    auto render_target = create_render_target(device, probe_width, probe_height);
    win32::D3D11DisplayListRenderer renderer(device.d3d_device());
    renderer.render(device.d3d_context(), *render_target.target_view.Get(),
                    winelement::rendering::Color::rgba(245, 247, 250), &scene, dirty, probe_dpi,
                    probe_width, probe_height, resource_cache, &frame_graph);

    auto pixels =
        read_back_texture(device, *render_target.texture.Get(), probe_width, probe_height, stride);
    resource_cache.clear();
    return pixels;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto output_path =
            argc > 1 ? fs::path(argv[1]) : fs::current_path() / "message_box_spacing_probe.png";
        std::uint32_t stride = 0U;
        const auto pixels = render_probe(stride);
        save_png(output_path, probe_width, probe_height, stride, pixels);
        fmt::print("message_box_spacing_probe output: {}\n", output_path.string());
        return 0;
    } catch (const std::exception& exception) {
        fmt::print(stderr, "message_box_spacing_probe failed: {}\n", exception.what());
        return 1;
    }
}
