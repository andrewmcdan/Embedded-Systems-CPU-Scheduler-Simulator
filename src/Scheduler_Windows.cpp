#include "Scheduler.h"

// Stub for Windows-inspired scheduling (priority classes + quanta).
//
// Planned data updates in ./data/**:
// - data/workloads/workload.json / data/workloads/workload.yml: add fields for
//   `priority_class` (Idle, BelowNormal, Normal, AboveNormal, High, Realtime)
//   and optional `boost_group` to mimic Windows priority adjustments.
// - data/workloads/workload_1.yml: include representative threads from multiple
//   priority classes to validate scheduler behaviour later.
// - data/scenarios/scenario_windows.yml (new): record system-wide quantum
//   lengths per priority class and whether priority boosting is enabled.
