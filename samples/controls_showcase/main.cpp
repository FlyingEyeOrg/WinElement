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
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <windows.h>
#include <psapi.h>
#include <malloc.h>
// clang-format on
#pragma comment(lib, "Psapi.lib")
#ifdef MessageBox
#undef MessageBox
#endif
#endif

namespace {

using namespace winelement;

constexpr auto canvas_width = 1440.0F;
constexpr auto canvas_height = 5000.0F;
constexpr auto showcase_window_width = 1320.0F;
constexpr auto showcase_window_height = 920.0F;
constexpr auto showcase_page_scrollbar_width = 14.0F;
constexpr auto showcase_page_gap = 8.0F;
constexpr auto showcase_image_resource_id = rendering::RenderResourceId{0x51494D475F53484FULL};
constexpr auto showcase_image_width = 640U;
constexpr auto showcase_image_height = 420U;

[[nodiscard]] float showcase_window_viewport_width(float window_width) noexcept {
    return std::max(window_width - showcase_page_gap - showcase_page_scrollbar_width, 1.0F);
}

[[nodiscard]] float showcase_window_viewport_width() noexcept {
    return showcase_window_viewport_width(showcase_window_width);
}

[[nodiscard]] style::UIElementStyle showcase_surface_style() noexcept {
    return style::style_from(style::default_panel_style(), [](style::UIElementStyle& surface) {
        surface.background = rendering::Color::rgba(0, 0, 0, 0);
        surface.border_color = rendering::Color::rgba(0, 0, 0, 0);
        surface.border_width = 0.0F;
        surface.shadow_visible = false;
    });
}

[[nodiscard]] std::uint8_t channel(float value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

[[nodiscard]] rendering::RenderResourceUpload make_showcase_image_upload() {
    constexpr auto stride = showcase_image_width * 4U;
    auto upload = rendering::RenderResourceUpload{
        .id = showcase_image_resource_id,
        .action = rendering::RenderResourceAction::Upload,
        .kind = rendering::RenderResourceKind::Image,
        .format = rendering::RenderResourceFormat::Bgra8Premultiplied,
        .reference_count = 1U,
        .width = showcase_image_width,
        .height = showcase_image_height,
        .stride = stride};
    upload.payload.resize(static_cast<std::size_t>(stride) * showcase_image_height);

    const auto write_pixel = [&upload](std::uint32_t x, std::uint32_t y, rendering::Color color) {
        const auto offset =
            static_cast<std::size_t>(y) * upload.stride + static_cast<std::size_t>(x) * 4U;
        upload.payload[offset + 0U] = std::byte{color.blue};
        upload.payload[offset + 1U] = std::byte{color.green};
        upload.payload[offset + 2U] = std::byte{color.red};
        upload.payload[offset + 3U] = std::byte{color.alpha};
    };

    for (std::uint32_t y = 0U; y < showcase_image_height; ++y) {
        const auto fy = static_cast<float>(y) / static_cast<float>(showcase_image_height - 1U);
        for (std::uint32_t x = 0U; x < showcase_image_width; ++x) {
            const auto fx = static_cast<float>(x) / static_cast<float>(showcase_image_width - 1U);
            auto red = 64.0F + 54.0F * fx;
            auto green = 145.0F + 80.0F * (1.0F - fy);
            auto blue = 210.0F + 32.0F * (1.0F - fx);

            const auto sun_dx = fx - 0.78F;
            const auto sun_dy = fy - 0.22F;
            if (sun_dx * sun_dx + sun_dy * sun_dy < 0.018F) {
                red = 255.0F;
                green = 205.0F;
                blue = 90.0F;
            }

            const auto ridge_a = 0.64F - std::abs(fx - 0.36F) * 0.75F;
            const auto ridge_b = 0.74F - std::abs(fx - 0.68F) * 0.95F;
            const auto ridge = std::max(ridge_a, ridge_b);
            if (fy > ridge) {
                const auto shade = std::clamp((fy - ridge) * 3.4F, 0.0F, 1.0F);
                red = 30.0F + 38.0F * shade + 26.0F * fx;
                green = 92.0F + 64.0F * shade;
                blue = 118.0F + 34.0F * (1.0F - shade);
            }

            if (fy > 0.78F) {
                const auto field = std::sin((fx * 26.0F + fy * 9.0F) * 3.1415926F) * 0.5F + 0.5F;
                red = 72.0F + 42.0F * field;
                green = 142.0F + 72.0F * field;
                blue = 82.0F + 26.0F * (1.0F - field);
            }

            const auto hash = (x * 73856093U) ^ (y * 19349663U) ^ 0x9E3779B9U;
            const auto noise = static_cast<float>(hash & 31U) - 15.0F;
            write_pixel(x, y,
                        rendering::Color::rgba(channel(red + noise), channel(green + noise),
                                               channel(blue + noise)));
        }
    }

    return upload;
}

void upload_showcase_resources(platform::Window& window) {
    window.upload_resource(make_showcase_image_upload());
}

[[nodiscard]] float loop_progress(animation::AnimationTimePoint now,
                                  float cycles_per_second) noexcept {
    if (!std::isfinite(cycles_per_second) || cycles_per_second <= 0.0F) {
        return 0.0F;
    }

    const auto cycle_ms =
        std::max(1LL, static_cast<long long>(std::lround(1000.0F / cycles_per_second)));
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const auto position = elapsed_ms >= 0 ? elapsed_ms % cycle_ms : 0LL;
    return static_cast<float>(position) / static_cast<float>(cycle_ms);
}

enum class MotionDemoKind { Translate, Scale, Rotate, Skew };

class MotionDemoPanel final : public controls::Panel {
  public:
    MotionDemoPanel& set_motion_kind(MotionDemoKind kind) noexcept {
        kind_ = kind;
        return *this;
    }

  protected:
    bool on_animation_frame(animation::AnimationTimePoint now) override {
        const auto seconds = std::chrono::duration<float>(now.time_since_epoch()).count();
        const auto phase = std::sin(seconds * 3.0F);
        if (std::abs(phase - phase_) >= 0.015F) {
            phase_ = phase;
            invalidate_paint();
        }
        return true;
    }

    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override {
        controls::Panel::on_paint(context, absolute_frame);

        const auto track =
            layout::Rect{absolute_frame.x + 16.0F, absolute_frame.y + absolute_frame.height - 28.0F,
                         std::max(0.0F, absolute_frame.width - 32.0F), 8.0F};
        context.fill_rounded_rect(track, rendering::CornerRadius::uniform(999.0F),
                                  rendering::Color::rgba(179, 216, 255));

        const auto center_x = track.x + track.width * (0.5F + phase_ * 0.32F);
        const auto center_y = track.y + track.height * 0.5F;
        auto marker = layout::Rect{center_x - 10.0F, center_y - 10.0F, 20.0F, 20.0F};
        auto radius = rendering::CornerRadius::uniform(10.0F);
        auto color = rendering::Color::rgba(64, 158, 255);
        switch (kind_) {
        case MotionDemoKind::Translate:
            break;
        case MotionDemoKind::Scale: {
            const auto size = 18.0F + (phase_ + 1.0F) * 4.0F;
            marker = layout::Rect{center_x - size * 0.5F, center_y - size * 0.5F, size, size};
            color = rendering::Color::rgba(103, 194, 58);
            break;
        }
        case MotionDemoKind::Rotate:
            marker = layout::Rect{center_x - 11.0F, center_y - 11.0F, 22.0F, 22.0F};
            radius = rendering::CornerRadius::uniform(4.0F + (phase_ + 1.0F) * 3.0F);
            color = rendering::Color::rgba(230, 162, 60);
            break;
        case MotionDemoKind::Skew:
            marker = layout::Rect{center_x - 14.0F + phase_ * 4.0F, center_y - 8.0F, 28.0F, 16.0F};
            color = rendering::Color::rgba(144, 147, 153);
            break;
        }
        context.fill_rounded_rect(marker, radius, color);
    }

  private:
    MotionDemoKind kind_ = MotionDemoKind::Translate;
    float phase_ = 0.0F;
};

struct LiveSample {
    float progress = 0.0F;
};

class ProgressTrackPanel final : public controls::Panel {
  public:
    ProgressTrackPanel& set_progress(float progress) {
        const auto normalized = std::clamp(progress, 0.0F, 1.0F);
        if (std::abs(normalized - progress_) < progress_epsilon_) {
            return *this;
        }
        progress_ = normalized;
        invalidate_paint();
        return *this;
    }

    ProgressTrackPanel& set_fill_color(rendering::Color color) {
        if (fill_color_ == color) {
            return *this;
        }
        fill_color_ = color;
        invalidate_paint();
        return *this;
    }

    ProgressTrackPanel& set_radius(float radius) noexcept {
        const auto normalized = std::max(radius, 0.0F);
        if (std::abs(normalized - radius_) < progress_epsilon_) {
            return *this;
        }
        radius_ = normalized;
        invalidate_paint();
        return *this;
    }

  protected:
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override {
        const auto radius = rendering::CornerRadius::uniform(radius_);
        context.fill_rounded_rect(absolute_frame, radius, track_color_);

        const auto fill_width =
            std::clamp(absolute_frame.width * progress_, 0.0F, absolute_frame.width);
        if (fill_width > 0.0F) {
            context.fill_rounded_rect(
                layout::Rect{absolute_frame.x, absolute_frame.y, fill_width, absolute_frame.height},
                radius, fill_color_);
        }

        context.stroke_rounded_rect(absolute_frame, radius, track_border_color_,
                                    track_border_width_);
    }

  private:
    static constexpr auto progress_epsilon_ = 0.005F;
    static constexpr auto track_border_width_ = 1.0F;
    float progress_ = 0.0F;
    float radius_ = 4.0F;
    rendering::Color track_color_ = rendering::Color::rgba(245, 247, 250);
    rendering::Color track_border_color_ = rendering::Color::rgba(220, 223, 230);
    rendering::Color fill_color_ = rendering::Color::rgba(64, 158, 255);
};

class SyncedViewportPanel final : public controls::Panel {
  public:
    using ScrollChangedHandler = std::function<void(layout::Point)>;

    SyncedViewportPanel& set_on_scroll_changed(ScrollChangedHandler handler) {
        scroll_changed_handler_ = std::move(handler);
        return *this;
    }

  protected:
    void on_pointer_event(elements::PointerEvent& event) override {
        const auto before = scroll_offset();
        controls::Panel::on_pointer_event(event);
        const auto after = scroll_offset();
        if ((before.x != after.x || before.y != after.y) && scroll_changed_handler_) {
            scroll_changed_handler_(after);
        }
    }

  private:
    ScrollChangedHandler scroll_changed_handler_;
};

struct ShowcaseWindowTree {
    std::unique_ptr<elements::UIElement> root;
    SyncedViewportPanel* viewport = nullptr;
    controls::Scrollbar* scrollbar = nullptr;
    elements::UIElement* virtual_content = nullptr;
};

struct ShowcaseWindowMetrics {
    float scroll_max = 0.0F;
    float scrollbar_max = 0.0F;
    layout::Rect viewport_rect{};
    layout::Rect content_rect{};
    float measured_content_height = 0.0F;
    std::size_t element_count = 0U;
    std::size_t realized_section_count = 0U;
};

struct ShowcaseRenderMetrics {
    std::size_t node_count = 0U;
    std::size_t command_count = 0U;
    rendering::PreparedRenderCacheStats prepared_cache{};
};

struct ShowcaseScrollSample {
    float offset = 0.0F;
    double layout_ms = 0.0;
    double commit_ms = 0.0;
    std::size_t element_count = 0U;
    bool virtualization_last_item_visible = false;
    ShowcaseRenderMetrics render{};
};

struct ShowcaseScrollProfile {
    float scroll_max = 0.0F;
    ShowcaseScrollSample top{};
    ShowcaseScrollSample middle{};
    ShowcaseScrollSample bottom{};
};

struct ProcessMemorySnapshot {
    bool available = false;
    std::size_t working_set_bytes = 0U;
    std::size_t peak_working_set_bytes = 0U;
    std::size_t private_bytes = 0U;
    std::size_t pagefile_bytes = 0U;
};

[[nodiscard]] ProcessMemorySnapshot query_process_memory() noexcept {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters)) == FALSE) {
        return {};
    }

    return ProcessMemorySnapshot{.available = true,
                                 .working_set_bytes = counters.WorkingSetSize,
                                 .peak_working_set_bytes = counters.PeakWorkingSetSize,
                                 .private_bytes = counters.PrivateUsage,
                                 .pagefile_bytes = counters.PagefileUsage};
#else
    return {};
#endif
}

[[nodiscard]] double bytes_to_mib(std::size_t bytes) noexcept {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void print_process_memory(std::string_view label) {
    const auto snapshot = query_process_memory();
    if (!snapshot.available) {
        std::cout << "memory " << label << ": unavailable\n";
        return;
    }

    const auto old_flags = std::cout.flags();
    const auto old_precision = std::cout.precision();
    std::cout << std::fixed << std::setprecision(1) << "memory " << label
              << ": working_set=" << bytes_to_mib(snapshot.working_set_bytes)
              << " MiB, private=" << bytes_to_mib(snapshot.private_bytes)
              << " MiB, peak_working_set=" << bytes_to_mib(snapshot.peak_working_set_bytes)
              << " MiB, pagefile=" << bytes_to_mib(snapshot.pagefile_bytes) << " MiB\n";
    std::cout.flags(old_flags);
    std::cout.precision(old_precision);
}

void trim_process_working_set() noexcept {
#ifdef _WIN32
    static_cast<void>(SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1),
                                               static_cast<SIZE_T>(-1)));
#endif
}

class LiveSampleCard final : public controls::Panel {
  public:
    using SampleFunction = std::function<LiveSample(animation::AnimationTimePoint)>;

    LiveSampleCard() {
        set_background(rendering::Color::rgba(255, 255, 255));
        set_border(rendering::Color::rgba(220, 223, 230), 1.0F);
        set_corner_radius(rendering::CornerRadius::uniform(4.0F));
        configure_layout([](layout::LayoutElement& item) {
            item.set_flex_direction(layout::FlexDirection::Column)
                .set_gap(layout::Gutter::Row, layout::Length::points(8.0F))
                .set_padding(layout::Edge::All, layout::Length::points(12.0F));
        });

        auto& label = append_new_child<controls::Text>();
        label.set_size(controls::TextSize::Small);
        label.configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(0.0F);
        });
        label_ = &label;

        auto& track = append_new_child<ProgressTrackPanel>();
        track.configure_layout([](layout::LayoutElement& item) {
            item.set_size(layout::Length::points(220.0F), layout::Length::points(8.0F))
                .set_flex_shrink(0.0F);
        });
        track_ = &track;
    }

    LiveSampleCard& set_sample_function(SampleFunction sample_function) {
        sample_function_ = std::move(sample_function);
        return *this;
    }

    LiveSampleCard& set_label(std::string_view label) {
        if (label_ != nullptr) {
            label_->set_text(label);
        }
        return *this;
    }

    LiveSampleCard& set_fill_color(rendering::Color color) {
        fill_color_ = color;
        if (track_ != nullptr) {
            track_->set_fill_color(color);
        }
        return *this;
    }

  protected:
    bool on_animation_frame(animation::AnimationTimePoint now) override {
        if (!sample_function_) {
            return false;
        }

        const auto sample = sample_function_(now);
        const auto progress = std::clamp(sample.progress, 0.0F, 1.0F);
        if (track_ != nullptr && std::abs(progress - fill_progress_) >= 0.005F) {
            fill_progress_ = progress;
            track_->set_progress(fill_progress_);
        }
        return true;
    }

  private:
    controls::Text* label_ = nullptr;
    ProgressTrackPanel* track_ = nullptr;
    SampleFunction sample_function_;
    rendering::Color fill_color_ = rendering::Color::rgba(64, 158, 255);
    float fill_progress_ = -1.0F;
};

[[nodiscard]] const core::Property<float>& implicit_demo_progress_property() {
    static const auto metadata = core::make_property<float>("showcase.implicit_demo_progress",
                                                            core::PropertyInvalidation::Paint);
    return metadata;
}

class ImplicitPropertyDemoPanel final : public controls::Panel {
  public:
    ImplicitPropertyDemoPanel() {
        set_background(rendering::Color::rgba(255, 255, 255));
        set_border(rendering::Color::rgba(220, 223, 230), 1.0F);
        set_corner_radius(rendering::CornerRadius::uniform(6.0F));
        configure_layout([](layout::LayoutElement& item) {
            item.set_flex_direction(layout::FlexDirection::Column)
                .set_gap(layout::Gutter::Row, layout::Length::points(10.0F))
                .set_padding(layout::Edge::All, layout::Length::points(14.0F));
        });

        auto& title = append_new_child<controls::Text>();
        title.set_text("Implicit property animation demo");
        title.set_size(controls::TextSize::Large);

        auto& desc = append_new_child<controls::Text>();
        desc.set_text(
                "Click replay to drive a custom property and mirror it into a live fill track.")
            .set_type(controls::TextType::Info)
            .set_size(controls::TextSize::Small);
        desc.configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(0.0F);
        });

        auto& value = append_new_child<controls::Text>();
        value.set_text("Progress preview");

        auto& track = append_new_child<ProgressTrackPanel>();
        track.set_radius(999.0F);
        track.configure_layout([](layout::LayoutElement& item) {
            item.set_size(layout::Length::points(track_width_), layout::Length::points(10.0F))
                .set_flex_shrink(0.0F);
        });
        fill_ = &track;

        auto& replay = append_new_child<controls::Button>();
        replay.set_text("Restart implicit property animation")
            .set_type(controls::ButtonType::Primary);
        replay.configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(280.0F)).set_flex_shrink(0.0F);
        });
        replay.set_on_click([this]() { replay_animation(); });

        set_property(implicit_demo_progress_property(), 0.0F);
        sync_visuals();
    }

    void replay_animation() {
        set_property(implicit_demo_progress_property(), 0.0F);
        animate_property(
            implicit_demo_progress_property(), 1.0F,
            animation::AnimationTiming{.duration = animation::AnimationDuration{0.7F},
                                       .iteration_count = 4.0F,
                                       .direction = animation::PlaybackDirection::Alternate,
                                       .fill_mode = animation::FillMode::Both,
                                       .easing = animation::EasingFunction::ease_in_out_cubic()});
        sync_visuals();
    }

  protected:
    bool on_animation_frame(animation::AnimationTimePoint now) override {
        static_cast<void>(now);
        sync_visuals();
        return false;
    }

  private:
    void sync_visuals() {
        const auto progress =
            std::clamp(properties().value(implicit_demo_progress_property(), 0.0F), 0.0F, 1.0F);
        if (fill_ != nullptr && std::abs(progress - fill_progress_) >= 0.005F) {
            fill_progress_ = progress;
            fill_->set_progress(fill_progress_);
        }
    }

    ProgressTrackPanel* fill_ = nullptr;
    float fill_progress_ = -1.0F;
    static constexpr auto track_width_ = 320.0F;
};

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
    frame.set_title(title).set_style(showcase_surface_style());
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
    group.set_title(title).set_style(showcase_surface_style());
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

[[nodiscard]] std::string message_box_action_label(controls::MessageBoxAction action) {
    switch (action) {
    case controls::MessageBoxAction::Confirm:
        return "confirm";
    case controls::MessageBoxAction::Cancel:
        return "cancel";
    case controls::MessageBoxAction::Close:
        return "close";
    }
    return "close";
}

[[nodiscard]] std::string dialog_action_label(controls::DialogAction action) {
    switch (action) {
    case controls::DialogAction::Confirm:
        return "confirm";
    case controls::DialogAction::Cancel:
        return "cancel";
    case controls::DialogAction::Close:
        return "close";
    }
    return "close";
}

[[nodiscard]] std::string long_feedback_copy(std::string_view component_name) {
    return std::string(component_name) +
           " can carry longer descriptive content when a workflow needs extra context. "
           "This sample intentionally uses several sentences so spacing, wrapping, and the "
           "footer distance can be inspected without switching to a separate page. "
           "Use it to compare the Element Plus style surface rhythm: title, body, and actions "
           "should still feel calm even when the content becomes dense. "
           "The body should wrap naturally, preserve readable line height, and avoid pushing "
           "the action row into an awkward cramped position.";
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

[[nodiscard]] std::string playback_direction_label(animation::PlaybackDirection direction) {
    switch (direction) {
    case animation::PlaybackDirection::Normal:
        return "Normal";
    case animation::PlaybackDirection::Reverse:
        return "Reverse";
    case animation::PlaybackDirection::Alternate:
        return "Alternate";
    case animation::PlaybackDirection::AlternateReverse:
        return "Alt reverse";
    }
    return "Normal";
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
    add_button(type_row, "Info", controls::ButtonType::Info);
    add_button(type_row, "Warning", controls::ButtonType::Warning);
    add_button(type_row, "Danger", controls::ButtonType::Danger);
    add_button(type_row, "Text", controls::ButtonType::Text).set_text_variant(true);

    auto& plain_row = section.append_new_child<controls::StackPanel>();
    configure_row(plain_row);
    add_button(plain_row, "Plain", controls::ButtonType::Default).set_plain(true);
    add_button(plain_row, "Primary", controls::ButtonType::Primary).set_plain(true);
    add_button(plain_row, "Success", controls::ButtonType::Success).set_plain(true);
    add_button(plain_row, "Info", controls::ButtonType::Info).set_plain(true);
    add_button(plain_row, "Warning", controls::ButtonType::Warning).set_plain(true);
    add_button(plain_row, "Danger", controls::ButtonType::Danger).set_plain(true);

    auto& round_row = section.append_new_child<controls::StackPanel>();
    configure_row(round_row);
    add_button(round_row, "Round", controls::ButtonType::Default).set_round(true);
    add_button(round_row, "Primary", controls::ButtonType::Primary).set_round(true);
    add_button(round_row, "Success", controls::ButtonType::Success).set_round(true);
    add_button(round_row, "Info", controls::ButtonType::Info).set_round(true);
    add_button(round_row, "Warning", controls::ButtonType::Warning).set_round(true);
    add_button(round_row, "Danger", controls::ButtonType::Danger).set_round(true);

    auto& dashed_row = section.append_new_child<controls::StackPanel>();
    configure_row(dashed_row);
    add_button(dashed_row, "Dashed", controls::ButtonType::Default).set_dashed(true);
    add_button(dashed_row, "Primary", controls::ButtonType::Primary)
        .set_plain(true)
        .set_dashed(true);
    add_button(dashed_row, "Success", controls::ButtonType::Success)
        .set_plain(true)
        .set_dashed(true);
    add_button(dashed_row, "Info", controls::ButtonType::Info).set_plain(true).set_dashed(true);
    add_button(dashed_row, "Warning", controls::ButtonType::Warning)
        .set_plain(true)
        .set_dashed(true);
    add_button(dashed_row, "Danger", controls::ButtonType::Danger).set_plain(true).set_dashed(true);

    auto& icon_row = section.append_new_child<controls::StackPanel>();
    configure_row(icon_row);
    add_button(icon_row, "", controls::ButtonType::Default)
        .set_circle(true)
        .set_icon_paths(elements::icons::Search);
    add_button(icon_row, "", controls::ButtonType::Primary)
        .set_circle(true)
        .set_icon_paths(elements::icons::Edit);
    add_button(icon_row, "", controls::ButtonType::Success)
        .set_circle(true)
        .set_icon_paths(elements::icons::Check);
    add_button(icon_row, "", controls::ButtonType::Info)
        .set_circle(true)
        .set_icon_paths(elements::icons::Message);
    add_button(icon_row, "", controls::ButtonType::Warning)
        .set_circle(true)
        .set_icon_paths(elements::icons::Star);
    add_button(icon_row, "", controls::ButtonType::Danger)
        .set_circle(true)
        .set_icon_paths(elements::icons::Delete);

    auto& variant_row = section.append_new_child<controls::StackPanel>();
    configure_row(variant_row);
    add_button(variant_row, "Large", controls::ButtonType::Primary)
        .set_size(controls::ButtonSize::Large);
    add_button(variant_row, "Small", controls::ButtonType::Default)
        .set_size(controls::ButtonSize::Small);
    add_button(variant_row, "Link", controls::ButtonType::Text).set_link_variant(true);
    auto& loading_button = add_button(variant_row, "Toggle loading", controls::ButtonType::Primary);
    loading_button.set_on_click(
        [&loading_button]() { loading_button.set_loading(!loading_button.loading()); });
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

    auto& vertical_viewport = vertical_line.append_new_child<SyncedViewportPanel>();
    vertical_viewport.set_background(rendering::Color::rgba(255, 255, 255));
    vertical_viewport.set_border(rendering::Color::rgba(220, 223, 230), 1.0F);
    vertical_viewport.set_corner_radius(rendering::CornerRadius::uniform(4.0F));
    vertical_viewport.set_overflow(layout::Overflow::Hidden);
    vertical_viewport.set_viewport(layout::Rect{0.0F, 0.0F, 342.0F, 156.0F});
    vertical_viewport.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(342.0F), layout::Length::points(156.0F))
            .set_flex_shrink(0.0F)
            .set_padding(layout::Edge::All, layout::Length::points(8.0F));
    });
    auto& vertical_content = vertical_viewport.append_new_child<controls::StackPanel>();
    vertical_content.set_gap(6.0F);
    vertical_content.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(310.0F), layout::Length::points(450.0F))
            .set_flex_shrink(0.0F);
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
        .set_range(0.0F, 302.0F, 156.0F)
        .set_value(0.0F)
        .set_on_scroll([&vertical_viewport](float value) {
            vertical_viewport.set_scroll_offset(layout::Point{0.0F, value});
        });
    vertical_viewport.set_on_scroll_changed(
        [&vertical](layout::Point offset) { vertical.set_value(offset.y); });
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

    auto& horizontal_viewport = horizontal_group.append_new_child<SyncedViewportPanel>();
    horizontal_viewport.set_background(rendering::Color::rgba(255, 255, 255));
    horizontal_viewport.set_border(rendering::Color::rgba(220, 223, 230), 1.0F);
    horizontal_viewport.set_corner_radius(rendering::CornerRadius::uniform(4.0F));
    horizontal_viewport.set_overflow(layout::Overflow::Hidden);
    horizontal_viewport.set_viewport(layout::Rect{0.0F, 0.0F, 540.0F, 120.0F});
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
        .set_range(0.0F, 328.0F, 540.0F)
        .set_value(0.0F)
        .set_on_scroll([&horizontal_viewport](float value) {
            horizontal_viewport.set_scroll_offset(layout::Point{value, 0.0F});
        });
    horizontal_viewport.set_on_scroll_changed(
        [&horizontal](layout::Point offset) { horizontal.set_value(offset.x); });
    horizontal.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(540.0F), layout::Length::points(14.0F))
            .set_flex_shrink(0.0F);
    });

    auto& container_group = scroll_row.append_new_child<controls::StackPanel>();
    container_group.set_gap(8.0F);
    container_group.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(420.0F)).set_flex_shrink(0.0F);
    });
    add_label(container_group, "Element Plus style scrollbar container");
    auto& container_scrollbar = container_group.append_new_child<controls::Scrollbar>();
    container_scrollbar.set_container_mode(true)
        .set_always_visible(true)
        .set_min_size(24.0F)
        .set_distance(16.0F);
    container_scrollbar.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_height(layout::Length::points(180.0F))
            .set_flex_shrink(0.0F);
    });
    auto& container_content = container_scrollbar.append_new_child<controls::StackPanel>();
    container_content.set_gap(8.0F);
    container_content.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_min_height(layout::Length::points(420.0F))
            .set_padding(layout::Edge::Top, layout::Length::points(10.0F))
            .set_padding(layout::Edge::Bottom, layout::Length::points(10.0F))
            .set_flex_shrink(0.0F);
    });
    for (auto index = 1; index <= 10; ++index) {
        auto& item = container_content.append_new_child<controls::Border>();
        item.set_preset(index % 2 == 0 ? controls::BorderPreset::Primary
                                       : controls::BorderPreset::Info);
        item.configure_layout([](layout::LayoutElement& cell) {
            cell.set_width(layout::Length::percent(100.0F))
                .set_min_height(layout::Length::points(36.0F))
                .set_padding(layout::Edge::All, layout::Length::points(8.0F))
                .set_flex_shrink(0.0F);
        });
        item.append_new_child<controls::Text>().set_text("Container item " + std::to_string(index));
    }

    auto& items = section.append_new_child<controls::ItemsControl>();
    items.set_items({"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta"})
        .set_reusable_container_limit(16U)
        .set_item_factory([](controls::ItemsControl::ItemContext context) {
            auto item = std::make_unique<controls::StackPanel>();
            item->set_gap(4.0F);

            auto& title = item->append_new_child<controls::Text>();
            title.set_text(std::string(context.item))
                .set_type(context.selected ? controls::TextType::Primary
                                           : controls::TextType::Primary);

            auto& meta = item->append_new_child<controls::Text>();
            meta.set_text("Item " + std::to_string(context.index + 1U) +
                          (context.selected ? " - selected" : " - hover to inspect"))
                .set_type(controls::TextType::Info)
                .set_size(controls::TextSize::Small);
            return item;
        })
        .set_selection_mode(controls::ItemsControl::SelectionMode::Multiple)
        .set_selected_indices({1U, 3U});
    items.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(420.0F))
            .set_min_height(layout::Length::points(150.0F))
            .set_flex_shrink(0.0F);
    });

    auto view_model = std::make_shared<core::ObservableObject>();
    view_model->set("headline", std::string{"MVVM bound virtual content"});
    view_model->set("status",
                    std::string{"Observable source, retained binding, keyed virtual tree"});

    auto& mvvm_panel = section.append_new_child<controls::StackPanel>();
    mvvm_panel.set_gap(8.0F);
    mvvm_panel.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(520.0F))
            .set_min_height(layout::Length::points(260.0F))
            .set_flex_shrink(0.0F);
    });
    auto& bound_title = mvvm_panel.append_new_child<controls::Text>();
    bound_title.set_type(controls::TextType::Primary)
        .set_size(controls::TextSize::Large)
        .bind_text(elements::Binding::path("headline").with_source(view_model));
    auto& bound_status = mvvm_panel.append_new_child<controls::Text>();
    bound_status.set_type(controls::TextType::Info)
        .set_size(controls::TextSize::Small)
        .bind_text(elements::Binding::path("status").with_source(view_model));
    view_model->set("status", std::string{"Bindings update without rebuilding the visual subtree"});

    auto observable_items = std::make_shared<core::ObservableStringList>();
    auto mvvm_rows = std::vector<std::string>{};
    mvvm_rows.reserve(48U);
    for (auto index = 0U; index < 48U; ++index) {
        mvvm_rows.push_back("Observable row " + std::to_string(index + 1U));
    }
    observable_items->reset(std::move(mvvm_rows));

    auto& mvvm_items = mvvm_panel.append_new_child<controls::ItemsControl>();
    mvvm_items.bind_items(observable_items)
        .set_reusable_container_limit(12U)
        .set_item_factory([](controls::ItemsControl::ItemContext context) {
            auto row = std::make_unique<controls::StackPanel>();
            row->set_gap(2.0F);
            row->append_new_child<controls::Text>()
                .set_text(std::string{context.item})
                .set_type(controls::TextType::Primary);
            row->append_new_child<controls::Text>()
                .set_text("Realized item " + std::to_string(context.index + 1U))
                .set_type(controls::TextType::Info)
                .set_size(controls::TextSize::Small);
            return row;
        });
    mvvm_items.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(420.0F))
            .set_min_height(layout::Length::points(136.0F))
            .set_flex_shrink(0.0F);
    });

    auto& virtual_host = mvvm_panel.append_new_child<controls::StackPanel>();
    virtual_host.set_gap(4.0F);
    auto virtual_children = std::vector<elements::VirtualElement>{};
    for (auto index = 0U; index < 3U; ++index) {
        virtual_children.push_back(elements::make_virtual_element<controls::Text>(
            "metric-" + std::to_string(index), [index](controls::Text& text) {
                text.set_text("Virtual metric " + std::to_string(index + 1U))
                    .set_type(index == 0U ? controls::TextType::Success : controls::TextType::Info)
                    .set_size(controls::TextSize::Small);
            }));
    }
    elements::ElementReconciler{}.reconcile_children(virtual_host, virtual_children);
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

void configure_showcase_image(controls::Image& image, layout::Size size,
                              rendering::Color background = rendering::Color::rgba(245, 247, 250)) {
    image.set_style(style::style_from(
        style::default_image_style(), [background](style::UIElementStyle& image_style) {
            image_style.background = background;
            image_style.border_color = rendering::Color::rgba(220, 223, 230);
            image_style.border_width = 1.0F;
            image_style.padding = layout::EdgeInsets{1.0F, 1.0F, 1.0F, 1.0F};
            image_style.pixel_snapped_border = true;
        }));
    image.configure_layout([size](layout::LayoutElement& item) {
        item.set_size(layout::Length::points(size.width), layout::Length::points(size.height))
            .set_flex_shrink(0.0F);
    });
}

controls::Image& add_image_sample(controls::StackPanel& row, std::string_view title,
                                  controls::ImageFit fit, layout::Size size,
                                  std::optional<layout::Rect> source_rect = std::nullopt,
                                  float position_x = 0.5F, float position_y = 0.5F,
                                  float opacity = 1.0F) {
    auto& card = row.append_new_child<controls::Border>();
    card.set_title(title).set_preset(controls::BorderPreset::Plain);
    configure_card(card, 260.0F);

    auto& stack = card.append_new_child<controls::StackPanel>();
    stack.set_gap(8.0F);
    stack.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(0.0F);
    });

    auto& label = stack.append_new_child<controls::Text>();
    label.set_text(title).set_size(controls::TextSize::Small);
    label.configure_layout([](layout::LayoutElement& item) { item.set_flex_shrink(0.0F); });

    auto& image = stack.append_new_child<controls::Image>();
    image.set_source(showcase_image_resource_id, showcase_image_width, showcase_image_height)
        .set_object_fit(fit)
        .set_object_position(position_x, position_y)
        .set_image_opacity(opacity)
        .set_alt_text(title);
    if (source_rect.has_value()) {
        image.set_source_rect(*source_rect);
    }
    configure_showcase_image(image, size,
                             opacity < 1.0F ? rendering::Color::rgba(236, 245, 255)
                                            : rendering::Color::rgba(245, 247, 250));
    return image;
}

void add_image_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Image: browser-style object fitting");
    auto& row = section.append_new_child<controls::StackPanel>();
    configure_row(row);

    add_image_sample(row, "object-fit: fill", controls::ImageFit::Fill,
                     layout::Size{220.0F, 140.0F});
    add_image_sample(row, "object-fit: contain", controls::ImageFit::Contain,
                     layout::Size{220.0F, 140.0F});
    add_image_sample(row, "object-fit: cover", controls::ImageFit::Cover,
                     layout::Size{220.0F, 140.0F});
    add_image_sample(row, "cover position: top left", controls::ImageFit::Cover,
                     layout::Size{220.0F, 140.0F}, std::nullopt, 0.0F, 0.0F);
    add_image_sample(row, "object-fit: none", controls::ImageFit::None,
                     layout::Size{220.0F, 140.0F}, std::nullopt, 0.0F, 0.0F);
    add_image_sample(row, "object-fit: scale-down", controls::ImageFit::ScaleDown,
                     layout::Size{220.0F, 140.0F});
    add_image_sample(row, "source crop + cover", controls::ImageFit::Cover,
                     layout::Size{220.0F, 140.0F}, layout::Rect{120.0F, 70.0F, 320.0F, 210.0F});
    add_image_sample(row, "image opacity", controls::ImageFit::Contain,
                     layout::Size{220.0F, 140.0F}, std::nullopt, 0.5F, 0.5F, 0.56F);
}

void add_feedback_section(controls::StackPanel& root, elements::UIElement& feedback_host,
                          platform::Window* host_window = nullptr) {
    auto& section = add_section(root, "Feedback components");
    auto dialog_windows = std::make_shared<std::vector<std::shared_ptr<controls::DialogWindow>>>();
    auto prune_dialog_windows = [dialog_windows]() {
        dialog_windows->erase(
            std::remove_if(dialog_windows->begin(), dialog_windows->end(),
                           [](const std::shared_ptr<controls::DialogWindow>& dialog) {
                               return dialog == nullptr || !dialog->is_open();
                           }),
            dialog_windows->end());
    };

    auto& messages = add_demo_group(section, "Message");
    auto& message_row = messages.append_new_child<controls::StackPanel>();
    configure_row(message_row);
    for (const auto type : {controls::MessageType::Primary, controls::MessageType::Success,
                            controls::MessageType::Warning, controls::MessageType::Info,
                            controls::MessageType::Error}) {
        const auto label = message_type_label(type);
        auto& button = add_button(message_row, "Show " + label, controls::ButtonType::Default);
        button.set_on_click([&feedback_host, type, label]() {
            controls::Message::show(
                feedback_host, controls::MessageOptions{
                                   .text = label + " message", .type = type, .duration_ms = 3000});
        });
    }
    add_button(message_row, "Manual close", controls::ButtonType::Info)
        .set_on_click([&feedback_host]() {
            controls::Message::show(feedback_host,
                                    controls::MessageOptions{.text = "Manual close message",
                                                             .type = controls::MessageType::Info,
                                                             .show_close = true,
                                                             .duration_ms = 0});
        });

    auto& message_boxes = add_demo_group(section, "MessageBox");
    auto& box_row = message_boxes.append_new_child<controls::StackPanel>();
    configure_row(box_row);
    add_button(box_row, "Alert", controls::ButtonType::Info).set_on_click([&feedback_host]() {
        controls::MessageBox::show(
            feedback_host,
            controls::MessageBoxOptions{.title = "Alert",
                                        .message = "Simple notification with one primary action.",
                                        .kind = controls::MessageBoxKind::Alert,
                                        .type = controls::MessageType::Info,
                                        .show_cancel_button = false,
                                        .close_on_click_modal = false,
                                        .close_on_press_escape = false});
    });
    add_button(box_row, "Confirm", controls::ButtonType::Warning).set_on_click([&feedback_host]() {
        controls::MessageBox::show(
            feedback_host, controls::MessageBoxOptions{
                               .title = "Confirm",
                               .message = "Confirm and cancel actions with close distinction.",
                               .kind = controls::MessageBoxKind::Confirm,
                               .type = controls::MessageType::Warning,
                               .distinguish_cancel_and_close = true,
                               .draggable = true,
                               .modal = true,
                               .close_on_click_modal = false,
                               .close_on_press_escape = false,
                               .on_action = [&feedback_host](controls::MessageBoxAction action,
                                                             std::string value) {
                                   static_cast<void>(value);
                                   controls::Message::show(
                                       feedback_host,
                                       controls::MessageOptions{
                                           .text = "MessageBox " + message_box_action_label(action),
                                           .type = controls::MessageType::Info,
                                           .show_close = true});
                               }});
    });
    add_button(box_row, "Prompt", controls::ButtonType::Success).set_on_click([&feedback_host]() {
        controls::MessageBox::show(
            feedback_host,
            controls::MessageBoxOptions{
                .title = "Prompt",
                .message = "Input validation, loading confirm and centered layout.",
                .kind = controls::MessageBoxKind::Prompt,
                .type = controls::MessageType::Success,
                .input_placeholder = "Project name",
                .input_text = "WinElement",
                .confirm_loading = false,
                .center = true,
                .close_on_click_modal = false,
                .close_on_press_escape = false,
                .content_builder =
                    [](controls::StackPanel& content) {
                        content.append_new_child<controls::Text>()
                            .set_text("Custom content builder")
                            .set_type(controls::TextType::Info)
                            .set_size(controls::TextSize::Small);
                    },
                .input_error_message = "Project name is required",
                .input_validator = [](std::string_view value) -> std::optional<std::string> {
                    return value.empty() ? std::optional<std::string>{"Project name is required"}
                                         : std::nullopt;
                },
                .on_action =
                    [&feedback_host](controls::MessageBoxAction action, std::string value) {
                        controls::Message::show(
                            feedback_host,
                            controls::MessageOptions{
                                .text = "Prompt " + message_box_action_label(action) + ": " + value,
                                .type = controls::MessageType::Success,
                                .show_close = true});
                    }});
    });
    add_button(box_row, "Non-modal", controls::ButtonType::Default)
        .set_on_click([&feedback_host]() {
            controls::MessageBox::show(feedback_host,
                                       controls::MessageBoxOptions{
                                           .title = "Non-modal MessageBox",
                                           .message = "No backdrop mask; the page remains visible.",
                                           .kind = controls::MessageBoxKind::Confirm,
                                           .type = controls::MessageType::Primary,
                                           .draggable = true,
                                           .modal = false,
                                           .close_on_click_modal = false});
        });
    add_button(box_row, "Large content", controls::ButtonType::Info)
        .set_on_click([&feedback_host]() {
            controls::MessageBox::show(
                feedback_host,
                controls::MessageBoxOptions{
                    .title = "Large content",
                    .message = long_feedback_copy("MessageBox"),
                    .kind = controls::MessageBoxKind::Confirm,
                    .type = controls::MessageType::Info,
                    .confirm_button_text = "I understand",
                    .cancel_button_text = "Cancel",
                    .show_cancel_button = true,
                    .modal = true,
                    .close_on_click_modal = false,
                    .close_on_press_escape = false,
                    .width = 520.0F,
                    .content_builder = [](controls::StackPanel& content) {
                        content.append_new_child<controls::Text>()
                            .set_text("Additional note: this row is built through the custom "
                                      "content slot.")
                            .set_type(controls::TextType::Info)
                            .set_size(controls::TextSize::Small);
                    }});
        });
    add_button(box_row, "Nested box", controls::ButtonType::Success)
        .set_on_click([&feedback_host]() {
            controls::MessageBox::show(
                feedback_host,
                controls::MessageBoxOptions{
                    .title = "First MessageBox",
                    .message = "Confirm this MessageBox to open another MessageBox on top.",
                    .kind = controls::MessageBoxKind::Confirm,
                    .type = controls::MessageType::Success,
                    .confirm_button_text = "Open next",
                    .cancel_button_text = "Cancel",
                    .distinguish_cancel_and_close = true,
                    .modal = true,
                    .close_on_click_modal = false,
                    .close_on_press_escape = false,
                    .close_on_confirm = false,
                    .on_action = [&feedback_host](controls::MessageBoxAction action,
                                                  std::string value) {
                        static_cast<void>(value);
                        if (action != controls::MessageBoxAction::Confirm) {
                            return;
                        }
                        controls::MessageBox::show(
                            feedback_host,
                            controls::MessageBoxOptions{
                                .title = "Second MessageBox",
                                .message = "Nested MessageBox content. This verifies stacked "
                                           "modal feedback, focus, and backdrop behavior.",
                                .kind = controls::MessageBoxKind::Alert,
                                .type = controls::MessageType::Primary,
                                .show_cancel_button = false,
                                .modal = true,
                                .close_on_click_modal = false,
                                .close_on_press_escape = false});
                    }});
        });

    auto& dialogs = add_demo_group(section, "Dialog");
    auto& dialog_row = dialogs.append_new_child<controls::StackPanel>();
    configure_row(dialog_row);
    add_button(dialog_row, "Open dialog", controls::ButtonType::Primary)
        .set_on_click([&feedback_host]() {
            controls::Dialog::show(
                feedback_host,
                controls::DialogOptions{
                    .title = "Dialog",
                    .body = "Modal surface with header, body, footer, close and confirm actions.",
                    .show_cancel_button = true,
                    .modal = true,
                    .close_on_click_modal = false,
                    .close_on_press_escape = false,
                    .draggable = true,
                    .on_action = [&feedback_host](controls::DialogAction action) {
                        controls::Message::show(feedback_host,
                                                controls::MessageOptions{
                                                    .text = "Dialog " + dialog_action_label(action),
                                                    .type = controls::MessageType::Primary,
                                                    .show_close = true});
                    }});
        });
    add_button(dialog_row, "Fullscreen dialog", controls::ButtonType::Warning)
        .set_on_click([&feedback_host]() {
            controls::Dialog::show(
                feedback_host, controls::DialogOptions{
                                   .title = "Fullscreen dialog",
                                   .body = "Element Plus style fullscreen dialog sample in the "
                                           "showcase. The dialog expands to the viewport and keeps "
                                           "the close affordance available.",
                                   .show_cancel_button = true,
                                   .modal = true,
                                   .close_on_click_modal = false,
                                   .close_on_press_escape = false,
                                   .fullscreen = true,
                                   .draggable = false});
        });
    add_button(dialog_row, "No cancel", controls::ButtonType::Default)
        .set_on_click([&feedback_host]() {
            controls::Dialog::show(
                feedback_host,
                controls::DialogOptions{.title = "Compact dialog",
                                        .body = "A compact dialog variant without a cancel button.",
                                        .show_cancel_button = false,
                                        .close_on_click_modal = false,
                                        .close_on_press_escape = false,
                                        .draggable = false,
                                        .width = 420.0F});
        });
    add_button(dialog_row, "Non-modal dialog", controls::ButtonType::Info)
        .set_on_click([&feedback_host]() {
            controls::Dialog::show(
                feedback_host, controls::DialogOptions{.title = "Non-modal dialog",
                                                       .body = "Element Plus style dialog without "
                                                               "modal mask.",
                                                       .show_cancel_button = true,
                                                       .modal = false,
                                                       .close_on_click_modal = false,
                                                       .draggable = true,
                                                       .width = 520.0F});
        });
    add_button(dialog_row, "Large content", controls::ButtonType::Success)
        .set_on_click([&feedback_host]() {
            controls::Dialog::show(
                feedback_host, controls::DialogOptions{.title = "Large content dialog",
                                                       .body = long_feedback_copy("Dialog") + " " +
                                                               long_feedback_copy("Dialog"),
                                                       .show_cancel_button = true,
                                                       .modal = true,
                                                       .close_on_click_modal = false,
                                                       .close_on_press_escape = false,
                                                       .draggable = true,
                                                       .width = 640.0F,
                                                       .height = 420.0F});
        });
    add_button(dialog_row, "Nested dialog", controls::ButtonType::Warning)
        .set_on_click([&feedback_host]() {
            controls::Dialog::show(
                feedback_host,
                controls::DialogOptions{
                    .title = "First dialog",
                    .body = "Confirm this dialog to open another dialog while keeping this one "
                            "underneath.",
                    .confirm_button_text = "Open next",
                    .show_cancel_button = true,
                    .modal = true,
                    .close_on_click_modal = false,
                    .close_on_press_escape = false,
                    .close_on_confirm = false,
                    .draggable = true,
                    .on_action = [&feedback_host](controls::DialogAction action) {
                        if (action != controls::DialogAction::Confirm) {
                            return;
                        }
                        controls::Dialog::show(
                            feedback_host,
                            controls::DialogOptions{
                                .title = "Second dialog",
                                .body = "Nested dialog content. This verifies stacked modal "
                                        "surfaces, focus, and shadow composition.",
                                .show_cancel_button = true,
                                .modal = true,
                                .close_on_click_modal = false,
                                .close_on_press_escape = false,
                                .draggable = true,
                                .width = 460.0F});
                    }});
        });

    auto& dialog_windows_group = add_demo_group(section, "DialogWindow");
    auto& dialog_window_row = dialog_windows_group.append_new_child<controls::StackPanel>();
    configure_row(dialog_window_row);
    add_button(dialog_window_row, "Modal window", controls::ButtonType::Primary)
        .set_on_click([&feedback_host, host_window]() {
            controls::DialogWindow dialog;
            dialog.set_title("DialogWindow")
                .set_body("Independent Win32 window with native title bar and Element Plus "
                          "style dialog body and footer actions.")
                .set_show_cancel_button(true)
                .set_modal(true)
                .set_owner(host_window)
                .set_on_action([&feedback_host](controls::DialogAction action) {
                    controls::Message::show(feedback_host,
                                            controls::MessageOptions{
                                                .text = "DialogWindow " +
                                                        dialog_action_label(action),
                                                .type = controls::MessageType::Primary,
                                                .show_close = true});
                });
            static_cast<void>(dialog.show_modal());
        });
    add_button(dialog_window_row, "Non-modal window", controls::ButtonType::Info)
        .set_on_click([&feedback_host, host_window, dialog_windows, prune_dialog_windows]() {
            prune_dialog_windows();
            auto dialog = std::make_shared<controls::DialogWindow>();
            dialog->set_title("Non-modal DialogWindow")
                .set_body("This is a standalone Win32 window. Its title lives in the native "
                          "caption bar, while the body and footer stay aligned with the "
                          "Element Plus dialog rhythm.")
                .set_show_cancel_button(true)
                .set_modal(false)
                .set_owner(host_window)
                .set_on_action([&feedback_host](controls::DialogAction action) {
                    controls::Message::show(
                        feedback_host,
                        controls::MessageOptions{
                            .text = "DialogWindow " + dialog_action_label(action),
                            .type = controls::MessageType::Info,
                            .show_close = true});
                });
            dialog->show();
            dialog_windows->push_back(std::move(dialog));
        });
    add_button(dialog_window_row, "Large content", controls::ButtonType::Success)
        .set_on_click([&feedback_host, host_window, dialog_windows, prune_dialog_windows]() {
            prune_dialog_windows();
            auto dialog = std::make_shared<controls::DialogWindow>();
            dialog->set_title("Large content window")
                .set_body(long_feedback_copy("DialogWindow") + " " +
                          long_feedback_copy("DialogWindow"))
                .set_show_cancel_button(true)
                .set_modal(false)
                .set_window_size(640, 320)
                .set_owner(host_window)
                .set_on_action([&feedback_host](controls::DialogAction action) {
                    controls::Message::show(
                        feedback_host,
                        controls::MessageOptions{
                            .text = "DialogWindow " + dialog_action_label(action),
                            .type = controls::MessageType::Success,
                            .show_close = true});
                });
            dialog->show();
            dialog_windows->push_back(std::move(dialog));
        });

    auto& loading_group = add_demo_group(section, "Loading");
    auto& loading_row = loading_group.append_new_child<controls::StackPanel>();
    configure_row(loading_row);
    add_button(loading_row, "Fullscreen loading", controls::ButtonType::Primary)
        .set_on_click([&feedback_host]() {
            controls::Loading::show(
                feedback_host,
                controls::LoadingOptions{.text = "Loading service", .fullscreen = true});
        });
    add_button(loading_row, "Target loading", controls::ButtonType::Default)
        .set_on_click([&feedback_host]() {
            controls::Loading::show(
                feedback_host,
                controls::LoadingOptions{.text = "Target loading", .fullscreen = false});
        });
    add_button(loading_row, "Custom loading", controls::ButtonType::Success)
        .set_on_click([&feedback_host]() {
            controls::Loading::show(
                feedback_host,
                controls::LoadingOptions{.text = "Custom color",
                                         .background = rendering::Color::rgba(240, 249, 235, 224),
                                         .spinner_color = rendering::Color::rgba(103, 194, 58),
                                         .fullscreen = false});
        });
}

void add_animation_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Animations");

    auto& effects = add_demo_group(section, "Live transforms and shadows");
    auto& transform_row = effects.append_new_child<controls::StackPanel>();
    configure_row(transform_row);
    const std::array<std::pair<std::string_view, MotionDemoKind>, 4U> transforms{{
        {"translate", MotionDemoKind::Translate},
        {"scale", MotionDemoKind::Scale},
        {"rotate", MotionDemoKind::Rotate},
        {"skew", MotionDemoKind::Skew},
    }};
    for (const auto& [name, kind] : transforms) {
        auto& card = transform_row.append_new_child<MotionDemoPanel>();
        card.set_motion_kind(kind).set_title(name).set_background(
            rendering::Color::rgba(236, 245, 255));
        card.set_border(rendering::Color::rgba(179, 216, 255), 1.0F);
        card.set_corner_radius(rendering::CornerRadius::uniform(4.0F));
        configure_card(card, 220.0F);
        card.append_new_child<controls::Text>().set_text(name);
    }

    auto& live_controls = effects.append_new_child<controls::StackPanel>();
    configure_row(live_controls);
    add_label(live_controls,
              "Loading controls are demonstrated from the Loading buttons above to avoid an "
              "always-on spinner in the baseline showcase.");
    auto& loading_preview_button = live_controls.append_new_child<controls::Button>();
    loading_preview_button.set_text("Loading")
        .set_type(controls::ButtonType::Primary)
        .set_loading(true)
        .set_on_click([&loading_preview_button]() {
            loading_preview_button.set_loading(!loading_preview_button.loading());
        });
    live_controls.append_new_child<controls::Switch>().set_checked(true);

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
    add_label(easing_group,
              "Read-only probes: step curves are discrete. Step start jumps at the beginning, "
              "step end holds until the last frame.");
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
        auto& card = curve_row.append_new_child<LiveSampleCard>();
        card.set_label(easing_curve_label(curve))
            .set_sample_function([curve](animation::AnimationTimePoint now) {
                const auto phase = loop_progress(now, 0.55F);
                const auto value = std::clamp(animation::apply_easing(curve, phase), 0.0F, 1.0F);
                return LiveSample{
                    .progress = value,
                };
            })
            .set_fill_color(rendering::Color::rgba(64, 158, 255));
        card.configure_layout([](layout::LayoutElement& item) {
            item.set_width(layout::Length::points(240.0F)).set_flex_shrink(0.0F);
        });
    }

    auto& timeline_group = add_demo_group(section, "Timeline, keyframes and physics");
    add_label(timeline_group,
              "These cards are sampling timeline math and physics outputs. They are visual probes, "
              "not interactive inputs.");
    auto& timing_row = timeline_group.append_new_child<controls::StackPanel>();
    configure_row(timing_row);
    for (const auto direction :
         {animation::PlaybackDirection::Normal, animation::PlaybackDirection::Reverse,
          animation::PlaybackDirection::Alternate,
          animation::PlaybackDirection::AlternateReverse}) {
        auto& badge = timing_row.append_new_child<LiveSampleCard>();
        const auto timing =
            animation::AnimationTiming{.duration = animation::AnimationDuration{1.0F},
                                       .iteration_count = 2.0F,
                                       .direction = direction,
                                       .fill_mode = animation::FillMode::Both,
                                       .easing = animation::EasingFunction::ease_out_cubic()};
        const auto timeline = animation::Timeline(timing);
        badge.set_label(playback_direction_label(direction))
            .set_sample_function([timeline](animation::AnimationTimePoint now) {
                const auto sample =
                    timeline.sample(animation::AnimationDuration{loop_progress(now, 0.45F) * 2.0F});
                return LiveSample{
                    .progress = sample.progress,
                };
            })
            .set_fill_color(rendering::Color::rgba(64, 158, 255));
        configure_card(badge, 260.0F);
    }

    auto& keyframe_row = timeline_group.append_new_child<controls::StackPanel>();
    configure_row(keyframe_row);
    auto& keyframe_card = keyframe_row.append_new_child<LiveSampleCard>();
    keyframe_card.set_label("Keyframe sample")
        .set_sample_function([](animation::AnimationTimePoint now) {
            static const auto opacity_track = animation::KeyframeTrack<float>({
                animation::Keyframe<float>{.offset = 0.0F, .value = 0.0F},
                animation::Keyframe<float>{.offset = 0.4F,
                                           .value = 1.0F,
                                           .easing = animation::EasingFunction::ease_out_cubic()},
                animation::Keyframe<float>{.offset = 1.0F,
                                           .value = 0.35F,
                                           .easing =
                                               animation::EasingFunction::ease_in_out_cubic()},
            });
            const auto value = opacity_track.sample(loop_progress(now, 0.4F));
            return LiveSample{
                .progress = value,
            };
        })
        .set_fill_color(rendering::Color::rgba(103, 194, 58));
    configure_card(keyframe_card, 300.0F);

    auto& spring_card = keyframe_row.append_new_child<LiveSampleCard>();
    spring_card.set_label("Spring response")
        .set_sample_function([](animation::AnimationTimePoint now) {
            static const auto spring = animation::SpringSimulation(0.0F, 1.0F);
            const auto value =
                spring.sample(animation::AnimationDuration{loop_progress(now, 0.55F) * 1.1F}).value;
            return LiveSample{
                .progress = std::clamp(value, 0.0F, 1.0F),
            };
        })
        .set_fill_color(rendering::Color::rgba(230, 162, 60));
    configure_card(spring_card, 260.0F);

    auto& friction_card = keyframe_row.append_new_child<LiveSampleCard>();
    friction_card.set_label("Friction decay")
        .set_sample_function([](animation::AnimationTimePoint now) {
            static const auto friction = animation::FrictionSimulation(0.0F, 720.0F);
            const auto value =
                friction.sample(animation::AnimationDuration{loop_progress(now, 0.35F) * 1.4F});
            return LiveSample{
                .progress = std::clamp(value.value / 720.0F, 0.0F, 1.0F),
            };
        })
        .set_fill_color(rendering::Color::rgba(144, 147, 153));
    configure_card(friction_card, 260.0F);

    auto& implicit_demo = section.append_new_child<ImplicitPropertyDemoPanel>();
    implicit_demo.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(420.0F)).set_flex_shrink(0.0F);
    });
    implicit_demo.replay_animation();
}

void add_virtualization_section(controls::StackPanel& root) {
    auto& section = add_section(root, "Virtualization (10 000 items)");

    constexpr float viewport_height = 400.0F;
    constexpr float item_height = 32.0F;
    constexpr std::size_t item_count = 10000;

    auto& row = section.append_new_child<controls::StackPanel>();
    row.set_orientation(controls::Orientation::Horizontal);
    row.set_gap(12.0F);
    row.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_height(layout::Length::points(viewport_height))
            .set_flex_shrink(0.0F);
    });

    auto& viewport = row.append_new_child<SyncedViewportPanel>();
    viewport.set_background(rendering::Color::rgba(255, 255, 255));
    viewport.set_border(rendering::Color::rgba(220, 223, 230), 1.0F);
    viewport.set_corner_radius(rendering::CornerRadius::uniform(4.0F));
    viewport.set_overflow(layout::Overflow::Hidden);
    viewport.set_scroll_wheel_enabled(true);
    viewport.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_height(layout::Length::points(viewport_height))
            .set_flex_grow(1.0F)
            .set_flex_shrink(1.0F);
    });

    auto& content = viewport.append_new_child<controls::StackPanel>();
    content.set_orientation(controls::Orientation::Vertical);
    content.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(0.0F);
    });
    content.set_vertical_virtual_children(
        item_count, item_height,
        [](std::size_t index) -> std::unique_ptr<elements::UIElement> {
            auto slot = std::make_unique<controls::Panel>();
            slot->set_background(index % 2 == 0 ? rendering::Color::rgba(255, 100, 100)
                                                : rendering::Color::rgba(100, 100, 255));
            slot->set_border(rendering::Color::rgba(235, 238, 245), 1.0F);
            slot->configure_layout([](layout::LayoutElement& item) {
                item.set_padding(layout::Edge::Horizontal, layout::Length::points(12.0F))
                    .set_align_items(layout::Align::Center);
            });
            slot->append_new_child<controls::Text>()
                .set_text("Item #" + std::to_string(index))
                .set_type(controls::TextType::Info)
                .set_font_size(13.0F);
            return slot;
        },
        viewport_height);
    viewport.set_on_scroll_changed([](layout::Point) {});

    const auto scrollbar_max =
        static_cast<float>(item_count) * item_height - viewport_height;
    auto& scrollbar = row.append_new_child<controls::Scrollbar>();
    scrollbar.set_orientation(controls::ScrollbarOrientation::Vertical)
        .set_always_visible(true)
        .set_min_thumb_extent(24.0F)
        .set_thickness(8.0F)
        .set_range(0.0F, scrollbar_max, viewport_height)
        .set_on_scroll([&viewport](float value) {
            viewport.set_scroll_offset(layout::Point{0.0F, value});
        });
    scrollbar.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(12.0F))
            .set_height(layout::Length::percent(100.0F))
            .set_flex_shrink(0.0F);
    });

    auto& info = section.append_new_child<controls::Text>();
    info.set_text("Scroll to test UIElement tree virtualization - offscreen children are skipped "
                  "by the core tree")
        .set_type(controls::TextType::Info)
        .set_font_size(12.0F);
}

constexpr auto showcase_content_padding = 24.0F;
constexpr auto showcase_content_gap = 18.0F;
constexpr auto showcase_virtualization_min_overscan = 480.0F;

enum class ShowcaseSectionId {
    Buttons,
    Inputs,
    Choices,
    Structure,
    Images,
    Feedback,
    Animations,
    Virtualization
};

constexpr auto showcase_section_ids =
    std::array{ShowcaseSectionId::Buttons,     ShowcaseSectionId::Inputs,
               ShowcaseSectionId::Choices,     ShowcaseSectionId::Structure,
               ShowcaseSectionId::Images,      ShowcaseSectionId::Feedback,
               ShowcaseSectionId::Animations,  ShowcaseSectionId::Virtualization};
using ShowcaseSectionHeights = std::array<float, showcase_section_ids.size()>;

struct ShowcaseSectionHeightCacheEntry {
    float viewport_width = 0.0F;
    ShowcaseSectionHeights heights{};
};

void configure_showcase_content_box(elements::UIElement& root) {
    root.set_background(rendering::Color::rgba(245, 247, 250));
    root.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_align_self(layout::Align::FlexStart)
            .set_flex_shrink(0.0F)
            .set_padding(layout::Edge::All, layout::Length::points(showcase_content_padding));
    });
}

void configure_showcase_content_stack(controls::StackPanel& root) {
    root.set_gap(showcase_content_gap);
    configure_showcase_content_box(root);
}

controls::Text& append_showcase_title(elements::UIElement& root) {
    auto& title = root.append_new_child<controls::Text>();
    title.set_text("WinElement Controls Showcase")
        .set_size(controls::TextSize::Large)
        .set_type(controls::TextType::Primary);
    title.configure_layout([](layout::LayoutElement& item) { item.set_flex_shrink(0.0F); });
    return title;
}

void add_showcase_section(ShowcaseSectionId section, controls::StackPanel& root,
                          elements::UIElement& feedback_host,
                          platform::Window* host_window = nullptr) {
    switch (section) {
    case ShowcaseSectionId::Buttons:
        add_button_section(root);
        return;
    case ShowcaseSectionId::Inputs:
        add_input_select_section(root);
        return;
    case ShowcaseSectionId::Choices:
        add_choice_scroll_section(root);
        return;
    case ShowcaseSectionId::Structure:
        add_structure_text_path_section(root);
        return;
    case ShowcaseSectionId::Images:
        add_image_section(root);
        return;
    case ShowcaseSectionId::Feedback:
        add_feedback_section(root, feedback_host, host_window);
        return;
    case ShowcaseSectionId::Animations:
        add_animation_section(root);
        return;
    case ShowcaseSectionId::Virtualization:
        add_virtualization_section(root);
        return;
    }
}

[[nodiscard]] float showcase_section_measure_width(float viewport_width) noexcept {
    return std::max(viewport_width - showcase_content_padding * 2.0F, 1.0F);
}

[[nodiscard]] float measure_showcase_section_height_uncached(ShowcaseSectionId section,
                                                             float viewport_width) {
    auto engine = layout::LayoutEngine{};
    auto feedback_host = controls::Panel{};
    auto scratch = controls::StackPanel{};
    scratch.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(0.0F);
    });
    add_showcase_section(section, scratch, feedback_host, nullptr);
    scratch.bind_layout_tree(engine);
    scratch.calculate_layout(
        layout::LayoutConstraints{.width = showcase_section_measure_width(viewport_width)});
    return std::max(scratch.frame().height, 1.0F);
}

void compact_showcase_measurement_heap() noexcept {
#ifdef _WIN32
    static_cast<void>(_heapmin());
#endif
}

[[nodiscard]] ShowcaseSectionHeights measure_showcase_section_heights(float viewport_width) {
    constexpr auto max_cached_widths = 4U;
    static auto cache = std::vector<ShowcaseSectionHeightCacheEntry>{};

    const auto normalized_width = std::max(viewport_width, 1.0F);
    for (const auto& entry : cache) {
        if (std::abs(entry.viewport_width - normalized_width) < 0.5F) {
            return entry.heights;
        }
    }

    auto heights = ShowcaseSectionHeights{};
    for (std::size_t index = 0U; index < showcase_section_ids.size(); ++index) {
        heights[index] =
            measure_showcase_section_height_uncached(showcase_section_ids[index], normalized_width);
    }

    if (cache.size() >= max_cached_widths) {
        cache.erase(cache.begin());
    }
    cache.push_back(
        ShowcaseSectionHeightCacheEntry{.viewport_width = normalized_width, .heights = heights});
    compact_showcase_measurement_heap();
    return heights;
}

[[nodiscard]] std::unique_ptr<controls::StackPanel>
build_showcase_content(elements::UIElement& feedback_host, float viewport_width,
                       platform::Window* host_window = nullptr) {
    auto root = std::make_unique<controls::StackPanel>();
    configure_showcase_content_stack(*root);
    append_showcase_title(*root);
    const auto section_heights =
        measure_showcase_section_heights(std::max(viewport_width, 1.0F));
    for (std::size_t index = 0U; index < showcase_section_ids.size(); ++index) {
        const auto section = showcase_section_ids[index];
        const auto height = section_heights[index];
        auto& slot = root->append_new_child<controls::StackPanel>();
        slot.configure_layout([height](layout::LayoutElement& item) {
            item.set_width(layout::Length::percent(100.0F))
                .set_height(layout::Length::points(height))
                .set_flex_shrink(0.0F);
        });
        slot.set_virtualization_materializer(
            [section, feedback_host_ptr = &feedback_host,
             host_window](const elements::ElementSnapshot&) {
                auto payload = std::make_unique<controls::StackPanel>();
                payload->configure_layout([](layout::LayoutElement& item) {
                    item.set_width(layout::Length::percent(100.0F)).set_flex_shrink(0.0F);
                });
                add_showcase_section(section, *payload, *feedback_host_ptr, host_window);
                return payload;
            });
    }
    return root;
}

[[nodiscard]] float measure_showcase_content_height(float viewport_width) {
    auto engine = layout::LayoutEngine{};
    auto feedback_host = controls::Panel{};
    auto content = build_showcase_content(feedback_host, viewport_width, nullptr);
    content->bind_layout_tree(engine);
    content->calculate_layout(layout::LayoutConstraints{.width = std::max(viewport_width, 1.0F)});
    return std::max(content->frame().height, 0.0F);
}

[[nodiscard]] std::unique_ptr<controls::StackPanel> build_showcase_tree() {
    auto root = std::make_unique<controls::StackPanel>();
    root->configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::percent(100.0F), layout::Length::percent(100.0F))
            .set_padding(layout::Edge::All, layout::Length::points(24.0F));
    });
    root->set_gap(18.0F);
    root->set_background(rendering::Color::rgba(245, 247, 250));
    root->set_overflow(layout::Overflow::Hidden);
    root->set_scroll_wheel_enabled(true);

    auto& title = root->append_new_child<controls::Text>();
    title.set_text("WinElement Controls Showcase")
        .set_size(controls::TextSize::Large)
        .set_type(controls::TextType::Primary);
    title.configure_layout([](layout::LayoutElement& item) { item.set_flex_shrink(0.0F); });

    add_button_section(*root);
    add_input_select_section(*root);
    add_choice_scroll_section(*root);
    add_structure_text_path_section(*root);
    add_image_section(*root);
    add_feedback_section(*root, *root, nullptr);
    add_animation_section(*root);
    return root;
}

[[nodiscard]] ShowcaseWindowTree
build_showcase_window_tree(float viewport_width_hint = showcase_window_viewport_width(),
                           platform::Window* host_window = nullptr) {
    ShowcaseWindowTree tree;

    auto root = std::make_unique<controls::Panel>();
    root->set_background(rendering::Color::rgba(245, 247, 250));
    root->configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::percent(100.0F), layout::Length::percent(100.0F))
            .set_padding(layout::Edge::All, layout::Length::points(0.0F));
    });

    auto& row = root->append_new_child<controls::StackPanel>();
    row.set_orientation(controls::Orientation::Horizontal)
        .set_gap(showcase_page_gap)
        .set_align_items(layout::Align::Stretch);
    row.configure_layout([](layout::LayoutElement& item) {
        item.set_size(layout::Length::percent(100.0F), layout::Length::percent(100.0F));
    });

    auto& viewport = row.append_new_child<SyncedViewportPanel>();
    viewport.set_background(rendering::Color::rgba(245, 247, 250));
    viewport.set_overflow(layout::Overflow::Hidden);
    viewport.set_scroll_wheel_enabled(true);
    viewport.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::percent(100.0F))
            .set_height(layout::Length::percent(100.0F))
            .set_flex_grow(1.0F)
            .set_flex_shrink(1.0F)
            .set_min_width(layout::Length::points(0.0F));
    });
    auto virtual_content = build_showcase_content(*root, viewport_width_hint, host_window);
    auto* virtual_content_ptr = virtual_content.get();
    viewport.append_child(std::move(virtual_content));

    auto& scrollbar = row.append_new_child<controls::Scrollbar>();
    scrollbar.set_orientation(controls::ScrollbarOrientation::Vertical)
        .set_visibility_mode(controls::ScrollbarVisibility::Always)
        .set_value(0.0F)
        .bind_range([&viewport]() {
            return controls::ScrollbarRange{.minimum = 0.0F,
                                            .maximum = viewport.max_scroll_offset().y,
                                            .page_size = viewport.viewport_rect().height,
                                            .value = viewport.scroll_offset().y};
        })
        .set_on_scroll([&viewport](float value) {
            viewport.set_scroll_offset(layout::Point{0.0F, value});
        });
    viewport.set_on_scroll_changed(
        [&scrollbar](layout::Point offset) {
            scrollbar.set_value(offset.y);
        });
    scrollbar.configure_layout([](layout::LayoutElement& item) {
        item.set_width(layout::Length::points(showcase_page_scrollbar_width))
            .set_height(layout::Length::percent(100.0F))
            .set_flex_shrink(0.0F);
    });

    tree.viewport = &viewport;
    tree.scrollbar = &scrollbar;
    tree.virtual_content = virtual_content_ptr;
    tree.root = std::move(root);
    return tree;
}

void sync_showcase_window_scrollbar(ShowcaseWindowTree& tree) {
    if (tree.scrollbar != nullptr) {
        tree.scrollbar->update();
    }
}

void calculate_showcase_window_layout(elements::UIElement& root, ShowcaseWindowTree& tree,
                                      float width, float height) {
    const auto constraints = layout::LayoutConstraints{.width = width, .height = height};
    root.calculate_layout(constraints);
    sync_showcase_window_scrollbar(tree);
}

[[nodiscard]] std::size_t count_elements(const elements::UIElement& element) noexcept {
    auto total = std::size_t{1U};
    for (auto index = std::size_t{0U}; index < element.child_count(); ++index) {
        total += count_elements(element.child_at(index));
    }
    return total;
}

[[nodiscard]] std::size_t count_realized_showcase_sections(
    const elements::UIElement& content) noexcept {
    auto total = std::size_t{0U};
    for (auto index = std::size_t{1U}; index < content.child_count(); ++index) {
        if (!content.child_at(index).subtree_virtualized()) {
            ++total;
        }
    }
    return total;
}

[[nodiscard]] ShowcaseWindowMetrics measure_showcase_window(float width, float height) {
    auto engine = layout::LayoutEngine{};
    auto tree = build_showcase_window_tree(showcase_window_viewport_width(width));
    tree.root->bind_layout_tree(engine);
    calculate_showcase_window_layout(*tree.root, tree, width, height);

    auto metrics = ShowcaseWindowMetrics{};
    metrics.scroll_max = tree.viewport != nullptr ? tree.viewport->max_scroll_offset().y : 0.0F;
    metrics.scrollbar_max = tree.scrollbar != nullptr ? tree.scrollbar->maximum() : 0.0F;
    metrics.content_rect =
        tree.viewport != nullptr ? tree.viewport->scrollable_content_rect() : layout::Rect{};
    metrics.viewport_rect =
        tree.viewport != nullptr ? tree.viewport->viewport_rect() : layout::Rect{};
    metrics.measured_content_height =
        measure_showcase_content_height(std::max(metrics.viewport_rect.width, 1.0F));
    metrics.element_count = tree.root != nullptr ? count_elements(*tree.root) : 0U;
    metrics.realized_section_count =
        tree.virtual_content != nullptr ? count_realized_showcase_sections(*tree.virtual_content)
                                        : 0U;
    return metrics;
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

[[nodiscard]] ShowcaseRenderMetrics
render_metrics_for_scene(const rendering::RenderScene& scene) noexcept {
    const auto* scene_root = scene.root();
    return ShowcaseRenderMetrics{
        .node_count = scene_root != nullptr ? count_nodes(*scene_root) : 0U,
        .command_count = scene_root != nullptr ? count_commands(*scene_root) : 0U,
        .prepared_cache = scene.prepared_cache() != nullptr
                              ? scene.prepared_cache()->stats()
                              : rendering::PreparedRenderCacheStats{}};
}

[[nodiscard]] bool subtree_contains_text(const elements::UIElement& element,
                                         std::string_view text) {
    if (element.text() == text) {
        return true;
    }
    for (std::size_t index = 0; index < element.child_count(); ++index) {
        if (subtree_contains_text(element.child_at(index), text)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] elements::UIElement*
find_virtual_children_host(elements::UIElement& element, std::size_t count) noexcept {
    if (element.virtual_child_count() == count) {
        return &element;
    }
    for (std::size_t index = 0; index < element.child_count(); ++index) {
        if (auto* match = find_virtual_children_host(element.child_at(index), count)) {
            return match;
        }
    }
    return nullptr;
}

[[nodiscard]] bool scroll_virtualization_list_to_bottom(ShowcaseWindowTree& tree) {
    if (tree.root == nullptr) {
        return false;
    }
    auto* host = find_virtual_children_host(*tree.root, 10000U);
    if (host == nullptr || host->parent() == nullptr) {
        return false;
    }
    auto* viewport = host->parent();
    viewport->set_scroll_offset(layout::Point{0.0F, viewport->max_scroll_offset().y});
    return true;
}

[[nodiscard]] double elapsed_ms(std::chrono::steady_clock::time_point start,
                                std::chrono::steady_clock::time_point finish) noexcept {
    return std::chrono::duration<double, std::milli>(finish - start).count();
}

[[nodiscard]] ShowcaseScrollSample sample_showcase_scroll(ShowcaseWindowTree& tree,
                                                          rendering::RenderScene& scene,
                                                          float width, float height,
                                                          float offset,
                                                          bool scroll_virtualization_to_bottom =
                                                              false) {
    if (tree.viewport != nullptr) {
        tree.viewport->set_scroll_offset(layout::Point{0.0F, offset});
    }

    const auto layout_start = std::chrono::steady_clock::now();
    calculate_showcase_window_layout(*tree.root, tree, width, height);
    if (scroll_virtualization_to_bottom && scroll_virtualization_list_to_bottom(tree)) {
        calculate_showcase_window_layout(*tree.root, tree, width, height);
    }
    const auto layout_finish = std::chrono::steady_clock::now();

    auto dirty = rendering::DirtyRegion{};
    const auto commit_start = std::chrono::steady_clock::now();
    tree.root->commit_render_scene(scene, &dirty);
    const auto commit_finish = std::chrono::steady_clock::now();

    return ShowcaseScrollSample{.offset = offset,
                                .layout_ms = elapsed_ms(layout_start, layout_finish),
                                .commit_ms = elapsed_ms(commit_start, commit_finish),
                                .element_count = count_elements(*tree.root),
                                .virtualization_last_item_visible =
                                    subtree_contains_text(*tree.root, "Item #9999"),
                                .render = render_metrics_for_scene(scene)};
}

[[nodiscard]] ShowcaseScrollProfile profile_showcase_window_scroll(float width, float height) {
    auto engine = layout::LayoutEngine{};
    auto tree = build_showcase_window_tree(showcase_window_viewport_width(width));
    tree.root->bind_layout_tree(engine);
    calculate_showcase_window_layout(*tree.root, tree, width, height);

    auto scene = rendering::RenderScene{};
    auto profile = ShowcaseScrollProfile{};
    profile.scroll_max = tree.viewport != nullptr ? tree.viewport->max_scroll_offset().y : 0.0F;
    profile.top = sample_showcase_scroll(tree, scene, width, height, 0.0F);
    profile.middle =
        sample_showcase_scroll(tree, scene, width, height, profile.scroll_max * 0.5F);
    profile.bottom = sample_showcase_scroll(tree, scene, width, height, profile.scroll_max, true);
    return profile;
}

} // namespace

int run_headless_showcase() {
    auto engine = layout::LayoutEngine{};
    auto root = build_showcase_tree();
    root->bind_layout_tree(engine);
    root->calculate_layout(
        layout::LayoutConstraints{.width = canvas_width, .height = canvas_height});

    const auto showcase_metrics =
        measure_showcase_window(showcase_window_width, showcase_window_height);
    constexpr auto wide_showcase_window_width = 1918.0F;
    constexpr auto wide_showcase_window_height = 1034.0F;
    const auto wide_showcase_metrics =
        measure_showcase_window(wide_showcase_window_width, wide_showcase_window_height);
    const auto maximized_scroll_profile =
        profile_showcase_window_scroll(wide_showcase_window_width, wide_showcase_window_height);

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
    std::cout << "  controls: panel border stack text image button input select radio switch "
                 "scrollbar items path context-menu message message-box loading dialog\n";
    std::cout << "  styles: Element Plus semantic variants sizes status borders shadows text "
                 "states dark/custom\n";
    std::cout << "  scroll: vertical and horizontal viewports with clipped overflowing content\n";
    std::cout << "  feedback: message message-box dialog loading triggered from buttons\n";
    std::cout << "  animations: transforms shadows all easing curves timeline keyframes physics "
                 "implicit property\n";
    std::cout << "  render nodes: " << node_count << '\n';
    std::cout << "  render commands: " << command_count << '\n';
    std::cout << "  window ui elements: " << showcase_metrics.element_count << '\n';
    std::cout << "  window realized sections: " << showcase_metrics.realized_section_count << '\n';
    std::cout << "  window scroll max: " << showcase_metrics.scroll_max << '\n';
    std::cout << "  window scrollbar max: " << showcase_metrics.scrollbar_max << '\n';
    std::cout << "  window viewport height: " << showcase_metrics.viewport_rect.height << '\n';
    std::cout << "  window content height: " << showcase_metrics.content_rect.height << '\n';
    std::cout << "  measured content height: " << showcase_metrics.measured_content_height << '\n';
    std::cout << "  wide window scroll max: " << wide_showcase_metrics.scroll_max << '\n';
    std::cout << "  wide window content height: " << wide_showcase_metrics.content_rect.height
              << '\n';
    std::cout << "  wide measured content height: " << wide_showcase_metrics.measured_content_height
              << '\n';
    std::cout << "  maximized scroll bottom: " << maximized_scroll_profile.scroll_max << '\n';
    std::cout << "  maximized top layout ms: " << maximized_scroll_profile.top.layout_ms << '\n';
    std::cout << "  maximized top commit ms: " << maximized_scroll_profile.top.commit_ms << '\n';
    std::cout << "  maximized top ui elements: " << maximized_scroll_profile.top.element_count
              << '\n';
    std::cout << "  maximized top render nodes: "
              << maximized_scroll_profile.top.render.node_count << '\n';
    std::cout << "  maximized top render commands: "
              << maximized_scroll_profile.top.render.command_count << '\n';
    std::cout << "  maximized top prepared glyphs: "
              << maximized_scroll_profile.top.render.prepared_cache.text_glyph_entries << " ("
              << maximized_scroll_profile.top.render.prepared_cache.text_glyph_bytes
              << " bytes)\n";
    std::cout << "  maximized middle layout ms: " << maximized_scroll_profile.middle.layout_ms
              << '\n';
    std::cout << "  maximized middle commit ms: " << maximized_scroll_profile.middle.commit_ms
              << '\n';
    std::cout << "  maximized middle ui elements: "
              << maximized_scroll_profile.middle.element_count << '\n';
    std::cout << "  maximized middle render nodes: "
              << maximized_scroll_profile.middle.render.node_count << '\n';
    std::cout << "  maximized middle render commands: "
              << maximized_scroll_profile.middle.render.command_count << '\n';
    std::cout << "  maximized middle prepared glyphs: "
              << maximized_scroll_profile.middle.render.prepared_cache.text_glyph_entries << " ("
              << maximized_scroll_profile.middle.render.prepared_cache.text_glyph_bytes
              << " bytes)\n";
    std::cout << "  maximized bottom layout ms: " << maximized_scroll_profile.bottom.layout_ms
              << '\n';
    std::cout << "  maximized bottom commit ms: " << maximized_scroll_profile.bottom.commit_ms
              << '\n';
    std::cout << "  maximized bottom ui elements: "
              << maximized_scroll_profile.bottom.element_count << '\n';
    std::cout << "  maximized bottom render nodes: "
              << maximized_scroll_profile.bottom.render.node_count << '\n';
    std::cout << "  maximized bottom render commands: "
              << maximized_scroll_profile.bottom.render.command_count << '\n';
    std::cout << "  maximized bottom Item #9999 visible: "
              << (maximized_scroll_profile.bottom.virtualization_last_item_visible ? "yes"
                                                                                   : "no")
              << '\n';
    std::cout << "  maximized bottom prepared glyphs: "
              << maximized_scroll_profile.bottom.render.prepared_cache.text_glyph_entries << " ("
              << maximized_scroll_profile.bottom.render.prepared_cache.text_glyph_bytes
              << " bytes)\n";
    std::cout << "  animation active during warmup: " << (animation_active ? "yes" : "no") << '\n';
    std::cout << "  dirty empty: " << (dirty.empty() ? "yes" : "no") << '\n';
    const auto content_height_matches_width =
        std::abs(showcase_metrics.content_rect.height - showcase_metrics.measured_content_height) <
            1.0F &&
        std::abs(wide_showcase_metrics.content_rect.height -
                 wide_showcase_metrics.measured_content_height) < 1.0F;
    return command_count > 0U && showcase_metrics.scroll_max > 0.0F &&
                   showcase_metrics.scrollbar_max > 0.0F &&
                   wide_showcase_metrics.scroll_max > 0.0F && content_height_matches_width &&
                   maximized_scroll_profile.scroll_max > 0.0F &&
                   maximized_scroll_profile.bottom.render.command_count > 0U &&
                   maximized_scroll_profile.bottom.virtualization_last_item_visible
               ? 0
               : 1;
}

int run_window_showcase() {
    platform::Application application;
    platform::Window window(platform::WindowOptions{
        .title = L"WinElement Controls Showcase", .width = 1320, .height = 920});
    upload_showcase_resources(window);
    auto tree = build_showcase_window_tree(showcase_window_viewport_width(), &window);
    window.set_content(std::move(tree.root));
    if (auto* content = window.content()) {
        calculate_showcase_window_layout(*content, tree, showcase_window_width,
                                         showcase_window_height);
    }
    window.show();
    return application.run();
}

int run_profiled_showcase_window(std::string_view label, bool scroll_to_bottom) {
    constexpr auto profiled_window_width = 1918;
    constexpr auto profiled_window_height = 1034;
    constexpr auto profiled_layout_width = static_cast<float>(profiled_window_width);
    constexpr auto profiled_layout_height = static_cast<float>(profiled_window_height);

    platform::Application application;
    platform::Window window(platform::WindowOptions{.title = L"WinElement Controls Showcase",
                                                    .width = profiled_window_width,
                                                    .height = profiled_window_height});
    upload_showcase_resources(window);

    auto label_text = std::string{label};
    print_process_memory(label_text + " after window");

    auto tree = build_showcase_window_tree(showcase_window_viewport_width(profiled_layout_width));
    window.set_content(std::move(tree.root));
    if (auto* content = window.content()) {
        calculate_showcase_window_layout(*content, tree, profiled_layout_width,
                                         profiled_layout_height);
    }

    if (scroll_to_bottom && tree.viewport != nullptr) {
        tree.viewport->set_scroll_offset(layout::Point{0.0F, tree.viewport->max_scroll_offset().y});
        if (auto* content = window.content()) {
            calculate_showcase_window_layout(*content, tree, profiled_layout_width,
                                             profiled_layout_height);
        }
    }
    print_process_memory(label_text + " after content");

    std::thread sampler([&window, label_text]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2600));
        print_process_memory(label_text + " after render");
        trim_process_working_set();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        print_process_memory(label_text + " after forced working-set trim");
        window.close();
    });

    window.show();
    const auto result = application.run();
    sampler.join();
    print_process_memory(label_text + " after close");
    return result;
}

int run_profile_memory_showcase() {
#ifdef _WIN32
    std::cout << "showcase profile pid: " << GetCurrentProcessId() << "\n";
#endif
    print_process_memory("startup");
    if (const auto result = run_profiled_showcase_window("maximized top", false); result != 0) {
        return result;
    }
    print_process_memory("between windows");
    return run_profiled_showcase_window("maximized bottom", true);
}

#ifndef WINELEMENT_CONTROLS_SHOWCASE_AS_LIBRARY
int main(int argc, char** argv) {
    for (auto index = 1; index < argc; ++index) {
        if (std::string_view{argv[index]} == "--headless") {
            return run_headless_showcase();
        }
        if (std::string_view{argv[index]} == "--profile-memory") {
            return run_profile_memory_showcase();
        }
    }
    return run_window_showcase();
}
#endif
