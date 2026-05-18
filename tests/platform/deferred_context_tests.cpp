#include "d3d11_display_list_renderer.hpp"
#include "d3d11_render_device.hpp"
#include "d3d11_render_resource_cache.hpp"

#include <winelement/rendering.hpp>

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

namespace core = winelement::core;
namespace layout = winelement::rendering::layout;
namespace rendering = winelement::rendering;
namespace win32 = winelement::platform::win32;

struct RenderTargetBundle {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_view;
};

[[nodiscard]] RenderTargetBundle create_render_target(ID3D11Device& device, std::uint32_t width,
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

    const auto create_texture_result =
        device.CreateTexture2D(&description, nullptr, &bundle.texture);
    if (FAILED(create_texture_result)) {
        ADD_FAILURE() << "CreateTexture2D failed: 0x" << std::hex
                      << static_cast<unsigned long>(create_texture_result);
        return {};
    }

    const auto create_view_result =
        device.CreateRenderTargetView(bundle.texture.Get(), nullptr, &bundle.target_view);
    if (FAILED(create_view_result)) {
        ADD_FAILURE() << "CreateRenderTargetView failed: 0x" << std::hex
                      << static_cast<unsigned long>(create_view_result);
        return {};
    }
    return bundle;
}

[[nodiscard]] std::array<std::uint8_t, 4> read_pixel(win32::D3D11RenderDevice& device,
                                                     ID3D11Texture2D& texture,
                                                     std::uint32_t x, std::uint32_t y) {
    D3D11_TEXTURE2D_DESC source_desc{};
    texture.GetDesc(&source_desc);

    D3D11_TEXTURE2D_DESC staging_desc = source_desc;
    staging_desc.BindFlags = 0U;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.MiscFlags = 0U;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture;
    const auto create_staging_result =
        device.d3d_device().CreateTexture2D(&staging_desc, nullptr, &staging_texture);
    if (FAILED(create_staging_result)) {
        ADD_FAILURE() << "Create staging texture failed: 0x" << std::hex
                      << static_cast<unsigned long>(create_staging_result);
        return {};
    }

    auto& context = device.d3d_context();
    context.CopyResource(staging_texture.Get(), &texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const auto map_result = context.Map(staging_texture.Get(), 0U, D3D11_MAP_READ, 0U, &mapped);
    if (FAILED(map_result)) {
        ADD_FAILURE() << "Map staging texture failed: 0x" << std::hex
                      << static_cast<unsigned long>(map_result);
        return {};
    }
    const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData) +
                        static_cast<std::size_t>(y) * mapped.RowPitch +
                        static_cast<std::size_t>(x) * 4U;
    const auto pixel = std::array<std::uint8_t, 4>{bytes[0], bytes[1], bytes[2], bytes[3]};
    context.Unmap(staging_texture.Get(), 0U);
    return pixel;
}

[[nodiscard]] std::vector<std::uint8_t> read_region(win32::D3D11RenderDevice& device,
                                                    ID3D11Texture2D& texture, std::uint32_t x,
                                                    std::uint32_t y, std::uint32_t width,
                                                    std::uint32_t height) {
    D3D11_TEXTURE2D_DESC source_desc{};
    texture.GetDesc(&source_desc);

    D3D11_TEXTURE2D_DESC staging_desc = source_desc;
    staging_desc.BindFlags = 0U;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.MiscFlags = 0U;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture;
    const auto create_staging_result =
        device.d3d_device().CreateTexture2D(&staging_desc, nullptr, &staging_texture);
    if (FAILED(create_staging_result)) {
        ADD_FAILURE() << "Create staging texture failed: 0x" << std::hex
                      << static_cast<unsigned long>(create_staging_result);
        return {};
    }

    auto& context = device.d3d_context();
    context.CopyResource(staging_texture.Get(), &texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const auto map_result = context.Map(staging_texture.Get(), 0U, D3D11_MAP_READ, 0U, &mapped);
    if (FAILED(map_result)) {
        ADD_FAILURE() << "Map staging texture failed: 0x" << std::hex
                      << static_cast<unsigned long>(map_result);
        return {};
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U);
    for (std::uint32_t row = 0U; row < height; ++row) {
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData) +
                             static_cast<std::size_t>(y + row) * mapped.RowPitch +
                             static_cast<std::size_t>(x) * 4U;
        auto* target = pixels.data() + static_cast<std::size_t>(row) * width * 4U;
        std::copy_n(source, static_cast<std::size_t>(width) * 4U, target);
    }
    context.Unmap(staging_texture.Get(), 0U);
    return pixels;
}

[[nodiscard]] std::vector<std::uint8_t> render_incremental_text_region(bool include_seed_text) {
    win32::D3D11RenderDevice device;
    auto target = create_render_target(device.d3d_device(), 320U, 128U);
    win32::D3D11RenderResourceCache resource_cache;
    win32::D3D11DisplayListRenderer renderer(device.d3d_device());

    const auto text_style = rendering::TextStyle{.font_family = "Segoe UI",
                                                 .locale = "en-us",
                                                 .font_size = 20.0F,
                                                 .color = core::Color::rgba(20, 35, 58)};
    rendering::RenderCommandRecorder recorder;
    if (include_seed_text) {
        recorder.draw_text("Seed glyph atlas upload abcdefghijklmnopqrstuvwxyz",
                           layout::Rect{16.0F, 18.0F, 288.0F, 30.0F}, text_style);
    }
    recorder.draw_text("Incremental atlas upload WXYZ 7890",
                       layout::Rect{16.0F, 66.0F, 288.0F, 30.0F}, text_style);

    rendering::RenderScene scene;
    scene.update_from_commands(recorder.take_command_list(), "deferred.context.text");
    const auto frame_graph = rendering::build_render_frame_graph(scene);

    rendering::DirtyRegion dirty_region;
    dirty_region.add(layout::Rect{0.0F, 0.0F, 320.0F, 128.0F});

    renderer.render(device.d3d_context(), *target.target_view.Get(), core::Color::rgba(255, 255, 255),
                    &scene, dirty_region, 96.0F, 320U, 128U, resource_cache, &frame_graph);

    return read_region(device, *target.texture.Get(), 8U, 56U, 304U, 48U);
}

TEST(DeferredContextTests, RendererExecutesDeferredClearOnImmediateContext) {
    win32::D3D11RenderDevice device;
    auto target = create_render_target(device.d3d_device(), 64U, 64U);
    win32::D3D11RenderResourceCache resource_cache;
    win32::D3D11DisplayListRenderer renderer(device.d3d_device());

    rendering::DirtyRegion dirty_region;
    dirty_region.add(layout::Rect{0.0F, 0.0F, 64.0F, 64.0F});

    renderer.render(device.d3d_context(), *target.target_view.Get(), core::Color::rgba(12, 34, 56),
                    nullptr, dirty_region, 96.0F, 64U, 64U, resource_cache, nullptr);

    const auto pixel = read_pixel(device, *target.texture.Get(), 32U, 32U);
    EXPECT_EQ(pixel[0], 56U);
    EXPECT_EQ(pixel[1], 34U);
    EXPECT_EQ(pixel[2], 12U);
    EXPECT_EQ(pixel[3], 255U);
}

TEST(DeferredContextTests, RendererExecutesDeferredGeometryCommands) {
    win32::D3D11RenderDevice device;
    auto target = create_render_target(device.d3d_device(), 64U, 64U);
    win32::D3D11RenderResourceCache resource_cache;
    win32::D3D11DisplayListRenderer renderer(device.d3d_device());

    rendering::RenderCommandRecorder recorder;
    recorder.fill_rect(layout::Rect{0.0F, 0.0F, 64.0F, 64.0F}, core::Color::rgba(255, 0, 0));

    rendering::RenderScene scene;
    scene.update_from_commands(recorder.take_command_list(), "deferred.context.rect");
    const auto frame_graph = rendering::build_render_frame_graph(scene);

    rendering::DirtyRegion dirty_region;
    dirty_region.add(layout::Rect{0.0F, 0.0F, 64.0F, 64.0F});

    renderer.render(device.d3d_context(), *target.target_view.Get(), core::Color::rgba(0, 0, 0),
                    &scene, dirty_region, 96.0F, 64U, 64U, resource_cache, &frame_graph);

    const auto pixel = read_pixel(device, *target.texture.Get(), 32U, 32U);
    EXPECT_EQ(pixel[0], 0U);
    EXPECT_EQ(pixel[1], 0U);
    EXPECT_EQ(pixel[2], 255U);
    EXPECT_EQ(pixel[3], 255U);
}

TEST(DeferredContextTests, RendererUploadsIncrementalGlyphAtlasBeforeExecutingCommandList) {
    const auto reference = render_incremental_text_region(false);
    const auto incremental = render_incremental_text_region(true);

    ASSERT_FALSE(reference.empty());
    ASSERT_EQ(reference.size(), incremental.size());

    auto dark_pixel_count = std::size_t{};
    auto total_delta = std::uint64_t{};
    for (std::size_t index = 0; index + 3U < reference.size(); index += 4U) {
        const auto blue = incremental[index];
        const auto green = incremental[index + 1U];
        const auto red = incremental[index + 2U];
        if (red < 180U || green < 180U || blue < 180U) {
            ++dark_pixel_count;
        }
        total_delta += static_cast<std::uint64_t>(
            std::abs(static_cast<int>(reference[index]) - static_cast<int>(incremental[index])));
        total_delta += static_cast<std::uint64_t>(std::abs(
            static_cast<int>(reference[index + 1U]) - static_cast<int>(incremental[index + 1U])));
        total_delta += static_cast<std::uint64_t>(std::abs(
            static_cast<int>(reference[index + 2U]) - static_cast<int>(incremental[index + 2U])));
        total_delta += static_cast<std::uint64_t>(std::abs(
            static_cast<int>(reference[index + 3U]) - static_cast<int>(incremental[index + 3U])));
    }

    EXPECT_GT(dark_pixel_count, 100U);
    EXPECT_LT(total_delta, 4000U);
}

} // namespace