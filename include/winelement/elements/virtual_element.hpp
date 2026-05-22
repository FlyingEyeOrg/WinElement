#pragma once

#include <winelement/elements/ui_element.hpp>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace winelement::elements {

struct VirtualElement {
    using Factory = std::function<std::unique_ptr<UIElement>()>;
    using Configure = std::function<void(UIElement& element)>;

    std::string type_name;
    std::string key;
    Factory factory;
    Configure configure;
    std::vector<VirtualElement> children;

    VirtualElement& child(VirtualElement element) {
        children.push_back(std::move(element));
        return *this;
    }
};

template <typename T, typename Configure>
[[nodiscard]] VirtualElement make_virtual_element(std::string key, Configure configure) {
    static_assert(std::is_base_of_v<UIElement, T>,
                  "make_virtual_element requires a UIElement-derived type");
    auto element = VirtualElement{};
    element.type_name = typeid(T).name();
    element.key = std::move(key);
    element.factory = []() { return std::make_unique<T>(); };
    element.configure = [configure = std::move(configure)](UIElement& target) mutable {
        configure(static_cast<T&>(target));
    };
    return element;
}

template <typename T> [[nodiscard]] VirtualElement make_virtual_element(std::string key = {}) {
    static_assert(std::is_base_of_v<UIElement, T>,
                  "make_virtual_element requires a UIElement-derived type");
    auto element = VirtualElement{};
    element.type_name = typeid(T).name();
    element.key = std::move(key);
    element.factory = []() { return std::make_unique<T>(); };
    return element;
}

class ElementReconciler final {
  public:
    void reconcile_children(UIElement& parent, std::span<const VirtualElement> children) const;
};

} // namespace winelement::elements
