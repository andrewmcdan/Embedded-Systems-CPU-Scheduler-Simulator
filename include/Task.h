#pragma once

#include <string>
#include <cstdint>

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

    uint64_t arrivalTime = 0;   // ms
    uint64_t execTime = 0;      // ms total CPU time required
    uint64_t remainingTime = 0; // ms left to execute
    uint64_t deadline = 0;      // ms, if applicable
    int priority = 0;
    std::string type = "background"; // "rt", "interactive", "background"
    TaskState state = TaskState::NEW;

    bool isComplete() const { return remainingTime == 0 || state == TaskState::COMPLETED; }
};
