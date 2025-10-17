#!/usr/bin/env python3
"""
Generate an interactive core timeline (Gantt-style) visualisation for a trace.

Example:
    python gantt_vis.py ../build/bin/Debug/results/trace.json --output timeline.html
"""

from __future__ import annotations

import argparse
from pathlib import Path

import metrics_loader as ml


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Render a core timeline for a simulation trace.")
    parser.add_argument(
        "trace",
        type=Path,
        help="Path to the trace JSON exported by the simulator.",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Optional path to save the figure as an HTML file. If omitted the figure is opened in a browser window.",
    )
    parser.add_argument(
        "--no-idle",
        action="store_true",
        help="Exclude idle spans from the timeline.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    trace = ml.load_trace(args.trace)
    fig = ml.make_core_timeline_figure(trace, include_idle=not args.no_idle)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        fig.write_html(args.output)
        print(f"Wrote timeline to {args.output}")
    else:
        fig.show()


if __name__ == "__main__":
    main()
