#pragma once

#include <cstdint>
#include <string>
#include <utility>

/**
 * @brief Represents a simulated process/task in the system.
 */
enum class TaskState {
    NEW,
    READY,
    RUNNING,
    WAITING,
    COMPLETED
};

struct Task {
    int id = -1;
    std::string name = "unnamed";

    uint64_t arrivalTime = 0; // ms
    std::pair<uint64_t, uint64_t> execTime { 0, 0 }; // ms [min, max]
    uint64_t remainingTime = 0; // ms left to execute
    uint64_t deadline = 0; // ms, if applicable
    int priority = 0;
    std::string type = "background"; // "rt", "interactive", "background"
    TaskState state = TaskState::NEW;
    uint64_t currentTimeSliceMs = 0; // Requested quantum for next dispatch (0 => run to completion)
    uint64_t lastRunDurationMs = 0; // Duration of the most recent dispatch slice

    bool isComplete() const { return remainingTime == 0 || state == TaskState::COMPLETED; }
    uint64_t minExecTime() const { return execTime.first; }
    uint64_t maxExecTime() const { return execTime.second; }
};
