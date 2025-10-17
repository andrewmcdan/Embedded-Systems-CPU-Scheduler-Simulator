#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Task.h"

using json = nlohmann::json;

/**
 * @brief Collects performance data, timeline traces, and aggregate metrics
 *        for a simulation execution.
 */
class MetricsCollector {
public:
    struct TimelineEvent {
        uint64_t start = 0;
        uint64_t end = 0;
        int taskId = -1;
        std::string taskName;
        std::string eventType;
        std::optional<size_t> coreIndex;
        json metadata = json::object();
    };

    struct TickSample {
        uint64_t timestamp = 0;
        size_t totalTasks = 0;
        size_t readyTasks = 0;
        size_t runningTasks = 0;
        size_t waitingTasks = 0;
        size_t completedTasks = 0;
        size_t coresBusy = 0;
        size_t coresTotal = 0;
        size_t pendingEvents = 0;
        std::vector<int> coreAssignments;

        json toJson() const;
    };

    struct TaskMetrics {
        int id = -1;
        std::string name;
        std::string taskClass;
        int priority = 0;
        uint64_t requestedExecMin = 0;
        uint64_t requestedExecMax = 0;
        uint64_t deadline = 0;

        uint64_t arrivalTime = 0;
        uint64_t firstDispatchTime = 0;
        uint64_t completionTime = 0;
        uint64_t totalRuntime = 0;
        uint64_t totalWaitTime = 0;
        uint32_t dispatchCount = 0;
        TaskState finalState = TaskState::NEW;

        uint64_t lastDispatchStart = 0;
        uint64_t lastStateChangeTime = 0;
        bool arrivalRecorded = false;

        void resetDynamic();
    };

    struct Counters {
        uint64_t taskArrivals = 0;
        uint64_t taskDispatches = 0;
        uint64_t taskCompletions = 0;
        uint64_t ioCompletions = 0;
        uint64_t contextSwitches = 0;
        uint64_t coreIdleSpans = 0;
        uint64_t coreIdleTotalMs = 0;
        uint64_t timerTicks = 0;
    };

    MetricsCollector() = default;

    void reset();

    void setScenarioMetadata(json metadata);
    void setWorkloadMetadata(json metadata);
    void setCoreCount(size_t cores);

    void registerTaskDefinition(const Task& task);

    void startSimulation(uint64_t startTimeMs);

    void recordTaskArrival(const Task& task, uint64_t timeMs);
    void recordTaskDispatch(const Task& task,
        size_t coreIndex,
        uint64_t startTimeMs,
        uint64_t expectedFinishMs,
        uint64_t contextSwitchCostUs);
    void recordTaskCompletion(const Task& task, uint64_t timeMs);
    void recordIoCompletion(const Task& task, uint64_t timeMs);
    void recordCoreIdle(size_t coreIndex, uint64_t startTimeMs, uint64_t endTimeMs);
    void recordTimerTick(uint64_t timeMs,
        const std::vector<int>& coreAssignments,
        const std::vector<Task>& tasks,
        size_t pendingEvents);

    void finalize(uint64_t simEndTimeMs);

    [[nodiscard]] const Counters& counters() const noexcept { return counters_; }
    [[nodiscard]] const std::unordered_map<int, TaskMetrics>& taskMetrics() const noexcept { return taskStats_; }
    [[nodiscard]] const std::vector<TimelineEvent>& timeline() const noexcept { return timeline_; }
    [[nodiscard]] const std::vector<TickSample>& tickSamples() const noexcept { return tickSamples_; }

    json buildReport() const;
    json timelineJson() const;
    json ticksJson() const;

private:
    TaskMetrics& ensureTaskMetrics(int taskId, const Task& task);
    void appendTimelineEvent(TimelineEvent event);

    std::vector<TimelineEvent> timeline_;
    std::vector<TickSample> tickSamples_;
    std::vector<json> taskDefinitions_;
    std::unordered_map<int, size_t> taskDefinitionIndex_;
    std::unordered_map<int, TaskMetrics> taskStats_;
    std::vector<uint64_t> coreIdleTimeMs_;

    Counters counters_{};
    uint64_t simulationStartMs_ = 0;
    uint64_t totalSimTimeMs_ = 0;
    double cpuUtilAccumulator_ = 0.0;
    size_t cpuUtilSamples_ = 0;

    json scenarioMetadata_ = json::object();
    json workloadMetadata_ = json::object();
};
