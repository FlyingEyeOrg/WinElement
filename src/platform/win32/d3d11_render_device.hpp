#pragma once

#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace winelement::platform::win32 {

class D3D11RenderDevice final {
  public:
    D3D11RenderDevice();
    ~D3D11RenderDevice();

    D3D11RenderDevice(const D3D11RenderDevice&) = delete;
    D3D11RenderDevice& operator=(const D3D11RenderDevice&) = delete;
    D3D11RenderDevice(D3D11RenderDevice&&) noexcept;
    D3D11RenderDevice& operator=(D3D11RenderDevice&&) noexcept;

    [[nodiscard]] ID3D11Device& d3d_device() const noexcept;
    [[nodiscard]] ID3D11DeviceContext& d3d_context() const noexcept;
    void recreate();
    void trim_idle_resources() noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace winelement::platform::win32
