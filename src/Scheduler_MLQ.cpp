#include "Scheduler.h"

// Stub for Multilevel Queue Scheduling (fixed queues with no feedback).
//
// Planned data updates in ./data/**:
// - data/workloads/workload.json / data/workloads/workload.yml: add a
//   `queue_level` (or similar) attribute per task to statically assign it to a
//   foreground/background batch queue.
// - data/workloads/workload_1.yml: create sample task groups that demonstrate
//   distinct queue assignments (e.g., system, interactive, batch).
// - data/scenarios/scenario_mlq.yml (new): define queue quanta/weights and map
//   to the new scheduler policy key so the simulator can pick this variant.
