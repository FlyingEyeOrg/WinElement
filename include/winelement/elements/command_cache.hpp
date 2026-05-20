#pragma once

#include <winelement/rendering/render_command_list.hpp>

#include <cstdint>
#include <optional>
#include <utility>

namespace winelement::elements {

class CommandCache final {
  public:
    [[nodiscard]] bool can_reuse(std::uint64_t generation, bool needs_paint) const noexcept {
        return commands_.has_value() && valid_ && !needs_paint && generation_ == generation;
    }

    [[nodiscard]] const rendering::RenderCommandList& commands() const noexcept;

    void store(rendering::RenderCommandList commands, std::uint64_t generation) {
        if (commands.empty()) {
            commands_.reset();
            generation_ = 0;
            valid_ = false;
            return;
        }
        commands_ = std::move(commands);
        generation_ = generation;
        valid_ = true;
    }

    void invalidate() noexcept {
        valid_ = false;
    }

    void clear() noexcept {
        commands_.reset();
        generation_ = 0;
        valid_ = false;
    }

  private:
    std::optional<rendering::RenderCommandList> commands_;
    std::uint64_t generation_ = 0;
    bool valid_ = false;
};

} // namespace winelement::elements
