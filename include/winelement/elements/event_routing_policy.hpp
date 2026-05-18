#pragma once

#include <winelement/elements/input_event.hpp>

namespace winelement::elements {

class EventRoutingPolicy final {
  public:
    [[nodiscard]] bool routes_to_top_layer(KeyEvent event) const noexcept;
    [[nodiscard]] bool is_text_selection_shortcut(KeyEvent event) const noexcept;
    [[nodiscard]] bool dismisses_top_layer(KeyEvent event) const noexcept;
    [[nodiscard]] bool moves_focus(KeyEvent event) const noexcept;
};

} // namespace winelement::elements