#pragma once

#include <winelement/elements/placement_engine.hpp>
#include <winelement/elements/ui_element.hpp>
#include <winelement/layout/layout_types.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace winelement::elements {

class PopupHandle final {
  public:
    PopupHandle() noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return id_ != 0U;
    }

    void reset() noexcept {
        id_ = 0U;
    }

  private:
    friend class PopupManager;

    explicit PopupHandle(std::uint64_t id) noexcept : id_(id) {}

    std::uint64_t id_ = 0;
};

struct PopupOptions {
    layout::Rect anchor_rect{};
    layout::Size size{};
    layout::Rect viewport_rect{};
    PopupPlacement placement = PopupPlacement::BottomStart;
    float gap = 4.0F;
    float viewport_margin = 4.0F;
    bool allow_flip = true;
    bool allow_shift = true;
    bool match_anchor_width = false;
    bool light_dismiss = true;
    bool preserve_focus = false;
    rendering::Color backdrop_color = rendering::Color{0, 0, 0, 0};
    bool close_on_escape = true;
    bool modal = false;
    std::function<void()> on_dismissed;
    const UIElement* logical_owner = nullptr;
};

struct PopupOpenResult {
    PopupHandle handle{};
    layout::Rect bounds{};
    PopupPlacement placement = PopupPlacement::BottomStart;
    bool flipped = false;
    bool shifted = false;
};

class PopupManager final {
  public:
    explicit PopupManager(UIElement& root) noexcept;

    [[nodiscard]] PopupOpenResult open(std::unique_ptr<UIElement> element, PopupOptions options);
    [[nodiscard]] PopupOpenResult
    open_for_anchor(UIElement& anchor, std::unique_ptr<UIElement> element, PopupOptions options);
    [[nodiscard]] bool close(PopupHandle handle);
    [[nodiscard]] bool bring_to_front(PopupHandle handle);
    [[nodiscard]] bool update_placement(PopupHandle handle, PopupOptions options);
    [[nodiscard]] bool update_placement_for_anchor(PopupHandle handle, UIElement& anchor,
                                                   PopupOptions options);
    [[nodiscard]] UIElement* element(PopupHandle handle) noexcept;
    [[nodiscard]] const UIElement* element(PopupHandle handle) const noexcept;

  private:
    [[nodiscard]] UIElement& host() noexcept;
    [[nodiscard]] const UIElement& host() const noexcept;
    [[nodiscard]] PopupPlacementResult resolve_placement(const UIElement& host,
                                                         PopupOptions options) const noexcept;

    UIElement* root_ = nullptr;
};

} // namespace winelement::elements