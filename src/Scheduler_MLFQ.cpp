/**
 * @file Scheduler_MLFQ.cpp
 * @author Andrew McDaniel
 * @brief Implementation of the Multi-Level Feedback Queue scheduler.
 *
 * Manages multiple priority queues with dynamic promotion and demotion to balance interactive
 * responsiveness and throughput.
 */
#include "Scheduler.h"

#include <algorithm>
#include <iomanip>
#include <spdlog/spdlog.h>

namespace {
constexpr int PRIORITY_MIN = 0;
constexpr int PRIORITY_MAX = 10;
}

MLFQScheduler::MLFQScheduler(size_t levels)
    : queues_(std::max<size_t>(levels, size_t(1)))
{
    SPDLOG_INFO("[MLFQ] Initialised with {} level(s)", this->queues_.size());
    this->quantaMs_.resize(this->queues_.size());
    for (size_t i = 0; i < this->quantaMs_.size(); ++i) {
        // Higher priority queues (lower index) receive shorter quanta
        this->quantaMs_[i] = 5u * static_cast<uint64_t>(1ULL << i);
    }
}

size_t MLFQScheduler::determineLevel(const Task& task) const
{
    if (this->queues_.size() <= 1) {
        return 0;
    }

    const int clampedPriority = std::clamp(task.priority, PRIORITY_MIN, PRIORITY_MAX);
    const double fraction = static_cast<double>(clampedPriority - PRIORITY_MIN) / (PRIORITY_MAX - PRIORITY_MIN + 1);
    const size_t level = static_cast<size_t>(fraction * this->queues_.size());
    return std::min(level, this->queues_.size() - 1);
}

void MLFQScheduler::enqueue(Task& task, size_t level)
{
    if (level >= this->queues_.size()) {
        level = this->queues_.size() - 1;
    }
    SPDLOG_TRACE("[MLFQ] Enqueue task {} -> level {}", task.name, level);
    this->queues_[level].push(&task);
    this->taskLevels_[task.id] = level;
    ++this->totalEnqueued_;
}

uint64_t MLFQScheduler::quantumForLevel(size_t level) const
{
    if (this->quantaMs_.empty()) {
        return 0;
    }
    if (level >= this->quantaMs_.size()) {
        level = this->quantaMs_.size() - 1;
    }
    return this->quantaMs_[level];
}

void MLFQScheduler::onTaskArrival(Task& task)
{
    const size_t targetLevel = this->determineLevel(task);
    this->enqueue(task, targetLevel);
}

void MLFQScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_TRACE("[MLFQ] Task completed {}", task.name);
    this->taskLevels_.erase(task.id);
}

void MLFQScheduler::onIOCompletion(Task& task)
{
    auto levelIt = this->taskLevels_.find(task.id);
    size_t currentLevel = this->determineLevel(task);
    if (levelIt != this->taskLevels_.end()) {
        currentLevel = levelIt->second;
    }
    const size_t promotedLevel = currentLevel == 0 ? 0 : currentLevel - 1;
    this->enqueue(task, promotedLevel);
}

Task* MLFQScheduler::pickNextTask()
{
    for (size_t level = 0; level < this->queues_.size(); ++level) {
        auto& queue = this->queues_[level];
        if (!queue.empty()) {
            Task* task = queue.front();
            queue.pop();
            SPDLOG_TRACE("[MLFQ] Dequeue task {} from level {}", task->name, level);
            this->taskLevels_[task->id] = level;
            const uint64_t quantum = this->quantumForLevel(level);
            task->currentTimeSliceMs = quantum;
            return task;
        }
    }
    return nullptr;
}

void MLFQScheduler::onTimeSliceExpired(Task& task)
{
    auto levelIt = this->taskLevels_.find(task.id);
    size_t level = this->determineLevel(task);
    if (levelIt != this->taskLevels_.end()) {
        level = levelIt->second;
    }
    if (this->queues_.size() > 1 && level + 1 < this->queues_.size()) {
        ++level;
        SPDLOG_TRACE("[MLFQ] Demote task {} to level {}", task.name, level);
    } else {
        SPDLOG_TRACE("[MLFQ] Time slice expired for task {} (stays at level {})", task.name, level);
    }
    this->enqueue(task, level);
}

void MLFQScheduler::printStats() const
{
    SPDLOG_INFO("[MLFQ] Total tasks enqueued: {}", this->totalEnqueued_);
    for (size_t level = 0; level < this->queues_.size(); ++level) {
        SPDLOG_INFO("[MLFQ] Level {} pending: {}", level, this->queues_[level].size());
    }
}
