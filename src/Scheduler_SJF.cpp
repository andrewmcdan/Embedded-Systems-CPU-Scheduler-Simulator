#include "Scheduler.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace {

uint64_t burstLength(const Task& task)
{
    if (task.remainingTime != 0) {
        return task.remainingTime;
    }
    const uint64_t minExec = task.minExecTime();
    if (minExec != 0) {
        return minExec;
    }
    const uint64_t maxExec = task.maxExecTime();
    if (maxExec != 0) {
        return maxExec;
    }
    return 1;
}

} // namespace

uint64_t SJFScheduler::nextBurst(const Task& task)
{
    return burstLength(task);
}

bool SJFScheduler::shorter(Task* lhs, Task* rhs)
{
    if (lhs == rhs) {
        return false;
    }
    const uint64_t lhsBurst = lhs ? nextBurst(*lhs) : 0;
    const uint64_t rhsBurst = rhs ? nextBurst(*rhs) : 0;
    if (lhsBurst != rhsBurst) {
        return lhsBurst < rhsBurst;
    }
    const uint64_t lhsArrival = lhs ? lhs->arrivalTime : 0;
    const uint64_t rhsArrival = rhs ? rhs->arrivalTime : 0;
    if (lhsArrival != rhsArrival) {
        return lhsArrival < rhsArrival;
    }
    const int lhsId = lhs ? lhs->id : -1;
    const int rhsId = rhs ? rhs->id : -1;
    return lhsId < rhsId;
}

void SJFScheduler::onTaskArrival(Task& task)
{
    SPDLOG_DEBUG("[SJF] Task {} arrived (id={})", task.name, task.id);
    this->enqueue(task, "arrival");
}

void SJFScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[SJF] Task {} completed (id={})", task.name, task.id);
}

void SJFScheduler::onIOCompletion(Task& task)
{
    SPDLOG_TRACE("[SJF] Task {} IO completion -> requeue", task.name);
    this->enqueue(task, "io completion");
}

void SJFScheduler::onTimeSliceExpired(Task& task)
{
    SPDLOG_TRACE("[SJF] Time slice expired for task {} (id={})", task.name, task.id);
    this->enqueue(task, "time slice expired");
}

Task* SJFScheduler::pickNextTask()
{
    if (this->readyQueue_.empty()) {
        SPDLOG_TRACE("[SJF] Ready queue empty");
        return nullptr;
    }

    auto it = std::min_element(this->readyQueue_.begin(), this->readyQueue_.end(), &SJFScheduler::shorter);
    Task* next = *it;
    this->readyQueue_.erase(it);
    ++this->totalDispatched_;

    SPDLOG_DEBUG("[SJF] Dispatching task {} (id={}) with expected burst {} ms; queue depth now {}",
        next->name,
        next->id,
        nextBurst(*next),
        this->readyQueue_.size());

    return next;
}

void SJFScheduler::printStats() const
{
    SPDLOG_INFO("[SJF] Total enqueues: {}", this->totalEnqueued_);
    SPDLOG_INFO("[SJF] Total dispatches: {}", this->totalDispatched_);
    SPDLOG_INFO("[SJF] Remaining queue depth: {}", this->readyQueue_.size());
}

void SJFScheduler::enqueue(Task& task, std::string_view reason)
{
    this->readyQueue_.push_back(&task);
    ++this->totalEnqueued_;
    SPDLOG_TRACE("[SJF] Enqueued task {} (id={}) via {} -> depth {} (burst {} ms)",
        task.name,
        task.id,
        reason,
        this->readyQueue_.size(),
        nextBurst(task));
}
