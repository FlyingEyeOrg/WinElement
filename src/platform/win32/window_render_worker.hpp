#pragma once

#include <winelement/rendering/compositor.hpp>
#include <winelement/rendering/render_frame_graph.hpp>
#include <winelement/rendering/render_resource_queue.hpp>
#include <winelement/rendering/render_scene.hpp>
#include <winelement/rendering/render_types.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace winelement::platform::win32 {

struct RenderFrame {
    rendering::Color clear_color{};
    rendering::DirtyRegion dirty_region{};
    std::shared_ptr<const rendering::RenderScene> render_scene{};
    rendering::CompositorPromotionPlan promotion_plan{};
    rendering::RenderFrameGraph frame_graph{};
    std::uint32_t target_pixel_width = 1U;
    std::uint32_t target_pixel_height = 1U;
};

enum class RenderJobResult { Completed, StaleTargetSize, Canceled, Failed };

class WindowRenderWorker final {
  public:
    explicit WindowRenderWorker(HWND hwnd);
    ~WindowRenderWorker();

    WindowRenderWorker(const WindowRenderWorker&) = delete;
    WindowRenderWorker& operator=(const WindowRenderWorker&) = delete;
    WindowRenderWorker(WindowRenderWorker&&) = delete;
    WindowRenderWorker& operator=(WindowRenderWorker&&) = delete;

    void set_dpi(float dpi) noexcept;
    void resize() noexcept;
    void discard() noexcept;
    void upload_resource(rendering::RenderResourceUpload upload) noexcept;
    void render(RenderFrame frame) noexcept;
    [[nodiscard]] RenderJobResult render_sync(RenderFrame frame) noexcept;

  private:
    enum class JobKind { SetDpi, Resize, Discard, UploadResource, Render, Stop };

    struct RenderJobCompletion;

    struct Job {
        JobKind kind = JobKind::Stop;
        float dpi = 96.0F;
        std::vector<rendering::RenderResourceUpload> resource_uploads{};
        RenderFrame frame{};
        std::shared_ptr<RenderJobCompletion> completion{};
    };

    using JobQueue = std::list<Job>;
    using JobIterator = JobQueue::iterator;

    [[nodiscard]] bool post(Job job) noexcept;
    void complete_job(Job& job, RenderJobResult result) noexcept;
    void clear_pending_iterator(JobIterator iterator) noexcept;
    void request_repaint() const noexcept;
    void stop() noexcept;
    void run() noexcept;

    std::atomic<HWND> hwnd_ = nullptr;
    std::mutex mutex_;
    std::condition_variable wake_;
    JobQueue jobs_;
    std::optional<JobIterator> pending_render_job_;
    std::optional<JobIterator> pending_resize_job_;
    std::optional<JobIterator> pending_set_dpi_job_;
    std::optional<JobIterator> pending_upload_job_;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace winelement::platform::win32