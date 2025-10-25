/**
 * @file SimulationEngine.cpp
 * @author Andrew McDaniel
 * @brief Implementation of the simulation engine orchestrating tasks and events.
 *
 * Loads workloads, schedules events, coordinates scheduler callbacks, and records metrics for
 * later analysis.
 */
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

namespace {

// Helper functions for SimulationEngine.cpp

/**
 * @brief Convert an optional numeric value to uint64_t, with fallback
 *
 * @param value
 * @param fallback
 * @return uint64_t
 */
uint64_t optionalNumberToUint(const std::optional<double>& value, uint64_t fallback)
{
    if (!value) {
        return fallback;
    }
    const double rawValue = *value;
    if (rawValue < 0.0) {
        return fallback;
    }
    return static_cast<uint64_t>(std::llround(rawValue));
}

/**
 * @brief Apply jitter percentage to a base time
 *
 * @param baseTimeMs
 * @param jitterPercent
 * @param occurrenceIndex
 * @param periodMs
 * @return uint64_t
 */
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

/**
 * @brief Select execution duration within a range based on occurrence index
 *
 * @param range
 * @param occurrenceIndex
 * @return uint64_t
 */
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

/**
 * @brief Generate arrival schedule based on task configuration and horizon.
 *
 * @param taskConfig
 * @param horizonMs
 * @return std::vector<uint64_t>
 */
std::vector<uint64_t> generateArrivalSchedule(const json& taskConfig, uint64_t horizonMs)
{
    std::vector<uint64_t> arrivals;
    if (horizonMs == 0) {
        return arrivals;
    }

    const uint64_t startMs = configUtils::extractDurationMs(taskConfig, "start", configUtils::extractDurationMs(taskConfig, "arrival", 0));
    if (startMs > horizonMs) {
        return arrivals;
    }

    const uint64_t jitterPercent = optionalNumberToUint(configUtils::tryGetNumber(taskConfig, "jitter_percent"), 0);

    const json* arrivalPattern = nullptr;
    if (auto patternIt = taskConfig.find("arrival_pattern"); patternIt != taskConfig.end() && patternIt->is_object()) {
        arrivalPattern = &(*patternIt);
    }

    auto readDurationMs = [&](const json& obj, std::string_view key, uint64_t fallback) {
        if (auto asDuration = configUtils::tryGetNumber(obj, key)) {
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
            const size_t burstSize = static_cast<size_t>(optionalNumberToUint(configUtils::tryGetNumber(*arrivalPattern, "burst_size"), 1));
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
            uint64_t intervalMs = optionalNumberToUint(configUtils::tryGetNumber(*arrivalPattern, "value"), 1) * 1000;
            if (intervalMs == 0) {
                intervalMs = 1000;
            }
            const size_t burstCount = static_cast<size_t>(optionalNumberToUint(configUtils::tryGetNumber(*arrivalPattern, "burst_count"), 1));
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
            uint64_t periodMs = configUtils::extractDurationMs(taskConfig, "period", 0);
            if (patternType == "periodic") {
                periodMs = configUtils::extractDurationMs(*arrivalPattern, "period", periodMs);
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
        uint64_t periodMs = configUtils::extractDurationMs(taskConfig, "period", 0);
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

/**
 * @brief Construct a new SimulationEngine object.
 *
 * @param scheduler Pointer to scheduler instance to use.
 */
SimulationEngine::SimulationEngine(std::unique_ptr<IScheduler> scheduler)
    : scheduler_(std::move(scheduler))
{
    if (!this->scheduler_) {
        throw std::invalid_argument("SimulationEngine requires a valid scheduler");
    }
    this->cores_.resize(1); // default single-core
    this->metrics_.reset();
    SPDLOG_DEBUG("SimulationEngine initialized with {} core(s)", this->cores_.size());
}

/**
 * @brief Load workload and schedule task arrivals
 *
 * @param workloadConfig
 * @param horizonMs
 */
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
            SPDLOG_WARN("Skipping poorly formed task entry (expected object, got {})", taskConfig.type_name());
            continue;
        }
        auto arrivals = generateArrivalSchedule(taskConfig, horizonMs);
        if (arrivals.empty()) {
            continue;
        }
        instanceCount += arrivals.size();
        expanded.push_back(ExpandedTask { &taskConfig, std::move(arrivals) });
    }

    this->tasks_.clear();
    this->tasks_.reserve(instanceCount);
    this->eventQueue_ = std::priority_queue<Event, std::vector<Event>, EventCompare>();

    size_t templateCount = expanded.size();
    uint64_t totalExecMs = 0;

    for (const auto& entry : expanded) {
        const json& taskConfig = *entry.config;
        const std::vector<uint64_t>& arrivals = entry.arrivals;

        const std::string baseName = configUtils::getStringOr(taskConfig, "name", "task_template");
        const std::string taskClass = configUtils::getStringOr(taskConfig, "class", "background");
        const TaskType taskType = taskTypeFromString(taskClass);
        const int priority = configUtils::getIntOr(taskConfig, "priority", 5);
        const auto execRangeRaw = configUtils::extractDurationRangeMs(taskConfig, "exec", { 5u, 5u });
        const uint64_t deadlineMs = configUtils::extractDurationMs(taskConfig, "deadline", 0);

        size_t occurrenceIndex = 0;
        for (uint64_t arrivalMs : arrivals) {
            const uint64_t execDurationMs = selectExecDurationMs(execRangeRaw, occurrenceIndex);

            Task instance;
            instance.id = static_cast<int>(this->tasks_.size());
            instance.name = baseName + "#" + std::to_string(occurrenceIndex + 1);
            instance.type = taskType;
            instance.priority = priority;
            instance.arrivalTime = arrivalMs;
            instance.execTime = { execDurationMs, execDurationMs };
            instance.deadline = deadlineMs;
            instance.remainingTime = execDurationMs;
            instance.state = TaskState::NEW;

            this->tasks_.push_back(instance);
            Task& stored = this->tasks_.back();

            this->metrics_.registerTaskDefinition(stored);
            this->eventQueue_.push(Event(EventType::TASK_ARRIVAL, arrivalMs, &stored));

            totalExecMs += execDurationMs;
            ++occurrenceIndex;

            SPDLOG_TRACE("Scheduled task '{}' arrival @ {} ms (exec={} ms)", stored.name, arrivalMs, execDurationMs);
        }
    }

    this->metrics_.setWorkloadMetadata({ { "task_templates", templateCount },
        { "task_instances", instanceCount },
        { "total_requested_exec_ms", totalExecMs },
        { "horizon_ms", horizonMs } });
}

/**
 * @brief Configure simulation scenario parameters
 *
 * @param scenarioConfig
 * @param plannedDurationMs
 */
void SimulationEngine::configureScenario(const json& scenarioConfig, uint64_t plannedDurationMs)
{
    // Create references to relevant config sections
    const json schedulerCfg = scenarioConfig.value("scheduler", json::object());
    const json hardwareCfg = scenarioConfig.value("hardware", json::object());
    const json loggingCfg = scenarioConfig.value("logging", json::object());
    const json systemCfg = scenarioConfig.value("system", json::object());
    const json timingCfg = scenarioConfig.value("timing", json::object());

    // Extract scenario parameters. Fall back to defaults or previously set values.
    this->systemName_ = configUtils::getStringOr(systemCfg, "name", this->systemName_);
    this->clockSpeedMhz_ = configUtils::tryGetNumber(systemCfg, "clock_speed_mhz").value_or(this->clockSpeedMhz_);
    this->basePowerWatts_ = configUtils::tryGetNumber(systemCfg, "base_power_watts").value_or(this->basePowerWatts_);
    this->maxPowerWatts_ = configUtils::tryGetNumber(systemCfg, "max_power_watts").value_or(this->maxPowerWatts_);
    this->idlePowerWatts_ = configUtils::tryGetNumber(systemCfg, "idle_power_watts").value_or(this->idlePowerWatts_);
    this->plannedRunDurationMs_ = plannedDurationMs;

    // Resolve 'cores' from several possible config locations (falls back to 1.0)
    const std::pair<const json*, std::string_view> coreSources[] = {
        { &schedulerCfg, "cores" },
        { &systemCfg, "cores" },
        { &hardwareCfg, "cores" },
        { &scenarioConfig, "cores" },
        { &scenarioConfig, "num_cores" },
    };

    std::optional<double> resolvedCores;
    for (const auto& src : coreSources) {
        const json* obj = src.first;
        if (!obj)
            continue;
        if (auto v = configUtils::tryGetNumber(*obj, src.second)) {
            resolvedCores = v;
            break;
        }
    }

    const double coresCandidate = resolvedCores.value_or(1.0);

    this->numCores_ = static_cast<size_t>(std::max<int>(1, static_cast<int>(std::llround(coresCandidate))));

    const uint64_t scenarioContextSwitchUs = configUtils::extractDurationUs(timingCfg, "context_switch_cost",
        configUtils::extractDurationUs(scenarioConfig, "context_switch_cost", this->contextSwitchCostUs_));
    this->contextSwitchCostUs_ = configUtils::extractDurationUs(schedulerCfg, "context_switch_cost", scenarioContextSwitchUs);

    this->ioCompletionQuantumUs_ = configUtils::extractDurationUs(timingCfg, "io_completion_quantum",
        configUtils::extractDurationUs(scenarioConfig, "io_completion_quantum", this->ioCompletionQuantumUs_));
    this->tickIntervalUs_ = configUtils::extractDurationUs(timingCfg, "tick_interval",
        configUtils::extractDurationUs(scenarioConfig, "tick_interval", this->tickIntervalUs_));
    this->tickIntervalMs_ = this->tickIntervalUs_ == 0 ? 0 : std::max<uint64_t>(1, (this->tickIntervalUs_ + 999) / 1000);

    this->verbose_ = configUtils::getBoolOr(schedulerCfg, "verbose",
        configUtils::getBoolOr(loggingCfg, "verbose",
            configUtils::getBoolOr(scenarioConfig, "verbose", false)));

    this->cores_.resize(this->numCores_);
    for (auto& core : this->cores_) {
        core.currentTask = nullptr;
        core.busyUntil = 0;
        core.idle = true;
        core.idleStart = this->currentTime_;
    }

    this->metrics_.setCoreCount(this->numCores_);
    this->metrics_.setScenarioMetadata({ { "system_name", this->systemName_ },
        { "clock_speed_mhz", this->clockSpeedMhz_ },
        { "base_power_watts", this->basePowerWatts_ },
        { "max_power_watts", this->maxPowerWatts_ },
        { "idle_power_watts", this->idlePowerWatts_ },
        { "num_cores", this->numCores_ },
        { "context_switch_cost_us", this->contextSwitchCostUs_ },
        { "tick_interval_us", this->tickIntervalUs_ },
        { "io_completion_quantum_us", this->ioCompletionQuantumUs_ },
        { "verbose_logging", this->verbose_ },
        { "planned_run_duration_ms", this->plannedRunDurationMs_ } });

    SPDLOG_INFO("Scenario configured: system='{}', cores={}, context_switch_cost_us={}, tick_interval_us={}, io_quantum_us={}, verbose={}",
        this->systemName_,
        this->numCores_,
        this->contextSwitchCostUs_,
        this->tickIntervalUs_,
        this->ioCompletionQuantumUs_,
        this->verbose_);
}

/**
 * @brief Run the full simulation for a given duration (ms)
 *
 * @param durationMs
 */
void SimulationEngine::run(uint64_t durationMs)
{
    SPDLOG_INFO("[Simulation Engine] Starting simulation ({} ms)", durationMs);
    this->currentTime_ = 0;
    this->plannedRunDurationMs_ = durationMs;

    this->metrics_.startSimulation(this->currentTime_);
    for (auto& core : this->cores_) {
        core.idle = true;
        core.idleStart = this->currentTime_;
        core.currentTask = nullptr;
        core.busyUntil = this->currentTime_;
    }

    if (this->tickIntervalMs_ != 0) {
        this->scheduleTimerTick(this->currentTime_);
    }

    while (!this->eventQueue_.empty() && this->currentTime_ <= durationMs) {
        Event scheduledEvent = this->eventQueue_.top();
        this->eventQueue_.pop();

        // Advance simulation clock
        this->currentTime_ = scheduledEvent.timestamp;
        SPDLOG_DEBUG("Advancing to {} ms -> processing event type {} for task {}",
            this->currentTime_,
            static_cast<int>(scheduledEvent.type),
            scheduledEvent.task ? scheduledEvent.task->name : "<null>");
        try {
            this->handleEvent(scheduledEvent);
            dispatchTasks();
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("Exception while processing event at {} ms: {}", this->currentTime_, ex.what());
            throw;
        }
    }

    // Wrap up
    SPDLOG_INFO("[Engine] Simulation complete at {} ms", this->currentTime_);
    this->scheduler_->printStats();
    const uint64_t finalTime = std::max(this->currentTime_, this->plannedRunDurationMs_);
    for (size_t coreIndex = 0; coreIndex < this->cores_.size(); ++coreIndex) {
        auto& core = this->cores_[coreIndex];
        if (core.idle && core.idleStart < finalTime) {
            this->metrics_.recordCoreIdle(coreIndex, core.idleStart, finalTime);
        }
    }
    this->metrics_.finalize(finalTime);
}

/**
 * @brief Handle a scheduled event
 *
 * @param eventRecord
 */
void SimulationEngine::handleEvent(const Event& eventRecord)
{
    switch (eventRecord.type) {
    case EventType::TASK_ARRIVAL:
        SPDLOG_DEBUG("Handling TASK_ARRIVAL for task {}", eventRecord.task ? eventRecord.task->name : "<null>");
        if (eventRecord.task) {
            eventRecord.task->state = TaskState::READY;
            this->metrics_.recordTaskArrival(*eventRecord.task, eventRecord.timestamp);
            this->scheduler_->onTaskArrival(*eventRecord.task);
        }
        break;

    case EventType::TASK_COMPLETION:
        SPDLOG_DEBUG("Handling TASK_COMPLETION for task {}", eventRecord.task ? eventRecord.task->name : "<null>");
        if (eventRecord.task) {
            eventRecord.task->state = TaskState::COMPLETED;
            eventRecord.task->remainingTime = 0;
            this->metrics_.recordTaskCompletion(*eventRecord.task, eventRecord.timestamp);

            for (auto& core : this->cores_) {
                if (core.currentTask == eventRecord.task) {
                    core.currentTask = nullptr;
                    core.busyUntil = eventRecord.timestamp;
                    core.idle = true;
                    core.idleStart = eventRecord.timestamp;
                }
            }

            this->scheduler_->onTaskCompletion(*eventRecord.task);
        }
        break;

    case EventType::IO_COMPLETION:
        SPDLOG_DEBUG("Handling IO_COMPLETION for task {}", eventRecord.task ? eventRecord.task->name : "<null>");
        if (eventRecord.task) {
            eventRecord.task->state = TaskState::READY;
            this->metrics_.recordIoCompletion(*eventRecord.task, eventRecord.timestamp);
            this->scheduler_->onIOCompletion(*eventRecord.task);
        }
        break;

    case EventType::TIME_SLICE_EXPIRE:
        SPDLOG_TRACE("Handling TIME_SLICE_EXPIRE for task {}", eventRecord.task ? eventRecord.task->name : "<null>");
        if (eventRecord.task) {
            for (auto& core : this->cores_) {
                if (core.currentTask == eventRecord.task) {
                    core.currentTask = nullptr;
                    core.busyUntil = eventRecord.timestamp;
                    core.idle = true;
                    core.idleStart = eventRecord.timestamp;
                }
            }
            eventRecord.task->state = TaskState::READY;
            this->scheduler_->onTimeSliceExpired(*eventRecord.task);
        }
        break;

    case EventType::TIMER_TICK:
        SPDLOG_TRACE("Handling TIMER_TICK @ {} ms", eventRecord.timestamp);
        this->metrics_.recordTimerTick(eventRecord.timestamp, this->currentCoreAssignments(), this->tasks_, this->eventQueue_.size());
        this->scheduleTimerTick(eventRecord.timestamp);
        break;

    default:
        SPDLOG_WARN("Received unknown event type {}", static_cast<int>(eventRecord.type));
        break;
    }
}

/**
 * @brief Dispatch tasks to available cores
 *
 */
void SimulationEngine::dispatchTasks()
{
    // Iterate over cores and assign tasks as needed
    for (size_t coreIndex = 0; coreIndex < this->cores_.size(); ++coreIndex) {
        // Create reference to the current core
        auto& core = this->cores_[coreIndex];
        // Check if core is available
        if (core.currentTask == nullptr || core.busyUntil <= this->currentTime_) {
            // Pick next task from scheduler
            Task* nextTask = this->scheduler_->pickNextTask();
            if (nextTask) {
                // Assign task to core
                if (core.idle && core.idleStart < this->currentTime_) {
                    this->metrics_.recordCoreIdle(coreIndex, core.idleStart, this->currentTime_);
                }
                core.idle = false;
                if (nextTask->remainingTime == 0) {
                    const uint64_t fallback = nextTask->execTime.second ? nextTask->execTime.second : (nextTask->execTime.first ? nextTask->execTime.first : 1);
                    nextTask->remainingTime = fallback;
                }

                // Determine run duration
                const uint64_t requestedSlice = nextTask->currentTimeSliceMs ? std::min(nextTask->currentTimeSliceMs, nextTask->remainingTime) : nextTask->remainingTime;
                const uint64_t runDuration = std::max<uint64_t>(1, std::min(requestedSlice, nextTask->remainingTime));
                uint64_t remainingAfter = nextTask->remainingTime > runDuration ? nextTask->remainingTime - runDuration : 0;

                // Update task and core state
                nextTask->state = TaskState::RUNNING;
                nextTask->currentTimeSliceMs = 0;
                nextTask->lastRunDurationMs = runDuration;
                nextTask->remainingTime = remainingAfter;

                // Schedule completion or time slice expiration event
                core.currentTask = nextTask;
                core.busyUntil = this->currentTime_ + runDuration;

                // Record dispatch in metrics
                this->metrics_.recordTaskDispatch(*nextTask, coreIndex, this->currentTime_, core.busyUntil, this->contextSwitchCostUs_);

                // Schedule event
                EventType completionType = remainingAfter == 0 ? EventType::TASK_COMPLETION : EventType::TIME_SLICE_EXPIRE;
                Event completionEvent(completionType, core.busyUntil, nextTask);
                this->eventQueue_.push(completionEvent);

                SPDLOG_DEBUG("Dispatching task {} on core {} for {} ms -> completes@ {} (remaining {})",
                    nextTask->name,
                    coreIndex,
                    runDuration,
                    core.busyUntil,
                    remainingAfter);

                if (this->verbose_)
                    SPDLOG_DEBUG("Dispatching task " + nextTask->name + " for " + std::to_string(runDuration) + "ms -> finishes @ " + std::to_string(core.busyUntil));
            } else {
                // No task available; core idle
                SPDLOG_TRACE("Core {} idle at {} ms (no task available)", coreIndex, this->currentTime_);
                core.currentTask = nullptr;
                core.busyUntil = this->currentTime_;
                if (!core.idle) {
                    core.idle = true;
                    core.idleStart = this->currentTime_;
                }
            }
        }
    }
}

/**
 * @brief Export simulation results as JSON
 *
 * @return json
 */
json SimulationEngine::exportResults() const
{
    return this->metrics_.buildReport();
}

/**
 * @brief Schedule the next timer tick event
 *
 * @param startTimeMs
 */
void SimulationEngine::scheduleTimerTick(uint64_t startTimeMs)
{
    if (this->tickIntervalMs_ == 0) {
        return;
    }

    const uint64_t nextTick = startTimeMs + this->tickIntervalMs_;
    this->eventQueue_.push(Event(EventType::TIMER_TICK, nextTick, nullptr));
}

/**
 * @brief Get current core assignments as a vector of task IDs (-1 for idle)
 *
 * @return std::vector<int>
 */
std::vector<int> SimulationEngine::currentCoreAssignments() const
{
    std::vector<int> assignments;
    assignments.reserve(this->cores_.size());
    for (const auto& core : this->cores_) {
        assignments.push_back(core.currentTask ? core.currentTask->id : -1);
    }
    return assignments;
}
