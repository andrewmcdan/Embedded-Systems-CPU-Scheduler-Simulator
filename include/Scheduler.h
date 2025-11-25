/**
 * @file Scheduler.h
 * @author Andrew McDaniel
 * @brief Scheduler interface definitions and concrete scheduler declarations.
 *
 * Declares the base IScheduler contract alongside implementations such as FCFS, RR, SJF, and
 * other policy variants used by the simulator.
 */
#pragma once

#include <cstdint>
#include <string>
#include <queue>
#include <deque>
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
 * @brief Earliest-Deadline-First scheduler.
 *
 * Orders tasks by the earliest absolute deadline, falling back to arrival order for ties.
 */
class EDFScheduler : public IScheduler {
public:
    explicit EDFScheduler(bool deadlinesAreAbsolute = false);

    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    void onIOCompletion(Task& task) override;
    void onTimeSliceExpired(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;

private:
    struct QueuedTask {
        Task* task = nullptr;
        uint64_t absoluteDeadline = 0;
        uint64_t sequence = 0;
    };

    struct Comparator {
        bool operator()(const QueuedTask& lhs, const QueuedTask& rhs) const;
    };

    std::priority_queue<QueuedTask, std::vector<QueuedTask>, Comparator> readyQueue_;
    uint64_t nextSequence_ = 0;
    size_t totalEnqueued_ = 0;
    size_t totalDispatched_ = 0;
    bool deadlinesAreAbsolute_ = false;

    uint64_t computeDeadline(const Task& task) const;

    void enqueue(Task& task, std::string_view reason);
};

/**
 * @brief Linux-inspired Completely Fair Scheduler (CFS-lite).
 *
 * Orders tasks by virtual runtime and assigns time slices proportional to weight
 * derived from priority (acting as nice).
 */
class LinuxScheduler : public IScheduler {
public:
    LinuxScheduler(double targetLatencyMs = 20.0, double minGranularityMs = 1.0);

    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    void onIOCompletion(Task& task) override;
    void onTimeSliceExpired(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;

private:
    struct QueuedTask {
        Task* task = nullptr;
        double vruntime = 0.0;
        double weight = 1.0;
        uint64_t sequence = 0;
    };

    struct Comparator {
        bool operator()(const QueuedTask& lhs, const QueuedTask& rhs) const;
    };

    std::priority_queue<QueuedTask, std::vector<QueuedTask>, Comparator> readyQueue_;
    std::unordered_map<int, double> vruntime_;
    double minVruntime_ = 0.0;
    double totalWeight_ = 0.0;
    uint64_t nextSequence_ = 0;
    size_t totalEnqueued_ = 0;
    size_t totalDispatched_ = 0;
    double targetLatencyMs_ = 20.0;
    double minGranularityMs_ = 1.0;
    double baseWeight_ = 1024.0;

    double weightFor(const Task& task) const;
    void updateVruntime(Task& task);
    void enqueue(Task& task, std::string_view reason);
    uint64_t sliceFor(double weight, double totalWeight) const;
};

/**
 * @brief POSIX real-time scheduler (SCHED_FIFO / SCHED_RR-like).
 */
class PosixRTScheduler : public IScheduler {
public:
    explicit PosixRTScheduler(bool roundRobin = false, uint64_t rrQuantumMs = 5);

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
    bool roundRobin_ = false;
    uint64_t rrQuantumMs_ = 5;
    uint64_t nextSequence_ = 0;
    size_t totalEnqueued_ = 0;
    size_t totalDispatched_ = 0;

    int rtPriority(const Task& task) const;
    void enqueue(Task& task, std::string_view reason);
};

/**
 * @brief Priority-based scheduler with basic aging (dynamic boosts).
 */
class PriorityAgingScheduler : public IScheduler {
public:
    PriorityAgingScheduler(int agingStep = 1, int maxBoost = 5, uint64_t quantumMs = 5);

    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    void onIOCompletion(Task& task) override;
    void onTimeSliceExpired(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;

private:
    struct QueuedTask {
        Task* task = nullptr;
        int effectivePriority = 0;
        uint64_t sequence = 0;
    };

    struct Comparator {
        bool operator()(const QueuedTask& lhs, const QueuedTask& rhs) const;
    };

    std::priority_queue<QueuedTask, std::vector<QueuedTask>, Comparator> readyQueue_;
    std::unordered_map<int, int> boosts_;
    uint64_t nextSequence_ = 0;
    size_t totalEnqueued_ = 0;
    size_t totalDispatched_ = 0;
    int agingStep_ = 1;
    int maxBoost_ = 5;
    uint64_t quantumMs_ = 5;

    int effectivePriorityFor(Task& task);
    void enqueue(Task& task, std::string_view reason);
};

/**
 * @brief Rate-Monotonic Scheduler.
 *
 * Orders periodic tasks by their period (shorter period -> higher priority).
 */
class RMSScheduler : public IScheduler {
public:
    RMSScheduler() = default;

    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    void onIOCompletion(Task& task) override;
    void onTimeSliceExpired(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;

private:
    struct QueuedTask {
        Task* task = nullptr;
        uint64_t periodMs = 0;
        uint64_t sequence = 0;
    };

    struct Comparator {
        bool operator()(const QueuedTask& lhs, const QueuedTask& rhs) const;
    };

    std::priority_queue<QueuedTask, std::vector<QueuedTask>, Comparator> readyQueue_;
    uint64_t nextSequence_ = 0;
    size_t totalEnqueued_ = 0;
    size_t totalDispatched_ = 0;

    uint64_t periodFor(const Task& task) const;
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
 * @brief Proportional share (weighted fair) scheduler.
 */
class ProportionalShareScheduler : public IScheduler {
public:
    explicit ProportionalShareScheduler(uint64_t baseQuantumMs = 5);

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

    std::deque<QueuedTask> queue_;
    std::unordered_map<int, uint64_t> weights_;
    uint64_t nextSequence_ = 0;
    size_t totalEnqueued_ = 0;
    size_t totalDispatched_ = 0;
    uint64_t baseQuantumMs_ = 5;

    uint64_t weightFor(const Task& task) const;
    uint64_t sliceFor(const Task& task) const;
    void enqueue(Task& task, std::string_view reason);
};

/**
 * @brief Windows-inspired scheduler with priority classes.
 */
class WindowsScheduler : public IScheduler {
public:
    WindowsScheduler();

    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    void onIOCompletion(Task& task) override;
    void onTimeSliceExpired(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;

private:
    std::vector<std::queue<Task*>> queues_;
    size_t totalEnqueued_ = 0;
    std::vector<uint64_t> quantaMs_;

    size_t classIndex(const Task& task) const;
    void enqueue(Task& task, size_t level, std::string_view reason);
};

/**
 * @brief Multi-Level Queue scheduler (fixed queues without feedback).
 */
class MLQScheduler : public IScheduler {
public:
    explicit MLQScheduler(size_t levels = 2);

    void onTaskArrival(Task& task) override;
    void onTaskCompletion(Task& task) override;
    void onIOCompletion(Task& task) override;
    void onTimeSliceExpired(Task& task) override;
    Task* pickNextTask() override;
    void printStats() const override;

private:
    std::vector<std::queue<Task*>> queues_;
    size_t totalEnqueued_ = 0;
    uint64_t quantumMs_ = 5;

    size_t assignQueue(const Task& task) const;
    void enqueue(Task& task, size_t level, std::string_view reason);
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
