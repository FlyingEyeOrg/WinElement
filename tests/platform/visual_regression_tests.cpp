#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#define WINELEMENT_RENDER_PIPELINE_DEMO_AS_LIBRARY 1
#include "../../samples/render_pipeline_demo/main.cpp"

namespace {

struct LoadedImage {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t stride = 0U;
    std::vector<std::byte> pixels;
};

struct DiffSummary {
    std::size_t mismatched_pixels = 0U;
    std::uint64_t total_channel_delta = 0U;
    std::uint8_t max_channel_delta = 0U;
    LoadedImage diff_image;
};

struct RegionCase {
    const char* name;
    layout::Rect DemoVisualRegions::*member;
};

[[nodiscard]] fs::path repo_root() {
    return fs::path(WINELEMENT_SOURCE_DIR);
}

[[nodiscard]] fs::path baseline_path() {
    return repo_root() / "tests" / "visual_baselines" / "render_pipeline_demo.png";
}

[[nodiscard]] fs::path failure_artifact_dir() {
    return fs::current_path() / "visual_regression_artifacts";
}

[[nodiscard]] bool should_update_baseline() {
    char* value = nullptr;
    std::size_t length = 0U;
    const auto error = _dupenv_s(&value, &length, "WINELEMENT_UPDATE_VISUAL_BASELINES");
    if (error != 0 || value == nullptr || length == 0U) {
        return false;
    }

    const auto update_requested = std::string_view(value) == "1";
    std::free(value);
    return update_requested;
}

[[nodiscard]] std::string sanitize_name(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (const auto ch : name) {
        const auto is_alnum = std::isalnum(static_cast<unsigned char>(ch)) != 0;
        result.push_back(is_alnum ? ch : '_');
    }
    return result;
}

[[nodiscard]] LoadedImage load_png(const fs::path& path) {
    ScopedComApartment com;

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    auto result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        throw make_hresult_error("failed to create WIC imaging factory", result);
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(result)) {
        throw make_hresult_error("failed to create PNG decoder", result);
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0U, &frame);
    if (FAILED(result)) {
        throw make_hresult_error("failed to load PNG frame", result);
    }

    LoadedImage image;
    result = frame->GetSize(&image.width, &image.height);
    if (FAILED(result)) {
        throw make_hresult_error("failed to query PNG size", result);
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(&converter);
    if (FAILED(result)) {
        throw make_hresult_error("failed to create WIC format converter", result);
    }

    result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    if (FAILED(result)) {
        throw make_hresult_error("failed to initialize WIC format converter", result);
    }

    image.stride = image.width * 4U;
    image.pixels.resize(static_cast<std::size_t>(image.stride) * image.height);
    result = converter->CopyPixels(nullptr, image.stride,
                                   static_cast<UINT>(image.pixels.size()),
                                   reinterpret_cast<BYTE*>(image.pixels.data()));
    if (FAILED(result)) {
        throw make_hresult_error("failed to copy PNG pixels", result);
    }

    return image;
}

[[nodiscard]] LoadedImage crop_image(const LoadedImage& image, layout::Rect region) {
    const auto origin_x = static_cast<std::uint32_t>(std::lround(region.x));
    const auto origin_y = static_cast<std::uint32_t>(std::lround(region.y));
    const auto width = static_cast<std::uint32_t>(std::lround(region.width));
    const auto height = static_cast<std::uint32_t>(std::lround(region.height));

    LoadedImage crop;
    crop.width = width;
    crop.height = height;
    crop.stride = width * 4U;
    crop.pixels.resize(static_cast<std::size_t>(crop.stride) * height);

    for (std::uint32_t row = 0U; row < height; ++row) {
        const auto* source = image.pixels.data() +
                             static_cast<std::size_t>(origin_y + row) * image.stride +
                             static_cast<std::size_t>(origin_x) * 4U;
        auto* destination = crop.pixels.data() + static_cast<std::size_t>(row) * crop.stride;
        std::copy_n(source, crop.stride, destination);
    }

    return crop;
}

[[nodiscard]] DiffSummary compare_images(const LoadedImage& actual, const LoadedImage& expected) {
    DiffSummary summary;
    summary.diff_image.width = actual.width;
    summary.diff_image.height = actual.height;
    summary.diff_image.stride = actual.stride;
    summary.diff_image.pixels.resize(actual.pixels.size());

    for (std::size_t index = 0U; index + 3U < actual.pixels.size(); index += 4U) {
        auto pixel_mismatch = false;
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            const auto actual_value =
                std::to_integer<std::uint8_t>(actual.pixels[index + channel]);
            const auto expected_value =
                std::to_integer<std::uint8_t>(expected.pixels[index + channel]);
            const auto delta = static_cast<std::uint8_t>(
                std::abs(static_cast<int>(actual_value) - static_cast<int>(expected_value)));
            summary.total_channel_delta += delta;
            summary.max_channel_delta = std::max(summary.max_channel_delta, delta);
            const auto amplified = delta == 0U
                                       ? static_cast<std::uint8_t>(255U)
                                       : static_cast<std::uint8_t>(std::min(255U, delta * 24U));
            summary.diff_image.pixels[index + channel] = std::byte{amplified};
            pixel_mismatch = pixel_mismatch || delta != 0U;
        }
        if (pixel_mismatch) {
            ++summary.mismatched_pixels;
            summary.diff_image.pixels[index + 3U] = std::byte{255U};
        }
    }

    return summary;
}

void write_failure_artifacts(std::string_view case_name, const LoadedImage& actual,
                             const LoadedImage& expected, const DiffSummary& diff) {
    const auto stem = sanitize_name(case_name);
    const auto output_directory = failure_artifact_dir();
    fs::create_directories(output_directory);
    save_png(output_directory / (stem + "_actual.png"), actual.width, actual.height, actual.stride,
             actual.pixels);
    save_png(output_directory / (stem + "_expected.png"), expected.width, expected.height,
             expected.stride, expected.pixels);
    save_png(output_directory / (stem + "_diff.png"), diff.diff_image.width, diff.diff_image.height,
             diff.diff_image.stride, diff.diff_image.pixels);
}

class RenderPipelineVisualRegressionTest : public ::testing::TestWithParam<RegionCase> {
  protected:
    static void SetUpTestSuite() {
        std::uint32_t stride = 0U;
        rendered_full_.width = canvas_width;
        rendered_full_.height = canvas_height;
        rendered_full_.pixels = render_demo_frame(rendered_demo_, stride);
        rendered_full_.stride = stride;

        if (should_update_baseline()) {
            fs::create_directories(baseline_path().parent_path());
            save_png(baseline_path(), rendered_full_.width, rendered_full_.height,
                     rendered_full_.stride, rendered_full_.pixels);
        }

        ASSERT_TRUE(fs::exists(baseline_path()))
            << "visual baseline is missing: " << baseline_path().string()
            << "\nSet WINELEMENT_UPDATE_VISUAL_BASELINES=1 once to regenerate it.";

        baseline_full_ = load_png(baseline_path());
        ASSERT_EQ(rendered_full_.width, baseline_full_.width);
        ASSERT_EQ(rendered_full_.height, baseline_full_.height);
        ASSERT_EQ(rendered_full_.stride, baseline_full_.stride);
    }

    static DemoArtifacts rendered_demo_;
    static LoadedImage rendered_full_;
    static LoadedImage baseline_full_;
};

DemoArtifacts RenderPipelineVisualRegressionTest::rendered_demo_{};
LoadedImage RenderPipelineVisualRegressionTest::rendered_full_{};
LoadedImage RenderPipelineVisualRegressionTest::baseline_full_{};

TEST_P(RenderPipelineVisualRegressionTest, MatchesCommittedBaseline) {
    const auto region = rendered_demo_.visual_regions.*GetParam().member;
    const auto actual = crop_image(rendered_full_, region);
    const auto expected = crop_image(baseline_full_, region);
    ASSERT_EQ(actual.width, expected.width);
    ASSERT_EQ(actual.height, expected.height);
    ASSERT_EQ(actual.stride, expected.stride);

    const auto diff = compare_images(actual, expected);
    if (diff.mismatched_pixels != 0U) {
        write_failure_artifacts(GetParam().name, actual, expected, diff);
    }

    EXPECT_EQ(diff.mismatched_pixels, 0U)
        << GetParam().name << " visual regression detected. mismatched pixels="
        << diff.mismatched_pixels << ", total channel delta=" << diff.total_channel_delta
        << ", max channel delta=" << static_cast<unsigned int>(diff.max_channel_delta)
        << ", artifacts=" << failure_artifact_dir().string();
}

INSTANTIATE_TEST_SUITE_P(
    RenderPipelineCoverage, RenderPipelineVisualRegressionTest,
    ::testing::Values(
        RegionCase{"FullCanvas", &DemoVisualRegions::full_canvas},
        RegionCase{"Header", &DemoVisualRegions::header},
        RegionCase{"PrimitivesPanel", &DemoVisualRegions::primitives_panel},
        RegionCase{"PrimitiveShapes", &DemoVisualRegions::primitive_shapes},
        RegionCase{"PrimitiveEdges", &DemoVisualRegions::primitive_edges},
        RegionCase{"PrimitiveCurves", &DemoVisualRegions::primitive_curves},
        RegionCase{"GeometryPanel", &DemoVisualRegions::geometry_panel},
        RegionCase{"GeometryFeatureMix", &DemoVisualRegions::geometry_feature_mix},
        RegionCase{"GeometryClipImage", &DemoVisualRegions::geometry_clip_image},
        RegionCase{"GeometryLargeMessage", &DemoVisualRegions::geometry_large_message},
        RegionCase{"GeometryMessageBoard", &DemoVisualRegions::geometry_message_board},
        RegionCase{"SvgPanel", &DemoVisualRegions::svg_panel},
        RegionCase{"SvgDetailBoard", &DemoVisualRegions::svg_detail_board},
        RegionCase{"SvgScaleBoard", &DemoVisualRegions::svg_scale_board},
        RegionCase{"TextPanel", &DemoVisualRegions::text_panel},
        RegionCase{"TextContent", &DemoVisualRegions::text_content},
        RegionCase{"TextSelection", &DemoVisualRegions::text_selection},
        RegionCase{"TextImage", &DemoVisualRegions::text_image},
        RegionCase{"TextLayer", &DemoVisualRegions::text_layer}),
    [](const ::testing::TestParamInfo<RegionCase>& info) { return info.param.name; });

} // namespace