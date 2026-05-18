#pragma once

#include <type_traits>
#include <utility>

namespace winelement::core {

template <typename Derived> class StaticDispatchBase {
  public:
    [[nodiscard]] constexpr Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    [[nodiscard]] constexpr const Derived& derived() const noexcept {
        return static_cast<const Derived&>(*this);
    }
};

template <typename Derived, typename Visitor>
constexpr decltype(auto) static_visit(Derived&& derived, Visitor&& visitor) {
    return std::forward<Visitor>(visitor)(std::forward<Derived>(derived));
}

template <typename Base, typename Derived>
inline constexpr bool is_static_dispatch_compatible_v =
    std::is_base_of_v<StaticDispatchBase<Derived>, Base> ||
    std::is_base_of_v<StaticDispatchBase<Derived>, Derived>;

} // namespace winelement::core
