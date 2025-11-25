/**
 * @file Scheduler_Windows.cpp
 * @author 
 * @brief Windows-inspired scheduling policy.
 *
 * 
 */
#include "Scheduler.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace {
// Rough mapping of Windows priority classes to indices (0 = highest)
constexpr int CLASS_REALTIME = 0;
constexpr int CLASS_HIGH = 1;
constexpr int CLASS_ABOVE_NORMAL = 2;
constexpr int CLASS_NORMAL = 3;
constexpr int CLASS_BELOW_NORMAL = 4;
constexpr int CLASS_IDLE = 5;
} // namespace

WindowsScheduler::WindowsScheduler()
{
    this->queues_.resize(6);
    this->quantaMs_ = { 4, 6, 8, 10, 12, 20 };
    SPDLOG_INFO("[WIN] Initialised with {} priority classes", this->queues_.size());
}

size_t WindowsScheduler::classIndex(const Task& task) const
{
    // Use task.priority to approximate a Windows priority class.
    const int p = task.priority;
    if (p <= 1) return CLASS_REALTIME;
    if (p == 2) return CLASS_HIGH;
    if (p == 3) return CLASS_ABOVE_NORMAL;
    if (p <= 5) return CLASS_NORMAL;
    if (p <= 8) return CLASS_BELOW_NORMAL;
    return CLASS_IDLE;
}

void WindowsScheduler::enqueue(Task& task, size_t level, std::string_view reason)
{
    if (level >= this->queues_.size()) {
        level = this->queues_.size() - 1;
    }
    this->queues_[level].push(&task);
    ++this->totalEnqueued_;
    SPDLOG_TRACE("[WIN] Enqueue task {} (id={}) to class {} via {} -> depth {}",
        task.name,
        task.id,
        level,
        reason,
        this->queues_[level].size());
}

void WindowsScheduler::onTaskArrival(Task& task)
{
    this->enqueue(task, this->classIndex(task), "arrival");
}

void WindowsScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_TRACE("[WIN] Task completed {}", task.name);
}

void WindowsScheduler::onIOCompletion(Task& task)
{
    // Simple boost for I/O-heavy threads: promote one class if possible
    size_t level = this->classIndex(task);
    if (level > 0) {
        --level;
    }
    this->enqueue(task, level, "io completion");
}

void WindowsScheduler::onTimeSliceExpired(Task& task)
{
    size_t level = this->classIndex(task);
    this->enqueue(task, level, "time slice expired");
}

Task* WindowsScheduler::pickNextTask()
{
    for (size_t level = 0; level < this->queues_.size(); ++level) {
        auto& q = this->queues_[level];
        if (!q.empty()) {
            Task* t = q.front();
            q.pop();
            const uint64_t slice = level < this->quantaMs_.size() ? this->quantaMs_[level] : this->quantaMs_.back();
            t->currentTimeSliceMs = slice;
            SPDLOG_DEBUG("[WIN] Dispatch task {} (id={}) class {} slice {} ms; remaining in class {}", t->name, t->id, level, slice, q.size());
            return t;
        }
    }
    return nullptr;
}

void WindowsScheduler::printStats() const
{
    SPDLOG_INFO("[WIN] Total enqueues: {}", this->totalEnqueued_);
    for (size_t level = 0; level < this->queues_.size(); ++level) {
        SPDLOG_INFO("[WIN] Class {} pending: {}", level, this->queues_[level].size());
    }
}
