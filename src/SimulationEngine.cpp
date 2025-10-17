#include "SimulationEngine.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <optional>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string_view>

#include "ConfigUtils.h"

using namespace simcfg;

// ---------- Constructor ----------
SimulationEngine::SimulationEngine(std::unique_ptr<IScheduler> scheduler)
    : scheduler_(std::move(scheduler))
{
    if (!scheduler_) {
        throw std::invalid_argument("SimulationEngine requires a valid scheduler");
    }
    cores_.resize(1); // default single-core
    metrics_.reset();
    SPDLOG_DEBUG("SimulationEngine initialized with {} core(s)", cores_.size());
}

// ---------- Load Workload ----------
void SimulationEngine::loadWorkload(const json& workloadConfig)
{
    if (!workloadConfig.contains("tasks") || !workloadConfig["tasks"].is_array()) {
        SPDLOG_WARN("Workload config missing 'tasks' array; skipping workload.");
        return;
    }

    const auto& tasksArray = workloadConfig["tasks"];
    tasks_.reserve(tasks_.size() + tasksArray.size());

    uint64_t totalExecMinMs = 0;
    uint64_t totalExecMaxMs = 0;

    for (const auto& t : tasksArray) {
        if (!t.is_object()) {
            SPDLOG_WARN("Skipping malformed task entry (expected object, got {})", t.type_name());
            continue;
        }

        Task task;
        task.id = static_cast<int>(tasks_.size());
        task.name = getStringOr(t, "name", "task_" + std::to_string(task.id));
        task.type = getStringOr(t, "class", "background");
        task.priority = getIntOr(t, "priority", 5);
        task.arrivalTime = extractDurationMs(t, "start", extractDurationMs(t, "arrival", 0));
        const auto execRange = extractDurationRangeMs(t, "exec", { 10u, 10u });
        task.execTime = execRange;
        task.deadline = extractDurationMs(t, "deadline", 0);

        task.remainingTime = task.execTime.second;
        tasks_.push_back(task);

        Task& trackedTask = tasks_.back();
        metrics_.registerTaskDefinition(trackedTask);
        totalExecMinMs += trackedTask.execTime.first;
        totalExecMaxMs += trackedTask.execTime.second;

        SPDLOG_DEBUG("Loaded task id={} name={} arrival={}ms exec[min={}, max={}] priority={} class={}",
            task.id,
            task.name,
            task.arrivalTime,
            task.execTime.first,
            task.execTime.second,
            task.priority,
            task.type);

        Event arrival(EventType::TASK_ARRIVAL, task.arrivalTime, &tasks_.back());
        eventQueue_.push(arrival);
        SPDLOG_DEBUG("Queued TASK_ARRIVAL at {} ms for task {}", task.arrivalTime, task.name);
    }

    metrics_.setWorkloadMetadata({
        { "task_count", tasks_.size() },
        { "total_requested_exec_min_ms", totalExecMinMs },
        { "total_requested_exec_max_ms", totalExecMaxMs }
    });
}

// ---------- Configure Scenario ----------
void SimulationEngine::configureScenario(const json& scenarioConfig)
{
    const json schedulerCfg = scenarioConfig.value("scheduler", json::object());
    const json hardwareCfg = scenarioConfig.value("hardware", json::object());
    const json loggingCfg = scenarioConfig.value("logging", json::object());
    const json systemCfg = scenarioConfig.value("system", json::object());
    const json timingCfg = scenarioConfig.value("timing", json::object());

    systemName_ = getStringOr(systemCfg, "name", systemName_);
    clockSpeedMhz_ = tryGetNumber(systemCfg, "clock_speed_mhz").value_or(clockSpeedMhz_);
    basePowerWatts_ = tryGetNumber(systemCfg, "base_power_watts").value_or(basePowerWatts_);
    maxPowerWatts_ = tryGetNumber(systemCfg, "max_power_watts").value_or(maxPowerWatts_);
    idlePowerWatts_ = tryGetNumber(systemCfg, "idle_power_watts").value_or(idlePowerWatts_);

    auto resolveNumber = [&](std::initializer_list<std::pair<const json*, std::string_view>> sources) -> std::optional<double> {
        for (const auto& [obj, key] : sources) {
            if (obj) {
                if (auto value = tryGetNumber(*obj, key)) {
                    return value;
                }
            }
        }
        return std::nullopt;
    };

    const double coresCandidate = resolveNumber({
                                        { &schedulerCfg, "cores" },
                                        { &systemCfg, "cores" },
                                        { &hardwareCfg, "cores" },
                                        { &scenarioConfig, "cores" },
                                        { &scenarioConfig, "num_cores" },
                                    })
                                      .value_or(1.0);

    numCores_ = static_cast<size_t>(std::max<int>(1, static_cast<int>(std::llround(coresCandidate))));

    const uint64_t scenarioContextSwitchUs = extractDurationUs(timingCfg, "context_switch_cost",
        extractDurationUs(scenarioConfig, "context_switch_cost", contextSwitchCostUs_));
    contextSwitchCostUs_ = extractDurationUs(schedulerCfg, "context_switch_cost", scenarioContextSwitchUs);

    ioCompletionQuantumUs_ = extractDurationUs(timingCfg, "io_completion_quantum",
        extractDurationUs(scenarioConfig, "io_completion_quantum", ioCompletionQuantumUs_));
    tickIntervalUs_ = extractDurationUs(timingCfg, "tick_interval",
        extractDurationUs(scenarioConfig, "tick_interval", tickIntervalUs_));
    tickIntervalMs_ = tickIntervalUs_ == 0 ? 0 : std::max<uint64_t>(1, (tickIntervalUs_ + 999) / 1000);

    verbose_ = getBoolOr(schedulerCfg, "verbose",
        getBoolOr(loggingCfg, "verbose",
            getBoolOr(scenarioConfig, "verbose", false)));

    cores_.resize(numCores_);
    for (auto& core : cores_) {
        core.currentTask = nullptr;
        core.busyUntil = 0;
    }

    metrics_.setScenarioMetadata({
        { "system_name", systemName_ },
        { "clock_speed_mhz", clockSpeedMhz_ },
        { "base_power_watts", basePowerWatts_ },
        { "max_power_watts", maxPowerWatts_ },
        { "idle_power_watts", idlePowerWatts_ },
        { "num_cores", numCores_ },
        { "context_switch_cost_us", contextSwitchCostUs_ },
        { "tick_interval_us", tickIntervalUs_ },
        { "io_completion_quantum_us", ioCompletionQuantumUs_ },
        { "verbose_logging", verbose_ }
    });

    SPDLOG_INFO("Scenario configured: system='{}', cores={}, context_switch_cost_us={}, tick_interval_us={}, io_quantum_us={}, verbose={}",
        systemName_,
        numCores_,
        contextSwitchCostUs_,
        tickIntervalUs_,
        ioCompletionQuantumUs_,
        verbose_);
}

// ---------- Run Simulation ----------
void SimulationEngine::run(uint64_t durationMs)
{
    SPDLOG_INFO("[Engine] Starting simulation ({} ms)", durationMs);
    currentTime_ = 0;

    metrics_.startSimulation(currentTime_);

    if (tickIntervalMs_ != 0) {
        scheduleTimerTick(currentTime_);
    }

    while (!eventQueue_.empty() && currentTime_ <= durationMs) {
        Event e = eventQueue_.top();
        eventQueue_.pop();

        // Advance simulation clock
        currentTime_ = e.timestamp;
        SPDLOG_DEBUG("Advancing to {} ms -> processing event type {} for task {}",
            currentTime_,
            static_cast<int>(e.type),
            e.task ? e.task->name : "<null>");
        handleEvent(e);

        dispatchTasks();
    }

    // Wrap up
    SPDLOG_INFO("[Engine] Simulation complete at {} ms", currentTime_);
    scheduler_->printStats();
    metrics_.finalize(currentTime_);
}

// ---------- Handle Events ----------
void SimulationEngine::handleEvent(const Event& e)
{
    switch (e.type) {
    case EventType::TASK_ARRIVAL:
        SPDLOG_DEBUG("Handling TASK_ARRIVAL for task {}", e.task ? e.task->name : "<null>");
        if (e.task) {
            e.task->state = TaskState::READY;
            metrics_.recordTaskArrival(*e.task, e.timestamp);
            scheduler_->onTaskArrival(*e.task);
        }
        break;

    case EventType::TASK_COMPLETION:
        SPDLOG_DEBUG("Handling TASK_COMPLETION for task {}", e.task ? e.task->name : "<null>");
        if (e.task) {
            e.task->state = TaskState::COMPLETED;
            e.task->remainingTime = 0;
            metrics_.recordTaskCompletion(*e.task, e.timestamp);

            for (auto& core : cores_) {
                if (core.currentTask == e.task) {
                    core.currentTask = nullptr;
                    core.busyUntil = e.timestamp;
                }
            }

            scheduler_->onTaskCompletion(*e.task);
        }
        break;

    case EventType::IO_COMPLETION:
        SPDLOG_DEBUG("Handling IO_COMPLETION for task {}", e.task ? e.task->name : "<null>");
        if (e.task) {
            e.task->state = TaskState::READY;
            metrics_.recordIoCompletion(*e.task, e.timestamp);
            scheduler_->onIOCompletion(*e.task);
        }
        break;

    case EventType::TIMER_TICK:
        SPDLOG_TRACE("Handling TIMER_TICK @ {} ms", e.timestamp);
        metrics_.recordTimerTick(e.timestamp, currentCoreAssignments(), tasks_, eventQueue_.size());
        scheduleTimerTick(e.timestamp);
        break;

    default:
        SPDLOG_WARN("Received unknown event type {}", static_cast<int>(e.type));
        break;
    }
}

// ---------- Dispatch Tasks ----------
void SimulationEngine::dispatchTasks()
{
    for (size_t coreIndex = 0; coreIndex < cores_.size(); ++coreIndex) {
        auto& core = cores_[coreIndex];
        if (core.currentTask == nullptr || core.busyUntil <= currentTime_) {
            Task* next = scheduler_->pickNextTask();
            if (next) {
                if (next->remainingTime == 0) {
                    next->remainingTime = next->execTime.second ? next->execTime.second : 1;
                }

                next->state = TaskState::RUNNING;
                core.currentTask = next;
                core.busyUntil = currentTime_ + next->remainingTime;

                metrics_.recordTaskDispatch(*next, coreIndex, currentTime_, core.busyUntil, contextSwitchCostUs_);

                Event completion(EventType::TASK_COMPLETION, core.busyUntil, next);
                eventQueue_.push(completion);

                SPDLOG_DEBUG("Dispatching task {} on core {} -> finishes @ {}",
                    next->name,
                    coreIndex,
                    core.busyUntil);

                if (verbose_)
                    log("Dispatching task " + next->name + " -> finishes @ " + std::to_string(core.busyUntil));
            } else {
                SPDLOG_TRACE("Core {} idle at {} ms (no task available)", coreIndex, currentTime_);
                core.currentTask = nullptr;
                core.busyUntil = currentTime_;
                metrics_.recordCoreIdle(coreIndex, currentTime_);
            }
        }
    }
}

// ---------- Export Results ----------
json SimulationEngine::exportResults() const
{
    return metrics_.buildReport();
}

// ---------- Log Helper ----------
void SimulationEngine::log(const std::string& msg) const
{
    SPDLOG_DEBUG("[t={} ms] {}", currentTime_, msg);
}

void SimulationEngine::scheduleTimerTick(uint64_t startTimeMs)
{
    if (tickIntervalMs_ == 0) {
        return;
    }

    const uint64_t nextTick = startTimeMs + tickIntervalMs_;
    eventQueue_.push(Event(EventType::TIMER_TICK, nextTick, nullptr));
}

std::vector<int> SimulationEngine::currentCoreAssignments() const
{
    std::vector<int> assignments;
    assignments.reserve(cores_.size());
    for (const auto& core : cores_) {
        assignments.push_back(core.currentTask ? core.currentTask->id : -1);
    }
    return assignments;
}
