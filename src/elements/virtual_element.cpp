#include <winelement/elements/virtual_element.hpp>

#include <algorithm>
#include <stdexcept>

namespace winelement::elements {
namespace {

const core::Property<std::string>& virtual_type_property() {
    static const auto property =
        core::make_property<std::string>("virtual.type", core::PropertyInvalidation::None);
    return property;
}

const core::Property<std::string>& virtual_key_property() {
    static const auto property =
        core::make_property<std::string>("virtual.key", core::PropertyInvalidation::None);
    return property;
}

struct RealizedVirtualElement {
    std::string type_name;
    std::string key;
    std::unique_ptr<UIElement> element;
};

[[nodiscard]] std::string property_string(const UIElement& element,
                                          const core::Property<std::string>& property) {
    if (const auto* value = element.properties().local_value(property)) {
        return *value;
    }
    return {};
}

[[nodiscard]] bool matches(const RealizedVirtualElement& realized,
                           const VirtualElement& next) noexcept {
    if (!realized.key.empty() || !next.key.empty()) {
        return realized.key == next.key && realized.type_name == next.type_name;
    }
    return realized.type_name == next.type_name;
}

} // namespace

void ElementReconciler::reconcile_children(UIElement& parent,
                                           std::span<const VirtualElement> children) const {
    auto realized = std::vector<RealizedVirtualElement>{};
    realized.reserve(parent.child_count());
    while (parent.child_count() > 0U) {
        auto child = parent.remove_child_at(parent.child_count() - 1U);
        realized.push_back(
            RealizedVirtualElement{.type_name = property_string(*child, virtual_type_property()),
                                   .key = property_string(*child, virtual_key_property()),
                                   .element = std::move(child)});
    }
    std::reverse(realized.begin(), realized.end());

    for (const auto& descriptor : children) {
        auto iterator =
            std::find_if(realized.begin(), realized.end(),
                         [&descriptor](const RealizedVirtualElement& candidate) noexcept {
                             return candidate.element != nullptr && matches(candidate, descriptor);
                         });

        auto element = std::unique_ptr<UIElement>{};
        if (iterator != realized.end()) {
            element = std::move(iterator->element);
        } else {
            if (!descriptor.factory) {
                throw std::invalid_argument("virtual element factory must not be empty");
            }
            element = descriptor.factory();
        }

        element->set_property(virtual_type_property(), descriptor.type_name)
            .set_property(virtual_key_property(), descriptor.key);
        if (descriptor.configure) {
            descriptor.configure(*element);
        }
        if (!descriptor.children.empty()) {
            reconcile_children(*element, descriptor.children);
        }
        parent.append_child(std::move(element));
    }
}

} // namespace winelement::elements
