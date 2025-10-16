#include "RunQueue.h"
#include "Scheduler.h"
#include <spdlog/spdlog.h>

void FCFSScheduler::onTaskArrival(Task& task)
{
    SPDLOG_DEBUG("[FCFS] Task {} arrived (id={})", task.name, task.id);
    readyQueue_.push(&task);
    SPDLOG_DEBUG("[FCFS] Queue size after arrival: {}", readyQueue_.size());
}

void FCFSScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[FCFS] Task {} completed (id={})", task.name, task.id);
}

Task* FCFSScheduler::pickNextTask()
{
    if (readyQueue_.empty()) {
        SPDLOG_TRACE("[FCFS] No task to dispatch");
        return nullptr;
    }
    Task* next = readyQueue_.front();
    readyQueue_.pop();
    SPDLOG_DEBUG("[FCFS] Dispatching task {} (id={}); remaining queue size {}",
        next->name,
        next->id,
        readyQueue_.size());
    return next;
}

void FCFSScheduler::printStats() const
{
    SPDLOG_INFO("[FCFS] Total queued tasks: {}", readyQueue_.size());
}
