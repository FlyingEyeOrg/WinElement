#pragma once

#include <winelement/core/binding_expression.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace winelement::elements {

enum class BindingMode { OneWay, TwoWay, OneTime };

using BindingConverter = std::function<std::optional<core::PropertyValue>(
    const core::PropertyValue& value, const core::PropertyMetadata* target)>;

struct Binding {
    std::shared_ptr<core::ObservableObject> source;
    core::BindingExpression expression;
    BindingMode mode = BindingMode::OneWay;
    BindingConverter converter;
    bool use_data_context = true;

    [[nodiscard]] static Binding path(std::string_view expression_text,
                                      BindingMode binding_mode = BindingMode::OneWay) {
        return Binding{.expression = core::parse_binding_expression(expression_text),
                       .mode = binding_mode};
    }

    [[nodiscard]] Binding
    with_source(std::shared_ptr<core::ObservableObject> explicit_source) const {
        auto copy = *this;
        copy.source = std::move(explicit_source);
        copy.use_data_context = copy.source == nullptr;
        return copy;
    }

    [[nodiscard]] Binding with_converter(BindingConverter next_converter) const {
        auto copy = *this;
        copy.converter = std::move(next_converter);
        return copy;
    }
};

} // namespace winelement::elements
