#pragma once

#include <winelement/rendering/render_types.hpp>

namespace winelement::style {

struct ElementColorScale {
    rendering::Color base{};
    rendering::Color dark2{};
    rendering::Color light3{};
    rendering::Color light5{};
    rendering::Color light7{};
    rendering::Color light8{};
    rendering::Color light9{};
};

struct ElementNeutralColors {
    rendering::Color text_primary{};
    rendering::Color text_regular{};
    rendering::Color text_secondary{};
    rendering::Color text_placeholder{};
    rendering::Color text_disabled{};
    rendering::Color border_darker{};
    rendering::Color border_dark{};
    rendering::Color border_base{};
    rendering::Color border_light{};
    rendering::Color border_lighter{};
    rendering::Color border_extra_light{};
    rendering::Color fill_darker{};
    rendering::Color fill_dark{};
    rendering::Color fill_base{};
    rendering::Color fill_light{};
    rendering::Color fill_lighter{};
    rendering::Color fill_extra_light{};
    rendering::Color fill_blank{};
};

struct ElementColorPalette {
    ElementColorScale primary{};
    ElementColorScale success{};
    ElementColorScale warning{};
    ElementColorScale danger{};
    ElementColorScale error{};
    ElementColorScale info{};
    ElementNeutralColors neutral{};
};

[[nodiscard]] constexpr ElementColorPalette element_colors() noexcept {
    return ElementColorPalette{
        .primary = {.base = rendering::Color::rgba(64, 158, 255),
                    .dark2 = rendering::Color::rgba(51, 126, 204),
                    .light3 = rendering::Color::rgba(121, 187, 255),
                    .light5 = rendering::Color::rgba(160, 207, 255),
                    .light7 = rendering::Color::rgba(198, 226, 255),
                    .light8 = rendering::Color::rgba(217, 236, 255),
                    .light9 = rendering::Color::rgba(236, 245, 255)},
        .success = {.base = rendering::Color::rgba(103, 194, 58),
                    .dark2 = rendering::Color::rgba(82, 155, 46),
                    .light3 = rendering::Color::rgba(149, 212, 117),
                    .light5 = rendering::Color::rgba(179, 225, 157),
                    .light7 = rendering::Color::rgba(209, 237, 196),
                    .light8 = rendering::Color::rgba(225, 243, 216),
                    .light9 = rendering::Color::rgba(240, 249, 235)},
        .warning = {.base = rendering::Color::rgba(230, 162, 60),
                    .dark2 = rendering::Color::rgba(184, 129, 48),
                    .light3 = rendering::Color::rgba(235, 181, 99),
                    .light5 = rendering::Color::rgba(243, 209, 158),
                    .light7 = rendering::Color::rgba(248, 227, 196),
                    .light8 = rendering::Color::rgba(250, 236, 216),
                    .light9 = rendering::Color::rgba(253, 246, 236)},
        .danger = {.base = rendering::Color::rgba(245, 108, 108),
                   .dark2 = rendering::Color::rgba(196, 86, 86),
                   .light3 = rendering::Color::rgba(248, 152, 152),
                   .light5 = rendering::Color::rgba(250, 182, 182),
                   .light7 = rendering::Color::rgba(252, 211, 211),
                   .light8 = rendering::Color::rgba(253, 226, 226),
                   .light9 = rendering::Color::rgba(254, 240, 240)},
        .error = {.base = rendering::Color::rgba(245, 108, 108),
                  .dark2 = rendering::Color::rgba(196, 86, 86),
                  .light3 = rendering::Color::rgba(248, 152, 152),
                  .light5 = rendering::Color::rgba(250, 182, 182),
                  .light7 = rendering::Color::rgba(252, 211, 211),
                  .light8 = rendering::Color::rgba(253, 226, 226),
                  .light9 = rendering::Color::rgba(254, 240, 240)},
        .info = {.base = rendering::Color::rgba(144, 147, 153),
                 .dark2 = rendering::Color::rgba(115, 118, 122),
                 .light3 = rendering::Color::rgba(177, 179, 184),
                 .light5 = rendering::Color::rgba(200, 201, 204),
                 .light7 = rendering::Color::rgba(222, 223, 224),
                 .light8 = rendering::Color::rgba(233, 233, 235),
                 .light9 = rendering::Color::rgba(244, 244, 245)},
        .neutral = {.text_primary = rendering::Color::rgba(48, 49, 51),
                    .text_regular = rendering::Color::rgba(96, 98, 102),
                    .text_secondary = rendering::Color::rgba(144, 147, 153),
                    .text_placeholder = rendering::Color::rgba(168, 171, 178),
                    .text_disabled = rendering::Color::rgba(192, 196, 204),
                    .border_darker = rendering::Color::rgba(205, 208, 214),
                    .border_dark = rendering::Color::rgba(212, 215, 222),
                    .border_base = rendering::Color::rgba(220, 223, 230),
                    .border_light = rendering::Color::rgba(228, 231, 237),
                    .border_lighter = rendering::Color::rgba(235, 238, 245),
                    .border_extra_light = rendering::Color::rgba(242, 246, 252),
                    .fill_darker = rendering::Color::rgba(230, 232, 235),
                    .fill_dark = rendering::Color::rgba(235, 237, 240),
                    .fill_base = rendering::Color::rgba(240, 242, 245),
                    .fill_light = rendering::Color::rgba(245, 247, 250),
                    .fill_lighter = rendering::Color::rgba(250, 250, 250),
                    .fill_extra_light = rendering::Color::rgba(250, 252, 255),
                    .fill_blank = rendering::Color::rgba(255, 255, 255)}};
}

} // namespace winelement::style