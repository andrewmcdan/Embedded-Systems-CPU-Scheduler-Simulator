/**
 * @file Scheduler_PriorityBased.cpp
 * @author Andrew McDaniel
 * @brief Priority-based scheduling with aging.
 *
 */
#include "Scheduler.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace {
constexpr int PRIORITY_MIN = 0;
constexpr int PRIORITY_MAX = 10;
} // namespace

PriorityAgingScheduler::PriorityAgingScheduler(int agingStep, int maxBoost, uint64_t quantumMs)
    : agingStep_(std::max(1, agingStep))
    , maxBoost_(std::max(0, maxBoost))
    , quantumMs_(std::max<uint64_t>(1, quantumMs))
{
    SPDLOG_INFO("[PRIO_AGING] Initialised (aging step {}, max boost {}, quantum {} ms)", this->agingStep_, this->maxBoost_, this->quantumMs_);
}

int PriorityAgingScheduler::effectivePriorityFor(Task& task)
{
    const int base = std::clamp(task.priority, PRIORITY_MIN, PRIORITY_MAX);
    int& boost = this->boosts_[task.id];
    boost = std::clamp(boost, 0, this->maxBoost_);
    return std::max(PRIORITY_MIN, base - boost);
}

bool PriorityAgingScheduler::Comparator::operator()(const QueuedTask& lhs, const QueuedTask& rhs) const
{
    if (!lhs.task || !rhs.task) {
        return lhs.sequence > rhs.sequence;
    }
    if (lhs.effectivePriority != rhs.effectivePriority) {
        // Lower numeric value is higher priority
        return lhs.effectivePriority > rhs.effectivePriority;
    }
    return lhs.sequence > rhs.sequence;
}

void PriorityAgingScheduler::enqueue(Task& task, std::string_view reason)
{
    const int eff = this->effectivePriorityFor(task);
    this->readyQueue_.push(QueuedTask { &task, eff, this->nextSequence_++ });
    ++this->totalEnqueued_;
    SPDLOG_TRACE("[PRIO_AGING] Enqueue task {} (id={}) via {} -> eff_prio {} depth {}",
        task.name,
        task.id,
        reason,
        eff,
        this->readyQueue_.size());
}

void PriorityAgingScheduler::onTaskArrival(Task& task)
{
    this->boosts_.erase(task.id);
    SPDLOG_DEBUG("[PRIO_AGING] Task {} arrived (id={}, prio={})", task.name, task.id, task.priority);
    this->enqueue(task, "arrival");
}

void PriorityAgingScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[PRIO_AGING] Task {} completed (id={})", task.name, task.id);
    this->boosts_.erase(task.id);
}

void PriorityAgingScheduler::onIOCompletion(Task& task)
{
    // Small boost for I/O-bound tasks
    this->boosts_[task.id] = std::min(this->maxBoost_, this->boosts_[task.id] + this->agingStep_);
    this->enqueue(task, "io completion");
}

void PriorityAgingScheduler::onTimeSliceExpired(Task& task)
{
    this->boosts_[task.id] = std::min(this->maxBoost_, this->boosts_[task.id] + this->agingStep_);
    this->enqueue(task, "time slice expired");
}

Task* PriorityAgingScheduler::pickNextTask()
{
    if (this->readyQueue_.empty()) {
        SPDLOG_TRACE("[PRIO_AGING] Ready queue empty");
        return nullptr;
    }

    QueuedTask entry = this->readyQueue_.top();
    this->readyQueue_.pop();
    ++this->totalDispatched_;

    Task* next = entry.task;
    if (!next) {
        SPDLOG_WARN("[PRIO_AGING] Encountered null task while dispatching");
        return nullptr;
    }

    next->currentTimeSliceMs = this->quantumMs_;
    SPDLOG_DEBUG("[PRIO_AGING] Dispatch task {} (id={}) eff_prio {} slice {} ms; queue depth {}",
        next->name,
        next->id,
        entry.effectivePriority,
        this->quantumMs_,
        this->readyQueue_.size());

    return next;
}

void PriorityAgingScheduler::printStats() const
{
    SPDLOG_INFO("[PRIO_AGING] Total enqueues: {}", this->totalEnqueued_);
    SPDLOG_INFO("[PRIO_AGING] Total dispatches: {}", this->totalDispatched_);
    SPDLOG_INFO("[PRIO_AGING] Remaining queue depth: {}", this->readyQueue_.size());
}
