#pragma once

#include <cstdint>
#include <string>
#include <queue>
#include <memory>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <string_view>
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
    virtual void onIOCompletion(Task& task) { (void)task; }
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
 * @brief Non-preemptive Shortest-Job-First scheduler.
 *
 * Tasks with the smallest remaining CPU burst are dispatched first.
 */
class SJFScheduler : public IScheduler {
public:
    SJFScheduler() = default;

    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    void onIOCompletion(Task& task) override;
    void onTimeSliceExpired(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;

private:
    std::vector<Task*> readyQueue_;
    size_t totalEnqueued_ = 0;
    size_t totalDispatched_ = 0;

    static uint64_t nextBurst(const Task& task);
    static bool shorter(Task* lhs, Task* rhs);
    void enqueue(Task& task, std::string_view reason);
};

/**
 * @brief Static priority scheduler (lower numeric value == higher priority).
 */
class PriorityScheduler : public IScheduler {
public:
    PriorityScheduler() = default;

    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    void onIOCompletion(Task& task) override;
    void onTimeSliceExpired(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;

private:
    struct QueuedTask {
        Task* task = nullptr;
        uint64_t sequence = 0;
    };

    struct Comparator {
        bool operator()(const QueuedTask& lhs, const QueuedTask& rhs) const;
    };

    std::priority_queue<QueuedTask, std::vector<QueuedTask>, Comparator> readyQueue_;
    uint64_t nextSequence_ = 0;
    size_t totalEnqueued_ = 0;
    size_t totalDispatched_ = 0;

    void enqueue(Task& task, std::string_view reason);
};

/**
 * @brief Classic Round-Robin scheduler with a fixed time quantum.
 *
 * Tasks are processed in FIFO order, each receiving up to the configured
 * quantum before being rotated to the back of the queue if not completed.
 */
class RRScheduler : public IScheduler {
public:
    explicit RRScheduler(uint64_t quantumMs = 5);

    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    void onIOCompletion(Task& task) override;
    void onTimeSliceExpired(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;

    uint64_t quantumMs() const { return this->timeQuantumMs_; }

private:
    std::queue<Task*> readyQueue_;
    uint64_t timeQuantumMs_;
    uint64_t totalEnqueued_ = 0;

    void enqueue(Task& task, std::string_view reason);
};

/**
 * @brief Multi-Level Feedback Queue scheduler.
 *
 * Tasks are distributed across several feedback queues based on their
 * priority. Higher priority queues (lower indices) are always
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
