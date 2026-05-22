#include <winelement/core/binding_expression.hpp>

#include <cctype>
#include <charconv>
#include <stdexcept>

namespace winelement::core {
namespace {

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1U);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1U);
    }
    return value;
}

[[nodiscard]] bool is_identifier_start(char value) noexcept {
    return std::isalpha(static_cast<unsigned char>(value)) != 0 || value == '_';
}

[[nodiscard]] bool is_identifier_continue(char value) noexcept {
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

[[nodiscard]] PropertyValue parse_default_literal(std::string_view literal) {
    literal = trim(literal);
    if (literal.empty()) {
        return PropertyValue{std::string{}};
    }

    if ((literal.front() == '"' && literal.back() == '"') ||
        (literal.front() == '\'' && literal.back() == '\'')) {
        literal.remove_prefix(1U);
        literal.remove_suffix(1U);
        return PropertyValue{std::string{literal}};
    }

    if (literal == "true") {
        return PropertyValue{true};
    }
    if (literal == "false") {
        return PropertyValue{false};
    }

    const auto has_decimal = literal.find('.') != std::string_view::npos;
    if (has_decimal) {
        auto parsed = float{};
        const auto result =
            std::from_chars(literal.data(), literal.data() + literal.size(), parsed);
        if (result.ec == std::errc{} && result.ptr == literal.data() + literal.size()) {
            return PropertyValue{parsed};
        }
    } else {
        auto parsed = int{};
        const auto result =
            std::from_chars(literal.data(), literal.data() + literal.size(), parsed);
        if (result.ec == std::errc{} && result.ptr == literal.data() + literal.size()) {
            return PropertyValue{parsed};
        }
    }

    return PropertyValue{std::string{literal}};
}

[[nodiscard]] const ObservableObject*
object_from_value(const PropertyValue* value, std::shared_ptr<ObservableObject>& keep_alive) {
    if (value == nullptr) {
        return nullptr;
    }
    if (const auto* object = value->get<ObservableObjectPtr>()) {
        keep_alive = *object;
        return keep_alive.get();
    }
    return nullptr;
}

[[nodiscard]] const ObservableObjectList*
list_from_value(const PropertyValue* value, std::shared_ptr<ObservableObjectList>& keep_alive) {
    if (value == nullptr) {
        return nullptr;
    }
    if (const auto* list = value->get<std::shared_ptr<ObservableObjectList>>()) {
        keep_alive = *list;
        return keep_alive.get();
    }
    return nullptr;
}

} // namespace

BindingExpression parse_binding_expression(std::string_view expression) {
    auto result = BindingExpression{};
    result.source = std::string{expression};

    auto path_text = trim(expression);
    if (const auto fallback_pos = path_text.find("??"); fallback_pos != std::string_view::npos) {
        result.default_value = parse_default_literal(path_text.substr(fallback_pos + 2U));
        path_text = trim(path_text.substr(0U, fallback_pos));
    }

    auto offset = std::size_t{0U};
    auto expect_segment = true;
    while (offset < path_text.size()) {
        const auto current = path_text[offset];
        if (std::isspace(static_cast<unsigned char>(current)) != 0) {
            ++offset;
            continue;
        }
        if (current == '.') {
            if (expect_segment) {
                throw std::invalid_argument("binding path contains an empty segment");
            }
            expect_segment = true;
            ++offset;
            continue;
        }
        if (current == '[') {
            ++offset;
            auto index = std::size_t{0U};
            const auto start = path_text.data() + offset;
            const auto end = path_text.data() + path_text.size();
            const auto parse_result = std::from_chars(start, end, index);
            if (parse_result.ec != std::errc{} || parse_result.ptr == start ||
                parse_result.ptr >= end || *parse_result.ptr != ']') {
                throw std::invalid_argument("binding index segment is invalid");
            }
            offset = static_cast<std::size_t>(parse_result.ptr - path_text.data()) + 1U;
            result.path.push_back(
                BindingPathSegment{.kind = BindingPathSegmentKind::Index, .index = index});
            expect_segment = false;
            continue;
        }
        if (!is_identifier_start(current)) {
            throw std::invalid_argument("binding path segment is invalid");
        }

        const auto start = offset++;
        while (offset < path_text.size() && is_identifier_continue(path_text[offset])) {
            ++offset;
        }
        result.path.push_back(
            BindingPathSegment{.kind = BindingPathSegmentKind::Property,
                               .property = std::string{path_text.substr(start, offset - start)}});
        expect_segment = false;
    }

    if (expect_segment && !result.path.empty()) {
        throw std::invalid_argument("binding path cannot end with a separator");
    }
    return result;
}

std::optional<PropertyValue> evaluate_binding_expression(const ObservableObject& source,
                                                         const BindingExpression& expression) {
    auto object = &source;
    auto object_keep_alive = std::shared_ptr<ObservableObject>{};
    auto list = static_cast<const ObservableObjectList*>(nullptr);
    auto list_keep_alive = std::shared_ptr<ObservableObjectList>{};
    auto value = static_cast<const PropertyValue*>(nullptr);

    for (const auto& segment : expression.path) {
        switch (segment.kind) {
        case BindingPathSegmentKind::Property:
            if (object == nullptr) {
                return expression.default_value;
            }
            value = object->value(segment.property);
            object = object_from_value(value, object_keep_alive);
            list = list_from_value(value, list_keep_alive);
            break;
        case BindingPathSegmentKind::Index:
            if (list == nullptr || segment.index >= list->size()) {
                return expression.default_value;
            }
            object_keep_alive = list->at(segment.index);
            object = object_keep_alive.get();
            value = nullptr;
            list = nullptr;
            list_keep_alive.reset();
            break;
        }
    }

    if (value != nullptr) {
        return PropertyValue{*value};
    }
    if (object_keep_alive != nullptr) {
        return PropertyValue{object_keep_alive};
    }
    return expression.default_value;
}

bool set_binding_expression_value(ObservableObject& source, const BindingExpression& expression,
                                  PropertyValue value) {
    if (expression.path.empty() ||
        expression.path.back().kind != BindingPathSegmentKind::Property) {
        return false;
    }

    auto* object = &source;
    auto object_keep_alive = std::shared_ptr<ObservableObject>{};
    auto list = static_cast<const ObservableObjectList*>(nullptr);
    auto list_keep_alive = std::shared_ptr<ObservableObjectList>{};
    auto current_value = static_cast<const PropertyValue*>(nullptr);
    for (auto index = std::size_t{0U}; index + 1U < expression.path.size(); ++index) {
        const auto& segment = expression.path[index];
        switch (segment.kind) {
        case BindingPathSegmentKind::Property:
            if (object == nullptr) {
                return false;
            }
            current_value = object->value(segment.property);
            object =
                const_cast<ObservableObject*>(object_from_value(current_value, object_keep_alive));
            list = list_from_value(current_value, list_keep_alive);
            break;
        case BindingPathSegmentKind::Index:
            if (list == nullptr || segment.index >= list->size()) {
                return false;
            }
            object_keep_alive = list->at(segment.index);
            object = object_keep_alive.get();
            current_value = nullptr;
            list = nullptr;
            list_keep_alive.reset();
            break;
        }
    }

    if (object == nullptr) {
        return false;
    }
    object->set_value(expression.path.back().property, std::move(value));
    return true;
}

} // namespace winelement::core
