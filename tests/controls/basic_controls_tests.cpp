#include <winelement/controls.hpp>
#include <winelement/layout.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace winelement::controls;
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

[[nodiscard]] std::string_view command_text(const RenderOpcodeRecord& command) {
    if (command.type() == RenderCommandType::DrawText) {
        return command.payload<DrawTextCommand>().text;
    }
    if (command.type() == RenderCommandType::DrawTextLayout) {
        const auto* layout = command.payload<DrawTextLayoutCommand>().layout_value();
        return layout != nullptr ? std::string_view{layout->text} : std::string_view{};
    }
    return {};
}

[[nodiscard]] const TextStyle& command_text_style(const RenderOpcodeRecord& command) {
    if (command.type() == RenderCommandType::DrawText) {
        return command.payload<DrawTextCommand>().style;
    }
    const auto* layout = command.payload<DrawTextLayoutCommand>().layout_value();
    static const auto fallback = TextStyle{};
    return layout != nullptr ? layout->style : fallback;
}

[[nodiscard]] Color command_fill_color(const RenderOpcodeRecord& command) {
    switch (command.type()) {
    case RenderCommandType::FillRect:
        return command.payload<FillRectCommand>().color;
    case RenderCommandType::FillPixelSnappedRect:
        return command.payload<FillPixelSnappedRectCommand>().color;
    case RenderCommandType::FillRoundedRect:
        return command.payload<FillRoundedRectCommand>().color;
    default:
        return {};
    }
}

[[nodiscard]] Color command_stroke_color(const RenderOpcodeRecord& command) {
    switch (command.type()) {
    case RenderCommandType::StrokeRect:
        return command.payload<StrokeRectCommand>().color;
    case RenderCommandType::StrokeRoundedRect:
        return command.payload<StrokeRoundedRectCommand>().color;
    default:
        return {};
    }
}

[[nodiscard]] Rect command_rect(const RenderOpcodeRecord& command) {
    switch (command.type()) {
    case RenderCommandType::FillRect:
        return command.payload<FillRectCommand>().rect;
    case RenderCommandType::FillPixelSnappedRect:
        return command.payload<FillPixelSnappedRectCommand>().rect;
    case RenderCommandType::StrokeRect:
        return command.payload<StrokeRectCommand>().rect;
    case RenderCommandType::FillRoundedRect:
        return command.payload<FillRoundedRectCommand>().rect;
    case RenderCommandType::StrokeRoundedRect:
        return command.payload<StrokeRoundedRectCommand>().rect;
    case RenderCommandType::DrawText:
        return command.payload<DrawTextCommand>().rect;
    case RenderCommandType::DrawTextLayout: {
        const auto& text_layout = command.payload<DrawTextLayoutCommand>();
        const auto* layout = text_layout.layout_value();
        return layout != nullptr ? Rect{text_layout.origin.x, text_layout.origin.y,
                                        layout->size.width, layout->size.height}
                                 : Rect{};
    }
    case RenderCommandType::DrawBoxShadow:
        return command.payload<DrawBoxShadowCommand>().rect;
    case RenderCommandType::DrawImage:
        return command.payload<DrawImageCommand>().options.destination;
    default:
        return {};
    }
}

[[nodiscard]] Point rect_center(Rect rect) noexcept {
    return Point{rect.x + rect.width * 0.5F, rect.y + rect.height * 0.5F};
}

[[nodiscard]] LayoutEngine create_unrounded_engine() {
    LayoutEngineOptions options;
    options.point_scale_factor = 0.0F;
    return LayoutEngine(options);
}

[[nodiscard]] const RenderOpcodeRecord* find_command(const RenderCommandRecorder& context,
                                                     RenderCommandType type,
                                                     std::string_view text = {}) {
    const auto& commands = context.commands();
    const auto iterator = std::find_if(commands.begin(), commands.end(), [&](const auto& command) {
        return command.type() == type && (text.empty() || command_text(command) == text);
    });
    return iterator == commands.end() ? nullptr : &*iterator;
}

[[nodiscard]] std::size_t find_command_index(const RenderCommandRecorder& context,
                                             RenderCommandType type,
                                             std::string_view text = {}) noexcept {
    const auto& commands = context.commands();
    const auto iterator = std::find_if(commands.begin(), commands.end(), [&](const auto& command) {
        return command.type() == type && (text.empty() || command_text(command) == text);
    });
    return iterator == commands.end()
               ? std::numeric_limits<std::size_t>::max()
               : static_cast<std::size_t>(std::distance(commands.begin(), iterator));
}

[[nodiscard]] std::size_t command_count(const RenderCommandRecorder& context,
                                        RenderCommandType type) noexcept {
    return static_cast<std::size_t>(
        std::count_if(context.commands().begin(), context.commands().end(),
                      [type](const auto& command) { return command.type() == type; }));
}

[[nodiscard]] const FillGeometryCommand*
find_fill_geometry_command(const RenderCommandRecorder& context, Color color) noexcept {
    for (const auto& command : context.commands()) {
        if (command.type() != RenderCommandType::FillGeometry) {
            continue;
        }
        const auto& payload = command.payload<FillGeometryCommand>();
        if (payload.color == color) {
            return &payload;
        }
    }
    return nullptr;
}

TEST(BasicControlsTests, TextMeasuresAndPaintsText) {
    auto engine = create_unrounded_engine();
    Text text;
    text.bind_layout_tree(engine);
    text.set_text("WinElement").set_font_size(16.0F);

    LayoutConstraints constraints;
    constraints.width = 300.0F;
    text.calculate_layout(constraints);

    EXPECT_GT(text.frame().width, 60.0F);
    EXPECT_GT(text.frame().height, 10.0F);

    RenderCommandRecorder context;
    text.paint(context);

    ASSERT_EQ(context.commands().size(), 1U);
    EXPECT_EQ(context.commands()[0].type(), RenderCommandType::DrawTextLayout);
    EXPECT_EQ(command_text(context.commands()[0]), "WinElement");
    EXPECT_FLOAT_EQ(command_text_style(context.commands()[0]).font_size, 16.0F);
}

TEST(BasicControlsTests, TextMaxLinesFlowsIntoTextLayoutOptions) {
    auto engine = create_unrounded_engine();
    Text text;
    text.bind_layout_tree(engine);
    text.set_text("Alpha Beta Gamma Delta").set_font_size(14.0F).set_max_lines(1U);
    text.configure_layout([](LayoutElement& layout) { layout.set_width(Length::points(70.0F)); });
    text.calculate_layout(LayoutConstraints{.width = 70.0F, .height = 80.0F});

    RenderCommandRecorder context;
    text.paint(context);

    const auto* text_command =
        find_command(context, RenderCommandType::DrawTextLayout, "Alpha Beta Gamma Delta");
    ASSERT_NE(text_command, nullptr);
    const auto* layout = text_command->payload<DrawTextLayoutCommand>().layout_value();
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->options.max_lines, 1U);
    EXPECT_LE(layout->lines.size(), 1U);
}

TEST(BasicControlsTests, TextMeasuresUtf8WithTextEngineAndRejectsInvalidFontSize) {
    auto engine = create_unrounded_engine();
    Text text;
    text.bind_layout_tree(engine);
    text.set_text("\xE4\xBD\xA0").set_font_size(16.0F);

    text.calculate_layout();

    EXPECT_GT(text.frame().width, 8.0F);
    EXPECT_LE(text.frame().width, 16.0F);
    EXPECT_THROW(text.set_font_size(0.0F), std::invalid_argument);
    EXPECT_THROW(text.set_font_size(std::numeric_limits<float>::quiet_NaN()),
                 std::invalid_argument);
}

TEST(BasicControlsTests, PanelRejectsInvalidBorderWidth) {
    auto engine = create_unrounded_engine();
    Panel panel;
    panel.bind_layout_tree(engine);

    EXPECT_THROW(panel.set_border(Color::rgba(220, 223, 230), -1.0F), std::invalid_argument);
    EXPECT_THROW(
        panel.set_border(Color::rgba(220, 223, 230), std::numeric_limits<float>::quiet_NaN()),
        std::invalid_argument);
}

TEST(BasicControlsTests, PanelPaintsBackgroundFillRect) {
    auto engine = create_unrounded_engine();
    Panel panel;
    panel.bind_layout_tree(engine);
    panel.set_background(Color::rgba(240, 242, 245));
    panel.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(100.0F), Length::points(50.0F));
    });
    panel.calculate_layout();

    RenderCommandRecorder context;
    panel.paint(context);

    ASSERT_GE(context.commands().size(), 1U);
    EXPECT_EQ(context.commands()[0].type(), RenderCommandType::FillRect);
    const auto& fill = context.commands()[0].payload<FillRectCommand>();
    EXPECT_EQ(fill.color, Color::rgba(240, 242, 245));
}

TEST(BasicControlsTests, MessagePaintsSemanticSurfaceAndCloseState) {
    auto engine = create_unrounded_engine();
    Message message;
    message.bind_layout_tree(engine);
    message.set_text("Saved successfully").set_type(MessageType::Success).set_show_close(true);
    message.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(360.0F), Length::points(48.0F));
    });
    message.calculate_layout();

    RenderCommandRecorder context;
    message.paint(context);

    EXPECT_TRUE(message.show_close());
    EXPECT_NE(find_command(context, RenderCommandType::DrawTextLayout, "Saved successfully"),
              nullptr);
    EXPECT_GE(command_count(context, RenderCommandType::FillRoundedRect), 1U);
    EXPECT_GE(command_count(context, RenderCommandType::DrawBoxShadow), 1U);
    EXPECT_EQ(find_command(context, RenderCommandType::DrawTextLayout, "x"), nullptr);
    EXPECT_GT(command_count(context, RenderCommandType::FillGeometry), 0U);
}

TEST(BasicControlsTests, MessagePrimaryIconUsesBrowserSvgFillRule) {
    auto engine = create_unrounded_engine();
    Message message;
    message.bind_layout_tree(engine);
    message.set_text("Primary message").set_type(MessageType::Primary).set_show_close(false);
    message.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(360.0F), Length::points(48.0F));
    });
    message.calculate_layout();

    RenderCommandRecorder context;
    message.paint(context);

    const auto* icon = find_fill_geometry_command(context, Color::rgba(64, 158, 255));
    ASSERT_NE(icon, nullptr);
    EXPECT_EQ(icon->geometry.fill_rule, GeometryFillRule::EvenOdd);
    EXPECT_GE(icon->geometry.figures.size(), 2U);

    EXPECT_EQ(command_count(context, RenderCommandType::FillGeometry), 1U);
}

TEST(BasicControlsTests, MessageShowPushesTopLayerEntry) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(640.0F), Length::points(360.0F));
    });
    root.calculate_layout();

    auto& message = Message::show(root, MessageOptions{.text = "Top message",
                                                       .type = MessageType::Primary,
                                                       .show_close = true,
                                                       .duration_ms = 0});
    auto& second = Message::show(
        root, MessageOptions{.text = "Updated message", .type = MessageType::Success});

    EXPECT_EQ(root.top_layer_count(), 2U);
    EXPECT_NE(&message, &second);
    EXPECT_EQ(second.text(), "Updated message");
    EXPECT_FALSE(second.show_close());
    EXPECT_GT(root.top_layer_bounds(second).y, root.top_layer_bounds(message).y);

    const auto now = winelement::animation::AnimationClockType::now();
    static_cast<void>(root.tick_animations(now + std::chrono::milliseconds(3200)));
    static_cast<void>(root.tick_animations(now + std::chrono::milliseconds(3520)));
    EXPECT_EQ(root.top_layer_count(), 1U);
}

TEST(BasicControlsTests, MessageBoxPromptKeepsInputAndPaintsActions) {
    auto engine = create_unrounded_engine();
    MessageBox box;
    box.bind_layout_tree(engine);
    box.set_title("Prompt")
        .set_message("Enter a project name")
        .set_kind(MessageBoxKind::Prompt)
        .set_type(MessageType::Warning)
        .set_input_text("WinElement")
        .set_confirm_button_text("Create")
        .set_cancel_button_text("Cancel")
        .set_show_cancel_button(true)
        .set_center(true)
        .set_distinguish_cancel_and_close(true)
        .set_input_error_message("Project name is required");
    box.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(420.0F), Length::points(250.0F));
    });
    box.calculate_layout();

    RenderCommandRecorder context;
    box.paint(context);

    EXPECT_EQ(box.kind(), MessageBoxKind::Prompt);
    EXPECT_TRUE(box.show_cancel_button());
    EXPECT_TRUE(box.center());
    EXPECT_TRUE(box.distinguish_cancel_and_close());
    EXPECT_EQ(box.input_text(), "WinElement");
    const auto* title_command = find_command(context, RenderCommandType::DrawTextLayout, "Prompt");
    const auto* message_command =
        find_command(context, RenderCommandType::DrawTextLayout, "Enter a project name");
    const auto* create_command = find_command(context, RenderCommandType::DrawTextLayout, "Create");
    const auto* cancel_command = find_command(context, RenderCommandType::DrawTextLayout, "Cancel");
    ASSERT_NE(title_command, nullptr);
    ASSERT_NE(message_command, nullptr);
    ASSERT_NE(create_command, nullptr);
    ASSERT_NE(cancel_command, nullptr);
    const auto title_rect = command_rect(*title_command);
    const auto message_rect = command_rect(*message_command);
    const auto create_rect = command_rect(*create_command);
    const auto cancel_rect = command_rect(*cancel_command);
    EXPECT_GT(message_rect.y - (title_rect.y + title_rect.height), 2.0F);
    EXPECT_LT(message_rect.y - (title_rect.y + title_rect.height), 10.0F);
    EXPECT_GT(create_rect.y - (message_rect.y + message_rect.height), 56.0F);
    EXPECT_LT(create_rect.y, 208.0F);
    EXPECT_NEAR(cancel_rect.y, create_rect.y, 1.0F);
    EXPECT_EQ(find_command(context, RenderCommandType::DrawTextLayout, "x"), nullptr);
    EXPECT_GE(command_count(context, RenderCommandType::DrawBoxShadow), 1U);
    EXPECT_GT(command_count(context, RenderCommandType::FillGeometry), 0U);
}

TEST(BasicControlsTests, MessageBoxAlertKeepsHeaderContentGapAtDefaultHeight) {
    auto engine = create_unrounded_engine();
    MessageBox box;
    box.bind_layout_tree(engine);
    box.set_title("Notice")
        .set_message("This is an Element-style alert message box.")
        .set_kind(MessageBoxKind::Alert)
        .set_type(MessageType::Info);
    box.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(420.0F), Length::points(148.0F));
    });
    box.calculate_layout();

    RenderCommandRecorder context;
    box.paint(context);

    const auto* title_command = find_command(context, RenderCommandType::DrawTextLayout, "Notice");
    const auto* message_command = find_command(context, RenderCommandType::DrawTextLayout,
                                               "This is an Element-style alert message box.");
    const auto* confirm_command = find_command(context, RenderCommandType::DrawTextLayout, "OK");
    ASSERT_NE(title_command, nullptr);
    ASSERT_NE(message_command, nullptr);
    ASSERT_NE(confirm_command, nullptr);

    const auto title_rect = command_rect(*title_command);
    const auto message_rect = command_rect(*message_command);
    const auto confirm_rect = command_rect(*confirm_command);
    EXPECT_GT(message_rect.y - (title_rect.y + title_rect.height), 2.0F);
    EXPECT_LT(message_rect.y - (title_rect.y + title_rect.height), 10.0F);
    EXPECT_GT(confirm_rect.y - (message_rect.y + message_rect.height), 8.0F);
    EXPECT_LT(confirm_rect.y, 104.0F);
}

TEST(BasicControlsTests, MessageBoxModalOptionsMatchElementPlusShortcuts) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(640.0F), Length::points(360.0F));
    });
    root.calculate_layout();
    EventRouter router(root);

    auto& alert = MessageBox::show(root, MessageBoxOptions{.title = "Alert",
                                                           .message = "Manual close only",
                                                           .kind = MessageBoxKind::Alert,
                                                           .show_cancel_button = false,
                                                           .close_on_click_modal = false,
                                                           .close_on_press_escape = false});
    const auto alert_bounds = root.top_layer_bounds(alert);
    static_cast<void>(router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                              .position = Point{8.0F, 8.0F},
                                                              .button = PointerButton::Primary}));
    EXPECT_EQ(root.top_layer_count(), 1U);
    EXPECT_EQ(root.top_layer_bounds(alert), alert_bounds);
    static_cast<void>(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Escape}));
    EXPECT_EQ(root.top_layer_count(), 1U);
    root.clear_top_layer();

    static_cast<void>(MessageBox::show(root, MessageBoxOptions{.title = "Confirm",
                                                               .message = "Click backdrop closes",
                                                               .kind = MessageBoxKind::Confirm,
                                                               .modal = false,
                                                               .close_on_click_modal = true}));
    EXPECT_EQ(root.top_layer_count(), 1U);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{8.0F, 8.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_EQ(root.top_layer_count(), 0U);
}

TEST(BasicControlsTests, MessageBoxModalBackdropFullyCoversUnderlyingContent) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(640.0F), Length::points(360.0F));
    });
    root.calculate_layout();

    static_cast<void>(MessageBox::show(root, MessageBoxOptions{.title = "Modal",
                                                               .message = "Backdrop covers page",
                                                               .kind = MessageBoxKind::Alert,
                                                               .modal = true}));

    RenderCommandRecorder context;
    root.paint(context);

    const auto iterator = std::find_if(context.commands().begin(), context.commands().end(),
                                       [](const auto& command) {
                                           return command.type() == RenderCommandType::FillRect &&
                                                  command_rect(command) ==
                                                      Rect{0.0F, 0.0F, 640.0F, 360.0F};
                                       });
    ASSERT_NE(iterator, context.commands().end());
    EXPECT_EQ(command_fill_color(*iterator), Color::rgba(0, 0, 0, 80));
}

TEST(BasicControlsTests, DialogModalBackdropFullyCoversUnderlyingContent) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(640.0F), Length::points(360.0F));
    });
    root.calculate_layout();

    static_cast<void>(Dialog::show(root, DialogOptions{.title = "Dialog",
                                                       .body = "Backdrop covers page",
                                                       .modal = true}));

    RenderCommandRecorder context;
    root.paint(context);

    const auto iterator = std::find_if(context.commands().begin(), context.commands().end(),
                                       [](const auto& command) {
                                           return command.type() == RenderCommandType::FillRect &&
                                                  command_rect(command) ==
                                                      Rect{0.0F, 0.0F, 640.0F, 360.0F};
                                       });
    ASSERT_NE(iterator, context.commands().end());
    EXPECT_EQ(command_fill_color(*iterator), Color::rgba(0, 0, 0, 80));
}

TEST(BasicControlsTests, DialogPaintsBodyAndFooterActions) {
    auto engine = create_unrounded_engine();
    Dialog dialog;
    dialog.bind_layout_tree(engine);
    dialog.set_title("Notice")
        .set_body("Dialog content stays in the current page context.")
        .set_confirm_button_text("Confirm")
        .set_cancel_button_text("Cancel")
        .set_show_cancel_button(true);
    dialog.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(520.0F), Length::points(150.0F));
    });
    dialog.calculate_layout();

    RenderCommandRecorder context;
    dialog.paint(context);

    EXPECT_TRUE(dialog.show_close());
    EXPECT_TRUE(dialog.show_cancel_button());
    const auto* title_command = find_command(context, RenderCommandType::DrawTextLayout, "Notice");
    const auto* body_command = find_command(context, RenderCommandType::DrawTextLayout,
                                            "Dialog content stays in the current page context.");
    const auto* confirm_command =
        find_command(context, RenderCommandType::DrawTextLayout, "Confirm");
    ASSERT_NE(title_command, nullptr);
    ASSERT_NE(body_command, nullptr);
    ASSERT_NE(confirm_command, nullptr);
    const auto title_rect = command_rect(*title_command);
    const auto body_rect = command_rect(*body_command);
    const auto confirm_rect = command_rect(*confirm_command);
    EXPECT_GT(body_rect.y - (title_rect.y + title_rect.height), 10.0F);
    EXPECT_LT(body_rect.y - (title_rect.y + title_rect.height), 34.0F);
    EXPECT_GT(confirm_rect.y - (body_rect.y + body_rect.height), 16.0F);
    EXPECT_LT(confirm_rect.y - (body_rect.y + body_rect.height), 34.0F);
    EXPECT_LT(confirm_rect.y, 120.0F);
    EXPECT_EQ(find_command(context, RenderCommandType::DrawTextLayout, "x"), nullptr);
    EXPECT_GE(command_count(context, RenderCommandType::DrawBoxShadow), 1U);
}

TEST(BasicControlsTests, DialogShowReusesExistingTopLayer) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(640.0F), Length::points(360.0F));
    });
    root.calculate_layout();

    auto& first = Dialog::show(root, DialogOptions{.title = "First", .body = "One"});
    auto& second = Dialog::show(root, DialogOptions{.title = "Second", .body = "Two"});

    EXPECT_EQ(root.top_layer_count(), 1U);
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(second.title(), "Second");
    EXPECT_EQ(second.body(), "Two");
}

TEST(BasicControlsTests, DialogCanBeNonModalAndKeepBackdropClickOpen) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(640.0F), Length::points(360.0F));
    });
    root.calculate_layout();
    EventRouter router(root);

    auto& dialog = Dialog::show(root, DialogOptions{.title = "Non-modal",
                                                    .body = "Page remains interactive.",
                                                    .modal = false,
                                                    .close_on_click_modal = false,
                                                    .close_on_press_escape = false});
    const auto bounds = root.top_layer_bounds(dialog);
    static_cast<void>(router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                              .position = Point{8.0F, 8.0F},
                                                              .button = PointerButton::Primary}));
    EXPECT_EQ(root.top_layer_count(), 1U);
    EXPECT_EQ(root.top_layer_bounds(dialog), bounds);
    static_cast<void>(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Escape}));
    EXPECT_EQ(root.top_layer_count(), 1U);
}

TEST(BasicControlsTests, LoadingAnimatesAndPaintsSpinner) {
    auto engine = create_unrounded_engine();
    Loading loading;
    loading.bind_layout_tree(engine);
    loading.set_text("Loading data").set_active(true);
    loading.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(240.0F), Length::points(160.0F));
    });
    loading.calculate_layout();

    EXPECT_TRUE(loading.tick_animations(winelement::animation::AnimationClockType::now()));

    RenderCommandRecorder context;
    loading.paint(context);

    EXPECT_TRUE(loading.active());
    EXPECT_FALSE(loading.show_close());
    EXPECT_GT(command_count(context, RenderCommandType::FillGeometry), 0U);
    const auto* label_command =
        find_command(context, RenderCommandType::DrawTextLayout, "Loading data");
    ASSERT_NE(label_command, nullptr);
    EXPECT_EQ(command_text_style(*label_command).alignment, TextAlignment::Center);
}

TEST(BasicControlsTests, MessageBoxAndDialogCanDragTopLayerBounds) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(640.0F), Length::points(360.0F));
    });
    root.calculate_layout();
    EventRouter router(root);

    auto& box = MessageBox::show(
        root, MessageBoxOptions{.title = "Drag", .message = "Move me", .draggable = true});
    const auto box_start = root.top_layer_bounds(box);
    const auto box_initial_transform = box.render_transform();
    EXPECT_EQ(router.cursor_for_point({box_start.x + 24.0F, box_start.y + 20.0F}),
              PointerCursor::Move);
    EXPECT_EQ(router.cursor_for_point({box_start.x + box_start.width - 12.0F, box_start.y + 20.0F}),
              PointerCursor::Hand);
    EXPECT_EQ(router.cursor_for_point({box_start.x + 24.0F, box_start.y + 42.0F}),
              PointerCursor::Arrow);
    EXPECT_EQ(router.cursor_for_point({box_start.x + 24.0F, box_start.y + 70.0F}),
              PointerCursor::Arrow);
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                            .position = {box_start.x + 24.0F, box_start.y + 42.0F},
                                            .button = PointerButton::Primary,
                                            .primary_button_down = true});
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                            .position = {box_start.x + 64.0F, box_start.y + 72.0F},
                                            .primary_button_down = true});
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                            .position = {box_start.x + 64.0F, box_start.y + 72.0F},
                                            .button = PointerButton::Primary});
    EXPECT_EQ(root.top_layer_bounds(box), box_start);
    EXPECT_EQ(box.render_transform(), box_initial_transform);
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                            .position = {box_start.x + 24.0F, box_start.y + 20.0F},
                                            .button = PointerButton::Primary,
                                            .primary_button_down = true});
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                            .position = {box_start.x + 64.0F, box_start.y + 50.0F},
                                            .primary_button_down = true});
    const auto box_mid = root.top_layer_bounds(box);
    EXPECT_NEAR(box_mid.x, box_start.x, 0.5F);
    EXPECT_NEAR(box_mid.y, box_start.y, 0.5F);
    EXPECT_EQ(box.render_transform(), Transform2D::translation(40.0F, 30.0F));
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                            .position = {box_start.x + 64.0F, box_start.y + 50.0F},
                                            .button = PointerButton::Primary});
    const auto box_end = root.top_layer_bounds(box);
    EXPECT_NEAR(box_end.x, box_start.x + 40.0F, 0.5F);
    EXPECT_NEAR(box_end.y, box_start.y + 30.0F, 0.5F);
    EXPECT_EQ(box.render_transform(), Transform2D::identity());
    root.clear_top_layer();

    auto& dialog = Dialog::show(
        root, DialogOptions{.title = "Drag dialog", .body = "Move me", .draggable = true});
    const auto dialog_start = root.top_layer_bounds(dialog);
    const auto dialog_initial_transform = dialog.render_transform();
    EXPECT_EQ(router.cursor_for_point({dialog_start.x + 24.0F, dialog_start.y + 24.0F}),
              PointerCursor::Move);
    EXPECT_EQ(router.cursor_for_point(
                  {dialog_start.x + dialog_start.width - 12.0F, dialog_start.y + 24.0F}),
              PointerCursor::Hand);
    EXPECT_EQ(router.cursor_for_point({dialog_start.x + 24.0F, dialog_start.y + 48.0F}),
              PointerCursor::Arrow);
    router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Down,
                     .position = {dialog_start.x + 24.0F, dialog_start.y + 48.0F},
                     .button = PointerButton::Primary,
                     .primary_button_down = true});
    router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move,
                     .position = {dialog_start.x + 74.0F, dialog_start.y + 73.0F},
                     .primary_button_down = true});
    router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Up,
                     .position = {dialog_start.x + 74.0F, dialog_start.y + 73.0F},
                     .button = PointerButton::Primary});
    EXPECT_EQ(root.top_layer_bounds(dialog), dialog_start);
    EXPECT_EQ(dialog.render_transform(), dialog_initial_transform);
    router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Down,
                     .position = {dialog_start.x + 24.0F, dialog_start.y + 24.0F},
                     .button = PointerButton::Primary,
                     .primary_button_down = true});
    router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move,
                     .position = {dialog_start.x + 74.0F, dialog_start.y + 49.0F},
                     .primary_button_down = true});
    const auto dialog_mid = root.top_layer_bounds(dialog);
    EXPECT_NEAR(dialog_mid.x, dialog_start.x, 0.5F);
    EXPECT_NEAR(dialog_mid.y, dialog_start.y, 0.5F);
    EXPECT_EQ(dialog.render_transform(), Transform2D::translation(50.0F, 25.0F));
    router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Up,
                     .position = {dialog_start.x + 74.0F, dialog_start.y + 49.0F},
                     .button = PointerButton::Primary});
    const auto dialog_end = root.top_layer_bounds(dialog);
    EXPECT_NEAR(dialog_end.x, dialog_start.x + 50.0F, 0.5F);
    EXPECT_NEAR(dialog_end.y, dialog_start.y + 25.0F, 0.5F);
    EXPECT_EQ(dialog.render_transform(), Transform2D::identity());
}

TEST(BasicControlsTests, LoadingShowReusesExistingTopLayerAndCanHideCloseButton) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(640.0F), Length::points(360.0F));
    });
    root.calculate_layout();

    auto& first = Loading::show(root, LoadingOptions{.text = "Preparing"});
    auto& second = Loading::show(
        root, LoadingOptions{.text = "Still preparing", .fullscreen = false, .show_close = false});
    auto centered_bounds = root.top_layer_bounds(second);
    EXPECT_FLOAT_EQ(centered_bounds.x, 0.0F);
    EXPECT_FLOAT_EQ(centered_bounds.y, 0.0F);
    EXPECT_FLOAT_EQ(centered_bounds.width, 640.0F);
    EXPECT_FLOAT_EQ(centered_bounds.height, 360.0F);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(800.0F), Length::points(400.0F));
    });
    root.calculate_layout();
    centered_bounds = root.top_layer_bounds(second);
    EXPECT_FLOAT_EQ(centered_bounds.x, 0.0F);
    EXPECT_FLOAT_EQ(centered_bounds.y, 0.0F);
    EXPECT_FLOAT_EQ(centered_bounds.width, 800.0F);
    EXPECT_FLOAT_EQ(centered_bounds.height, 400.0F);
    auto& third = Loading::show(
        root, LoadingOptions{.text = "Blocking", .fullscreen = true, .show_close = true});

    EXPECT_EQ(root.top_layer_count(), 1U);
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(&first, &third);
    EXPECT_EQ(third.text(), "Blocking");
    EXPECT_FALSE(third.show_close());
}

TEST(BasicControlsTests, StackPanelConfiguresLayoutDirection) {
    auto engine = create_unrounded_engine();
    StackPanel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(200.0F), Length::points(40.0F));
    });
    auto first = std::make_unique<Text>();
    first->set_text("A");
    auto second = std::make_unique<Text>();
    second->set_text("B");

    auto& first_ref = root.append_child(std::move(first));
    auto& second_ref = root.append_child(std::move(second));

    LayoutConstraints constraints;
    constraints.width = 200.0F;
    constraints.height = 40.0F;
    root.calculate_layout(constraints);

    EXPECT_FLOAT_EQ(first_ref.frame().y, 0.0F);
    EXPECT_GT(second_ref.frame().y, first_ref.frame().y);

    root.set_orientation(Orientation::Horizontal);
    root.calculate_layout(constraints);

    EXPECT_FLOAT_EQ(first_ref.frame().x, 0.0F);
    EXPECT_GT(second_ref.frame().x, first_ref.frame().x);
}

TEST(BasicControlsTests, VirtualizationPlannerComputesOverscannedWindow) {
    VirtualizationPlanner planner;
    planner.set_total_count(100U);
    planner.set_item_extent(20.0F);
    planner.set_overscan(2U);

    const auto window = planner.window_for(100.0F, 60.0F);
    EXPECT_EQ(window.start_index, 3U);
    EXPECT_EQ(window.count, 8U);
    EXPECT_FLOAT_EQ(window.leading_spacer, 60.0F);
    EXPECT_GT(window.trailing_spacer, 0.0F);
}

TEST(BasicControlsTests, RecyclePoolDropsItemsBeyondCapacity) {
    RecyclePool<int> pool;
    pool.set_capacity(2U);

    pool.release(std::make_unique<int>(1));
    pool.release(std::make_unique<int>(2));
    pool.release(std::make_unique<int>(3));

    EXPECT_EQ(pool.capacity(), 2U);
    EXPECT_EQ(pool.size(), 2U);

    pool.set_capacity(1U);
    EXPECT_EQ(pool.size(), 1U);

    EXPECT_NE(pool.acquire(), nullptr);
    EXPECT_EQ(pool.acquire(), nullptr);
}

TEST(BasicControlsTests, ItemsControlCanRealizeViewportWindow) {
    auto engine = create_unrounded_engine();
    ItemsControl items;
    items.bind_layout_tree(engine);
    items.set_items({"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"});

    items.set_virtualization_window(40.0F, 60.0F, 20.0F, 1U);

    EXPECT_TRUE(items.virtualized());
    EXPECT_EQ(items.realized_start_index(), 1U);
    EXPECT_EQ(items.realized_count(), 6U);
    EXPECT_EQ(items.child_count(), 6U);
}

TEST(BasicControlsTests, ButtonHandlesPointerAndKeyboardClick) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(40.0F));
    });

    auto button = std::make_unique<Button>();
    button->set_text("Save");
    button->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F));
    });

    auto click_count = 0;
    button->set_on_click([&click_count]() { ++click_count; });
    auto& button_ref = static_cast<Button&>(root.append_child(std::move(button)));

    LayoutConstraints constraints;
    constraints.width = 120.0F;
    constraints.height = 40.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{5.0F, 5.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(button_ref.pressed());
    EXPECT_EQ(router.focus_manager().focused_element(), &button_ref);
    EXPECT_EQ(router.pointer_capture(), &button_ref);

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{5.0F, 5.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_FALSE(button_ref.pressed());
    EXPECT_EQ(router.pointer_capture(), nullptr);
    EXPECT_EQ(click_count, 1);

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Enter}).handled);
    EXPECT_EQ(click_count, 2);

    RenderCommandRecorder context;
    root.paint(context);
    const auto* text_command = find_command(context, RenderCommandType::DrawTextLayout, "Save");
    ASSERT_NE(text_command, nullptr);
    EXPECT_EQ(command_text_style(*text_command).alignment, TextAlignment::Center);
}

TEST(BasicControlsTests, ButtonIntrinsicHeightsMatchElementPlusSizes) {
    auto engine = create_unrounded_engine();
    StackPanel root;
    root.bind_layout_tree(engine);
    root.set_orientation(Orientation::Horizontal).set_gap(8.0F).set_align_items(Align::FlexStart);

    auto& large = root.append_new_child<Button>();
    large.set_text("Large").set_size(ButtonSize::Large);
    auto& normal = root.append_new_child<Button>();
    normal.set_text("Default");
    auto& small = root.append_new_child<Button>();
    small.set_text("Small").set_size(ButtonSize::Small);

    root.calculate_layout(LayoutConstraints{.width = 360.0F, .height = 80.0F});

    EXPECT_FLOAT_EQ(large.frame().height, 40.0F);
    EXPECT_FLOAT_EQ(normal.frame().height, 32.0F);
    EXPECT_FLOAT_EQ(small.frame().height, 24.0F);
}

TEST(BasicControlsTests, ButtonLoadingAnimationUsesBuiltInSvgIconAndFrameTick) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(140.0F), Length::points(40.0F));
    });

    auto button = std::make_unique<Button>();
    button->set_text("Save").set_loading(true);
    auto& button_ref = static_cast<Button&>(root.append_child(std::move(button)));
    root.calculate_layout(LayoutConstraints{.width = 140.0F, .height = 40.0F});

    root.clear_paint_dirty_subtree();
    const auto now = winelement::animation::AnimationClockType::now();
    EXPECT_TRUE(root.tick_animations(now + std::chrono::milliseconds(80)));
    EXPECT_TRUE(button_ref.needs_paint());

    RenderCommandRecorder context;
    root.paint(context);
    EXPECT_NE(find_command(context, RenderCommandType::DrawTextLayout, "Save"), nullptr);
    EXPECT_EQ(find_command(context, RenderCommandType::DrawTextLayout, "Loading... Save"), nullptr);
    EXPECT_GT(command_count(context, RenderCommandType::FillGeometry), 0U);
    EXPECT_GT(command_count(context, RenderCommandType::PushLayer), 0U);
}

TEST(BasicControlsTests, ButtonReleasesPressedStateOnCancelOrFocusLoss) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(40.0F));
    });

    auto button = std::make_unique<Button>();
    button->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F));
    });
    auto& button_ref = static_cast<Button&>(root.append_child(std::move(button)));
    auto click_count = 0;
    button_ref.set_on_click([&click_count]() { ++click_count; });

    LayoutConstraints constraints;
    constraints.width = 120.0F;
    constraints.height = 40.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                            .position = Point{5.0F, 5.0F},
                                            .button = PointerButton::Primary});
    ASSERT_TRUE(button_ref.pressed());
    EXPECT_EQ(router.pointer_capture(), &button_ref);

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{140.0F, 50.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_FALSE(button_ref.pressed());
    EXPECT_EQ(router.pointer_capture(), nullptr);
    EXPECT_EQ(click_count, 0);

    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                            .position = Point{5.0F, 5.0F},
                                            .button = PointerButton::Primary});
    ASSERT_TRUE(button_ref.pressed());

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Cancel,
                                                      .position = Point{5.0F, 5.0F}})
                    .handled);
    EXPECT_FALSE(button_ref.pressed());

    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                            .position = Point{5.0F, 5.0F},
                                            .button = PointerButton::Primary});
    ASSERT_TRUE(button_ref.pressed());

    EXPECT_TRUE(router.focus_manager().clear_focus());
    EXPECT_FALSE(button_ref.pressed());
}

TEST(BasicControlsTests, ButtonFocusVisibilityInvalidatesPaint) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);

    auto button = std::make_unique<Button>();
    auto& button_ref = static_cast<Button&>(root.append_child(std::move(button)));
    root.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 40.0F});

    EventRouter router(root);
    root.clear_paint_dirty_subtree();
    ASSERT_TRUE(router.focus_manager().set_focus(&button_ref, true));
    EXPECT_TRUE(button_ref.needs_paint());

    root.clear_paint_dirty_subtree();
    ASSERT_TRUE(router.focus_manager().clear_focus());
    EXPECT_TRUE(button_ref.needs_paint());
}

TEST(BasicControlsTests, InputReceivesTextThroughFocusedKeyRoute) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(160.0F), Length::points(40.0F));
    });

    auto input = std::make_unique<Input>();
    input->set_placeholder("Name");
    input->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F));
    });

    auto& input_ref = static_cast<Input&>(root.append_child(std::move(input)));

    LayoutConstraints constraints;
    constraints.width = 160.0F;
    constraints.height = 40.0F;
    root.calculate_layout(constraints);

    EventRouter router(root);
    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                            .position = Point{4.0F, 4.0F},
                                            .button = PointerButton::Primary});
    EXPECT_EQ(router.focus_manager().focused_element(), &input_ref);

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "A"}).handled);
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "b"}).handled);
    EXPECT_EQ(input_ref.text(), "Ab");

    EXPECT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Backspace})
                    .handled);
    EXPECT_EQ(input_ref.text(), "A");

    RenderCommandRecorder context;
    root.paint(context);
    ASSERT_FALSE(context.commands().empty());
    const auto* text_command = find_command(context, RenderCommandType::DrawTextLayout, "A");
    ASSERT_NE(text_command, nullptr);
    EXPECT_EQ(command_text_style(*text_command).color, input_ref.style().text_color);
}

TEST(BasicControlsTests, InputFormattedCanonicalValueDoesNotReintroduceSpaces) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_width(Length::points(180.0F));
        layout.set_height(Length::points(40.0F));
    });
    input.set_text("ab12")
        .set_formatter([](std::string_view value) {
            auto result = std::string(value);
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            return result;
        })
        .set_parser([](std::string_view value) {
            auto result = std::string(value);
            result.erase(std::remove(result.begin(), result.end(), ' '), result.end());
            return result;
        });
    input.set_caret_byte_offset(input.text().size());
    input.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 40.0F});

    RenderCommandRecorder initial_context;
    input.paint(initial_context);
    EXPECT_NE(find_command(initial_context, RenderCommandType::DrawTextLayout, "AB12"), nullptr);
    EXPECT_EQ(find_command(initial_context, RenderCommandType::DrawTextLayout, "AB 12"), nullptr);

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));
    ASSERT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = " "}).handled);
    ASSERT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "c"}).handled);
    EXPECT_EQ(input.text(), "ab12c");

    RenderCommandRecorder edited_context;
    input.paint(edited_context);
    EXPECT_NE(find_command(edited_context, RenderCommandType::DrawTextLayout, "AB12C"), nullptr);
    EXPECT_EQ(find_command(edited_context, RenderCommandType::DrawTextLayout, "AB 12C"), nullptr);
}

TEST(BasicControlsTests, InputCompositionStartHidesPlaceholderAndUpdateDrawsCompositionText) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_placeholder("Placeholder");
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(40.0F));
    });
    input.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 40.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    EXPECT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionStart}).handled);

    RenderCommandRecorder start_context;
    input.paint(start_context);
    EXPECT_EQ(find_command(start_context, RenderCommandType::DrawTextLayout, "Placeholder"),
              nullptr);

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionUpdate, .text = "你好"})
            .handled);

    RenderCommandRecorder context;
    input.paint(context);

    EXPECT_EQ(find_command(context, RenderCommandType::DrawTextLayout, "Placeholder"), nullptr);
    const auto* composition_command =
        find_command(context, RenderCommandType::DrawTextLayout, "你好");
    ASSERT_NE(composition_command, nullptr);
    EXPECT_EQ(command_text_style(*composition_command).color, input.style().text_color);
}

TEST(BasicControlsTests, InputCompositionStartInvalidatesParentPlaceholderCache) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(200.0F), Length::points(48.0F));
    });

    auto input = std::make_unique<Input>();
    input->set_placeholder("Placeholder");
    input->configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(40.0F));
    });
    auto& input_ref = static_cast<Input&>(root.append_child(std::move(input)));
    root.calculate_layout(LayoutConstraints{.width = 200.0F, .height = 48.0F});

    RenderCommandRecorder initial_context;
    root.paint(initial_context);
    ASSERT_NE(find_command(initial_context, RenderCommandType::DrawTextLayout, "Placeholder"),
              nullptr);

    EventRouter router(root);
    ASSERT_TRUE(router.focus_manager().set_focus(&input_ref));
    ASSERT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionStart}).handled);

    RenderCommandRecorder composition_context;
    root.paint(composition_context);
    EXPECT_EQ(find_command(composition_context, RenderCommandType::DrawTextLayout, "Placeholder"),
              nullptr);
}

TEST(BasicControlsTests, InputCompositionMovesCaretBehindCompositionText) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(40.0F));
    });
    input.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 40.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    const auto initial_caret_rect = input.text_input_caret_rect();
    ASSERT_TRUE(initial_caret_rect.has_value());

    ASSERT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionStart}).handled);
    ASSERT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionUpdate, .text = "ni"})
            .handled);

    RenderCommandRecorder context;
    input.paint(context);

    const auto* composition_command =
        find_command(context, RenderCommandType::DrawTextLayout, "ni");
    ASSERT_NE(composition_command, nullptr);

    const auto composition_rect = command_rect(*composition_command);
    const auto composed_caret_rect = input.text_input_caret_rect();
    ASSERT_TRUE(composed_caret_rect.has_value());
    EXPECT_GT(composed_caret_rect->x, initial_caret_rect->x);
    EXPECT_GE(composed_caret_rect->x + 0.5F, composition_rect.x + composition_rect.width);
}

TEST(BasicControlsTests, InputSingleLineAutoScrollsCaretIntoViewAtTextEnd) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(40.0F));
    });
    input.set_text("This is a long single-line value that should scroll horizontally.");
    input.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 40.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    RenderCommandRecorder context;
    input.paint(context);

    const auto caret_rect = input.text_input_caret_rect();
    ASSERT_TRUE(caret_rect.has_value());
    const auto visible_right = input.frame().width - input.style().padding.right;
    EXPECT_LE(caret_rect->x + caret_rect->width, visible_right + 1.0F);
    EXPECT_GE(caret_rect->x, input.style().padding.left - 1.0F);

    const auto* text_command =
        find_command(context, RenderCommandType::DrawTextLayout,
                     "This is a long single-line value that should scroll horizontally.");
    ASSERT_NE(text_command, nullptr);
    EXPECT_LT(command_rect(*text_command).x, input.style().padding.left);
}

TEST(BasicControlsTests, InputCompositionUpdateAfterCommittedChineseKeepsCaretVisible) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(40.0F));
    });
    input.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 40.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    ASSERT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionStart}).handled);
    ASSERT_TRUE(
        router
            .route_key_event(KeyEvent{.kind = KeyEventKind::CompositionEnd, .text = "\xE4\xBD\xA0"})
            .handled);

    ASSERT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionStart}).handled);
    ASSERT_TRUE(router
                    .route_key_event(KeyEvent{.kind = KeyEventKind::CompositionUpdate,
                                              .text = "sdfsdfsdfsdfsdfsdf"})
                    .handled);

    RenderCommandRecorder context;
    input.paint(context);

    const auto caret_rect = input.text_input_caret_rect();
    ASSERT_TRUE(caret_rect.has_value());
    const auto visible_right = input.frame().width - input.style().padding.right;
    EXPECT_LE(caret_rect->x + caret_rect->width, visible_right + 1.0F);

    const auto* composition_command =
        find_command(context, RenderCommandType::DrawTextLayout, "sdfsdfsdfsdfsdfsdf");
    ASSERT_NE(composition_command, nullptr);
    EXPECT_LT(command_rect(*composition_command).x, input.style().padding.left);
}

TEST(BasicControlsTests, InputCommittedChineseThenEnglishPaintsCaretAtTextEnd) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_clearable(true)
        .set_prefix_text("user")
        .set_suffix_text("id")
        .set_max_length(24U)
        .set_show_word_limit(true);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(360.0F), Length::points(40.0F));
    });
    input.calculate_layout(LayoutConstraints{.width = 360.0F, .height = 40.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));
    ASSERT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionStart}).handled);
    ASSERT_TRUE(router
                    .route_key_event(KeyEvent{.kind = KeyEventKind::CompositionEnd,
                                              .text = "\xE4\xBD\xA0\xE5\xA5\xBD"})
                    .handled);
    for (const auto character : {"k", "s", "j", "d", "k", "f", "l"}) {
        ASSERT_TRUE(
            router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = character})
                .handled);
    }
    ASSERT_EQ(input.text(), "\xE4\xBD\xA0\xE5\xA5\xBD"
                            "ksjdkfl");
    ASSERT_EQ(input.caret_byte_offset(), input.text().size());

    RenderCommandRecorder context;
    input.paint(context);

    const auto* text_command = find_command(context, RenderCommandType::DrawTextLayout,
                                            "\xE4\xBD\xA0\xE5\xA5\xBD"
                                            "ksjdkfl");
    ASSERT_NE(text_command, nullptr);
    const auto text_index = find_command_index(context, RenderCommandType::DrawTextLayout,
                                               "\xE4\xBD\xA0\xE5\xA5\xBD"
                                               "ksjdkfl");
    ASSERT_NE(text_index, std::numeric_limits<std::size_t>::max());
    const auto caret_begin =
        std::next(context.commands().begin(), static_cast<std::ptrdiff_t>(text_index));
    const auto caret_iterator =
        std::find_if(caret_begin, context.commands().end(), [&](const auto& command) {
            return command.type() == RenderCommandType::FillPixelSnappedRect &&
                   command_fill_color(command) == input.style().caret_color;
        });
    const auto* caret_command =
        caret_iterator == context.commands().end() ? nullptr : &*caret_iterator;
    ASSERT_NE(caret_command, nullptr);

    const auto text_rect = command_rect(*text_command);
    const auto caret_rect = command_rect(*caret_command);
    EXPECT_GE(caret_rect.x + 0.5F, text_rect.x + text_rect.width);
}

TEST(BasicControlsTests, InputReadOnlyConsumesEditingKeysWithoutChangingText) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("A").set_read_only(true);

    EventRouter router(input);
    EXPECT_TRUE(router.focus_manager().set_focus(&input));

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "B"}).handled);
    EXPECT_EQ(input.text(), "A");

    EXPECT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Backspace})
                    .handled);
    EXPECT_EQ(input.text(), "A");

    RenderCommandRecorder context;
    input.paint(context);
    ASSERT_FALSE(context.commands().empty());
    EXPECT_EQ(command_fill_color(context.commands().front()), Color::rgba(245, 247, 250));
}

TEST(BasicControlsTests, InputReadOnlyReleasesActivePointerSelection) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("abcdef");
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(40.0F));
    });
    input.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 40.0F});

    EventRouter router(input);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{12.0F, 20.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    ASSERT_EQ(router.pointer_capture(), &input);

    input.set_read_only(true);
    EXPECT_EQ(router.pointer_capture(), nullptr);
    EXPECT_TRUE(input.read_only());
}

TEST(BasicControlsTests, InputBackspaceRemovesOneUtf8CodePoint) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("A\xE4\xBD\xA0");

    EventRouter router(input);
    EXPECT_TRUE(router.focus_manager().set_focus(&input));

    EXPECT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Backspace})
                    .handled);
    EXPECT_EQ(input.text(), "A");
}

TEST(BasicControlsTests, InputEditsAtCaretAndDrawsCaretWithTextEngine) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("AC");
    input.set_caret_byte_offset(1U);

    EventRouter router(input);
    EXPECT_TRUE(router.focus_manager().set_focus(&input));

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "B"}).handled);
    EXPECT_EQ(input.text(), "ABC");
    EXPECT_EQ(input.caret_byte_offset(), 2U);

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Left}).handled);
    EXPECT_EQ(input.caret_byte_offset(), 1U);

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Delete}).handled);
    EXPECT_EQ(input.text(), "AC");
    EXPECT_EQ(input.caret_byte_offset(), 1U);

    RenderCommandRecorder context;
    input.paint(context);
    const auto* text_command = find_command(context, RenderCommandType::DrawTextLayout, "AC");
    ASSERT_NE(text_command, nullptr);
    const auto* caret_command = find_command(context, RenderCommandType::FillPixelSnappedRect);
    ASSERT_NE(caret_command, nullptr);
    EXPECT_NE(std::find_if(context.commands().begin(), context.commands().end(),
                           [&](const auto& command) {
                               return command.type() == RenderCommandType::FillPixelSnappedRect &&
                                      command_fill_color(command) == input.style().caret_color;
                           }),
              context.commands().end());
}

TEST(BasicControlsTests, InputUndoRedoRestoresCaretAndSelectionState) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("abcdef").set_selection(1U, 4U);

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    ASSERT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "X"}).handled);
    EXPECT_EQ(input.text(), "aXef");
    EXPECT_EQ(input.caret_byte_offset(), 2U);
    EXPECT_FALSE(input.has_selection());

    ASSERT_TRUE(router
                    .route_key_event(KeyEvent{.kind = KeyEventKind::Down,
                                              .key = Key::Z,
                                              .modifiers = KeyModifiers{.control = true}})
                    .handled);
    EXPECT_EQ(input.text(), "abcdef");
    EXPECT_EQ(input.caret_byte_offset(), 4U);
    EXPECT_EQ(input.selection_anchor_byte_offset(), 1U);
    EXPECT_EQ(input.selection_active_byte_offset(), 4U);
    EXPECT_TRUE(input.has_selection());

    ASSERT_TRUE(
        router
            .route_key_event(KeyEvent{.kind = KeyEventKind::Down,
                                      .key = Key::Z,
                                      .modifiers = KeyModifiers{.shift = true, .control = true}})
            .handled);
    EXPECT_EQ(input.text(), "aXef");
    EXPECT_EQ(input.caret_byte_offset(), 2U);
    EXPECT_FALSE(input.has_selection());
}

TEST(BasicControlsTests, InputCompositionStartReplacesSelectedTextWithoutOverlap) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("abcdef").set_selection(1U, 4U);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(40.0F));
    });
    input.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 40.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    ASSERT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionStart}).handled);
    EXPECT_EQ(input.text(), "aef");
    EXPECT_EQ(input.caret_byte_offset(), 1U);
    EXPECT_FALSE(input.has_selection());

    ASSERT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionUpdate, .text = "X"})
            .handled);

    RenderCommandRecorder composing_context;
    input.paint(composing_context);
    EXPECT_NE(find_command(composing_context, RenderCommandType::DrawTextLayout, "aef"), nullptr);
    EXPECT_NE(find_command(composing_context, RenderCommandType::DrawTextLayout, "X"), nullptr);
    EXPECT_EQ(find_command(composing_context, RenderCommandType::DrawTextLayout, "abcdef"),
              nullptr);

    ASSERT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::CompositionEnd, .text = "X"})
                    .handled);
    EXPECT_EQ(input.text(), "aXef");
    EXPECT_EQ(input.caret_byte_offset(), 2U);

    ASSERT_TRUE(router
                    .route_key_event(KeyEvent{.kind = KeyEventKind::Down,
                                              .key = Key::Z,
                                              .modifiers = KeyModifiers{.control = true}})
                    .handled);
    EXPECT_EQ(input.text(), "abcdef");
    EXPECT_EQ(input.selection_anchor_byte_offset(), 1U);
    EXPECT_EQ(input.selection_active_byte_offset(), 4U);
}

TEST(BasicControlsTests, ClearAffordancesUseHandCursor) {
    auto engine = create_unrounded_engine();
    StackPanel root;
    root.bind_layout_tree(engine);
    root.set_orientation(Orientation::Vertical).set_gap(8.0F);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(240.0F), Length::points(96.0F));
    });

    auto& input = root.append_new_child<Input>();
    input.set_text("value").set_clearable(true);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(40.0F)).set_flex_shrink(0.0F);
    });

    auto& select = root.append_new_child<Select>();
    select.set_options({SelectOption{.label = "One", .value = "1"}})
        .set_selected_index(0U)
        .set_clearable(true);
    select.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(40.0F)).set_flex_shrink(0.0F);
    });

    root.calculate_layout(LayoutConstraints{.width = 240.0F, .height = 96.0F});
    EventRouter router(root);

    const auto input_frame = input.absolute_frame();
    const auto input_clear_point =
        Point{input_frame.x + input_frame.width - 21.0F, input_frame.y + input_frame.height * 0.5F};
    static_cast<void>(router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move, .position = input_clear_point}));
    EXPECT_EQ(router.cursor_for_point(input_clear_point), PointerCursor::Hand);
    const auto input_clear_slot_edge =
        Point{input_frame.x + input_frame.width - 12.0F, input_clear_point.y};
    static_cast<void>(router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move, .position = input_clear_slot_edge}));
    EXPECT_NE(router.cursor_for_point(input_clear_slot_edge), PointerCursor::Hand);

    const auto select_frame = select.absolute_frame();
    const auto select_clear_point =
        Point{select_frame.x + select_frame.width - select.style().padding.right - 33.0F,
              select_frame.y + select_frame.height * 0.5F};
    static_cast<void>(router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move, .position = select_clear_point}));
    EXPECT_EQ(router.cursor_for_point(select_clear_point), PointerCursor::Hand);
    const auto select_clear_slot_edge =
        Point{select_frame.x + select_frame.width - select.style().padding.right - 23.5F,
              select_clear_point.y};
    static_cast<void>(router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move, .position = select_clear_slot_edge}));
    EXPECT_NE(router.cursor_for_point(select_clear_slot_edge), PointerCursor::Hand);
}

TEST(BasicControlsTests, InputCaretCommandKeepsStyleWidthWithAndWithoutText) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    RenderCommandRecorder empty_context;
    input.paint(empty_context);
    const auto* empty_caret = find_command(empty_context, RenderCommandType::FillPixelSnappedRect);
    ASSERT_NE(empty_caret, nullptr);
    EXPECT_FLOAT_EQ(command_rect(*empty_caret).width, input.style().caret_width);

    input.set_text("WinElement").set_caret_byte_offset(3U);

    RenderCommandRecorder text_context;
    input.paint(text_context);
    const auto* text_caret = find_command(text_context, RenderCommandType::FillPixelSnappedRect);
    ASSERT_NE(text_caret, nullptr);
    EXPECT_FLOAT_EQ(command_rect(*text_caret).width, input.style().caret_width);
}

TEST(BasicControlsTests, InputDragSelectsTextWithMouseCapture) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("abcdef");
    input.configure_layout([](LayoutElement& layout) { layout.set_width(Length::points(180.0F)); });
    input.calculate_layout(LayoutConstraints{.width = 180.0F});

    EventRouter router(input);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{12.0F, 12.0F},
                                                      .button = PointerButton::Primary,
                                                      .primary_button_down = true})
                    .handled);
    EXPECT_EQ(router.pointer_capture(), &input);

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                                      .position = Point{120.0F, 12.0F},
                                                      .primary_button_down = true})
                    .handled);
    EXPECT_TRUE(input.has_selection());
    EXPECT_LT(input.selection_anchor_byte_offset(), input.selection_active_byte_offset());

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{120.0F, 12.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_EQ(router.pointer_capture(), nullptr);
    EXPECT_TRUE(input.has_selection());
}

TEST(BasicControlsTests, InputPasswordSelectionDrawsMaskedFeedback) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input
        .set_text("a\xE4\xBD\xA0"
                  "b")
        .set_type(InputType::Password)
        .set_selection(0U, 4U);
    input.calculate_layout(LayoutConstraints{.width = 180.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    RenderCommandRecorder context;
    input.paint(context);
    EXPECT_NE(find_command(context, RenderCommandType::DrawTextLayout, "***"), nullptr);
    EXPECT_NE(std::find_if(context.commands().begin(), context.commands().end(),
                           [&](const auto& command) {
                               return command.type() == RenderCommandType::FillRect &&
                                      command_fill_color(command) == input.style().hover_background;
                           }),
              context.commands().end());

    EXPECT_FALSE(input.text_input_edit_command_state().can_copy);
}

TEST(BasicControlsTests, InputDragSelectionAutoScrollsTextarea) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) { layout.set_width(Length::points(180.0F)); });
    input.set_type(InputType::Textarea)
        .set_rows(2U)
        .set_autosize(false)
        .set_text("A\nB\nC\nD\nE\nF\nG\nH");
    input.calculate_layout(LayoutConstraints{.width = 180.0F});

    EventRouter router(input);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{10.0F, 10.0F},
                                                      .button = PointerButton::Primary,
                                                      .primary_button_down = true})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(
                        PointerEvent{.kind = PointerEventKind::Move,
                                     .position = Point{10.0F, input.frame().height + 48.0F},
                                     .primary_button_down = true})
                    .handled);

    EXPECT_TRUE(input.has_selection());
    EXPECT_GT(input.vertical_scroll_offset(), 0.0F);
}

TEST(BasicControlsTests, InputTextareaDragSelectsAcrossLines) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) { layout.set_width(Length::points(220.0F)); });
    input.set_type(InputType::Textarea).set_rows(3U).set_text("first\nsecond");
    input.calculate_layout(LayoutConstraints{.width = 220.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{14.0F, 7.0F},
                                                      .button = PointerButton::Primary,
                                                      .primary_button_down = true})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                                      .position = Point{100.0F, 60.0F},
                                                      .primary_button_down = true})
                    .handled);

    const auto newline_offset = input.text().find('\n');
    ASSERT_NE(newline_offset, std::string::npos);
    EXPECT_TRUE(input.has_selection());
    EXPECT_GT(std::max(input.selection_anchor_byte_offset(), input.selection_active_byte_offset()),
              newline_offset + 1U);
}

TEST(BasicControlsTests, InputCaretNavigationRespectsUtf8Boundaries) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("A\xE4\xBD\xA0");

    EventRouter router(input);
    EXPECT_TRUE(router.focus_manager().set_focus(&input));
    EXPECT_EQ(input.caret_byte_offset(), input.text().size());

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Left}).handled);
    EXPECT_EQ(input.caret_byte_offset(), 1U);

    EXPECT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Backspace})
                    .handled);
    EXPECT_EQ(input.text(), "\xE4\xBD\xA0");
    EXPECT_EQ(input.caret_byte_offset(), 0U);
}

TEST(BasicControlsTests, InputAndButtonUseCustomUiElementStyles) {
    auto engine = create_unrounded_engine();
    auto custom = default_input_style();
    custom.background = Color::rgba(1, 2, 3);
    custom.border_color = Color::rgba(4, 5, 6);
    custom.focus_border_color = Color::rgba(7, 8, 9);
    custom.text_color = Color::rgba(10, 11, 12);
    custom.caret_color = Color::rgba(13, 14, 15);

    Input input;

    input.bind_layout_tree(engine);
    input.set_text("Styled").set_style(custom);

    EventRouter router(input);
    EXPECT_TRUE(router.focus_manager().set_focus(&input));

    RenderCommandRecorder input_context;
    input.paint(input_context);
    ASSERT_FALSE(input_context.commands().empty());
    EXPECT_EQ(command_fill_color(input_context.commands()[0]), custom.background);
    EXPECT_EQ(command_stroke_color(input_context.commands()[1]), custom.focus_border_color);
    const auto* text_command =
        find_command(input_context, RenderCommandType::DrawTextLayout, "Styled");
    ASSERT_NE(text_command, nullptr);
    EXPECT_EQ(command_text_style(*text_command).color, custom.text_color);
    EXPECT_NE(std::find_if(input_context.commands().begin(), input_context.commands().end(),
                           [&](const auto& command) {
                               return command.type() == RenderCommandType::FillPixelSnappedRect &&
                                      command_fill_color(command) == custom.caret_color;
                           }),
              input_context.commands().end());

    auto button_style = default_button_style();
    button_style.background = Color::rgba(20, 21, 22);
    button_style.text_color = Color::rgba(23, 24, 25);

    Button button;

    button.bind_layout_tree(engine);
    button.set_style(button_style);
    button.calculate_layout();

    RenderCommandRecorder button_context;
    button.paint(button_context);
    ASSERT_GE(button_context.commands().size(), 5U);
    EXPECT_EQ(command_fill_color(button_context.commands()[0]), button_style.background);
    const auto* button_text_command =
        find_command(button_context, RenderCommandType::DrawTextLayout, "Button");
    ASSERT_NE(button_text_command, nullptr);
    EXPECT_EQ(command_text_style(*button_text_command).color, button_style.text_color);
}

TEST(BasicControlsTests, InputUsesSemanticStyleTokensForAuxiliaryAndInvalidStates) {
    auto engine = create_unrounded_engine();
    auto custom = default_input_style();
    custom.semantic.secondary_text = Color::rgba(1, 2, 3);
    custom.semantic.surface_subtle = Color::rgba(4, 5, 6);
    custom.semantic.danger = Color::rgba(7, 8, 9);

    Input input;

    input.bind_layout_tree(engine);
    input.set_style(custom)
        .set_text("value")
        .set_prefix_text("user")
        .set_suffix_text("id")
        .set_prepend_text("https://")
        .set_append_text(".com")
        .set_show_word_limit(true)
        .set_max_length(3U);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(360.0F), Length::points(40.0F));
    });
    input.calculate_layout(LayoutConstraints{.width = 360.0F, .height = 40.0F});

    RenderCommandRecorder context;
    input.paint(context);

    EXPECT_EQ(command_fill_color(context.commands()[0]), custom.semantic.surface_subtle);
    EXPECT_EQ(command_stroke_color(context.commands()[1]), custom.border_color);
    const auto* prepend_text = find_command(context, RenderCommandType::DrawText, "https://");
    ASSERT_NE(prepend_text, nullptr);
    EXPECT_EQ(command_text_style(*prepend_text).color, custom.semantic.secondary_text);

    const auto* prefix_text = find_command(context, RenderCommandType::DrawText, "user");
    ASSERT_NE(prefix_text, nullptr);
    EXPECT_EQ(command_text_style(*prefix_text).color, custom.semantic.secondary_text);

    const auto* suffix_text = find_command(context, RenderCommandType::DrawText, "id");
    ASSERT_NE(suffix_text, nullptr);
    EXPECT_EQ(command_text_style(*suffix_text).color, custom.semantic.secondary_text);

    const auto* word_limit_text = find_command(context, RenderCommandType::DrawText, "5 / 3");
    ASSERT_NE(word_limit_text, nullptr);
    EXPECT_EQ(command_text_style(*word_limit_text).color, custom.semantic.danger);

    const auto border_it = std::find_if(
        context.commands().begin(), context.commands().end(), [&](const auto& command) {
            return (command.type() == RenderCommandType::StrokeRoundedRect ||
                    command.type() == RenderCommandType::StrokeRect) &&
                   command_stroke_color(command) == custom.semantic.danger;
        });
    ASSERT_NE(border_it, context.commands().end());
    EXPECT_EQ(command_stroke_color(*border_it), custom.semantic.danger);
}

TEST(BasicControlsTests, InputUsesSemanticDisabledTextColor) {
    auto engine = create_unrounded_engine();
    auto custom = default_input_style();
    custom.semantic.disabled_text = Color::rgba(31, 32, 33);

    Input input;

    input.bind_layout_tree(engine);
    input.set_style(custom).set_text("Disabled").set_disabled(true);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(40.0F));
    });
    input.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 40.0F});

    RenderCommandRecorder context;
    input.paint(context);

    const auto* text_command = find_command(context, RenderCommandType::DrawTextLayout, "Disabled");
    ASSERT_NE(text_command, nullptr);
    EXPECT_EQ(command_text_style(*text_command).color, custom.semantic.disabled_text);
}

TEST(BasicControlsTests, ContextMenuUsesContextMenuSemanticDisabledTextColor) {
    auto engine = create_unrounded_engine();
    ContextMenu menu;
    menu.bind_layout_tree(engine);
    menu.set_items({ContextMenuItem{"Open", true}, ContextMenuItem{"Delete", false}});
    menu.calculate_layout();

    RenderCommandRecorder context;
    menu.paint(context);

    const auto* disabled_item = find_command(context, RenderCommandType::DrawText, "Delete");
    ASSERT_NE(disabled_item, nullptr);
    EXPECT_EQ(command_text_style(*disabled_item).color,
              default_context_menu_style().semantic.disabled_text);
}

TEST(BasicControlsTests, ContextMenuHoverBackgroundStaysInsidePopupBorder) {
    auto engine = create_unrounded_engine();
    ContextMenu menu;
    menu.bind_layout_tree(engine);
    menu.set_items({ContextMenuItem{.text = "Open"}, ContextMenuItem{.text = "Copy"}});
    menu.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 80.0F});

    RenderCommandRecorder context;
    menu.paint(context);

    const auto hover = default_context_menu_style().hover_background;
    const auto hover_command = std::find_if(
        context.commands().begin(), context.commands().end(), [&](const auto& command) {
            return command.type() == RenderCommandType::FillRoundedRect &&
                   command_fill_color(command) == hover;
        });
    ASSERT_NE(hover_command, context.commands().end());
    const auto hover_rect = command_rect(*hover_command);
    const auto menu_rect = menu.frame();
    EXPECT_GT(hover_rect.x, menu_rect.x);
    EXPECT_LT(hover_rect.x + hover_rect.width, menu_rect.x + menu_rect.width);
}

TEST(BasicControlsTests, ContextMenuSizesToLocalizedTextAndReturnsSelectedItem) {
    auto engine = create_unrounded_engine();
    ContextMenu menu;
    menu.bind_layout_tree(engine);
    menu.set_items({ContextMenuItem{.text = "A very long localized menu command", .id = "long"},
                    ContextMenuItem{.text = "Disabled", .enabled = false, .id = "disabled"}});

    const auto preferred = menu.preferred_size();
    EXPECT_GT(preferred.width, 136.0F);

    std::string selected_id;
    auto selected_index = std::numeric_limits<std::size_t>::max();
    menu.set_on_select(
        [&selected_id, &selected_index](const ContextMenuItem& item, std::size_t index) {
            selected_id = item.id;
            selected_index = index;
        });
    menu.calculate_layout(LayoutConstraints{.width = preferred.width, .height = preferred.height});

    EventRouter router(menu);
    ASSERT_TRUE(router.focus_manager().set_focus(&menu));
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Enter}).handled);
    EXPECT_EQ(selected_id, "long");
    EXPECT_EQ(selected_index, 0U);
}

TEST(BasicControlsTests, ContextMenuPaintsItemMetadata) {
    auto engine = create_unrounded_engine();
    ContextMenu menu;
    menu.bind_layout_tree(engine);
    menu.set_items(
        {ContextMenuItem{.text = "Checked", .checkable = true, .checked = true},
         ContextMenuItem{.text = "Radio", .checkable = true, .checked = true, .radio = true},
         ContextMenuItem{.separator = true},
         ContextMenuItem{.text = "Copy", .shortcut_text = "Ctrl+C"},
         ContextMenuItem{.text = "More", .submenu = {ContextMenuItem{.text = "Nested"}}}});
    menu.calculate_layout();

    RenderCommandRecorder context;
    menu.paint(context);

    EXPECT_NE(find_command(context, RenderCommandType::DrawText, "Ctrl+C"), nullptr);
    EXPECT_NE(find_command(context, RenderCommandType::DrawText, "More"), nullptr);
    EXPECT_GE(command_count(context, RenderCommandType::DrawLine), 1U);
    EXPECT_GE(command_count(context, RenderCommandType::FillGeometry), 2U);
    EXPECT_GE(command_count(context, RenderCommandType::FillEllipse), 1U);
    EXPECT_GE(command_count(context, RenderCommandType::StrokeEllipse), 1U);
}

TEST(BasicControlsTests, ContextMenuConsumesPaddingWheelAndSkipsUnchangedHoverPaint) {
    auto engine = create_unrounded_engine();
    ContextMenu menu;
    menu.bind_layout_tree(engine);
    menu.set_items({ContextMenuItem{.text = "Cut", .id = "cut"},
                    ContextMenuItem{.text = "Copy", .id = "copy"}});
    menu.calculate_layout();

    EventRouter router(menu);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Wheel,
                                                      .position = Point{8.0F, 2.0F},
                                                      .wheel_delta = Point{0.0F, -1.0F}})
                    .handled);

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                                      .position = Point{8.0F, 12.0F}})
                    .handled);
    menu.clear_paint_dirty();
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                                      .position = Point{8.0F, 12.0F}})
                    .handled);
    EXPECT_FALSE(menu.needs_paint());
}

TEST(BasicControlsTests, ContextMenuHitTestUsesActualLayoutWidth) {
    auto engine = create_unrounded_engine();
    ContextMenu menu;
    menu.bind_layout_tree(engine);
    menu.set_items({ContextMenuItem{.text = "Open"}, ContextMenuItem{.text = "Copy"}});
    menu.configure_layout([](LayoutElement& layout) { layout.set_width(Length::points(260.0F)); });
    menu.calculate_layout(
        LayoutConstraints{.width = 260.0F, .height = menu.preferred_size().height});

    const auto hit = menu.item_at(Point{220.0F, menu.metrics().vertical_padding + 10.0F});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, 0U);
}

TEST(BasicControlsTests, ContextMenuOpensSubmenuIntoOwnedTopLayer) {
    auto engine = create_unrounded_engine();
    ContextMenu menu;
    menu.bind_layout_tree(engine);
    menu.set_items(
        {ContextMenuItem{.text = "Open"},
         ContextMenuItem{.text = "More", .submenu = {ContextMenuItem{.text = "Nested"}}}});
    menu.calculate_layout();

    EventRouter router(menu);
    const auto more_y = menu.metrics().vertical_padding + menu.metrics().item_height * 1.5F;
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                                      .position = Point{24.0F, more_y}})
                    .handled);

    ASSERT_EQ(menu.top_layer_count(), 1U);
    const auto& submenu = static_cast<const ContextMenu&>(menu.top_layer_at(0U));
    ASSERT_EQ(submenu.items().size(), 1U);
    EXPECT_EQ(submenu.items().front().text, "Nested");
}

TEST(BasicControlsTests, ContextMenuSubmenuTracksOwnerScrollOffset) {
    auto engine = create_unrounded_engine();
    StackPanel root;
    root.bind_layout_tree(engine);
    root.set_viewport(Rect{0.0F, 0.0F, 240.0F, 80.0F}).set_overflow(Overflow::Hidden);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(240.0F), Length::points(80.0F));
    });

    auto& spacer = root.append_new_child<Panel>();
    spacer.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(16.0F)).set_flex_shrink(0.0F);
    });
    auto& menu = root.append_new_child<ContextMenu>();
    menu.set_items(
        {ContextMenuItem{.text = "Open"},
         ContextMenuItem{.text = "More", .submenu = {ContextMenuItem{.text = "Nested"}}}});
    menu.configure_layout([](LayoutElement& layout) { layout.set_flex_shrink(0.0F); });
    auto& tail = root.append_new_child<Panel>();
    tail.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(80.0F)).set_flex_shrink(0.0F);
    });
    root.calculate_layout(LayoutConstraints{.width = 240.0F, .height = 80.0F});

    EventRouter router(menu);
    const auto menu_frame = menu.absolute_frame();
    const auto more_y =
        menu_frame.y + menu.metrics().vertical_padding + menu.metrics().item_height * 1.5F;
    EXPECT_TRUE(
        router
            .route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                              .position = Point{menu_frame.x + 24.0F, more_y}})
            .handled);

    ASSERT_EQ(root.top_layer_count(), 1U);
    auto& submenu = root.top_layer_at(0U);
    const auto before_scroll_bounds = root.top_layer_bounds(submenu);

    root.set_scroll_offset(Point{0.0F, 24.0F});

    const auto after_scroll_bounds = root.top_layer_bounds(submenu);
    EXPECT_FLOAT_EQ(after_scroll_bounds.x, before_scroll_bounds.x);
    EXPECT_FLOAT_EQ(after_scroll_bounds.y, before_scroll_bounds.y - 24.0F);
}

TEST(BasicControlsTests, ContextMenuNestedSubmenuSelectionDismissesSafely) {
    auto engine = create_unrounded_engine();
    ContextMenu menu;
    menu.bind_layout_tree(engine);
    menu.set_items(
        {ContextMenuItem{.text = "Open"},
         ContextMenuItem{.text = "More", .submenu = {ContextMenuItem{.text = "Nested"}}}});

    std::string selected_text;
    menu.set_on_select(
        [&selected_text](const ContextMenuItem& item, std::size_t) { selected_text = item.text; });
    menu.calculate_layout();

    EventRouter router(menu);
    const auto more_y = menu.metrics().vertical_padding + menu.metrics().item_height * 1.5F;
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                                      .position = Point{24.0F, more_y}})
                    .handled);
    ASSERT_EQ(menu.top_layer_count(), 1U);

    auto& submenu = menu.top_layer_at(0U);
    const auto submenu_bounds = menu.top_layer_bounds(submenu);
    const auto nested_position =
        Point{submenu_bounds.x + 18.0F, submenu_bounds.y + menu.metrics().vertical_padding + 10.0F};
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = nested_position,
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = nested_position,
                                                      .button = PointerButton::Primary})
                    .handled);

    EXPECT_EQ(selected_text, "Nested");
    EXPECT_EQ(menu.top_layer_count(), 0U);
}

TEST(BasicControlsTests, SelectDrawsClearAndLoadingBuiltInSvgIcons) {
    auto engine = create_unrounded_engine();
    Select select;
    select.bind_layout_tree(engine);
    select.set_options({SelectOption{.label = "One", .value = "1"}})
        .set_selected_index(0U)
        .set_clearable(true);
    select.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(36.0F));
    });
    select.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 36.0F});

    EventRouter router(select);
    router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move, .position = Point{12.0F, 12.0F}});

    RenderCommandRecorder clear_context;
    select.paint(clear_context);
    EXPECT_GE(command_count(clear_context, RenderCommandType::FillGeometry), 2U);

    select.set_loading(true);
    select.clear_paint_dirty_subtree();
    const auto now = winelement::animation::AnimationClockType::now();
    EXPECT_TRUE(select.tick_animations(now + std::chrono::milliseconds(80)));

    RenderCommandRecorder loading_context;
    select.paint(loading_context);
    EXPECT_GT(command_count(loading_context, RenderCommandType::FillGeometry), 0U);
    EXPECT_GT(command_count(loading_context, RenderCommandType::PushLayer), 0U);
}

TEST(BasicControlsTests, SelectMultipleTagCloseRemovesSelectedItem) {
    auto engine = create_unrounded_engine();
    Select select;
    select.bind_layout_tree(engine);
    select
        .set_options(
            {SelectOption{.label = "A", .value = "a"}, SelectOption{.label = "B", .value = "b"}})
        .set_multiple(true)
        .set_tags_visible(true)
        .set_selected_indices({0U, 1U});
    select.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(36.0F));
    });
    select.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 36.0F});

    EventRouter router(select);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Click,
                                                      .position = Point{36.0F, 18.0F},
                                                      .button = PointerButton::Primary})
                    .handled);

    EXPECT_EQ(select.selected_indices(), (std::vector<std::size_t>{1U}));
    EXPECT_FALSE(select.popup_open());
}

TEST(BasicControlsTests, SelectMultipleTagCloseUsesInputClearAffordance) {
    auto engine = create_unrounded_engine();
    Select select;
    select.bind_layout_tree(engine);
    select
        .set_options(
            {SelectOption{.label = "A", .value = "a"}, SelectOption{.label = "B", .value = "b"}})
        .set_multiple(true)
        .set_tags_visible(true)
        .set_selected_indices({0U, 1U});
    select.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(36.0F));
    });
    select.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 36.0F});

    EventRouter router(select);
    router.route_pointer_event(
        PointerEvent{.kind = PointerEventKind::Move, .position = Point{36.0F, 18.0F}});

    RenderCommandRecorder context;
    select.paint(context);
    const auto small_hover_affordance = std::find_if(
        context.commands().begin(), context.commands().end(), [&select](const auto& command) {
            return command.type() == RenderCommandType::FillRoundedRect &&
                   command_fill_color(command) == select.style().hover_background &&
                   command_rect(command).width <= 18.0F && command_rect(command).height <= 18.0F;
        });

    EXPECT_EQ(small_hover_affordance, context.commands().end());
    EXPECT_GT(command_count(context, RenderCommandType::FillGeometry), 0U);

    router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                            .position = Point{36.0F, 18.0F},
                                            .button = PointerButton::Primary});

    RenderCommandRecorder pressed_context;
    select.paint(pressed_context);
    const auto active_affordance =
        std::find_if(pressed_context.commands().begin(), pressed_context.commands().end(),
                     [&select](const auto& command) {
                         return command.type() == RenderCommandType::FillRoundedRect &&
                                command_fill_color(command) == select.style().active_background &&
                                command_rect(command).width <= 18.0F &&
                                command_rect(command).height <= 18.0F;
                     });
    EXPECT_EQ(active_affordance, pressed_context.commands().end());
}

TEST(BasicControlsTests, SwitchMeasuresBothStateLabelsBeforeToggle) {
    auto engine = create_unrounded_engine();
    Switch switch_control;
    switch_control.bind_layout_tree(engine);
    switch_control.set_active_text("I").set_inactive_text("Inactive").set_checked(true);
    switch_control.calculate_layout();

    switch_control.set_checked(false);

    RenderCommandRecorder context;
    switch_control.paint(context);

    const auto* inactive_text = find_command(context, RenderCommandType::DrawText, "Inactive");
    ASSERT_NE(inactive_text, nullptr);
    EXPECT_GT(command_rect(*inactive_text).width, 40.0F);
}

TEST(BasicControlsTests, SelectFilterAcceptsTextWhenDropdownOwnsFocus) {
    auto engine = create_unrounded_engine();
    Select select;
    select.bind_layout_tree(engine);
    select
        .set_options({SelectOption{.label = "Primary", .value = "primary"},
                      SelectOption{.label = "Warning", .value = "warning"},
                      SelectOption{.label = "Danger", .value = "danger"}})
        .set_filterable(true);
    select.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(36.0F));
    });
    select.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 36.0F});

    EventRouter router(select);
    ASSERT_TRUE(router.focus_manager().set_focus(&select));
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Down}).handled);
    ASSERT_TRUE(select.popup_open());
    ASSERT_EQ(select.top_layer_count(), 1U);

    auto& dropdown = select.top_layer_at(0U);
    ASSERT_TRUE(router.focus_manager().set_focus(&dropdown));
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "war"}).handled);
    EXPECT_EQ(select.filter_text(), "war");
    EXPECT_EQ(select.filtered_option_count(), 1U);

    EXPECT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Backspace})
                    .handled);
    EXPECT_EQ(select.filter_text(), "wa");
}

TEST(BasicControlsTests, SelectFilterCaretBlinksWhileDropdownIsActive) {
    auto engine = create_unrounded_engine();
    Select select;
    select.bind_layout_tree(engine);
    select
        .set_options({SelectOption{.label = "Primary", .value = "primary"},
                      SelectOption{.label = "Warning", .value = "warning"}})
        .set_filterable(true);
    select.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(36.0F));
    });
    select.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 36.0F});

    EventRouter router(select);
    ASSERT_TRUE(router.focus_manager().set_focus(&select));
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Down}).handled);
    ASSERT_TRUE(select.popup_open());
    ASSERT_EQ(select.top_layer_count(), 1U);
    auto& dropdown = select.top_layer_at(0U);

    RenderCommandRecorder visible_context;
    dropdown.paint(visible_context);
    EXPECT_GT(command_count(visible_context, RenderCommandType::FillPixelSnappedRect), 0U);

    const auto now = winelement::animation::AnimationClockType::now();
    EXPECT_TRUE(dropdown.tick_animations(now + std::chrono::milliseconds(600)));
    RenderCommandRecorder hidden_context;
    dropdown.paint(hidden_context);
    EXPECT_EQ(command_count(hidden_context, RenderCommandType::FillPixelSnappedRect), 0U);

    EXPECT_TRUE(dropdown.tick_animations(now + std::chrono::milliseconds(1100)));
    RenderCommandRecorder shown_context;
    dropdown.paint(shown_context);
    EXPECT_GT(command_count(shown_context, RenderCommandType::FillPixelSnappedRect), 0U);
}

TEST(BasicControlsTests, NewControlsUseCurrentThemeDefaultsAtConstruction) {
    ScopedThemeReset reset;
    auto theme = make_default_theme();
    auto button_style = require_theme_style(theme, theme_class::button);
    button_style.background = Color::rgba(1, 2, 3);
    set_theme_style_class(theme, theme_class::button, button_style);
    auto input_style = require_theme_style(theme, theme_class::input);
    input_style.focus_border_color = Color::rgba(4, 5, 6);
    set_theme_style_class(theme, theme_class::input, input_style);
    auto text_style = require_theme_style(theme, theme_class::text);
    text_style.text_color = Color::rgba(7, 8, 9);
    set_theme_style_class(theme, theme_class::text, text_style);
    auto panel_style = require_theme_style(theme, theme_class::panel);
    panel_style.background = Color::rgba(10, 11, 12);
    panel_style.border_width = 3.0F;
    set_theme_style_class(theme, theme_class::panel, panel_style);

    set_theme(theme);

    auto engine = create_unrounded_engine();
    Button button;
    button.bind_layout_tree(engine);
    Input input;
    input.bind_layout_tree(engine);
    Text text;
    text.bind_layout_tree(engine);
    Panel panel;
    panel.bind_layout_tree(engine);

    EXPECT_EQ(button.style().background, button_style.background);
    EXPECT_EQ(input.style().focus_border_color, input_style.focus_border_color);
    EXPECT_EQ(text.style().text_color, text_style.text_color);
    EXPECT_EQ(panel.style().background, panel_style.background);
    EXPECT_FLOAT_EQ(panel.style().border_width, panel_style.border_width);
}

TEST(BasicControlsTests, BuiltInDarkThemeFlowsIntoNewControlsAndContextMenu) {
    ScopedThemeReset reset;
    set_theme(make_dark_theme());

    auto engine = create_unrounded_engine();
    Button button;
    button.bind_layout_tree(engine);
    Input input;
    input.bind_layout_tree(engine);
    Text text;
    text.bind_layout_tree(engine);
    ContextMenu menu;
    menu.bind_layout_tree(engine);
    menu.set_items({ContextMenuItem{"Delete", false}});
    menu.calculate_layout();

    const auto& dark = current_theme();
    const auto& dark_button = require_theme_style(dark, theme_class::button);
    const auto& dark_input = require_theme_style(dark, theme_class::input);
    const auto& dark_context_menu = require_theme_style(dark, theme_class::context_menu);
    const auto& dark_text = require_theme_style(dark, theme_class::text);
    EXPECT_EQ(button.style().background, dark_button.background);
    EXPECT_EQ(input.style().text_color, dark_input.text_color);
    EXPECT_EQ(text.style().text_color, dark_text.text_color);

    RenderCommandRecorder context;
    menu.paint(context);

    const auto* background = find_command(context, RenderCommandType::FillRoundedRect);
    ASSERT_NE(background, nullptr);
    EXPECT_EQ(command_fill_color(*background), dark_context_menu.background);
    const auto* border = find_command(context, RenderCommandType::StrokeRoundedRect);
    ASSERT_NE(border, nullptr);
    EXPECT_EQ(command_stroke_color(*border), dark_context_menu.border_color);

    const auto* disabled_item = find_command(context, RenderCommandType::DrawText, "Delete");
    ASSERT_NE(disabled_item, nullptr);
    EXPECT_EQ(command_text_style(*disabled_item).color, dark_context_menu.semantic.disabled_text);
}

TEST(BasicControlsTests, ThemeManagerReappliesThemeToExistingControlTree) {
    ScopedThemeReset reset;

    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(320.0F), Length::points(180.0F));
    });

    auto button = std::make_unique<Button>();
    auto& button_ref = static_cast<Button&>(root.append_child(std::move(button)));

    auto input = std::make_unique<Input>();
    input->set_text("theme");
    auto& input_ref = static_cast<Input&>(root.push_top_layer(std::move(input), TopLayerOptions{}));

    auto custom_text = std::make_unique<Text>();
    custom_text->set_text("Static");
    auto custom_style = default_text_style();
    custom_style.text_color = Color::rgba(1, 2, 3);
    custom_text->set_style(custom_style);
    auto& custom_text_ref = static_cast<Text&>(root.append_child(std::move(custom_text)));

    ThemeManager::apply_theme(root, make_dark_theme());

    const auto& dark = current_theme();
    const auto& dark_panel = require_theme_style(dark, theme_class::panel);
    const auto& dark_button = require_theme_style(dark, theme_class::button);
    const auto& dark_input = require_theme_style(dark, theme_class::input);
    const auto& dark_text = require_theme_style(dark, theme_class::text);
    EXPECT_EQ(root.style().border_color, dark_panel.border_color);
    EXPECT_EQ(button_ref.style().background, dark_button.background);
    EXPECT_EQ(input_ref.style().text_color, dark_input.text_color);
    EXPECT_EQ(custom_text_ref.style().text_color, custom_style.text_color);
    EXPECT_FALSE(custom_text_ref.theme_managed());

    custom_text_ref.set_theme_managed(true);
    ThemeManager::reapply_current_theme(root);

    EXPECT_TRUE(custom_text_ref.theme_managed());
    EXPECT_EQ(custom_text_ref.style().text_color, dark_text.text_color);
}

TEST(BasicControlsTests, LocalThemeScopesCascadeThroughPanelAndTopLayerSubtrees) {
    ScopedThemeReset reset;

    auto global = make_default_theme();
    auto global_button = require_theme_style(global, theme_class::button);
    global_button.background = Color::rgba(11, 12, 13);
    set_theme_style_class(global, theme_class::button, global_button);
    auto global_input = require_theme_style(global, theme_class::input);
    global_input.text_color = Color::rgba(14, 15, 16);
    set_theme_style_class(global, theme_class::input, global_input);
    set_theme(global);

    auto panel_theme = make_dark_theme();
    auto panel_button = require_theme_style(panel_theme, theme_class::button);
    panel_button.background = Color::rgba(21, 22, 23);
    set_theme_style_class(panel_theme, theme_class::button, panel_button);
    auto panel_style = require_theme_style(panel_theme, theme_class::panel);
    panel_style.border_color = Color::rgba(24, 25, 26);
    set_theme_style_class(panel_theme, theme_class::panel, panel_style);

    auto popup_theme = make_dark_theme();
    auto popup_input_style = require_theme_style(popup_theme, theme_class::input);
    popup_input_style.text_color = Color::rgba(31, 32, 33);
    set_theme_style_class(popup_theme, theme_class::input, popup_input_style);

    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(320.0F), Length::points(180.0F));
    });

    auto scoped_panel = std::make_unique<Panel>();
    scoped_panel->set_background(Color::rgba(41, 42, 43));
    scoped_panel->set_local_theme(panel_theme);
    auto& scoped_panel_ref = static_cast<Panel&>(root.append_child(std::move(scoped_panel)));

    auto scoped_button = std::make_unique<Button>();
    auto& scoped_button_ref =
        static_cast<Button&>(scoped_panel_ref.append_child(std::move(scoped_button)));

    auto outside_button = std::make_unique<Button>();
    auto& outside_button_ref = static_cast<Button&>(root.append_child(std::move(outside_button)));

    auto popup_input = std::make_unique<Input>();
    popup_input->set_local_theme(popup_theme);
    popup_input->set_text("scope");
    auto& popup_input_ref =
        static_cast<Input&>(root.push_top_layer(std::move(popup_input), TopLayerOptions{}));

    ThemeManager::reapply_current_theme(root);

    EXPECT_FALSE(scoped_panel_ref.theme_managed());
    EXPECT_TRUE(scoped_panel_ref.has_local_theme());
    EXPECT_EQ(scoped_panel_ref.background(), Color::rgba(41, 42, 43));
    EXPECT_EQ(scoped_button_ref.style().background, panel_button.background);
    EXPECT_EQ(outside_button_ref.style().background, global_button.background);
    EXPECT_EQ(popup_input_ref.style().text_color, popup_input_style.text_color);
}

TEST(BasicControlsTests, PopupManagerPopupInheritsLogicalOwnerThemeScopeOnOpenAndReapply) {
    ScopedThemeReset reset;

    auto global = make_default_theme();
    auto global_button = require_theme_style(global, theme_class::button);
    global_button.background = Color::rgba(11, 12, 13);
    set_theme_style_class(global, theme_class::button, global_button);
    set_theme(global);

    auto scoped = make_dark_theme();
    auto scoped_button = require_theme_style(scoped, theme_class::button);
    scoped_button.background = Color::rgba(21, 22, 23);
    set_theme_style_class(scoped, theme_class::button, scoped_button);

    auto updated = make_dark_theme();
    auto updated_button = require_theme_style(updated, theme_class::button);
    updated_button.background = Color::rgba(31, 32, 33);
    set_theme_style_class(updated, theme_class::button, updated_button);

    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(320.0F), Length::points(180.0F));
    });

    auto scoped_panel = std::make_unique<Panel>();
    scoped_panel->set_local_theme(scoped);
    auto& scoped_panel_ref = static_cast<Panel&>(root.append_child(std::move(scoped_panel)));

    PopupManager popup_manager(scoped_panel_ref);
    auto popup = std::make_unique<Button>();
    const auto opened = popup_manager.open(
        std::move(popup), PopupOptions{.anchor_rect = Rect{12.0F, 14.0F, 1.0F, 1.0F},
                                       .size = Size{88.0F, 32.0F},
                                       .placement = PopupPlacement::BottomStart,
                                       .light_dismiss = false});
    root.calculate_layout(LayoutConstraints{.width = 320.0F, .height = 180.0F});

    ASSERT_TRUE(opened.handle.valid());
    ASSERT_EQ(root.top_layer_count(), 1U);
    auto& popup_button_ref = static_cast<Button&>(root.top_layer_at(0U));
    EXPECT_EQ(popup_button_ref.style().background, scoped_button.background);

    scoped_panel_ref.set_local_theme(updated);
    ThemeManager::reapply_current_theme(root);

    EXPECT_EQ(popup_button_ref.style().background, updated_button.background);
}

TEST(BasicControlsTests, PopupManagerPopupClosesWhenLogicalOwnerIsRemoved) {
    ScopedThemeReset reset;

    auto global = make_default_theme();
    auto global_button = require_theme_style(global, theme_class::button);
    global_button.background = Color::rgba(41, 42, 43);
    set_theme_style_class(global, theme_class::button, global_button);
    set_theme(global);

    auto scoped = make_dark_theme();
    auto scoped_button = require_theme_style(scoped, theme_class::button);
    scoped_button.background = Color::rgba(51, 52, 53);
    set_theme_style_class(scoped, theme_class::button, scoped_button);

    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(320.0F), Length::points(180.0F));
    });

    auto scoped_panel = std::make_unique<Panel>();
    scoped_panel->set_local_theme(scoped);
    auto& scoped_panel_ref = static_cast<Panel&>(root.append_child(std::move(scoped_panel)));

    PopupManager popup_manager(scoped_panel_ref);
    auto popup = std::make_unique<Button>();
    const auto opened = popup_manager.open(
        std::move(popup), PopupOptions{.anchor_rect = Rect{20.0F, 20.0F, 1.0F, 1.0F},
                                       .size = Size{88.0F, 32.0F},
                                       .placement = PopupPlacement::BottomStart,
                                       .light_dismiss = false});
    root.calculate_layout(LayoutConstraints{.width = 320.0F, .height = 180.0F});

    ASSERT_TRUE(opened.handle.valid());
    ASSERT_EQ(root.top_layer_count(), 1U);
    auto& popup_button_ref = static_cast<Button&>(root.top_layer_at(0U));
    EXPECT_EQ(popup_button_ref.style().background, scoped_button.background);

    auto removed_scope = root.remove_child(scoped_panel_ref);
    removed_scope.reset();

    EXPECT_EQ(root.top_layer_count(), 0U);
}

TEST(BasicControlsTests, UiElementCopyFeedsSharedClipboardForInputPaste) {
    auto engine = create_unrounded_engine();
    auto clipboard = std::make_shared<TextClipboardService>();
    UIElement source;
    source.bind_layout_tree(engine);
    source.set_text_clipboard_service(clipboard);
    source.set_text("UIElement copy -> Input paste")
        .set_text_selectable(true)
        .set_text_selection(0U, source.text().size());

    EXPECT_TRUE(source.invoke_text_input_edit_command(TextInputEditCommand::Copy));

    Input input;

    input.bind_layout_tree(engine);
    input.set_text_clipboard_service(clipboard);
    EXPECT_TRUE(input.invoke_text_input_edit_command(TextInputEditCommand::Paste));
    EXPECT_EQ(input.text(), "UIElement copy -> Input paste");
}

TEST(BasicControlsTests, ControlsApplyVisualStyleFromUiElementStyles) {
    auto engine = create_unrounded_engine();
    auto button_style = default_button_style();
    button_style.visual.opacity = 0.6F;
    button_style.visual.transform = Transform2D::translation(12.0F, 0.0F);
    button_style.visual.layer_enabled = true;

    Button button;

    button.bind_layout_tree(engine);
    button.set_style(button_style);
    button.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(36.0F));
    });
    button.calculate_layout();

    EXPECT_FLOAT_EQ(button.opacity(), 0.6F);
    EXPECT_EQ(button.render_transform(), button_style.visual.transform);
    EXPECT_TRUE(button.layer_enabled());

    RenderCommandList command_list;
    button.commit_render_commands(command_list);

    ASSERT_FALSE(command_list.commands().empty());
    EXPECT_EQ(command_list.commands().front().type(), RenderCommandType::PushLayer);
    const auto& layer = command_list.commands().front().payload<PushLayerCommand>().options;
    EXPECT_FLOAT_EQ(layer.opacity, 0.6F);
    EXPECT_FALSE(is_identity_transform(layer.transform));
}

TEST(BasicControlsTests, PanelUsesRectangleStyleTokens) {
    auto engine = create_unrounded_engine();
    auto panel_style = default_panel_style();
    panel_style.background = Color::rgba(250, 251, 252);
    panel_style.border_color = Color::rgba(64, 158, 255);
    panel_style.border_width = 1.0F;
    panel_style.corner_radius = CornerRadius::uniform(6.0F);
    panel_style.shadow_visible = true;
    panel_style.shadow = ShadowStyle{
        .color = Color::rgba(0, 0, 0, 32), .offset = {0.0F, 2.0F}, .blur_radius = 10.0F};

    Panel panel;

    panel.bind_layout_tree(engine);
    panel.set_style(panel_style);
    panel.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(48.0F));
    });
    panel.calculate_layout();

    EXPECT_EQ(panel.style().background, panel_style.background);
    EXPECT_EQ(panel.background(), panel_style.background);
    EXPECT_EQ(panel.border_color(), panel_style.border_color);
    EXPECT_FLOAT_EQ(panel.border_width(), panel_style.border_width);

    RenderCommandRecorder context;
    panel.paint(context);

    ASSERT_EQ(context.commands().size(), 3U);
    EXPECT_EQ(context.commands()[0].type(), RenderCommandType::DrawBoxShadow);
    EXPECT_EQ(context.commands()[1].type(), RenderCommandType::FillRoundedRect);
    EXPECT_EQ(context.commands()[2].type(), RenderCommandType::StrokeRoundedRect);
    EXPECT_EQ(command_fill_color(context.commands()[1]), panel_style.background);
    EXPECT_EQ(command_stroke_color(context.commands()[2]), panel_style.border_color);
}

TEST(BasicControlsTests, ButtonTextLayoutUsesFullHeightForDescenders) {
    auto engine = create_unrounded_engine();
    Button button;
    button.bind_layout_tree(engine);
    button.set_text("Save");
    button.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(48.0F));
    });
    button.calculate_layout();

    RenderCommandRecorder context;
    button.paint(context);

    const auto* text_command = find_command(context, RenderCommandType::DrawTextLayout, "Save");
    ASSERT_NE(text_command, nullptr);
    const auto content_rect =
        Rect{button.style().padding.left, button.style().padding.top,
             button.frame().width - button.style().padding.left - button.style().padding.right,
             button.frame().height - button.style().padding.top - button.style().padding.bottom};
    EXPECT_FLOAT_EQ(command_rect(*text_command).x, content_rect.x);
    EXPECT_FLOAT_EQ(command_rect(*text_command).y, 0.0F);
    const auto* layout = text_command->payload<DrawTextLayoutCommand>().layout_value();
    ASSERT_NE(layout, nullptr);
    EXPECT_FLOAT_EQ(layout->options.max_height, button.frame().height);
    EXPECT_EQ(command_text_style(*text_command).alignment, TextAlignment::Center);
    EXPECT_EQ(command_text_style(*text_command).vertical_alignment, TextVerticalAlignment::Center);
    EXPECT_EQ(command_text_style(*text_command).trimming, TextTrimming::CharacterEllipsis);
}

TEST(BasicControlsTests, ButtonUsesSvgIconAndElementPlusActiveBorder) {
    auto engine = create_unrounded_engine();
    Button button;
    button.bind_layout_tree(engine);
    button.set_text("Save").set_icon_data("M0 0 L10 0 L10 10 Z");
    button.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(120.0F), Length::points(40.0F));
    });
    button.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 40.0F});

    RenderCommandRecorder icon_context;
    button.paint(icon_context);
    EXPECT_GE(command_count(icon_context, RenderCommandType::FillGeometry), 1U);

    EventRouter router(button);
    ASSERT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{8.0F, 8.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    const auto now = winelement::animation::AnimationClockType::now();
    EXPECT_TRUE(button.tick_animations(now + std::chrono::milliseconds(80)));

    RenderCommandRecorder active_context;
    button.paint(active_context);
    const auto active_border =
        std::find_if(active_context.commands().begin(), active_context.commands().end(),
                     [&](const auto& command) {
                         return command.type() == RenderCommandType::StrokeRoundedRect &&
                                command_stroke_color(command) == button.style().focus_border_color;
                     });
    ASSERT_NE(active_border, active_context.commands().end());

    ASSERT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{8.0F, 8.0F},
                                                      .button = PointerButton::Primary})
                    .handled);

    RenderCommandRecorder click_context;
    button.paint(click_context);
    EXPECT_GE(command_count(click_context, RenderCommandType::FillGeometry), 1U);
    EXPECT_EQ(command_count(click_context, RenderCommandType::FillRoundedRect),
              command_count(icon_context, RenderCommandType::FillRoundedRect));
}

TEST(BasicControlsTests, InputSupportsClearPasswordAffixesAndCallbacks) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) { layout.set_width(Length::points(320.0F)); });
    input.set_text("secret")
        .set_type(InputType::Password)
        .set_show_password_toggle(true)
        .set_clearable(true)
        .set_prefix_text("user")
        .set_suffix_text("id")
        .set_prepend_text("https://")
        .set_append_text(".com");

    auto input_count = 0;
    auto change_count = 0;
    auto clear_count = 0;
    input
        .set_on_input([&](std::string_view value) {
            ++input_count;
            EXPECT_TRUE(value.empty());
        })
        .set_on_change([&](std::string_view value) {
            ++change_count;
            EXPECT_TRUE(value.empty());
        })
        .set_on_clear([&]() { ++clear_count; });

    input.calculate_layout(LayoutConstraints{.width = 320.0F});
    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    RenderCommandRecorder masked_context;
    input.paint(masked_context);
    EXPECT_NE(find_command(masked_context, RenderCommandType::DrawText, "https://"), nullptr);
    EXPECT_NE(find_command(masked_context, RenderCommandType::DrawText, ".com"), nullptr);
    EXPECT_NE(find_command(masked_context, RenderCommandType::DrawTextLayout, "******"), nullptr);
    EXPECT_EQ(find_command(masked_context, RenderCommandType::DrawText, "x"), nullptr);
    EXPECT_EQ(find_command(masked_context, RenderCommandType::DrawText, "*"), nullptr);
    EXPECT_GE(command_count(masked_context, RenderCommandType::FillGeometry), 2U);

    input.toggle_password_visibility();
    RenderCommandRecorder visible_context;
    input.paint(visible_context);
    EXPECT_NE(find_command(visible_context, RenderCommandType::DrawTextLayout, "secret"), nullptr);
    EXPECT_EQ(find_command(visible_context, RenderCommandType::DrawText, "o"), nullptr);
    EXPECT_GE(command_count(visible_context, RenderCommandType::FillGeometry), 2U);

    input.clear();
    EXPECT_TRUE(input.text().empty());
    EXPECT_EQ(input_count, 1);
    EXPECT_EQ(change_count, 1);
    EXPECT_EQ(clear_count, 1);
}

TEST(BasicControlsTests, InputAppliesFormatterParserLengthAndWordLimit) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) { layout.set_width(Length::points(240.0F)); });
    input.set_max_length(3U)
        .set_show_word_limit(true)
        .set_formatter([](std::string_view value) { return "$" + std::string(value); })
        .set_parser([](std::string_view value) {
            std::string parsed;
            for (const auto character : value) {
                if (character != '$' && character != ',') {
                    parsed.push_back(character);
                }
            }
            return parsed;
        });

    auto last_input = std::string{};
    input.set_on_input([&](std::string_view value) { last_input = std::string(value); });

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "$"}).handled);
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "1"}).handled);
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "2"}).handled);
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "3"}).handled);
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "4"}).handled);

    EXPECT_EQ(input.text(), "123");
    EXPECT_EQ(last_input, "123");
    EXPECT_EQ(input.text_length(), 3U);
    EXPECT_TRUE(input.length_valid());

    input.calculate_layout(LayoutConstraints{.width = 240.0F});
    RenderCommandRecorder context;
    input.paint(context);
    EXPECT_NE(find_command(context, RenderCommandType::DrawTextLayout, "$123"), nullptr);
    EXPECT_NE(find_command(context, RenderCommandType::DrawText, "3 / 3"), nullptr);
}

TEST(BasicControlsTests, InputFormatterKeepsCaretAtRenderedTextEnd) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(40.0F));
    });
    input.set_formatter([](std::string_view value) { return "$" + std::string(value); })
        .set_parser([](std::string_view value) {
            std::string parsed;
            for (const auto character : value) {
                if (character != '$') {
                    parsed.push_back(character);
                }
            }
            return parsed;
        })
        .set_text("123");
    input.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 40.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));
    ASSERT_EQ(input.caret_byte_offset(), input.text().size());

    RenderCommandRecorder context;
    input.paint(context);

    const auto* text_command = find_command(context, RenderCommandType::DrawTextLayout, "$123");
    ASSERT_NE(text_command, nullptr);
    const auto caret_rect = input.text_input_caret_rect();
    ASSERT_TRUE(caret_rect.has_value());

    const auto text_rect = command_rect(*text_command);
    EXPECT_GE(caret_rect->x + 0.5F, text_rect.x + text_rect.width);
}

TEST(BasicControlsTests, InputFormatterMapsPointerHitTestingBackToSourceOffsets) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(40.0F));
    });
    input.set_formatter([](std::string_view value) { return "$" + std::string(value); })
        .set_parser([](std::string_view value) {
            std::string parsed;
            for (const auto character : value) {
                if (character != '$') {
                    parsed.push_back(character);
                }
            }
            return parsed;
        })
        .set_text("123");
    input.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 40.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    const auto prefix_width =
        TextEngine()
            .measure_single_line("$", TextStyle{.font_size = input.style().font_size,
                                                .color = input.style().text_color,
                                                .alignment = TextAlignment::Start})
            .width;
    ASSERT_GT(prefix_width, 0.0F);

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{
                        .kind = PointerEventKind::Down,
                        .position = Point{input.style().padding.left + prefix_width * 0.9F,
                                          input.frame().height * 0.5F},
                        .button = PointerButton::Primary,
                        .primary_button_down = true})
                    .handled);
    EXPECT_EQ(input.caret_byte_offset(), 0U);
}

TEST(BasicControlsTests, InputCountsGraphemesAndAutosizesTextarea) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) { layout.set_width(Length::points(220.0F)); });
    input.set_type(InputType::Textarea)
        .set_rows(2U)
        .set_autosize(true)
        .set_autosize_limits(2U, 3U)
        .set_max_length(2U)
        .set_show_word_limit(true)
        .set_word_limit_position(InputWordLimitPosition::Outside)
        .set_count_graphemes(true);

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "A"}).handled);
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Enter}).handled);
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "B"}).handled);
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Enter}).handled);
    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "C"}).handled);

    EXPECT_EQ(input.text(), "A\nB");
    EXPECT_EQ(input.text_length(), 2U);
    input.calculate_layout(LayoutConstraints{.width = 220.0F});
    EXPECT_GT(input.frame().height, input.style().min_height);

    RenderCommandRecorder context;
    input.paint(context);
    EXPECT_NE(find_command(context, RenderCommandType::DrawText, "2 / 2"), nullptr);
}

TEST(BasicControlsTests, InputTextareaScrollsCaretIntoView) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) { layout.set_width(Length::points(180.0F)); });
    input.set_type(InputType::Textarea).set_rows(2U).set_autosize(false);
    input.calculate_layout(LayoutConstraints{.width = 180.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));
    for (auto index = 0; index < 6; ++index) {
        EXPECT_TRUE(
            router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "Line"})
                .handled);
        EXPECT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Enter})
                        .handled);
    }

    EXPECT_GT(input.vertical_scroll_offset(), 0.0F);
    const auto caret_rect = input.text_input_caret_rect();
    ASSERT_TRUE(caret_rect.has_value());
    EXPECT_LE(caret_rect->y + caret_rect->height,
              input.frame().height - input.style().padding.bottom + 1.0F);
}

TEST(BasicControlsTests, InputTextareaScrollsWithMouseWheel) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.configure_layout([](LayoutElement& layout) { layout.set_width(Length::points(180.0F)); });
    input.set_type(InputType::Textarea).set_rows(2U).set_autosize(false);
    input.calculate_layout(LayoutConstraints{.width = 180.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));
    for (auto index = 0; index < 6; ++index) {
        EXPECT_TRUE(
            router.route_key_event(KeyEvent{.kind = KeyEventKind::TextInput, .text = "Line"})
                .handled);
        EXPECT_TRUE(router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Enter})
                        .handled);
    }

    const auto bottom_scroll = input.vertical_scroll_offset();
    ASSERT_GT(bottom_scroll, 0.0F);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Wheel,
                                                      .position = Point{10.0F, 10.0F},
                                                      .wheel_delta = Point{0.0F, 1.0F}})
                    .handled);
    EXPECT_LT(input.vertical_scroll_offset(), bottom_scroll);

    const auto upper_scroll = input.vertical_scroll_offset();
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Wheel,
                                                      .position = Point{10.0F, 10.0F},
                                                      .wheel_delta = Point{0.0F, -1.0F}})
                    .handled);
    EXPECT_GT(input.vertical_scroll_offset(), upper_scroll);
}

TEST(BasicControlsTests, InputTextEditCommandsBackContextMenuActions) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("hello").set_selection(0U, 5U);

    auto state = input.text_input_edit_command_state();
    EXPECT_TRUE(state.can_copy);
    EXPECT_TRUE(state.can_cut);
    EXPECT_FALSE(state.can_select_all);

    EXPECT_TRUE(input.invoke_text_input_edit_command(TextInputEditCommand::Copy));
    input.set_caret_byte_offset(input.text().size());
    state = input.text_input_edit_command_state();
    EXPECT_TRUE(state.can_paste);

    EXPECT_TRUE(input.invoke_text_input_edit_command(TextInputEditCommand::Paste));
    EXPECT_EQ(input.text(), "hellohello");

    input.set_selection(0U, 5U);
    EXPECT_TRUE(input.invoke_text_input_edit_command(TextInputEditCommand::Cut));
    EXPECT_EQ(input.text(), "hello");

    state = input.text_input_edit_command_state();
    EXPECT_TRUE(state.can_select_all);
    EXPECT_TRUE(input.invoke_text_input_edit_command(TextInputEditCommand::SelectAll));
    EXPECT_TRUE(input.has_selection());
}

TEST(BasicControlsTests, InputPaintsAndHandlesOwnContextMenu) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("hello").set_selection(0U, 5U);
    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(160.0F));
    });
    input.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 160.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));
    ASSERT_TRUE(input.show_text_input_context_menu(Point{20.0F, 20.0F}));
    EXPECT_TRUE(input.text_input_context_menu_open());
    EXPECT_TRUE(input.text_input_context_menu_hit_test(Point{24.0F, 28.0F}));

    RenderCommandRecorder context;
    input.paint(context);
    const auto* cut_command = find_command(context, RenderCommandType::DrawText, "Cut");
    ASSERT_NE(cut_command, nullptr);
    EXPECT_NE(find_command(context, RenderCommandType::DrawText, "Copy"), nullptr);
    EXPECT_NE(find_command(context, RenderCommandType::DrawText, "Paste"), nullptr);
    EXPECT_NE(find_command(context, RenderCommandType::DrawText, "Select all"), nullptr);
    const auto cut_point = rect_center(command_rect(*cut_command));

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = cut_point,
                                                      .button = PointerButton::Primary,
                                                      .primary_button_down = true})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = cut_point,
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(input.text().empty());
    EXPECT_FALSE(input.text_input_context_menu_open());
}

TEST(BasicControlsTests, InputContextMenuPaintsAboveLaterSiblings) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(140.0F));
    });

    auto input = std::make_unique<Input>();
    input->set_text("hello").set_selection(0U, 5U);
    input->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F))
            .set_width(Length::points(180.0F));
    });
    auto& input_ref = static_cast<Input&>(root.append_child(std::move(input)));

    auto cover = std::make_unique<Button>();
    cover->set_text("Cover");
    cover->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F))
            .set_size(Length::points(220.0F), Length::points(140.0F));
    });
    root.append_child(std::move(cover));
    root.calculate_layout(LayoutConstraints{.width = 220.0F, .height = 140.0F});

    EventRouter router(root);
    ASSERT_TRUE(router.focus_manager().set_focus(&input_ref));
    ASSERT_TRUE(input_ref.show_text_input_context_menu(Point{20.0F, 20.0F}));

    RenderCommandRecorder context;
    root.paint(context);

    const auto cover_index =
        find_command_index(context, RenderCommandType::DrawTextLayout, "Cover");
    const auto cut_index = find_command_index(context, RenderCommandType::DrawText, "Cut");
    ASSERT_NE(cover_index, std::numeric_limits<std::size_t>::max());
    ASSERT_NE(cut_index, std::numeric_limits<std::size_t>::max());
    EXPECT_GT(cut_index, cover_index);
}

TEST(BasicControlsTests, InputContextMenuRoutesAboveLaterSiblings) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(140.0F));
    });

    auto input = std::make_unique<Input>();
    input->set_text("hello").set_selection(0U, 5U);
    input->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F))
            .set_width(Length::points(180.0F));
    });
    auto& input_ref = static_cast<Input&>(root.append_child(std::move(input)));

    auto cover = std::make_unique<Button>();
    cover->set_text("Cover");
    cover->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F))
            .set_size(Length::points(220.0F), Length::points(140.0F));
    });
    auto click_count = 0;
    cover->set_on_click([&click_count]() { ++click_count; });
    root.append_child(std::move(cover));
    root.calculate_layout(LayoutConstraints{.width = 220.0F, .height = 140.0F});

    EventRouter router(root);
    ASSERT_TRUE(router.focus_manager().set_focus(&input_ref));
    ASSERT_TRUE(input_ref.show_text_input_context_menu(Point{20.0F, 20.0F}));

    RenderCommandRecorder context;
    root.paint(context);
    const auto* cut_command = find_command(context, RenderCommandType::DrawText, "Cut");
    ASSERT_NE(cut_command, nullptr);
    const auto cut_point = rect_center(command_rect(*cut_command));

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = cut_point,
                                                      .button = PointerButton::Primary,
                                                      .primary_button_down = true})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = cut_point,
                                                      .button = PointerButton::Primary})
                    .handled);

    EXPECT_TRUE(input_ref.text().empty());
    EXPECT_FALSE(input_ref.text_input_context_menu_open());
    EXPECT_EQ(click_count, 0);
}

TEST(BasicControlsTests, InputContextMenuLightDismissConsumesOutsidePointer) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(140.0F));
    });

    auto input = std::make_unique<Input>();
    input->set_text("hello").set_selection(0U, 5U);
    input->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F))
            .set_width(Length::points(180.0F));
    });
    auto& input_ref = static_cast<Input&>(root.append_child(std::move(input)));

    auto cover = std::make_unique<Button>();
    cover->set_text("Cover");
    cover->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F))
            .set_size(Length::points(220.0F), Length::points(140.0F));
    });
    auto click_count = 0;
    cover->set_on_click([&click_count]() { ++click_count; });
    root.append_child(std::move(cover));
    root.calculate_layout(LayoutConstraints{.width = 220.0F, .height = 140.0F});

    EventRouter router(root);
    ASSERT_TRUE(router.focus_manager().set_focus(&input_ref));
    ASSERT_TRUE(input_ref.show_text_input_context_menu(Point{20.0F, 20.0F}));

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{200.0F, 20.0F},
                                                      .button = PointerButton::Primary,
                                                      .primary_button_down = true})
                    .handled);
    EXPECT_FALSE(input_ref.text_input_context_menu_open());

    static_cast<void>(router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                              .position = Point{200.0F, 20.0F},
                                                              .button = PointerButton::Primary}));
    EXPECT_EQ(input_ref.text(), "hello");
    EXPECT_EQ(click_count, 0);
}

TEST(BasicControlsTests, InputContextMenuDismissesOnEscapeAndKeepsFocus) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(140.0F));
    });

    auto input = std::make_unique<Input>();
    input->set_text("hello").set_selection(0U, 5U);
    input->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F))
            .set_width(Length::points(180.0F));
    });
    auto& input_ref = static_cast<Input&>(root.append_child(std::move(input)));
    root.calculate_layout(LayoutConstraints{.width = 220.0F, .height = 140.0F});

    EventRouter router(root);
    ASSERT_TRUE(router.focus_manager().set_focus(&input_ref));
    ASSERT_TRUE(input_ref.show_text_input_context_menu(Point{20.0F, 20.0F}));
    EXPECT_EQ(root.top_layer_count(), 1U);

    const auto result =
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Escape});

    EXPECT_TRUE(result.handled);
    EXPECT_FALSE(input_ref.text_input_context_menu_open());
    EXPECT_EQ(root.top_layer_count(), 0U);
    EXPECT_EQ(router.focus_manager().focused_element(), &input_ref);
    EXPECT_EQ(input_ref.text(), "hello");
}

TEST(BasicControlsTests, InputContextMenuKeyboardNavigationInvokesCommand) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(140.0F));
    });

    auto input = std::make_unique<Input>();
    input->set_text("hello").set_selection(0U, 5U);
    input->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(0.0F))
            .set_position(Edge::Top, Length::points(0.0F))
            .set_width(Length::points(180.0F));
    });
    auto& input_ref = static_cast<Input&>(root.append_child(std::move(input)));
    root.calculate_layout(LayoutConstraints{.width = 220.0F, .height = 140.0F});

    EventRouter router(root);
    ASSERT_TRUE(router.focus_manager().set_focus(&input_ref));
    ASSERT_TRUE(input_ref.show_text_input_context_menu(Point{20.0F, 20.0F}));

    EXPECT_TRUE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Enter}).handled);
    EXPECT_FALSE(input_ref.text_input_context_menu_open());
    EXPECT_EQ(root.top_layer_count(), 0U);
    EXPECT_EQ(router.focus_manager().focused_element(), &input_ref);
    EXPECT_TRUE(input_ref.text().empty());
}

TEST(BasicControlsTests, InputPasswordMaskUsesCenteredTextAndCaretRect) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_text("pw").set_type(InputType::Password);
    input.calculate_layout(LayoutConstraints{.width = 160.0F});

    EventRouter router(input);
    ASSERT_TRUE(router.focus_manager().set_focus(&input));

    RenderCommandRecorder context;
    input.paint(context);
    const auto* mask_command = find_command(context, RenderCommandType::DrawTextLayout, "**");
    ASSERT_NE(mask_command, nullptr);
    const auto mask_rect = command_rect(*mask_command);
    EXPECT_GT(mask_rect.y + mask_rect.height * 0.5F, input.frame().height * 0.5F);
    EXPECT_LT(mask_rect.y, input.frame().height - input.style().padding.bottom);

    const auto caret_rect = input.text_input_caret_rect();
    ASSERT_TRUE(caret_rect.has_value());
    EXPECT_GE(caret_rect->y, mask_rect.y);
    EXPECT_LE(caret_rect->y + caret_rect->height, mask_rect.y + mask_rect.height);
}

TEST(BasicControlsTests, ElementButtonVariantsDisableAndLoadingState) {
    auto engine = create_unrounded_engine();
    Button button;
    button.bind_layout_tree(engine);
    button.set_type(ButtonType::Primary)
        .set_size(ButtonSize::Large)
        .set_plain(true)
        .set_round(true);

    EXPECT_EQ(button.type(), ButtonType::Primary);
    EXPECT_EQ(button.size(), ButtonSize::Large);
    EXPECT_TRUE(button.plain());
    EXPECT_TRUE(button.round());
    EXPECT_EQ(button.style().text_color, default_primary_button_style().background);
    EXPECT_GT(button.style().min_height, default_button_style().min_height);

    auto click_count = 0;
    button.set_on_click([&click_count]() { ++click_count; });
    EventRouter router(button);
    button.set_loading(true);
    EXPECT_TRUE(button.loading());
    EXPECT_FALSE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Enter}).handled);
    EXPECT_EQ(click_count, 0);

    button.set_loading(false).set_disabled(true);
    EXPECT_TRUE(button.disabled());
    EXPECT_FALSE(
        router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Space}).handled);
    EXPECT_EQ(click_count, 0);
}

TEST(BasicControlsTests, ElementInputMetadataStatusAndSizeAreStateful) {
    auto engine = create_unrounded_engine();
    Input input;
    input.bind_layout_tree(engine);
    input.set_size(InputSize::Small)
        .set_status(InputStatus::Warning)
        .set_validate_event(false)
        .set_autocomplete("username")
        .set_input_mode("email")
        .set_placeholder("Account");

    EXPECT_EQ(input.size(), InputSize::Small);
    EXPECT_EQ(input.status(), InputStatus::Warning);
    EXPECT_FALSE(input.validate_event());
    EXPECT_EQ(input.autocomplete(), "username");
    EXPECT_EQ(input.input_mode(), "email");
    EXPECT_EQ(input.placeholder(), "Account");

    input.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(180.0F), Length::points(32.0F));
    });
    input.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 32.0F});
    RenderCommandRecorder context;
    input.paint(context);
    const auto border_command = std::find_if(
        context.commands().begin(), context.commands().end(), [&](const auto& command) {
            return (command.type() == RenderCommandType::StrokeRoundedRect ||
                    command.type() == RenderCommandType::StrokeRect) &&
                   command_stroke_color(command) == input.style().semantic.warning;
        });
    EXPECT_NE(border_command, context.commands().end());
}

TEST(BasicControlsTests, TextSemanticWrapperDelegatesToBaseUiElementRendering) {
    auto engine = create_unrounded_engine();
    Text text;
    text.bind_layout_tree(engine);
    text.set_text("Semantic text")
        .set_type(TextType::Success)
        .set_size(TextSize::Small)
        .set_truncated(true);

    EXPECT_EQ(text.type(), TextType::Success);
    EXPECT_EQ(text.size(), TextSize::Small);
    EXPECT_TRUE(text.truncated());
    EXPECT_EQ(text.style().text_color, default_text_style().semantic.success);
    EXPECT_EQ(text.text_style().trimming, TextTrimming::CharacterEllipsis);

    text.calculate_layout(LayoutConstraints{.width = 140.0F});
    RenderCommandRecorder context;
    text.paint(context);
    ASSERT_EQ(context.commands().size(), 1U);
    EXPECT_EQ(context.commands()[0].type(), RenderCommandType::DrawTextLayout);
}

TEST(BasicControlsTests, BorderRadioSwitchScrollbarSelectAndItemsControlExposeCoreState) {
    auto engine = create_unrounded_engine();

    Border border_control;

    border_control.bind_layout_tree(engine);
    border_control.set_preset(BorderPreset::Primary).set_shadow_preset(BorderShadow::Light);
    EXPECT_EQ(border_control.preset(), BorderPreset::Primary);
    EXPECT_EQ(border_control.shadow_preset(), BorderShadow::Light);
    EXPECT_EQ(border_control.style().border_color, default_button_style().focus_border_color);
    EXPECT_TRUE(border_control.style().shadow_visible);

    Radio radio;

    radio.bind_layout_tree(engine);
    auto radio_changes = 0;
    auto radio_checked_value = false;
    radio.set_text("Choice").set_on_change([&radio_changes, &radio_checked_value](bool checked) {
        ++radio_changes;
        radio_checked_value = checked;
    });
    radio.calculate_layout(LayoutConstraints{.width = 120.0F, .height = 32.0F});
    EventRouter radio_router(radio);
    EXPECT_FALSE(radio.checked());
    EXPECT_TRUE(radio_router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{4.0F, 4.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(radio_router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{4.0F, 4.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(radio.checked());
    EXPECT_EQ(radio_changes, 1);
    EXPECT_TRUE(radio_checked_value);
    EXPECT_TRUE(radio_router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{4.0F, 4.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(radio_router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{4.0F, 4.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_FALSE(radio.checked());
    EXPECT_EQ(radio_changes, 2);
    EXPECT_FALSE(radio_checked_value);

    auto radio_group = std::make_shared<RadioGroupContext>();
    Radio first_radio;
    Radio skipped_radio;
    Radio second_radio;
    first_radio.set_value("first").set_group(radio_group);
    skipped_radio.set_value("skip").set_group(radio_group).set_disabled(true);
    second_radio.set_value("second").set_group(radio_group);
    radio_group->set_value("second");
    EXPECT_FALSE(first_radio.checked());
    EXPECT_TRUE(second_radio.checked());
    first_radio.set_checked(true);
    EXPECT_EQ(radio_group->value(), "first");
    EXPECT_TRUE(first_radio.checked());
    EXPECT_FALSE(second_radio.checked());
    EXPECT_TRUE(radio_group->move_selection(first_radio, 1));
    EXPECT_EQ(radio_group->value(), "second");
    EXPECT_FALSE(skipped_radio.checked());

    Switch switch_control;

    switch_control.bind_layout_tree(engine);
    auto switch_value = false;
    switch_control.set_size(SwitchSize::Large)
        .set_active_text("ON")
        .set_inactive_text("OFF")
        .set_on_change([&switch_value](bool checked) { switch_value = checked; });
    EXPECT_EQ(switch_control.size(), SwitchSize::Large);
    EventRouter switch_router(switch_control);
    EXPECT_TRUE(switch_router.focus_manager().set_focus(&switch_control));
    EXPECT_TRUE(
        switch_router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Space})
            .handled);
    EXPECT_TRUE(switch_control.checked());
    EXPECT_TRUE(switch_value);

    Scrollbar scrollbar;

    scrollbar.bind_layout_tree(engine);
    auto scroll_value = 0.0F;
    scrollbar.set_orientation(ScrollbarOrientation::Horizontal)
        .set_range(10.0F, 30.0F, 5.0F)
        .set_on_scroll([&scroll_value](float value) { scroll_value = value; })
        .set_value(100.0F);
    EXPECT_EQ(scrollbar.orientation(), ScrollbarOrientation::Horizontal);
    EXPECT_FLOAT_EQ(scrollbar.minimum(), 10.0F);
    EXPECT_FLOAT_EQ(scrollbar.maximum(), 30.0F);
    EXPECT_FLOAT_EQ(scrollbar.value(), 30.0F);
    EXPECT_FLOAT_EQ(scroll_value, 30.0F);
    EventRouter scrollbar_router(scrollbar);
    ASSERT_TRUE(scrollbar_router.focus_manager().set_focus(&scrollbar));
    scrollbar.set_value(10.0F);
    EXPECT_TRUE(
        scrollbar_router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Right})
            .handled);
    EXPECT_FLOAT_EQ(scrollbar.value(), 11.0F);
    EXPECT_TRUE(
        scrollbar_router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::PageDown})
            .handled);
    EXPECT_FLOAT_EQ(scrollbar.value(), 16.0F);
    EXPECT_TRUE(
        scrollbar_router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::End})
            .handled);
    EXPECT_FLOAT_EQ(scrollbar.value(), 30.0F);
    EXPECT_TRUE(
        scrollbar_router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Home})
            .handled);
    EXPECT_FLOAT_EQ(scrollbar.value(), 10.0F);
    EXPECT_FALSE(
        scrollbar_router.route_key_event(KeyEvent{.kind = KeyEventKind::Down, .key = Key::Up})
            .handled);
    EXPECT_FLOAT_EQ(scrollbar.value(), 10.0F);

    ScrollbarScrollData scroll_data;
    std::vector<ScrollbarEndDirection> reached;
    scrollbar.set_always_visible(true)
        .set_min_size(20.0F)
        .set_distance(5.0F)
        .set_on_scroll_data([&scroll_data](ScrollbarScrollData data) { scroll_data = data; })
        .set_on_end_reached(
            [&reached](ScrollbarEndDirection direction) { reached.push_back(direction); })
        .scroll_to(24.0F, 0.0F);
    EXPECT_EQ(scrollbar.visibility_mode(), ScrollbarVisibility::Always);
    EXPECT_FLOAT_EQ(scrollbar.min_size(), 20.0F);
    EXPECT_FLOAT_EQ(scrollbar.distance(), 5.0F);
    EXPECT_FLOAT_EQ(scrollbar.value(), 24.0F);
    EXPECT_FLOAT_EQ(scroll_data.scroll_left, 24.0F);
    scrollbar.set_value(30.0F);
    ASSERT_EQ(reached.size(), 1U);
    EXPECT_EQ(reached.back(), ScrollbarEndDirection::Right);

    scrollbar.set_orientation(ScrollbarOrientation::Vertical)
        .set_range(0.0F, 100.0F, 25.0F)
        .set_scroll_top(40.0F)
        .set_scroll_left(80.0F);
    EXPECT_FLOAT_EQ(scrollbar.value(), 40.0F);
    scrollbar.scroll_to(12.0F, 60.0F);
    EXPECT_FLOAT_EQ(scrollbar.value(), 60.0F);
    scrollbar.calculate_layout(LayoutConstraints{.width = 6.0F, .height = 120.0F});
    RenderCommandRecorder scrollbar_context;
    scrollbar.paint(scrollbar_context);
    const auto thumb_command = std::find_if(
        scrollbar_context.commands().begin(), scrollbar_context.commands().end(),
        [](const auto& command) { return command.type() == RenderCommandType::FillRoundedRect; });
    ASSERT_NE(thumb_command, scrollbar_context.commands().end());
    const auto thumb = command_rect(*thumb_command);
    EXPECT_FLOAT_EQ(thumb.width, 6.0F);
    EXPECT_GE(thumb.y, 2.0F);
    EXPECT_LE(thumb.y + thumb.height, 118.0F);

    scrollbar.set_value(0.0F);
    EXPECT_TRUE(scrollbar_router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{3.0F, 10.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(scrollbar_router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                                      .position = Point{3.0F, 90.0F}})
                    .handled);
    EXPECT_TRUE(scrollbar_router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{3.0F, 90.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_GT(scrollbar.value(), 70.0F);

    Select select;

    select.bind_layout_tree(engine);
    std::optional<std::size_t> selected;
    select
        .set_options({SelectOption{.label = "Alpha", .value = "a"},
                      SelectOption{.label = "Beta", .value = "b", .disabled = true}})
        .set_clearable(true)
        .set_filterable(true)
        .set_size(SelectSize::Large)
        .set_on_change([&selected](std::optional<std::size_t> index) { selected = index; })
        .set_selected_index(0U);
    EXPECT_EQ(select.selected_index(), 0U);
    EXPECT_EQ(select.selected_label(), "Alpha");
    EXPECT_TRUE(selected.has_value());
    EXPECT_TRUE(select.clearable());
    EXPECT_TRUE(select.filterable());
    EXPECT_EQ(select.size(), SelectSize::Large);
    select.set_filter_text("alp");
    EXPECT_EQ(select.filter_text(), "alp");
    EXPECT_EQ(select.filtered_option_count(), 1U);
    select.set_filter_text("ALP");
    EXPECT_EQ(select.filtered_option_count(), 1U);
    select
        .set_options({SelectOption{.label = "Alpha", .value = "a", .group = "latin"},
                      SelectOption{.label = "\xC3\x89"
                                            "clair",
                                   .value = "e",
                                   .group = "latin"},
                      SelectOption{.label = "Gamma", .value = "g", .group = "greek"}})
        .set_option_groups({SelectOptionGroup{.label = "Latin", .start_index = 0U, .count = 2U},
                            SelectOptionGroup{.label = "Greek", .start_index = 2U, .count = 1U}})
        .set_multiple(true)
        .set_tags_visible(true)
        .set_selected_indices({0U, 2U})
        .set_label_formatter([](const SelectOption& option, std::size_t) {
            return option.group + ":" + option.label;
        });
    EXPECT_TRUE(select.multiple());
    EXPECT_TRUE(select.tags_visible());
    EXPECT_EQ(select.option_groups().size(), 2U);
    EXPECT_EQ(select.selected_indices(), (std::vector<std::size_t>{0U, 2U}));
    EXPECT_EQ(select.selected_label(), "latin:Alpha, greek:Gamma");
    EXPECT_EQ(select.selected_tags().size(), 2U);
    select.set_filter_text("\xC3\xA9");
    EXPECT_EQ(select.filtered_option_count(), 1U);
    auto remote_query = std::string{};
    select.set_remote_search(true).set_remote_search_handler(
        [&remote_query](std::string_view query) { remote_query = std::string(query); });
    select.set_filter_text("server");
    EXPECT_EQ(remote_query, "server");

    ItemsControl items;

    items.bind_layout_tree(engine);
    items.set_items({"One", "Two"});
    EXPECT_EQ(items.items().size(), 2U);
    EXPECT_EQ(items.child_count(), 2U);
    items.set_selection_mode(ItemsControl::SelectionMode::Single)
        .set_item_factory([&engine](ItemsControl::ItemContext context) {
            auto element = std::make_unique<Text>();
            element->set_text(std::to_string(context.index) + ":" + std::string(context.item));
            return element;
        });
    EXPECT_EQ(items.child_count(), 2U);
    ASSERT_EQ(items.child_at(1U).child_count(), 1U);
    EXPECT_EQ(items.child_at(1U).child_at(0U).text(), "1:Two");
    items.set_selected_index(1U);
    EXPECT_EQ(items.selected_index(), 1U);
    items.set_selection_mode(ItemsControl::SelectionMode::Multiple).set_selected_indices({0U, 1U});
    EXPECT_EQ(items.selected_indices(), (std::vector<std::size_t>{0U, 1U}));
    items.set_virtualized(true).set_realized_range(1U, 1U);
    EXPECT_EQ(items.child_count(), 1U);
    ASSERT_EQ(items.child_at(0U).child_count(), 1U);
    EXPECT_EQ(items.child_at(0U).child_at(0U).text(), "1:Two");
    items.set_items({"Short", "Medium", "Tall"})
        .set_virtualization_window(0.0F, 25.0F, std::vector<float>{10.0F, 20.0F, 30.0F}, 0U);
    EXPECT_TRUE(items.virtualized());
    EXPECT_EQ(items.realized_start_index(), 0U);
    EXPECT_EQ(items.realized_count(), 3U);

    Path path;

    path.bind_layout_tree(engine);
    path.set_data("F0 M 0 0 L 10 0 H 20 V 10 Q 20 20 10 20 C 0 20 0 10 0 0 "
                  "A 4 4 0 0 1 8 8 Z")
        .set_fill(Color::rgba(236, 245, 255))
        .set_stroke(Color::rgba(64, 158, 255))
        .set_stroke_width(2.0F)
        .set_stretch(PathStretch::Fill);
    EXPECT_EQ(path.geometry().fill_rule, GeometryFillRule::EvenOdd);
    ASSERT_EQ(path.geometry().figures.size(), 1U);
    EXPECT_EQ(path.geometry().figures.front().end, GeometryFigureEnd::Closed);
    EXPECT_EQ(path.geometry().figures.front().segments.size(), 6U);
    EXPECT_TRUE(path.fill().has_value());
    EXPECT_TRUE(path.stroke().has_value());
    EXPECT_FLOAT_EQ(path.stroke_style().width, 2.0F);
    EXPECT_EQ(path.stretch(), PathStretch::Fill);
    EXPECT_THROW(static_cast<void>(Path::parse_path_data("M 0")), std::invalid_argument);
}

TEST(BasicControlsTests, VerticalScrollbarDragSynchronizesViewportScrollOffset) {
    auto engine = create_unrounded_engine();
    StackPanel root;
    root.bind_layout_tree(engine);
    root.set_orientation(Orientation::Horizontal).set_gap(12.0F).set_align_items(Align::Center);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(120.0F));
    });

    auto& viewport = root.append_new_child<Panel>();
    viewport.set_viewport(Rect{0.0F, 0.0F, 184.0F, 120.0F}).set_overflow(Overflow::Hidden);
    viewport.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(184.0F), Length::points(120.0F)).set_flex_shrink(0.0F);
    });

    auto& content = viewport.append_new_child<StackPanel>();
    content.configure_layout([](LayoutElement& layout) {
        layout.set_width(Length::percent(100.0F))
            .set_height(Length::points(320.0F))
            .set_gap(Gutter::Row, Length::points(8.0F))
            .set_padding(Edge::All, Length::points(10.0F))
            .set_flex_shrink(0.0F);
    });
    for (auto index = 0; index < 7; ++index) {
        auto& item = content.append_new_child<Border>();
        item.configure_layout([](LayoutElement& layout) {
            layout.set_size(Length::points(164.0F), Length::points(36.0F)).set_flex_shrink(0.0F);
        });
    }

    auto& scrollbar = root.append_new_child<Scrollbar>();
    scrollbar.set_orientation(ScrollbarOrientation::Vertical)
        .set_range(0.0F, 200.0F, 120.0F)
        .set_always_visible(true)
        .set_on_scroll_data([&viewport](ScrollbarScrollData data) {
            viewport.set_scroll_offset(Point{0.0F, data.scroll_top});
        });
    scrollbar.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(12.0F), Length::points(120.0F)).set_flex_shrink(0.0F);
    });

    root.calculate_layout(LayoutConstraints{.width = 220.0F, .height = 120.0F});
    const auto initial_content_y = content.absolute_frame().y;

    EventRouter router(root);
    const auto bar = scrollbar.absolute_frame();
    const auto x = bar.x + bar.width * 0.5F;
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{x, bar.y + 6.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                                      .position = Point{x, bar.y + 96.0F}})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{x, bar.y + 96.0F},
                                                      .button = PointerButton::Primary})
                    .handled);

    EXPECT_GT(scrollbar.value(), 100.0F);
    EXPECT_FLOAT_EQ(viewport.scroll_offset().y, scrollbar.value());
    EXPECT_LT(content.absolute_frame().y, initial_content_y - 100.0F);
}

TEST(BasicControlsTests, HorizontalScrollbarDragSynchronizesViewportScrollOffset) {
    auto engine = create_unrounded_engine();
    StackPanel root;
    root.bind_layout_tree(engine);
    root.set_orientation(Orientation::Vertical).set_gap(8.0F);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(240.0F), Length::points(120.0F));
    });

    auto& viewport = root.append_new_child<Panel>();
    viewport.set_viewport(Rect{0.0F, 0.0F, 220.0F, 84.0F}).set_overflow(Overflow::Hidden);
    viewport.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(84.0F)).set_flex_shrink(0.0F);
    });

    auto& content = viewport.append_new_child<StackPanel>();
    content.set_orientation(Orientation::Horizontal).set_gap(8.0F).set_wrap(Wrap::NoWrap);
    content.configure_layout([](LayoutElement& layout) {
        layout.set_width(Length::points(520.0F))
            .set_height(Length::points(84.0F))
            .set_flex_shrink(0.0F);
    });
    for (auto index = 0; index < 5; ++index) {
        auto& item = content.append_new_child<Border>();
        item.configure_layout([](LayoutElement& layout) {
            layout.set_size(Length::points(96.0F), Length::points(64.0F)).set_flex_shrink(0.0F);
        });
    }

    auto& scrollbar = root.append_new_child<Scrollbar>();
    scrollbar.set_orientation(ScrollbarOrientation::Horizontal)
        .set_range(0.0F, 300.0F, 220.0F)
        .set_always_visible(true)
        .set_on_scroll_data([&viewport](ScrollbarScrollData data) {
            viewport.set_scroll_offset(Point{data.scroll_left, 0.0F});
        });
    scrollbar.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(12.0F)).set_flex_shrink(0.0F);
    });

    root.calculate_layout(LayoutConstraints{.width = 240.0F, .height = 120.0F});
    const auto initial_content_x = content.absolute_frame().x;

    EventRouter router(root);
    const auto bar = scrollbar.absolute_frame();
    const auto y = bar.y + bar.height * 0.5F;
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{bar.x + 8.0F, y},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Move,
                                                      .position = Point{bar.x + 170.0F, y}})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{bar.x + 170.0F, y},
                                                      .button = PointerButton::Primary})
                    .handled);

    EXPECT_GT(scrollbar.value(), 180.0F);
    EXPECT_FLOAT_EQ(viewport.scroll_offset().x, scrollbar.value());
    EXPECT_LT(content.absolute_frame().x, initial_content_x - 180.0F);
}

TEST(BasicControlsTests, SelectUsesSvgIconArrowAndAnimatedDropdownLayer) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(160.0F));
    });

    auto select = std::make_unique<Select>();
    select
        ->set_options({SelectOption{.label = "Alpha", .value = "a"},
                       SelectOption{.label = "Beta", .value = "b"}})
        .set_selected_index(0U);
    select->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(8.0F))
            .set_position(Edge::Top, Length::points(8.0F))
            .set_width(Length::points(180.0F));
    });
    auto& select_ref = static_cast<Select&>(root.append_child(std::move(select)));
    root.calculate_layout(LayoutConstraints{.width = 220.0F, .height = 160.0F});

    RenderCommandRecorder closed_context;
    root.paint(closed_context);
    EXPECT_EQ(find_command(closed_context, RenderCommandType::DrawText, "v"), nullptr);
    EXPECT_EQ(find_command(closed_context, RenderCommandType::DrawText, "^"), nullptr);
    EXPECT_GE(command_count(closed_context, RenderCommandType::FillGeometry), 1U);

    EventRouter router(root);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{16.0F, 16.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{16.0F, 16.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(select_ref.popup_open());
    ASSERT_EQ(root.top_layer_count(), 1U);

    RenderCommandRecorder open_context;
    root.paint(open_context);
    EXPECT_EQ(find_command(open_context, RenderCommandType::DrawText, "v"), nullptr);
    EXPECT_EQ(find_command(open_context, RenderCommandType::DrawText, "^"), nullptr);
    EXPECT_GE(command_count(open_context, RenderCommandType::FillGeometry), 1U);
    const auto animated_layer = std::find_if(
        open_context.commands().begin(), open_context.commands().end(), [](const auto& command) {
            return command.type() == RenderCommandType::PushLayer &&
                   command.payload<PushLayerCommand>().options.opacity < 1.0F &&
                   command.payload<PushLayerCommand>().options.transform.dy < 0.0F;
        });
    EXPECT_NE(animated_layer, open_context.commands().end());
}

TEST(BasicControlsTests, ButtonMenuIndicatorPaintsChevron) {
    auto engine = create_unrounded_engine();
    Button button;
    button.bind_layout_tree(engine);
    button.set_text("More").set_role(ButtonRole::Menu);
    button.calculate_layout();

    RenderCommandRecorder context;
    button.paint(context);

    EXPECT_GE(command_count(context, RenderCommandType::DrawLine), 2U);
}

TEST(BasicControlsTests, SplitButtonDividerDoesNotOverlapText) {
    auto engine = create_unrounded_engine();
    Button button;
    button.bind_layout_tree(engine);
    button.set_text("Split").set_split(true).set_role(ButtonRole::SplitPrimary);
    button.calculate_layout();

    RenderCommandRecorder context;
    button.paint(context);

    const auto* text_command = find_command(context, RenderCommandType::DrawTextLayout, "Split");
    ASSERT_NE(text_command, nullptr);
    const auto text_rect = command_rect(*text_command);

    const auto divider =
        std::find_if(context.commands().begin(), context.commands().end(), [](const auto& command) {
            if (command.type() != RenderCommandType::DrawLine) {
                return false;
            }
            const auto& line = command.payload<DrawLineCommand>();
            return line.start.x == line.end.x;
        });
    ASSERT_NE(divider, context.commands().end());

    const auto& line = divider->payload<DrawLineCommand>();
    EXPECT_GT(line.start.x, text_rect.x + text_rect.width);
}

TEST(BasicControlsTests, SelectPaintsTagsAndDropdownGroupHeaders) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(240.0F), Length::points(180.0F));
    });

    auto select = std::make_unique<Select>();
    select
        ->set_options({SelectOption{.label = "Alpha", .value = "a", .group = "latin"},
                       SelectOption{.label = "Beta", .value = "b", .group = "latin"},
                       SelectOption{.label = "Gamma", .value = "g", .group = "greek"}})
        .set_option_groups({SelectOptionGroup{.label = "Latin", .start_index = 0U, .count = 2U},
                            SelectOptionGroup{.label = "Greek", .start_index = 2U, .count = 1U}})
        .set_multiple(true)
        .set_tags_visible(true)
        .set_selected_indices({0U, 2U});
    select->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(8.0F))
            .set_position(Edge::Top, Length::points(8.0F))
            .set_width(Length::points(200.0F));
    });
    auto& select_ref = static_cast<Select&>(root.append_child(std::move(select)));
    root.calculate_layout(LayoutConstraints{.width = 240.0F, .height = 180.0F});

    RenderCommandRecorder closed_context;
    root.paint(closed_context);
    EXPECT_NE(find_command(closed_context, RenderCommandType::DrawText, "Alpha"), nullptr);
    EXPECT_NE(find_command(closed_context, RenderCommandType::DrawText, "Gamma"), nullptr);

    EventRouter router(root);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{16.0F, 16.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{16.0F, 16.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    ASSERT_TRUE(select_ref.popup_open());

    RenderCommandRecorder open_context;
    root.paint(open_context);
    EXPECT_NE(find_command(open_context, RenderCommandType::DrawText, "Latin"), nullptr);
    EXPECT_NE(find_command(open_context, RenderCommandType::DrawText, "Greek"), nullptr);
}

TEST(BasicControlsTests, ItemsControlAltArrowInvokesReorder) {
    auto engine = create_unrounded_engine();
    ItemsControl items;
    items.bind_layout_tree(engine);
    items.set_items({"One", "Two", "Three"});

    auto from_index = std::numeric_limits<std::size_t>::max();
    auto to_index = std::numeric_limits<std::size_t>::max();
    items.set_on_reorder([&from_index, &to_index](std::size_t from, std::size_t to) {
        from_index = from;
        to_index = to;
    });
    items.calculate_layout(LayoutConstraints{.width = 180.0F, .height = 120.0F});

    EventRouter router(items);
    ASSERT_TRUE(router.focus_manager().set_focus(&items.child_at(0U)));
    EXPECT_TRUE(router
                    .route_key_event(KeyEvent{.kind = KeyEventKind::Down,
                                              .key = Key::Down,
                                              .modifiers = KeyModifiers{.alt = true}})
                    .handled);
    EXPECT_EQ(from_index, 0U);
    EXPECT_EQ(to_index, 1U);
}

TEST(BasicControlsTests, SelectToggleClosesPopupAndFiltersUseInsetText) {
    auto engine = create_unrounded_engine();
    Panel root;
    root.bind_layout_tree(engine);
    root.configure_layout([](LayoutElement& layout) {
        layout.set_size(Length::points(220.0F), Length::points(180.0F));
    });

    auto select = std::make_unique<Select>();
    select
        ->set_options({SelectOption{.label = "Alpha", .value = "a"},
                       SelectOption{.label = "Beta", .value = "b"}})
        .set_filterable(true);
    select->configure_layout([](LayoutElement& layout) {
        layout.set_position_type(PositionType::Absolute)
            .set_position(Edge::Left, Length::points(8.0F))
            .set_position(Edge::Top, Length::points(8.0F))
            .set_width(Length::points(180.0F));
    });
    auto& select_ref = static_cast<Select&>(root.append_child(std::move(select)));
    root.calculate_layout(LayoutConstraints{.width = 220.0F, .height = 180.0F});

    EventRouter router(root);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{16.0F, 16.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                      .position = Point{16.0F, 16.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    ASSERT_TRUE(select_ref.popup_open());
    ASSERT_EQ(root.top_layer_count(), 1U);

    RenderCommandRecorder open_context;
    root.paint(open_context);
    const auto* filter_text =
        find_command(open_context, RenderCommandType::DrawText, "Filter options");
    ASSERT_NE(filter_text, nullptr);
    EXPECT_GE(command_rect(*filter_text).x, 24.0F);

    EXPECT_TRUE(router
                    .route_pointer_event(PointerEvent{.kind = PointerEventKind::Down,
                                                      .position = Point{16.0F, 16.0F},
                                                      .button = PointerButton::Primary})
                    .handled);
    EXPECT_FALSE(select_ref.popup_open());
    EXPECT_EQ(root.top_layer_count(), 0U);

    static_cast<void>(router.route_pointer_event(PointerEvent{.kind = PointerEventKind::Up,
                                                              .position = Point{16.0F, 16.0F},
                                                              .button = PointerButton::Primary}));
    EXPECT_FALSE(select_ref.popup_open());
    EXPECT_EQ(root.top_layer_count(), 0U);
}

TEST(BasicControlsTests, SelectDisabledClearsFilterText) {
    Select select;
    select
        .set_options({SelectOption{.label = "Alpha", .value = "a"},
                      SelectOption{.label = "Beta", .value = "b"}})
        .set_filterable(true)
        .set_filter_text("alp");
    ASSERT_EQ(select.filter_text(), "alp");

    select.set_disabled(true);
    EXPECT_TRUE(select.filter_text().empty());
    EXPECT_TRUE(select.disabled());
}

} // namespace
