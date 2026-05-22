#pragma once

#include <winelement/core/observable.hpp>

#include <memory>
#include <string>
#include <typeindex>
#include <utility>

namespace winelement::core {

class ViewModel : public ObservableObject {
  public:
    [[nodiscard]] virtual std::type_index view_model_type() const noexcept = 0;
};

template <typename T>
class TypedViewModel : public ViewModel {
  public:
    [[nodiscard]] std::type_index view_model_type() const noexcept override {
        return std::type_index(typeid(T));
    }

    template <typename ValueType>
    ObservableProperty<ValueType> declare(std::string name, ValueType default_value = {}) {
        set(std::string{name}, std::move(default_value));
        return ObservableProperty<ValueType>{shared_from_this(), std::move(name)};
    }
};

}  // namespace winelement::core
