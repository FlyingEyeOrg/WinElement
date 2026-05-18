#include <winelement/elements/text_edit_model.hpp>

#include <algorithm>
#include <utility>

namespace winelement::elements {

void TextEditModel::set_text(std::string text) {
    text_ = std::move(text);
    selection_anchor_ = clamp_offset(selection_anchor_);
    selection_active_ = clamp_offset(selection_active_);
    if (composition_.active) {
        composition_.replacement_range.start = clamp_offset(composition_.replacement_range.start);
        composition_.replacement_range.end = clamp_offset(composition_.replacement_range.end);
    }
}

const std::string& TextEditModel::text() const noexcept {
    return text_;
}

void TextEditModel::set_selection(std::size_t anchor, std::size_t active) noexcept {
    selection_anchor_ = clamp_offset(anchor);
    selection_active_ = clamp_offset(active);
}

void TextEditModel::clear_selection() noexcept {
    selection_anchor_ = selection_active_;
}

TextRange TextEditModel::ordered_selection() const noexcept {
    return TextRange{.start = std::min(selection_anchor_, selection_active_),
                     .end = std::max(selection_anchor_, selection_active_)};
}

bool TextEditModel::has_selection() const noexcept {
    return selection_anchor_ != selection_active_;
}

std::size_t TextEditModel::caret_offset() const noexcept {
    return selection_active_;
}

void TextEditModel::set_composition(std::string text, TextRange replacement_range) {
    replacement_range.start = clamp_offset(replacement_range.start);
    replacement_range.end = clamp_offset(replacement_range.end);
    if (replacement_range.end < replacement_range.start) {
        std::swap(replacement_range.start, replacement_range.end);
    }
    composition_ = ImeCompositionState{
        .text = std::move(text), .replacement_range = replacement_range, .active = true};
}

void TextEditModel::clear_composition() noexcept {
    composition_ = {};
}

const ImeCompositionState& TextEditModel::composition() const noexcept {
    return composition_;
}

std::string TextEditModel::selected_text() const {
    const auto range = ordered_selection();
    if (range.start >= range.end || range.start >= text_.size()) {
        return {};
    }
    return text_.substr(range.start, range.end - range.start);
}

std::size_t TextEditModel::clamp_offset(std::size_t offset) const noexcept {
    return std::min(offset, text_.size());
}

} // namespace winelement::elements