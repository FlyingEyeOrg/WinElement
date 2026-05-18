#include "d3d11_render_device.hpp"

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

#include <stdexcept>
#include <string>
#include <string_view>

namespace winelement::platform::win32 {
namespace {

[[nodiscard]] std::runtime_error make_hresult_error(std::string_view message, HRESULT result) {
    auto text = std::string(message);
    text += " HRESULT=0x";

    constexpr auto digits = "0123456789ABCDEF";
    for (auto shift = 28; shift >= 0; shift -= 4) {
        text += digits[(static_cast<unsigned long>(result) >> shift) & 0x0F];
    }

    return std::runtime_error(text);
}

} // namespace

class D3D11RenderDevice::Impl final {
  public:
    Impl() {
        create_devices();
    }

    [[nodiscard]] ID3D11Device& d3d_device() const noexcept {
        return *d3d_device_.Get();
    }

    [[nodiscard]] ID3D11DeviceContext& d3d_context() const noexcept {
        return *d3d_context_.Get();
    }

    void recreate() {
        Impl replacement;
        d3d_device_ = std::move(replacement.d3d_device_);
        d3d_context_ = std::move(replacement.d3d_context_);
        feature_level_ = replacement.feature_level_;
    }

  private:
    void create_devices() {
        const auto create_flags = static_cast<UINT>(D3D11_CREATE_DEVICE_BGRA_SUPPORT);
        D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                              D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                              D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
                                              D3D_FEATURE_LEVEL_9_1};

        auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, create_flags,
                                        feature_levels, ARRAYSIZE(feature_levels),
                                        D3D11_SDK_VERSION, d3d_device_.GetAddressOf(),
                                        &feature_level_, d3d_context_.GetAddressOf());
        if (FAILED(result)) {
            d3d_device_.Reset();
            d3d_context_.Reset();
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, create_flags,
                                       feature_levels, ARRAYSIZE(feature_levels), D3D11_SDK_VERSION,
                                       d3d_device_.GetAddressOf(), &feature_level_,
                                       d3d_context_.GetAddressOf());
        }
        if (FAILED(result)) {
            throw make_hresult_error("failed to create Direct3D device", result);
        }

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
        result = d3d_device_.As(&dxgi_device);
        if (FAILED(result)) {
            throw make_hresult_error("failed to query DXGI device", result);
        }

        Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi_device1;
        if (SUCCEEDED(dxgi_device.As(&dxgi_device1))) {
            dxgi_device1->SetMaximumFrameLatency(1);
        }
    }

    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;
    D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_9_1;
};

D3D11RenderDevice::D3D11RenderDevice() : impl_(std::make_unique<Impl>()) {}

D3D11RenderDevice::~D3D11RenderDevice() = default;

D3D11RenderDevice::D3D11RenderDevice(D3D11RenderDevice&&) noexcept = default;

D3D11RenderDevice& D3D11RenderDevice::operator=(D3D11RenderDevice&&) noexcept = default;

ID3D11Device& D3D11RenderDevice::d3d_device() const noexcept {
    return impl_->d3d_device();
}

ID3D11DeviceContext& D3D11RenderDevice::d3d_context() const noexcept {
    return impl_->d3d_context();
}

void D3D11RenderDevice::recreate() {
    impl_->recreate();
}

} // namespace winelement::platform::win32
