#include <winelement/elements/semantics.hpp>

#include <utility>

namespace winelement::elements {

void SemanticsTree::set_root(SemanticsNode node) {
    root_ = std::move(node);
    has_root_ = true;
}

void SemanticsTree::clear() noexcept {
    root_ = {};
    has_root_ = false;
}

const SemanticsNode* SemanticsTree::root() const noexcept {
    return has_root_ ? &root_ : nullptr;
}

bool SemanticsTree::empty() const noexcept {
    return !has_root_;
}

std::size_t SemanticsTree::node_count() const noexcept {
    return has_root_ ? count_nodes(root_) : 0U;
}

std::size_t SemanticsTree::count_nodes(const SemanticsNode& node) noexcept {
    auto count = std::size_t{1U};
    for (const auto& child : node.children) {
        count += count_nodes(child);
    }
    return count;
}

} // namespace winelement::elements