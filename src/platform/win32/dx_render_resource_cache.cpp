#include "dx_render_resource_cache.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>

namespace winelement::platform::win32 {
namespace {

[[nodiscard]] bool is_valid_upload_payload(const rendering::RenderResourceUpload& upload,
                                           std::uint32_t bytes_per_pixel) noexcept {
    if (upload.id.value == 0U || upload.width == 0U || upload.height == 0U || upload.stride == 0U ||
        upload.payload.empty()) {
        return false;
    }

    const auto minimum_stride = static_cast<std::uint64_t>(upload.width) * bytes_per_pixel;
    const auto total_bytes = static_cast<std::uint64_t>(upload.stride) * upload.height;
    return upload.stride >= minimum_stride && total_bytes <= upload.payload.size();
}

[[nodiscard]] DXGI_FORMAT dxgi_format(rendering::RenderResourceFormat format) noexcept {
    return format == rendering::RenderResourceFormat::Alpha8 ? DXGI_FORMAT_R8_UNORM
                                                             : DXGI_FORMAT_B8G8R8A8_UNORM;
}

[[nodiscard]] std::uint32_t bytes_per_pixel(rendering::RenderResourceFormat format) noexcept {
    return format == rendering::RenderResourceFormat::Alpha8 ? 1U : 4U;
}

[[nodiscard]] DxRenderResourceCache::TextureResource
create_texture(ID3D11Device& device, const rendering::RenderResourceUpload& upload) {
    const auto pixel_width = bytes_per_pixel(upload.format);
    if (!is_valid_upload_payload(upload, pixel_width)) {
        return {};
    }

    D3D11_TEXTURE2D_DESC texture_description{};
    texture_description.Width = upload.width;
    texture_description.Height = upload.height;
    texture_description.MipLevels = 1;
    texture_description.ArraySize = 1;
    texture_description.Format = dxgi_format(upload.format);
    texture_description.SampleDesc.Count = 1;
    texture_description.Usage = D3D11_USAGE_IMMUTABLE;
    texture_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial_data{};
    initial_data.pSysMem = upload.payload.data();
    initial_data.SysMemPitch = upload.stride;

    DxRenderResourceCache::TextureResource resource;
    resource.width = upload.width;
    resource.height = upload.height;
    resource.format = upload.format;
    if (FAILED(device.CreateTexture2D(&texture_description, &initial_data, &resource.texture))) {
        return {};
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
    view_description.Format = texture_description.Format;
    view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view_description.Texture2D.MipLevels = 1;
    if (FAILED(device.CreateShaderResourceView(resource.texture.Get(), &view_description,
                                               &resource.view))) {
        return {};
    }

    return resource;
}

} // namespace

DxRenderResourceCache::~DxRenderResourceCache() {
    assert_no_live_resources();
}

void DxRenderResourceCache::upload(ID3D11Device& device,
                                   const rendering::RenderResourceUpload& upload) {
    if (upload.id.value == 0U) {
        return;
    }

    if (upload.action == rendering::RenderResourceAction::Discard) {
        discard(upload.id);
        return;
    }

    if (upload.action == rendering::RenderResourceAction::Retain) {
        retain(upload.id, upload.reference_count);
        return;
    }

    if (upload.action == rendering::RenderResourceAction::Release) {
        release(upload.id, upload.reference_count);
        return;
    }

    switch (upload.kind) {
    case rendering::RenderResourceKind::Image:
        if (auto texture = create_texture(device, upload); texture.view != nullptr) {
            image_textures_[upload.id.value] = std::move(texture);
            reference_counts_[upload.id.value] = std::max(1U, upload.reference_count);
        }
        break;
    case rendering::RenderResourceKind::GlyphAtlas:
        if (auto texture = create_texture(device, upload); texture.view != nullptr) {
            glyph_atlases_[upload.id.value] = std::move(texture);
            reference_counts_[upload.id.value] = std::max(1U, upload.reference_count);
        }
        break;
    case rendering::RenderResourceKind::Effect:
        reference_counts_[upload.id.value] = std::max(1U, upload.reference_count);
        break;
    case rendering::RenderResourceKind::User:
    default:
        break;
    }
}

void DxRenderResourceCache::discard(rendering::RenderResourceId id) noexcept {
    image_textures_.erase(id.value);
    glyph_atlases_.erase(id.value);
    reference_counts_.erase(id.value);
}

void DxRenderResourceCache::clear() noexcept {
    image_textures_.clear();
    glyph_atlases_.clear();
    reference_counts_.clear();
}

const DxRenderResourceCache::TextureResource*
DxRenderResourceCache::texture(rendering::RenderResourceId id) const noexcept {
    if (const auto iterator = image_textures_.find(id.value); iterator != image_textures_.end()) {
        return &iterator->second;
    }
    if (const auto iterator = glyph_atlases_.find(id.value); iterator != glyph_atlases_.end()) {
        return &iterator->second;
    }
    return nullptr;
}

std::size_t DxRenderResourceCache::image_texture_count() const noexcept {
    return image_textures_.size();
}

std::size_t DxRenderResourceCache::glyph_atlas_count() const noexcept {
    return glyph_atlases_.size();
}

std::size_t DxRenderResourceCache::effect_count() const noexcept {
    return 0U;
}

std::size_t DxRenderResourceCache::live_resource_count() const noexcept {
    return image_texture_count() + glyph_atlas_count();
}

std::uint32_t
DxRenderResourceCache::reference_count(rendering::RenderResourceId id) const noexcept {
    const auto iterator = reference_counts_.find(id.value);
    return iterator == reference_counts_.end() ? 0U : iterator->second;
}

void DxRenderResourceCache::retain(rendering::RenderResourceId id, std::uint32_t count) noexcept {
    if (id.value == 0U || count == 0U || reference_count(id) == 0U) {
        return;
    }
    reference_counts_[id.value] += count;
}

void DxRenderResourceCache::release(rendering::RenderResourceId id, std::uint32_t count) noexcept {
    if (id.value == 0U || count == 0U) {
        return;
    }
    const auto iterator = reference_counts_.find(id.value);
    if (iterator == reference_counts_.end()) {
        return;
    }
    if (iterator->second <= count) {
        discard(id);
        return;
    }
    iterator->second -= count;
}

void DxRenderResourceCache::assert_no_live_resources() const noexcept {
#ifndef NDEBUG
    assert(live_resource_count() == 0U &&
           "DxRenderResourceCache still owns GPU resources; call clear() before teardown");
#endif
}

} // namespace winelement::platform::win32
