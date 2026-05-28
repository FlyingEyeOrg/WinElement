#include "d3d11_composition_surface.hpp"

#include "d3d11_display_list_renderer.hpp"
#include "d3d11_render_resource_cache.hpp"
#include "direct_composition_bridge.hpp"
#include "hresult_error.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace winelement::platform::win32 {
namespace {

constexpr auto default_dpi = 96.0F;
constexpr auto max_surface_present_rects = 32U;

struct PixelSize {
    std::uint32_t width = 1U;
    std::uint32_t height = 1U;
};

[[nodiscard]] bool is_device_lost(HRESULT result) noexcept {
    return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET;
}

[[nodiscard]] PixelSize client_pixel_size(HWND hwnd) noexcept {
    RECT rect{};
    if (hwnd == nullptr || GetClientRect(hwnd, &rect) == FALSE) {
        return PixelSize{};
    }
    const auto width = std::max<LONG>(rect.right - rect.left, 1);
    const auto height = std::max<LONG>(rect.bottom - rect.top, 1);
    return PixelSize{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
}

[[nodiscard]] PixelSize make_pixel_size(std::uint32_t width, std::uint32_t height) noexcept {
    return PixelSize{std::max(width, 1U), std::max(height, 1U)};
}

[[nodiscard]] bool same_pixel_size(PixelSize left, PixelSize right) noexcept {
    return left.width == right.width && left.height == right.height;
}

[[nodiscard]] rendering::layout::Rect target_dip_rect(PixelSize pixel_size, float dpi) noexcept {
    const auto scale = default_dpi / std::max(dpi, 1.0F);
    return rendering::layout::Rect{0.0F, 0.0F, static_cast<float>(pixel_size.width) * scale,
                                   static_cast<float>(pixel_size.height) * scale};
}

[[nodiscard]] RECT clipped_present_rect(rendering::layout::Rect rect, float dpi,
                                        PixelSize target_size) noexcept {
    const auto scale = dpi / default_dpi;
    auto left = static_cast<LONG>(std::floor(rect.x * scale));
    auto top = static_cast<LONG>(std::floor(rect.y * scale));
    auto right = static_cast<LONG>(std::ceil((rect.x + rect.width) * scale));
    auto bottom = static_cast<LONG>(std::ceil((rect.y + rect.height) * scale));

    left = std::clamp<LONG>(left, 0, static_cast<LONG>(target_size.width));
    top = std::clamp<LONG>(top, 0, static_cast<LONG>(target_size.height));
    right = std::clamp<LONG>(right, left, static_cast<LONG>(target_size.width));
    bottom = std::clamp<LONG>(bottom, top, static_cast<LONG>(target_size.height));
    return RECT{left, top, right, bottom};
}

[[nodiscard]] bool rect_contains_rect(rendering::layout::Rect outer,
                                      rendering::layout::Rect inner) noexcept {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

struct SurfaceDirtyRegion {
    std::optional<rendering::DirtyRegion> normalized_region;
    const rendering::DirtyRegion* original_region = nullptr;
    std::vector<RECT> present_rects;

    [[nodiscard]] const rendering::DirtyRegion* region() const noexcept {
        return normalized_region.has_value() ? &*normalized_region : original_region;
    }

    [[nodiscard]] bool empty() const noexcept {
        const auto* effective_region = region();
        return effective_region == nullptr || effective_region->empty() || present_rects.empty();
    }
};

void append_present_rect(std::vector<RECT>& present_rects, rendering::layout::Rect rect, float dpi,
                         PixelSize target_size) {
    auto present_rect = clipped_present_rect(rect, dpi, target_size);
    if (present_rect.right > present_rect.left && present_rect.bottom > present_rect.top) {
        present_rects.push_back(present_rect);
    }
}

[[nodiscard]] SurfaceDirtyRegion
prepare_surface_dirty_region(const rendering::DirtyRegion& dirty_region,
                             rendering::layout::Rect target_rect, float dpi, PixelSize target_size,
                             bool force_full_target) {
    auto result = SurfaceDirtyRegion{};

    if (force_full_target) {
        result.normalized_region.emplace();
        result.normalized_region->add(target_rect);
        append_present_rect(result.present_rects, target_rect, dpi, target_size);
        return result;
    }

    if (dirty_region.rects().size() > max_surface_present_rects) {
        const auto dirty_bounds =
            rendering::layout::intersect_rects(dirty_region.bounds(), target_rect);
        if (rendering::layout::is_visible_rect(dirty_bounds)) {
            result.normalized_region.emplace();
            result.normalized_region->add(dirty_bounds);
            append_present_rect(result.present_rects, dirty_bounds, dpi, target_size);
        }
        return result;
    }

    auto needs_normalization = false;

    if (!needs_normalization) {
        result.present_rects.reserve(dirty_region.rects().size());
        for (const auto rect : dirty_region.rects()) {
            if (!rect_contains_rect(target_rect, rect)) {
                needs_normalization = true;
                result.present_rects.clear();
                break;
            }
            append_present_rect(result.present_rects, rect, dpi, target_size);
        }
    }

    if (!needs_normalization) {
        result.original_region = &dirty_region;
        return result;
    }

    result.normalized_region.emplace(dirty_region);
    if (force_full_target) {
        result.normalized_region->add(target_rect);
    }
    result.normalized_region->clip(target_rect);
    result.normalized_region->optimize();
    result.present_rects.reserve(result.normalized_region->rects().size());
    for (const auto rect : result.normalized_region->rects()) {
        append_present_rect(result.present_rects, rect, dpi, target_size);
    }
    return result;
}

} // namespace

class D3D11CompositionSurface::Impl final {
  public:
    Impl(HWND hwnd, std::shared_ptr<D3D11RenderDevice> device)
        : hwnd_(hwnd), device_(std::move(device)) {
        if (hwnd_ == nullptr) {
            throw std::invalid_argument("render surface requires a valid HWND");
        }
        if (device_ == nullptr) {
            throw std::invalid_argument("render surface requires a DirectX render device");
        }
    }

    ~Impl() {
        discard();
    }

    void set_dpi(float dpi) noexcept {
        dpi_ = std::max(dpi, 1.0F);
        invalidate_surface_size();
    }

    void invalidate_surface_size() noexcept {
        if (hwnd_ == nullptr) {
            return;
        }
        validated_client_size_.reset();
        release_render_target();
    }

    void discard() noexcept {
        if (composition_bridge_ != nullptr) {
            composition_bridge_->clear_content();
        }
        release_render_target();
        resource_cache_.clear();
        swap_chain_.Reset();
        display_list_renderer_.reset();
        validated_client_size_.reset();
        if (device_ != nullptr) {
            device_->trim_idle_resources();
        }
    }

    void trim_idle_resources() noexcept {
        try {
            if (display_list_renderer_ != nullptr) {
                display_list_renderer_->trim_idle_resources();
            }
            if (device_ != nullptr) {
                device_->trim_idle_resources();
            }
        } catch (...) {
        }
    }

    void upload_resource(rendering::RenderResourceUpload upload) noexcept {
        try {
            resource_cache_.upload(device_->d3d_device(), upload);
        } catch (...) {
            handle_device_lost();
        }
    }

    [[nodiscard]] RenderResult render(rendering::Color clear_color,
                                      const rendering::DirtyRegion& dirty_region,
                                      std::uint32_t target_pixel_width,
                                      std::uint32_t target_pixel_height,
                                      const rendering::RenderScene* scene,
                                      const rendering::CompositorPromotionPlan& promotion_plan,
                                      const rendering::RenderFrameGraph* frame_graph) {
        const auto target_size = make_pixel_size(target_pixel_width, target_pixel_height);
        if (!validated_client_size_.has_value() ||
            !same_pixel_size(*validated_client_size_, target_size)) {
            const auto client_size_before_render = client_pixel_size(hwnd_);
            validated_client_size_ = client_size_before_render;
            if (!same_pixel_size(target_size, client_size_before_render)) {
                return RenderResult::StaleTargetSize;
            }
        }

        const auto target_rect = target_dip_rect(target_size, dpi_);
        const auto has_current_render_target = has_render_target_for_size(target_size);
        auto effective_dirty = prepare_surface_dirty_region(
            dirty_region, target_rect, dpi_, target_size, !has_current_render_target);

        if (effective_dirty.empty()) {
            if (!apply_composition_plan(promotion_plan)) {
                return RenderResult::DeviceLost;
            }
            return RenderResult::Skipped;
        }

        if (!ensure_render_target(target_size)) {
            return RenderResult::DeviceLost;
        }

        const auto* effective_dirty_region = effective_dirty.region();
        if (effective_dirty_region == nullptr) {
            return RenderResult::Skipped;
        }

        display_list_renderer_->render(device_->d3d_context(), *render_target_view_.Get(),
                                       clear_color, scene, *effective_dirty_region, dpi_,
                                       target_size.width, target_size.height, resource_cache_,
                                       frame_graph);

        const auto client_size_after_render = client_pixel_size(hwnd_);
        validated_client_size_ = client_size_after_render;
        if (!same_pixel_size(target_size, client_size_after_render)) {
            return RenderResult::StaleTargetSize;
        }

        if (effective_dirty.present_rects.empty()) {
            if (!apply_composition_plan(promotion_plan)) {
                return RenderResult::DeviceLost;
            }
            return RenderResult::Skipped;
        }

        DXGI_PRESENT_PARAMETERS present_parameters{};
        present_parameters.DirtyRectsCount =
            static_cast<UINT>(effective_dirty.present_rects.size());
        present_parameters.pDirtyRects = effective_dirty.present_rects.data();

        const auto present_result = swap_chain_->Present1(1, 0, &present_parameters);
        if (is_device_lost(present_result)) {
            handle_device_lost();
            return RenderResult::DeviceLost;
        }
        if (FAILED(present_result)) {
            throw win32_detail::make_hresult_error("failed to present DXGI swap chain",
                                                   present_result);
        }

        if (!apply_composition_plan(promotion_plan)) {
            return RenderResult::DeviceLost;
        }

        return RenderResult::Rendered;
    }

    [[nodiscard]] bool consume_device_recreated() noexcept {
        const auto recreated = device_recreated_;
        device_recreated_ = false;
        return recreated;
    }

  private:
    [[nodiscard]] bool recover_device_lost(HRESULT result) noexcept {
        if (!is_device_lost(result)) {
            return false;
        }
        handle_device_lost();
        return true;
    }

    void handle_device_lost() noexcept {
        discard();
        try {
            composition_bridge_.reset();
            device_->recreate();
            device_recreated_ = true;
        } catch (...) {
            discard();
        }
    }

    [[nodiscard]] bool has_render_target_for_size(PixelSize pixel_size) const noexcept {
        return render_target_view_ != nullptr && same_pixel_size(target_pixel_size_, pixel_size);
    }

    [[nodiscard]] bool ensure_render_target(PixelSize pixel_size) {
        if (has_render_target_for_size(pixel_size) && display_list_renderer_ != nullptr) {
            return true;
        }
        return create_or_resize_window_size_resources(pixel_size);
    }

    [[nodiscard]] bool
    apply_composition_plan(const rendering::CompositorPromotionPlan& promotion_plan) noexcept {
        if (composition_bridge_ == nullptr) {
            return true;
        }

        try {
            composition_bridge_->apply_promotion_plan(promotion_plan);
            composition_bridge_->commit();
            return true;
        } catch (...) {
            handle_device_lost();
            return false;
        }
    }

    [[nodiscard]] bool create_or_resize_window_size_resources(PixelSize pixel_size) {
        release_render_target();

        if (swap_chain_ == nullptr) {
            if (!create_swap_chain(pixel_size)) {
                return false;
            }
        } else {
            const auto result = swap_chain_->ResizeBuffers(0, pixel_size.width, pixel_size.height,
                                                           DXGI_FORMAT_UNKNOWN, 0);
            if (recover_device_lost(result)) {
                return false;
            }
            if (FAILED(result)) {
                throw win32_detail::make_hresult_error("failed to resize DXGI swap chain", result);
            }
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
        auto result = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        if (recover_device_lost(result)) {
            return false;
        }
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to query D3D11 back buffer", result);
        }

        result = device_->d3d_device().CreateRenderTargetView(back_buffer.Get(), nullptr,
                                                              &render_target_view_);
        if (recover_device_lost(result)) {
            return false;
        }
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to create D3D11 render target view",
                                                   result);
        }

        if (display_list_renderer_ == nullptr) {
            display_list_renderer_ =
                std::make_unique<D3D11DisplayListRenderer>(device_->d3d_device());
            display_list_renderer_->trim_parallel_recording_resources();
        }
        target_pixel_size_ = pixel_size;
        return true;
    }

    void release_render_target() noexcept {
        render_target_view_.Reset();
        target_pixel_size_ = PixelSize{0U, 0U};
    }

    [[nodiscard]] bool create_swap_chain(PixelSize pixel_size) {
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
        auto result = device_->d3d_device().QueryInterface(IID_PPV_ARGS(&dxgi_device));
        if (recover_device_lost(result)) {
            return false;
        }
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to query DXGI device", result);
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        result = dxgi_device->GetAdapter(&adapter);
        if (recover_device_lost(result)) {
            return false;
        }
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to query DXGI adapter", result);
        }

        Microsoft::WRL::ComPtr<IDXGIFactory2> dxgi_factory;
        result = adapter->GetParent(IID_PPV_ARGS(&dxgi_factory));
        if (recover_device_lost(result)) {
            return false;
        }
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to query DXGI factory", result);
        }

        DXGI_SWAP_CHAIN_DESC1 swap_chain_description{};
        swap_chain_description.Width = pixel_size.width;
        swap_chain_description.Height = pixel_size.height;
        swap_chain_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swap_chain_description.Stereo = FALSE;
        swap_chain_description.SampleDesc.Count = 1;
        swap_chain_description.SampleDesc.Quality = 0;
        swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_description.BufferCount = 2;
        swap_chain_description.Scaling = DXGI_SCALING_STRETCH;
        swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swap_chain_description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> temp_swap_chain;
        result = dxgi_factory->CreateSwapChainForComposition(
            &device_->d3d_device(), &swap_chain_description, nullptr, &temp_swap_chain);
        if (recover_device_lost(result)) {
            return false;
        }
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to create DXGI swap chain", result);
        }

        std::unique_ptr<DirectCompositionBridge> temp_composition_bridge;
        DirectCompositionBridge* active_bridge = composition_bridge_.get();
        if (active_bridge == nullptr) {
            temp_composition_bridge =
                std::make_unique<DirectCompositionBridge>(hwnd_, dxgi_device.Get());
            active_bridge = temp_composition_bridge.get();
        }

        active_bridge->bind_swap_chain(temp_swap_chain.Get());
        dxgi_factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);

        swap_chain_ = std::move(temp_swap_chain);
        if (temp_composition_bridge != nullptr) {
            composition_bridge_ = std::move(temp_composition_bridge);
        }
        return true;
    }

    HWND hwnd_ = nullptr;
    float dpi_ = default_dpi;
    std::shared_ptr<D3D11RenderDevice> device_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view_;
    PixelSize target_pixel_size_{0U, 0U};
    std::unique_ptr<D3D11DisplayListRenderer> display_list_renderer_;
    std::unique_ptr<DirectCompositionBridge> composition_bridge_;
    D3D11RenderResourceCache resource_cache_;
    std::optional<PixelSize> validated_client_size_;
    bool device_recreated_ = false;
};

D3D11CompositionSurface::D3D11CompositionSurface(HWND hwnd)
    : D3D11CompositionSurface(hwnd, std::make_shared<D3D11RenderDevice>()) {}

D3D11CompositionSurface::D3D11CompositionSurface(HWND hwnd, D3D11RenderDeviceDriver driver)
    : D3D11CompositionSurface(hwnd, std::make_shared<D3D11RenderDevice>(driver)) {}

D3D11CompositionSurface::D3D11CompositionSurface(HWND hwnd,
                                                 std::shared_ptr<D3D11RenderDevice> device)
    : impl_(std::make_unique<Impl>(hwnd, std::move(device))) {}

D3D11CompositionSurface::~D3D11CompositionSurface() = default;

D3D11CompositionSurface::D3D11CompositionSurface(D3D11CompositionSurface&&) noexcept = default;

D3D11CompositionSurface&
D3D11CompositionSurface::operator=(D3D11CompositionSurface&&) noexcept = default;

void D3D11CompositionSurface::set_dpi(float dpi) noexcept {
    impl_->set_dpi(dpi);
}

void D3D11CompositionSurface::invalidate_surface_size() noexcept {
    impl_->invalidate_surface_size();
}

void D3D11CompositionSurface::discard() noexcept {
    impl_->discard();
}

void D3D11CompositionSurface::trim_idle_resources() noexcept {
    impl_->trim_idle_resources();
}

void D3D11CompositionSurface::upload_resource(rendering::RenderResourceUpload upload) noexcept {
    impl_->upload_resource(std::move(upload));
}

D3D11CompositionSurface::RenderResult D3D11CompositionSurface::render(
    rendering::Color clear_color, const rendering::DirtyRegion& dirty_region,
    std::uint32_t target_pixel_width, std::uint32_t target_pixel_height,
    const rendering::RenderScene* scene, const rendering::CompositorPromotionPlan& promotion_plan,
    const rendering::RenderFrameGraph* frame_graph) {
    return impl_->render(clear_color, dirty_region, target_pixel_width, target_pixel_height, scene,
                         promotion_plan, frame_graph);
}

bool D3D11CompositionSurface::consume_device_recreated() noexcept {
    return impl_->consume_device_recreated();
}

} // namespace winelement::platform::win32
