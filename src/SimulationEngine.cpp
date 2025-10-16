#include "SimulationEngine.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <optional>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string_view>

namespace {
using json = nlohmann::json;

std::optional<double> tryGetNumber(const json& obj, std::string_view key)
{
    if (!obj.is_object()) {
        return std::nullopt;
    }
    const std::string keyStr(key);
    const auto it = obj.find(keyStr);
    if (it == obj.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_number_float()) {
        return it->get<double>();
    }
    if (it->is_number_integer()) {
        return static_cast<double>(it->get<int64_t>());
    }
    if (it->is_string()) {
        const std::string text = it->get<std::string>();
        try {
            size_t idx = 0;
            const double value = std::stod(text, &idx);
            if (idx == text.size()) {
                return value;
            }
        } catch (...) {
        }
    }
    SPDLOG_WARN("Field '{}' is not numeric; ignoring entry", keyStr);
    return std::nullopt;
}

std::optional<bool> tryGetBool(const json& obj, std::string_view key)
{
    if (!obj.is_object()) {
        return std::nullopt;
    }
    const std::string keyStr(key);
    const auto it = obj.find(keyStr);
    if (it == obj.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_string()) {
        std::string text = it->get<std::string>();
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text == "true" || text == "1" || text == "yes") {
            return true;
        }
        if (text == "false" || text == "0" || text == "no") {
            return false;
        }
    }
    SPDLOG_WARN("Field '{}' is not boolean; ignoring entry", keyStr);
    return std::nullopt;
}

std::optional<std::string> tryGetString(const json& obj, std::string_view key)
{
    if (!obj.is_object()) {
        return std::nullopt;
    }
    const std::string keyStr(key);
    const auto it = obj.find(keyStr);
    if (it == obj.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    SPDLOG_WARN("Field '{}' is not a string; ignoring entry", keyStr);
    return std::nullopt;
}

std::optional<double> asNumber(const json& node)
{
    if (node.is_number_float()) {
        return node.get<double>();
    }
    if (node.is_number_integer()) {
        return static_cast<double>(node.get<int64_t>());
    }
    if (node.is_string()) {
        const std::string text = node.get<std::string>();
        try {
            size_t idx = 0;
            const double value = std::stod(text, &idx);
            if (idx == text.size()) {
                return value;
            }
        } catch (...) {
        }
    }
    return std::nullopt;
}

uint64_t toMs(double value)
{
    return static_cast<uint64_t>(std::llround(value));
}

uint64_t toUs(double value)
{
    return static_cast<uint64_t>(std::llround(value));
}

std::pair<uint64_t, uint64_t> extractDurationRangeMs(const json& obj, std::string_view keyBase, std::pair<uint64_t, uint64_t> defaultRange)
{
    uint64_t minMs = defaultRange.first;
    uint64_t maxMs = defaultRange.second;

    auto clampRange = [&](double minValMs, double maxValMs) {
        uint64_t minCandidate = toMs(minValMs);
        uint64_t maxCandidate = toMs(maxValMs);
        if (minCandidate == 0 && maxCandidate != 0) {
            minCandidate = maxCandidate;
        }
        if (maxCandidate == 0 && minCandidate != 0) {
            maxCandidate = minCandidate;
        }
        if (minCandidate == 0 && maxCandidate == 0) {
            minCandidate = std::max<uint64_t>(1, defaultRange.first ? defaultRange.first : 1);
            maxCandidate = std::max<uint64_t>(minCandidate, defaultRange.second ? defaultRange.second : minCandidate);
        }
        if (minCandidate > maxCandidate) {
            std::swap(minCandidate, maxCandidate);
        }
        minMs = minCandidate;
        maxMs = maxCandidate;
    };

    auto readWithScale = [&](const json& container, const char* key, double scale) -> std::optional<double> {
        const auto it = container.find(key);
        if (it != container.end()) {
            if (auto value = asNumber(*it)) {
                return *value * scale;
            }
        }
        return std::nullopt;
    };

    auto parseNode = [&](const json& node, double defaultScale) -> bool {
        bool localFound = false;
        double localMin = 0.0;
        double localMax = 0.0;

        if (node.is_object()) {
            if (auto minVal = readWithScale(node, "min", defaultScale)) {
                localMin = *minVal;
                localFound = true;
            }
            if (auto minVal = readWithScale(node, "min_ms", 1.0)) {
                localMin = *minVal;
                localFound = true;
            }
            if (auto minVal = readWithScale(node, "min_us", 0.001)) {
                localMin = *minVal;
                localFound = true;
            }

            if (auto maxVal = readWithScale(node, "max", defaultScale)) {
                localMax = *maxVal;
                localFound = true;
            }
            if (auto maxVal = readWithScale(node, "max_ms", 1.0)) {
                localMax = *maxVal;
                localFound = true;
            }
            if (auto maxVal = readWithScale(node, "max_us", 0.001)) {
                localMax = *maxVal;
                localFound = true;
            }

            if (!localFound) {
                if (auto meanVal = readWithScale(node, "mean", defaultScale)) {
                    localMin = localMax = *meanVal;
                    localFound = true;
                } else if (auto meanVal = readWithScale(node, "mean_ms", 1.0)) {
                    localMin = localMax = *meanVal;
                    localFound = true;
                } else if (auto meanVal = readWithScale(node, "mean_us", 0.001)) {
                    localMin = localMax = *meanVal;
                    localFound = true;
                } else if (auto valueVal = readWithScale(node, "value", defaultScale)) {
                    localMin = localMax = *valueVal;
                    localFound = true;
                }
            }
        } else if (auto numeric = asNumber(node)) {
            localMin = localMax = *numeric * defaultScale;
            localFound = true;
        }

        if (localFound) {
            const double fallback = localMax != 0.0 ? localMax : (localMin != 0.0 ? localMin : static_cast<double>(defaultRange.second));
            const double resolvedMin = localMin != 0.0 ? localMin : fallback;
            const double resolvedMax = localMax != 0.0 ? localMax : fallback;
            clampRange(resolvedMin, resolvedMax);
        }
        return localFound;
    };

    const std::string base(keyBase);
    bool updated = false;

    if (const auto it = obj.find(base + "_ms"); it != obj.end()) {
        updated = parseNode(*it, 1.0);
    }
    if (!updated) {
        if (const auto it = obj.find(base + "_us"); it != obj.end()) {
            updated = parseNode(*it, 0.001);
        }
    }
    if (!updated) {
        if (const auto it = obj.find(base); it != obj.end()) {
            updated = parseNode(*it, 1.0);
            if (!updated) {
                updated = parseNode(*it, 0.001);
            }
        }
    }

    if (!updated) {
        clampRange(defaultRange.first, defaultRange.second);
    }

    return { minMs, maxMs };
}

uint64_t extractDurationMs(const json& obj, std::string_view keyBase, uint64_t defaultValue)
{
    if (!obj.is_object()) {
        return defaultValue;
    }
    const std::string base(keyBase);
    const std::string msKey = base + "_ms";
    if (auto msVal = tryGetNumber(obj, msKey)) {
        return toMs(*msVal);
    }
    const std::string sKey = base + "_s";
    if (auto sVal = tryGetNumber(obj, sKey)) {
        return toMs(*sVal * 1000.0);
    }
    const std::string usKey = base + "_us";
    if (auto usVal = tryGetNumber(obj, usKey)) {
        return toMs(*usVal / 1000.0);
    }
    const auto usObjIt = obj.find(usKey);
    if (usObjIt != obj.end() && usObjIt->is_object()) {
        if (auto maxVal = tryGetNumber(*usObjIt, "max")) {
            return toMs(*maxVal / 1000.0);
        }
        if (auto meanVal = tryGetNumber(*usObjIt, "mean")) {
            return toMs(*meanVal / 1000.0);
        }
        if (auto valueVal = tryGetNumber(*usObjIt, "value")) {
            return toMs(*valueVal / 1000.0);
        }
        if (auto minVal = tryGetNumber(*usObjIt, "min")) {
            return toMs(*minVal / 1000.0);
        }
    }
    const auto baseIt = obj.find(base);
    if (baseIt != obj.end() && baseIt->is_object()) {
        const json& nested = *baseIt;
        if (auto meanMs = tryGetNumber(nested, "mean_ms")) {
            return toMs(*meanMs);
        }
        if (auto offsetMs = tryGetNumber(nested, "offset_ms")) {
            return toMs(*offsetMs);
        }
        if (auto meanUs = tryGetNumber(nested, "mean_us")) {
            return toMs(*meanUs / 1000.0);
        }
        if (nested.contains("type")) {
            SPDLOG_DEBUG("Duration '{}' described as distribution '{}'; using representative value", base, nested.value("type", std::string("unknown")));
        }
    }
    return defaultValue;
}

uint64_t extractDurationUs(const json& obj, std::string_view keyBase, uint64_t defaultValue)
{
    if (!obj.is_object()) {
        return defaultValue;
    }
    const std::string base(keyBase);
    const std::string usKey = base + "_us";
    if (auto usVal = tryGetNumber(obj, usKey)) {
        return toUs(*usVal);
    }
    const std::string msKey = base + "_ms";
    if (auto msVal = tryGetNumber(obj, msKey)) {
        return toUs(*msVal * 1000.0);
    }
    const auto baseIt = obj.find(base);
    if (baseIt != obj.end() && baseIt->is_object()) {
        const json& nested = *baseIt;
        if (auto valueUs = tryGetNumber(nested, "us")) {
            return toUs(*valueUs);
        }
        if (auto valueMs = tryGetNumber(nested, "ms")) {
            return toUs(*valueMs * 1000.0);
        }
    }
    return defaultValue;
}

int getIntOr(const json& obj, std::string_view key, int defaultValue)
{
    if (auto value = tryGetNumber(obj, key)) {
        return static_cast<int>(std::llround(*value));
    }
    return defaultValue;
}

std::string getStringOr(const json& obj, std::string_view key, std::string defaultValue)
{
    if (auto value = tryGetString(obj, key)) {
        return *value;
    }
    return defaultValue;
}

bool getBoolOr(const json& obj, std::string_view key, bool defaultValue)
{
    if (auto value = tryGetBool(obj, key)) {
        return *value;
    }
    return defaultValue;
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
}

// ---------- Configure Scenario ----------
void SimulationEngine::configureScenario(const json& scenarioConfig)
{
    const json schedulerCfg = scenarioConfig.value("scheduler", json::object());
    const json hardwareCfg = scenarioConfig.value("hardware", json::object());
    const json loggingCfg = scenarioConfig.value("logging", json::object());

    const double coresCandidate = tryGetNumber(scenarioConfig, "cores")
                                      .value_or(tryGetNumber(scenarioConfig, "num_cores")
                                              .value_or(tryGetNumber(hardwareCfg, "cores").value_or(1.0)));

    numCores_ = static_cast<size_t>(std::max<int>(1, static_cast<int>(std::llround(coresCandidate))));

    const uint64_t scenarioContextSwitchUs = extractDurationUs(scenarioConfig, "context_switch_cost", 15);
    contextSwitchCostUs_ = extractDurationUs(schedulerCfg, "context_switch_cost", scenarioContextSwitchUs);

    verbose_ = getBoolOr(schedulerCfg, "verbose",
        getBoolOr(loggingCfg, "verbose",
            getBoolOr(scenarioConfig, "verbose", false)));

    cores_.resize(numCores_);
    for (auto& core : cores_) {
        core.currentTask = nullptr;
        core.busyUntil = 0;
    }

    SPDLOG_INFO("Scenario configured: cores={}, context_switch_cost_us={}, verbose={}",
        numCores_,
        contextSwitchCostUs_,
        verbose_);
}

// ---------- Run Simulation ----------
void SimulationEngine::run(uint64_t durationMs)
{
    SPDLOG_INFO("[Engine] Starting simulation ({} ms)", durationMs);
    currentTime_ = 0;

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
        scheduler_->onTaskArrival(*e.task);
        metrics_.recordArrival(e.task->id, e.timestamp);
        break;

    case EventType::TASK_COMPLETION:
        SPDLOG_DEBUG("Handling TASK_COMPLETION for task {}", e.task ? e.task->name : "<null>");
        scheduler_->onTaskCompletion(*e.task);
        metrics_.recordCompletion(e.task->id, e.timestamp);
        break;

    case EventType::IO_COMPLETION:
        SPDLOG_DEBUG("Handling IO_COMPLETION for task {}", e.task ? e.task->name : "<null>");
        scheduler_->onIOCompletion(*e.task);
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
                core.currentTask = next;
                core.busyUntil = currentTime_ + next->remainingTime;

                metrics_.recordDispatch(next->id, currentTime_);
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
            }
        }
    }
}

// ---------- Export Results ----------
json SimulationEngine::exportResults() const
{
    json out;
    out["simulation_time_ms"] = currentTime_;
    out["num_cores"] = numCores_;
    out["context_switch_cost_us"] = contextSwitchCostUs_;
    out["metrics"] = metrics_.toJson();
    out["timeline"] = metrics_.exportTimeline();
    return out;
}

// ---------- Log Helper ----------
void SimulationEngine::log(const std::string& msg) const
{
    SPDLOG_DEBUG("[t={} ms] {}", currentTime_, msg);
}
