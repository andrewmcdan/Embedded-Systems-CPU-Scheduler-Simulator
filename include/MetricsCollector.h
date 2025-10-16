#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "Task.h"

using json = nlohmann::json;

/**
 * @brief Collects performance data and simulation timeline.
 */
class MetricsCollector {
public:
    struct Record {
        uint64_t startTime;
        uint64_t endTime;
        int taskId;
        std::string name;
        std::string event;
    };

    void recordArrival(int id, uint64_t time) {
        timeline_.push_back({time, time, id, "task_" + std::to_string(id), "arrival"});
    }

    void recordDispatch(int id, uint64_t time) {
        timeline_.push_back({time, time, id, "task_" + std::to_string(id), "dispatch"});
    }

    void recordCompletion(int id, uint64_t time) {
        timeline_.push_back({time, time, id, "task_" + std::to_string(id), "complete"});
    }

    void finalize(uint64_t simEnd) {
        totalSimTime_ = simEnd;
    }

    json toJson() const {
        json j;
        j["total_sim_time_ms"] = totalSimTime_;
        j["num_events"] = timeline_.size();
        return j;
    }

    json exportTimeline() const {
        json arr = json::array();
        for (const auto& r : timeline_) {
            arr.push_back({
                {"task_id", r.taskId},
                {"name", r.name},
                {"event", r.event},
                {"time", r.startTime}
            });
        }
        return arr;
    }

private:
    std::vector<Record> timeline_;
    uint64_t totalSimTime_ = 0;
};
