/**
 * @file Scheduler_Priority.cpp
 * @author Andrew McDaniel
 * @brief Implementation of a static priority scheduler.
 *
 * Orders ready tasks by configured priority levels, requeueing them on time-slice expiration
 * and I/O completion.
 */
#include "Scheduler.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace {

constexpr int PRIORITY_MIN = 0;
constexpr int PRIORITY_MAX = 10;

int normalizedPriority(const Task& task)
{
    return std::clamp(task.priority, PRIORITY_MIN, PRIORITY_MAX);
}

} // namespace

bool PriorityScheduler::Comparator::operator()(const QueuedTask& lhs, const QueuedTask& rhs) const
{
    if (!lhs.task || !rhs.task) {
        return lhs.sequence > rhs.sequence;
    }
    const int lhsPriority = normalizedPriority(*lhs.task);
    const int rhsPriority = normalizedPriority(*rhs.task);
    if (lhsPriority != rhsPriority) {
        // Lower numeric value => higher priority, so invert comparison
        return lhsPriority > rhsPriority;
    }
    // FIFO order within the same priority band
    return lhs.sequence > rhs.sequence;
}

void PriorityScheduler::enqueue(Task& task, std::string_view reason)
{
    QueuedTask entry { &task, this->nextSequence_++ };
    this->readyQueue_.push(entry);
    ++this->totalEnqueued_;
    SPDLOG_TRACE("[PRIO] Enqueue task {} (id={}) via {} -> depth {} (priority {})",
        task.name,
        task.id,
        reason,
        this->readyQueue_.size(),
        normalizedPriority(task));
}

void PriorityScheduler::onTaskArrival(Task& task)
{
    SPDLOG_DEBUG("[PRIO] Task {} arrived (id={}, priority={})",
        task.name,
        task.id,
        normalizedPriority(task));
    this->enqueue(task, "arrival");
}

void PriorityScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[PRIO] Task {} completed (id={})", task.name, task.id);
}

void PriorityScheduler::onIOCompletion(Task& task)
{
    SPDLOG_TRACE("[PRIO] Task {} IO completion -> requeue", task.name);
    this->enqueue(task, "io completion");
}

void PriorityScheduler::onTimeSliceExpired(Task& task)
{
    SPDLOG_TRACE("[PRIO] Time slice expired for task {} (id={})", task.name, task.id);
    this->enqueue(task, "time slice expired");
}

Task* PriorityScheduler::pickNextTask()
{
    if (this->readyQueue_.empty()) {
        SPDLOG_TRACE("[PRIO] Ready queue empty");
        return nullptr;
    }

    Task* next = this->readyQueue_.top().task;
    this->readyQueue_.pop();
    ++this->totalDispatched_;

    if (!next) {
        SPDLOG_WARN("[PRIO] Encountered null task while dispatching");
        return nullptr;
    }

    SPDLOG_DEBUG("[PRIO] Dispatch task {} (id={}) with priority {}; queue depth now {}",
        next->name,
        next->id,
        normalizedPriority(*next),
        this->readyQueue_.size());
    return next;
}

void PriorityScheduler::printStats() const
{
    SPDLOG_INFO("[PRIO] Total enqueues: {}", this->totalEnqueued_);
    SPDLOG_INFO("[PRIO] Total dispatches: {}", this->totalDispatched_);
    SPDLOG_INFO("[PRIO] Remaining queue depth: {}", this->readyQueue_.size());
}
