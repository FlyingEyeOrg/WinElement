#pragma once

#include "d3d11_render_device.hpp"

#include <winelement/rendering/compositor.hpp>
#include <winelement/rendering/render_command_list.hpp>
#include <winelement/rendering/render_frame_graph.hpp>
#include <winelement/rendering/render_resource_queue.hpp>
#include <winelement/rendering/render_scene.hpp>
#include <winelement/rendering/render_types.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <memory>

namespace winelement::platform::win32 {

class D3D11CompositionSurface final {
  public:
    enum class RenderResult { Rendered, Skipped, StaleTargetSize, DeviceLost };

    explicit D3D11CompositionSurface(HWND hwnd);
    D3D11CompositionSurface(HWND hwnd, D3D11RenderDeviceDriver driver);
    D3D11CompositionSurface(HWND hwnd, std::shared_ptr<D3D11RenderDevice> device);
    ~D3D11CompositionSurface();

    D3D11CompositionSurface(const D3D11CompositionSurface&) = delete;
    D3D11CompositionSurface& operator=(const D3D11CompositionSurface&) = delete;
    D3D11CompositionSurface(D3D11CompositionSurface&&) noexcept;
    D3D11CompositionSurface& operator=(D3D11CompositionSurface&&) noexcept;

    void set_dpi(float dpi) noexcept;
    void invalidate_surface_size() noexcept;
    void discard() noexcept;
    void trim_idle_resources() noexcept;
    void upload_resource(rendering::RenderResourceUpload upload) noexcept;
    [[nodiscard]] RenderResult render(rendering::Color clear_color,
                                      const rendering::DirtyRegion& dirty_region,
                                      std::uint32_t target_pixel_width,
                                      std::uint32_t target_pixel_height,
                                      const rendering::RenderScene* scene,
                                      const rendering::CompositorPromotionPlan& promotion_plan,
                                      const rendering::RenderFrameGraph* frame_graph);
    [[nodiscard]] bool consume_device_recreated() noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace winelement::platform::win32
