#pragma once

#include <winelement/elements/ui_element.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace winelement::elements {

template <typename T> class ElementBuilder final {
  public:
    static_assert(std::is_base_of_v<UIElement, T>, "ElementBuilder requires a UIElement type");

    explicit ElementBuilder(std::unique_ptr<T> element) noexcept : element_(std::move(element)) {}

    template <typename Configure> ElementBuilder& with(Configure&& configure) {
        std::forward<Configure>(configure)(*element_);
        return *this;
    }

    template <typename Configure> ElementBuilder& layout(Configure&& configure) {
        element_->configure_layout(std::forward<Configure>(configure));
        return *this;
    }

    template <typename Child> ElementBuilder& child(ElementBuilder<Child> child_builder) {
        element_->append_child(child_builder.build());
        return *this;
    }

    ElementBuilder& child(std::unique_ptr<UIElement> child_element) {
        element_->append_child(std::move(child_element));
        return *this;
    }

    [[nodiscard]] T& get() noexcept {
        return *element_;
    }

    [[nodiscard]] std::unique_ptr<T> build() noexcept {
        return std::move(element_);
    }

  private:
    std::unique_ptr<T> element_;
};

template <typename T, typename... Args> [[nodiscard]] ElementBuilder<T> element(Args&&... args) {
    static_assert(std::is_base_of_v<UIElement, T>, "element<T>() requires a UIElement type");
    return ElementBuilder<T>{std::make_unique<T>(std::forward<Args>(args)...)};
}

} // namespace winelement::elements
