#include "MetricsCollector.h"

#include <algorithm>
#include <numeric>

using json = nlohmann::json;

namespace {

std::string_view toString(TaskState state)
{
    switch (state) {
    case TaskState::NEW:
        return "new";
    case TaskState::READY:
        return "ready";
    case TaskState::RUNNING:
        return "running";
    case TaskState::WAITING:
        return "waiting";
    case TaskState::COMPLETED:
        return "completed";
    default:
        return "unknown";
    }
}

} // namespace

void MetricsCollector::TaskMetrics::resetDynamic()
{
    arrivalTime = 0;
    firstDispatchTime = 0;
    completionTime = 0;
    totalRuntime = 0;
    totalWaitTime = 0;
    dispatchCount = 0;
    finalState = TaskState::NEW;
    lastDispatchStart = 0;
    lastStateChangeTime = 0;
    arrivalRecorded = false;
}

json MetricsCollector::TickSample::toJson() const
{
    json out = {
        { "timestamp_ms", timestamp },
        { "total_tasks", totalTasks },
        { "ready_tasks", readyTasks },
        { "running_tasks", runningTasks },
        { "waiting_tasks", waitingTasks },
        { "completed_tasks", completedTasks },
        { "cores_busy", coresBusy },
        { "cores_total", coresTotal },
        { "pending_events", pendingEvents },
    };

    const double util = coresTotal == 0 ? 0.0 : static_cast<double>(coresBusy) / static_cast<double>(coresTotal);
    out["cpu_utilization"] = util;
    out["core_assignments"] = coreAssignments;

    return out;
}

void MetricsCollector::reset()
{
    timeline_.clear();
    tickSamples_.clear();
    taskDefinitions_.clear();
    taskDefinitionIndex_.clear();
    taskStats_.clear();
    counters_ = {};
    simulationStartMs_ = 0;
    totalSimTimeMs_ = 0;
    cpuUtilAccumulator_ = 0.0;
    cpuUtilSamples_ = 0;
    scenarioMetadata_ = json::object();
    workloadMetadata_ = json::object();
}

void MetricsCollector::setScenarioMetadata(json metadata)
{
    scenarioMetadata_ = std::move(metadata);
}

void MetricsCollector::setWorkloadMetadata(json metadata)
{
    workloadMetadata_ = std::move(metadata);
}

void MetricsCollector::registerTaskDefinition(const Task& task)
{
    auto& stats = ensureTaskMetrics(task.id, task);
    stats.resetDynamic();

    json definition = {
        { "task_id", task.id },
        { "name", task.name },
        { "class", task.type },
        { "priority", task.priority },
        { "arrival_ms", task.arrivalTime },
        { "exec_min_ms", task.execTime.first },
        { "exec_max_ms", task.execTime.second },
        { "deadline_ms", task.deadline }
    };

    const auto defIt = taskDefinitionIndex_.find(task.id);
    if (defIt == taskDefinitionIndex_.end()) {
        const size_t index = taskDefinitions_.size();
        taskDefinitions_.push_back(definition);
        taskDefinitionIndex_.emplace(task.id, index);
    } else {
        taskDefinitions_[defIt->second] = definition;
    }

    workloadMetadata_["task_count"] = taskStats_.size();
}

void MetricsCollector::startSimulation(uint64_t startTimeMs)
{
    simulationStartMs_ = startTimeMs;
    timeline_.clear();
    tickSamples_.clear();
    counters_ = {};
    cpuUtilAccumulator_ = 0.0;
    cpuUtilSamples_ = 0;

    for (auto& [_, stats] : taskStats_) {
        stats.resetDynamic();
    }
}

void MetricsCollector::recordTaskArrival(const Task& task, uint64_t timeMs)
{
    auto& stats = ensureTaskMetrics(task.id, task);
    if (!stats.arrivalRecorded) {
        stats.arrivalTime = timeMs;
        stats.arrivalRecorded = true;
    }
    stats.lastStateChangeTime = timeMs;
    stats.finalState = TaskState::READY;

    counters_.taskArrivals++;

    TimelineEvent event;
    event.start = timeMs;
    event.end = timeMs;
    event.taskId = task.id;
    event.taskName = stats.name;
    event.eventType = "task_arrival";
    event.metadata = {
        { "task_class", stats.taskClass },
        { "priority", stats.priority }
    };

    appendTimelineEvent(std::move(event));
}

void MetricsCollector::recordTaskDispatch(const Task& task,
    size_t coreIndex,
    uint64_t startTimeMs,
    uint64_t expectedFinishMs,
    uint64_t contextSwitchCostUs)
{
    auto& stats = ensureTaskMetrics(task.id, task);

    if (stats.arrivalRecorded && startTimeMs >= stats.lastStateChangeTime) {
        stats.totalWaitTime += startTimeMs - stats.lastStateChangeTime;
    }

    if (stats.firstDispatchTime == 0) {
        stats.firstDispatchTime = startTimeMs;
    }

    stats.dispatchCount += 1;
    stats.lastDispatchStart = startTimeMs;
    stats.lastStateChangeTime = startTimeMs;
    stats.finalState = TaskState::RUNNING;

    counters_.taskDispatches++;
    counters_.contextSwitches++;

    TimelineEvent event;
    event.start = startTimeMs;
    event.end = expectedFinishMs;
    event.taskId = task.id;
    event.taskName = stats.name;
    event.eventType = "task_dispatch";
    event.coreIndex = coreIndex;

    const uint64_t expectedRuntime = expectedFinishMs > startTimeMs ? expectedFinishMs - startTimeMs : 0;
    event.metadata = {
        { "expected_finish_ms", expectedFinishMs },
        { "expected_runtime_ms", expectedRuntime },
        { "context_switch_cost_us", contextSwitchCostUs },
        { "dispatch_count", stats.dispatchCount }
    };

    appendTimelineEvent(std::move(event));
}

void MetricsCollector::recordTaskCompletion(const Task& task, uint64_t timeMs)
{
    auto& stats = ensureTaskMetrics(task.id, task);

    if (timeMs >= stats.lastDispatchStart) {
        stats.totalRuntime += timeMs - stats.lastDispatchStart;
    }

    stats.completionTime = timeMs;
    stats.lastStateChangeTime = timeMs;
    stats.finalState = TaskState::COMPLETED;

    counters_.taskCompletions++;

    TimelineEvent event;
    event.start = timeMs;
    event.end = timeMs;
    event.taskId = task.id;
    event.taskName = stats.name;
    event.eventType = "task_completion";
    event.metadata = {
        { "total_runtime_ms", stats.totalRuntime },
        { "dispatch_count", stats.dispatchCount }
    };

    appendTimelineEvent(std::move(event));
}

void MetricsCollector::recordIoCompletion(const Task& task, uint64_t timeMs)
{
    auto& stats = ensureTaskMetrics(task.id, task);
    stats.lastStateChangeTime = timeMs;
    stats.finalState = TaskState::READY;

    counters_.ioCompletions++;

    TimelineEvent event;
    event.start = timeMs;
    event.end = timeMs;
    event.taskId = task.id;
    event.taskName = stats.name;
    event.eventType = "io_completion";
    event.metadata = {
        { "task_class", stats.taskClass }
    };

    appendTimelineEvent(std::move(event));
}

void MetricsCollector::recordCoreIdle(size_t coreIndex, uint64_t timeMs)
{
    counters_.coreIdleSamples++;

    TimelineEvent event;
    event.start = timeMs;
    event.end = timeMs;
    event.eventType = "core_idle";
    event.coreIndex = coreIndex;
    event.metadata = json::object();

    appendTimelineEvent(std::move(event));
}

void MetricsCollector::recordTimerTick(uint64_t timeMs,
    const std::vector<int>& coreAssignments,
    const std::vector<Task>& tasks,
    size_t pendingEvents)
{
    size_t ready = 0;
    size_t running = 0;
    size_t waiting = 0;
    size_t completed = 0;

    for (const auto& task : tasks) {
        switch (task.state) {
        case TaskState::READY:
            ready++;
            break;
        case TaskState::RUNNING:
            running++;
            break;
        case TaskState::WAITING:
            waiting++;
            break;
        case TaskState::COMPLETED:
            completed++;
            break;
        case TaskState::NEW:
        default:
            break;
        }
    }

    const size_t busyCores = std::count_if(
        coreAssignments.begin(),
        coreAssignments.end(),
        [](int taskId) { return taskId >= 0; });

    TickSample sample;
    sample.timestamp = timeMs;
    sample.totalTasks = tasks.size();
    sample.readyTasks = ready;
    sample.runningTasks = running;
    sample.waitingTasks = waiting;
    sample.completedTasks = completed;
    sample.coresBusy = busyCores;
    sample.coresTotal = coreAssignments.size();
    sample.pendingEvents = pendingEvents;
    sample.coreAssignments = coreAssignments;

    tickSamples_.push_back(sample);

    const double util = sample.coresTotal == 0 ? 0.0 : static_cast<double>(sample.coresBusy) / static_cast<double>(sample.coresTotal);
    cpuUtilAccumulator_ += util;
    cpuUtilSamples_++;

    counters_.timerTicks++;
}

void MetricsCollector::finalize(uint64_t simEndTimeMs)
{
    totalSimTimeMs_ = simEndTimeMs;
}

json MetricsCollector::buildReport() const
{
    json report;
    report["config"] = scenarioMetadata_;
    report["workload"] = workloadMetadata_;
    report["workload"]["tasks"] = taskDefinitions_;

    json tasksArray = json::array();
    double totalWait = 0.0;
    double totalTurnaround = 0.0;
    double totalResponse = 0.0;
    double totalRuntime = 0.0;
    size_t waitSamples = 0;
    size_t completedTasks = 0;
    size_t respondedTasks = 0;

    for (const auto& [taskId, stats] : taskStats_) {
        json entry = {
            { "task_id", taskId },
            { "name", stats.name },
            { "class", stats.taskClass },
            { "priority", stats.priority },
            { "requested_exec_min_ms", stats.requestedExecMin },
            { "requested_exec_max_ms", stats.requestedExecMax },
            { "deadline_ms", stats.deadline },
            { "arrival_ms", stats.arrivalTime },
            { "first_dispatch_ms", stats.firstDispatchTime },
            { "completion_ms", stats.completionTime },
            { "dispatch_count", stats.dispatchCount },
            { "wait_time_ms", stats.totalWaitTime },
            { "runtime_ms", stats.totalRuntime },
            { "final_state", std::string(toString(stats.finalState)) }
        };

        if (stats.dispatchCount > 0) {
            totalWait += static_cast<double>(stats.totalWaitTime);
            waitSamples++;
            totalRuntime += static_cast<double>(stats.totalRuntime);
        }

        if (stats.arrivalRecorded && stats.firstDispatchTime >= stats.arrivalTime && stats.firstDispatchTime != 0) {
            totalResponse += static_cast<double>(stats.firstDispatchTime - stats.arrivalTime);
            respondedTasks++;
        }

        if (stats.arrivalRecorded && stats.completionTime >= stats.arrivalTime && stats.completionTime != 0) {
            totalTurnaround += static_cast<double>(stats.completionTime - stats.arrivalTime);
            completedTasks++;
            entry["turnaround_time_ms"] = stats.completionTime - stats.arrivalTime;
        } else {
            entry["turnaround_time_ms"] = nullptr;
        }

        tasksArray.push_back(std::move(entry));
    }

    report["tasks"]["lifecycle"] = std::move(tasksArray);

    json summary;
    summary["simulation_start_ms"] = simulationStartMs_;
    summary["simulation_end_ms"] = totalSimTimeMs_;
    summary["total_simulation_time_ms"] = totalSimTimeMs_ >= simulationStartMs_
        ? totalSimTimeMs_ - simulationStartMs_
        : totalSimTimeMs_;
    summary["task_count"] = taskStats_.size();
    summary["completed_tasks"] = completedTasks;

    summary["average_wait_time_ms"] = waitSamples == 0 ? 0.0 : totalWait / static_cast<double>(waitSamples);
    summary["average_turnaround_time_ms"] = completedTasks == 0 ? 0.0 : totalTurnaround / static_cast<double>(completedTasks);
    summary["average_response_time_ms"] = respondedTasks == 0 ? 0.0 : totalResponse / static_cast<double>(respondedTasks);
    summary["average_runtime_ms"] = waitSamples == 0 ? 0.0 : totalRuntime / static_cast<double>(waitSamples);

    const double avgUtil = cpuUtilSamples_ == 0 ? 0.0 : cpuUtilAccumulator_ / static_cast<double>(cpuUtilSamples_);
    summary["cpu_utilization"] = {
        { "average", avgUtil },
        { "samples", cpuUtilSamples_ }
    };

    report["summary"] = std::move(summary);

    report["counters"] = {
        { "task_arrivals", counters_.taskArrivals },
        { "task_dispatches", counters_.taskDispatches },
        { "task_completions", counters_.taskCompletions },
        { "io_completions", counters_.ioCompletions },
        { "context_switches", counters_.contextSwitches },
        { "core_idle_samples", counters_.coreIdleSamples },
        { "timer_ticks", counters_.timerTicks }
    };

    report["timeline"] = timelineJson();
    report["ticks"] = ticksJson();

    return report;
}

json MetricsCollector::timelineJson() const
{
    json arr = json::array();

    for (const auto& event : timeline_) {
        json entry = {
            { "start_ms", event.start },
            { "end_ms", event.end },
            { "event", event.eventType },
            { "metadata", event.metadata }
        };

        if (event.taskId >= 0) {
            entry["task_id"] = event.taskId;
        } else {
            entry["task_id"] = nullptr;
        }

        if (!event.taskName.empty()) {
            entry["task_name"] = event.taskName;
        }

        if (event.coreIndex.has_value()) {
            entry["core"] = *event.coreIndex;
        } else {
            entry["core"] = nullptr;
        }

        arr.push_back(std::move(entry));
    }

    return arr;
}

json MetricsCollector::ticksJson() const
{
    json arr = json::array();
    for (const auto& sample : tickSamples_) {
        arr.push_back(sample.toJson());
    }
    return arr;
}

MetricsCollector::TaskMetrics& MetricsCollector::ensureTaskMetrics(int taskId, const Task& task)
{
    auto [it, inserted] = taskStats_.try_emplace(taskId);
    TaskMetrics& stats = it->second;
    if (inserted) {
        stats.id = taskId;
        stats.name = task.name;
        stats.taskClass = task.type;
        stats.priority = task.priority;
        stats.requestedExecMin = task.execTime.first;
        stats.requestedExecMax = task.execTime.second;
        stats.deadline = task.deadline;
        stats.resetDynamic();
    } else {
        stats.name = task.name;
        stats.taskClass = task.type;
        stats.priority = task.priority;
        stats.requestedExecMin = task.execTime.first;
        stats.requestedExecMax = task.execTime.second;
        stats.deadline = task.deadline;
    }
    return stats;
}

void MetricsCollector::appendTimelineEvent(TimelineEvent event)
{
    if (!event.metadata.is_object()) {
        event.metadata = json::object();
    }
    timeline_.push_back(std::move(event));
}
