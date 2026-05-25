#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace winelement::platform {

struct RenderThreadLease {
    std::uint64_t window_id = 0U;
    std::size_t thread_index = 0U;
};

class RenderThreadPoolService final {
  public:
    using WorkItem = std::function<void()>;

    explicit RenderThreadPoolService(std::size_t worker_count = 0U);
    ~RenderThreadPoolService();

    RenderThreadPoolService(const RenderThreadPoolService&) = delete;
    RenderThreadPoolService& operator=(const RenderThreadPoolService&) = delete;
    RenderThreadPoolService(RenderThreadPoolService&&) noexcept;
    RenderThreadPoolService& operator=(RenderThreadPoolService&&) noexcept;

    [[nodiscard]] static RenderThreadPoolService& shared();

    [[nodiscard]] std::future<void> submit(WorkItem work);
    void wait_idle();

    template <typename Callback>
    void parallel_for(std::size_t item_count, std::size_t min_chunk_size, Callback&& callback) {
        if (item_count == 0U) {
            return;
        }

        const auto workers = worker_count();
        if (workers <= 1U || running_on_pool_thread() ||
            item_count <= std::max<std::size_t>(min_chunk_size, 1U)) {
            std::forward<Callback>(callback)(0U, item_count);
            return;
        }

        const auto chunk_size = std::max<std::size_t>(std::max<std::size_t>(min_chunk_size, 1U),
                                                      (item_count + workers - 1U) / workers);
        auto futures = std::vector<std::future<void>>{};
        futures.reserve((item_count + chunk_size - 1U) / chunk_size);
        auto shared_callback =
            std::make_shared<std::decay_t<Callback>>(std::forward<Callback>(callback));

        for (std::size_t first = 0U; first < item_count; first += chunk_size) {
            const auto last = std::min(item_count, first + chunk_size);
            futures.push_back(
                submit([shared_callback, first, last]() { (*shared_callback)(first, last); }));
        }

        for (auto& future : futures) {
            future.get();
        }
    }

    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] std::size_t queued_job_count() const noexcept;
    [[nodiscard]] bool running_on_pool_thread() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] RenderThreadPoolService& shared_render_thread_pool();

class RenderThreadPoolPlanner final {
  public:
    explicit RenderThreadPoolPlanner(std::size_t max_threads = 1U) noexcept
        : max_threads_(std::max<std::size_t>(max_threads, 1U)) {}

    [[nodiscard]] RenderThreadLease lease(std::uint64_t window_id) {
        const std::scoped_lock lock(mutex_);
        const auto thread_index = next_thread_index_++ % max_threads_;
        leases_.push_back(RenderThreadLease{.window_id = window_id, .thread_index = thread_index});
        return leases_.back();
    }

    void release(std::uint64_t window_id) noexcept {
        const std::scoped_lock lock(mutex_);
        leases_.erase(
            std::remove_if(leases_.begin(), leases_.end(),
                           [window_id](auto lease) { return lease.window_id == window_id; }),
            leases_.end());
    }

    [[nodiscard]] std::size_t max_threads() const noexcept {
        return max_threads_;
    }

    [[nodiscard]] std::size_t active_window_count() const noexcept {
        const std::scoped_lock lock(mutex_);
        return leases_.size();
    }

  private:
    std::size_t max_threads_ = 1U;
    mutable std::mutex mutex_;
    std::size_t next_thread_index_ = 0U;
    std::vector<RenderThreadLease> leases_;
};

} // namespace winelement::platform
