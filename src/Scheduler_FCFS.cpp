/**
 * @file Scheduler_FCFS.cpp
 * @author Andrew McDaniel
 * @brief Implementation of the First-Come-First-Served scheduler.
 *
 * Provides a simple FIFO policy where tasks are executed in arrival order without preemption.
 */
#include "RunQueue.h"
#include "Scheduler.h"
#include <spdlog/spdlog.h>

void FCFSScheduler::onTaskArrival(Task& task)
{
    SPDLOG_DEBUG("[FCFS] Task {} arrived (id={})", task.name, task.id);
    this->readyQueue_.push(&task);
    SPDLOG_DEBUG("[FCFS] Queue size after arrival: {}", this->readyQueue_.size());
}

void FCFSScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[FCFS] Task {} completed (id={})", task.name, task.id);
}

Task* FCFSScheduler::pickNextTask()
{
    if (this->readyQueue_.empty()) {
        SPDLOG_TRACE("[FCFS] No task to dispatch");
        return nullptr;
    }
    Task* next = this->readyQueue_.front();
    this->readyQueue_.pop();
    SPDLOG_DEBUG("[FCFS] Dispatching task {} (id={}); remaining queue size {}",
        next->name,
        next->id,
        this->readyQueue_.size());
    return next;
}

void FCFSScheduler::printStats() const
{
    SPDLOG_INFO("[FCFS] Total queued tasks: {}", this->readyQueue_.size());
}
