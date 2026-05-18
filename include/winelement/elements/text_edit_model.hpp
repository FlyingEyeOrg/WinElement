#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace winelement::elements {

struct TextRange {
    std::size_t start = 0U;
    std::size_t end = 0U;
};

struct ImeCompositionState {
    std::string text;
    TextRange replacement_range{};
    bool active = false;
};

class TextEditModel final {
  public:
    void set_text(std::string text);
    [[nodiscard]] const std::string& text() const noexcept;

    void set_selection(std::size_t anchor, std::size_t active) noexcept;
    void clear_selection() noexcept;
    [[nodiscard]] TextRange ordered_selection() const noexcept;
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] std::size_t caret_offset() const noexcept;

    void set_composition(std::string text, TextRange replacement_range);
    void clear_composition() noexcept;
    [[nodiscard]] const ImeCompositionState& composition() const noexcept;

    [[nodiscard]] std::string selected_text() const;

  private:
    [[nodiscard]] std::size_t clamp_offset(std::size_t offset) const noexcept;

    std::string text_;
    std::size_t selection_anchor_ = 0U;
    std::size_t selection_active_ = 0U;
    ImeCompositionState composition_{};
};

} // namespace winelement::elements