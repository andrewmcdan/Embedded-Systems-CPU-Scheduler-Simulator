#pragma once

#include <cstdint>
#include <string>
#include "Task.h"

/**
 * @brief Enumeration of simulation event types.
 */
enum class EventType {
    TASK_ARRIVAL,
    TASK_COMPLETION,
    IO_COMPLETION,
    TIME_SLICE_EXPIRE,
    TIMER_TICK,
    THROTTLE_ON,
    THROTTLE_OFF
};

/**
 * @brief Represents a scheduled event in the simulation timeline.
 */
struct Event {
    EventType type;
    uint64_t timestamp; // ms
    Task* task;

    Event(EventType t, uint64_t ts, Task* ptr)
        : type(t), timestamp(ts), task(ptr) {}
};

/**
 * @brief Comparator for priority queue (earlier timestamp = higher priority).
 */
struct EventCompare {
    bool operator()(const Event& a, const Event& b) const {
        return a.timestamp > b.timestamp;
    }
};
