#!/usr/bin/env python3
"""
Generate a set of metrics plots from a simulation trace file.

By default the script writes three HTML files to the specified output
directory:

    - cpu_utilization.html
    - core_idle.html
    - task_runtime.html

Usage example:
    python plot_metrics.py ../build/bin/Debug/results/trace.json --output plots/
"""



import argparse
from pathlib import Path

import metrics_loader as ml


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create metrics plots for a scheduler trace.")
    parser.add_argument(
        "trace",
        type=Path,
        help="Path to the simulation trace JSON.",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("plots"),
        help="Directory where plots will be written (HTML Files) (default: ./plots).",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Display the generated figures interactively after saving them.",
    )
    parser.add_argument(
        "--rolling-window",
        type=int,
        default=200,
        help="Rolling window (number of samples) for utilization graph smoothing.",
    )
    return parser.parse_args()


def save_figure(fig, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.write_html(path)
    print(f"Wrote {path}")


def main() -> None:
    args = parse_args()
    trace = ml.load_trace(args.trace)

    cpu_fig = ml.make_cpu_utilization_figure(trace, rolling_window=args.rolling_window)
    idle_fig = ml.make_core_idle_bar_figure(trace)
    task_fig = ml.make_task_runtime_scatter(trace)

    save_figure(cpu_fig, args.output / "cpu_utilization.html")
    save_figure(idle_fig, args.output / "core_idle.html")
    save_figure(task_fig, args.output / "task_runtime.html")

    if args.show:
        cpu_fig.show()
        idle_fig.show()
        task_fig.show()


if __name__ == "__main__":
    main()
