#pragma once

#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace winelement::platform::win32 {

class DxRenderDevice final {
  public:
    DxRenderDevice();
    ~DxRenderDevice();

    DxRenderDevice(const DxRenderDevice&) = delete;
    DxRenderDevice& operator=(const DxRenderDevice&) = delete;
    DxRenderDevice(DxRenderDevice&&) noexcept;
    DxRenderDevice& operator=(DxRenderDevice&&) noexcept;

    [[nodiscard]] ID3D11Device& d3d_device() const noexcept;
    [[nodiscard]] ID3D11DeviceContext& d3d_context() const noexcept;
    void recreate();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace winelement::platform::win32
