#include <winelement/animation.hpp>
#include <winelement/controls.hpp>
#include <winelement/elements.hpp>
#include <winelement/layout.hpp>
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
constexpr auto canvas_height = 2200.0F;

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
    auto& section = add_section(root, "Buttons: all semantic types, sizes, variants");
    auto& type_row = section.append_new_child<controls::StackPanel>();
    configure_row(type_row);
    add_button(type_row, "Default", controls::ButtonType::Default);
    add_button(type_row, "Primary", controls::ButtonType::Primary);
    add_button(type_row, "Success", controls::ButtonType::Success);
    add_button(type_row, "Warning", controls::ButtonType::Warning);
    add_button(type_row, "Danger", controls::ButtonType::Danger);
    add_button(type_row, "Info", controls::ButtonType::Info);
    add_button(type_row, "Text", controls::ButtonType::Text).set_text_variant(true);

    auto& variant_row = section.append_new_child<controls::StackPanel>();
    configure_row(variant_row);
    add_button(variant_row, "Large", controls::ButtonType::Primary).set_size(controls::ButtonSize::Large);
    add_button(variant_row, "Small", controls::ButtonType::Default).set_size(controls::ButtonSize::Small);
    add_button(variant_row, "Plain", controls::ButtonType::Success).set_plain(true);
    add_button(variant_row, "Round", controls::ButtonType::Warning).set_round(true);
    add_button(variant_row, "Circle", controls::ButtonType::Danger).set_circle(true);
    add_button(variant_row, "Dashed", controls::ButtonType::Info).set_dashed(true);
    add_button(variant_row, "Link", controls::ButtonType::Text).set_link_variant(true);
    add_button(variant_row, "Loading", controls::ButtonType::Primary).set_loading(true);
    add_button(variant_row, "Dark", controls::ButtonType::Default).set_dark_mode(true);
    add_button(variant_row, "Custom", controls::ButtonType::Default)
        .set_custom_color(rendering::Color::rgba(91, 62, 196));
}

void add_input_select_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Input and select: all sizes and status styles");
    auto& input_row = section.append_new_child<controls::StackPanel>();
    configure_row(input_row);
    for (const auto size : {controls::InputSize::Large, controls::InputSize::Default,
                            controls::InputSize::Small}) {
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
    auto& section = add_section(root, "Choice, scrolling and data controls");
    auto& row = section.append_new_child<controls::StackPanel>();
    configure_row(row);

    auto group = std::make_shared<controls::RadioGroupContext>();
    group->set_value("b");
    for (const auto value : {std::string_view{"a"}, std::string_view{"b"},
                             std::string_view{"c"}}) {
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
    row.append_new_child<controls::Switch>().set_loading(true).set_size(controls::SwitchSize::Default);
    row.append_new_child<controls::Switch>().set_disabled(true).set_size(controls::SwitchSize::Small);

    auto& vertical = row.append_new_child<controls::Scrollbar>();
    vertical.set_orientation(controls::ScrollbarOrientation::Vertical)
        .set_visibility_mode(controls::ScrollbarVisibility::Always)
        .set_range(0.0F, 1000.0F, 180.0F)
        .set_value(320.0F);
    vertical.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(20.0F), layout::Length::points(160.0F))
            .set_flex_shrink(0.0F);
    });

    auto& horizontal = row.append_new_child<controls::Scrollbar>();
    horizontal.set_orientation(controls::ScrollbarOrientation::Horizontal)
        .set_visibility_mode(controls::ScrollbarVisibility::Auto)
        .set_range(0.0F, 500.0F, 120.0F)
        .set_value(200.0F);
    horizontal.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(260.0F), layout::Length::points(20.0F))
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
    for (const auto type : {controls::TextType::Primary, controls::TextType::Success,
                            controls::TextType::Warning, controls::TextType::Danger,
                            controls::TextType::Info}) {
        auto& text = text_row.append_new_child<controls::Text>();
        text.set_text("Text style").set_type(type).set_size(controls::TextSize::Large);
        text.configure_layout([](layout::LayoutElement& item) { item.set_flex_shrink(0.0F); });
    }

    auto& path_row = section.append_new_child<controls::StackPanel>();
    configure_row(path_row);
    const auto star_path = "M 50 4 L 61 36 L 95 36 L 67 56 L 78 90 L 50 69 L 22 90 L 33 56 L 5 36 L 39 36 Z";
    for (const auto stretch : {controls::PathStretch::None, controls::PathStretch::Fill,
                               controls::PathStretch::Uniform,
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
                    controls::ContextMenuItem{.text = "Checked", .id = "checked", .checkable = true, .checked = true},
                    controls::ContextMenuItem{.text = "Submenu", .id = "submenu", .submenu = {controls::ContextMenuItem{.text = "Nested", .id = "nested"}}}});
    menu.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(260.0F), layout::Length::points(160.0F))
            .set_flex_shrink(0.0F);
    });
}

void add_feedback_effects_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Feedback, transforms, animations and shadows");
    auto& effects = section.append_new_child<controls::StackPanel>();
    configure_row(effects);

    const std::array<std::pair<std::string_view, rendering::Transform2D>, 4U> transforms{{
        {"translate", rendering::Transform2D::translation(16.0F, 4.0F)},
        {"scale", scale_transform(1.06F)},
        {"rotate", rotate_transform(-4.0F)},
        {"skew", skew_transform(0.12F)},
    }};
    for (const auto& [name, transform] : transforms) {
        auto& card = effects.append_new_child<controls::Panel>();
        card.set_title(name).set_background(rendering::Color::rgba(255, 255, 255));
        card.set_corner_radius(rendering::CornerRadius::uniform(8.0F));
        card.set_shadow(rendering::ShadowStyle{.color = rendering::Color::rgba(0, 0, 0, 36),
                                               .offset = layout::Point{0.0F, 8.0F},
                                               .blur_radius = 18.0F,
                                               .spread = 1.0F});
        card.set_render_transform(transform);
        configure_card(card, 220.0F);
        card.append_new_child<controls::Text>().set_text(name);
    }

    auto& shadow_row = section.append_new_child<controls::StackPanel>();
    configure_row(shadow_row);
    for (const auto shadow : {controls::BorderShadow::None, controls::BorderShadow::Light,
                              controls::BorderShadow::Base, controls::BorderShadow::Dark}) {
        auto& border = shadow_row.append_new_child<controls::Border>();
        border.set_title("Shadow").set_shadow_preset(shadow).set_preset(controls::BorderPreset::Info);
        configure_card(border, 220.0F);
        border.append_new_child<controls::Text>().set_text("Shadow preset");
    }

    auto& feedback = section.append_new_child<controls::StackPanel>();
    configure_row(feedback);
    feedback.append_new_child<controls::Message>()
        .set_text("Message component")
        .set_type(controls::MessageType::Success)
        .set_show_close(true);
    feedback.append_new_child<controls::MessageBox>()
        .set_title("MessageBox")
        .set_message("Prompt and confirm states")
        .set_kind(controls::MessageBoxKind::Prompt)
        .set_type(controls::MessageType::Warning)
        .set_input_text("42");
    feedback.append_new_child<controls::Loading>()
        .set_text("Loading animation")
        .set_active(true)
        .set_show_close(true);
    feedback.append_new_child<controls::Dialog>()
        .set_title("Dialog")
        .set_body("Dialog with open animation and shadow surface")
        .set_show_cancel_button(true);
}

void add_animation_summary(controls::StackPanel& root) {
    auto& section = add_section(root, "Animation timing coverage");
    const std::array<animation::EasingCurve, 14U> curves{
        animation::EasingCurve::Linear,
        animation::EasingCurve::StepStart,
        animation::EasingCurve::StepEnd,
        animation::EasingCurve::EaseInSine,
        animation::EasingCurve::EaseOutSine,
        animation::EasingCurve::EaseInOutSine,
        animation::EasingCurve::EaseInQuad,
        animation::EasingCurve::EaseOutQuad,
        animation::EasingCurve::EaseInOutQuad,
        animation::EasingCurve::EaseInCubic,
        animation::EasingCurve::EaseOutCubic,
        animation::EasingCurve::EaseInOutCubic,
        animation::EasingCurve::EaseOutBack,
        animation::EasingCurve::EaseInOutBack};

    auto value_text = std::string{"Easing samples: "};
    for (const auto curve : curves) {
        const auto value = animation::apply_easing(curve, 0.5F);
        value_text += std::to_string(static_cast<int>(std::round(value * 100.0F)));
        value_text.push_back(' ');
    }
    add_label(section, value_text);

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
    root->configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(canvas_width), layout::Length::points(canvas_height))
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
    add_feedback_effects_section(*root);
    add_animation_summary(*root);
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

int main() {
    auto engine = layout::LayoutEngine{};
    auto root = build_showcase_tree();
    root->bind_layout_tree(engine);
    root->calculate_layout(layout::LayoutConstraints{.width = canvas_width, .height = canvas_height});

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
    std::cout << "  styles: semantic variants sizes status borders shadows text states dark/custom\n";
    std::cout << "  transforms: translate scale rotate skew\n";
    std::cout << "  animations: button loading switch loading loading spinner dialog/message open "
                 "implicit property timeline all easing curves\n";
    std::cout << "  render nodes: " << node_count << '\n';
    std::cout << "  render commands: " << command_count << '\n';
    std::cout << "  animation active during warmup: " << (animation_active ? "yes" : "no")
              << '\n';
    std::cout << "  dirty empty: " << (dirty.empty() ? "yes" : "no") << '\n';
    return command_count > 0U ? 0 : 1;
}
