#include "SimulationEngine.h"
#include <iomanip>
#include <spdlog/spdlog.h>
#include <stdexcept>

// ---------- Constructor ----------
SimulationEngine::SimulationEngine(std::unique_ptr<IScheduler> scheduler)
    : scheduler_(std::move(scheduler))
{
    if (!scheduler_) {
        throw std::invalid_argument("SimulationEngine requires a valid scheduler");
    }
    cores_.resize(1); // default single-core
    spdlog::debug("SimulationEngine initialized with {} core(s)", cores_.size());
}

// ---------- Load Workload ----------
void SimulationEngine::loadWorkload(const json& workloadConfig)
{
    if (!workloadConfig.contains("tasks")) {
        spdlog::warn("No tasks found in workload config.");
        return;
    }

    for (const auto& t : workloadConfig["tasks"]) {
        Task task;
        task.id = tasks_.size();
        task.name = t.value("name", "task_" + std::to_string(task.id));
        task.arrivalTime = t.value("arrival_ms", 0);
        task.execTime = t.value("exec_ms", 10);
        task.priority = t.value("priority", 5);
        task.deadline = t.value("deadline_ms", 0);
        task.type = t.value("class", "background");
        task.remainingTime = task.execTime;
        tasks_.push_back(task);

        spdlog::debug("Loaded task id={} name={} arrival={}ms exec={}ms priority={}",
            task.id,
            task.name,
            task.arrivalTime,
            task.execTime,
            task.priority);

        // Enqueue initial event
        Event e(EventType::TASK_ARRIVAL, task.arrivalTime, &tasks_.back());
        eventQueue_.push(e);
        spdlog::debug("Queued TASK_ARRIVAL at {} ms for task {}", task.arrivalTime, task.name);
    }
}

// ---------- Configure Scenario ----------
void SimulationEngine::configureScenario(const json& scenarioConfig)
{
    numCores_ = scenarioConfig.value("cores", 1);
    contextSwitchCostUs_ = scenarioConfig.value("context_switch_cost_us", 15);
    verbose_ = scenarioConfig.value("verbose", false);
    cores_.resize(numCores_);

    spdlog::info("Scenario configured: cores={}, context_switch_cost_us={}, verbose={}",
        numCores_,
        contextSwitchCostUs_,
        verbose_);
}

// ---------- Run Simulation ----------
void SimulationEngine::run(uint64_t durationMs)
{
    spdlog::info("[Engine] Starting simulation ({} ms)", durationMs);
    currentTime_ = 0;

    while (!eventQueue_.empty() && currentTime_ <= durationMs) {
        Event e = eventQueue_.top();
        eventQueue_.pop();

        // Advance simulation clock
        currentTime_ = e.timestamp;
        spdlog::debug("Advancing to {} ms -> processing event type {} for task {}",
            currentTime_,
            static_cast<int>(e.type),
            e.task ? e.task->name : "<null>");
        handleEvent(e);

        dispatchTasks();
    }

    // Wrap up
    spdlog::info("[Engine] Simulation complete at {} ms", currentTime_);
    scheduler_->printStats();
    metrics_.finalize(currentTime_);
}

// ---------- Handle Events ----------
void SimulationEngine::handleEvent(const Event& e)
{
    switch (e.type) {
    case EventType::TASK_ARRIVAL:
        spdlog::debug("Handling TASK_ARRIVAL for task {}", e.task ? e.task->name : "<null>");
        scheduler_->onTaskArrival(*e.task);
        metrics_.recordArrival(e.task->id, e.timestamp);
        break;

    case EventType::TASK_COMPLETION:
        spdlog::debug("Handling TASK_COMPLETION for task {}", e.task ? e.task->name : "<null>");
        scheduler_->onTaskCompletion(*e.task);
        metrics_.recordCompletion(e.task->id, e.timestamp);
        break;

    case EventType::IO_COMPLETION:
        spdlog::debug("Handling IO_COMPLETION for task {}", e.task ? e.task->name : "<null>");
        scheduler_->onIOCompletion(*e.task);
        break;

    default:
        spdlog::warn("Received unknown event type {}", static_cast<int>(e.type));
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

                spdlog::debug("Dispatching task {} on core {} -> finishes @ {}",
                    next->name,
                    coreIndex,
                    core.busyUntil);

                if (verbose_)
                    log("Dispatching task " + next->name + " -> finishes @ " + std::to_string(core.busyUntil));
            } else {
                spdlog::trace("Core {} idle at {} ms (no task available)", coreIndex, currentTime_);
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
    spdlog::debug("[t={} ms] {}", currentTime_, msg);
}
