#include <winelement/core/frame_scheduler.hpp>

#include <utility>

namespace winelement::core {

FrameScheduler::TaskId FrameScheduler::post(Task task, FrameTaskPriority priority,
                                            std::string coalesce_key) {
    if (!task) {
        return 0U;
    }

    const std::scoped_lock lock(mutex_);
    if (!coalesce_key.empty()) {
        if (const auto iterator = coalesced_tasks_.find(coalesce_key);
            iterator != coalesced_tasks_.end()) {
            static_cast<void>(cancel_active_task(iterator->second));
        }
    }

    const auto id = next_id_++;
    active_ids_.insert(id);
    if (!coalesce_key.empty()) {
        coalesced_tasks_[coalesce_key] = id;
        task_keys_[id] = coalesce_key;
    }
    tasks_[static_cast<std::size_t>(priority)].push_back(
        PendingTask{.id = id,
                    .sequence = next_sequence_++,
                    .priority = priority,
                    .coalesce_key = std::move(coalesce_key),
                    .task = std::move(task)});
    return id;
}

bool FrameScheduler::cancel(TaskId id) noexcept {
    const std::scoped_lock lock(mutex_);
    return cancel_active_task(id);
}

void FrameScheduler::clear() noexcept {
    const std::scoped_lock lock(mutex_);
    for (auto& bucket : tasks_) {
        bucket.clear();
    }
    active_ids_.clear();
    canceled_ids_.clear();
    coalesced_tasks_.clear();
    task_keys_.clear();
}

FrameSchedulerStats FrameScheduler::drain(FrameBudget budget) {
    FrameSchedulerStats stats;
    const auto started_at = std::chrono::steady_clock::now();

    for (;;) {
        if (budget.max_tasks > 0U && stats.executed_task_count >= budget.max_tasks) {
            stats.budget_exhausted = true;
            break;
        }
        if (budget.max_duration.count() > 0.0F && stats.executed_task_count > 0U &&
            std::chrono::steady_clock::now() - started_at >= budget.max_duration) {
            stats.budget_exhausted = true;
            break;
        }

        PendingTask pending;
        {
            const std::scoped_lock lock(mutex_);
            if (active_ids_.empty() || !pop_next_task(pending)) {
                break;
            }
        }

        auto task = std::move(pending.task);
        task();
        ++stats.executed_task_count;
    }

    {
        const std::scoped_lock lock(mutex_);
        stats.remaining_task_count = active_ids_.size();
    }
    return stats;
}

std::size_t FrameScheduler::pending_count() const noexcept {
    const std::scoped_lock lock(mutex_);
    return active_ids_.size();
}

bool FrameScheduler::empty() const noexcept {
    const std::scoped_lock lock(mutex_);
    return active_ids_.empty();
}

bool FrameScheduler::cancel_active_task(TaskId id) noexcept {
    if (active_ids_.erase(id) == 0U) {
        return false;
    }

    canceled_ids_.insert(id);
    if (const auto key_iterator = task_keys_.find(id); key_iterator != task_keys_.end()) {
        if (const auto coalesced = coalesced_tasks_.find(key_iterator->second);
            coalesced != coalesced_tasks_.end() && coalesced->second == id) {
            coalesced_tasks_.erase(coalesced);
        }
        task_keys_.erase(key_iterator);
    }
    return true;
}

bool FrameScheduler::pop_next_task(PendingTask& task) noexcept {
    for (auto& bucket : tasks_) {
        while (!bucket.empty()) {
            auto pending = std::move(bucket.front());
            bucket.pop_front();

            if (canceled_ids_.erase(pending.id) != 0U) {
                continue;
            }

            if (active_ids_.erase(pending.id) == 0U) {
                continue;
            }

            if (const auto key_iterator = task_keys_.find(pending.id);
                key_iterator != task_keys_.end()) {
                if (const auto coalesced = coalesced_tasks_.find(key_iterator->second);
                    coalesced != coalesced_tasks_.end() && coalesced->second == pending.id) {
                    coalesced_tasks_.erase(coalesced);
                }
                task_keys_.erase(key_iterator);
            }

            task = std::move(pending);
            return true;
        }
    }
    return false;
}

} // namespace winelement::core
