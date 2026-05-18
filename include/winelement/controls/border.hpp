#pragma once

#include <winelement/controls/control.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <string>
#include <string_view>

namespace winelement::controls {

enum class BorderPreset { Plain, Primary, Success, Warning, Danger, Info };
enum class BorderShadow { None, Light, Base, Dark };
enum class BorderSectionRole { Frame, Card, Header, Body, Footer };

class Border final : public Control {
  public:
    Border();

    Border& set_preset(BorderPreset preset);
    Border& set_shadow_preset(BorderShadow shadow);
    Border& set_section_role(BorderSectionRole role) noexcept;
    Border& set_title(std::string_view title);
    Border& set_style(style::UIElementStyle style) override;
    [[nodiscard]] BorderPreset preset() const noexcept;
    [[nodiscard]] BorderShadow shadow_preset() const noexcept;
    [[nodiscard]] BorderSectionRole section_role() const noexcept;
    [[nodiscard]] const std::string& title() const noexcept;

  private:
    void apply_semantic_style();

    BorderPreset preset_ = BorderPreset::Plain;
    BorderShadow shadow_preset_ = BorderShadow::None;
    BorderSectionRole section_role_ = BorderSectionRole::Frame;
    std::string title_;
};

} // namespace winelement::controls
