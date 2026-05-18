#include <winelement/animation.hpp>
#include <winelement/controls.hpp>
#include <winelement/elements.hpp>
#include <winelement/elements/all_icons.hpp>
#include <winelement/layout.hpp>
#include <winelement/platform.hpp>
#include <winelement/rendering.hpp>
#include <winelement/style.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace winelement;

constexpr auto canvas_width = 1440.0F;
constexpr auto canvas_height = 4200.0F;

[[nodiscard]] rendering::Transform2D scale_transform(float scale) noexcept {
    return rendering::Transform2D{.m11 = scale, .m22 = scale};
}

[[nodiscard]] rendering::Transform2D rotate_transform(float degrees) noexcept {
    const auto radians = degrees * 3.14159265358979323846F / 180.0F;
    const auto c = std::cos(radians);
    const auto s = std::sin(radians);
    return rendering::Transform2D{.m11 = c, .m12 = s, .m21 = -s, .m22 = c};
}

[[nodiscard]] rendering::Transform2D skew_transform(float x_shear) noexcept {
    return rendering::Transform2D{.m11 = 1.0F, .m21 = x_shear, .m22 = 1.0F};
}

void configure_card(elements::UIElement& element, float width = 320.0F) {
    element.configure_layout([width](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(width))
            .set_min_height(layout::Length::points(72.0F))
            .set_flex_shrink(0.0F)
            .set_padding(layout::Edge::All, layout::Length::points(12.0F));
    });
}

void configure_row(controls::StackPanel& row) {
    row.set_orientation(controls::Orientation::Horizontal)
        .set_gap(12.0F)
        .set_wrap(layout::Wrap::Wrap)
        .set_align_items(layout::Align::Center);
    row.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_gap(layout::Gutter::Row, layout::Length::points(12.0F))
            .set_flex_shrink(0.0F);
    });
}

controls::StackPanel& add_section(controls::StackPanel& root, std::string_view title) {
    auto& frame = root.append_new_child<controls::Border>();
    frame.set_title(title).set_shadow_preset(controls::BorderShadow::Light);
    frame.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_flex_shrink(0.0F)
            .set_padding(layout::Edge::All, layout::Length::points(16.0F));
    });

    auto& stack = frame.append_new_child<controls::StackPanel>();
    stack.set_gap(14.0F);
    stack.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(0.0F);
    });

    auto& heading = stack.append_new_child<controls::Text>();
    heading.set_text(title).set_size(controls::TextSize::Large);
    heading.configure_layout([](layout::LayoutElement& item) { item.set_flex_shrink(0.0F); });
    return stack;
}

controls::Text& add_label(controls::StackPanel& parent, std::string_view text) {
    auto& label = parent.append_new_child<controls::Text>();
    label.set_text(text).set_type(controls::TextType::Info).set_size(controls::TextSize::Small);
    label.configure_layout([](layout::LayoutElement& item) { item.set_flex_shrink(0.0F); });
    return label;
}

controls::StackPanel& add_demo_group(controls::StackPanel& parent, std::string_view title) {
    auto& group = parent.append_new_child<controls::Border>();
    group.set_title(title).set_shadow_preset(controls::BorderShadow::Light);
    group.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_flex_shrink(0.0F)
            .set_padding(layout::Edge::All, layout::Length::points(12.0F));
    });

    auto& stack = group.append_new_child<controls::StackPanel>();
    stack.set_gap(10.0F);
    stack.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(0.0F);
    });
    return stack;
}

[[nodiscard]] std::string message_type_label(controls::MessageType type) {
    switch (type) {
    case controls::MessageType::Primary:
        return "Primary";
    case controls::MessageType::Success:
        return "Success";
    case controls::MessageType::Warning:
        return "Warning";
    case controls::MessageType::Info:
        return "Info";
    case controls::MessageType::Error:
        return "Error";
    }
    return "Info";
}

[[nodiscard]] std::string easing_curve_label(animation::EasingCurve curve) {
    switch (curve) {
    case animation::EasingCurve::Linear:
        return "Linear";
    case animation::EasingCurve::StepStart:
        return "Step start";
    case animation::EasingCurve::StepEnd:
        return "Step end";
    case animation::EasingCurve::EaseInSine:
        return "Ease in sine";
    case animation::EasingCurve::EaseOutSine:
        return "Ease out sine";
    case animation::EasingCurve::EaseInOutSine:
        return "Ease in-out sine";
    case animation::EasingCurve::EaseInQuad:
        return "Ease in quad";
    case animation::EasingCurve::EaseOutQuad:
        return "Ease out quad";
    case animation::EasingCurve::EaseInOutQuad:
        return "Ease in-out quad";
    case animation::EasingCurve::EaseInCubic:
        return "Ease in cubic";
    case animation::EasingCurve::EaseOutCubic:
        return "Ease out cubic";
    case animation::EasingCurve::EaseInOutCubic:
        return "Ease in-out cubic";
    case animation::EasingCurve::EaseOutBack:
        return "Ease out back";
    case animation::EasingCurve::EaseInOutBack:
        return "Ease in-out back";
    }
    return "Linear";
}

controls::Button& add_button(controls::StackPanel& row, std::string_view text,
                             controls::ButtonType type) {
    auto& button = row.append_new_child<controls::Button>();
    button.set_text(text).set_type(type);
    button.configure_layout([](layout::LayoutElement& item) { item.set_flex_shrink(0.0F); });
    return button;
}

std::vector<controls::SelectOption> select_options() {
    return {controls::SelectOption{.label = "Primary", .value = "primary", .group = "Semantic"},
            controls::SelectOption{.label = "Success", .value = "success", .group = "Semantic"},
            controls::SelectOption{.label = "Warning", .value = "warning", .group = "Semantic"},
            controls::SelectOption{.label = "Danger", .value = "danger", .group = "Semantic"},
            controls::SelectOption{.label = "Disabled", .value = "disabled", .disabled = true},
            controls::SelectOption{.label = "Info", .value = "info", .group = "Neutral"}};
}

void add_button_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Button: Element Plus semantic styles and states");
    auto& type_row = section.append_new_child<controls::StackPanel>();
    configure_row(type_row);
    add_button(type_row, "Default", controls::ButtonType::Default);
    add_button(type_row, "Primary", controls::ButtonType::Primary);
    add_button(type_row, "Success", controls::ButtonType::Success);
    add_button(type_row, "Warning", controls::ButtonType::Warning);
    add_button(type_row, "Danger", controls::ButtonType::Danger);
    add_button(type_row, "Info", controls::ButtonType::Info);
    add_button(type_row, "Text", controls::ButtonType::Text).set_text_variant(true);

    auto& plain_row = section.append_new_child<controls::StackPanel>();
    configure_row(plain_row);
    add_button(plain_row, "Plain", controls::ButtonType::Default).set_plain(true);
    add_button(plain_row, "Primary", controls::ButtonType::Primary).set_plain(true);
    add_button(plain_row, "Success", controls::ButtonType::Success).set_plain(true);
    add_button(plain_row, "Warning", controls::ButtonType::Warning).set_plain(true);
    add_button(plain_row, "Danger", controls::ButtonType::Danger).set_plain(true);
    add_button(plain_row, "Info", controls::ButtonType::Info).set_plain(true);

    auto& variant_row = section.append_new_child<controls::StackPanel>();
    configure_row(variant_row);
    add_button(variant_row, "Large", controls::ButtonType::Primary)
        .set_size(controls::ButtonSize::Large);
    add_button(variant_row, "Small", controls::ButtonType::Default)
        .set_size(controls::ButtonSize::Small);
    add_button(variant_row, "Round", controls::ButtonType::Warning).set_round(true);
    add_button(variant_row, "", controls::ButtonType::Primary)
        .set_circle(true)
        .set_icon_paths(elements::icons::Search);
    add_button(variant_row, "Dashed", controls::ButtonType::Info).set_dashed(true);
    add_button(variant_row, "Link", controls::ButtonType::Text).set_link_variant(true);
    add_button(variant_row, "Loading", controls::ButtonType::Primary).set_loading(true);
    add_button(variant_row, "Disabled", controls::ButtonType::Default).set_disabled(true);
    add_button(variant_row, "Disabled", controls::ButtonType::Primary).set_disabled(true);
    add_button(variant_row, "Dark", controls::ButtonType::Default).set_dark_mode(true);
    add_button(variant_row, "Custom", controls::ButtonType::Default)
        .set_custom_color(rendering::Color::rgba(91, 62, 196));
}

void add_input_select_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Input and select: all sizes and status styles");
    auto& input_row = section.append_new_child<controls::StackPanel>();
    configure_row(input_row);
    for (const auto size :
         {controls::InputSize::Large, controls::InputSize::Default, controls::InputSize::Small}) {
        auto& input = input_row.append_new_child<controls::Input>();
        input.set_text(size == controls::InputSize::Large ? "Large input" : "Input")
            .set_placeholder("Placeholder")
            .set_size(size)
            .set_clearable(true)
            .set_prefix_text("$")
            .set_suffix_text("ms")
            .set_show_word_limit(true)
            .set_max_length(32U);
        input.configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(280.0F)).set_flex_shrink(0.0F);
        });
    }

    auto& status_row = section.append_new_child<controls::StackPanel>();
    configure_row(status_row);
    for (const auto status : {controls::InputStatus::Default, controls::InputStatus::Success,
                              controls::InputStatus::Warning, controls::InputStatus::Error}) {
        auto& input = status_row.append_new_child<controls::Input>();
        input.set_text("Validated")
            .set_status(status)
            .set_prepend_text("https://")
            .set_append_text(".dev");
        input.configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(340.0F)).set_flex_shrink(0.0F);
        });
    }

    auto& text_area = section.append_new_child<controls::Input>();
    text_area.set_type(controls::InputType::Textarea)
        .set_rows(3U)
        .set_autosize(true)
        .set_text("Textarea with wrapping and autosize enabled.")
        .set_status(controls::InputStatus::Success);
    text_area.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(620.0F)).set_flex_shrink(0.0F);
    });

    auto& select_row = section.append_new_child<controls::StackPanel>();
    configure_row(select_row);
    auto options = select_options();
    for (const auto size : {controls::SelectSize::Large, controls::SelectSize::Default,
                            controls::SelectSize::Small}) {
        auto& select = select_row.append_new_child<controls::Select>();
        select.set_options(options).set_selected_index(1U).set_size(size).set_clearable(true);
        select.configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(260.0F)).set_flex_shrink(0.0F);
        });
    }
    auto& multi = select_row.append_new_child<controls::Select>();
    multi.set_options(options)
        .set_multiple(true)
        .set_tags_visible(true)
        .set_selected_indices({0U, 2U, 3U})
        .set_filterable(true)
        .set_filter_text("a");
    multi.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(360.0F)).set_flex_shrink(0.0F);
    });
}

void add_choice_scroll_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Choice controls, scrollable content and data");
    auto& row = section.append_new_child<controls::StackPanel>();
    configure_row(row);

    auto group = std::make_shared<controls::RadioGroupContext>();
    group->set_value("b");
    for (const auto value : {std::string_view{"a"}, std::string_view{"b"}, std::string_view{"c"}}) {
        auto& radio = row.append_new_child<controls::Radio>();
        radio.set_text(std::string{"Radio "} + std::string(value))
            .set_value(value)
            .set_group(group);
    }

    row.append_new_child<controls::Switch>()
        .set_checked(true)
        .set_active_text("On")
        .set_inactive_text("Off")
        .set_size(controls::SwitchSize::Large);
    row.append_new_child<controls::Switch>().set_loading(true).set_size(
        controls::SwitchSize::Default);
    row.append_new_child<controls::Switch>().set_disabled(true).set_size(
        controls::SwitchSize::Small);

    auto& scroll_row = section.append_new_child<controls::StackPanel>();
    configure_row(scroll_row);

    auto& vertical_group = scroll_row.append_new_child<controls::StackPanel>();
    vertical_group.set_gap(8.0F);
    vertical_group.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(390.0F)).set_flex_shrink(0.0F);
    });
    add_label(vertical_group, "Vertical content viewport");

    auto& vertical_line = vertical_group.append_new_child<controls::StackPanel>();
    vertical_line.set_orientation(controls::Orientation::Horizontal)
        .set_gap(8.0F)
        .set_align_items(layout::Align::FlexStart);
    vertical_line.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(0.0F);
    });

    auto& vertical_viewport = vertical_line.append_new_child<controls::Panel>();
    vertical_viewport.set_background(rendering::Color::rgba(255, 255, 255));
    vertical_viewport.set_border(rendering::Color::rgba(220, 223, 230), 1.0F);
    vertical_viewport.set_corner_radius(rendering::CornerRadius::uniform(4.0F));
    vertical_viewport.set_overflow(layout::Overflow::Hidden);
    vertical_viewport.set_viewport(layout::Rect{0.0F, 0.0F, 342.0F, 156.0F});
    vertical_viewport.set_scroll_offset(layout::Point{0.0F, 96.0F});
    vertical_viewport.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(342.0F), layout::Length::points(156.0F))
            .set_flex_shrink(0.0F)
            .set_padding(layout::Edge::All, layout::Length::points(8.0F));
    });
    auto& vertical_content = vertical_viewport.append_new_child<controls::StackPanel>();
    vertical_content.set_gap(6.0F);
    vertical_content.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(310.0F)).set_flex_shrink(0.0F);
    });
    for (auto index = 1; index <= 12; ++index) {
        auto& item = vertical_content.append_new_child<controls::Border>();
        item.set_preset(index % 3 == 0   ? controls::BorderPreset::Primary
                        : index % 3 == 1 ? controls::BorderPreset::Plain
                                         : controls::BorderPreset::Info);
        item.configure_layout([](layout::LayoutElement& cell) {
            cell.set_width(layout::Length::percent(100.0F))
                .set_min_height(layout::Length::points(32.0F))
                .set_padding(layout::Edge::All, layout::Length::points(6.0F))
                .set_flex_shrink(0.0F);
        });
        item.append_new_child<controls::Text>().set_text("Scrollable row " + std::to_string(index));
    }

    auto& vertical = vertical_line.append_new_child<controls::Scrollbar>();
    vertical.set_orientation(controls::ScrollbarOrientation::Vertical)
        .set_visibility_mode(controls::ScrollbarVisibility::Always)
        .set_range(0.0F, 384.0F, 156.0F)
        .set_value(96.0F);
    vertical.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(14.0F), layout::Length::points(156.0F))
            .set_flex_shrink(0.0F);
    });

    auto& horizontal_group = scroll_row.append_new_child<controls::StackPanel>();
    horizontal_group.set_gap(8.0F);
    horizontal_group.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(560.0F)).set_flex_shrink(0.0F);
    });
    add_label(horizontal_group, "Horizontal content viewport");

    auto& horizontal_viewport = horizontal_group.append_new_child<controls::Panel>();
    horizontal_viewport.set_background(rendering::Color::rgba(255, 255, 255));
    horizontal_viewport.set_border(rendering::Color::rgba(220, 223, 230), 1.0F);
    horizontal_viewport.set_corner_radius(rendering::CornerRadius::uniform(4.0F));
    horizontal_viewport.set_overflow(layout::Overflow::Hidden);
    horizontal_viewport.set_viewport(layout::Rect{0.0F, 0.0F, 540.0F, 120.0F});
    horizontal_viewport.set_scroll_offset(layout::Point{180.0F, 0.0F});
    horizontal_viewport.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(540.0F), layout::Length::points(120.0F))
            .set_flex_shrink(0.0F)
            .set_padding(layout::Edge::All, layout::Length::points(8.0F));
    });
    auto& horizontal_content = horizontal_viewport.append_new_child<controls::StackPanel>();
    horizontal_content.set_orientation(controls::Orientation::Horizontal)
        .set_gap(8.0F)
        .set_wrap(layout::Wrap::NoWrap)
        .set_align_items(layout::Align::Stretch);
    horizontal_content.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(860.0F)).set_flex_shrink(0.0F);
    });
    for (auto index = 1; index <= 8; ++index) {
        auto& column = horizontal_content.append_new_child<controls::Border>();
        column.set_title("Column " + std::to_string(index))
            .set_preset(controls::BorderPreset::Info);
        column.configure_layout([](layout::LayoutElement& cell) {
            cell.set_width(layout::Length::points(96.0F))
                .set_min_height(layout::Length::points(96.0F))
                .set_padding(layout::Edge::All, layout::Length::points(8.0F))
                .set_flex_shrink(0.0F);
        });
        column.append_new_child<controls::Text>().set_text("Data " + std::to_string(index));
    }

    auto& horizontal = horizontal_group.append_new_child<controls::Scrollbar>();
    horizontal.set_orientation(controls::ScrollbarOrientation::Horizontal)
        .set_visibility_mode(controls::ScrollbarVisibility::Always)
        .set_range(0.0F, 860.0F, 540.0F)
        .set_value(180.0F);
    horizontal.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(540.0F), layout::Length::points(14.0F))
            .set_flex_shrink(0.0F);
    });

    auto& items = section.append_new_child<controls::ItemsControl>();
    items.set_items({"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta"})
        .set_selection_mode(controls::ItemsControl::SelectionMode::Multiple)
        .set_selected_indices({1U, 3U})
        .set_virtualized(true)
        .set_virtualization_window(24.0F, 120.0F, 28.0F, 1U);
    items.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(420.0F))
            .set_min_height(layout::Length::points(150.0F))
            .set_flex_shrink(0.0F);
    });
}

void add_structure_text_path_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Panels, borders, text, paths and menus");
    auto& panels = section.append_new_child<controls::StackPanel>();
    configure_row(panels);
    for (const auto preset : {controls::BorderPreset::Plain, controls::BorderPreset::Primary,
                              controls::BorderPreset::Success, controls::BorderPreset::Warning,
                              controls::BorderPreset::Danger, controls::BorderPreset::Info}) {
        auto& border = panels.append_new_child<controls::Border>();
        border.set_preset(preset).set_shadow_preset(controls::BorderShadow::Base);
        configure_card(border, 180.0F);
        border.append_new_child<controls::Text>().set_text("Border preset");
    }

    auto& text_row = section.append_new_child<controls::StackPanel>();
    configure_row(text_row);
    for (const auto type :
         {controls::TextType::Primary, controls::TextType::Success, controls::TextType::Warning,
          controls::TextType::Danger, controls::TextType::Info}) {
        auto& text = text_row.append_new_child<controls::Text>();
        text.set_text("Text style").set_type(type).set_size(controls::TextSize::Large);
        text.configure_layout([](layout::LayoutElement& item) { item.set_flex_shrink(0.0F); });
    }

    auto& path_row = section.append_new_child<controls::StackPanel>();
    configure_row(path_row);
    const auto star_path =
        "M 50 4 L 61 36 L 95 36 L 67 56 L 78 90 L 50 69 L 22 90 L 33 56 L 5 36 L 39 36 Z";
    for (const auto stretch :
         {controls::PathStretch::None, controls::PathStretch::Fill, controls::PathStretch::Uniform,
          controls::PathStretch::UniformToFill}) {
        auto& path = path_row.append_new_child<controls::Path>();
        path.set_data(star_path)
            .set_stretch(stretch)
            .set_fill(rendering::Color::rgba(64, 158, 255, 96))
            .set_stroke(rendering::Color::rgba(64, 158, 255))
            .set_stroke_width(2.0F);
        path.configure_layout([](layout::LayoutElement& item) {
            item.set_size(layout::Length::points(96.0F), layout::Length::points(96.0F))
                .set_flex_shrink(0.0F);
        });
    }

    auto& menu = section.append_new_child<controls::ContextMenu>();
    menu.set_items({controls::ContextMenuItem{.text = "Open", .id = "open", .icon_name = "Folder"},
                    controls::ContextMenuItem{.separator = true},
                    controls::ContextMenuItem{
                        .text = "Checked", .id = "checked", .checkable = true, .checked = true},
                    controls::ContextMenuItem{
                        .text = "Submenu",
                        .id = "submenu",
                        .submenu = {controls::ContextMenuItem{.text = "Nested", .id = "nested"}}}});
    menu.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(260.0F), layout::Length::points(160.0F))
            .set_flex_shrink(0.0F);
    });
}

void add_feedback_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Feedback components");

    auto& messages = add_demo_group(section, "Message");
    auto& message_row = messages.append_new_child<controls::StackPanel>();
    configure_row(message_row);
    for (const auto type : {controls::MessageType::Primary, controls::MessageType::Success,
                            controls::MessageType::Warning, controls::MessageType::Info,
                            controls::MessageType::Error}) {
        const auto label = message_type_label(type);
        auto& message = message_row.append_new_child<controls::Message>();
        message.set_text(label + " message").set_type(type).set_show_close(true);
        message.configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(260.0F))
                .set_min_height(layout::Length::points(44.0F))
                .set_flex_shrink(0.0F);
        });
    }

    auto& message_boxes = add_demo_group(section, "MessageBox");
    auto& box_row = message_boxes.append_new_child<controls::StackPanel>();
    configure_row(box_row);
    box_row.append_new_child<controls::MessageBox>()
        .set_title("Alert")
        .set_message("Simple notification with one primary action.")
        .set_kind(controls::MessageBoxKind::Alert)
        .set_type(controls::MessageType::Info)
        .set_show_cancel_button(false)
        .configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(400.0F))
                .set_min_height(layout::Length::points(150.0F))
                .set_flex_shrink(0.0F);
        });
    box_row.append_new_child<controls::MessageBox>()
        .set_title("Confirm")
        .set_message("Confirm and cancel actions with close distinction.")
        .set_kind(controls::MessageBoxKind::Confirm)
        .set_type(controls::MessageType::Warning)
        .set_distinguish_cancel_and_close(true)
        .set_draggable(true)
        .configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(400.0F))
                .set_min_height(layout::Length::points(150.0F))
                .set_flex_shrink(0.0F);
        });
    box_row.append_new_child<controls::MessageBox>()
        .set_title("Prompt")
        .set_message("Input validation, loading confirm and centered layout.")
        .set_kind(controls::MessageBoxKind::Prompt)
        .set_type(controls::MessageType::Success)
        .set_input_placeholder("Project name")
        .set_input_text("WinElement")
        .set_input_error_message("Project name is required")
        .set_confirm_loading(true)
        .set_center(true)
        .set_content_builder([](controls::StackPanel& content) {
            content.append_new_child<controls::Text>()
                .set_text("Custom content builder")
                .set_type(controls::TextType::Info)
                .set_size(controls::TextSize::Small);
        })
        .configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(400.0F))
                .set_min_height(layout::Length::points(220.0F))
                .set_flex_shrink(0.0F);
        });

    auto& dialogs = add_demo_group(section, "Dialog");
    auto& dialog_row = dialogs.append_new_child<controls::StackPanel>();
    configure_row(dialog_row);
    dialog_row.append_new_child<controls::Dialog>()
        .set_title("Dialog")
        .set_body("Modal surface with header, body, footer, close and confirm actions.")
        .set_show_cancel_button(true)
        .set_draggable(true)
        .configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(420.0F))
                .set_min_height(layout::Length::points(210.0F))
                .set_flex_shrink(0.0F);
        });
    dialog_row.append_new_child<controls::Dialog>()
        .set_title("Compact dialog")
        .set_body("A compact dialog variant without a cancel button.")
        .set_show_cancel_button(false)
        .set_draggable(false)
        .configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(420.0F))
                .set_min_height(layout::Length::points(190.0F))
                .set_flex_shrink(0.0F);
        });

    auto& loading_group = add_demo_group(section, "Loading");
    auto& loading_row = loading_group.append_new_child<controls::StackPanel>();
    configure_row(loading_row);
    loading_row.append_new_child<controls::Loading>()
        .set_text("Loading service")
        .set_active(true)
        .set_spinner_color(rendering::Color::rgba(64, 158, 255))
        .configure_layout([](layout::LayoutElement& item) {
            item.set_size(layout::Length::points(260.0F), layout::Length::points(120.0F))
                .set_flex_shrink(0.0F);
        });
    loading_row.append_new_child<controls::Loading>()
        .set_text("Custom color")
        .set_active(true)
        .set_background(rendering::Color::rgba(240, 249, 235, 224))
        .set_spinner_color(rendering::Color::rgba(103, 194, 58))
        .configure_layout([](layout::LayoutElement& item) {
            item.set_size(layout::Length::points(260.0F), layout::Length::points(120.0F))
                .set_flex_shrink(0.0F);
        });
}

void add_animation_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Animations");

    auto& effects = add_demo_group(section, "Transforms and shadows");
    auto& transform_row = effects.append_new_child<controls::StackPanel>();
    configure_row(transform_row);
    const std::array<std::pair<std::string_view, rendering::Transform2D>, 4U> transforms{{
        {"translate", rendering::Transform2D::translation(16.0F, 4.0F)},
        {"scale", scale_transform(1.06F)},
        {"rotate", rotate_transform(-4.0F)},
        {"skew", skew_transform(0.12F)},
    }};
    for (const auto& [name, transform] : transforms) {
        auto& card = transform_row.append_new_child<controls::Panel>();
        card.set_title(name).set_background(rendering::Color::rgba(255, 255, 255));
        card.set_corner_radius(rendering::CornerRadius::uniform(4.0F));
        card.set_shadow(rendering::ShadowStyle{.color = rendering::Color::rgba(0, 0, 0, 36),
                                               .offset = layout::Point{0.0F, 8.0F},
                                               .blur_radius = 18.0F,
                                               .spread = 1.0F});
        card.set_render_transform(transform);
        configure_card(card, 220.0F);
        card.append_new_child<controls::Text>().set_text(name);
    }

    auto& shadow_row = effects.append_new_child<controls::StackPanel>();
    configure_row(shadow_row);
    for (const auto shadow : {controls::BorderShadow::None, controls::BorderShadow::Light,
                              controls::BorderShadow::Base, controls::BorderShadow::Dark}) {
        auto& border = shadow_row.append_new_child<controls::Border>();
        border.set_title("Shadow").set_shadow_preset(shadow).set_preset(
            controls::BorderPreset::Info);
        configure_card(border, 220.0F);
        border.append_new_child<controls::Text>().set_text("Shadow preset");
    }

    auto& easing_group = add_demo_group(section, "Easing curves");
    const std::array<animation::EasingCurve, 14U> curves{
        animation::EasingCurve::Linear,        animation::EasingCurve::StepStart,
        animation::EasingCurve::StepEnd,       animation::EasingCurve::EaseInSine,
        animation::EasingCurve::EaseOutSine,   animation::EasingCurve::EaseInOutSine,
        animation::EasingCurve::EaseInQuad,    animation::EasingCurve::EaseOutQuad,
        animation::EasingCurve::EaseInOutQuad, animation::EasingCurve::EaseInCubic,
        animation::EasingCurve::EaseOutCubic,  animation::EasingCurve::EaseInOutCubic,
        animation::EasingCurve::EaseOutBack,   animation::EasingCurve::EaseInOutBack};
    auto& curve_row = easing_group.append_new_child<controls::StackPanel>();
    configure_row(curve_row);
    for (const auto curve : curves) {
        const auto value = std::clamp(animation::apply_easing(curve, 0.55F), 0.0F, 1.0F);
        auto& card = curve_row.append_new_child<controls::StackPanel>();
        card.set_gap(6.0F);
        card.configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(240.0F)).set_flex_shrink(0.0F);
        });
        card.append_new_child<controls::Text>()
            .set_text(easing_curve_label(curve))
            .set_size(controls::TextSize::Small);
        auto& track = card.append_new_child<controls::Panel>();
        track.set_background(rendering::Color::rgba(245, 247, 250));
        track.set_corner_radius(rendering::CornerRadius::uniform(4.0F));
        track.configure_layout([](layout::LayoutElement& item) {
            item.set_size(layout::Length::points(220.0F), layout::Length::points(8.0F))
                .set_flex_shrink(0.0F);
        });
        auto& fill = track.append_new_child<controls::Panel>();
        fill.set_background(rendering::Color::rgba(64, 158, 255));
        fill.set_corner_radius(rendering::CornerRadius::uniform(4.0F));
        fill.configure_layout([value](layout::LayoutElement& item) {
            item.set_size(layout::Length::points(220.0F * value), layout::Length::points(8.0F))
                .set_flex_shrink(0.0F);
        });
    }

    auto& timeline_group = add_demo_group(section, "Timeline, keyframes and physics");
    auto& timing_row = timeline_group.append_new_child<controls::StackPanel>();
    configure_row(timing_row);
    for (const auto direction :
         {animation::PlaybackDirection::Normal, animation::PlaybackDirection::Reverse,
          animation::PlaybackDirection::Alternate,
          animation::PlaybackDirection::AlternateReverse}) {
        animation::Timeline timeline(
            animation::AnimationTiming{.duration = animation::AnimationDuration{1.0F},
                                       .iteration_count = 2.0F,
                                       .direction = direction,
                                       .fill_mode = animation::FillMode::Both,
                                       .easing = animation::EasingFunction::ease_out_cubic()});
        const auto sample = timeline.sample(animation::AnimationDuration{0.65F});
        auto& badge = timing_row.append_new_child<controls::Border>();
        badge.set_preset(controls::BorderPreset::Primary);
        configure_card(badge, 260.0F);
        badge.append_new_child<controls::Text>().set_text(
            "Direction progress " + std::to_string(static_cast<int>(sample.progress * 100.0F)) +
            "%");
    }

    auto& keyframe_row = timeline_group.append_new_child<controls::StackPanel>();
    configure_row(keyframe_row);
    animation::KeyframeTrack<float> opacity_track({
        animation::Keyframe<float>{.offset = 0.0F, .value = 0.0F},
        animation::Keyframe<float>{
            .offset = 0.4F, .value = 1.0F, .easing = animation::EasingFunction::ease_out_cubic()},
        animation::Keyframe<float>{.offset = 1.0F,
                                   .value = 0.35F,
                                   .easing = animation::EasingFunction::ease_in_out_cubic()},
    });
    auto& keyframe_card = keyframe_row.append_new_child<controls::Border>();
    keyframe_card.set_title("Keyframe track").set_preset(controls::BorderPreset::Success);
    configure_card(keyframe_card, 300.0F);
    keyframe_card.append_new_child<controls::Text>().set_text(
        "Sample " + std::to_string(static_cast<int>(opacity_track.sample(0.55F) * 100.0F)) + "%");

    animation::SpringSimulation spring(0.0F, 1.0F);
    animation::FrictionSimulation friction(0.0F, 720.0F);
    auto& spring_card = keyframe_row.append_new_child<controls::Border>();
    spring_card.set_title("Spring").set_preset(controls::BorderPreset::Warning);
    configure_card(spring_card, 260.0F);
    spring_card.append_new_child<controls::Text>().set_text(
        "Value " +
        std::to_string(
            static_cast<int>(spring.sample(animation::AnimationDuration{0.28F}).value * 100.0F)) +
        "%");
    auto& friction_card = keyframe_row.append_new_child<controls::Border>();
    friction_card.set_title("Friction").set_preset(controls::BorderPreset::Info);
    configure_card(friction_card, 260.0F);
    friction_card.append_new_child<controls::Text>().set_text(
        "Offset " + std::to_string(static_cast<int>(
                        friction.sample(animation::AnimationDuration{0.28F}).value)));

    auto& animated = section.append_new_child<controls::Button>();
    animated.set_text("Implicit property animation sample").set_type(controls::ButtonType::Primary);
    animated.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(360.0F)).set_flex_shrink(0.0F);
    });
    const auto opacity_property =
        core::make_property_metadata<float>("showcase.opacity", core::PropertyInvalidation::Paint);
    animated.set_property(opacity_property, 0.0F);
    animated.animate_property<float>(
        opacity_property, 1.0F,
        animation::AnimationTiming{.duration = animation::AnimationDuration{0.3F},
                                   .iteration_count = 2.0F,
                                   .direction = animation::PlaybackDirection::Alternate,
                                   .fill_mode = animation::FillMode::Both,
                                   .easing = animation::EasingFunction::ease_in_out_cubic()});
}

[[nodiscard]] std::unique_ptr<controls::StackPanel> build_showcase_tree() {
    auto root = std::make_unique<controls::StackPanel>();
    root->set_gap(18.0F);
    root->set_background(rendering::Color::rgba(245, 247, 250));
    root->set_overflow(layout::Overflow::Hidden);
    root->set_scroll_wheel_enabled(true);
    root->configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::percent(100.0F), layout::Length::percent(100.0F))
            .set_padding(layout::Edge::All, layout::Length::points(24.0F));
    });

    auto& title = root->append_new_child<controls::Text>();
    title.set_text("WinElement Controls Showcase")
        .set_size(controls::TextSize::Large)
        .set_type(controls::TextType::Primary);
    title.configure_layout([](layout::LayoutElement& item) { item.set_flex_shrink(0.0F); });

    add_button_section(*root);
    add_input_select_section(*root);
    add_choice_scroll_section(*root);
    add_structure_text_path_section(*root);
    add_feedback_section(*root);
    add_animation_section(*root);
    return root;
}

[[nodiscard]] std::size_t count_nodes(const rendering::RenderNode& node) noexcept {
    auto total = std::size_t{1U};
    for (const auto& child : node.children) {
        total += count_nodes(child);
    }
    return total;
}

[[nodiscard]] std::size_t count_commands(const rendering::RenderNode& node) noexcept {
    auto total = node.commands.command_count();
    for (const auto& child : node.children) {
        total += count_commands(child);
    }
    return total;
}

} // namespace

int run_headless_showcase() {
    auto engine = layout::LayoutEngine{};
    auto root = build_showcase_tree();
    root->bind_layout_tree(engine);
    root->calculate_layout(
        layout::LayoutConstraints{.width = canvas_width, .height = canvas_height});

    const auto now = animation::AnimationClockType::now();
    auto animation_active = false;
    for (auto step = 0; step < 4; ++step) {
        animation_active =
            root->tick_animations(now + std::chrono::milliseconds(80 * step)) || animation_active;
    }

    auto scene = rendering::RenderScene{};
    auto dirty = rendering::DirtyRegion{};
    root->commit_render_scene(scene, &dirty);

    const auto* scene_root = scene.root();
    const auto node_count = scene_root != nullptr ? count_nodes(*scene_root) : 0U;
    const auto command_count = scene_root != nullptr ? count_commands(*scene_root) : 0U;

    std::cout << "controls_showcase\n";
    std::cout << "  controls: panel border stack text button input select radio switch scrollbar "
                 "items path context-menu message message-box loading dialog\n";
    std::cout << "  styles: Element Plus semantic variants sizes status borders shadows text "
                 "states dark/custom\n";
    std::cout << "  scroll: vertical and horizontal viewports with clipped overflowing content\n";
    std::cout << "  feedback: message message-box dialog loading split into dedicated blocks\n";
    std::cout << "  animations: transforms shadows all easing curves timeline keyframes physics "
                 "implicit property\n";
    std::cout << "  render nodes: " << node_count << '\n';
    std::cout << "  render commands: " << command_count << '\n';
    std::cout << "  animation active during warmup: " << (animation_active ? "yes" : "no") << '\n';
    std::cout << "  dirty empty: " << (dirty.empty() ? "yes" : "no") << '\n';
    return command_count > 0U ? 0 : 1;
}

int run_window_showcase() {
    platform::Application application;
    platform::Window window(platform::WindowOptions{
        .title = L"WinElement Controls Showcase", .width = 1320, .height = 920});
    window.set_content(build_showcase_tree());
    window.show();
    return application.run();
}

int main(int argc, char** argv) {
    for (auto index = 1; index < argc; ++index) {
        if (std::string_view{argv[index]} == "--headless") {
            return run_headless_showcase();
        }
    }
    return run_window_showcase();
}
