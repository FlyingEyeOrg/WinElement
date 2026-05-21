#include "window_render_worker.hpp"

#include "d3d11_composition_surface.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace winelement::platform::win32 {
namespace {

constexpr auto idle_memory_trim_delay = std::chrono::milliseconds(3000);

class ResourceUploadReplayCache final {
  public:
    void remember(const rendering::RenderResourceUpload& upload) {
        const auto id = upload.id.value;
        const auto existing = indices_.find(id);

        if (upload.action == rendering::RenderResourceAction::Discard) {
            erase(existing);
            return;
        }

        if (upload.action == rendering::RenderResourceAction::Retain) {
            if (existing != indices_.end()) {
                uploads_[existing->second].reference_count += upload.reference_count;
            }
            return;
        }

        if (upload.action == rendering::RenderResourceAction::Release) {
            if (existing == indices_.end()) {
                return;
            }
            auto& cached = uploads_[existing->second];
            if (cached.reference_count <= upload.reference_count) {
                erase(existing);
                return;
            }
            cached.reference_count -= upload.reference_count;
            return;
        }

        if (existing == indices_.end()) {
            indices_.emplace(id, uploads_.size());
            uploads_.push_back(upload);
            return;
        }

        uploads_[existing->second] = upload;
    }

    [[nodiscard]] const std::vector<rendering::RenderResourceUpload>& uploads() const noexcept {
        return uploads_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return uploads_.size();
    }

  private:
    using IndexIterator = std::unordered_map<std::uint64_t, std::size_t>::iterator;

    void erase(IndexIterator iterator) noexcept {
        if (iterator == indices_.end()) {
            return;
        }

        const auto index = iterator->second;
        const auto last_index = uploads_.size() - 1U;
        if (index != last_index) {
            uploads_[index] = std::move(uploads_[last_index]);
            indices_[uploads_[index].id.value] = index;
        }
        uploads_.pop_back();
        indices_.erase(iterator);
    }

    std::vector<rendering::RenderResourceUpload> uploads_;
    std::unordered_map<std::uint64_t, std::size_t> indices_;
};

[[nodiscard]] bool replay_cached_resource_uploads(D3D11CompositionSurface& surface,
                                                  const ResourceUploadReplayCache& cache) noexcept {
    const auto max_replay_passes = std::max<std::size_t>(16U, cache.size() * 2U + 1U);
    for (std::size_t pass = 0; pass < max_replay_passes; ++pass) {
        static_cast<void>(surface.consume_device_recreated());
        for (const auto& upload : cache.uploads()) {
            surface.upload_resource(upload);
            if (surface.consume_device_recreated()) {
                break;
            }
        }
        if (!surface.consume_device_recreated()) {
            return true;
        }
    }
    return false;
}

} // namespace

WindowRenderWorker::WindowRenderWorker(HWND hwnd, bool trim_memory_on_idle)
    : hwnd_(hwnd), trim_memory_on_idle_(trim_memory_on_idle) {
    worker_ = std::thread([this]() noexcept { run(); });
}

WindowRenderWorker::~WindowRenderWorker() {
    stop();
}

struct WindowRenderWorker::RenderJobCompletion final {
    std::mutex mutex;
    std::condition_variable wake;
    RenderJobResult result = RenderJobResult::Canceled;
    bool done = false;
};

void WindowRenderWorker::set_dpi(float dpi) noexcept {
    static_cast<void>(post(Job{.kind = JobKind::SetDpi, .dpi = dpi}));
}

void WindowRenderWorker::resize() noexcept {
    static_cast<void>(post(Job{.kind = JobKind::Resize}));
}

void WindowRenderWorker::discard() noexcept {
    static_cast<void>(post(Job{.kind = JobKind::Discard}));
}

void WindowRenderWorker::upload_resource(rendering::RenderResourceUpload upload) noexcept {
    {
        const std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }

        if (pending_upload_job_.has_value()) {
            (*pending_upload_job_)->resource_uploads.push_back(std::move(upload));
            return;
        }

        Job job;
        job.kind = JobKind::UploadResource;
        job.resource_uploads.push_back(std::move(upload));
        jobs_.push_back(std::move(job));
        pending_upload_job_ = std::prev(jobs_.end());
    }
    wake_.notify_one();
}

void WindowRenderWorker::render(RenderFrame frame) noexcept {
    static_cast<void>(post(Job{.kind = JobKind::Render, .frame = std::move(frame)}));
}

RenderJobResult WindowRenderWorker::render_sync(RenderFrame frame) noexcept {
    if (worker_.get_id() == std::this_thread::get_id()) {
        render(std::move(frame));
        return RenderJobResult::Canceled;
    }

    try {
        auto completion = std::make_shared<RenderJobCompletion>();
        if (!post(Job{
                .kind = JobKind::Render, .frame = std::move(frame), .completion = completion})) {
            return RenderJobResult::Failed;
        }

        std::unique_lock lock(completion->mutex);
        completion->wake.wait(lock, [&completion]() noexcept { return completion->done; });
        return completion->result;
    } catch (...) {
        return RenderJobResult::Failed;
    }
}

bool WindowRenderWorker::post(Job job) noexcept {
    try {
        {
            const std::scoped_lock lock(mutex_);
            if (stopping_) {
                return false;
            }

            if (job.kind == JobKind::Render && pending_render_job_.has_value()) {
                auto& pending = **pending_render_job_;
                job.frame.dirty_region.add(pending.frame.dirty_region);
                complete_job(pending, RenderJobResult::Canceled);
                jobs_.erase(*pending_render_job_);
                pending_render_job_.reset();
            }

            if (job.kind == JobKind::Resize && pending_resize_job_.has_value()) {
                jobs_.erase(*pending_resize_job_);
                pending_resize_job_.reset();
            }

            if (job.kind == JobKind::SetDpi && pending_set_dpi_job_.has_value()) {
                jobs_.erase(*pending_set_dpi_job_);
                pending_set_dpi_job_.reset();
            }

            jobs_.push_back(std::move(job));
            auto inserted = std::prev(jobs_.end());
            switch (inserted->kind) {
            case JobKind::Render:
                pending_render_job_ = inserted;
                break;
            case JobKind::Resize:
                pending_resize_job_ = inserted;
                break;
            case JobKind::SetDpi:
                pending_set_dpi_job_ = inserted;
                break;
            case JobKind::UploadResource:
                pending_upload_job_ = inserted;
                break;
            case JobKind::Discard:
            case JobKind::Stop:
                break;
            }
        }
        wake_.notify_one();
        return true;
    } catch (...) {
        return false;
    }
}

void WindowRenderWorker::complete_job(Job& job, RenderJobResult result) noexcept {
    if (job.completion == nullptr) {
        return;
    }

    {
        const std::scoped_lock lock(job.completion->mutex);
        if (job.completion->done) {
            return;
        }
        job.completion->result = result;
        job.completion->done = true;
    }
    job.completion->wake.notify_all();
}

void WindowRenderWorker::clear_pending_iterator(JobIterator iterator) noexcept {
    if (pending_render_job_.has_value() && *pending_render_job_ == iterator) {
        pending_render_job_.reset();
    }
    if (pending_resize_job_.has_value() && *pending_resize_job_ == iterator) {
        pending_resize_job_.reset();
    }
    if (pending_set_dpi_job_.has_value() && *pending_set_dpi_job_ == iterator) {
        pending_set_dpi_job_.reset();
    }
    if (pending_upload_job_.has_value() && *pending_upload_job_ == iterator) {
        pending_upload_job_.reset();
    }
}

void WindowRenderWorker::request_repaint() const noexcept {
    const auto hwnd = hwnd_.load(std::memory_order_acquire);
    if (hwnd != nullptr && IsWindow(hwnd) != FALSE) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void WindowRenderWorker::stop() noexcept {
    {
        const std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        for (auto& job : jobs_) {
            complete_job(job, RenderJobResult::Canceled);
        }
        jobs_.clear();
        pending_render_job_.reset();
        pending_resize_job_.reset();
        pending_set_dpi_job_.reset();
        pending_upload_job_.reset();
    }
    wake_.notify_one();

    if (worker_.joinable()) {
        worker_.join();
    }
    hwnd_.store(nullptr, std::memory_order_release);
}

void WindowRenderWorker::run() noexcept {
    std::unique_ptr<D3D11CompositionSurface> surface;
    ResourceUploadReplayCache cached_uploads;
    auto idle_trim_pending = false;

    for (;;) {
        Job job;
        auto trim_idle_resources = false;
        {
            std::unique_lock lock(mutex_);
            for (;;) {
                if (stopping_ || !jobs_.empty()) {
                    break;
                }
                if (trim_memory_on_idle_ && idle_trim_pending && surface != nullptr) {
                    if (wake_.wait_for(lock, idle_memory_trim_delay,
                                       [this]() noexcept { return stopping_ || !jobs_.empty(); })) {
                        continue;
                    }
                    idle_trim_pending = false;
                    trim_idle_resources = true;
                    break;
                }
                wake_.wait(lock, [this]() noexcept { return stopping_ || !jobs_.empty(); });
            }

            if (!trim_idle_resources) {
                if (jobs_.empty() && stopping_) {
                    return;
                }
                auto iterator = jobs_.begin();
                job = std::move(*iterator);
                clear_pending_iterator(iterator);
                jobs_.erase(iterator);
            }
        }

        if (trim_idle_resources) {
            try {
                surface->trim_idle_resources();
            } catch (...) {
            }
            continue;
        }

        if (job.kind == JobKind::Stop) {
            return;
        }

        auto job_result = RenderJobResult::Completed;
        try {
            if (surface == nullptr) {
                const auto hwnd = hwnd_.load(std::memory_order_acquire);
                if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
                    complete_job(job, RenderJobResult::Failed);
                    return;
                }
                surface = std::make_unique<D3D11CompositionSurface>(hwnd);
            }

            switch (job.kind) {
            case JobKind::SetDpi:
                surface->set_dpi(job.dpi);
                break;
            case JobKind::Resize:
                surface->invalidate_surface_size();
                break;
            case JobKind::Discard:
                surface->discard();
                if (!replay_cached_resource_uploads(*surface, cached_uploads)) {
                    job_result = RenderJobResult::Failed;
                    request_repaint();
                }
                break;
            case JobKind::UploadResource:
                for (auto& resource_upload : job.resource_uploads) {
                    cached_uploads.remember(resource_upload);
                    surface->upload_resource(std::move(resource_upload));
                }
                if (surface->consume_device_recreated()) {
                    if (!replay_cached_resource_uploads(*surface, cached_uploads)) {
                        job_result = RenderJobResult::Failed;
                        request_repaint();
                    }
                }
                break;
            case JobKind::Render: {
                auto render_result = surface->render(
                    job.frame.clear_color, job.frame.dirty_region, job.frame.target_pixel_width,
                    job.frame.target_pixel_height, job.frame.render_scene.get(),
                    job.frame.promotion_plan, &job.frame.frame_graph);
                if (surface->consume_device_recreated()) {
                    if (replay_cached_resource_uploads(*surface, cached_uploads)) {
                        render_result = surface->render(
                            job.frame.clear_color, job.frame.dirty_region,
                            job.frame.target_pixel_width, job.frame.target_pixel_height,
                            job.frame.render_scene.get(), job.frame.promotion_plan,
                            &job.frame.frame_graph);
                    } else {
                        render_result = D3D11CompositionSurface::RenderResult::DeviceLost;
                    }
                }
                if (render_result == D3D11CompositionSurface::RenderResult::StaleTargetSize) {
                    request_repaint();
                    job_result = RenderJobResult::StaleTargetSize;
                } else if (render_result == D3D11CompositionSurface::RenderResult::DeviceLost) {
                    job_result = RenderJobResult::Failed;
                }
                break;
            }
            case JobKind::Stop:
                return;
            }
        } catch (...) {
            if (surface != nullptr) {
                surface->discard();
            }
            job_result = RenderJobResult::Failed;
        }
        complete_job(job, job_result);
        if (job_result == RenderJobResult::Failed) {
            request_repaint();
        }
        if (trim_memory_on_idle_ && surface != nullptr) {
            idle_trim_pending = true;
        }
    }
}

} // namespace winelement::platform::win32
