#include <winelement/elements.hpp>
#include <winelement/layout.hpp>

#include <winelement/rendering.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace winelement::elements;
using namespace winelement::layout;
using namespace winelement::rendering;
using namespace winelement::style;

struct ScopedThemeReset {
    ~ScopedThemeReset() {
        reset_theme();
    }
};

[[nodiscard]] const UIElementStyle& require_theme_style(const Theme& theme,
                                                        std::string_view style_class) {
    const auto* style = theme_style_for_class(theme, style_class);
    EXPECT_NE(style, nullptr);
    static const auto fallback = UIElementStyle{};
    return style != nullptr ? *style : fallback;
}

template <typename CommandPayload>
[[nodiscard]] const CommandPayload& command_as(const RenderOpcodeRecord& command) {
    return command.payload<CommandPayload>();
}

struct PointerRecord {
    const UIElement* current_target = nullptr;
    Point local_position{};
};

class RecordingElement final : public UIElement {
  public:
    using UIElement::UIElement;

    std::string route_name;
    std::vector<std::string>* route_log = nullptr;
    std::vector<PointerRecord> tunnel_records;
    std::vector<PointerRecord> pointer_records;
    std::vector<PointerEventKind> pointer_kinds;
    std::vector<PointerButton> pointer_buttons;
    std::vector<Point> wheel_deltas;
    std::vector<std::uint8_t> click_counts;
    std::vector<bool> primary_button_states;
    std::vector<Key> key_records;
    std::vector<bool> focus_records;
    bool handle_pointer_tunnel = false;
    bool handle_pointer = false;
    bool handle_key = false;

  protected:
    void on_pointer_tunnel_event(PointerEvent& event) override {
        tunnel_records.push_back(PointerRecord{.current_target = event.current_target,
                                               .local_position = event.local_position});
        if (route_log != nullptr) {
            route_log->push_back("T:" + route_name);
        }
        event.handled = handle_pointer_tunnel;
    }

    void on_pointer_event(PointerEvent& event) override {
        pointer_records.push_back(PointerRecord{.current_target = event.current_target,
                                                .local_position = event.local_position});
        pointer_kinds.push_back(event.kind);
        pointer_buttons.push_back(event.button);
        wheel_deltas.push_back(event.wheel_delta);
        click_counts.push_back(event.click_count);
        primary_button_states.push_back(event.primary_button_down);
        if (route_log != nullptr) {
            route_log->push_back("B:" + route_name);
        }
        event.handled = handle_pointer;
    }

    void on_key_event(KeyEvent& event) override {
        key_records.push_back(event.key);
        event.handled = handle_key;
    }

    void on_focus_changed(const FocusChangeEvent& event) override {
        focus_records.push_back(event.focused);
    }

    void on_paint(RenderContext& context, Rect absolute_frame) const override {
        context.stroke_rect(absolute_frame, Color::rgba(64, 158, 255), 1.0F);
    }
};

class CountingPaintElement final : public UIElement {
  public:
    explicit CountingPaintElement(int& paint_count) : paint_count_(&paint_count) {}

  protected:
    void on_paint(RenderContext& context, Rect absolute_frame) const override {
        ++(*paint_count_);
        context.fill_rect(absolute_frame, Color::rgba(64, 158, 255));
    }

  private:
    int* paint_count_ = nullptr;
};

[[nodiscard]] LayoutEngine create_unrounded_engine() {
    LayoutEngineOptions options;
    options.point_scale_factor = 0.0F;
    return LayoutEngine(options);
}

TEST(UIElementTests, MirrorsVisualTreeIntoLayoutTree) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_flex_direction(FlexDirection::Row)
            .set_size(Length::points(300.0F), Length::points(100.0F));
    });

    auto left = std::make_unique<UIElement>();
    left->configure_layout([](LayoutElement& layout) { layout.set_flex_grow(1.0F); });

    auto right = std::make_unique<UIElement>();
    right->configure_layout([](LayoutElement& layout) { layout.set_flex_grow(2.0F); });

    auto& left_ref = root.append_child(std::move(left));
    auto& right_ref = root.append_child(std::move(right));

    EXPECT_EQ(left_ref.parent(), &root);
    EXPECT_EQ(left_ref.layout().parent(), &root.layout());
    EXPECT_EQ(root.child_count(), 2U);

    LayoutConstraints constraints;
    constraints.width = 300.0F;
    constraints.height = 100.0F;
    root.calculate_layout(constraints);

    EXPECT_FALSE(root.needs_layout());
    EXPECT_FALSE(left_ref.needs_layout());
    EXPECT_FLOAT_EQ(left_ref.frame().width, 100.0F);
    EXPECT_FLOAT_EQ(right_ref.frame().x, 100.0F);
    EXPECT_FLOAT_EQ(right_ref.frame().width, 200.0F);
}

TEST(UIElementTests, PropertyStoreInvalidationMarksLayoutAndPaintDirty) {
    auto engine = create_unrounded_engine();
    UIElement element;
    element.bind_layout_tree(engine);
    element.calculate_layout();
    element.clear_paint_dirty_subtree();
    ASSERT_FALSE(element.needs_layout());
    ASSERT_FALSE(element.needs_paint());

    const auto width_property = winelement::core::make_property_metadata<float>(
        "width", winelement::core::PropertyInvalidation::Layout |
                     winelement::core::PropertyInvalidation::Paint);
    element.set_property(width_property, 120.0F);

    EXPECT_TRUE(element.needs_layout());
    EXPECT_TRUE(element.needs_paint());
    EXPECT_FLOAT_EQ(element.properties().value(width_property, 0.0F), 120.0F);
}

TEST(UIElementTests, SemanticsTreeExportsVisibleChildrenAndLabels) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.set_semantics_role(SemanticsRole::Window).set_semantics_label("Root");
    auto child = std::make_unique<UIElement>();
    child->set_text("Child text").set_semantics_role(SemanticsRole::Text);
    root.append_child(std::move(child));
    root.calculate_layout();

    const auto semantics = root.build_semantics_tree();
    EXPECT_EQ(semantics.role, SemanticsRole::Window);
    EXPECT_EQ(semantics.label, "Root");
    ASSERT_EQ(semantics.children.size(), 1U);
    EXPECT_EQ(semantics.children[0].role, SemanticsRole::Text);
    EXPECT_EQ(semantics.children[0].label, "Child text");
}

TEST(UIElementTests, ElementDifferSeparatesUpdatesReplacementsAndInserts) {
    ElementDiffer differ;
    const std::vector<ElementDescriptor> before = {
        ElementDescriptor{.type_name = "Text", .key = "a"},
        ElementDescriptor{.type_name = "Button", .key = "b"}};
    const std::vector<ElementDescriptor> after = {
        ElementDescriptor{.type_name = "Text", .key = "a"},
        ElementDescriptor{.type_name = "Input", .key = "b"},
        ElementDescriptor{.type_name = "Button", .key = "c"}};

    const auto operations = differ.diff(before, after);
    ASSERT_EQ(operations.size(), 3U);
    EXPECT_EQ(operations[0].kind, ElementDiffKind::Update);
    EXPECT_EQ(operations[1].kind, ElementDiffKind::Replace);
    EXPECT_EQ(operations[2].kind, ElementDiffKind::Insert);
}

TEST(UIElementTests, TextEditModelTracksSelectionAndComposition) {
    TextEditModel model;
    model.set_text("WinElement");
    model.set_selection(3U, 10U);
    EXPECT_TRUE(model.has_selection());
    EXPECT_EQ(model.selected_text(), "Element");
    EXPECT_EQ(model.caret_offset(), 10U);

    model.set_composition("x", TextRange{.start = 1U, .end = 3U});
    EXPECT_TRUE(model.composition().active);
    EXPECT_EQ(model.composition().replacement_range.start, 1U);
    model.clear_composition();
    EXPECT_FALSE(model.composition().active);
}

TEST(UIElementTests, BaseElementPaintsStyledBoxAndTextContent) {
    auto engine = create_unrounded_engine();
    UIElement element;
    element.bind_layout_tree(engine);
    element.set_background(Color::rgba(250, 251, 252))
        .set_border(Color::rgba(64, 158, 255), 1.0F)
        .set_corner_radius(CornerRadius::uniform(6.0F))
        .set_shadow(ShadowStyle{
            .color = Color::rgba(0, 0, 0, 40), .offset = {0.0F, 2.0F}, .blur_radius = 8.0F})
        .set_padding(EdgeInsets{12.0F, 6.0F, 12.0F, 6.0F})
        .set_text("Label")
        .set_text_color(Color::rgba(48, 49, 51));
    element.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(140.0F), Length::points(48.0F));
    });
    element.calculate_layout();

    RenderCommandRecorder context;
    element.paint(context);

    ASSERT_EQ(context.commands().size(), 4U);
    EXPECT_EQ(context.commands()[0].type(), RenderCommandType::DrawBoxShadow);
    EXPECT_EQ(context.commands()[1].type(), RenderCommandType::FillRoundedRect);
    EXPECT_EQ(context.commands()[2].type(), RenderCommandType::StrokeRoundedRect);
    EXPECT_EQ(context.commands()[3].type(), RenderCommandType::DrawTextLayout);
    EXPECT_EQ(command_as<FillRoundedRectCommand>(context.commands()[1]).color,
              element.background());
    EXPECT_EQ(command_as<StrokeRoundedRectCommand>(context.commands()[2]).color,
              element.border_color());

    const auto& text_command = command_as<DrawTextLayoutCommand>(context.commands()[3]);
    ASSERT_NE(text_command.layout_value(), nullptr);
    EXPECT_EQ(text_command.layout_value()->text, "Label");
    EXPECT_EQ(text_command.layout_value()->style.color, element.text_color());
    EXPECT_FLOAT_EQ(text_command.origin.x, element.padding().left);
    EXPECT_FLOAT_EQ(text_command.origin.y, element.padding().top);
}

TEST(UIElementTests, BaseElementUsesSharedDefaultStyleUntilFirstMutation) {
    auto engine = create_unrounded_engine();
    UIElement element;
    element.bind_layout_tree(engine);

    EXPECT_EQ(element.style().background, default_panel_style().background);
    EXPECT_EQ(element.style().border_color, default_panel_style().border_color);
    EXPECT_FLOAT_EQ(element.style().border_width, default_panel_style().border_width);

    element.set_background(Color::rgba(10, 20, 30));

    EXPECT_EQ(element.style().background, Color::rgba(10, 20, 30));
    EXPECT_EQ(element.style().border_color, default_panel_style().border_color);
    EXPECT_FLOAT_EQ(element.style().border_width, default_panel_style().border_width);
    EXPECT_EQ(default_panel_style().background, Color::rgba(0, 0, 0, 0));
}

TEST(UIElementTests, BaseElementMeasuresTextContentLikeLabel) {
    auto engine = create_unrounded_engine();
    UIElement label;
    label.bind_layout_tree(engine);
    label.set_text("WinElement")
        .set_font_size(16.0F)
        .set_padding(EdgeInsets{8.0F, 4.0F, 8.0F, 4.0F});

    label.calculate_layout();
    const auto first_width = label.frame().width;
    EXPECT_GT(first_width, 40.0F);
    EXPECT_GT(label.frame().height, 16.0F);

    label.clear_paint_dirty_subtree();
    label.set_text("WinElement label");
    EXPECT_TRUE(label.needs_layout());

    label.calculate_layout();
    EXPECT_GT(label.frame().width, first_width);
}

TEST(UIElementTests, BaseElementTextSelectionIsOptInAndPaintsSelection) {
    auto engine = create_unrounded_engine();
    UIElement label;
    label.bind_layout_tree(engine);
    label.set_text("WinElement selectable text")
        .set_font_size(16.0F)
        .set_padding(EdgeInsets{})
        .set_text_selection_background(Color::rgba(1, 2, 3, 96));
    label.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(260.0F), Length::points(40.0F));
    });
    label.calculate_layout();

    EventRouter router(label);
    const auto ignored = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                                 .position = Point{4.0F, 12.0F},
                                                                 .button = PointerButton::Primary});
    EXPECT_FALSE(ignored.handled);
    EXPECT_FALSE(label.has_text_selection());

    label.set_text_selectable(true);
    const auto down = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                              .position = Point{4.0F, 12.0F},
                                                              .button = PointerButton::Primary});
    EXPECT_TRUE(down.handled);
    EXPECT_FALSE(label.has_text_selection());

    const auto move = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                                              .position = Point{180.0F, 12.0F},
                                                              .primary_button_down = true});
    EXPECT_TRUE(move.handled);

    const auto up = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                            .position = Point{180.0F, 12.0F},
                                                            .button = PointerButton::Primary});
    EXPECT_TRUE(up.handled);
    EXPECT_TRUE(label.has_text_selection());
    EXPECT_FALSE(label.selected_text().empty());

    RenderCommandRecorder context;
    label.paint(context);

    ASSERT_GE(context.commands().size(), 2U);
    EXPECT_EQ(context.commands()[context.commands().size() - 2U].type(),
              RenderCommandType::FillRect);
    EXPECT_EQ(context.commands().back().type(), RenderCommandType::DrawTextLayout);
    EXPECT_EQ(command_as<FillRectCommand>(context.commands()[context.commands().size() - 2U]).color,
              Color::rgba(1, 2, 3, 96));
}

TEST(UIElementTests, BaseElementTextSelectionCanBeControlledByStyle) {
    auto engine = create_unrounded_engine();
    UIElement label;
    label.bind_layout_tree(engine);
    auto style = default_text_style();
    style.text_selection_mode = TextSelectionMode::Text;
    style.text_selection_background = Color::rgba(64, 158, 255, 64);

    label.set_text("Styled").set_style(style).set_text_selection(0U, 6U);

    EXPECT_TRUE(label.text_selectable());
    EXPECT_TRUE(label.has_text_selection());
    EXPECT_EQ(label.selected_text(), "Styled");

    style.text_selection_mode = TextSelectionMode::None;
    label.set_style(style);

    EXPECT_FALSE(label.text_selectable());
    EXPECT_FALSE(label.has_text_selection());
}

TEST(UIElementTests, BaseElementCopyCommandTracksSelectionAvailability) {
    auto engine = create_unrounded_engine();
    UIElement label;
    label.bind_layout_tree(engine);
    label.set_text("Copy me").set_text_selectable(true);

    auto state = label.text_input_edit_command_state();
    EXPECT_FALSE(state.can_copy);
    EXPECT_TRUE(state.can_select_all);

    EXPECT_TRUE(label.invoke_text_input_edit_command(TextInputEditCommand::SelectAll));

    state = label.text_input_edit_command_state();
    EXPECT_TRUE(state.can_copy);
    EXPECT_FALSE(state.can_select_all);
    EXPECT_TRUE(label.invoke_text_input_edit_command(TextInputEditCommand::Copy));
}

TEST(UIElementTests, EventRouterRoutesCopyToCurrentTextSelectionOwner) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    auto child = std::make_unique<UIElement>();
    child->set_text("copy from display text").set_text_selectable(true);
    auto& child_ref = root.append_child(std::move(child));
    EventRouter router(root);

    child_ref.set_text_selection(0U, 4U);

    EXPECT_EQ(router.text_selection_owner(), &child_ref);
    const auto result = router.route_key_event(
        KeyEvent{.kind = KeyEventKind::Down, .key = Key::C, .modifiers = {.control = true}});
    EXPECT_TRUE(result.handled);
    EXPECT_EQ(result.handled_by, &child_ref);

    child_ref.clear_text_selection();
    EXPECT_EQ(router.text_selection_owner(), nullptr);
}

TEST(UIElementTests, EventRouterSynthesizesElementHoverEnterLeave) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(80.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(10.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(60.0F), Length::points(40.0F));
    });
    auto& child_ref = static_cast<RecordingElement&>(root.append_child(std::move(child)));
    root.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 80.0F});

    EventRouter router(root);
    static_cast<void>(router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move, .position = Point{20.0F, 20.0F}}));
    ASSERT_GE(child_ref.pointer_kinds.size(), 2U);
    EXPECT_EQ(child_ref.pointer_kinds[0], PointerEventKind::Enter);
    EXPECT_EQ(child_ref.pointer_kinds[1], PointerEventKind::Move);

    static_cast<void>(router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move, .position = Point{24.0F, 24.0F}}));
    EXPECT_EQ(std::count(child_ref.pointer_kinds.begin(), child_ref.pointer_kinds.end(),
                         PointerEventKind::Enter),
              1);

    static_cast<void>(router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move, .position = Point{120.0F, 20.0F}}));
    EXPECT_NE(std::find(child_ref.pointer_kinds.begin(), child_ref.pointer_kinds.end(),
                        PointerEventKind::Leave),
              child_ref.pointer_kinds.end());
}

TEST(UIElementTests, FocusManagerCanFocusFirstGloballyOrWithinScope) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    auto first = std::make_unique<RecordingElement>();
    first->set_focusable(true);
    auto& first_ref = static_cast<RecordingElement&>(root.append_child(std::move(first)));

    auto scope = std::make_unique<UIElement>();
    auto scoped_child = std::make_unique<RecordingElement>();
    scoped_child->set_focusable(true);
    auto& scoped_child_ref =
        static_cast<RecordingElement&>(scope->append_child(std::move(scoped_child)));
    auto& scope_ref = root.append_child(std::move(scope));

    EventRouter router(root);
    EXPECT_TRUE(router.focus_manager().focus_first());
    EXPECT_EQ(router.focus_manager().focused_element(), &first_ref);

    EXPECT_TRUE(router.focus_manager().focus_first_within(scope_ref));
    EXPECT_EQ(router.focus_manager().focused_element(), &scoped_child_ref);
}

TEST(UIElementTests, ThemeManagerReappliesThemeAcrossChildrenAndTopLayer) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(300.0F), Length::points(160.0F));
    });

    auto child = std::make_unique<UIElement>();
    child->set_theme_class(theme_class::text);
    child->set_text("Child");
    auto& child_ref = root.append_child(std::move(child));

    auto opted_out = std::make_unique<UIElement>();
    opted_out->clear_theme_class();
    opted_out->set_style(default_button_style());
    const auto opted_out_background = opted_out->background();
    auto& opted_out_ref = root.append_child(std::move(opted_out));

    auto overlay = std::make_unique<UIElement>();
    overlay->set_theme_class(theme_class::button);
    auto& overlay_ref = root.push_top_layer(std::move(overlay), TopLayerOptions{});

    ThemeManager::apply_theme(root, make_dark_theme());

    EXPECT_EQ(root.style().border_color, default_panel_style().border_color);
    EXPECT_EQ(child_ref.style().text_color, default_text_style().text_color);
    EXPECT_EQ(overlay_ref.style().background, default_button_style().background);
    EXPECT_EQ(opted_out_ref.background(), opted_out_background);
}

TEST(UIElementTests, ThemeClassUsesSingleNamedPathAndMissingClassLeavesStyleUnchanged) {
    ScopedThemeReset reset;

    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);

    auto custom_button = std::make_unique<UIElement>();
    custom_button->set_theme_class("brand.cta");
    auto& custom_button_ref = root.append_child(std::move(custom_button));

    auto theme = make_default_theme();
    auto cta_style = require_theme_style(theme, theme_class::button);
    cta_style.background = Color::rgba(40, 50, 60);
    cta_style.text_color = Color::rgba(250, 251, 252);
    set_theme_style_class(theme, "brand.cta", cta_style);

    ThemeManager::apply_theme(root, theme);

    EXPECT_EQ(custom_button_ref.style().background, Color::rgba(40, 50, 60));
    EXPECT_EQ(custom_button_ref.style().text_color, Color::rgba(250, 251, 252));
    EXPECT_EQ(std::string(custom_button_ref.theme_class()), "brand.cta");

    auto fallback_theme = make_default_theme();
    const auto previous_background = custom_button_ref.style().background;
    const auto previous_text_color = custom_button_ref.style().text_color;
    ThemeManager::apply_theme(root, fallback_theme);

    EXPECT_EQ(custom_button_ref.style().background, previous_background);
    EXPECT_EQ(custom_button_ref.style().text_color, previous_text_color);
}

TEST(UIElementTests, ManualStyleMutationsDetachFromThemeManagerAndCanReattach) {
    ScopedThemeReset reset;

    auto engine = create_unrounded_engine();
    UIElement element;
    element.bind_layout_tree(engine);
    element.set_theme_class(theme_class::button)
        .set_background(Color::rgba(1, 2, 3))
        .set_opacity(0.35F)
        .set_render_transform(Transform2D::translation(8.0F, 0.0F));

    EXPECT_FALSE(element.theme_managed());
    EXPECT_EQ(element.style().background, Color::rgba(1, 2, 3));
    EXPECT_FLOAT_EQ(element.style().visual.opacity, 0.35F);
    EXPECT_EQ(element.style().visual.transform, Transform2D::translation(8.0F, 0.0F));

    ThemeManager::apply_theme(element, make_dark_theme());

    EXPECT_EQ(element.background(), Color::rgba(1, 2, 3));
    EXPECT_FLOAT_EQ(element.opacity(), 0.35F);
    EXPECT_EQ(element.render_transform(), Transform2D::translation(8.0F, 0.0F));

    element.set_theme_class("brand.cta");
    EXPECT_TRUE(element.theme_managed());

    auto theme = current_theme();
    auto cta_style = require_theme_style(theme, theme_class::button);
    cta_style.background = Color::rgba(11, 12, 13);
    set_theme_style_class(theme, "brand.cta", cta_style);
    set_theme(theme);
    ThemeManager::reapply_current_theme(element);

    EXPECT_TRUE(element.theme_managed());
    EXPECT_EQ(element.background(), Color::rgba(11, 12, 13));
    EXPECT_FLOAT_EQ(element.opacity(), default_button_style().visual.opacity);
    EXPECT_EQ(element.render_transform(), default_button_style().visual.transform);
}

TEST(UIElementTests, ThemeManagerReapplyOnSubtreeUsesNearestAncestorLocalTheme) {
    ScopedThemeReset reset;

    auto global = make_default_theme();
    auto global_text = require_theme_style(global, theme_class::text);
    global_text.text_color = Color::rgba(1, 2, 3);
    set_theme_style_class(global, theme_class::text, global_text);
    set_theme(global);

    auto local = make_dark_theme();
    auto local_text = require_theme_style(local, theme_class::text);
    local_text.text_color = Color::rgba(7, 8, 9);
    set_theme_style_class(local, theme_class::text, local_text);

    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    auto scope = std::make_unique<UIElement>();
    scope->set_local_theme(local);

    auto leaf = std::make_unique<UIElement>();
    leaf->set_theme_class(theme_class::text);
    auto& leaf_ref = scope->append_child(std::move(leaf));
    auto& scope_ref = root.append_child(std::move(scope));

    ThemeManager::reapply_current_theme(leaf_ref);

    EXPECT_TRUE(scope_ref.has_local_theme());
    EXPECT_EQ(leaf_ref.style().text_color, local_text.text_color);
}

TEST(UIElementTests, ThemeManagerReapplyThroughClasslessRootTracksGlobalThemeGeneration) {
    ScopedThemeReset reset;

    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);

    auto leaf = std::make_unique<UIElement>();
    leaf->set_theme_class(theme_class::text);
    auto& leaf_ref = root.append_child(std::move(leaf));

    auto first = make_default_theme();
    auto first_text = require_theme_style(first, theme_class::text);
    first_text.text_color = Color::rgba(1, 2, 3);
    set_theme_style_class(first, theme_class::text, first_text);
    set_theme(first);
    ThemeManager::reapply_current_theme(root);

    auto second = make_default_theme();
    auto second_text = require_theme_style(second, theme_class::text);
    second_text.text_color = Color::rgba(4, 5, 6);
    set_theme_style_class(second, theme_class::text, second_text);
    set_theme(second);
    ThemeManager::reapply_current_theme(root);

    EXPECT_EQ(leaf_ref.style().text_color, second_text.text_color);
}

TEST(UIElementTests, BaseElementTextCanComposeWithChildrenLikeDiv) {
    auto engine = create_unrounded_engine();
    UIElement element;
    element.bind_layout_tree(engine);
    element.set_background(Color::rgba(255, 255, 255))
        .set_padding(EdgeInsets{8.0F, 4.0F, 8.0F, 4.0F})
        .set_text("Badge");
    element.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(96.0F), Length::points(32.0F));
    });

    auto child = std::make_unique<UIElement>();
    child->set_background(Color::rgba(64, 158, 255));
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(16.0F), Length::points(16.0F));
    });
    auto& child_ref = element.append_child(std::move(child));

    element.calculate_layout();

    RenderCommandRecorder context;
    element.paint(context);

    ASSERT_EQ(context.commands().size(), 3U);
    EXPECT_EQ(context.commands()[0].type(), RenderCommandType::FillRect);
    EXPECT_EQ(context.commands()[1].type(), RenderCommandType::DrawTextLayout);
    EXPECT_EQ(context.commands()[2].type(), RenderCommandType::FillRect);
    const auto* text_layout =
        command_as<DrawTextLayoutCommand>(context.commands()[1]).layout_value();
    ASSERT_NE(text_layout, nullptr);
    EXPECT_EQ(text_layout->text, "Badge");

    auto removed = element.remove_child(child_ref);
    ASSERT_NE(removed, nullptr);
    element.configure_layout(
        [](LayoutElement& layout) { layout.set_size(Length::undefined(), Length::undefined()); });
    element.calculate_layout();
    EXPECT_GT(element.frame().width, 30.0F);
}

TEST(UIElementTests, BaseElementRestoresTextMeasureAfterClearingCustomMeasure) {
    auto engine = create_unrounded_engine();
    UIElement label;
    label.bind_layout_tree(engine);
    label.set_text("WinElement");
    label.set_measure_callback([](const MeasureInput&) { return Size{12.0F, 10.0F}; });

    label.calculate_layout();
    EXPECT_FLOAT_EQ(label.frame().width, 12.0F);

    label.clear_measure_callback();
    label.calculate_layout();
    EXPECT_GT(label.frame().width, 40.0F);
}

TEST(UIElementTests, BaseElementAppliesVisualStyleFromStyleObject) {
    auto engine = create_unrounded_engine();
    auto style = default_panel_style();
    style.background = Color::rgba(255, 255, 255);
    style.visual.opacity = 0.5F;
    style.visual.transform = Transform2D::translation(10.0F, 0.0F);
    style.visual.layer_enabled = true;

    UIElement element;

    element.bind_layout_tree(engine);
    element.set_style(style);
    element.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(80.0F), Length::points(32.0F));
    });
    element.calculate_layout();

    EXPECT_FLOAT_EQ(element.opacity(), 0.5F);
    EXPECT_EQ(element.render_transform(), style.visual.transform);
    EXPECT_TRUE(element.layer_enabled());

    RenderCommandList command_list;
    element.commit_render_commands(command_list);

    ASSERT_GE(command_list.commands().size(), 3U);
    EXPECT_EQ(command_list.commands().front().type(), RenderCommandType::PushLayer);
    const auto& layer = command_as<PushLayerCommand>(command_list.commands().front()).options;
    EXPECT_FLOAT_EQ(layer.opacity, 0.5F);
    EXPECT_FALSE(is_identity_transform(layer.transform));
}

TEST(UIElementTests, BaseElementMirrorsLayoutStyleFieldsIntoYogaNode) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(200.0F), Length::points(80.0F));
    });

    auto child = std::make_unique<UIElement>();
    child->set_style(UIElementStyle{.margin = EdgeInsets{7.0F, 5.0F, 3.0F, 2.0F},
                                    .min_width = 70.0F,
                                    .min_height = 24.0F,
                                    .overflow = Overflow::Hidden,
                                    .box_sizing = BoxSizing::ContentBox});
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(20.0F), Length::points(10.0F));
    });
    auto& child_ref = root.append_child(std::move(child));

    root.calculate_layout(LayoutConstraints{.width = 200.0F, .height = 80.0F});

    EXPECT_EQ(child_ref.overflow(), Overflow::Hidden);
    EXPECT_EQ(child_ref.box_sizing(), BoxSizing::ContentBox);
    EXPECT_FLOAT_EQ(child_ref.margin().left, 7.0F);
    EXPECT_FLOAT_EQ(child_ref.margin().top, 5.0F);
    EXPECT_FLOAT_EQ(child_ref.layout().layout_margin().left, 7.0F);
    EXPECT_FLOAT_EQ(child_ref.layout().layout_margin().top, 5.0F);
    EXPECT_FLOAT_EQ(child_ref.frame().x, 7.0F);
    EXPECT_FLOAT_EQ(child_ref.frame().y, 5.0F);
    EXPECT_FLOAT_EQ(child_ref.frame().width, 70.0F);
    EXPECT_FLOAT_EQ(child_ref.frame().height, 24.0F);
}

TEST(UIElementTests, BaseElementViewportOffsetsChildrenAndClipsHitTesting) {
    auto engine = create_unrounded_engine();
    UIElement container;
    container.bind_layout_tree(engine);
    container.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(80.0F), Length::points(40.0F));
    });
    container.set_viewport(Rect{10.0F, 6.0F, 30.0F, 20.0F});

    auto child = std::make_unique<UIElement>();
    child->set_background(Color::rgba(64, 158, 255));
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(30.0F), Length::points(20.0F));
    });
    auto& child_ref = container.append_child(std::move(child));

    container.calculate_layout(LayoutConstraints{.width = 80.0F, .height = 40.0F});

    EXPECT_TRUE(container.has_custom_viewport());
    EXPECT_FLOAT_EQ(container.viewport_rect().x, 10.0F);
    EXPECT_FLOAT_EQ(container.viewport_rect().y, 6.0F);
    EXPECT_FLOAT_EQ(container.viewport_rect().width, 30.0F);
    EXPECT_FLOAT_EQ(container.viewport_rect().height, 20.0F);
    EXPECT_FLOAT_EQ(container.absolute_viewport_rect().x, 10.0F);
    EXPECT_FLOAT_EQ(container.absolute_viewport_rect().y, 6.0F);
    EXPECT_FLOAT_EQ(child_ref.absolute_frame().x, 10.0F);
    EXPECT_FLOAT_EQ(child_ref.absolute_frame().y, 6.0F);

    RenderCommandRecorder recorder;
    container.paint(recorder);
    ASSERT_GE(recorder.commands().size(), 3U);
    EXPECT_EQ(recorder.commands()[0].type(), RenderCommandType::PushClip);
    EXPECT_EQ(recorder.commands()[1].type(), RenderCommandType::FillRect);
    EXPECT_EQ(recorder.commands()[2].type(), RenderCommandType::PopClip);
    EXPECT_FLOAT_EQ(command_as<PushClipCommand>(recorder.commands()[0]).rect.x, 10.0F);
    EXPECT_FLOAT_EQ(command_as<PushClipCommand>(recorder.commands()[0]).rect.y, 6.0F);
    EXPECT_FLOAT_EQ(command_as<PushClipCommand>(recorder.commands()[0]).rect.width, 30.0F);
    EXPECT_FLOAT_EQ(command_as<PushClipCommand>(recorder.commands()[0]).rect.height, 20.0F);

    EXPECT_EQ(container.hit_test(Point{15.0F, 10.0F}), &child_ref);
    EXPECT_EQ(container.hit_test(Point{5.0F, 10.0F}), &container);
}

TEST(UIElementTests, BaseElementCustomViewportWithoutChildrenDoesNotCreateScrollRange) {
    auto engine = create_unrounded_engine();
    UIElement container;
    container.bind_layout_tree(engine);
    container.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(80.0F), Length::points(40.0F));
    });
    container.set_viewport(Rect{10.0F, 6.0F, 20.0F, 12.0F});

    container.calculate_layout(LayoutConstraints{.width = 80.0F, .height = 40.0F});

    EXPECT_FLOAT_EQ(container.scrollable_content_rect().width, 0.0F);
    EXPECT_FLOAT_EQ(container.scrollable_content_rect().height, 0.0F);
    EXPECT_FLOAT_EQ(container.min_scroll_offset().x, 0.0F);
    EXPECT_FLOAT_EQ(container.min_scroll_offset().y, 0.0F);
    EXPECT_FLOAT_EQ(container.max_scroll_offset().x, 0.0F);
    EXPECT_FLOAT_EQ(container.max_scroll_offset().y, 0.0F);

    container.set_scroll_offset(Point{25.0F, 25.0F});
    EXPECT_FLOAT_EQ(container.scroll_offset().x, 0.0F);
    EXPECT_FLOAT_EQ(container.scroll_offset().y, 0.0F);
}

TEST(UIElementTests, BaseElementScrollOffsetRepositionsChildrenAndClampsToContent) {
    auto engine = create_unrounded_engine();
    UIElement container;
    container.bind_layout_tree(engine);
    container.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F));
    });

    auto child = std::make_unique<UIElement>();
    child->set_background(Color::rgba(64, 158, 255));
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(90.0F));
    });
    auto& child_ref = container.append_child(std::move(child));

    container.calculate_layout(LayoutConstraints{.width = 40.0F, .height = 30.0F});

    EXPECT_FLOAT_EQ(container.scrollable_content_rect().height, 90.0F);
    EXPECT_FLOAT_EQ(container.max_scroll_offset().y, 60.0F);

    container.set_scroll_offset(Point{0.0F, 100.0F});

    EXPECT_FLOAT_EQ(container.scroll_offset().y, 60.0F);
    EXPECT_FLOAT_EQ(child_ref.absolute_frame().y, -60.0F);
    EXPECT_EQ(container.hit_test(Point{10.0F, 10.0F}), &child_ref);
}

TEST(UIElementTests, BaseElementPreservesPreLayoutScrollOffsetUntilContentRangeExists) {
    auto engine = create_unrounded_engine();
    UIElement container;
    container.bind_layout_tree(engine);
    container.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F));
    });
    container.set_viewport(Rect{0.0F, 0.0F, 40.0F, 30.0F});
    container.set_scroll_offset(Point{0.0F, 45.0F});

    auto child = std::make_unique<UIElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(90.0F));
    });
    auto& child_ref = container.append_child(std::move(child));

    container.calculate_layout(LayoutConstraints{.width = 40.0F, .height = 30.0F});

    EXPECT_FLOAT_EQ(container.scroll_offset().y, 45.0F);
    EXPECT_FLOAT_EQ(child_ref.absolute_frame().y, -45.0F);
}

TEST(UIElementTests, BaseElementScrollableExtentClipsChildrenAtZeroScrollOffset) {
    auto engine = create_unrounded_engine();
    UIElement container;
    container.bind_layout_tree(engine);
    container.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F));
    });

    auto child = std::make_unique<UIElement>();
    child->set_background(Color::rgba(64, 158, 255));
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(90.0F));
    });
    container.append_child(std::move(child));

    container.calculate_layout(LayoutConstraints{.width = 40.0F, .height = 30.0F});

    EXPECT_FLOAT_EQ(container.scroll_offset().y, 0.0F);
    EXPECT_FLOAT_EQ(container.max_scroll_offset().y, 60.0F);

    RenderCommandRecorder recorder;
    container.paint(recorder);

    ASSERT_GE(recorder.commands().size(), 3U);
    EXPECT_EQ(recorder.commands()[0].type(), RenderCommandType::PushClip);
    EXPECT_EQ(recorder.commands()[1].type(), RenderCommandType::FillRect);
    EXPECT_EQ(recorder.commands()[2].type(), RenderCommandType::PopClip);

    const auto& clip = command_as<PushClipCommand>(recorder.commands()[0]).rect;
    EXPECT_FLOAT_EQ(clip.x, 0.0F);
    EXPECT_FLOAT_EQ(clip.y, 0.0F);
    EXPECT_FLOAT_EQ(clip.width, 40.0F);
    EXPECT_FLOAT_EQ(clip.height, 30.0F);
}

TEST(UIElementTests, BaseElementChildClipKeepsContentInsideVisibleBorder) {
    auto engine = create_unrounded_engine();
    UIElement container;
    container.bind_layout_tree(engine);
    container.set_background(Color::rgba(255, 255, 255))
        .set_border(Color::rgba(64, 158, 255), 1.0F)
        .set_overflow(Overflow::Hidden);
    container.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F));
    });

    auto child = std::make_unique<UIElement>();
    child->set_background(Color::rgba(245, 108, 108));
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F));
    });
    auto& child_ref = container.append_child(std::move(child));

    container.calculate_layout(LayoutConstraints{.width = 40.0F, .height = 30.0F});

    RenderCommandRecorder recorder;
    container.paint(recorder);

    const auto clip_iterator = std::find_if(
        recorder.commands().begin(), recorder.commands().end(),
        [](const auto& command) { return command.type() == RenderCommandType::PushClip; });
    ASSERT_NE(clip_iterator, recorder.commands().end());
    const auto& clip = command_as<PushClipCommand>(*clip_iterator).rect;
    EXPECT_FLOAT_EQ(clip.x, 1.0F);
    EXPECT_FLOAT_EQ(clip.y, 1.0F);
    EXPECT_FLOAT_EQ(clip.width, 38.0F);
    EXPECT_FLOAT_EQ(clip.height, 28.0F);

    EXPECT_EQ(container.hit_test(Point{0.5F, 15.0F}), &container);
    EXPECT_EQ(container.hit_test(Point{2.0F, 15.0F}), &child_ref);
}

TEST(UIElementTests, BaseElementScrollInvalidatesCachedChildRenderCommands) {
    auto engine = create_unrounded_engine();
    UIElement container;
    container.bind_layout_tree(engine);
    container.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F));
    });

    auto paint_count = 0;
    auto child = std::make_unique<CountingPaintElement>(paint_count);
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(90.0F));
    });
    auto& child_ref = container.append_child(std::move(child));

    container.calculate_layout(LayoutConstraints{.width = 40.0F, .height = 30.0F});

    RenderCommandList first_commit;
    container.commit_render_commands(first_commit, nullptr);
    container.clear_paint_dirty_subtree();

    container.set_scroll_offset(Point{0.0F, 20.0F});

    RenderCommandList second_commit;
    container.commit_render_commands(second_commit, nullptr);

    auto fill_rect_y = std::numeric_limits<float>::quiet_NaN();
    for (const auto& command : second_commit.commands()) {
        if (command.type() != RenderCommandType::FillRect) {
            continue;
        }
        fill_rect_y = command_as<FillRectCommand>(command).rect.y;
        break;
    }

    ASSERT_TRUE(std::isfinite(fill_rect_y));
    EXPECT_FLOAT_EQ(fill_rect_y, -20.0F);
    EXPECT_GT(child_ref.layout_generation(), 1U);
}

TEST(UIElementTests, BaseElementViewportSkipsOffscreenChildCommandRecording) {
    auto engine = create_unrounded_engine();
    UIElement container;
    container.bind_layout_tree(engine);
    container.configure_layout([](LayoutElement& layout) {
        layout.set_flex_direction(FlexDirection::Column)
            .set_size(Length::points(40.0F), Length::points(30.0F));
    });

    auto first_count = 0;
    auto first = std::make_unique<CountingPaintElement>(first_count);
    first->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F)).set_flex_shrink(0.0F);
    });
    container.append_child(std::move(first));

    auto second_count = 0;
    auto second = std::make_unique<CountingPaintElement>(second_count);
    second->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F)).set_flex_shrink(0.0F);
    });
    container.append_child(std::move(second));

    container.calculate_layout(LayoutConstraints{.width = 40.0F, .height = 30.0F});

    RenderCommandList command_list;
    container.commit_render_commands(command_list, nullptr);

    EXPECT_EQ(first_count, 1);
    EXPECT_EQ(second_count, 0);
    EXPECT_FLOAT_EQ(container.max_scroll_offset().y, 30.0F);
}

TEST(UIElementTests, BaseElementViewportInvalidatesCachedRenderCommands) {
    auto engine = create_unrounded_engine();
    UIElement container;
    container.bind_layout_tree(engine);
    container.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(60.0F), Length::points(40.0F));
    });

    auto paint_count = 0;
    auto child = std::make_unique<CountingPaintElement>(paint_count);
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(60.0F), Length::points(40.0F));
    });
    container.append_child(std::move(child));

    container.calculate_layout(LayoutConstraints{.width = 60.0F, .height = 40.0F});

    RenderCommandList first_commit;
    container.commit_render_commands(first_commit, nullptr);
    container.clear_paint_dirty_subtree();

    container.set_viewport(Rect{4.0F, 6.0F, 40.0F, 24.0F});

    RenderCommandList second_commit;
    container.commit_render_commands(second_commit, nullptr);

    EXPECT_GT(paint_count, 1);
}

TEST(UIElementTests, BaseElementWheelScrollsScrollableAncestor) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(60.0F));
    });

    auto scroller = std::make_unique<UIElement>();
    scroller->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F));
    });

    auto tall_child = std::make_unique<UIElement>();
    tall_child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(90.0F));
    });
    scroller->append_child(std::move(tall_child));

    auto& scroller_ref = root.append_child(std::move(scroller));
    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 60.0F});

    EventRouter router(root);
    const auto result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Wheel,
                                                                .position = Point{10.0F, 10.0F},
                                                                .wheel_delta = Point{0.0F, -1.0F}});

    EXPECT_TRUE(result.handled);
    EXPECT_EQ(result.handled_by, &scroller_ref);
    EXPECT_GT(scroller_ref.scroll_offset().y, 0.0F);
}

TEST(UIElementTests, BaseElementScrollAndViewportPreservePointerLocalPosition) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(60.0F));
    });

    auto scroller = std::make_unique<UIElement>();
    scroller->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F));
    });
    scroller->set_viewport(Rect{5.0F, 4.0F, 20.0F, 10.0F});

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(50.0F), Length::points(20.0F));
    });
    auto* child_ptr = child.get();
    scroller->append_child(std::move(child));

    auto& scroller_ref = root.append_child(std::move(scroller));
    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 60.0F});
    scroller_ref.set_scroll_offset(Point{12.0F, 3.0F});

    EventRouter router(root);
    const auto result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                                .position = Point{6.0F, 8.0F},
                                                                .button = PointerButton::Primary});

    EXPECT_EQ(result.target, child_ptr);
    ASSERT_FALSE(child_ptr->pointer_records.empty());
    EXPECT_FLOAT_EQ(child_ptr->pointer_records.back().local_position.x, 13.0F);
    EXPECT_FLOAT_EQ(child_ptr->pointer_records.back().local_position.y, 7.0F);
}

TEST(UIElementTests, BaseElementOverflowHiddenClipsChildContentAndHitTest) {
    auto engine = create_unrounded_engine();
    UIElement container;
    container.bind_layout_tree(engine);
    container.set_background(Color::rgba(255, 255, 255)).set_overflow(Overflow::Hidden);
    container.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(80.0F), Length::points(40.0F));
    });

    auto under = std::make_unique<UIElement>();
    under->set_background(Color::rgba(64, 158, 255));
    under->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(10.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(20.0F), Length::points(20.0F));
    });

    auto overflow_child = std::make_unique<UIElement>();
    overflow_child->set_background(Color::rgba(234, 67, 53));
    overflow_child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(70.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(20.0F), Length::points(20.0F));
    });

    container.append_child(std::move(under));
    container.append_child(std::move(overflow_child));
    container.calculate_layout(LayoutConstraints{.width = 80.0F, .height = 40.0F});

    EXPECT_EQ(container.overflow(), Overflow::Hidden);

    // Point inside parent and inside partially-visible child -> child found
    EXPECT_EQ(container.hit_test(Point{75.0F, 20.0F}), &container.child_at(1));
    // Point entirely outside parent -> nullptr
    EXPECT_EQ(container.hit_test(Point{85.0F, 20.0F}), nullptr);

    // Visible overflow: same hit-test behavior
    container.set_overflow(Overflow::Visible);
    EXPECT_EQ(container.overflow(), Overflow::Visible);
    EXPECT_EQ(container.hit_test(Point{75.0F, 20.0F}), &container.child_at(1));
}

TEST(UIElementTests, BaseElementMinWidthAndHeightAffectLayout) {
    auto engine = create_unrounded_engine();
    UIElement element;
    element.bind_layout_tree(engine);
    element.set_text("Hi").set_min_width(60.0F).set_min_height(30.0F);
    element.set_padding(EdgeInsets{4.0F, 2.0F, 4.0F, 2.0F});
    element.calculate_layout();

    EXPECT_GE(element.frame().width, 60.0F);
    EXPECT_GE(element.frame().height, 30.0F);
    EXPECT_FLOAT_EQ(element.min_width(), 60.0F);
    EXPECT_FLOAT_EQ(element.min_height(), 30.0F);
}

TEST(UIElementTests, RestoresLayoutOwnershipWhenRemovingChildren) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    auto child = std::make_unique<UIElement>();

    auto& child_ref = root.append_child(std::move(child));
    EXPECT_EQ(child_ref.layout().parent(), &root.layout());

    auto removed = root.remove_child(child_ref);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->parent(), nullptr);
    EXPECT_EQ(removed->layout().parent(), nullptr);
    EXPECT_EQ(root.child_count(), 0U);

    auto& attached_again = root.append_child(std::move(removed));
    EXPECT_EQ(attached_again.parent(), &root);
    EXPECT_EQ(attached_again.layout().parent(), &root.layout());
}

TEST(UIElementTests, AppendsNewChildrenThroughParentFactoryWithoutExplicitEngine) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);

    auto& child = root.append_new_child<UIElement>();

    EXPECT_EQ(child.parent(), &root);
    EXPECT_EQ(child.layout().parent(), &root.layout());
    EXPECT_EQ(root.child_count(), 1U);
}

TEST(UIElementTests, DetachedElementsBindToParentLayoutEngineOnInsert) {
    UIElement root;
    auto child = std::make_unique<UIElement>();
    auto* child_ptr = child.get();

    root.append_child(std::move(child));

    EXPECT_EQ(root.child_count(), 1U);
    EXPECT_EQ(&root.child_at(0), child_ptr);
    EXPECT_EQ(child_ptr->parent(), &root);
    EXPECT_EQ(child_ptr->layout().parent(), &root.layout());
}

TEST(UIElementTests, DetachedLayoutConfigurationMaterializesOnCalculate) {
    UIElement root;
    root.configure_layout([](LayoutElement& layout) {
        layout.set_flex_direction(FlexDirection::Row)
            .set_size(Length::points(180.0F), Length::points(60.0F));
    });

    auto left = std::make_unique<UIElement>();
    left->configure_layout([](LayoutElement& layout) { layout.set_flex_grow(1.0F); });

    auto right = std::make_unique<UIElement>();
    right->configure_layout([](LayoutElement& layout) { layout.set_flex_grow(2.0F); });

    auto& left_ref = root.append_child(std::move(left));
    auto& right_ref = root.append_child(std::move(right));

    root.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 60.0F});

    EXPECT_EQ(left_ref.layout().parent(), &root.layout());
    EXPECT_EQ(right_ref.layout().parent(), &root.layout());
    EXPECT_FLOAT_EQ(left_ref.frame().width, 60.0F);
    EXPECT_FLOAT_EQ(right_ref.frame().x, 60.0F);
    EXPECT_FLOAT_EQ(right_ref.frame().width, 120.0F);
}

TEST(UIElementTests, DetachedSubtreeRemountPreservesPendingLayoutStyleAcrossEngines) {
    auto first_engine = create_unrounded_engine();
    auto second_engine = create_unrounded_engine();

    UIElement first_root;
    first_root.bind_layout_tree(first_engine);
    auto child = std::make_unique<UIElement>();
    child->set_margin(EdgeInsets{4.0F, 0.0F, 0.0F, 0.0F});
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(32.0F), Length::points(16.0F));
    });
    auto& child_ref = first_root.append_child(std::move(child));
    first_root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 40.0F});

    auto detached = first_root.remove_child(child_ref);
    UIElement second_root;
    second_root.bind_layout_tree(second_engine);
    auto& rebound = second_root.append_child(std::move(detached));
    second_root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 40.0F});

    EXPECT_EQ(rebound.layout().parent(), &second_root.layout());
    EXPECT_FLOAT_EQ(rebound.frame().x, 4.0F);
    EXPECT_FLOAT_EQ(rebound.frame().width, 32.0F);
    EXPECT_FLOAT_EQ(rebound.layout().layout_margin().left, 4.0F);
}

TEST(UIElementTests, VisitsPaintOrderWithAbsoluteFrames) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_flex_direction(FlexDirection::Row)
            .set_size(Length::points(120.0F), Length::points(40.0F))
            .set_padding(Edge::Left, Length::points(10.0F));
    });

    auto container = std::make_unique<UIElement>();
    container->configure_layout([](LayoutElement& layout) {
        layout.set_flex_direction(FlexDirection::Row)
            .set_size(Length::points(60.0F), Length::points(30.0F))
            .set_padding(Edge::Left, Length::points(5.0F));
    });

    auto leaf = std::make_unique<UIElement>();
    leaf->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(20.0F), Length::points(10.0F));
    });

    auto& container_ref = root.append_child(std::move(container));
    auto& leaf_ref = container_ref.append_child(std::move(leaf));

    LayoutConstraints constraints;
    constraints.width = 120.0F;
    constraints.height = 40.0F;
    root.calculate_layout(constraints);

    EXPECT_FLOAT_EQ(container_ref.frame().x, 10.0F);
    EXPECT_FLOAT_EQ(leaf_ref.frame().x, 5.0F);
    EXPECT_FLOAT_EQ(leaf_ref.absolute_frame().x, 15.0F);

    std::vector<Rect> paint_frames;
    root.visit_paint_order(
        [&](UIElement&, Rect absolute_frame) { paint_frames.push_back(absolute_frame); });

    ASSERT_EQ(paint_frames.size(), 3U);
    EXPECT_FLOAT_EQ(paint_frames[0].x, 0.0F);
    EXPECT_FLOAT_EQ(paint_frames[1].x, 10.0F);
    EXPECT_FLOAT_EQ(paint_frames[2].x, 15.0F);
}

TEST(UIElementTests, VisitPaintOrderUsesZIndexOrdering) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(60.0F));
    });

    auto high = std::make_unique<UIElement>();
    high->set_z_index(10);
    high->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(8.0F))
            .set_position(Edge::Top, Length::points(8.0F))
            .set_size(Length::points(24.0F), Length::points(24.0F));
    });
    auto* high_ptr = high.get();

    auto low = std::make_unique<UIElement>();
    low->set_z_index(-5);
    low->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(12.0F))
            .set_position(Edge::Top, Length::points(12.0F))
            .set_size(Length::points(24.0F), Length::points(24.0F));
    });
    auto* low_ptr = low.get();

    root.append_child(std::move(high));
    root.append_child(std::move(low));
    root.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 60.0F});

    std::vector<const UIElement*> visit_order;
    root.visit_paint_order(
        [&](const UIElement& element, Rect) { visit_order.push_back(&element); });

    ASSERT_EQ(visit_order.size(), 3U);
    EXPECT_EQ(visit_order[0], &root);
    EXPECT_EQ(visit_order[1], low_ptr);
    EXPECT_EQ(visit_order[2], high_ptr);
}

TEST(UIElementTests, CommitsLayoutSnapshotForFastAbsoluteFrameQueries) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_flex_direction(FlexDirection::Row)
            .set_size(Length::points(120.0F), Length::points(40.0F))
            .set_padding(Edge::Left, Length::points(10.0F));
    });

    auto container = std::make_unique<UIElement>();
    container->configure_layout([](LayoutElement& layout) {
        layout.set_flex_direction(FlexDirection::Row)
            .set_size(Length::points(60.0F), Length::points(30.0F))
            .set_padding(Edge::Left, Length::points(5.0F));
    });

    auto leaf = std::make_unique<UIElement>();
    leaf->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(20.0F), Length::points(10.0F));
    });

    auto& container_ref = root.append_child(std::move(container));
    auto& leaf_ref = container_ref.append_child(std::move(leaf));

    LayoutConstraints constraints;
    constraints.width = 120.0F;
    constraints.height = 40.0F;
    root.calculate_layout(constraints);

    EXPECT_EQ(root.layout_generation(), 1U);
    EXPECT_EQ(container_ref.layout_generation(), root.layout_generation());
    EXPECT_EQ(leaf_ref.layout_generation(), root.layout_generation());
    EXPECT_FLOAT_EQ(leaf_ref.frame().x, 5.0F);
    EXPECT_FLOAT_EQ(leaf_ref.absolute_frame().x, 15.0F);

    root.configure_layout(
        [](LayoutElement& layout) { layout.set_padding(Edge::Left, Length::points(20.0F)); });
    EXPECT_TRUE(root.needs_layout());
    EXPECT_FLOAT_EQ(leaf_ref.absolute_frame().x, 15.0F);

    root.calculate_layout(constraints);

    EXPECT_EQ(root.layout_generation(), 2U);
    EXPECT_FLOAT_EQ(leaf_ref.absolute_frame().x, 25.0F);
}

TEST(UIElementTests, TransfersDetachedSubtreeThreadAccessOnInsert) {
    UIElement root;
    std::promise<std::unique_ptr<UIElement>> detached_subtree_promise;
    auto detached_subtree_future = detached_subtree_promise.get_future();

    std::thread worker([&detached_subtree_promise]() mutable {
        auto subtree = std::make_unique<UIElement>();
        subtree->append_child(std::make_unique<UIElement>());
        detached_subtree_promise.set_value(std::move(subtree));
    });

    auto detached_subtree = detached_subtree_future.get();
    worker.join();

    EXPECT_FALSE(detached_subtree->check_thread_access());

    auto& attached = root.append_child(std::move(detached_subtree));

    EXPECT_TRUE(attached.check_thread_access());
    EXPECT_TRUE(attached.child_at(0).check_thread_access());
}

TEST(UIElementTests, VerifyThreadAccessRejectsForeignThread) {
    UIElement element;
    std::promise<bool> rejected_promise;
    auto rejected_future = rejected_promise.get_future();

    std::thread worker([&element, &rejected_promise]() mutable {
        try {
            element.verify_thread_access();
            rejected_promise.set_value(false);
        } catch (const std::logic_error&) {
            rejected_promise.set_value(true);
        } catch (...) {
            rejected_promise.set_value(false);
        }
    });

    EXPECT_TRUE(rejected_future.get());
    worker.join();
}

TEST(UIElementTests, HitTestFindsDeepestElementInReversePaintOrder) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(100.0F));
    });

    auto background = std::make_unique<UIElement>();
    background->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F))
            .set_size(Length::points(60.0F), Length::points(60.0F));
    });

    auto foreground = std::make_unique<UIElement>();
    foreground->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F))
            .set_size(Length::points(60.0F), Length::points(60.0F))
            .set_padding(Edge::Left, Length::points(10.0F))
            .set_padding(Edge::Top, Length::points(10.0F));
    });

    auto leaf = std::make_unique<UIElement>();
    leaf->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(20.0F), Length::points(20.0F));
    });

    auto& background_ref = root.append_child(std::move(background));
    auto& foreground_ref = root.append_child(std::move(foreground));
    auto& leaf_ref = foreground_ref.append_child(std::move(leaf));

    LayoutConstraints constraints;
    constraints.width = 100.0F;
    constraints.height = 100.0F;
    root.calculate_layout(constraints);

    EXPECT_EQ(root.hit_test(Point{15.0F, 15.0F}), &leaf_ref);
    EXPECT_EQ(root.hit_test(Point{5.0F, 5.0F}), &foreground_ref);
    EXPECT_EQ(root.hit_test(Point{60.0F, 10.0F}), &root);
    EXPECT_EQ(root.hit_test(Point{120.0F, 10.0F}), nullptr);

    const auto& const_root = root;
    EXPECT_EQ(const_root.hit_test(Point{15.0F, 15.0F}), static_cast<const UIElement*>(&leaf_ref));

    foreground_ref.set_hit_test_visible(false);
    EXPECT_FALSE(foreground_ref.hit_test_visible());
    // hit_test_visible_=false on parent: parent doesn't hit, but children still can (CSS
    // pointer-events:none semantics)
    EXPECT_EQ(root.hit_test(Point{15.0F, 15.0F}), &leaf_ref);

    foreground_ref.set_hit_test_visible(true).set_visible(false);
    EXPECT_EQ(root.hit_test(Point{15.0F, 15.0F}), &background_ref);

    root.set_hit_test_visible(false);
    // root hit_test_visible=false: root itself doesn't hit, but children still can
    EXPECT_EQ(root.hit_test(Point{15.0F, 15.0F}), &background_ref);
}

TEST(UIElementTests, HitTestingFollowsRenderTransforms) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(200.0F), Length::points(100.0F));
    });

    auto child = std::make_unique<UIElement>();
    child->set_render_transform(Transform2D::translation(30.0F, 0.0F));
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(20.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(20.0F), Length::points(20.0F));
    });

    auto& child_ref = root.append_child(std::move(child));

    LayoutConstraints constraints;
    constraints.width = 200.0F;
    constraints.height = 100.0F;
    root.calculate_layout(constraints);

    EXPECT_EQ(root.hit_test(Point{25.0F, 15.0F}), &root);
    EXPECT_EQ(root.hit_test(Point{55.0F, 15.0F}), &child_ref);

    const auto& const_root = root;
    EXPECT_EQ(const_root.hit_test(Point{55.0F, 15.0F}), static_cast<const UIElement*>(&child_ref));
}

TEST(UIElementTests, RoutesPointerEventsThroughHitPathAndFocusesTarget) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(200.0F), Length::points(100.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->set_focusable(true);
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(20.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(50.0F), Length::points(20.0F));
    });

    auto& child_ref = static_cast<RecordingElement&>(root.append_child(std::move(child)));

    LayoutConstraints constraints;
    constraints.width = 200.0F;
    constraints.height = 100.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    const auto result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                                .position = Point{25.0F, 15.0F},
                                                                .button = PointerButton::Primary});

    EXPECT_EQ(result.target, &child_ref);
    EXPECT_FALSE(result.handled);
    EXPECT_EQ(router.focus_manager().focused_element(), &child_ref);
    ASSERT_EQ(child_ref.pointer_records.size(), 1U);
    ASSERT_EQ(root.pointer_records.size(), 1U);
    EXPECT_EQ(child_ref.pointer_records[0].current_target, &child_ref);
    EXPECT_FLOAT_EQ(child_ref.pointer_records[0].local_position.x, 5.0F);
    EXPECT_FLOAT_EQ(child_ref.pointer_records[0].local_position.y, 5.0F);
    EXPECT_EQ(root.pointer_records[0].current_target, &root);
    EXPECT_FLOAT_EQ(root.pointer_records[0].local_position.x, 25.0F);
    EXPECT_FLOAT_EQ(root.pointer_records[0].local_position.y, 15.0F);
    EXPECT_EQ(child_ref.focus_records, std::vector<bool>({true}));
}

TEST(UIElementTests, PointerLocalPositionFollowsRenderTransforms) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(200.0F), Length::points(100.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->set_focusable(true).set_render_transform(Transform2D::translation(30.0F, 0.0F));
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(20.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(20.0F), Length::points(20.0F));
    });

    auto& child_ref = static_cast<RecordingElement&>(root.append_child(std::move(child)));

    LayoutConstraints constraints;
    constraints.width = 200.0F;
    constraints.height = 100.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    const auto result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                                .position = Point{55.0F, 15.0F},
                                                                .button = PointerButton::Primary});

    EXPECT_EQ(result.target, &child_ref);
    ASSERT_EQ(child_ref.pointer_records.size(), 1U);
    EXPECT_FLOAT_EQ(child_ref.pointer_records[0].local_position.x, 5.0F);
    EXPECT_FLOAT_EQ(child_ref.pointer_records[0].local_position.y, 5.0F);
    ASSERT_EQ(root.pointer_records.size(), 1U);
    EXPECT_FLOAT_EQ(root.pointer_records[0].local_position.x, 55.0F);
    EXPECT_FLOAT_EQ(root.pointer_records[0].local_position.y, 15.0F);
}

TEST(UIElementTests, RoutesPointerEventsThroughTunnelBeforeBubble) {
    auto engine = create_unrounded_engine();
    std::vector<std::string> route_log;

    RecordingElement root;

    root.bind_layout_tree(engine);
    root.route_name = "root";
    root.route_log = &route_log;
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(240.0F), Length::points(120.0F));
    });

    auto panel = std::make_unique<RecordingElement>();
    panel->route_name = "panel";
    panel->route_log = &route_log;
    panel->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(20.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(100.0F), Length::points(60.0F))
            .set_padding(Edge::Left, Length::points(10.0F))
            .set_padding(Edge::Top, Length::points(10.0F));
    });

    auto leaf = std::make_unique<RecordingElement>();
    leaf->route_name = "leaf";
    leaf->route_log = &route_log;
    leaf->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(30.0F), Length::points(20.0F));
    });

    auto& panel_ref = static_cast<RecordingElement&>(root.append_child(std::move(panel)));
    auto& leaf_ref = static_cast<RecordingElement&>(panel_ref.append_child(std::move(leaf)));

    LayoutConstraints constraints;
    constraints.width = 240.0F;
    constraints.height = 120.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    const auto result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                                .position = Point{35.0F, 25.0F},
                                                                .button = PointerButton::Primary});

    EXPECT_EQ(result.target, &leaf_ref);
    EXPECT_FALSE(result.handled);
    EXPECT_EQ(route_log, std::vector<std::string>(
                             {"T:root", "T:panel", "T:leaf", "B:leaf", "B:panel", "B:root"}));
    ASSERT_EQ(leaf_ref.tunnel_records.size(), 1U);
    EXPECT_FLOAT_EQ(leaf_ref.tunnel_records[0].local_position.x, 5.0F);
    EXPECT_FLOAT_EQ(leaf_ref.tunnel_records[0].local_position.y, 5.0F);

    route_log.clear();
    panel_ref.handle_pointer_tunnel = true;
    const auto handled_result =
        router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                .position = Point{35.0F, 25.0F},
                                                .button = PointerButton::Primary});

    EXPECT_TRUE(handled_result.handled);
    EXPECT_EQ(handled_result.handled_by, &panel_ref);
    EXPECT_EQ(handled_result.handled_phase, EventRoutePhase::Tunnel);
    EXPECT_EQ(route_log, std::vector<std::string>({"T:root", "T:panel"}));
}

TEST(UIElementTests, MovesFocusWithTabAndRoutesKeysToFocusedElement) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(200.0F), Length::points(40.0F));
    });

    auto first = std::make_unique<RecordingElement>();
    first->set_focusable(true);
    first->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_size(Length::points(40.0F), Length::points(20.0F));
    });

    auto second = std::make_unique<RecordingElement>();
    second->set_focusable(true);
    second->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(50.0F))
            .set_size(Length::points(40.0F), Length::points(20.0F));
    });

    auto& first_ref = static_cast<RecordingElement&>(root.append_child(std::move(first)));
    auto& second_ref = static_cast<RecordingElement&>(root.append_child(std::move(second)));

    LayoutConstraints constraints;
    constraints.width = 200.0F;
    constraints.height = 40.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    EXPECT_TRUE(router.focus_manager().focus_next());
    EXPECT_EQ(router.focus_manager().focused_element(), &first_ref);

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Tab}).handled);
    EXPECT_EQ(router.focus_manager().focused_element(), &second_ref);

    second_ref.handle_key = true;
    const auto key_result =
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Enter});
    EXPECT_TRUE(key_result.handled);
    EXPECT_EQ(key_result.target, &second_ref);
    EXPECT_EQ(key_result.handled_by, &second_ref);
    EXPECT_EQ(second_ref.key_records, std::vector<Key>({Key::Enter}));

    second_ref.handle_key = false;
    EXPECT_TRUE(router
                    .route_key_event(KeyEvent{.kind = KeyEventKind::Down,
                                              .key = Key::Tab,
                                              .modifiers = KeyModifiers{.shift = true}})
                    .handled);
    EXPECT_EQ(router.focus_manager().focused_element(), &first_ref);
}

TEST(UIElementTests, PaintsVisibleSubtreeInPaintOrder) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(80.0F), Length::points(40.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(10.0F))
            .set_size(Length::points(30.0F), Length::points(20.0F));
    });
    root.append_child(std::move(child));

    LayoutConstraints constraints;
    constraints.width = 80.0F;
    constraints.height = 40.0F;
    root.calculate_layout(constraints);

    RenderCommandRecorder context;
    root.paint(context);

    ASSERT_EQ(context.commands().size(), 2U);
    EXPECT_EQ(context.commands()[0].type(), RenderCommandType::StrokeRect);
    EXPECT_FLOAT_EQ(command_as<StrokeRectCommand>(context.commands()[0]).rect.x, 0.0F);
    EXPECT_EQ(context.commands()[1].type(), RenderCommandType::StrokeRect);
    EXPECT_FLOAT_EQ(command_as<StrokeRectCommand>(context.commands()[1]).rect.x, 10.0F);
}

TEST(UIElementTests, TopLayerPaintsAfterVisualTreeWithIndependentBounds) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(60.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(8.0F))
            .set_position(Edge::Top, Length::points(6.0F))
            .set_size(Length::points(20.0F), Length::points(16.0F));
    });
    root.append_child(std::move(child));

    auto layer = std::make_unique<RecordingElement>();
    auto& layer_ref = root.push_top_layer(
        std::move(layer),
        TopLayerOptions{.bounds = Rect{30.0F, 10.0F, 40.0F, 20.0F}, .light_dismiss = false});

    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 60.0F});

    EXPECT_EQ(layer_ref.parent(), &root);
    EXPECT_EQ(root.top_layer_count(), 1U);
    EXPECT_FLOAT_EQ(root.top_layer_bounds(layer_ref).x, 30.0F);
    EXPECT_FLOAT_EQ(layer_ref.absolute_frame().x, 30.0F);
    EXPECT_FLOAT_EQ(layer_ref.absolute_frame().y, 10.0F);
    EXPECT_FLOAT_EQ(layer_ref.absolute_frame().width, 40.0F);
    EXPECT_FLOAT_EQ(layer_ref.absolute_frame().height, 20.0F);

    RenderCommandRecorder context;
    root.paint(context);

    ASSERT_EQ(context.commands().size(), 3U);
    EXPECT_EQ(command_as<StrokeRectCommand>(context.commands()[0]).rect.x, 0.0F);
    EXPECT_EQ(command_as<StrokeRectCommand>(context.commands()[1]).rect.x, 8.0F);
    EXPECT_EQ(command_as<StrokeRectCommand>(context.commands()[2]).rect.x, 30.0F);
}

TEST(UIElementTests, TopLayerHitTestingTakesPriorityOverVisualTree) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(60.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(10.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(40.0F), Length::points(24.0F));
    });
    auto& child_ref = static_cast<RecordingElement&>(root.append_child(std::move(child)));

    auto layer = std::make_unique<RecordingElement>();
    auto& layer_ref = static_cast<RecordingElement&>(root.push_top_layer(
        std::move(layer),
        TopLayerOptions{.bounds = Rect{10.0F, 10.0F, 40.0F, 24.0F}, .light_dismiss = false}));

    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 60.0F});

    EXPECT_EQ(root.hit_test(Point{20.0F, 20.0F}), &child_ref);
    EXPECT_EQ(root.hit_test_top_layer(Point{20.0F, 20.0F}), &layer_ref);

    EventRouter router(root);
    const auto result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                                .position = Point{20.0F, 20.0F},
                                                                .button = PointerButton::Primary});

    EXPECT_EQ(result.target, &layer_ref);
    EXPECT_TRUE(child_ref.pointer_records.empty());
    ASSERT_EQ(layer_ref.pointer_records.size(), 1U);
    EXPECT_FLOAT_EQ(layer_ref.pointer_records[0].local_position.x, 10.0F);
    EXPECT_FLOAT_EQ(layer_ref.pointer_records[0].local_position.y, 10.0F);
}

TEST(UIElementTests, TopLayerLightDismissConsumesOutsidePointer) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(60.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(70.0F))
            .set_position(Edge::Top, Length::points(32.0F))
            .set_size(Length::points(20.0F), Length::points(20.0F));
    });
    auto& child_ref = static_cast<RecordingElement&>(root.append_child(std::move(child)));

    auto layer = std::make_unique<RecordingElement>();
    auto& layer_ref = static_cast<RecordingElement&>(root.push_top_layer(
        std::move(layer),
        TopLayerOptions{.bounds = Rect{10.0F, 10.0F, 20.0F, 16.0F}, .light_dismiss = true}));

    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 60.0F});

    EventRouter router(root);
    const auto result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                                .position = Point{80.0F, 40.0F},
                                                                .button = PointerButton::Primary});

    // Outside click is consumed; no element receives the event.
    EXPECT_TRUE(result.handled);
    EXPECT_EQ(result.target, nullptr);
    EXPECT_EQ(root.top_layer_count(), 0U);
    EXPECT_TRUE(layer_ref.pointer_records.empty());
    EXPECT_TRUE(child_ref.pointer_records.empty());
}

TEST(UIElementTests, TopLayerLightDismissOnlyClosesTopmostEntry) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(80.0F));
    });

    auto bottom = std::make_unique<RecordingElement>();
    auto bottom_dismissed = 0;
    auto& bottom_ref = root.push_top_layer(
        std::move(bottom),
        TopLayerOptions{.bounds = Rect{10.0F, 10.0F, 80.0F, 50.0F},
                        .light_dismiss = true,
                        .on_dismissed = [&bottom_dismissed]() { ++bottom_dismissed; }});

    auto top = std::make_unique<RecordingElement>();
    auto top_dismissed = 0;
    root.push_top_layer(std::move(top),
                        TopLayerOptions{.bounds = Rect{30.0F, 30.0F, 30.0F, 20.0F},
                                        .light_dismiss = true,
                                        .on_dismissed = [&top_dismissed]() { ++top_dismissed; }});

    root.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 80.0F});

    EventRouter router(root);
    const auto result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                                .position = Point{20.0F, 20.0F},
                                                                .button = PointerButton::Primary});

    EXPECT_TRUE(result.handled);
    ASSERT_EQ(root.top_layer_count(), 1U);
    EXPECT_EQ(&root.top_layer_at(0U), &bottom_ref);
    EXPECT_EQ(top_dismissed, 1);
    EXPECT_EQ(bottom_dismissed, 0);
}

TEST(UIElementTests, TopLayerEscapeOnlyClosesTopmostCloseableEntry) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(80.0F));
    });

    auto bottom = std::make_unique<RecordingElement>();
    auto bottom_dismissed = 0;
    auto& bottom_ref = root.push_top_layer(
        std::move(bottom),
        TopLayerOptions{.bounds = Rect{10.0F, 10.0F, 80.0F, 50.0F},
                        .light_dismiss = true,
                        .on_dismissed = [&bottom_dismissed]() { ++bottom_dismissed; }});

    auto top = std::make_unique<RecordingElement>();
    auto top_dismissed = 0;
    root.push_top_layer(std::move(top),
                        TopLayerOptions{.bounds = Rect{30.0F, 30.0F, 30.0F, 20.0F},
                                        .light_dismiss = true,
                                        .on_dismissed = [&top_dismissed]() { ++top_dismissed; }});

    root.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 80.0F});

    EventRouter router(root);
    router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Escape});

    ASSERT_EQ(root.top_layer_count(), 1U);
    EXPECT_EQ(&root.top_layer_at(0U), &bottom_ref);
    EXPECT_EQ(top_dismissed, 1);
    EXPECT_EQ(bottom_dismissed, 0);
}

TEST(UIElementTests, TopLayerPersistentBackdropBlocksUnderlyingDismissal) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(80.0F));
    });

    auto bottom = std::make_unique<RecordingElement>();
    auto bottom_dismissed = 0;
    root.push_top_layer(
        std::move(bottom),
        TopLayerOptions{.bounds = Rect{10.0F, 10.0F, 80.0F, 50.0F},
                        .light_dismiss = true,
                        .on_dismissed = [&bottom_dismissed]() { ++bottom_dismissed; }});

    auto top = std::make_unique<RecordingElement>();
    auto& top_ref = static_cast<RecordingElement&>(root.push_top_layer(
        std::move(top), TopLayerOptions{.bounds = Rect{30.0F, 30.0F, 30.0F, 20.0F},
                                        .light_dismiss = false,
                                        .backdrop_color = Color::rgba(0, 0, 0, 64),
                                        .close_on_escape = false}));

    root.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 80.0F});

    EventRouter router(root);
    router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Escape});
    EXPECT_EQ(root.top_layer_count(), 2U);
    EXPECT_EQ(bottom_dismissed, 0);

    const auto result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                                .position = Point{5.0F, 5.0F},
                                                                .button = PointerButton::Primary});

    EXPECT_FALSE(result.handled);
    EXPECT_EQ(result.target, &top_ref);
    EXPECT_EQ(root.top_layer_count(), 2U);
    EXPECT_EQ(bottom_dismissed, 0);
}

TEST(UIElementTests, TopLayerDefaultBoundsUseRootViewportAsLayoutBoundary) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(60.0F));
    });

    auto layer = std::make_unique<UIElement>();
    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(12.0F))
            .set_position(Edge::Top, Length::points(8.0F))
            .set_size(Length::points(30.0F), Length::points(20.0F));
    });
    auto* child_ptr = child.get();
    layer->append_child(std::move(child));

    auto& layer_ref = root.push_top_layer(
        std::move(layer), TopLayerOptions{.light_dismiss = false, .close_on_escape = false});

    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 60.0F});

    const auto bounds = root.top_layer_bounds(layer_ref);
    EXPECT_FLOAT_EQ(bounds.x, 0.0F);
    EXPECT_FLOAT_EQ(bounds.y, 0.0F);
    EXPECT_FLOAT_EQ(bounds.width, 100.0F);
    EXPECT_FLOAT_EQ(bounds.height, 60.0F);
    EXPECT_FLOAT_EQ(layer_ref.absolute_frame().x, 0.0F);
    EXPECT_FLOAT_EQ(layer_ref.absolute_frame().y, 0.0F);
    EXPECT_FLOAT_EQ(layer_ref.absolute_frame().width, 100.0F);
    EXPECT_FLOAT_EQ(layer_ref.absolute_frame().height, 60.0F);
    EXPECT_FLOAT_EQ(child_ptr->absolute_frame().x, 12.0F);
    EXPECT_FLOAT_EQ(child_ptr->absolute_frame().y, 8.0F);
}

TEST(UIElementTests, ModalTopLayerTrapsTabFocus) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(160.0F), Length::points(90.0F));
    });

    auto background = std::make_unique<RecordingElement>();
    background->set_focusable(true);
    background->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(20.0F));
    });
    auto& background_ref = static_cast<RecordingElement&>(root.append_child(std::move(background)));

    root.calculate_layout(LayoutConstraints{.width = 160.0F, .height = 90.0F});
    EventRouter router(root);
    ASSERT_TRUE(router.focus_manager().set_focus(&background_ref));

    auto modal = std::make_unique<UIElement>();
    modal->configure_layout([](LayoutElement& layout) {
        layout.set_flex_direction(FlexDirection::Row)
            .set_gap(Gutter::Column, Length::points(8.0F))
            .set_size(Length::points(90.0F), Length::points(30.0F));
    });

    auto first = std::make_unique<RecordingElement>();
    first->set_focusable(true);
    first->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F));
    });
    auto* first_ptr = first.get();
    modal->append_child(std::move(first));

    auto second = std::make_unique<RecordingElement>();
    second->set_focusable(true);
    second->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(30.0F));
    });
    auto* second_ptr = second.get();
    modal->append_child(std::move(second));

    root.push_top_layer(std::move(modal),
                        TopLayerOptions{.bounds = Rect{20.0F, 20.0F, 100.0F, 40.0F},
                                        .light_dismiss = false,
                                        .close_on_escape = false,
                                        .modal = true});
    root.calculate_layout(LayoutConstraints{.width = 160.0F, .height = 90.0F});

    EXPECT_EQ(router.focus_manager().focused_element(), nullptr);
    EXPECT_FALSE(router.focus_manager().set_focus(&background_ref));

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Tab}).handled);
    EXPECT_EQ(router.focus_manager().focused_element(), first_ptr);

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Tab}).handled);
    EXPECT_EQ(router.focus_manager().focused_element(), second_ptr);

    EXPECT_TRUE(router
                    .route_key_event(KeyEvent{.kind = KeyEventKind::Down,
                                              .key = Key::Tab,
                                              .modifiers = KeyModifiers{.shift = true}})
                    .handled);
    EXPECT_EQ(router.focus_manager().focused_element(), first_ptr);
}

TEST(UIElementTests, ModalTopLayerInvalidatesExistingFocusableCache) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(160.0F), Length::points(90.0F));
    });

    auto background = std::make_unique<RecordingElement>();
    background->set_focusable(true);
    auto& background_ref = static_cast<RecordingElement&>(root.append_child(std::move(background)));

    root.calculate_layout(LayoutConstraints{.width = 160.0F, .height = 90.0F});
    EventRouter router(root);
    ASSERT_TRUE(router.focus_manager().focus_next());
    ASSERT_EQ(router.focus_manager().focused_element(), &background_ref);

    auto modal = std::make_unique<UIElement>();
    auto modal_child = std::make_unique<RecordingElement>();
    modal_child->set_focusable(true);
    auto* modal_child_ptr = modal_child.get();
    modal->append_child(std::move(modal_child));

    root.push_top_layer(std::move(modal), TopLayerOptions{.modal = true});
    root.calculate_layout(LayoutConstraints{.width = 160.0F, .height = 90.0F});

    EXPECT_TRUE(router.focus_manager().focus_next());
    EXPECT_EQ(router.focus_manager().focused_element(), modal_child_ptr);
}

TEST(UIElementTests, BringingModalTopLayerToFrontClearsExternalFocus) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(160.0F), Length::points(90.0F));
    });

    auto first_modal = std::make_unique<UIElement>();
    first_modal->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(70.0F), Length::points(30.0F));
    });
    auto first_focusable = std::make_unique<RecordingElement>();
    first_focusable->set_focusable(true);
    first_focusable->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(30.0F), Length::points(20.0F));
    });
    auto* first_focusable_ptr = first_focusable.get();
    first_modal->append_child(std::move(first_focusable));
    auto& first_modal_ref = root.push_top_layer(
        std::move(first_modal), TopLayerOptions{.bounds = Rect{10.0F, 10.0F, 80.0F, 40.0F},
                                                .close_on_escape = false,
                                                .modal = true});

    auto second_modal = std::make_unique<UIElement>();
    second_modal->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(70.0F), Length::points(30.0F));
    });
    auto second_focusable = std::make_unique<RecordingElement>();
    second_focusable->set_focusable(true);
    second_focusable->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(30.0F), Length::points(20.0F));
    });
    auto* second_focusable_ptr = second_focusable.get();
    second_modal->append_child(std::move(second_focusable));
    root.push_top_layer(std::move(second_modal),
                        TopLayerOptions{.bounds = Rect{50.0F, 20.0F, 80.0F, 40.0F},
                                        .close_on_escape = false,
                                        .modal = true});

    root.calculate_layout(LayoutConstraints{.width = 160.0F, .height = 90.0F});
    EventRouter router(root);
    ASSERT_TRUE(router.focus_manager().set_focus(second_focusable_ptr));

    root.bring_top_layer_to_front(first_modal_ref);

    EXPECT_EQ(router.focus_manager().focused_element(), nullptr);
    EXPECT_FALSE(router.focus_manager().set_focus(second_focusable_ptr));
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Tab}).handled);
    EXPECT_EQ(router.focus_manager().focused_element(), first_focusable_ptr);
}

TEST(UIElementTests, LogicalOwnerAllowsOwnedPopupFocusInsideModalScope) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(120.0F));
    });

    auto background = std::make_unique<RecordingElement>();
    background->set_focusable(true);
    background->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(20.0F));
    });
    auto& background_ref = static_cast<RecordingElement&>(root.append_child(std::move(background)));

    auto modal = std::make_unique<UIElement>();
    modal->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(60.0F));
    });

    auto owner = std::make_unique<RecordingElement>();
    owner->set_focusable(true);
    owner->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(60.0F), Length::points(24.0F));
    });
    auto& owner_ref = static_cast<RecordingElement&>(modal->append_child(std::move(owner)));

    root.push_top_layer(std::move(modal),
                        TopLayerOptions{.bounds = Rect{20.0F, 20.0F, 130.0F, 70.0F},
                                        .light_dismiss = false,
                                        .close_on_escape = false,
                                        .modal = true});
    root.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 120.0F});

    EventRouter router(root);
    EXPECT_FALSE(router.focus_manager().set_focus(&background_ref));
    ASSERT_TRUE(router.focus_manager().set_focus(&owner_ref));

    auto popup = std::make_unique<UIElement>();
    popup->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(80.0F), Length::points(30.0F));
    });
    auto popup_child = std::make_unique<RecordingElement>();
    popup_child->set_focusable(true);
    popup_child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(40.0F), Length::points(20.0F));
    });
    auto* popup_child_ptr = popup_child.get();
    popup->append_child(std::move(popup_child));

    PopupManager popup_manager(owner_ref);
    const auto opened =
        popup_manager.open(std::move(popup), PopupOptions{.anchor_rect = owner_ref.absolute_frame(),
                                                          .size = Size{80.0F, 30.0F},
                                                          .placement = PopupPlacement::BottomStart,
                                                          .light_dismiss = true});
    root.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 120.0F});

    ASSERT_TRUE(opened.handle.valid());
    EXPECT_TRUE(router.focus_manager().set_focus(popup_child_ptr));
    EXPECT_EQ(router.focus_manager().focused_element(), popup_child_ptr);

    EXPECT_TRUE(router
                    .route_key_event(KeyEvent{.kind = KeyEventKind::Down,
                                              .key = Key::Tab,
                                              .modifiers = KeyModifiers{.shift = true}})
                    .handled);
    EXPECT_EQ(router.focus_manager().focused_element(), &owner_ref);
}

TEST(UIElementTests, BringingTopLayerOwnerToFrontKeepsLogicalDescendantsAboveIt) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(120.0F));
    });

    auto owner_layer = std::make_unique<RecordingElement>();
    auto& owner_layer_ref = static_cast<RecordingElement&>(root.push_top_layer(
        std::move(owner_layer), TopLayerOptions{.bounds = Rect{10.0F, 10.0F, 80.0F, 60.0F},
                                                .light_dismiss = false,
                                                .close_on_escape = false}));

    auto owned_layer = std::make_unique<RecordingElement>();
    auto& owned_layer_ref = static_cast<RecordingElement&>(root.push_top_layer(
        std::move(owned_layer), TopLayerOptions{.bounds = Rect{20.0F, 20.0F, 40.0F, 30.0F},
                                                .light_dismiss = false,
                                                .close_on_escape = false,
                                                .logical_owner = &owner_layer_ref}));

    auto unrelated_layer = std::make_unique<RecordingElement>();
    auto& unrelated_layer_ref = static_cast<RecordingElement&>(root.push_top_layer(
        std::move(unrelated_layer), TopLayerOptions{.bounds = Rect{70.0F, 30.0F, 50.0F, 40.0F},
                                                    .light_dismiss = false,
                                                    .close_on_escape = false}));

    ASSERT_EQ(root.top_layer_count(), 3U);
    EXPECT_EQ(&root.top_layer_at(0U), &owner_layer_ref);
    EXPECT_EQ(&root.top_layer_at(1U), &owned_layer_ref);
    EXPECT_EQ(&root.top_layer_at(2U), &unrelated_layer_ref);

    root.bring_top_layer_to_front(owner_layer_ref);

    ASSERT_EQ(root.top_layer_count(), 3U);
    EXPECT_EQ(&root.top_layer_at(0U), &unrelated_layer_ref);
    EXPECT_EQ(&root.top_layer_at(1U), &owner_layer_ref);
    EXPECT_EQ(&root.top_layer_at(2U), &owned_layer_ref);
}

TEST(UIElementTests, RemovingLogicalOwnerRemovesOwnedTopLayerEntries) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(140.0F), Length::points(90.0F));
    });

    auto owner = std::make_unique<RecordingElement>();
    owner->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(50.0F), Length::points(20.0F));
    });
    auto& owner_ref = static_cast<RecordingElement&>(root.append_child(std::move(owner)));
    root.calculate_layout(LayoutConstraints{.width = 140.0F, .height = 90.0F});

    auto dismissed = 0;
    PopupManager popup_manager(owner_ref);
    const auto opened =
        popup_manager.open(std::make_unique<RecordingElement>(),
                           PopupOptions{.anchor_rect = owner_ref.absolute_frame(),
                                        .size = Size{50.0F, 24.0F},
                                        .placement = PopupPlacement::BottomStart,
                                        .light_dismiss = true,
                                        .on_dismissed = [&dismissed]() { ++dismissed; }});
    root.calculate_layout(LayoutConstraints{.width = 140.0F, .height = 90.0F});

    ASSERT_TRUE(opened.handle.valid());
    ASSERT_EQ(root.top_layer_count(), 1U);

    auto removed_owner = root.remove_child(owner_ref);
    removed_owner.reset();

    EXPECT_EQ(root.top_layer_count(), 0U);
    EXPECT_EQ(dismissed, 1);
}

TEST(UIElementTests, TopLayerDismissCallbackMayMutateTopLayerStack) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(140.0F), Length::points(90.0F));
    });

    auto dismissed = 0;
    auto& dismissed_layer = root.push_top_layer(
        std::make_unique<RecordingElement>(),
        TopLayerOptions{.bounds = Rect{10.0F, 10.0F, 30.0F, 20.0F},
                        .light_dismiss = false,
                        .on_dismissed = [&]() {
                            ++dismissed;
                            root.push_top_layer(
                                std::make_unique<RecordingElement>(),
                                TopLayerOptions{.bounds = Rect{70.0F, 10.0F, 30.0F, 20.0F},
                                                .light_dismiss = false});
                        }});
    auto& stable_layer = root.push_top_layer(
        std::make_unique<RecordingElement>(),
        TopLayerOptions{.bounds = Rect{40.0F, 10.0F, 30.0F, 20.0F}, .light_dismiss = false});

    auto removed = root.remove_top_layer(dismissed_layer);

    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(dismissed, 1);
    ASSERT_EQ(root.top_layer_count(), 2U);
    EXPECT_EQ(&root.top_layer_at(0U), &stable_layer);
}

TEST(UIElementTests, PlacementEngineFlipsAndShiftsIntoViewport) {
    const auto result = PlacementEngine::place(
        PopupPlacementOptions{.anchor_rect = Rect{80.0F, 80.0F, 10.0F, 10.0F},
                              .popup_size = Size{40.0F, 30.0F},
                              .viewport_rect = Rect{0.0F, 0.0F, 100.0F, 100.0F},
                              .preferred_placement = PopupPlacement::BottomStart,
                              .gap = 4.0F,
                              .viewport_margin = 4.0F});

    EXPECT_EQ(result.placement, PopupPlacement::TopStart);
    EXPECT_TRUE(result.flipped);
    EXPECT_TRUE(result.shifted);
    EXPECT_FLOAT_EQ(result.bounds.x, 56.0F);
    EXPECT_FLOAT_EQ(result.bounds.y, 46.0F);
    EXPECT_FLOAT_EQ(result.bounds.width, 40.0F);
    EXPECT_FLOAT_EQ(result.bounds.height, 30.0F);
}

TEST(UIElementTests, PlacementEngineCanMatchAnchorWidth) {
    const auto result = PlacementEngine::place(
        PopupPlacementOptions{.anchor_rect = Rect{10.0F, 10.0F, 96.0F, 24.0F},
                              .popup_size = Size{40.0F, 30.0F},
                              .viewport_rect = Rect{0.0F, 0.0F, 200.0F, 120.0F},
                              .preferred_placement = PopupPlacement::BottomStart,
                              .match_anchor_width = true});

    EXPECT_FLOAT_EQ(result.bounds.width, 96.0F);
    EXPECT_FLOAT_EQ(result.bounds.x, 10.0F);
}

TEST(UIElementTests, PopupManagerOpenForAnchorUsesAnchorGeometryAndLogicalOwner) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(120.0F));
    });
    auto anchor = std::make_unique<RecordingElement>();
    anchor->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(16.0F))
            .set_position(Edge::Top, Length::points(20.0F))
            .set_size(Length::points(84.0F), Length::points(28.0F));
    });
    auto& anchor_ref = static_cast<RecordingElement&>(root.append_child(std::move(anchor)));
    root.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 120.0F});

    PopupManager popup_manager(root);
    const auto opened =
        popup_manager.open_for_anchor(anchor_ref, std::make_unique<RecordingElement>(),
                                      PopupOptions{.size = Size{42.0F, 22.0F},
                                                   .placement = PopupPlacement::BottomStart,
                                                   .match_anchor_width = true,
                                                   .light_dismiss = false});
    root.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 120.0F});

    ASSERT_TRUE(opened.handle.valid());
    ASSERT_EQ(root.top_layer_count(), 1U);
    EXPECT_FLOAT_EQ(root.top_layer_at(0U).absolute_frame().x, anchor_ref.absolute_frame().x);
    EXPECT_FLOAT_EQ(root.top_layer_at(0U).absolute_frame().width,
                    anchor_ref.absolute_frame().width);

    auto removed_anchor = root.remove_child(anchor_ref);
    removed_anchor.reset();
    EXPECT_EQ(root.top_layer_count(), 0U);
}

TEST(UIElementTests, PopupManagerOpensUpdatesAndClosesTopLayerEntry) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(90.0F));
    });
    root.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 90.0F});

    PopupManager manager(root);
    auto popup = std::make_unique<RecordingElement>();
    const auto opened =
        manager.open(std::move(popup), PopupOptions{.anchor_rect = Rect{10.0F, 12.0F, 20.0F, 18.0F},
                                                    .size = Size{36.0F, 20.0F},
                                                    .placement = PopupPlacement::BottomStart,
                                                    .gap = 2.0F,
                                                    .light_dismiss = false});
    root.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 90.0F});

    ASSERT_TRUE(opened.handle.valid());
    ASSERT_NE(manager.element(opened.handle), nullptr);
    EXPECT_EQ(root.top_layer_count(), 1U);
    EXPECT_FLOAT_EQ(manager.element(opened.handle)->absolute_frame().x, 10.0F);
    EXPECT_FLOAT_EQ(manager.element(opened.handle)->absolute_frame().y, 32.0F);

    EXPECT_TRUE(manager.update_placement(
        opened.handle, PopupOptions{.anchor_rect = Rect{70.0F, 70.0F, 12.0F, 12.0F},
                                    .size = Size{36.0F, 20.0F},
                                    .placement = PopupPlacement::BottomStart,
                                    .gap = 2.0F,
                                    .light_dismiss = false}));
    root.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 90.0F});
    EXPECT_EQ(root.top_layer_count(), 1U);
    EXPECT_EQ(manager.element(opened.handle), &root.top_layer_at(0U));
    EXPECT_TRUE(manager.close(opened.handle));
    EXPECT_EQ(root.top_layer_count(), 0U);
}

TEST(UIElementTests, PopupManagerDefersCloseDuringEventDispatch) {
    class ClosingElement final : public UIElement {
      public:
        using UIElement::UIElement;

        PopupHandle handle;
        std::vector<PointerEventKind> pointer_kinds;

      protected:
        void on_pointer_event(PointerEvent& event) override {
            pointer_kinds.push_back(event.kind);
            if (event.kind == PointerEventKind::Down) {
                PopupManager manager(*this);
                static_cast<void>(manager.close(handle));
                event.handled = true;
            }
        }

        void on_paint(RenderContext& context, Rect absolute_frame) const override {
            context.stroke_rect(absolute_frame, Color::rgba(64, 158, 255), 1.0F);
        }
    };

    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(80.0F));
    });
    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 80.0F});

    PopupManager manager(root);
    auto popup = std::make_unique<ClosingElement>();
    auto* popup_ptr = popup.get();
    const auto opened =
        manager.open(std::move(popup), PopupOptions{.anchor_rect = Rect{10.0F, 10.0F, 1.0F, 1.0F},
                                                    .size = Size{30.0F, 20.0F},
                                                    .gap = 0.0F,
                                                    .light_dismiss = true,
                                                    .preserve_focus = true});
    popup_ptr->handle = opened.handle;
    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 80.0F});

    EventRouter router(root);
    const auto result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                                .position = Point{12.0F, 12.0F},
                                                                .button = PointerButton::Primary});

    EXPECT_TRUE(result.handled);
    EXPECT_EQ(result.target, nullptr);
    EXPECT_EQ(root.top_layer_count(), 0U);
}

TEST(UIElementTests, CommitsRenderCommandsAndCollectsLocalDirtyRegion) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(60.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(12.0F))
            .set_position(Edge::Top, Length::points(8.0F))
            .set_size(Length::points(30.0F), Length::points(20.0F));
    });
    auto& child_ref = root.append_child(std::move(child));

    LayoutConstraints constraints;
    constraints.width = 100.0F;
    constraints.height = 60.0F;
    root.calculate_layout(constraints);
    root.clear_paint_dirty_subtree();

    child_ref.invalidate_paint();

    RenderCommandList command_list;
    DirtyRegion dirty_region;
    root.commit_render_commands(command_list, &dirty_region);

    EXPECT_EQ(command_list.commands().size(), 2U);
    ASSERT_EQ(dirty_region.rects().size(), 1U);
    EXPECT_FLOAT_EQ(dirty_region.rects()[0].x, 12.0F);
    EXPECT_FLOAT_EQ(dirty_region.rects()[0].y, 8.0F);
    EXPECT_FLOAT_EQ(dirty_region.rects()[0].width, 30.0F);
    EXPECT_FLOAT_EQ(dirty_region.rects()[0].height, 20.0F);
}

TEST(UIElementTests, ReusesCleanSubtreeCommandCacheOnCommit) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_flex_direction(FlexDirection::Row)
            .set_size(Length::points(100.0F), Length::points(40.0F));
    });

    auto left_count = 0;
    auto right_count = 0;
    auto left = std::make_unique<CountingPaintElement>(left_count);
    left->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(50.0F), Length::points(40.0F));
    });
    auto right = std::make_unique<CountingPaintElement>(right_count);
    right->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(50.0F), Length::points(40.0F));
    });

    auto& left_ref = root.append_child(std::move(left));
    root.append_child(std::move(right));

    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 40.0F});

    RenderCommandList first_commit;
    root.commit_render_commands(first_commit, nullptr);
    root.clear_paint_dirty_subtree();

    ASSERT_EQ(first_commit.commands().size(), 2U);
    EXPECT_EQ(left_count, 1);
    EXPECT_EQ(right_count, 1);

    left_ref.invalidate_paint();

    RenderCommandList second_commit;
    root.commit_render_commands(second_commit, nullptr);

    EXPECT_EQ(second_commit.commands().size(), 2U);
    EXPECT_EQ(left_count, 2);
    EXPECT_EQ(right_count, 1);
}

TEST(UIElementTests, LayeredElementWrapsContentCommandsAndInvalidatesPaint) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(40.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(50.0F), Length::points(30.0F));
    });
    child->set_opacity(0.5F).set_render_transform(Transform2D::translation(8.0F, 0.0F));
    auto& child_ref = root.append_child(std::move(child));

    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 40.0F});

    RenderCommandList command_list;
    root.commit_render_commands(command_list, nullptr);

    ASSERT_EQ(command_list.commands().size(), 4U);
    EXPECT_EQ(command_list.commands()[0].type(), RenderCommandType::StrokeRect);
    EXPECT_EQ(command_list.commands()[1].type(), RenderCommandType::PushLayer);
    EXPECT_EQ(command_list.commands()[2].type(), RenderCommandType::StrokeRect);
    EXPECT_EQ(command_list.commands()[3].type(), RenderCommandType::PopLayer);
    const auto& layer = command_as<PushLayerCommand>(command_list.commands()[1]).options;
    EXPECT_FLOAT_EQ(layer.opacity, 0.5F);
    EXPECT_FLOAT_EQ(layer.bounds.width, 50.0F);
    EXPECT_FALSE(is_identity_transform(layer.transform));

    root.clear_paint_dirty_subtree();
    child_ref.set_layer_enabled(true);
    EXPECT_TRUE(root.needs_paint());
}

TEST(UIElementTests, CommitRenderSceneBuildsRetainedLayerNodes) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(40.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(50.0F), Length::points(30.0F));
    });
    child->set_opacity(0.5F).set_render_transform(Transform2D::translation(8.0F, 0.0F));
    root.append_child(std::move(child));

    root.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 40.0F});

    RenderScene scene;
    root.commit_render_scene(scene, nullptr);

    ASSERT_NE(scene.root(), nullptr);
    ASSERT_EQ(scene.root()->children.size(), 1U);
    const auto& root_content = scene.root()->children.front();
    ASSERT_EQ(root_content.children.size(), 1U);
    const auto& child_layer = root_content.children.front();
    EXPECT_EQ(child_layer.kind, RenderNodeKind::Layer);
    EXPECT_FLOAT_EQ(child_layer.opacity, 0.5F);
    EXPECT_FLOAT_EQ(child_layer.bounds.width, 50.0F);
    EXPECT_FALSE(is_identity_transform(child_layer.transform));
}

TEST(UIElementTests, TracksMeasureAndPaintInvalidation) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    auto label = std::make_unique<UIElement>();
    label->configure_layout([](LayoutElement& item) { item.set_element_kind(ElementKind::Text); });

    Size measured_size{40.0F, 10.0F};
    label->set_measure_callback([&](const MeasureInput&) { return measured_size; });
    auto& label_ref = root.append_child(std::move(label));

    LayoutConstraints constraints;
    constraints.width = 100.0F;
    root.calculate_layout(constraints);
    EXPECT_FLOAT_EQ(label_ref.frame().width, 40.0F);
    EXPECT_TRUE(root.needs_paint());

    root.clear_paint_dirty_subtree();
    EXPECT_FALSE(root.needs_paint());
    EXPECT_FALSE(label_ref.needs_paint());

    measured_size = {80.0F, 12.0F};
    label_ref.mark_measure_dirty();
    EXPECT_TRUE(root.needs_layout());

    root.calculate_layout(constraints);
    EXPECT_FLOAT_EQ(label_ref.frame().width, 80.0F);
    EXPECT_TRUE(label_ref.needs_paint());
}

TEST(UIElementTests, DirectLayoutMutationInvalidatesUiTree) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    auto child = std::make_unique<UIElement>();
    auto& child_ref = root.append_child(std::move(child));

    LayoutConstraints constraints;
    constraints.width = 100.0F;
    constraints.height = 40.0F;
    root.calculate_layout(constraints);
    root.clear_paint_dirty_subtree();

    child_ref.configure_layout(
        [](LayoutElement& layout) { layout.set_width(Length::points(60.0F)); });

    EXPECT_TRUE(child_ref.needs_layout());
    EXPECT_TRUE(root.needs_layout());
    EXPECT_TRUE(root.needs_paint());
}

TEST(UIElementTests, RelayoutBoundaryKeepsDirtyRootTightButSchedulesRoot) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(80.0F));
    });

    auto boundary = std::make_unique<UIElement>();
    boundary->set_relayout_boundary(true);
    boundary->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(80.0F), Length::points(40.0F));
    });
    auto leaf = std::make_unique<UIElement>();
    auto& leaf_ref = boundary->append_child(std::move(leaf));
    auto& boundary_ref = root.append_child(std::move(boundary));

    const auto constraints = LayoutConstraints{.width = 120.0F, .height = 80.0F};
    root.calculate_layout(constraints);
    root.clear_paint_dirty_subtree();

    leaf_ref.configure_layout(
        [](LayoutElement& layout) { layout.set_width(Length::points(30.0F)); });

    EXPECT_TRUE(leaf_ref.needs_layout());
    EXPECT_TRUE(boundary_ref.needs_layout());
    EXPECT_TRUE(root.needs_layout());

    root.calculate_layout(constraints);
    EXPECT_TRUE(boundary_ref.needs_paint());
    EXPECT_FALSE(root.needs_layout());
}

TEST(UIElementTests, RepaintBoundaryUsesLayerDirtyRegion) {
    auto engine = create_unrounded_engine();
    UIElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(80.0F));
    });

    auto boundary = std::make_unique<UIElement>();
    boundary->set_repaint_boundary(true);
    boundary->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(80.0F), Length::points(40.0F));
    });
    auto leaf = std::make_unique<UIElement>();
    auto& leaf_ref = boundary->append_child(std::move(leaf));
    auto& boundary_ref = root.append_child(std::move(boundary));

    root.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 80.0F});
    RenderCommandList commands;
    root.commit_render_commands(commands);
    root.clear_paint_dirty_subtree();

    leaf_ref.invalidate_paint();

    DirtyRegion dirty_region;
    RenderCommandList next_commands;
    root.commit_render_commands(next_commands, &dirty_region);

    ASSERT_FALSE(dirty_region.empty());
    EXPECT_FLOAT_EQ(dirty_region.bounds().width, boundary_ref.absolute_frame().width);
    EXPECT_FLOAT_EQ(dirty_region.bounds().height, boundary_ref.absolute_frame().height);
}

TEST(UIElementTests, RenderObjectSnapshotAndImplicitPropertyAnimationStayBehindUIElement) {
    auto engine = create_unrounded_engine();
    UIElement element;
    element.bind_layout_tree(engine);
    element.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(40.0F));
    });
    element.calculate_layout(LayoutConstraints{.width = 100.0F, .height = 40.0F});

    const auto snapshot = element.render_object_snapshot();
    EXPECT_EQ(snapshot.role, ElementTreeRole::RenderObject);
    EXPECT_FLOAT_EQ(snapshot.frame.width, 100.0F);

    const auto opacity_property = winelement::core::make_property_metadata<float>(
        "implicit.opacity", winelement::core::PropertyInvalidation::Paint);
    element.clear_paint_dirty_subtree();
    element.set_property(opacity_property, 0.0F);
    element.clear_paint_dirty_subtree();
    element.animate_property<float>(opacity_property, 1.0F,
                                    winelement::animation::make_transition_timing(
                                        winelement::animation::AnimationDuration{1.0F},
                                        winelement::animation::EasingFunction::linear()));
    EXPECT_TRUE(element.has_running_animations());

    const auto now = winelement::animation::AnimationClockType::now();
    EXPECT_TRUE(element.tick_animations(now + std::chrono::milliseconds(500)));
    EXPECT_GT(element.properties().value(opacity_property, 0.0F), 0.0F);
    EXPECT_TRUE(element.needs_paint());
}

TEST(UIElementTests, SnapshotSeparatesElementAndRenderObjectState) {
    auto engine = create_unrounded_engine();
    UIElement element;
    element.bind_layout_tree(engine);
    auto child = std::make_unique<UIElement>();
    child->set_visible(false);
    auto& child_ref = *child;
    element.append_child(std::move(child));
    element.set_theme_class("panel");
    element.set_relayout_boundary(true);
    element.set_repaint_boundary(true);
    element.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(64.0F), Length::points(32.0F));
    });

    element.calculate_layout();

    const auto element_snapshot = element.element_snapshot();
    const auto render_snapshot = element.render_object_snapshot();

    EXPECT_EQ(element_snapshot.role, ElementTreeRole::Element);
    EXPECT_EQ(element_snapshot.theme_class, "panel");
    EXPECT_TRUE(element_snapshot.relayout_boundary);
    EXPECT_FALSE(element_snapshot.type_name.empty());
    ASSERT_EQ(element_snapshot.children.size(), 1U);
    EXPECT_EQ(element_snapshot.children[0].visible, child_ref.visible());
    EXPECT_TRUE(render_snapshot.repaint_boundary);
    EXPECT_TRUE(render_snapshot.has_layer);
    EXPECT_FLOAT_EQ(render_snapshot.frame.width, 64.0F);
}

TEST(UIElementTests, GestureArenaAcceptsFirstResolvedRecognizer) {
    GestureArena arena;
    auto tap_count = 0;
    auto drag_count = 0;
    arena.add(std::make_unique<TapGestureRecognizer>([&](const PointerEvent&) { ++tap_count; }));
    arena.add(
        std::make_unique<DragGestureRecognizer>([&](const PointerEvent&) { ++drag_count; }, 4.0F));

    arena.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                           .position = Point{4.0F, 4.0F},
                                           .button = PointerButton::Primary});
    arena.route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                           .position = Point{20.0F, 4.0F},
                                           .button = PointerButton::Primary});
    arena.route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                           .position = Point{20.0F, 4.0F},
                                           .button = PointerButton::Primary});

    ASSERT_TRUE(arena.accepted_recognizer().has_value());
    EXPECT_EQ(*arena.accepted_recognizer(), 1U);
    EXPECT_EQ(tap_count, 0);
    EXPECT_EQ(drag_count, 1);
    EXPECT_EQ(arena.disposition(0), GestureDisposition::Rejected);
    EXPECT_EQ(arena.disposition(1), GestureDisposition::Accepted);
}

TEST(UIElementTests, RemovingFocusedCapturedSubtreeClearsManagers) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(60.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->set_focusable(true);
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(10.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(40.0F), Length::points(20.0F));
    });

    auto& child_ref = static_cast<RecordingElement&>(root.append_child(std::move(child)));

    LayoutConstraints constraints;
    constraints.width = 120.0F;
    constraints.height = 60.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    EXPECT_TRUE(router.capture_pointer(child_ref));
    ASSERT_NE(router.pointer_capture(), nullptr);
    EXPECT_EQ(router
                  .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                    .position = Point{15.0F, 15.0F},
                                                    .button = PointerButton::Primary})
                  .target,
              &child_ref);
    EXPECT_EQ(router.focus_manager().focused_element(), &child_ref);

    auto removed = root.remove_child(child_ref);

    EXPECT_EQ(router.pointer_capture(), nullptr);
    EXPECT_EQ(router.focus_manager().focused_element(), nullptr);
    EXPECT_FALSE(static_cast<RecordingElement&>(*removed).focused());

    router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Enter});
    EXPECT_TRUE(static_cast<RecordingElement&>(*removed).key_records.empty());
}

TEST(UIElementTests, HidingFocusedSubtreeClearsFocusAndCapture) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(60.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->set_focusable(true);
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_size(Length::points(40.0F), Length::points(20.0F));
    });

    auto& child_ref = static_cast<RecordingElement&>(root.append_child(std::move(child)));

    LayoutConstraints constraints;
    constraints.width = 120.0F;
    constraints.height = 60.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    EXPECT_TRUE(router.capture_pointer(child_ref));
    ASSERT_NE(router.pointer_capture(), nullptr);
    EXPECT_TRUE(router.focus_manager().set_focus(&child_ref));

    child_ref.set_visible(false);

    EXPECT_EQ(router.pointer_capture(), nullptr);
    EXPECT_EQ(router.focus_manager().focused_element(), nullptr);
    EXPECT_FALSE(child_ref.focused());
}

TEST(UIElementTests, DestroyingRoutedRootDetachesManagersBeforeRouterDestruction) {
    auto engine = create_unrounded_engine();
    auto root = std::make_unique<RecordingElement>();
    root->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(80.0F), Length::points(40.0F));
    });
    root->set_focusable(true);

    LayoutConstraints constraints;
    constraints.width = 80.0F;
    constraints.height = 40.0F;
    root->calculate_layout(constraints);

    auto router = std::make_unique<EventRouter>(*root);
    EXPECT_TRUE(router->capture_pointer(*root));
    EXPECT_TRUE(router->focus_manager().set_focus(root.get()));

    root.reset();

    EXPECT_EQ(router->pointer_capture(), nullptr);
    EXPECT_EQ(router->focus_manager().focused_element(), nullptr);
}

TEST(UIElementTests, SynthesizesClickAfterDownUpOnSameTarget) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(60.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(10.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(40.0F), Length::points(20.0F));
    });
    auto& child_ref = static_cast<RecordingElement&>(root.append_child(std::move(child)));

    LayoutConstraints constraints;
    constraints.width = 120.0F;
    constraints.height = 60.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                            .position = Point{15.0F, 15.0F},
                                            .button = PointerButton::Primary,
                                            .primary_button_down = true});
    const auto up_result = router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                                   .position = Point{15.0F, 15.0F},
                                                                   .button = PointerButton::Primary,
                                                                   .click_count = 1});

    EXPECT_EQ(up_result.target, &child_ref);
    EXPECT_EQ(child_ref.pointer_kinds,
              std::vector<PointerEventKind>(
                  {PointerEventKind::Down, PointerEventKind::Up, PointerEventKind::Click}));
    EXPECT_EQ(child_ref.pointer_buttons.back(), PointerButton::Primary);
    EXPECT_EQ(child_ref.click_counts.back(), 1U);
    EXPECT_TRUE(child_ref.primary_button_states.front());
}

TEST(UIElementTests, DoesNotSynthesizeClickWhenReleaseLeavesPressedTarget) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(60.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(10.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(40.0F), Length::points(20.0F));
    });
    auto& child_ref = static_cast<RecordingElement&>(root.append_child(std::move(child)));

    LayoutConstraints constraints;
    constraints.width = 120.0F;
    constraints.height = 60.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                            .position = Point{15.0F, 15.0F},
                                            .button = PointerButton::Primary});
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                            .position = Point{80.0F, 40.0F},
                                            .button = PointerButton::Primary});

    EXPECT_EQ(child_ref.pointer_kinds, std::vector<PointerEventKind>({PointerEventKind::Down}));
}

TEST(UIElementTests, RoutesDoubleClickAndWheelMetadata) {
    auto engine = create_unrounded_engine();
    RecordingElement root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(60.0F));
    });

    auto child = std::make_unique<RecordingElement>();
    child->set_focusable(true);
    child->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(10.0F))
            .set_position(Edge::Top, Length::points(10.0F))
            .set_size(Length::points(40.0F), Length::points(20.0F));
    });
    auto& child_ref = static_cast<RecordingElement&>(root.append_child(std::move(child)));

    LayoutConstraints constraints;
    constraints.width = 120.0F;
    constraints.height = 60.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::DoubleClick,
                                            .position = Point{15.0F, 15.0F},
                                            .button = PointerButton::X1,
                                            .click_count = 2,
                                            .x1_button_down = true});
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Wheel,
                                            .position = Point{15.0F, 15.0F},
                                            .wheel_delta = Point{0.0F, 1.0F}});
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::HorizontalWheel,
                                            .position = Point{15.0F, 15.0F},
                                            .wheel_delta = Point{-1.0F, 0.0F}});

    EXPECT_EQ(router.focus_manager().focused_element(), &child_ref);
    ASSERT_EQ(child_ref.pointer_kinds.size(), 3U);
    EXPECT_EQ(child_ref.pointer_kinds[0], PointerEventKind::DoubleClick);
    EXPECT_EQ(child_ref.pointer_buttons[0], PointerButton::X1);
    EXPECT_EQ(child_ref.click_counts[0], 2U);
    EXPECT_EQ(child_ref.pointer_kinds[1], PointerEventKind::Wheel);
    EXPECT_FLOAT_EQ(child_ref.wheel_deltas[1].y, 1.0F);
    EXPECT_EQ(child_ref.pointer_kinds[2], PointerEventKind::HorizontalWheel);
    EXPECT_FLOAT_EQ(child_ref.wheel_deltas[2].x, -1.0F);
}

TEST(UIElementTests, DeclarativeBuilderCreatesInspectableTrees) {
    auto engine = create_unrounded_engine();
    auto child_builder = element<UIElement>();
    child_builder.with([](UIElement& child) {
        child.set_semantics_role(SemanticsRole::Button).set_semantics_label("Run");
    });
    auto root_builder = element<UIElement>();
    root_builder.with([](UIElement& root) { root.set_theme_class("sample.root"); })
        .layout([](LayoutElement& layout) {
            layout.set_size(Length::points(120.0F), Length::points(40.0F));
        })
        .child(std::move(child_builder));

    auto root = root_builder.build();
    root->calculate_layout(LayoutConstraints{.width = 120.0F, .height = 40.0F});

    const auto inspection = ElementInspector{}.inspect(*root);

    EXPECT_EQ(root->child_count(), 1U);
    EXPECT_EQ(inspection.theme_class, "sample.root");
    ASSERT_EQ(inspection.children.size(), 1U);
    EXPECT_EQ(inspection.children.front().role, SemanticsRole::Button);
    EXPECT_EQ(ElementInspector::count_nodes(inspection), 2U);
}

TEST(UIElementTests, FrameRateMonitorAndUiaAdapterExposeDebugSnapshots) {
    FrameRateMonitor monitor;
    const auto start = FrameRateMonitor::Clock::now();
    monitor.sample(start);
    monitor.sample(start + std::chrono::milliseconds(16));

    const auto fps = monitor.snapshot();
    EXPECT_EQ(fps.sample_count, 2U);
    EXPECT_GT(fps.frames_per_second, 0.0);

    const auto node = SemanticsNode{.role = SemanticsRole::TextInput,
                                    .label = "Search",
                                    .value = "element",
                                    .bounds = Rect{1.0F, 2.0F, 30.0F, 12.0F}};
    const auto automation = UiaSemanticsAdapter{}.convert(node);

    EXPECT_EQ(automation.control_type, AutomationControlType::Edit);
    EXPECT_EQ(automation.name, "Search");
    EXPECT_EQ(automation.value, "element");
}

} // namespace
