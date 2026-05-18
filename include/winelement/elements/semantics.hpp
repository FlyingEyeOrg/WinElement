#pragma once

#include <winelement/layout/layout_types.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace winelement::elements {

enum class SemanticsRole {
    Generic,
    Window,
    Button,
    Text,
    TextInput,
    List,
    ListItem,
    Checkbox,
    Radio,
    Switch,
    Slider,
    Dialog,
    Menu,
    MenuItem,
};

struct SemanticsState {
    bool disabled = false;
    bool focusable = false;
    bool focused = false;
    bool selected = false;
    bool checked = false;
    bool expanded = false;
    bool editable = false;
};

struct SemanticsNode {
    SemanticsRole role = SemanticsRole::Generic;
    std::string label;
    std::string value;
    layout::Rect bounds{};
    SemanticsState state{};
    std::vector<SemanticsNode> children;
};

class SemanticsTree final {
  public:
    void set_root(SemanticsNode node);
    void clear() noexcept;
    [[nodiscard]] const SemanticsNode* root() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept;

  private:
    [[nodiscard]] static std::size_t count_nodes(const SemanticsNode& node) noexcept;

    SemanticsNode root_{};
    bool has_root_ = false;
};

} // namespace winelement::elements