#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace winelement::win32_detail {

[[nodiscard]] inline std::runtime_error make_hresult_error(std::string_view message,
                                                           HRESULT result) {
    auto text = std::string(message);
    text += " HRESULT=0x";

    constexpr auto digits = "0123456789ABCDEF";
    for (auto shift = 28; shift >= 0; shift -= 4) {
        text += digits[(static_cast<unsigned long>(result) >> shift) & 0x0F];
    }

    return std::runtime_error(text);
}

} // namespace winelement::win32_detail
