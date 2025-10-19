#include "Scheduler.h"

// Stub for Linux-inspired scheduling (CFS / nice levels).
//
// Planned data updates in ./data/**:
// - data/workloads/workload.json / data/workloads/workload.yml: add `nice`
//   values or CFS weights for tasks to emulate Linux priority groups.
// - data/workloads/workload_1.yml: construct example tasks with different nice
//   levels to demonstrate timeslice distribution once implemented.
// - data/scenarios/scenario_linux.yml (new): hold global CFS tunables such as
//   target latency or minimum granularity for realism.
