#include "direct_composition_bridge.hpp"

#include "hresult_error.hpp"

#include <winelement/rendering/compositor.hpp>

#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <unordered_map>

namespace winelement::platform::win32 {

class DirectCompositionBridge::Impl final {
  public:
    Impl(HWND hwnd, IDXGIDevice* dxgi_device) {
        if (hwnd == nullptr || dxgi_device == nullptr) {
            throw std::invalid_argument(
                "DirectComposition bridge requires a valid HWND and DXGI device");
        }

        auto result = DCompositionCreateDevice(dxgi_device, IID_PPV_ARGS(&device_));
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to create DirectComposition device",
                                                   result);
        }

        result = device_->CreateTargetForHwnd(hwnd, TRUE, &target_);
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to create DirectComposition target",
                                                   result);
        }

        result = device_->CreateVisual(&root_visual_);
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to create DirectComposition root visual",
                                                   result);
        }

        result = device_->CreateVisual(&content_visual_);
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error(
                "failed to create DirectComposition content visual", result);
        }

        result = root_visual_->AddVisual(content_visual_.Get(), TRUE, nullptr);
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error(
                "failed to attach DirectComposition content visual", result);
        }

        result = target_->SetRoot(root_visual_.Get());
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to set DirectComposition root visual",
                                                   result);
        }

        result = device_->Commit();
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error("failed to commit DirectComposition target",
                                                   result);
        }
    }

    void bind_swap_chain(IDXGISwapChain1* swap_chain) {
        if (swap_chain == nullptr) {
            throw std::invalid_argument("DirectComposition bridge requires a valid swap chain");
        }

        const auto result = content_visual_->SetContent(swap_chain);
        if (FAILED(result)) {
            throw win32_detail::make_hresult_error(
                "failed to bind swap chain to DirectComposition visual", result);
        }

        commit();
    }

    void apply_promotion_plan(const rendering::CompositorPromotionPlan& plan) {
        static_cast<void>(plan);
        // Promotion candidates remain advisory until the renderer provides real promoted surfaces.
        clear_promoted_visuals();
    }

    void clear_content() noexcept {
        if (root_visual_ == nullptr || content_visual_ == nullptr) {
            return;
        }

        static_cast<void>(content_visual_->SetContent(nullptr));
        clear_promoted_visuals();
    }

    void commit() {
        if (device_ != nullptr) {
            const auto result = device_->Commit();
            if (FAILED(result)) {
                throw win32_detail::make_hresult_error("failed to commit DirectComposition device",
                                                       result);
            }
        }
    }

  private:
    void clear_promoted_visuals() noexcept {
        if (root_visual_ == nullptr) {
            promoted_visuals_.clear();
            return;
        }

        for (auto& [id, promoted] : promoted_visuals_) {
            static_cast<void>(id);
            if (promoted.visual != nullptr && promoted.attached) {
                static_cast<void>(root_visual_->RemoveVisual(promoted.visual.Get()));
            }
        }
        promoted_visuals_.clear();
    }
    struct PromotedVisual {
        Microsoft::WRL::ComPtr<IDCompositionVisual> visual;
        bool attached = false;
    };

    Microsoft::WRL::ComPtr<IDCompositionDevice> device_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> root_visual_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> content_visual_;
    std::unordered_map<std::uint64_t, PromotedVisual> promoted_visuals_;
};

DirectCompositionBridge::DirectCompositionBridge(HWND hwnd, IDXGIDevice* dxgi_device)
    : impl_(std::make_unique<Impl>(hwnd, dxgi_device)) {}

DirectCompositionBridge::~DirectCompositionBridge() = default;

DirectCompositionBridge::DirectCompositionBridge(DirectCompositionBridge&&) noexcept = default;

DirectCompositionBridge&
DirectCompositionBridge::operator=(DirectCompositionBridge&&) noexcept = default;

void DirectCompositionBridge::bind_swap_chain(IDXGISwapChain1* swap_chain) {
    if (impl_ != nullptr) {
        impl_->bind_swap_chain(swap_chain);
    }
}

void DirectCompositionBridge::apply_promotion_plan(const rendering::CompositorPromotionPlan& plan) {
    if (impl_ != nullptr) {
        impl_->apply_promotion_plan(plan);
    }
}

void DirectCompositionBridge::clear_content() noexcept {
    if (impl_ != nullptr) {
        impl_->clear_content();
    }
}

void DirectCompositionBridge::commit() {
    if (impl_ != nullptr) {
        impl_->commit();
    }
}

} // namespace winelement::platform::win32
