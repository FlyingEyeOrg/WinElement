#include <winelement/elements/event_routing_policy.hpp>

namespace winelement::elements {

bool EventRoutingPolicy::routes_to_top_layer(KeyEvent event) const noexcept {
    if (event.kind != KeyEventKind::Down) {
        return false;
    }

    switch (event.key) {
    case Key::Escape:
    case Key::Up:
    case Key::Down:
    case Key::Enter:
    case Key::Space:
        return true;
    case Key::Unknown:
    case Key::Tab:
    case Key::Backspace:
    case Key::Delete:
    case Key::Left:
    case Key::Right:
    case Key::Home:
    case Key::End:
    case Key::PageUp:
    case Key::PageDown:
    case Key::A:
    case Key::C:
    case Key::V:
    case Key::X:
    case Key::Z:
        return false;
    }

    return false;
}

bool EventRoutingPolicy::is_text_selection_shortcut(KeyEvent event) const noexcept {
    return event.kind == KeyEventKind::Down && event.modifiers.control &&
           (event.key == Key::A || event.key == Key::C);
}

bool EventRoutingPolicy::dismisses_top_layer(KeyEvent event) const noexcept {
    return event.kind == KeyEventKind::Down && event.key == Key::Escape;
}

bool EventRoutingPolicy::moves_focus(KeyEvent event) const noexcept {
    return event.kind == KeyEventKind::Down && event.key == Key::Tab;
}

} // namespace winelement::elements