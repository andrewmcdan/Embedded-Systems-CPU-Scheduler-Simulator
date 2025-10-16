#pragma once

#include <string>
#include <memory>
#include <queue>
#include <vector>
#include <iostream>
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