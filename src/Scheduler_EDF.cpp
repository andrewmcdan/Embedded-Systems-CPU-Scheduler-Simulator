/**
 * @file Scheduler_EDF.cpp
 * @author Andrew McDaniel
 * @brief Earliest-Deadline-First scheduling policy.
 *
 * Selects the ready task with the nearest deadline (absolute) and dispatches it to run. Tasks
 * without a deadline are de-prioritized behind any real-time work.
 */
#include "Scheduler.h"

#include <limits>
#include <spdlog/spdlog.h>

namespace {
constexpr uint64_t NO_DEADLINE = std::numeric_limits<uint64_t>::max();
}

EDFScheduler::EDFScheduler(bool deadlinesAreAbsolute)
    : deadlinesAreAbsolute_(deadlinesAreAbsolute)
{
    SPDLOG_INFO("[EDF] Initialised (deadlines treated as {})",
        deadlinesAreAbsolute ? "absolute" : "relative to arrival");
}

uint64_t EDFScheduler::computeDeadline(const Task& task) const
{
    if (task.deadline == 0) {
        return NO_DEADLINE;
    }

    if (this->deadlinesAreAbsolute_) {
        return task.deadline;
    }

    const uint64_t arrival = task.arrivalTime;
    const uint64_t offset = task.deadline;
    if (arrival > NO_DEADLINE - offset) {
        return NO_DEADLINE;
    }
    return arrival + offset;
}

bool EDFScheduler::Comparator::operator()(const QueuedTask& lhs, const QueuedTask& rhs) const
{
    if (lhs.absoluteDeadline != rhs.absoluteDeadline) {
        return lhs.absoluteDeadline > rhs.absoluteDeadline;
    }

    const uint64_t lhsArrival = lhs.task ? lhs.task->arrivalTime : 0;
    const uint64_t rhsArrival = rhs.task ? rhs.task->arrivalTime : 0;
    if (lhsArrival != rhsArrival) {
        return lhsArrival > rhsArrival;
    }

    const int lhsId = lhs.task ? lhs.task->id : -1;
    const int rhsId = rhs.task ? rhs.task->id : -1;
    if (lhsId != rhsId) {
        return lhsId > rhsId;
    }

    return lhs.sequence > rhs.sequence;
}

void EDFScheduler::enqueue(Task& task, std::string_view reason)
{
    const uint64_t absDeadline = this->computeDeadline(task);
    this->readyQueue_.push(QueuedTask { &task, absDeadline, this->nextSequence_++ });
    ++this->totalEnqueued_;

    const std::string deadlineText = absDeadline == NO_DEADLINE
        ? std::string("none")
        : std::to_string(absDeadline);

    SPDLOG_TRACE("[EDF] Enqueue task {} (id={}) via {} -> depth {} (deadline_ms={})",
        task.name,
        task.id,
        reason,
        this->readyQueue_.size(),
        deadlineText);
}

void EDFScheduler::onTaskArrival(Task& task)
{
    SPDLOG_DEBUG("[EDF] Task {} arrived (id={}) deadline_ms={}",
        task.name,
        task.id,
        task.deadline);
    this->enqueue(task, "arrival");
}

void EDFScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[EDF] Task {} completed (id={})", task.name, task.id);
}

void EDFScheduler::onIOCompletion(Task& task)
{
    SPDLOG_TRACE("[EDF] Task {} IO completion -> requeue", task.name);
    this->enqueue(task, "io completion");
}

void EDFScheduler::onTimeSliceExpired(Task& task)
{
    SPDLOG_TRACE("[EDF] Time slice expired for task {} (id={})", task.name, task.id);
    this->enqueue(task, "time slice expired");
}

Task* EDFScheduler::pickNextTask()
{
    if (this->readyQueue_.empty()) {
        SPDLOG_TRACE("[EDF] Ready queue empty");
        return nullptr;
    }

    QueuedTask entry = this->readyQueue_.top();
    this->readyQueue_.pop();
    ++this->totalDispatched_;

    Task* next = entry.task;
    if (!next) {
        SPDLOG_WARN("[EDF] Encountered null task while dispatching");
        return nullptr;
    }

    const std::string deadlineText = entry.absoluteDeadline == NO_DEADLINE
        ? std::string("none")
        : std::to_string(entry.absoluteDeadline);

    SPDLOG_DEBUG("[EDF] Dispatch task {} (id={}) deadline_ms={} -> queue depth {}",
        next->name,
        next->id,
        deadlineText,
        this->readyQueue_.size());
    return next;
}

void EDFScheduler::printStats() const
{
    SPDLOG_INFO("[EDF] Total enqueues: {}", this->totalEnqueued_);
    SPDLOG_INFO("[EDF] Total dispatches: {}", this->totalDispatched_);
    SPDLOG_INFO("[EDF] Remaining queue depth: {}", this->readyQueue_.size());
}
