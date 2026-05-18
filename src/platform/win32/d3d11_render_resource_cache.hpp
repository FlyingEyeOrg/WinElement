#pragma once

#include <winelement/rendering/render_resource_queue.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace winelement::platform::win32 {

class D3D11RenderResourceCache final {
  public:
    struct TextureResource {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        rendering::RenderResourceFormat format =
            rendering::RenderResourceFormat::Bgra8Premultiplied;
    };

    ~D3D11RenderResourceCache();

    void upload(ID3D11Device& device, const rendering::RenderResourceUpload& upload);
    void discard(rendering::RenderResourceId id) noexcept;
    void clear() noexcept;

    [[nodiscard]] const TextureResource* texture(rendering::RenderResourceId id) const noexcept;
    [[nodiscard]] std::size_t image_texture_count() const noexcept;
    [[nodiscard]] std::size_t glyph_atlas_count() const noexcept;
    [[nodiscard]] std::size_t effect_count() const noexcept;
    [[nodiscard]] std::size_t live_resource_count() const noexcept;
    [[nodiscard]] std::uint32_t reference_count(rendering::RenderResourceId id) const noexcept;
    void assert_no_live_resources() const noexcept;

  private:
    void retain(rendering::RenderResourceId id, std::uint32_t count) noexcept;
    void release(rendering::RenderResourceId id, std::uint32_t count) noexcept;

    std::unordered_map<std::uint64_t, TextureResource> image_textures_;
    std::unordered_map<std::uint64_t, TextureResource> glyph_atlases_;
    std::unordered_map<std::uint64_t, std::uint32_t> reference_counts_;
};

} // namespace winelement::platform::win32