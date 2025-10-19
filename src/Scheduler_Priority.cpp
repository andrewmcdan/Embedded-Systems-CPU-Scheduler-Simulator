#include "Scheduler.h"

// Stub for classic Priority Scheduling (static priority ordering).
//
// Planned data updates in ./data/**:
// - data/workloads/workload.json / data/workloads/workload.yml: ensure every
//   task explicitly sets `priority` and document the allowed range so that
//   strictly higher numbers (or lower) map to higher precedence.
// - data/workloads/workload_1.yml: provide at least one workload section that
//   varies priorities meaningfully to exercise the scheduler.
// - data/scenarios/scenario_priority.yml (new): configure the simulator to use
//   the priority scheduler and describe how ties should be broken.
