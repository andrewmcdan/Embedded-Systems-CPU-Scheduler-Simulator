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
    SPDLOG_INFO("[MLFQ] Initialised with {} level(s)", queues_.size());
    quantaMs_.resize(queues_.size());
    for (size_t i = 0; i < quantaMs_.size(); ++i) {
        // Higher priority queues (lower index) receive shorter quanta
        quantaMs_[i] = 5u * static_cast<uint64_t>(1ULL << i);
    }
}

size_t MLFQScheduler::determineLevel(const Task& task) const
{
    if (queues_.size() <= 1) {
        return 0;
    }

    const int clampedPriority = std::clamp(task.priority, PRIORITY_MIN, PRIORITY_MAX);
    const double fraction = static_cast<double>(clampedPriority - PRIORITY_MIN) / (PRIORITY_MAX - PRIORITY_MIN + 1);
    const size_t level = static_cast<size_t>(fraction * queues_.size());
    return std::min(level, queues_.size() - 1);
}

void MLFQScheduler::enqueue(Task& task, size_t level)
{
    if (level >= queues_.size()) {
        level = queues_.size() - 1;
    }
    SPDLOG_TRACE("[MLFQ] Enqueue task {} -> level {}", task.name, level);
    queues_[level].push(&task);
    taskLevels_[task.id] = level;
    ++totalEnqueued_;
}

uint64_t MLFQScheduler::quantumForLevel(size_t level) const
{
    if (quantaMs_.empty()) {
        return 0;
    }
    if (level >= quantaMs_.size()) {
        level = quantaMs_.size() - 1;
    }
    return quantaMs_[level];
}

void MLFQScheduler::onTaskArrival(Task& task)
{
    const size_t targetLevel = determineLevel(task);
    enqueue(task, targetLevel);
}

void MLFQScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_TRACE("[MLFQ] Task completed {}", task.name);
    taskLevels_.erase(task.id);
}

void MLFQScheduler::onIOCompletion(Task& task)
{
    auto it = taskLevels_.find(task.id);
    size_t currentLevel = determineLevel(task);
    if (it != taskLevels_.end()) {
        currentLevel = it->second;
    }
    const size_t promotedLevel = currentLevel == 0 ? 0 : currentLevel - 1;
    enqueue(task, promotedLevel);
}

Task* MLFQScheduler::pickNextTask()
{
    for (size_t level = 0; level < queues_.size(); ++level) {
        auto& queue = queues_[level];
        if (!queue.empty()) {
            Task* task = queue.front();
            queue.pop();
            SPDLOG_TRACE("[MLFQ] Dequeue task {} from level {}", task->name, level);
            taskLevels_[task->id] = level;
            const uint64_t quantum = quantumForLevel(level);
            task->currentTimeSliceMs = quantum;
            return task;
        }
    }
    return nullptr;
}

void MLFQScheduler::onTimeSliceExpired(Task& task)
{
    auto it = taskLevels_.find(task.id);
    size_t level = determineLevel(task);
    if (it != taskLevels_.end()) {
        level = it->second;
    }
    if (queues_.size() > 1 && level + 1 < queues_.size()) {
        ++level;
        SPDLOG_TRACE("[MLFQ] Demote task {} to level {}", task.name, level);
    } else {
        SPDLOG_TRACE("[MLFQ] Time slice expired for task {} (stays at level {})", task.name, level);
    }
    enqueue(task, level);
}

void MLFQScheduler::printStats() const
{
    SPDLOG_INFO("[MLFQ] Total tasks enqueued: {}", totalEnqueued_);
    for (size_t level = 0; level < queues_.size(); ++level) {
        SPDLOG_INFO("[MLFQ] Level {} pending: {}", level, queues_[level].size());
    }
}
