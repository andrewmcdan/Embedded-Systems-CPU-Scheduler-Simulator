/**
 * @file Scheduler_MLQ.cpp
 * @author Andrew McDaniel
 * @brief Multi-Level Queue scheduler.
 *
 * Uses multiple fixed queues (no feedback). Tasks are assigned to a queue based on
 * task priority (lower -> higher-priority queue). Queues are served in strict order.
 */
#include "Scheduler.h"

#include <spdlog/spdlog.h>

namespace {
constexpr int PRIORITY_MIN = 0;
constexpr int PRIORITY_MAX = 10;
} // namespace

MLQScheduler::MLQScheduler(size_t levels)
    : queues_(std::max<size_t>(levels, 1))
{
    SPDLOG_INFO("[MLQ] Initialised with {} level(s)", this->queues_.size());
}

size_t MLQScheduler::assignQueue(const Task& task) const
{
    if (this->queues_.size() == 1) {
        return 0;
    }
    const int clamped = std::clamp(task.priority, PRIORITY_MIN, PRIORITY_MAX);
    const double fraction = static_cast<double>(clamped - PRIORITY_MIN) / (PRIORITY_MAX - PRIORITY_MIN + 1);
    const size_t level = static_cast<size_t>(fraction * this->queues_.size());
    return std::min(level, this->queues_.size() - 1);
}

void MLQScheduler::enqueue(Task& task, size_t level, std::string_view reason)
{
    if (level >= this->queues_.size()) {
        level = this->queues_.size() - 1;
    }
    this->queues_[level].push(&task);
    ++this->totalEnqueued_;
    SPDLOG_TRACE("[MLQ] Enqueue task {} (id={}) to level {} via {} -> depth {}", task.name, task.id, level, reason, this->queues_[level].size());
}

void MLQScheduler::onTaskArrival(Task& task)
{
    const size_t level = this->assignQueue(task);
    this->enqueue(task, level, "arrival");
}

void MLQScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_TRACE("[MLQ] Task completed {}", task.name);
}

void MLQScheduler::onIOCompletion(Task& task)
{
    const size_t level = this->assignQueue(task);
    this->enqueue(task, level, "io completion");
}

void MLQScheduler::onTimeSliceExpired(Task& task)
{
    const size_t level = this->assignQueue(task);
    this->enqueue(task, level, "time slice expired");
}

Task* MLQScheduler::pickNextTask()
{
    for (size_t level = 0; level < this->queues_.size(); ++level) {
        auto& q = this->queues_[level];
        if (!q.empty()) {
            Task* t = q.front();
            q.pop();
            t->currentTimeSliceMs = this->quantumMs_;
            SPDLOG_TRACE("[MLQ] Dispatch task {} from level {} (queue depth now {})", t->name, level, q.size());
            return t;
        }
    }
    return nullptr;
}

void MLQScheduler::printStats() const
{
    SPDLOG_INFO("[MLQ] Total enqueues: {}", this->totalEnqueued_);
    for (size_t level = 0; level < this->queues_.size(); ++level) {
        SPDLOG_INFO("[MLQ] Level {} pending: {}", level, this->queues_[level].size());
    }
}
