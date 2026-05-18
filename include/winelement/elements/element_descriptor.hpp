#pragma once

#include <winelement/core/property.hpp>
#include <winelement/layout/layout_types.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace winelement::elements {

struct ElementPropertyValue {
    core::PropertyMetadata metadata{};
    std::string string_value;
};

struct ElementDescriptor {
    std::string type_name;
    std::string key;
    std::vector<ElementPropertyValue> properties;
    std::vector<ElementDescriptor> children;
};

enum class ElementDiffKind { Insert, Remove, Replace, Update };

struct ElementDiffOperation {
    ElementDiffKind kind = ElementDiffKind::Update;
    std::string key;
    std::size_t old_index = 0U;
    std::size_t new_index = 0U;
};

class ElementDiffer final {
  public:
    [[nodiscard]] std::vector<ElementDiffOperation>
    diff(const std::vector<ElementDescriptor>& old_children,
         const std::vector<ElementDescriptor>& new_children) const;
};

enum class ElementTreeRole { Element, RenderObject };

struct ElementSnapshot {
    ElementTreeRole role = ElementTreeRole::Element;
    std::string type_name;
    std::string key;
    std::string theme_class;
    std::uint64_t layout_generation = 0U;
    layout::Rect frame{};
    layout::Rect absolute_frame{};
    bool relayout_boundary = false;
    bool needs_layout = false;
    bool visible = true;
    std::vector<ElementSnapshot> children;
};

} // namespace winelement::elements