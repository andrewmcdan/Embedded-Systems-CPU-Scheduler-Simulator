/**
 * @file Scheduler_RMS.cpp
 * @author Andrew McDaniel
 * @brief Rate-Monotonic Scheduling.
 *
 */
#include "Scheduler.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace {
constexpr uint64_t DEFAULT_PERIOD_MS = 1000;
}

uint64_t RMSScheduler::periodFor(const Task& task) const
{
    // Use deadline as a stand-in for period if present, otherwise fall back.
    if (task.deadline > 0) {
        return task.deadline;
    }
    // Fallback to exec window or default
    if (task.execTime.first > 0) {
        return task.execTime.first * 2;
    }
    return DEFAULT_PERIOD_MS;
}

bool RMSScheduler::Comparator::operator()(const QueuedTask& lhs, const QueuedTask& rhs) const
{
    if (lhs.periodMs != rhs.periodMs) {
        return lhs.periodMs > rhs.periodMs; // shorter period => higher priority
    }
    return lhs.sequence > rhs.sequence;
}

void RMSScheduler::enqueue(Task& task, std::string_view reason)
{
    const uint64_t period = this->periodFor(task);
    this->readyQueue_.push(QueuedTask { &task, period, this->nextSequence_++ });
    ++this->totalEnqueued_;
    SPDLOG_TRACE("[RMS] Enqueue task {} (id={}) period {} via {} -> depth {}",
        task.name,
        task.id,
        period,
        reason,
        this->readyQueue_.size());
}

void RMSScheduler::onTaskArrival(Task& task)
{
    SPDLOG_DEBUG("[RMS] Task {} arrived (id={})", task.name, task.id);
    this->enqueue(task, "arrival");
}

void RMSScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[RMS] Task {} completed (id={})", task.name, task.id);
}

void RMSScheduler::onIOCompletion(Task& task)
{
    this->enqueue(task, "io completion");
}

void RMSScheduler::onTimeSliceExpired(Task& task)
{
    this->enqueue(task, "time slice expired");
}

Task* RMSScheduler::pickNextTask()
{
    if (this->readyQueue_.empty()) {
        SPDLOG_TRACE("[RMS] Ready queue empty");
        return nullptr;
    }

    QueuedTask entry = this->readyQueue_.top();
    this->readyQueue_.pop();
    ++this->totalDispatched_;

    Task* next = entry.task;
    if (!next) {
        SPDLOG_WARN("[RMS] Encountered null task while dispatching");
        return nullptr;
    }

    // Run until completion or preempted by higher-priority arrival (sim handled)
    next->currentTimeSliceMs = 0;
    SPDLOG_DEBUG("[RMS] Dispatch task {} (id={}) period {}ms; queue depth {}",
        next->name,
        next->id,
        entry.periodMs,
        this->readyQueue_.size());
    return next;
}

void RMSScheduler::printStats() const
{
    SPDLOG_INFO("[RMS] Total enqueues: {}", this->totalEnqueued_);
    SPDLOG_INFO("[RMS] Total dispatches: {}", this->totalDispatched_);
    SPDLOG_INFO("[RMS] Remaining queue depth: {}", this->readyQueue_.size());
}
