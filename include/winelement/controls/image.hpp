#pragma once

#include <winelement/controls/control.hpp>
#include <winelement/rendering/render_resource_queue.hpp>
#include <winelement/rendering/render_types.hpp>
#include <winelement/style/ui_element_style.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace winelement::rendering {
class RenderContext;
}

namespace winelement::controls {

enum class ImageFit { Fill, Contain, Cover, None, ScaleDown };

struct ImageSource {
    rendering::RenderResourceId resource_id{};
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return resource_id.value != 0U && width > 0U && height > 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(ImageSource, ImageSource) noexcept = default;
};

class Image final : public Control {
  public:
    Image();

    Image& set_source(ImageSource source);
    Image& set_source(rendering::RenderResourceId resource_id, std::uint32_t width,
                      std::uint32_t height);
    Image& clear_source();
    Image& set_source_rect(layout::Rect source_rect);
    Image& set_source_rect(std::optional<layout::Rect> source_rect);
    Image& clear_source_rect();
    Image& set_object_fit(ImageFit fit) noexcept;
    Image& set_object_position(float x, float y);
    Image& set_image_opacity(float opacity) noexcept;
    Image& set_alt_text(std::string_view alt_text);
    Image& set_style(style::UIElementStyle style) override;

    [[nodiscard]] ImageSource source() const noexcept;
    [[nodiscard]] bool has_source() const noexcept;
    [[nodiscard]] std::optional<layout::Rect> source_rect() const noexcept;
    [[nodiscard]] ImageFit object_fit() const noexcept;
    [[nodiscard]] float object_position_x() const noexcept;
    [[nodiscard]] float object_position_y() const noexcept;
    [[nodiscard]] float image_opacity() const noexcept;
    [[nodiscard]] const std::string& alt_text() const noexcept;

  private:
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override;

    [[nodiscard]] layout::Size measure_image(const layout::MeasureInput& input) const;
    [[nodiscard]] layout::Rect image_content_rect(layout::Rect absolute_frame) const noexcept;
    [[nodiscard]] std::optional<layout::Rect> effective_source_rect() const noexcept;
    [[nodiscard]] layout::Rect destination_rect_for(layout::Rect content,
                                                    layout::Rect source) const noexcept;

    ImageSource source_;
    std::optional<layout::Rect> source_rect_;
    ImageFit object_fit_ = ImageFit::Contain;
    float object_position_x_ = 0.5F;
    float object_position_y_ = 0.5F;
    float image_opacity_ = 1.0F;
    std::string alt_text_;
};

} // namespace winelement::controls
