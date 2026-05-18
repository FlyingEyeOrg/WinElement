#include <winelement/elements/element_descriptor.hpp>

#include <algorithm>

namespace winelement::elements {
namespace {

[[nodiscard]] bool same_identity(const ElementDescriptor& left,
                                 const ElementDescriptor& right) noexcept {
    if (!left.key.empty() || !right.key.empty()) {
        return left.key == right.key && left.type_name == right.type_name;
    }
    return left.type_name == right.type_name;
}

} // namespace

std::vector<ElementDiffOperation>
ElementDiffer::diff(const std::vector<ElementDescriptor>& old_children,
                    const std::vector<ElementDescriptor>& new_children) const {
    std::vector<ElementDiffOperation> operations;
    const auto common = std::min(old_children.size(), new_children.size());
    operations.reserve(std::max(old_children.size(), new_children.size()));

    for (std::size_t index = 0U; index < common; ++index) {
        const auto& old_child = old_children[index];
        const auto& new_child = new_children[index];
        operations.push_back(ElementDiffOperation{
            .kind = same_identity(old_child, new_child) ? ElementDiffKind::Update
                                                        : ElementDiffKind::Replace,
            .key = new_child.key.empty() ? old_child.key : new_child.key,
            .old_index = index,
            .new_index = index});
    }

    for (std::size_t index = common; index < old_children.size(); ++index) {
        operations.push_back(ElementDiffOperation{.kind = ElementDiffKind::Remove,
                                                  .key = old_children[index].key,
                                                  .old_index = index,
                                                  .new_index = common});
    }
    for (std::size_t index = common; index < new_children.size(); ++index) {
        operations.push_back(ElementDiffOperation{.kind = ElementDiffKind::Insert,
                                                  .key = new_children[index].key,
                                                  .old_index = common,
                                                  .new_index = index});
    }
    return operations;
}

} // namespace winelement::elements