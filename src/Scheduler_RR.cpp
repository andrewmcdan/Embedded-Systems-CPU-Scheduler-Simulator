#include "Scheduler.h"

#include <algorithm>
#include <string_view>
#include <spdlog/spdlog.h>

RRScheduler::RRScheduler(uint64_t quantumMs)
    : timeQuantumMs_(std::max<uint64_t>(1, quantumMs))
{
    SPDLOG_INFO("[RR] Initialised (quantum={} ms)", this->timeQuantumMs_);
}

void RRScheduler::onTaskArrival(Task& task)
{
    SPDLOG_DEBUG("[RR] Task {} arrived (id={})", task.name, task.id);
    this->enqueue(task, "arrival");
}

void RRScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[RR] Task {} completed (id={})", task.name, task.id);
}

void RRScheduler::onIOCompletion(Task& task)
{
    SPDLOG_TRACE("[RR] Task {} IO completion (id={})", task.name, task.id);
    this->enqueue(task, "io completion");
}

void RRScheduler::onTimeSliceExpired(Task& task)
{
    SPDLOG_TRACE("[RR] Time slice expired for task {} (id={})", task.name, task.id);
    this->enqueue(task, "time slice expired");
}

Task* RRScheduler::pickNextTask()
{
    if (this->readyQueue_.empty()) {
        SPDLOG_TRACE("[RR] No task ready to dispatch");
        return nullptr;
    }

    Task* next = this->readyQueue_.front();
    this->readyQueue_.pop();
    next->currentTimeSliceMs = this->timeQuantumMs_;

    SPDLOG_DEBUG("[RR] Dispatch task {} (id={}) for up to {} ms; queue size now {}",
        next->name,
        next->id,
        this->timeQuantumMs_,
        this->readyQueue_.size());

    return next;
}

void RRScheduler::printStats() const
{
    SPDLOG_INFO("[RR] Total queue operations: {}", this->totalEnqueued_);
    SPDLOG_INFO("[RR] Ready queue depth at shutdown: {}", this->readyQueue_.size());
}

void RRScheduler::enqueue(Task& task, std::string_view reason)
{
    this->readyQueue_.push(&task);
    ++this->totalEnqueued_;
    SPDLOG_TRACE("[RR] Enqueue task {} (id={}) via {} -> depth {}",
        task.name,
        task.id,
        reason,
        this->readyQueue_.size());
}
