# Embedded Systems CPU Scheduler Simulator

Simulator for exploring CPU scheduling policies on embedded-style workloads. It models task arrivals, dispatch decisions, I/O completions, timer ticks, and exports rich JSON traces for offline analysis and dashboards.

## Features
- Multiple scheduling algorithms: FCFS, SJF, Priority, Round Robin, MLFQ, EDF, Linux/CFS-inspired, MLQ, Proportional Share, POSIX RT (FIFO/RR), Windows, Priority Aging, RMS.
- Configurable workloads and scenarios via JSON/YAML (see `data/`).
- Metrics and traces: per-task lifecycle, timeline events, tick samples, summary stats.
- Python notebooks for batch runs (`python/batch_runner.ipynb`) and dashboards (`python/dashboard.ipynb`).

## Repository Layout
- `include/` – headers (Scheduler interface, Task/Event definitions, RunQueue, SimulationEngine, MetricsCollector).
- `src/` – scheduler implementations, simulation engine, CLI (`main.cpp`).
- `data/` – example workloads/scenarios and generated outputs.
- `python/` – analysis notebooks and `metrics_loader.py`.
- `build/` – build output (created by CMake); simulator binary typically in `build/bin/`.

## Build
Requirements: CMake ≥3.20, a C++23 compiler.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Artifacts:
- `build/bin/scheduler_sim` (or `.exe` on Windows)
- `build/bin/data` and `build/bin/logs` copied after build

## Running the Simulator
Basic run with defaults:
```bash
./build/bin/scheduler_sim \
  --workload_file data/workload.json \
  --scenario_file data/scenarios/scenario.yml \
  --scheduler_policy fcfs \
  --duration 10000 \
  --out_file build/bin/results/trace.json
```

Key CLI flags (see `main.cpp` for full list):
- `--scheduler_policy` comma-separated policies (e.g., `fcfs,mlfq,edf`; supported canonicals: fcfs, sjf, priority, rr, mlfq, mlq, edf, linux, posix_rt, priority_based, proportional, rms, windows).
- `--workload_file` / `--scenario_file` JSON or YAML inputs.
- `--duration` simulation length in ms.
- `--out_file` JSON trace path; `--results_subdir` for batch naming.
- Logging: `--log_stdout_level`, `--log_file_level`, `--log_file`.
- `--verbose` for extra dispatch logs.

## End-to-End Data Workflow
Follow these steps to go from source checkout to final plots/dashboards:

1) Build the simulator  
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Artifacts land in `build/bin/` (binary plus copied `data/` and `logs/`).

2) Run a simulation to generate traces  
Pick a scheduler (see supported list below), workload, and scenario, then run:
```bash
./build/bin/scheduler_sim \
  --workload_file data/workload.json \
  --scenario_file data/scenarios/scenario_1.yml \
  --scheduler_policy fcfs \
  --duration 10000 \
  --out_file build/bin/results/trace.json
```
- To run multiple schedulers in one go: `--scheduler_policy fcfs,mlfq,edf` (files get suffixed per policy).
- Use `--results_subdir experiment_01` to keep runs organized (`build/bin/results/experiment_01/`).

3) Inspect raw outputs (optional but handy)  
Traces are JSON; quick checks:
```bash
jq '.summary' build/bin/results/trace.json
python -m json.tool build/bin/results/trace.json | head
```

4) Prepare a Python environment for notebooks  
From the repo root (or `python/`), create/activate a venv and install deps:
```bash
python -m venv .venv
source .venv/bin/activate  # Windows: .venv\\Scripts\\activate
pip install --upgrade pip
pip install pandas plotly ipywidgets kaleido
```

5) Run batch simulations via notebook (optional)  
Open `python/batch_runner.ipynb` (e.g., `jupyter lab python/batch_runner.ipynb`) and run the cells:
- It auto-discovers the built binary under `build/bin/**`.
- Pick scenarios/workloads/policies, set output root (defaults to `build/bin/results/`), then execute to emit multiple `trace*.json` files.

6) Explore and export visuals  
Open `python/dashboard.ipynb` from the `python/` directory:
- It scans for `trace*.json` under `build/bin/results/**` (or paths you supply).
- Run the notebook cells to view timelines, utilization, idle bars, scatter plots, and class breakdowns.
- Use the provided export cells (plotly/kaleido) to save PNG/HTML artifacts for reports.

7) Final data hand-off  
Bundle the generated `trace*.json` plus any exported PNG/HTML from the dashboard. Include the `results_subdir` name so others can rerun with the same config.

## Workloads and Scenarios
- Workloads live under `data/workloads/` and define tasks: arrival/period, exec range, deadlines, priorities, task class, optional I/O waits.
- Scenarios under `data/scenarios/` set hardware (cores), scheduler options, timing parameters, logging, etc.
- Generated variants may appear in `data/.../generated/`.

## Scheduling Policies (implemented)
- FCFS (`fcfs`)
- Shortest Job First (`sjf`; aliases: `shortest_job_first`, `shortest-job-first`)
- Priority (`priority`; aliases: `priority_scheduling`, `priority-scheduling`)
- Round Robin (`rr`; aliases: `round_robin`, `round-robin`)
- Multi-Level Queue (`mlq`; aliases: `multi_level_queue`, `multi-level-queue`)
- Multi-Level Feedback Queue (`mlfq`)
- Earliest Deadline First (`edf`; aliases: `earliest_deadline_first`, `earliest-deadline-first`) – supports relative or absolute deadlines via constructor; CLI uses relative-to-arrival by default.
- Linux/CFS-inspired (`linux`; aliases: `cfs`, `cfs_linux`)
- POSIX RT (`posix_rt`; aliases: `posix-rt`, `rt`, `sched_fifo`, `sched_rr`)
- Priority Aging (`priority_based`; aliases: `priority-aging`, `priority_aging`)
- Proportional Share (`proportional`; aliases: `proportional_share`, `weighted_fair`)
- Rate Monotonic (`rms`; aliases: `rate_monotonic`, `rate-monotonic`)
- Windows-style (`windows`; aliases: `win`, `win32`)

## Metrics and Output
`MetricsCollector` writes `trace.json` with:
- `config`, `workload` metadata
- `summary` aggregates (wait/turnaround/response, utilization, idle times)
- `tasks.lifecycle` rows per task
- `timeline` event spans (dispatches, idle, arrivals, completions, I/O)
- `ticks` periodic snapshots (if enabled)

## Python Notebooks
- `python/metrics_loader.py` – helpers for loading traces into pandas/plotly.
- `python/batch_runner.ipynb` – discover scenarios/workloads, run simulator batches; auto-finds the binary under `build/bin/**`.
- `python/dashboard.ipynb` – discovers `trace*.json` under build/results paths and renders timelines, utilization, idle bars, task scatter plots. Run from `python/` and ensure `ipywidgets`, `pandas`, `plotly`, `kaleido` are installed.

## Testing & Tips
- After code changes: `cmake --build build` (reconfig if needed).
- To inspect results quickly: `jq`/`python -m json.tool build/bin/results/trace.json`.
- If notebooks fail to find the binary or traces, rebuild and confirm outputs under `build/bin/` and `build/bin/results/`.

## Extending
- Add new policies by subclassing `IScheduler` (see `src/Scheduler_*.cpp`), update `include/Scheduler.h`, and wire into `createScheduler` in `src/main.cpp`.
- Expand workloads/scenarios with new fields as needed by schedulers (e.g., deadlines for EDF).
