#include "Scheduler.h"

// Stub for Proportional Share / Weighted Fair scheduling.
//
// Planned data updates in ./data/**:
// - data/workloads/workload.json / data/workloads/workload.yml: add a
//   `share_weight` (or ticket count) for each task to determine CPU allocation.
// - data/workloads/workload_1.yml: provide a representative workload with
//   varied weights so proportional fairness can be tested.
// - data/scenarios/scenario_proportional.yml (new): specify any global weight
//   normalisation rules and enable this scheduler policy.
