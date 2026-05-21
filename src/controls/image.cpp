#include <winelement/controls/image.hpp>

#include <winelement/elements/semantics.hpp>
#include <winelement/rendering/render_context.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace winelement::controls {
namespace {

[[nodiscard]] float finite_non_negative(float value) noexcept {
    return std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
}

[[nodiscard]] float constrained_available(float value) noexcept {
    return std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
}

[[nodiscard]] float measured_axis(float natural, layout::MeasureMode mode, float available) {
    const auto constraint = constrained_available(available);
    switch (mode) {
    case layout::MeasureMode::Exactly:
        return constraint;
    case layout::MeasureMode::AtMost:
        return std::min(natural, constraint);
    case layout::MeasureMode::Undefined:
        return natural;
    }
    return natural;
}

} // namespace

Image::Image() : Control() {
    apply_style_value(style::default_image_style(), true);
    set_theme_class(style::theme_class::image);
    set_semantics_role(elements::SemanticsRole::Image);
    set_measure_callback(
        [this](const layout::MeasureInput& input) { return measure_image(input); });
}

Image& Image::set_source(ImageSource source) {
    if (source_ == source) {
        return *this;
    }

    source_ = source;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Image& Image::set_source(rendering::RenderResourceId resource_id, std::uint32_t width,
                         std::uint32_t height) {
    return set_source(ImageSource{.resource_id = resource_id, .width = width, .height = height});
}

Image& Image::clear_source() {
    if (!source_.valid() && !source_rect_.has_value()) {
        return *this;
    }

    source_ = {};
    source_rect_.reset();
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Image& Image::set_source_rect(layout::Rect source_rect) {
    if (!layout::is_finite_rect(source_rect) || source_rect.width < 0.0F ||
        source_rect.height < 0.0F) {
        throw std::invalid_argument("image source rect must be finite and non-negative");
    }
    return set_source_rect(std::optional<layout::Rect>{source_rect});
}

Image& Image::set_source_rect(std::optional<layout::Rect> source_rect) {
    if (source_rect.has_value() && (!layout::is_finite_rect(*source_rect) ||
                                    source_rect->width < 0.0F || source_rect->height < 0.0F)) {
        throw std::invalid_argument("image source rect must be finite and non-negative");
    }
    if (source_rect_ == source_rect) {
        return *this;
    }

    source_rect_ = source_rect;
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Image& Image::clear_source_rect() {
    if (!source_rect_.has_value()) {
        return *this;
    }

    source_rect_.reset();
    mark_measure_dirty();
    invalidate_paint();
    return *this;
}

Image& Image::set_object_fit(ImageFit fit) noexcept {
    if (object_fit_ == fit) {
        return *this;
    }

    object_fit_ = fit;
    invalidate_paint();
    return *this;
}

Image& Image::set_object_position(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        throw std::invalid_argument("image object position must be finite");
    }
    if (object_position_x_ == x && object_position_y_ == y) {
        return *this;
    }

    object_position_x_ = x;
    object_position_y_ = y;
    invalidate_paint();
    return *this;
}

Image& Image::set_image_opacity(float opacity) noexcept {
    const auto normalized = std::isfinite(opacity) ? std::clamp(opacity, 0.0F, 1.0F) : 1.0F;
    if (image_opacity_ == normalized) {
        return *this;
    }

    image_opacity_ = normalized;
    invalidate_paint();
    return *this;
}

Image& Image::set_alt_text(std::string_view alt_text) {
    if (alt_text_ == alt_text) {
        return *this;
    }

    alt_text_ = std::string(alt_text);
    set_semantics_label(alt_text_);
    return *this;
}

Image& Image::set_style(style::UIElementStyle style) {
    apply_style_value(std::move(style), false);
    mark_measure_dirty();
    return *this;
}

ImageSource Image::source() const noexcept {
    return source_;
}

bool Image::has_source() const noexcept {
    return source_.valid();
}

std::optional<layout::Rect> Image::source_rect() const noexcept {
    return source_rect_;
}

ImageFit Image::object_fit() const noexcept {
    return object_fit_;
}

float Image::object_position_x() const noexcept {
    return object_position_x_;
}

float Image::object_position_y() const noexcept {
    return object_position_y_;
}

float Image::image_opacity() const noexcept {
    return image_opacity_;
}

const std::string& Image::alt_text() const noexcept {
    return alt_text_;
}

layout::Size Image::measure_image(const layout::MeasureInput& input) const {
    const auto& current_style = style_storage();
    const auto horizontal_padding = current_style.padding.left + current_style.padding.right;
    const auto vertical_padding = current_style.padding.top + current_style.padding.bottom;
    const auto available_content_width =
        constrained_available(input.available_width - horizontal_padding);
    const auto available_content_height =
        constrained_available(input.available_height - vertical_padding);

    auto source = effective_source_rect();
    auto natural_width = source ? finite_non_negative(source->width) : 0.0F;
    auto natural_height = source ? finite_non_negative(source->height) : 0.0F;
    const auto has_aspect = natural_width > 0.0F && natural_height > 0.0F;
    const auto aspect = has_aspect ? natural_width / natural_height : 1.0F;

    auto width = natural_width;
    auto height = natural_height;
    if (input.width_mode == layout::MeasureMode::Exactly &&
        input.height_mode == layout::MeasureMode::Exactly) {
        width = available_content_width;
        height = available_content_height;
    } else if (input.width_mode == layout::MeasureMode::Exactly) {
        width = available_content_width;
        height = has_aspect ? width / aspect
                            : measured_axis(height, input.height_mode, available_content_height);
        if (input.height_mode == layout::MeasureMode::AtMost) {
            height = std::min(height, available_content_height);
        }
    } else if (input.height_mode == layout::MeasureMode::Exactly) {
        height = available_content_height;
        width = has_aspect ? height * aspect
                           : measured_axis(width, input.width_mode, available_content_width);
        if (input.width_mode == layout::MeasureMode::AtMost) {
            width = std::min(width, available_content_width);
        }
    } else if (has_aspect && input.width_mode == layout::MeasureMode::AtMost &&
               input.height_mode == layout::MeasureMode::AtMost) {
        const auto scale = std::min({1.0F, available_content_width / natural_width,
                                     available_content_height / natural_height});
        width = natural_width * scale;
        height = natural_height * scale;
    } else if (has_aspect && input.width_mode == layout::MeasureMode::AtMost) {
        width = std::min(natural_width, available_content_width);
        height = width / aspect;
    } else if (has_aspect && input.height_mode == layout::MeasureMode::AtMost) {
        height = std::min(natural_height, available_content_height);
        width = height * aspect;
    } else {
        width = measured_axis(width, input.width_mode, available_content_width);
        height = measured_axis(height, input.height_mode, available_content_height);
    }

    return layout::Size{
        std::max(finite_non_negative(width) + horizontal_padding, current_style.min_width),
        std::max(finite_non_negative(height) + vertical_padding, current_style.min_height)};
}

layout::Rect Image::image_content_rect(layout::Rect absolute_frame) const noexcept {
    const auto padding = style_storage().padding;
    absolute_frame.x += padding.left;
    absolute_frame.y += padding.top;
    absolute_frame.width = std::max(0.0F, absolute_frame.width - padding.left - padding.right);
    absolute_frame.height = std::max(0.0F, absolute_frame.height - padding.top - padding.bottom);
    return absolute_frame;
}

std::optional<layout::Rect> Image::effective_source_rect() const noexcept {
    if (!source_.valid()) {
        return std::nullopt;
    }

    const auto full_source = layout::Rect{0.0F, 0.0F, static_cast<float>(source_.width),
                                          static_cast<float>(source_.height)};
    auto source = source_rect_.value_or(full_source);
    const auto left = std::clamp(source.x, 0.0F, full_source.width);
    const auto top = std::clamp(source.y, 0.0F, full_source.height);
    const auto right = std::clamp(source.x + source.width, 0.0F, full_source.width);
    const auto bottom = std::clamp(source.y + source.height, 0.0F, full_source.height);
    if (right <= left || bottom <= top) {
        return std::nullopt;
    }

    return layout::Rect{left, top, right - left, bottom - top};
}

layout::Rect Image::destination_rect_for(layout::Rect content, layout::Rect source) const noexcept {
    if (object_fit_ == ImageFit::Fill || source.width <= 0.0F || source.height <= 0.0F) {
        return content;
    }

    const auto scale_x = content.width / source.width;
    const auto scale_y = content.height / source.height;
    auto width = source.width;
    auto height = source.height;
    switch (object_fit_) {
    case ImageFit::Contain: {
        const auto scale = std::min(scale_x, scale_y);
        width = source.width * scale;
        height = source.height * scale;
        break;
    }
    case ImageFit::Cover: {
        const auto scale = std::max(scale_x, scale_y);
        width = source.width * scale;
        height = source.height * scale;
        break;
    }
    case ImageFit::ScaleDown: {
        const auto scale = std::min({1.0F, scale_x, scale_y});
        width = source.width * scale;
        height = source.height * scale;
        break;
    }
    case ImageFit::None:
        break;
    case ImageFit::Fill:
        return content;
    }

    return layout::Rect{content.x + (content.width - width) * object_position_x_,
                        content.y + (content.height - height) * object_position_y_, width, height};
}

void Image::on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const {
    if (style_storage().background.alpha != 0U || style_storage().border_color.alpha != 0U ||
        style_storage().shadow_visible) {
        style::paint_rectangle(context, absolute_frame,
                               style::rectangle_style_from(style_storage(),
                                                           style_storage().background,
                                                           style_storage().border_color));
    }

    const auto source = effective_source_rect();
    const auto content = image_content_rect(absolute_frame);
    if (!source || !layout::is_visible_rect(content) || image_opacity_ <= 0.0F) {
        return;
    }

    const auto destination = destination_rect_for(content, *source);
    if (!layout::is_visible_rect(destination)) {
        return;
    }

    context.push_clip(content);
    context.draw_image(source_.resource_id,
                       rendering::RenderImageOptions{.destination = destination,
                                                     .source = *source,
                                                     .opacity = image_opacity_});
    context.pop_clip();
}

} // namespace winelement::controls
