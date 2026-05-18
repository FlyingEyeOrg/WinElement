#pragma once

#include <winelement/elements/command_cache.hpp>
#include <winelement/elements/element_descriptor.hpp>
#include <winelement/layout/layout_types.hpp>

#include <cstdint>
#include <utility>

namespace winelement::elements {

struct RenderObjectSnapshot {
    ElementTreeRole role = ElementTreeRole::RenderObject;
    std::uint64_t layout_generation = 0;
    layout::Rect frame{};
    layout::Rect absolute_frame{};
    bool repaint_boundary = false;
    bool has_layer = false;
    bool needs_paint = false;
};

struct RenderObjectState {
    std::uint64_t layout_generation = 0;
    layout::Rect frame{};
    layout::Rect absolute_frame{};
    bool repaint_boundary = false;
    bool has_layer = false;
    bool needs_paint = false;
};

class RenderObject final {
  public:
    [[nodiscard]] RenderObjectSnapshot snapshot(RenderObjectState state) const noexcept {
        return RenderObjectSnapshot{.role = ElementTreeRole::RenderObject,
                                    .layout_generation = state.layout_generation,
                                    .frame = state.frame,
                                    .absolute_frame = state.absolute_frame,
                                    .repaint_boundary = state.repaint_boundary,
                                    .has_layer = state.has_layer,
                                    .needs_paint = state.needs_paint};
    }

    [[nodiscard]] bool can_reuse_content(std::uint64_t generation,
                                         bool needs_paint) const noexcept {
        return content_commands_.can_reuse(generation, needs_paint);
    }

    [[nodiscard]] bool can_reuse_overlay(std::uint64_t generation,
                                         bool needs_paint) const noexcept {
        return overlay_commands_.can_reuse(generation, needs_paint);
    }

    [[nodiscard]] const rendering::RenderCommandList& content_commands() const noexcept {
        return content_commands_.commands();
    }

    [[nodiscard]] const rendering::RenderCommandList& overlay_commands() const noexcept {
        return overlay_commands_.commands();
    }

    void store_content(rendering::RenderCommandList commands, std::uint64_t generation) {
        content_commands_.store(std::move(commands), generation);
    }

    void store_overlay(rendering::RenderCommandList commands, std::uint64_t generation) {
        overlay_commands_.store(std::move(commands), generation);
    }

    void invalidate_commands() noexcept {
        content_commands_.invalidate();
        overlay_commands_.invalidate();
    }

  private:
    CommandCache content_commands_;
    CommandCache overlay_commands_;
};

} // namespace winelement::elements