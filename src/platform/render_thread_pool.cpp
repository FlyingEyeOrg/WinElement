#include <winelement/platform/render_thread_pool.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>

namespace winelement::platform {
namespace {

[[nodiscard]] std::size_t default_worker_count(std::size_t requested) noexcept {
    if (requested > 0U) {
        return requested;
    }

    const auto hardware = std::thread::hardware_concurrency();
    if (hardware <= 1U) {
        return 1U;
    }
    return std::max<std::size_t>(hardware - 1U, 1U);
}

} // namespace

class RenderThreadPoolService::Impl final {
  public:
    explicit Impl(std::size_t requested_worker_count)
        : queues_(default_worker_count(requested_worker_count)) {
        for (auto& queue : queues_) {
            queue = std::make_unique<WorkerQueue>();
        }

        threads_.reserve(queues_.size());
        for (std::size_t index = 0U; index < queues_.size(); ++index) {
            threads_.emplace_back([this, index]() noexcept { worker_loop(index); });
        }
    }

    ~Impl() {
        stop();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    [[nodiscard]] std::future<void> submit(WorkItem work) {
        if (!work) {
            std::promise<void> promise;
            promise.set_exception(std::make_exception_ptr(
                std::invalid_argument("render thread pool job must not be empty")));
            return promise.get_future();
        }

        auto task = std::make_shared<Task>();
        task->work = std::move(work);
        auto future = task->completion.get_future();

        const auto queue_index = select_queue();
        {
            const std::scoped_lock lock(queues_[queue_index]->mutex);
            if (stopping_.load(std::memory_order_acquire)) {
                task->completion.set_exception(std::make_exception_ptr(
                    std::runtime_error("render thread pool is stopping")));
                return future;
            }
            queues_[queue_index]->tasks.push_back(std::move(task));
            queued_jobs_.fetch_add(1U, std::memory_order_release);
        }
        wake_.notify_one();
        return future;
    }

    void wait_idle() {
        std::unique_lock lock(idle_mutex_);
        idle_.wait(lock, [this]() noexcept {
            return queued_jobs_.load(std::memory_order_acquire) == 0U &&
                   active_jobs_.load(std::memory_order_acquire) == 0U;
        });
    }

    [[nodiscard]] std::size_t worker_count() const noexcept {
        return queues_.size();
    }

    [[nodiscard]] std::size_t queued_job_count() const noexcept {
        return queued_jobs_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool running_on_pool_thread() const noexcept {
        return current_pool_ == this;
    }

  private:
    struct Task {
        WorkItem work;
        std::promise<void> completion;
    };

    struct WorkerQueue {
        std::mutex mutex;
        std::deque<std::shared_ptr<Task>> tasks;
    };

    [[nodiscard]] std::size_t select_queue() noexcept {
        if (current_pool_ == this && current_worker_index_ < queues_.size()) {
            return current_worker_index_;
        }
        return next_queue_.fetch_add(1U, std::memory_order_relaxed) % queues_.size();
    }

    [[nodiscard]] std::shared_ptr<Task> pop_local(std::size_t worker_index) noexcept {
        auto& queue = *queues_[worker_index];
        const std::scoped_lock lock(queue.mutex);
        if (queue.tasks.empty()) {
            return nullptr;
        }
        auto task = std::move(queue.tasks.back());
        queue.tasks.pop_back();
        active_jobs_.fetch_add(1U, std::memory_order_acq_rel);
        queued_jobs_.fetch_sub(1U, std::memory_order_acq_rel);
        return task;
    }

    [[nodiscard]] std::shared_ptr<Task> steal_from_other(std::size_t worker_index) noexcept {
        for (std::size_t offset = 1U; offset < queues_.size(); ++offset) {
            const auto victim_index = (worker_index + offset) % queues_.size();
            auto& victim = *queues_[victim_index];
            const std::scoped_lock lock(victim.mutex);
            if (victim.tasks.empty()) {
                continue;
            }
            auto task = std::move(victim.tasks.front());
            victim.tasks.pop_front();
            active_jobs_.fetch_add(1U, std::memory_order_acq_rel);
            queued_jobs_.fetch_sub(1U, std::memory_order_acq_rel);
            return task;
        }
        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<Task> take_task(std::size_t worker_index) noexcept {
        if (auto local = pop_local(worker_index); local != nullptr) {
            return local;
        }
        return steal_from_other(worker_index);
    }

    void worker_loop(std::size_t worker_index) noexcept {
        current_pool_ = this;
        current_worker_index_ = worker_index;

        for (;;) {
            auto task = take_task(worker_index);
            if (task == nullptr) {
                std::unique_lock lock(wake_mutex_);
                wake_.wait(lock, [this]() noexcept {
                    return stopping_.load(std::memory_order_acquire) ||
                           queued_jobs_.load(std::memory_order_acquire) > 0U;
                });
                if (stopping_.load(std::memory_order_acquire) &&
                    queued_jobs_.load(std::memory_order_acquire) == 0U) {
                    break;
                }
                continue;
            }

            try {
                task->work();
                task->completion.set_value();
            } catch (...) {
                task->completion.set_exception(std::current_exception());
            }
            active_jobs_.fetch_sub(1U, std::memory_order_acq_rel);
            notify_idle_if_needed();
        }

        current_pool_ = nullptr;
        current_worker_index_ = invalid_worker_index;
    }

    void notify_idle_if_needed() noexcept {
        if (queued_jobs_.load(std::memory_order_acquire) == 0U &&
            active_jobs_.load(std::memory_order_acquire) == 0U) {
            const std::scoped_lock lock(idle_mutex_);
            idle_.notify_all();
        }
    }

    void stop() noexcept {
        const auto already_stopping = stopping_.exchange(true, std::memory_order_acq_rel);
        if (already_stopping) {
            return;
        }
        wake_.notify_all();
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        notify_idle_if_needed();
    }

    static constexpr auto invalid_worker_index = static_cast<std::size_t>(-1);
    static thread_local Impl* current_pool_;
    static thread_local std::size_t current_worker_index_;

    std::vector<std::unique_ptr<WorkerQueue>> queues_;
    std::vector<std::thread> threads_;
    std::condition_variable wake_;
    std::mutex wake_mutex_;
    std::condition_variable idle_;
    std::mutex idle_mutex_;
    std::atomic<std::size_t> queued_jobs_{0U};
    std::atomic<std::size_t> active_jobs_{0U};
    std::atomic<std::size_t> next_queue_{0U};
    std::atomic<bool> stopping_{false};
};

thread_local RenderThreadPoolService::Impl* RenderThreadPoolService::Impl::current_pool_ = nullptr;
thread_local std::size_t RenderThreadPoolService::Impl::current_worker_index_ =
    RenderThreadPoolService::Impl::invalid_worker_index;

RenderThreadPoolService::RenderThreadPoolService(std::size_t worker_count)
    : impl_(std::make_unique<Impl>(worker_count)) {}

RenderThreadPoolService::~RenderThreadPoolService() = default;

RenderThreadPoolService::RenderThreadPoolService(RenderThreadPoolService&&) noexcept = default;

RenderThreadPoolService&
RenderThreadPoolService::operator=(RenderThreadPoolService&&) noexcept = default;

RenderThreadPoolService& RenderThreadPoolService::shared() {
    static RenderThreadPoolService service;
    return service;
}

RenderThreadPoolService& shared_render_thread_pool() {
    return RenderThreadPoolService::shared();
}

std::future<void> RenderThreadPoolService::submit(WorkItem work) {
    return impl_->submit(std::move(work));
}

void RenderThreadPoolService::wait_idle() {
    impl_->wait_idle();
}

std::size_t RenderThreadPoolService::worker_count() const noexcept {
    return impl_->worker_count();
}

std::size_t RenderThreadPoolService::queued_job_count() const noexcept {
    return impl_->queued_job_count();
}

bool RenderThreadPoolService::running_on_pool_thread() const noexcept {
    return impl_->running_on_pool_thread();
}

} // namespace winelement::platform