#pragma once

#include <memory>
#include <queue>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "Scheduler.h"
#include "Task.h"
#include "Event.h"
#include "MetricsCollector.h"

using json = nlohmann::json;

/**
 * @brief The SimulationEngine manages simulation time, events, and scheduler coordination.
 * It is responsible for:
 *  - Advancing simulated time
 *  - Dispatching events (arrivals, completions, I/O, etc.)
 *  - Invoking the Scheduler for task selection
 *  - Recording metrics for later export
 */
class SimulationEngine {
public:
    explicit SimulationEngine(std::unique_ptr<IScheduler> scheduler);

    // Load workload and scenario configuration from JSON
    void loadWorkload(const json& workloadConfig, uint64_t horizonMs);
    void configureScenario(const json& scenarioConfig, uint64_t plannedDurationMs);

    // Run the full simulation for a given duration (ms)
    void run(uint64_t durationMs);

    // Export metrics + timeline trace
    json exportResults() const;

    MetricsCollector& metrics() { return metrics_; }
    const MetricsCollector& metrics() const { return metrics_; }

private:
    // --- Internal structures ---
    struct Core {
        Task* currentTask = nullptr;
        uint64_t busyUntil = 0;
        bool idle = true;
        uint64_t idleStart = 0;
    };

    std::unique_ptr<IScheduler> scheduler_;
    MetricsCollector metrics_;
    std::vector<Core> cores_;
    std::priority_queue<Event, std::vector<Event>, EventCompare> eventQueue_;

    std::vector<Task> tasks_;
    uint64_t currentTime_ = 0;
    uint64_t contextSwitchCostUs_ = 15;
    uint64_t tickIntervalUs_ = 0;
    uint64_t tickIntervalMs_ = 0;
    uint64_t ioCompletionQuantumUs_ = 0;
    uint64_t plannedRunDurationMs_ = 0;
    uint32_t numCores_ = 1;
    std::string systemName_ = "default";
    double clockSpeedMhz_ = 0.0;
    double basePowerWatts_ = 0.0;
    double maxPowerWatts_ = 0.0;
    double idlePowerWatts_ = 0.0;
    bool verbose_ = false;

    // --- Internal helper functions ---
    void handleEvent(const Event& e);
    void dispatchTasks();
    void scheduleNextEvent(Task& t);
    void log(const std::string& msg) const;
    void scheduleTimerTick(uint64_t startTimeMs);
    std::vector<int> currentCoreAssignments() const;
};
