/**
 * @file Task.h
 * @author Andrew McDaniel
 * @brief Task data structures and helper utilities for the simulator.
 *
 * Describes Task, TaskState, and TaskType enumerations used to represent workloads across the
 * scheduling policies and simulation engine.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
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

enum class TaskType {
    Background,
    Interactive,
    RealTime,
    Unknown
};

inline std::string_view toString(TaskType type) noexcept
{
    switch (type) {
    case TaskType::Background:
        return "background";
    case TaskType::Interactive:
        return "interactive";
    case TaskType::RealTime:
        return "rt";
    case TaskType::Unknown:
    default:
        return "unknown";
    }
}

inline TaskType taskTypeFromString(std::string_view value) noexcept
{
    std::string normalised(value.begin(), value.end());
    std::transform(normalised.begin(), normalised.end(), normalised.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalised == "rt" || normalised == "real_time" || normalised == "real-time") {
        return TaskType::RealTime;
    }
    if (normalised == "interactive") {
        return TaskType::Interactive;
    }
    if (normalised == "background") {
        return TaskType::Background;
    }
    return TaskType::Unknown;
}

struct Task {
    int id = -1;
    std::string name = "unnamed";

    uint64_t arrivalTime = 0; // ms
    std::pair<uint64_t, uint64_t> execTime { 0, 0 }; // ms [min, max]
    uint64_t remainingTime = 0; // ms left to execute
   uint64_t deadline = 0; // ms, if applicable
   int priority = 0;
    TaskType type = TaskType::Background;
    TaskState state = TaskState::NEW;
    uint64_t currentTimeSliceMs = 0; // Requested quantum for next dispatch (0 => run to completion)
    uint64_t lastRunDurationMs = 0; // Duration of the most recent dispatch slice

    bool isComplete() const { return remainingTime == 0 || state == TaskState::COMPLETED; }
    uint64_t minExecTime() const { return execTime.first; }
    uint64_t maxExecTime() const { return execTime.second; }
};
