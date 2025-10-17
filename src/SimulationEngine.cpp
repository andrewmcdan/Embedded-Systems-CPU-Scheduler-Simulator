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

namespace {

uint64_t optionalNumberToUint(const std::optional<double>& value, uint64_t fallback)
{
    if (!value) {
        return fallback;
    }
    const double raw = *value;
    if (raw < 0.0) {
        return fallback;
    }
    return static_cast<uint64_t>(std::llround(raw));
}

uint64_t applyJitterPercent(uint64_t baseTimeMs, uint64_t jitterPercent, size_t occurrenceIndex, uint64_t periodMs)
{
    if (jitterPercent == 0 || periodMs == 0) {
        return baseTimeMs;
    }
    uint64_t swing = (periodMs * jitterPercent) / 100;
    if (swing == 0) {
        swing = 1;
    }
    const int64_t sign = (occurrenceIndex % 2 == 0) ? -1 : 1;
    int64_t jittered = static_cast<int64_t>(baseTimeMs) + sign * static_cast<int64_t>(swing / 2);
    if (jittered < 0) {
        jittered = 0;
    }
    return static_cast<uint64_t>(jittered);
}

uint64_t selectExecDurationMs(const std::pair<uint64_t, uint64_t>& range, size_t occurrenceIndex)
{
    const uint64_t minMs = std::max<uint64_t>(1, range.first);
    const uint64_t maxMs = std::max<uint64_t>(minMs, range.second);
    if (maxMs == minMs) {
        return minMs;
    }
    const uint64_t span = maxMs - minMs + 1;
    const uint64_t offset = occurrenceIndex % span;
    return minMs + offset;
}

std::vector<uint64_t> generateArrivalSchedule(const json& taskConfig, uint64_t horizonMs)
{
    std::vector<uint64_t> arrivals;
    if (horizonMs == 0) {
        return arrivals;
    }

    const uint64_t startMs = extractDurationMs(taskConfig, "start", extractDurationMs(taskConfig, "arrival", 0));
    if (startMs > horizonMs) {
        return arrivals;
    }

    const uint64_t jitterPercent = optionalNumberToUint(tryGetNumber(taskConfig, "jitter_percent"), 0);

    const json* arrivalPattern = nullptr;
    if (auto patternIt = taskConfig.find("arrival_pattern"); patternIt != taskConfig.end() && patternIt->is_object()) {
        arrivalPattern = &(*patternIt);
    }

    auto readDurationMs = [&](const json& obj, std::string_view key, uint64_t fallback) {
        if (auto asDuration = tryGetNumber(obj, key)) {
            return optionalNumberToUint(asDuration, fallback);
        }
        return fallback;
    };

    auto appendArrival = [&](uint64_t timeMs) {
        if (timeMs <= horizonMs) {
            arrivals.push_back(timeMs);
        }
    };

    if (arrivalPattern) {
        const std::string patternType = arrivalPattern->value("type", std::string());
        if (patternType == "burst") {
            uint64_t intervalMs = readDurationMs(*arrivalPattern, "mean_interval_ms", 1000);
            if (intervalMs == 0) {
                intervalMs = 1;
            }
            const size_t burstSize = static_cast<size_t>(optionalNumberToUint(tryGetNumber(*arrivalPattern, "burst_size"), 1));
            const uint64_t spacingMs = readDurationMs(*arrivalPattern, "burst_spacing_ms", 1);
            uint64_t burstStart = startMs;
            size_t burstIndex = 0;
            while (burstStart <= horizonMs) {
                for (size_t i = 0; i < burstSize; ++i) {
                    appendArrival(burstStart + i * spacingMs);
                }
                ++burstIndex;
                burstStart = startMs + intervalMs * burstIndex;
                if (intervalMs == 0) {
                    break;
                }
            }
        } else if (patternType == "burst_every_s") {
            uint64_t intervalMs = optionalNumberToUint(tryGetNumber(*arrivalPattern, "value"), 1) * 1000;
            if (intervalMs == 0) {
                intervalMs = 1000;
            }
            const size_t burstCount = static_cast<size_t>(optionalNumberToUint(tryGetNumber(*arrivalPattern, "burst_count"), 1));
            const uint64_t spacingMs = readDurationMs(*arrivalPattern, "burst_spacing_ms", 1);
            uint64_t burstStart = startMs;
            size_t burstIndex = 0;
            while (burstStart <= horizonMs) {
                for (size_t i = 0; i < burstCount; ++i) {
                    appendArrival(burstStart + i * spacingMs);
                }
                ++burstIndex;
                burstStart = startMs + intervalMs * burstIndex;
            }
        } else {
            uint64_t periodMs = extractDurationMs(taskConfig, "period", 0);
            if (patternType == "periodic") {
                periodMs = extractDurationMs(*arrivalPattern, "period", periodMs);
            } else if (patternType == "poisson") {
                periodMs = readDurationMs(*arrivalPattern, "mean_ms", periodMs ? periodMs : 50);
            }
            if (periodMs == 0) {
                appendArrival(startMs);
            } else {
                uint64_t current = startMs;
                size_t occurrence = 0;
                while (current <= horizonMs) {
                    appendArrival(applyJitterPercent(current, jitterPercent, occurrence, periodMs));
                    ++occurrence;
                    current = startMs + periodMs * occurrence;
                }
            }
        }
    } else {
        uint64_t periodMs = extractDurationMs(taskConfig, "period", 0);
        if (periodMs == 0) {
            appendArrival(startMs);
        } else {
            uint64_t current = startMs;
            size_t occurrence = 0;
            while (current <= horizonMs) {
                appendArrival(applyJitterPercent(current, jitterPercent, occurrence, periodMs));
                ++occurrence;
                current = startMs + periodMs * occurrence;
            }
        }
    }

    std::sort(arrivals.begin(), arrivals.end());
    arrivals.erase(std::unique(arrivals.begin(), arrivals.end()), arrivals.end());
    return arrivals;
}

} // namespace

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
void SimulationEngine::loadWorkload(const json& workloadConfig, uint64_t horizonMs)
{
    if (!workloadConfig.contains("tasks") || !workloadConfig["tasks"].is_array()) {
        SPDLOG_WARN("Workload config missing 'tasks' array; skipping workload.");
        return;
    }

    const auto& tasksArray = workloadConfig["tasks"];

    struct ExpandedTask {
        const json* config = nullptr;
        std::vector<uint64_t> arrivals;
    };

    std::vector<ExpandedTask> expanded;
    expanded.reserve(tasksArray.size());
    size_t instanceCount = 0;

    for (const auto& taskConfig : tasksArray) {
        if (!taskConfig.is_object()) {
            SPDLOG_WARN("Skipping malformed task entry (expected object, got {})", taskConfig.type_name());
            continue;
        }
        auto arrivals = generateArrivalSchedule(taskConfig, horizonMs);
        if (arrivals.empty()) {
            continue;
        }
        instanceCount += arrivals.size();
        expanded.push_back(ExpandedTask { &taskConfig, std::move(arrivals) });
    }

    tasks_.clear();
    tasks_.reserve(instanceCount);
    eventQueue_ = std::priority_queue<Event, std::vector<Event>, EventCompare>();

    size_t templateCount = expanded.size();
    uint64_t totalExecMs = 0;

    for (const auto& entry : expanded) {
        const json& taskConfig = *entry.config;
        const std::vector<uint64_t>& arrivals = entry.arrivals;

        const std::string baseName = getStringOr(taskConfig, "name", "task_template");
        const std::string taskClass = getStringOr(taskConfig, "class", "background");
        const int priority = getIntOr(taskConfig, "priority", 5);
        const auto execRangeRaw = extractDurationRangeMs(taskConfig, "exec", { 5u, 5u });
        const uint64_t deadlineMs = extractDurationMs(taskConfig, "deadline", 0);

        size_t occurrenceIndex = 0;
        for (uint64_t arrivalMs : arrivals) {
            const uint64_t execDurationMs = selectExecDurationMs(execRangeRaw, occurrenceIndex);

            Task instance;
            instance.id = static_cast<int>(tasks_.size());
            instance.name = baseName + "#" + std::to_string(occurrenceIndex + 1);
            instance.type = taskClass;
            instance.priority = priority;
            instance.arrivalTime = arrivalMs;
            instance.execTime = { execDurationMs, execDurationMs };
            instance.deadline = deadlineMs;
            instance.remainingTime = execDurationMs;
            instance.state = TaskState::NEW;

            tasks_.push_back(instance);
            Task& stored = tasks_.back();

            metrics_.registerTaskDefinition(stored);
            eventQueue_.push(Event(EventType::TASK_ARRIVAL, arrivalMs, &stored));

            totalExecMs += execDurationMs;
            ++occurrenceIndex;

            SPDLOG_TRACE("Scheduled task '{}' arrival @ {} ms (exec={} ms)", stored.name, arrivalMs, execDurationMs);
        }
    }

    metrics_.setWorkloadMetadata({
        { "task_templates", templateCount },
        { "task_instances", instanceCount },
        { "total_requested_exec_ms", totalExecMs },
        { "horizon_ms", horizonMs }
    });
}

// ---------- Configure Scenario ----------
void SimulationEngine::configureScenario(const json& scenarioConfig, uint64_t plannedDurationMs)
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
    plannedRunDurationMs_ = plannedDurationMs;

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
        core.idle = true;
        core.idleStart = currentTime_;
    }

    metrics_.setCoreCount(numCores_);
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
        { "verbose_logging", verbose_ },
        { "planned_run_duration_ms", plannedRunDurationMs_ }
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
    plannedRunDurationMs_ = durationMs;

    metrics_.startSimulation(currentTime_);
    for (auto& core : cores_) {
        core.idle = true;
        core.idleStart = currentTime_;
        core.currentTask = nullptr;
        core.busyUntil = currentTime_;
    }

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
        try {
            handleEvent(e);
            dispatchTasks();
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("Exception while processing event at {} ms: {}", currentTime_, ex.what());
            throw;
        }
    }

    // Wrap up
    SPDLOG_INFO("[Engine] Simulation complete at {} ms", currentTime_);
    scheduler_->printStats();
    const uint64_t finalTime = std::max(currentTime_, plannedRunDurationMs_);
    for (size_t coreIndex = 0; coreIndex < cores_.size(); ++coreIndex) {
        auto& core = cores_[coreIndex];
        if (core.idle && core.idleStart < finalTime) {
            metrics_.recordCoreIdle(coreIndex, core.idleStart, finalTime);
        }
    }
    metrics_.finalize(finalTime);
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
                    core.idle = true;
                    core.idleStart = e.timestamp;
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
                if (core.idle && core.idleStart < currentTime_) {
                    metrics_.recordCoreIdle(coreIndex, core.idleStart, currentTime_);
                }
                core.idle = false;
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
                if (!core.idle) {
                    core.idle = true;
                    core.idleStart = currentTime_;
                }
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
