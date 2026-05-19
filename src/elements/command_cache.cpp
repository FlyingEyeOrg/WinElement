#include <winelement/elements/command_cache.hpp>

namespace winelement::elements {

const rendering::RenderCommandList& CommandCache::commands() const noexcept {
    static const auto empty_commands = rendering::RenderCommandList{};
    return commands_.has_value() ? *commands_ : empty_commands;
}

} // namespace winelement::elements
