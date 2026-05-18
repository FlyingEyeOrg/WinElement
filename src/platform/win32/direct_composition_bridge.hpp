#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <memory>

struct IDXGIDevice;
struct IDXGISwapChain1;

namespace winelement::rendering {
struct CompositorPromotionPlan;
}

namespace winelement::platform::win32 {

class DirectCompositionBridge final {
  public:
    DirectCompositionBridge(HWND hwnd, IDXGIDevice* dxgi_device);
    ~DirectCompositionBridge();

    DirectCompositionBridge(const DirectCompositionBridge&) = delete;
    DirectCompositionBridge& operator=(const DirectCompositionBridge&) = delete;
    DirectCompositionBridge(DirectCompositionBridge&&) noexcept;
    DirectCompositionBridge& operator=(DirectCompositionBridge&&) noexcept;

    void bind_swap_chain(IDXGISwapChain1* swap_chain);
    void apply_promotion_plan(const rendering::CompositorPromotionPlan& plan);
    void clear_content() noexcept;
    void commit();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace winelement::platform::win32