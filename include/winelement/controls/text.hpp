#pragma once

#include <winelement/controls/control.hpp>
#include <winelement/rendering/render_types.hpp>
#include <winelement/rendering/text_engine.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace winelement::rendering {
class RenderContext;
struct TextLayout;
} // namespace winelement::rendering

namespace winelement::controls {

enum class TextType { Primary, Success, Warning, Danger, Info };
enum class TextSize { Default, Large, Small };

class Text final : public Control {
  public:
    Text();

    Text& set_text(std::string_view text);
    Text& set_type(TextType type);
    Text& set_size(TextSize size);
    Text& set_truncated(bool truncated);
    Text& set_max_lines(std::size_t max_lines);
    Text& set_selectable(bool selectable);
    Text& set_copyable(bool copyable);
    Text& set_link_target(std::string_view target);
    Text& set_font_size(float font_size);
    Text& set_color(rendering::Color color) noexcept;
    Text& set_style(style::UIElementStyle style) override;
    [[nodiscard]] TextType type() const noexcept;
    [[nodiscard]] TextSize size() const noexcept;
    [[nodiscard]] bool truncated() const noexcept;
    [[nodiscard]] std::size_t max_lines() const noexcept;
    [[nodiscard]] bool selectable() const noexcept;
    [[nodiscard]] bool copyable() const noexcept;
    [[nodiscard]] const std::string& link_target() const noexcept;
    [[nodiscard]] const std::string& text() const noexcept;

  private:
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;
    void apply_semantic_style();
    [[nodiscard]] layout::Size measure_text(const layout::MeasureInput& input) const;
    [[nodiscard]] rendering::TextLayout text_layout_for_content_rect(layout::Rect rect) const;

    TextType type_ = TextType::Primary;
    TextSize size_ = TextSize::Default;
    std::string link_target_;
    std::size_t max_lines_ = 0;
    bool truncated_ = false;
    bool selectable_ = false;
    bool copyable_ = false;
};

} // namespace winelement::controls
