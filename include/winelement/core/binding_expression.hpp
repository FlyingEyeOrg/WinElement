#pragma once

#include <winelement/core/observable.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winelement::core {

enum class BindingPathSegmentKind { Property, Index };

struct BindingPathSegment {
    BindingPathSegmentKind kind = BindingPathSegmentKind::Property;
    std::string property;
    std::size_t index = 0U;
};

struct BindingExpression {
    std::string source;
    std::vector<BindingPathSegment> path;
    std::optional<PropertyValue> default_value;

    [[nodiscard]] bool empty() const noexcept {
        return path.empty();
    }
};

[[nodiscard]] BindingExpression parse_binding_expression(std::string_view expression);
[[nodiscard]] std::optional<PropertyValue>
evaluate_binding_expression(const ObservableObject& source, const BindingExpression& expression);
[[nodiscard]] bool set_binding_expression_value(ObservableObject& source,
                                                const BindingExpression& expression,
                                                PropertyValue value);

} // namespace winelement::core
