#pragma once

#include <winelement/controls/control.hpp>
#include <winelement/rendering/render_types.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <string>
#include <string_view>

namespace winelement::rendering {
class RenderContext;
}

namespace winelement::controls {

enum class PanelSemanticRole { Generic, Card, Header, Body, Footer, Toolbar };

class Panel : public Control {
  public:
    Panel();

    Panel& set_semantic_role(PanelSemanticRole role) noexcept;
    Panel& set_title(std::string_view title);
    Panel& set_background(rendering::Color color) noexcept;
    Panel& set_border(rendering::Color color, float width);
    Panel& set_style(style::UIElementStyle style) override;
    [[nodiscard]] PanelSemanticRole semantic_role() const noexcept;
    [[nodiscard]] const std::string& title() const noexcept;

  protected:
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;

  private:
    PanelSemanticRole semantic_role_ = PanelSemanticRole::Generic;
    std::string title_;
};

} // namespace winelement::controls
