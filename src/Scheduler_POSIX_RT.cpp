/**
 * @file Scheduler_POSIX_RT.cpp
 * @author Andrew McDaniel
 * @brief POSIX real-time scheduling.
 *
 */
#include "Scheduler.h"

// Stub for POSIX Real-Time Scheduling (SCHED_FIFO / SCHED_RR variants).
//
// Planned data updates in ./data/**:
// - data/workloads/workload.json / data/workloads/workload.yml: introduce a
//   `rt_policy` key (e.g., fifo, rr) and `rt_priority` that mirrors POSIX APIs.
// - data/workloads/workload_1.yml: add at least one section modeling POSIX RT
//   threads with explicit priorities and quantum expectations.
// - data/scenarios/scenario_posix_rt.yml (new): capture system-wide settings
//   such as maximum RT priority and default RR quantum.
