/**
 * @file Scheduler_POSIX_RT.cpp
 * @author Andrew McDaniel
 * @brief POSIX real-time scheduling.
 *
 */
#include "Scheduler.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace {
constexpr int RT_PRIO_MIN = 1;
constexpr int RT_PRIO_MAX = 99;
} // namespace

PosixRTScheduler::PosixRTScheduler(bool roundRobin, uint64_t rrQuantumMs)
    : roundRobin_(roundRobin)
    , rrQuantumMs_(std::max<uint64_t>(1, rrQuantumMs))
{
    SPDLOG_INFO("[POSIX_RT] Initialised ({})", roundRobin ? "SCHED_RR" : "SCHED_FIFO");
}

int PosixRTScheduler::rtPriority(const Task& task) const
{
    // Interpret Task::priority as RT priority; clamp to typical POSIX range.
    return std::clamp(task.priority, RT_PRIO_MIN, RT_PRIO_MAX);
}

bool PosixRTScheduler::Comparator::operator()(const QueuedTask& lhs, const QueuedTask& rhs) const
{
    if (!lhs.task || !rhs.task) {
        return lhs.sequence > rhs.sequence;
    }
    const int lp = lhs.task->priority;
    const int rp = rhs.task->priority;
    if (lp != rp) {
        // Higher numeric value => higher RT priority
        return lp < rp;
    }
    return lhs.sequence > rhs.sequence;
}

void PosixRTScheduler::enqueue(Task& task, std::string_view reason)
{
    this->readyQueue_.push(QueuedTask { &task, this->nextSequence_++ });
    ++this->totalEnqueued_;
    SPDLOG_TRACE("[POSIX_RT] Enqueue task {} (id={}) prio={} via {} -> depth {}",
        task.name,
        task.id,
        this->rtPriority(task),
        reason,
        this->readyQueue_.size());
}

void PosixRTScheduler::onTaskArrival(Task& task)
{
    SPDLOG_DEBUG("[POSIX_RT] Task {} arrived (id={}, prio={})", task.name, task.id, this->rtPriority(task));
    this->enqueue(task, "arrival");
}

void PosixRTScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[POSIX_RT] Task {} completed (id={})", task.name, task.id);
}

void PosixRTScheduler::onIOCompletion(Task& task)
{
    SPDLOG_TRACE("[POSIX_RT] Task {} IO completion -> requeue", task.name);
    this->enqueue(task, "io completion");
}

void PosixRTScheduler::onTimeSliceExpired(Task& task)
{
    // Only relevant in RR mode; FIFO ignores time slice expiry.
    if (!this->roundRobin_) {
        SPDLOG_TRACE("[POSIX_RT] Time slice ignored for FIFO task {}", task.name);
        this->enqueue(task, "fifo continued");
        return;
    }
    SPDLOG_TRACE("[POSIX_RT] Time slice expired for task {} (id={})", task.name, task.id);
    this->enqueue(task, "time slice expired");
}

Task* PosixRTScheduler::pickNextTask()
{
    if (this->readyQueue_.empty()) {
        SPDLOG_TRACE("[POSIX_RT] Ready queue empty");
        return nullptr;
    }

    QueuedTask entry = this->readyQueue_.top();
    this->readyQueue_.pop();
    ++this->totalDispatched_;

    Task* next = entry.task;
    if (!next) {
        SPDLOG_WARN("[POSIX_RT] Encountered null task while dispatching");
        return nullptr;
    }

    if (this->roundRobin_) {
        next->currentTimeSliceMs = this->rrQuantumMs_;
    } else {
        next->currentTimeSliceMs = 0; // run to completion or preempted by higher prio
    }

    SPDLOG_DEBUG("[POSIX_RT] Dispatch task {} (id={}) prio={} slice={}ms; queue depth {}",
        next->name,
        next->id,
        this->rtPriority(*next),
        next->currentTimeSliceMs,
        this->readyQueue_.size());

    return next;
}

void PosixRTScheduler::printStats() const
{
    SPDLOG_INFO("[POSIX_RT] Mode: {}", this->roundRobin_ ? "SCHED_RR" : "SCHED_FIFO");
    SPDLOG_INFO("[POSIX_RT] Total enqueues: {}", this->totalEnqueued_);
    SPDLOG_INFO("[POSIX_RT] Total dispatches: {}", this->totalDispatched_);
    SPDLOG_INFO("[POSIX_RT] Remaining queue depth: {}", this->readyQueue_.size());
}
