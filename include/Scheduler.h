#pragma once

#include <string>
#include <memory>
#include <queue>
#include <vector>
#include <iostream>
#include <unordered_map>
#include "Task.h"

/**
 * @brief Abstract base class for all scheduling algorithms.
 * Concrete schedulers (FCFS, RR, SJF, etc.) should inherit from IScheduler.
 */
class IScheduler {
public:
    virtual ~IScheduler() = default;

    virtual void onTaskArrival(Task& task) = 0;
    virtual void onTaskCompletion(Task& task) = 0;
    virtual void onIOCompletion(Task& task) {}
    virtual void onTimeSliceExpired(Task& task) { onTaskArrival(task); }
    virtual Task* pickNextTask() = 0;

    virtual void printStats() const {}
};

/**
 * @brief Simple placeholder scheduler (FIFO / FCFS)
 */
class FCFSScheduler : public IScheduler {
private:
    std::queue<Task*> readyQueue_;
public:
    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;
};

/**
 * @brief Multi-Level Feedback Queue scheduler.
 *
 * Tasks are distributed across several feedback queues based on their
 * declared priority. Higher priority queues (lower indices) are always
 * served first. Tasks that re-enter the scheduler after I/O completion
 * are promoted one level to favour interactive workloads.
 */
class MLFQScheduler : public IScheduler {
public:
    explicit MLFQScheduler(size_t levels = 3);

    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    void onIOCompletion(Task& task) override;
    void onTimeSliceExpired(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;

private:
    size_t determineLevel(const Task& task) const;
    void enqueue(Task& task, size_t level);
    uint64_t quantumForLevel(size_t level) const;

    std::vector<std::queue<Task*>> queues_;
    std::unordered_map<int, size_t> taskLevels_;
    std::vector<uint64_t> quantaMs_;
    size_t totalEnqueued_ = 0;
};
