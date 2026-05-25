#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace winelement::core {

enum class FrameTaskPriority : std::uint8_t {
    Immediate = 0U,
    UserInput = 1U,
    Animation = 2U,
    Layout = 3U,
    Render = 4U,
    Idle = 5U,
};

struct FrameBudget {
    std::size_t max_tasks = 0U;
    std::chrono::duration<float> max_duration{0.0F};
};

struct FrameSchedulerStats {
    std::size_t executed_task_count = 0U;
    std::size_t remaining_task_count = 0U;
    bool budget_exhausted = false;
};

class FrameScheduler final {
  public:
    using Task = std::function<void()>;
    using TaskId = std::uint64_t;

    [[nodiscard]] TaskId post(Task task, FrameTaskPriority priority = FrameTaskPriority::Render,
                              std::string coalesce_key = {});
    [[nodiscard]] bool cancel(TaskId id) noexcept;
    void clear() noexcept;

    [[nodiscard]] FrameSchedulerStats drain(FrameBudget budget = {});
    [[nodiscard]] std::size_t pending_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

  private:
    struct PendingTask {
        TaskId id = 0U;
        std::uint64_t sequence = 0U;
        FrameTaskPriority priority = FrameTaskPriority::Render;
        std::string coalesce_key;
        Task task;
    };

    [[nodiscard]] bool cancel_active_task(TaskId id) noexcept;
    [[nodiscard]] bool pop_next_task(PendingTask& task) noexcept;

    static constexpr std::size_t priority_bucket_count =
        static_cast<std::size_t>(FrameTaskPriority::Idle) + 1U;

    std::array<std::deque<PendingTask>, priority_bucket_count> tasks_;
    std::unordered_set<TaskId> active_ids_;
    std::unordered_set<TaskId> canceled_ids_;
    std::unordered_map<std::string, TaskId> coalesced_tasks_;
    std::unordered_map<TaskId, std::string> task_keys_;
    mutable std::mutex mutex_;
    TaskId next_id_ = 1U;
    std::uint64_t next_sequence_ = 1U;
};

} // namespace winelement::core
