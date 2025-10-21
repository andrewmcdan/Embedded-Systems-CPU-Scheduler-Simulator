#include "Scheduler.h"

// Stub for Rate-Monotonic Scheduling.
//
// Planned data updates in ./data/**:
// - data/workloads/workload.json / data/workloads/workload.yml: ensure every
//   periodic task defines `period_ms` (or re-use the existing `period`) and a
//   worst-case execution time so RMS priority ordering is well-defined.
// - data/workloads/workload_1.yml: validate that harmonic periods are present
//   (or add new periodic tasks) for RMS test cases.
// - data/scenarios/scenario_rms.yml (new): enable the rate-monotonic policy and
//   document any global caps such as maximum utilization to check.
