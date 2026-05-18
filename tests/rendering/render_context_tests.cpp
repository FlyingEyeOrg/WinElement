#include <winelement/rendering.hpp>
#include <winelement/rendering/svg_path_parser.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace {

using namespace winelement::layout;
using namespace winelement::rendering;

template <typename CommandPayload>
[[nodiscard]] const CommandPayload& command_as(const RenderOpcodeRecord& command) {
    return command.payload<CommandPayload>();
}

[[nodiscard]] Geometry make_triangle_geometry() {
    Geometry geometry;
    geometry.figures.push_back(GeometryFigure{
        .start = Point{10.0F, 10.0F},
        .end = GeometryFigureEnd::Closed,
        .segments = {
            GeometrySegment{.type = GeometrySegmentType::Line, .point = Point{40.0F, 10.0F}},
            GeometrySegment{.type = GeometrySegmentType::Line, .point = Point{25.0F, 40.0F}}}});
    return geometry;
}

[[nodiscard]] Geometry make_quadratic_geometry() {
    Geometry geometry;
    geometry.figures.push_back(
        GeometryFigure{.start = Point{0.0F, 0.0F},
                       .end = GeometryFigureEnd::Open,
                       .segments = {GeometrySegment{.type = GeometrySegmentType::QuadraticBezier,
                                                    .point = Point{100.0F, 0.0F},
                                                    .control_point1 = Point{50.0F, 100.0F}}}});
    return geometry;
}

[[nodiscard]] Geometry make_cubic_geometry() {
    Geometry geometry;
    geometry.figures.push_back(
        GeometryFigure{.start = Point{0.0F, 0.0F},
                       .end = GeometryFigureEnd::Open,
                       .segments = {GeometrySegment{.type = GeometrySegmentType::CubicBezier,
                                                    .point = Point{100.0F, 0.0F},
                                                    .control_point1 = Point{0.0F, 100.0F},
                                                    .control_point2 = Point{100.0F, 100.0F}}}});
    return geometry;
}

[[nodiscard]] Geometry make_quarter_arc_geometry() {
    Geometry geometry;
    geometry.figures.push_back(GeometryFigure{
        .start = Point{100.0F, 50.0F},
        .end = GeometryFigureEnd::Open,
        .segments = {GeometrySegment{.type = GeometrySegmentType::Arc,
                                     .point = Point{50.0F, 100.0F},
                                     .radius = Size{50.0F, 50.0F},
                                     .arc_size = GeometryArcSize::Small,
                                     .sweep_direction = GeometryArcSweepDirection::Clockwise}}});
    return geometry;
}

TEST(RenderContextTests, RenderCommandRecorderCapturesBoxShadowCommand) {
    RenderCommandRecorder recorder;
    const auto rect = Rect{10.0F, 12.0F, 80.0F, 24.0F};
    const auto style = ShadowStyle{.color = Color::rgba(0, 0, 0, 96),
                                   .offset = Point{2.0F, 4.0F},
                                   .blur_radius = 12.0F,
                                   .spread = 3.0F};

    recorder.draw_box_shadow(rect, style);

    ASSERT_EQ(recorder.commands().size(), 1U);
    const auto& command = recorder.commands()[0];
    ASSERT_EQ(command.type(), RenderCommandType::DrawBoxShadow);
    const auto& payload = command_as<DrawBoxShadowCommand>(command);
    EXPECT_EQ(payload.style.color, style.color);
    EXPECT_FLOAT_EQ(payload.style.blur_radius, style.blur_radius);
    EXPECT_FLOAT_EQ(payload.style.spread, style.spread);
}

TEST(RenderContextTests, RecorderAppendsCachedCommandListsAndKeepsBounds) {
    RenderCommandRecorder cached_recorder;
    cached_recorder.fill_rect(Rect{80.0F, 0.0F, 20.0F, 20.0F}, Color::rgba(0, 0, 255));

    RenderCommandRecorder recorder;
    recorder.fill_rect(Rect{0.0F, 0.0F, 10.0F, 10.0F}, Color::rgba(255, 0, 0));
    recorder.append(cached_recorder.take_command_list());
    const auto command_list = recorder.take_command_list();

    ASSERT_EQ(command_list.commands().size(), 2U);
    EXPECT_FLOAT_EQ(command_list.bounds().x, 0.0F);
    EXPECT_FLOAT_EQ(command_list.bounds().width, 100.0F);
}

TEST(RenderContextTests, CommandListExposesFlatOpcodesAndBatchSummary) {
    RenderCommandRecorder recorder;
    recorder.fill_rect(Rect{0.0F, 0.0F, 10.0F, 10.0F}, Color::rgba(64, 158, 255));
    recorder.draw_text("Element", Rect{0.0F, 10.0F, 80.0F, 20.0F},
                       TextStyle{.color = Color::rgba(48, 49, 51)});
    const auto command_list = recorder.take_command_list();

    ASSERT_EQ(command_list.opcodes().size(), 2U);
    EXPECT_EQ(command_list.opcodes()[0].opcode, RenderCommandType::FillRect);
    EXPECT_FALSE(command_list.serialized_opcodes().empty());

    const auto batches = command_list.draw_batches();
    EXPECT_TRUE(std::any_of(batches.begin(), batches.end(), [](const auto& batch) {
        return batch.kind == RenderBatchKind::Geometry && batch.command_count == 1U;
    }));
    EXPECT_TRUE(std::any_of(batches.begin(), batches.end(), [](const auto& batch) {
        return batch.kind == RenderBatchKind::Text && batch.command_count == 1U;
    }));
}

TEST(RenderContextTests, CommandListMoveKeepsOpcodePayloadOwnersAndSerializedCache) {
    RenderCommandRecorder recorder;
    recorder.fill_rect(Rect{1.0F, 2.0F, 3.0F, 4.0F}, Color::rgba(10, 20, 30));
    recorder.draw_text("shared text", Rect{0.0F, 10.0F, 80.0F, 20.0F},
                       TextStyle{.color = Color::rgba(48, 49, 51)});

    auto command_list = recorder.take_command_list();
    const auto first_serialized = command_list.serialized_opcodes();
    const auto second_serialized = command_list.serialized_opcodes();
    EXPECT_EQ(first_serialized, second_serialized);

    auto moved = std::move(command_list);
    ASSERT_EQ(moved.commands().size(), 2U);
    const auto& rect = moved.commands()[0].payload<FillRectCommand>();
    EXPECT_FLOAT_EQ(rect.rect.x, 1.0F);
    EXPECT_EQ(moved.commands()[1].payload<DrawTextCommand>().text_view(), "shared text");
}

TEST(RenderContextTests, DrawBatchesSplitByStateAndResourceKeys) {
    RenderCommandRecorder recorder;
    recorder.draw_image(RenderResourceId{1U},
                        RenderImageOptions{.destination = Rect{0.0F, 0.0F, 8.0F, 8.0F}});
    recorder.draw_image(RenderResourceId{2U},
                        RenderImageOptions{.destination = Rect{10.0F, 0.0F, 8.0F, 8.0F}});
    recorder.fill_rect(Rect{0.0F, 12.0F, 8.0F, 8.0F}, Color::rgba(1, 2, 3));
    recorder.fill_rect(Rect{10.0F, 12.0F, 8.0F, 8.0F}, Color::rgba(1, 2, 3));

    const auto batches = recorder.take_command_list().draw_batches();
    const auto image_batches =
        std::count_if(batches.begin(), batches.end(),
                      [](const auto& batch) { return batch.kind == RenderBatchKind::Image; });
    EXPECT_EQ(image_batches, 2);
    EXPECT_TRUE(std::any_of(batches.begin(), batches.end(), [](const auto& batch) {
        return batch.kind == RenderBatchKind::Geometry && batch.command_count == 2U;
    }));
}

TEST(RenderContextTests, RecorderBatchApisAppendCommandsWithSingleVisibleResult) {
    const auto rects = std::vector<FillRectCommand>{
        FillRectCommand{.rect = Rect{0.0F, 0.0F, 4.0F, 4.0F}, .color = Color::rgba(1, 2, 3)},
        FillRectCommand{.rect = Rect{5.0F, 0.0F, 4.0F, 4.0F}, .color = Color::rgba(1, 2, 3)}};
    const auto images = std::vector<DrawImageCommand>{
        DrawImageCommand{.resource_id = RenderResourceId{1U},
                         .options = RenderImageOptions{
                             .destination = Rect{0.0F, 10.0F, 4.0F, 4.0F}}},
        DrawImageCommand{.resource_id = RenderResourceId{2U},
                         .options = RenderImageOptions{
                             .destination = Rect{5.0F, 10.0F, 4.0F, 4.0F}}}};

    RenderCommandRecorder recorder;
    recorder.fill_rects(rects);
    recorder.draw_images(images);
    const auto command_list = recorder.take_command_list();

    ASSERT_EQ(command_list.command_count(), 4U);
    EXPECT_EQ(command_list.fill_rect_payloads().size(), 2U);
    EXPECT_EQ(command_list.draw_image_payloads().size(), 2U);
    EXPECT_FLOAT_EQ(command_list.bounds().height, 14.0F);
}

TEST(RenderContextTests, RecorderCompressesEmptySaveRestorePairs) {
    RenderCommandRecorder recorder;
    recorder.save();
    recorder.restore();
    recorder.fill_rect(Rect{0.0F, 0.0F, 10.0F, 10.0F}, Color::rgba(1, 2, 3));

    const auto command_list = recorder.take_command_list();

    ASSERT_EQ(command_list.command_count(), 1U);
    EXPECT_EQ(command_list.commands().front().opcode, RenderCommandType::FillRect);
}

TEST(RenderContextTests, RecorderAppendsMovedCommandListWithoutCopyingTextPayload) {
    RenderCommandRecorder source;
    source.draw_text("interned", Rect{0.0F, 0.0F, 80.0F, 20.0F},
                     TextStyle{.color = Color::rgba(48, 49, 51)});
    auto cached = source.take_command_list();
    const auto text_handle = cached.draw_text_payloads().front().text_handle;
    const auto text_storage = cached.text_parameters().at(text_handle.value - 1U);

    RenderCommandRecorder target;
    target.append(std::move(cached));
    const auto appended = target.take_command_list();

    ASSERT_EQ(appended.draw_text_payloads().size(), 1U);
    const auto appended_handle = appended.draw_text_payloads().front().text_handle;
    EXPECT_EQ(appended.text_parameters().at(appended_handle.value - 1U), text_storage);
    EXPECT_EQ(appended.draw_text_payloads().front().text_view(), "interned");
    EXPECT_EQ(appended.text_parameter(appended_handle), "interned");
}

TEST(RenderContextTests, RenderFrameGraphKeepsOrderedPassesAndEstimatesDraws) {
    RenderCommandRecorder recorder;
    recorder.push_clip(Rect{0.0F, 0.0F, 200.0F, 120.0F});
    recorder.fill_rect(Rect{0.0F, 0.0F, 10.0F, 10.0F}, Color::rgba(64, 158, 255));
    recorder.draw_image(RenderResourceId{3U},
                        RenderImageOptions{.destination = Rect{12.0F, 0.0F, 32.0F, 32.0F}});
    recorder.draw_text("Element", Rect{0.0F, 40.0F, 80.0F, 20.0F},
                       TextStyle{.color = Color::rgba(48, 49, 51)});
    recorder.pop_clip();

    const auto graph = build_render_frame_graph(recorder.take_command_list());

    ASSERT_FALSE(graph.empty());
    EXPECT_GE(graph.estimated_draw_call_count, 3U);
    ASSERT_GE(graph.passes.size(), 4U);
    EXPECT_EQ(graph.passes[0].kind, RenderFramePassKind::State);
    EXPECT_EQ(graph.passes[1].kind, RenderFramePassKind::Geometry);
    ASSERT_FALSE(graph.pass_groups.empty());
    EXPECT_TRUE(graph.pass_groups.front().barrier_before);
    EXPECT_TRUE(std::any_of(graph.pass_groups.begin(), graph.pass_groups.end(),
                            [](const RenderFramePassGroup& group) {
                                return group.can_record_parallel;
                            }));
}

TEST(RenderContextTests, RenderResourceUploadQueueDrainsAndKeepsLifetimeActions) {
    RenderResourceUploadQueue queue;
    queue.push(RenderResourceUpload{.id = RenderResourceId{7},
                                    .action = RenderResourceAction::Upload,
                                    .kind = RenderResourceKind::Effect,
                                    .reference_count = 2});
    queue.push(RenderResourceUpload{.id = RenderResourceId{7},
                                    .action = RenderResourceAction::Release,
                                    .reference_count = 1});
    queue.push(RenderResourceUpload{.id = RenderResourceId{9},
                                    .kind = RenderResourceKind::Image,
                                    .width = 512,
                                    .height = 512,
                                    .payload = std::vector<std::byte>(128U * 1024U)});

    EXPECT_EQ(queue.size(RenderResourceUploadLane::HighFrequencySmall), 2U);
    EXPECT_EQ(queue.size(RenderResourceUploadLane::LowFrequencyLarge), 1U);

    const auto large_uploads = queue.drain(RenderResourceUploadLane::LowFrequencyLarge);
    ASSERT_EQ(large_uploads.size(), 1U);
    EXPECT_FALSE(queue.empty());

    const auto uploads = queue.drain();

    ASSERT_EQ(uploads.size(), 2U);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(uploads[0].action, RenderResourceAction::Upload);
    EXPECT_EQ(uploads[1].action, RenderResourceAction::Release);
    EXPECT_TRUE(queue.drain().empty());
}

TEST(RenderContextTests, RenderSceneBuildsOrderedRetainedLayerTree) {
    RenderCommandRecorder recorder;
    recorder.fill_rect(Rect{0.0F, 0.0F, 10.0F, 10.0F}, Color::rgba(10, 20, 30));
    recorder.push_layer(RenderLayerOptions{.bounds = Rect{20.0F, 0.0F, 40.0F, 40.0F},
                                           .opacity = 0.5F,
                                           .transform = Transform2D::translation(4.0F, 0.0F),
                                           .clips_to_bounds = true});
    recorder.fill_rect(Rect{20.0F, 0.0F, 40.0F, 40.0F}, Color::rgba(40, 50, 60));
    recorder.pop_layer();
    recorder.fill_rect(Rect{70.0F, 0.0F, 10.0F, 10.0F}, Color::rgba(70, 80, 90));

    RenderScene scene;
    scene.update_from_commands(recorder.take_command_list(), "test.retained");

    ASSERT_NE(scene.root(), nullptr);
    ASSERT_EQ(scene.root()->children.size(), 3U);
    EXPECT_EQ(scene.root()->children[0].kind, RenderNodeKind::Picture);
    EXPECT_EQ(scene.root()->children[1].kind, RenderNodeKind::Layer);
    EXPECT_EQ(scene.root()->children[2].kind, RenderNodeKind::Picture);
}

TEST(RenderContextTests, CompositorFrameBuilderKeepsSceneAndLayerMetadata) {
    RenderCommandRecorder recorder;
    recorder.fill_rect(Rect{0.0F, 0.0F, 20.0F, 20.0F}, Color::rgba(4, 5, 6));

    RenderScene scene;
    scene.update_from_commands(recorder.take_command_list());

    auto frame = CompositorFrameBuilder(42U)
                     .set_clear_color(Color::rgba(255, 255, 255))
                     .set_render_scene(std::move(scene))
                     .add_layer(CompositorLayer{.id = 7U,
                                                .bounds = Rect{0.0F, 0.0F, 20.0F, 20.0F},
                                                .opacity = 0.75F})
                     .build();

    EXPECT_EQ(frame.frame_id, 42U);
    ASSERT_EQ(frame.layers.size(), 1U);
    EXPECT_EQ(frame.layers[0].id, 7U);
}

TEST(RenderContextTests, GeometryClipCommandsPrepareFillPayloads) {
    RenderCommandRecorder recorder;
    const auto geometry = make_triangle_geometry();

    recorder.push_geometry_clip(geometry);
    recorder.fill_rect(Rect{0.0F, 0.0F, 80.0F, 80.0F}, Color::rgba(64, 158, 255));
    recorder.pop_geometry_clip();

    const auto command_list = recorder.take_command_list();
    ASSERT_EQ(command_list.commands().size(), 3U);
    const auto& clip = command_as<PushGeometryClipCommand>(command_list.commands()[0]);
    ASSERT_NE(clip.prepared_fill, nullptr);
    EXPECT_FALSE(clip.prepared_fill->filled_contours.empty());
    EXPECT_FALSE(clip.prepared_fill->tessellated_vertices.empty());
}

TEST(RenderContextTests, PreparedRenderCacheSharesGeometryAcrossRecorders) {
    const auto cache = std::make_shared<PreparedRenderCache>();
    const auto geometry = make_triangle_geometry();

    RenderCommandRecorder fill_recorder(cache);
    fill_recorder.fill_geometry(geometry, Color::rgba(64, 158, 255));
    auto fill_commands = fill_recorder.take_command_list();

    RenderCommandRecorder stroke_recorder(cache);
    stroke_recorder.stroke_geometry(geometry, Color::rgba(48, 49, 51),
                                    GeometryStrokeStyle{.width = 2.0F});
    auto stroke_commands = stroke_recorder.take_command_list();

    const auto& fill = command_as<FillGeometryCommand>(fill_commands.commands()[0]);
    const auto& stroke = command_as<StrokeGeometryCommand>(stroke_commands.commands()[0]);
    ASSERT_NE(fill.prepared_fill, nullptr);
    ASSERT_NE(stroke.prepared_stroke, nullptr);
    EXPECT_EQ(fill.prepared_fill->flatten, stroke.prepared_stroke->flatten);
}

TEST(RenderContextTests, SvgPathParserDoesNotEmitLeadingEmptyFigure) {
    const auto geometry = parse_svg_path("M0 0 L20 0 L20 20 z M30 30 L40 30");

    ASSERT_GE(geometry.figures.size(), 2U);
    EXPECT_FALSE(geometry.figures.front().segments.empty());
}

TEST(RenderContextTests, GeometryBoundsUseExactCurveExtrema) {
    RenderCommandRecorder recorder;
    recorder.fill_geometry(make_quadratic_geometry(), Color::rgba(64, 158, 255));
    auto quadratic_commands = recorder.take_command_list();
    EXPECT_FLOAT_EQ(quadratic_commands.bounds().width, 100.0F);
    EXPECT_FLOAT_EQ(quadratic_commands.bounds().height, 50.0F);

    recorder.fill_geometry(make_cubic_geometry(), Color::rgba(64, 158, 255));
    auto cubic_commands = recorder.take_command_list();
    EXPECT_FLOAT_EQ(cubic_commands.bounds().width, 100.0F);
    EXPECT_FLOAT_EQ(cubic_commands.bounds().height, 75.0F);

    recorder.fill_geometry(make_quarter_arc_geometry(), Color::rgba(64, 158, 255));
    auto arc_commands = recorder.take_command_list();
    EXPECT_NEAR(arc_commands.bounds().x, 50.0F, 0.0001F);
    EXPECT_NEAR(arc_commands.bounds().y, 50.0F, 0.0001F);
}

} // namespace
