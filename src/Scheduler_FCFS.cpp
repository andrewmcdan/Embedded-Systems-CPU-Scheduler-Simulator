#include "RunQueue.h"
#include "Scheduler.h"
#include <spdlog/spdlog.h>

void FCFSScheduler::onTaskArrival(Task& task)
{
    spdlog::debug("[FCFS] Task {} arrived (id={})", task.name, task.id);
    readyQueue_.push(&task);
    spdlog::debug("[FCFS] Queue size after arrival: {}", readyQueue_.size());
}

void FCFSScheduler::onTaskCompletion(Task& task)
{
    spdlog::debug("[FCFS] Task {} completed (id={})", task.name, task.id);
}

Task* FCFSScheduler::pickNextTask()
{
    if (readyQueue_.empty()) {
        spdlog::trace("[FCFS] No task to dispatch");
        return nullptr;
    }
    Task* next = readyQueue_.front();
    readyQueue_.pop();
    spdlog::debug("[FCFS] Dispatching task {} (id={}); remaining queue size {}",
        next->name,
        next->id,
        readyQueue_.size());
    return next;
}

void FCFSScheduler::printStats() const
{
    spdlog::info("[FCFS] Total queued tasks: {}", readyQueue_.size());
}
