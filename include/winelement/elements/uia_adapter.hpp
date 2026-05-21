#pragma once

#include <winelement/elements/semantics.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace winelement::elements {

enum class AutomationControlType {
    Custom,
    Window,
    Button,
    Text,
    Image,
    Edit,
    List,
    ListItem,
    RadioButton,
    Menu,
    MenuItem,
};

struct AutomationNodeSnapshot {
    AutomationControlType control_type = AutomationControlType::Custom;
    std::string name;
    std::string value;
    layout::Rect bounds{};
    SemanticsState state{};
    std::vector<AutomationNodeSnapshot> children;
};

class UiaSemanticsAdapter final {
  public:
    [[nodiscard]] AutomationNodeSnapshot convert(const SemanticsNode& node) const {
        auto snapshot = AutomationNodeSnapshot{.control_type = map_role(node.role),
                                               .name = node.label,
                                               .value = node.value,
                                               .bounds = node.bounds,
                                               .state = node.state};
        snapshot.children.reserve(node.children.size());
        for (const auto& child : node.children) {
            snapshot.children.push_back(convert(child));
        }
        return snapshot;
    }

    [[nodiscard]] static AutomationControlType map_role(SemanticsRole role) noexcept {
        switch (role) {
        case SemanticsRole::Window:
            return AutomationControlType::Window;
        case SemanticsRole::Button:
            return AutomationControlType::Button;
        case SemanticsRole::Text:
            return AutomationControlType::Text;
        case SemanticsRole::Image:
            return AutomationControlType::Image;
        case SemanticsRole::TextInput:
            return AutomationControlType::Edit;
        case SemanticsRole::List:
            return AutomationControlType::List;
        case SemanticsRole::ListItem:
            return AutomationControlType::ListItem;
        case SemanticsRole::Radio:
            return AutomationControlType::RadioButton;
        case SemanticsRole::Menu:
            return AutomationControlType::Menu;
        case SemanticsRole::MenuItem:
            return AutomationControlType::MenuItem;
        default:
            return AutomationControlType::Custom;
        }
    }
};

} // namespace winelement::elements
