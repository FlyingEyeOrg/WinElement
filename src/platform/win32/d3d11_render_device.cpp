#include "d3d11_render_device.hpp"

#include "hresult_error.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

namespace winelement::platform::win32 {
namespace {

[[nodiscard]] bool environment_prefers_warp_driver() noexcept {
    wchar_t buffer[16]{};
    const auto length =
        GetEnvironmentVariableW(L"WINELEMENT_D3D_DRIVER", buffer, ARRAYSIZE(buffer));
    if (length == 0 || length >= ARRAYSIZE(buffer)) {
        return false;
    }

    return _wcsicmp(buffer, L"warp") == 0 || _wcsicmp(buffer, L"software") == 0;
}

} // namespace

class D3D11RenderDevice::Impl final {
  public:
    explicit Impl(D3D11RenderDeviceDriver driver) : driver_(driver) {
        create_devices();
    }

    [[nodiscard]] ID3D11Device& d3d_device() const noexcept {
        return *d3d_device_.Get();
    }

    [[nodiscard]] ID3D11DeviceContext& d3d_context() const noexcept {
        return *d3d_context_.Get();
    }

    void recreate() {
        Impl replacement(driver_);
        d3d_device_ = std::move(replacement.d3d_device_);
        d3d_context_ = std::move(replacement.d3d_context_);
        feature_level_ = replacement.feature_level_;
    }

    void trim_idle_resources() noexcept {
        try {
            if (d3d_context_ != nullptr) {
                d3d_context_->ClearState();
                d3d_context_->Flush();
            }

            Microsoft::WRL::ComPtr<IDXGIDevice3> dxgi_device;
            if (d3d_device_ != nullptr && SUCCEEDED(d3d_device_.As(&dxgi_device))) {
                dxgi_device->Trim();
            }
        } catch (...) {
        }
    }

  private:
    void create_devices() {
        const auto create_flags = static_cast<UINT>(D3D11_CREATE_DEVICE_BGRA_SUPPORT);
        D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                              D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                              D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
                                              D3D_FEATURE_LEVEL_9_1};

        const auto use_warp =
            driver_ == D3D11RenderDeviceDriver::Warp ||
            (driver_ == D3D11RenderDeviceDriver::Auto && environment_prefers_warp_driver());
        auto result = HRESULT{};
        if (!use_warp) {
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, create_flags,
                                       feature_levels, ARRAYSIZE(feature_levels),
                                       D3D11_SDK_VERSION, d3d_device_.GetAddressOf(),
                                       &feature_level_, d3d_context_.GetAddressOf());
        }
        if (use_warp || FAILED(result)) {
            d3d_device_.Reset();
            d3d_context_.Reset();
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, create_flags,
                                       feature_levels, ARRAYSIZE(feature_levels), D3D11_SDK_VERSION,
                                       d3d_device_.GetAddressOf(), &feature_level_,
                                       d3d_context_.GetAddressOf());
        }
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to create Direct3D device", result);
        }

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
        result = d3d_device_.As(&dxgi_device);
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to query DXGI device", result);
        }

        Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi_device1;
        if (SUCCEEDED(dxgi_device.As(&dxgi_device1))) {
            result = dxgi_device1->SetMaximumFrameLatency(1);
            if (FAILED(result)) {
                throw win32_detail::make_hresult_error("failed to set maximum DXGI frame latency",
                                                       result);
            }
        }
    }

    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;
    D3D11RenderDeviceDriver driver_ = D3D11RenderDeviceDriver::Auto;
    D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_9_1;
};

D3D11RenderDevice::D3D11RenderDevice(D3D11RenderDeviceDriver driver)
    : impl_(std::make_unique<Impl>(driver)) {}

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

void D3D11RenderDevice::trim_idle_resources() noexcept {
    impl_->trim_idle_resources();
}

} // namespace winelement::platform::win32
