/**
 * @file Scheduler_Linux.cpp
 * @author Andrew McDaniel
 * @brief Placeholder for a Linux-inspired scheduler policy.
 *
 * Captures future work to model Linux CFS-style behaviour with niceness weights and latency
 * targets within the simulator.
 */
#include "Scheduler.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace {

constexpr int PRIORITY_MIN = -20;
constexpr int PRIORITY_MAX = 19;

} // namespace

LinuxScheduler::LinuxScheduler(double targetLatencyMs, double minGranularityMs)
    : targetLatencyMs_(std::max(1.0, targetLatencyMs))
    , minGranularityMs_(std::max(0.5, minGranularityMs))
{
    SPDLOG_INFO("[Linux] Initialised (target latency {:.2f} ms, min granularity {:.2f} ms)",
        this->targetLatencyMs_,
        this->minGranularityMs_);
}

bool LinuxScheduler::Comparator::operator()(const QueuedTask& lhs, const QueuedTask& rhs) const
{
    if (lhs.vruntime != rhs.vruntime) {
        return lhs.vruntime > rhs.vruntime;
    }
    return lhs.sequence > rhs.sequence;
}

double LinuxScheduler::weightFor(const Task& task) const
{
    // Treat Task::priority as a "nice" value; lower is higher priority.
    const int nice = std::clamp(task.priority, PRIORITY_MIN, PRIORITY_MAX);
    // Approximate Linux weight curve with a simple exponential scaling.
    const double factor = std::pow(1.25, static_cast<double>(-nice));
    const double weight = this->baseWeight_ * factor;
    return std::max(1.0, weight);
}

void LinuxScheduler::updateVruntime(Task& task)
{
    const double weight = this->weightFor(task);
    auto it = this->vruntime_.find(task.id);
    double vr = (it == this->vruntime_.end()) ? this->minVruntime_ : it->second;

    if (task.lastRunDurationMs > 0) {
        const double delta = static_cast<double>(task.lastRunDurationMs) * (this->baseWeight_ / weight);
        vr += delta;
    }

    this->vruntime_[task.id] = vr;
    this->minVruntime_ = this->readyQueue_.empty() ? vr : std::min(this->minVruntime_, vr);
}

void LinuxScheduler::enqueue(Task& task, std::string_view reason)
{
    // Ensure vruntime is updated before enqueuing
    this->updateVruntime(task);

    const double weight = this->weightFor(task);
    const double vr = this->vruntime_.at(task.id);

    this->readyQueue_.push(QueuedTask { &task, vr, weight, this->nextSequence_++ });
    this->totalWeight_ += weight;
    ++this->totalEnqueued_;
    if (!this->readyQueue_.empty()) {
        this->minVruntime_ = std::min(this->minVruntime_, vr);
    }

    SPDLOG_TRACE("[Linux] Enqueue task {} (id={}) via {} -> depth {} (vruntime {:.3f}, weight {:.1f})",
        task.name,
        task.id,
        reason,
        this->readyQueue_.size(),
        vr,
        weight);
}

void LinuxScheduler::onTaskArrival(Task& task)
{
    SPDLOG_DEBUG("[Linux] Task {} arrived (id={}, prio={})", task.name, task.id, task.priority);
    this->enqueue(task, "arrival");
}

void LinuxScheduler::onTaskCompletion(Task& task)
{
    SPDLOG_DEBUG("[Linux] Task {} completed (id={})", task.name, task.id);
    this->vruntime_.erase(task.id);
}

void LinuxScheduler::onIOCompletion(Task& task)
{
    SPDLOG_TRACE("[Linux] Task {} IO completion -> requeue", task.name);
    this->enqueue(task, "io completion");
}

void LinuxScheduler::onTimeSliceExpired(Task& task)
{
    SPDLOG_TRACE("[Linux] Time slice expired for task {} (id={})", task.name, task.id);
    this->enqueue(task, "time slice expired");
}

uint64_t LinuxScheduler::sliceFor(double weight, double totalWeight) const
{
    if (totalWeight <= 0.0) {
        return static_cast<uint64_t>(std::max(1.0, this->targetLatencyMs_));
    }
    const double share = this->targetLatencyMs_ * (weight / totalWeight);
    const double sliceMs = std::max(this->minGranularityMs_, share);
    return std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(sliceMs)));
}

Task* LinuxScheduler::pickNextTask()
{
    if (this->readyQueue_.empty()) {
        SPDLOG_TRACE("[Linux] Ready queue empty");
        return nullptr;
    }

    const QueuedTask top = this->readyQueue_.top();
    this->readyQueue_.pop();

    const double totalWeightBefore = this->totalWeight_;
    this->totalWeight_ = std::max(0.0, this->totalWeight_ - top.weight);
    ++this->totalDispatched_;

    Task* next = top.task;
    if (!next) {
        SPDLOG_WARN("[Linux] Encountered null task while dispatching");
        return nullptr;
    }

    const uint64_t slice = this->sliceFor(top.weight, totalWeightBefore > 0.0 ? totalWeightBefore : top.weight);
    next->currentTimeSliceMs = slice;
    this->vruntime_[next->id] = top.vruntime;

    if (this->readyQueue_.empty()) {
        this->minVruntime_ = top.vruntime;
    } else {
        this->minVruntime_ = this->readyQueue_.top().vruntime;
    }

    SPDLOG_DEBUG("[Linux] Dispatch task {} (id={}) vruntime {:.3f} weight {:.1f} slice {} ms; queue depth {}",
        next->name,
        next->id,
        top.vruntime,
        top.weight,
        slice,
        this->readyQueue_.size());

    return next;
}

void LinuxScheduler::printStats() const
{
    SPDLOG_INFO("[Linux] Total enqueues: {}", this->totalEnqueued_);
    SPDLOG_INFO("[Linux] Total dispatches: {}", this->totalDispatched_);
    SPDLOG_INFO("[Linux] Remaining queue depth: {}", this->readyQueue_.size());
}
