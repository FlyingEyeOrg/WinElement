#define WINELEMENT_CONTROLS_SHOWCASE_AS_LIBRARY
#include "../controls_showcase/main.cpp"

#ifdef MessageBox
#undef MessageBox
#endif

#include "d3d11_display_list_renderer.hpp"
#include "d3d11_render_device.hpp"
#include "d3d11_render_resource_cache.hpp"

#include <fmt/format.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#ifdef MessageBox
#undef MessageBox
#endif

#include <combaseapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace win32 = winelement::platform::win32;

constexpr auto probe_width = 677U;
constexpr auto probe_height = 398U;
constexpr auto probe_dpi = 96.0F;
constexpr auto feedback_scroll_y = 2600.0F;

enum class ProbeKind { Dialog, MessageBox };

class ProbeComApartment final {
  public:
    ProbeComApartment() {
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

    ~ProbeComApartment() {
        if (initialized_) {
            CoUninitialize();
        }
    }

    ProbeComApartment(const ProbeComApartment&) = delete;
    ProbeComApartment& operator=(const ProbeComApartment&) = delete;

  private:
    HRESULT result_ = S_OK;
    bool initialized_ = false;
};

struct ProbeRenderTarget {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_view;
};

[[nodiscard]] std::runtime_error probe_hresult_error(std::string_view message, HRESULT result) {
    std::ostringstream stream;
    stream << message << " HRESULT=0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result);
    return std::runtime_error(stream.str());
}

[[nodiscard]] ProbeRenderTarget create_probe_render_target(win32::D3D11RenderDevice& device,
                                                           std::uint32_t width,
                                                           std::uint32_t height) {
    ProbeRenderTarget bundle;

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
        throw probe_hresult_error("failed to create D3D11 render target texture", result);
    }

    result = device.d3d_device().CreateRenderTargetView(bundle.texture.Get(), nullptr,
                                                        &bundle.target_view);
    if (FAILED(result)) {
        throw probe_hresult_error("failed to create D3D11 render target view", result);
    }

    return bundle;
}

[[nodiscard]] std::vector<std::byte>
read_back_probe_texture(win32::D3D11RenderDevice& device, ID3D11Texture2D& texture,
                        std::uint32_t width, std::uint32_t height, std::uint32_t& stride) {
    D3D11_TEXTURE2D_DESC description{};
    texture.GetDesc(&description);

    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0U;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0U;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture;
    auto result = device.d3d_device().CreateTexture2D(&description, nullptr, &staging_texture);
    if (FAILED(result)) {
        throw probe_hresult_error("failed to create staging texture", result);
    }

    device.d3d_context().CopyResource(staging_texture.Get(), &texture);
    device.d3d_context().Flush();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = device.d3d_context().Map(staging_texture.Get(), 0U, D3D11_MAP_READ, 0U, &mapped);
    if (FAILED(result)) {
        throw probe_hresult_error("failed to map staging texture", result);
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

void save_probe_png(const fs::path& output_path, std::uint32_t width, std::uint32_t height,
                    std::uint32_t stride, const std::vector<std::byte>& pixels) {
    ProbeComApartment com;

    if (!output_path.parent_path().empty()) {
        fs::create_directories(output_path.parent_path());
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    auto result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        throw probe_hresult_error("failed to create WIC imaging factory", result);
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    result = factory->CreateStream(&stream);
    if (FAILED(result)) {
        throw probe_hresult_error("failed to create WIC stream", result);
    }

    result = stream->InitializeFromFilename(output_path.c_str(), GENERIC_WRITE);
    if (FAILED(result)) {
        throw probe_hresult_error("failed to initialize WIC stream", result);
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(result)) {
        throw probe_hresult_error("failed to create PNG encoder", result);
    }

    result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(result)) {
        throw probe_hresult_error("failed to initialize PNG encoder", result);
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    Microsoft::WRL::ComPtr<IPropertyBag2> properties;
    result = encoder->CreateNewFrame(&frame, &properties);
    if (FAILED(result)) {
        throw probe_hresult_error("failed to create PNG frame", result);
    }

    result = frame->Initialize(properties.Get());
    if (FAILED(result)) {
        throw probe_hresult_error("failed to initialize PNG frame", result);
    }

    result = frame->SetSize(width, height);
    if (FAILED(result)) {
        throw probe_hresult_error("failed to set PNG frame size", result);
    }

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppPBGRA;
    result = frame->SetPixelFormat(&pixel_format);
    if (FAILED(result)) {
        throw probe_hresult_error("failed to set PNG pixel format", result);
    }

    result = frame->WritePixels(height, stride, static_cast<UINT>(pixels.size()),
                                reinterpret_cast<BYTE*>(const_cast<std::byte*>(pixels.data())));
    if (FAILED(result)) {
        throw probe_hresult_error("failed to write PNG pixels", result);
    }

    result = frame->Commit();
    if (FAILED(result)) {
        throw probe_hresult_error("failed to commit PNG frame", result);
    }

    result = encoder->Commit();
    if (FAILED(result)) {
        throw probe_hresult_error("failed to commit PNG encoder", result);
    }
}

[[nodiscard]] winelement::rendering::RenderScene build_probe_scene(ProbeKind kind) {
    winelement::layout::LayoutEngineOptions layout_options;
    layout_options.point_scale_factor = 0.0F;
    winelement::layout::LayoutEngine engine(layout_options);

    auto root = build_showcase_tree();
    root->bind_layout_tree(engine);
    root->calculate_layout(winelement::layout::LayoutConstraints{
        .width = static_cast<float>(probe_width), .height = static_cast<float>(probe_height)});
    root->set_scroll_offset(winelement::layout::Point{0.0F, feedback_scroll_y});
    root->calculate_layout(winelement::layout::LayoutConstraints{
        .width = static_cast<float>(probe_width), .height = static_cast<float>(probe_height)});

    controls::Message::show(
        *root, controls::MessageOptions{.text = "Manual close message",
                                        .type = controls::MessageType::Info,
                                        .show_close = true,
                                        .duration_ms = 0});

    if (kind == ProbeKind::Dialog) {
        controls::Dialog::show(
            *root, controls::DialogOptions{
                       .title = "Dialog",
                       .body = "Modal surface with header, body, footer, close and confirm actions.",
                       .show_cancel_button = true,
                       .modal = true,
                       .close_on_click_modal = false,
                       .close_on_press_escape = false,
                       .draggable = true});
    } else {
        controls::MessageBox::show(
            *root, controls::MessageBoxOptions{.title = "Alert",
                                               .message = "Simple notification with one primary "
                                                          "action.",
                                               .kind = controls::MessageBoxKind::Alert,
                                               .type = controls::MessageType::Info,
                                               .show_cancel_button = false,
                                               .close_on_click_modal = false,
                                               .close_on_press_escape = false});
    }

    rendering::RenderScene scene;
    rendering::DirtyRegion dirty;
    root->commit_render_scene(scene, &dirty);
    return scene;
}

[[nodiscard]] std::vector<std::byte> render_probe(ProbeKind kind, std::uint32_t& stride) {
    const auto scene = build_probe_scene(kind);
    auto frame_graph = rendering::build_render_frame_graph(scene);
    rendering::DirtyRegion dirty;
    dirty.add(winelement::layout::Rect{0.0F, 0.0F, static_cast<float>(probe_width),
                                       static_cast<float>(probe_height)});

    win32::D3D11RenderDevice device;
    win32::D3D11RenderResourceCache resource_cache;
    auto render_target = create_probe_render_target(device, probe_width, probe_height);
    win32::D3D11DisplayListRenderer renderer(device.d3d_device());
    renderer.render(device.d3d_context(), *render_target.target_view.Get(),
                    rendering::Color::rgba(245, 247, 250), &scene, dirty, probe_dpi, probe_width,
                    probe_height, resource_cache, &frame_graph);

    auto pixels = read_back_probe_texture(device, *render_target.texture.Get(), probe_width,
                                          probe_height, stride);
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

[[nodiscard]] float row_contrast(const std::vector<std::byte>& pixels, std::uint32_t stride,
                                 std::uint32_t y) {
    constexpr auto left = 8U;
    constexpr auto right = probe_width - 8U;
    const auto line = row_average_luma(pixels, stride, y, left, right);
    const auto before = row_average_luma(pixels, stride, y - 3U, left, right);
    const auto after = row_average_luma(pixels, stride, y + 3U, left, right);
    return std::abs(line - ((before + after) * 0.5F));
}

[[nodiscard]] float column_average_luma(const std::vector<std::byte>& pixels,
                                        std::uint32_t stride, std::uint32_t x,
                                        std::uint32_t top, std::uint32_t bottom) {
    auto total = 0.0F;
    auto count = 0U;
    for (auto y = top; y < bottom; ++y) {
        total += luma_at(pixels, stride, x, y);
        ++count;
    }
    return count == 0U ? 0.0F : total / static_cast<float>(count);
}

[[nodiscard]] float column_contrast(const std::vector<std::byte>& pixels, std::uint32_t stride,
                                    std::uint32_t x) {
    constexpr auto top = 8U;
    constexpr auto bottom = probe_height - 8U;
    const auto line = column_average_luma(pixels, stride, x, top, bottom);
    const auto before = column_average_luma(pixels, stride, x - 3U, top, bottom);
    const auto after = column_average_luma(pixels, stride, x + 3U, top, bottom);
    return std::abs(line - ((before + after) * 0.5F));
}

[[nodiscard]] std::vector<std::uint32_t> high_contrast_rows(const std::vector<std::byte>& pixels,
                                                           std::uint32_t stride) {
    std::vector<std::uint32_t> rows;
    for (auto y = 3U; y + 3U < probe_height; ++y) {
        if (row_contrast(pixels, stride, y) > 12.0F) {
            if (rows.empty() || y > rows.back() + 2U) {
                rows.push_back(y);
            }
        }
    }
    return rows;
}

[[nodiscard]] std::vector<std::uint32_t>
high_contrast_columns(const std::vector<std::byte>& pixels, std::uint32_t stride) {
    std::vector<std::uint32_t> columns;
    for (auto x = 3U; x + 3U < probe_width; ++x) {
        if (column_contrast(pixels, stride, x) > 12.0F) {
            if (columns.empty() || x > columns.back() + 2U) {
                columns.push_back(x);
            }
        }
    }
    return columns;
}

void write_probe_file(ProbeKind kind, const fs::path& output_dir) {
    const auto stem = kind == ProbeKind::Dialog ? "dialog" : "messagebox";
    std::uint32_t stride = 0U;
    const auto pixels = render_probe(kind, stride);
    const auto png_path = output_dir / (std::string{stem} + ".png");
    save_probe_png(png_path, probe_width, probe_height, stride, pixels);

    std::ofstream report(output_dir / "report.txt", std::ios::app);
    report << stem << "\n";
    report << "  png: " << png_path.string() << "\n";
    report << "  high_contrast_rows:";
    for (const auto y : high_contrast_rows(pixels, stride)) {
        report << " " << y << "(" << row_contrast(pixels, stride, y) << ")";
    }
    report << "\n";
    report << "  high_contrast_columns:";
    for (const auto x : high_contrast_columns(pixels, stride)) {
        report << " " << x << "(" << column_contrast(pixels, stride, x) << ")";
    }
    report << "\n";
    for (const auto y : {36U, 119U, 270U, 344U}) {
        report << "  row_" << y << "_contrast: " << row_contrast(pixels, stride, y) << "\n";
    }
    for (const auto x : {169U, 338U, 508U}) {
        report << "  column_" << x << "_contrast: " << column_contrast(pixels, stride, x)
               << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto output_dir =
            argc > 1 ? fs::path(argv[1]) : fs::current_path() / "controls_showcase_mask_probe";
        fs::create_directories(output_dir);
        std::ofstream(output_dir / "report.txt", std::ios::trunc);
        write_probe_file(ProbeKind::Dialog, output_dir);
        write_probe_file(ProbeKind::MessageBox, output_dir);
        fmt::print("controls_showcase_mask_probe output: {}\n", output_dir.string());
        return 0;
    } catch (const std::exception& exception) {
        fmt::print(stderr, "controls_showcase_mask_probe failed: {}\n", exception.what());
        return 1;
    }
}
