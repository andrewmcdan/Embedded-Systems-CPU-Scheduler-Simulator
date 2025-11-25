/**
 * @file Scheduler_ProportionalShare.cpp
 * @author Andrew McDaniel
 * @brief Proportional share (weighted fair) scheduling.
 *
 */
#include "Scheduler.h"

#include <algorithm>
#include <spdlog/spdlog.h>

ProportionalShareScheduler::ProportionalShareScheduler(uint64_t baseQuantumMs)
    : baseQuantumMs_(std::max<uint64_t>(1, baseQuantumMs))
{
    SPDLOG_INFO("[PS] Initialised (base quantum {} ms)", this->baseQuantumMs_);
}

uint64_t ProportionalShareScheduler::weightFor(const Task& task) const
{
    // Use priority as weight proxy; higher priority => higher weight.
    const int prio = std::clamp(task.priority, 1, 20);
    return static_cast<uint64_t>(prio);
}

uint64_t ProportionalShareScheduler::sliceFor(const Task& task) const
{
    const uint64_t weight = this->weightFor(task);
    const uint64_t slice = this->baseQuantumMs_ * weight;
    return std::max<uint64_t>(this->baseQuantumMs_, slice);
}

void ProportionalShareScheduler::enqueue(Task& task, std::string_view reason)
{
    this->weights_[task.id] = this->weightFor(task);
    this->queue_.push_back(QueuedTask { &task, this->nextSequence_++ });
    ++this->totalEnqueued_;
    SPDLOG_TRACE("[PS] Enqueue task {} (id={}) weight {} via {} -> depth {}",
        task.name,
        task.id,
        this->weights_[task.id],
        reason,
        this->queue_.size());
}

void ProportionalShareScheduler::onTaskArrival(Task& task)
{
    SPDLOG_DEBUG("[PS] Task {} arrived (id={})", task.name, task.id);
    this->enqueue(task, "arrival");
}

void ProportionalShareScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[PS] Task {} completed (id={})", task.name, task.id);
    this->weights_.erase(task.id);
}

void ProportionalShareScheduler::onIOCompletion(Task& task)
{
    this->enqueue(task, "io completion");
}

void ProportionalShareScheduler::onTimeSliceExpired(Task& task)
{
    this->enqueue(task, "time slice expired");
}

Task* ProportionalShareScheduler::pickNextTask()
{
    if (this->queue_.empty()) {
        SPDLOG_TRACE("[PS] Ready queue empty");
        return nullptr;
    }

    QueuedTask entry = this->queue_.front();
    this->queue_.pop_front();
    Task* next = entry.task;
    if (!next) {
        SPDLOG_WARN("[PS] Encountered null task while dispatching");
        return nullptr;
    }

    const uint64_t slice = this->sliceFor(*next);
    next->currentTimeSliceMs = slice;
    ++this->totalDispatched_;

    SPDLOG_DEBUG("[PS] Dispatch task {} (id={}) weight {} slice {} ms; queue depth {}",
        next->name,
        next->id,
        this->weights_[next->id],
        slice,
        this->queue_.size());

    return next;
}

void ProportionalShareScheduler::printStats() const
{
    SPDLOG_INFO("[PS] Total enqueues: {}", this->totalEnqueued_);
    SPDLOG_INFO("[PS] Remaining queue depth: {}", this->queue_.size());
}
