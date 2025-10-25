"""
@file metrics_loader.py
@author Andrew McDaniel
@brief Utilities for loading simulation traces and metrics into Python.

Provides helper routines that parse JSON results into pandas DataFrames used by analysis
notebooks and plotting utilities.
"""
# -*- coding: utf-8 -*-
"""
Utilities for loading and visualizing scheduler simulation trace files.

The helpers in this module expect the JSON structure produced by
``SimulationEngine::exportResults`` which contains:

    - config: scenario metadata
    - counters: simple integer counters
    - summary: aggregate statistics collected by MetricsCollector
    - tasks.lifecycle: per-task lifecycle metrics
    - timeline: chronological event log (dispatch, idle, arrivals, etc.)
    - ticks: optional timer tick snapshots

The module exposes small, composable functions that return Pandas data
frames and Plotly figures so that scripts and notebooks can share a single
implementation.
"""



import json
from pathlib import Path
from typing import Iterable, Optional

try:
    import pandas as pd
except ImportError as exc:  # pragma: no cover - informative message for missing dependency
    raise ImportError("metrics_loader requires pandas (pip install pandas)") from exc

try:
    import plotly.express as px
    import plotly.graph_objects as go
except ImportError as exc:  # pragma: no cover
    raise ImportError("metrics_loader requires plotly (pip install plotly)") from exc

TraceDict = dict

# --- Data loading and extraction functions ---

def load_trace(path: Path | str) -> TraceDict:
    """Load a trace JSON file produced by the simulator."""
    path = Path(path)
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def timeline_dataFrame(trace: TraceDict) -> pd.DataFrame:
    """Return the full timeline table as a DataFrame."""
    entries = trace.get("timeline", [])
    if not entries:
        return pd.DataFrame(columns=["start_ms", "end_ms", "event"])
    frame = pd.DataFrame(entries)
    frame["start_ms"] = frame["start_ms"].astype(float)
    frame["end_ms"] = frame["end_ms"].astype(float)
    return frame


def task_lifecycle_dataFrame(trace: TraceDict) -> pd.DataFrame:
    """Return lifecycle data for each task instance."""
    rows = trace.get("tasks", {}).get("lifecycle", [])
    if not rows:
        return pd.DataFrame()
    return pd.DataFrame(rows)


def ticks_dataFrame(trace: TraceDict) -> pd.DataFrame:
    """Return timer tick samples as DataFrame."""
    rows = trace.get("ticks", [])
    if not rows:
        return pd.DataFrame()
    frame = pd.DataFrame(rows)
    frame["timestamp_ms"] = frame["timestamp_ms"].astype(float)
    return frame


def counter_dict(trace: TraceDict) -> dict:
    """Shortcut for the top-level counters dictionary."""
    return dict(trace.get("counters", {}))


def summary_dict(trace: TraceDict) -> dict:
    """Shortcut for the top-level summary dictionary."""
    return dict(trace.get("summary", {}))


def config_dict(trace: TraceDict) -> dict:
    """Shortcut for the top-level config dictionary."""
    return dict(trace.get("config", {}))


def _adjust_end_time(start_ms: float, end_ms: float) -> float:
    """Ensure that timeline segments have a non-zero length."""
    if end_ms <= start_ms:
        # add a minimal sliver (0.1 ms) so the bar is visible when rendered
        return start_ms + 0.1
    return end_ms


# --- Visualization functions ---

def core_schedule_dataFrame(
    trace: TraceDict,
    include_idle: bool = True,
    events: Optional[Iterable[str]] = None,
) -> pd.DataFrame:
    """
    Extract a per-core schedule view from the timeline.

    Parameters
    ----------
    trace:
        Loaded trace dictionary.
    include_idle:
        Whether to include idle spans.
    events:
        Optional iterable of event names to keep (defaults to
        ``{"task_dispatch", "core_idle"}``).
    """
    # Create base timeline DataFrame
    base = timeline_dataFrame(trace)
    if base.empty:
        return pd.DataFrame(columns=["core", "label", "start_dt", "end_dt", "event"])

    # Filter to core-related events
    frame = base[base["core"].notna()].copy()
    if frame.empty:
        return pd.DataFrame(columns=["core", "label", "start_dt", "end_dt", "event"])

    # Convert core to integer type
    frame["core"] = frame["core"].astype("Int64")
    # Filter to allowed events
    allowed_events = set(events or {"task_dispatch", "core_idle"})
    frame = frame[frame["event"].isin(allowed_events)]
    if not include_idle:
        frame = frame[frame["event"] != "core_idle"]

    # If no entries remain, return empty DataFrame
    if frame.empty:
        return pd.DataFrame(columns=["core", "label", "start_dt", "end_dt", "event"])

    # Adjust end times to ensure non-zero length
    frame["end_ms"] = frame.apply(
        lambda row: _adjust_end_time(row["start_ms"], row["end_ms"]),
        axis=1,
    )

    # Convert timestamps to datetime
    frame["start_dt"] = pd.to_datetime(frame["start_ms"], unit="ms")
    frame["end_dt"] = pd.to_datetime(frame["end_ms"], unit="ms")

    # Create human-readable labels
    def _label(row: pd.Series) -> str:
        if pd.notna(row.get("task_name")) and row["task_name"]:
            return row["task_name"]
        return row["event"]

    # Add label column
    frame["label"] = frame.apply(_label, axis=1)
    return frame[["core", "label", "start_dt", "end_dt", "event", "task_id", "task_name", "metadata"]]


def make_core_timeline_figure(trace: TraceDict, include_idle: bool = True) -> go.Figure:
    """
    Create a Plotly timeline figure showing task execution per core.
    
    Parameters
    ----------
    trace:
        Loaded trace dictionary
    include_idle:
        Whether to include idle spans in the timeline.
    
    Returns
    -------
    go.Figure
        Plotly figure object with the core timeline visualization.

    """

    # Get core schedule DataFrame
    schedule = core_schedule_dataFrame(trace, include_idle=include_idle)
    if schedule.empty:
        fig = go.Figure()
        fig.update_layout(
            title="Core schedule (no timeline entries found)",
            xaxis_title="Simulation time",
            yaxis_title="Core",
        )
        return fig

    # Create timeline figure using Plotly.
    fig = px.timeline(
        schedule,
        x_start="start_dt",
        x_end="end_dt",
        y=schedule["core"].astype(str),
        color="event",
        hover_data={
            "label": True,
            "core": True,
            "event": True,
            "start_dt": True,
            "end_dt": True,
        },
    )
    fig.update_layout(
        title=f"CPU core timeline (idle {'included' if include_idle else 'excluded'})",
        xaxis_title="Simulation time",
        yaxis_title="Core",
        legend_title="Event",
        bargap=0.1,
    )
    fig.update_yaxes(autorange="reversed")
    return fig


def make_cpu_utilization_figure(trace: TraceDict, rolling_window: int = 50) -> go.Figure:
    """
    Plot CPU utilization over time based on tick samples.
    
    Parameters
    ----------
    trace:
        Loaded trace dictionary.
    rolling_window:
        Size of the rolling window (in samples) for smoothing the utilization curve.

    Returns
    -------
    go.Figure
        Plotly figure object with the CPU utilization timeline.

    """
    ticks = ticks_dataFrame(trace)
    fig = go.Figure()
    if ticks.empty:
        fig.update_layout(
            title="CPU utilization (no tick samples available)",
            xaxis_title="Simulation time",
            yaxis_title="Utilization",
        )
        return fig

    # Calculate utilization
    ticks = ticks.copy()
    total = ticks["cores_total"].replace(0, pd.NA).astype("Float64")
    util = ticks["cores_busy"].astype(float) / total
    util = util.fillna(0.0)
    ticks["utilization"] = util
    ticks["timestamp_dt"] = pd.to_datetime(ticks["timestamp_ms"], unit="ms")

    # Add instantaneous utilization trace
    fig.add_trace(
        go.Scatter(
            x=ticks["timestamp_dt"],
            y=ticks["utilization"],
            name="Instantaneous",
            mode="lines",
        )
    )

    # Add rolling average trace
    if rolling_window > 1:
        ticks["utilization_avg"] = ticks["utilization"].rolling(rolling_window, min_periods=1).mean()
        fig.add_trace(
            go.Scatter(
                x=ticks["timestamp_dt"],
                y=ticks["utilization_avg"],
                name=f"Rolling mean ({rolling_window} samples)",
                mode="lines",
                line=dict(width=3),
            )
        )

    #  Finalize layout
    fig.update_layout(
        title="CPU utilization timeline",
        xaxis_title="Simulation time",
        yaxis_title="Utilization (fraction of busy cores)",
        yaxis=dict(range=[0, 1]),
    )
    return fig


def make_core_idle_bar_figure(trace: TraceDict) -> go.Figure:
    """
    Return a bar chart showing idle time per core.
    
    Parameters
    ----------
    trace:
        Loaded trace dictionary.
    
    Returns
    -------
    go.Figure
        Plotly figure object with the core idle time bar chart.
    
    """
    summary = summary_dict(trace)
    idle = summary.get("core_idle_time_ms")
    if not idle:
        fig = go.Figure()
        fig.update_layout(
            title="Core idle time (no data available)",
            xaxis_title="Core",
            yaxis_title="Idle time (ms)",
        )
        return fig
    df = pd.DataFrame({"core": range(len(idle)), "idle_ms": idle})
    fig = px.bar(
        df,
        x="core",
        y="idle_ms",
        text="idle_ms",
        labels={"core": "Core", "idle_ms": "Idle time (ms)"},
        title="Total idle time per core",
    )
    fig.update_traces(texttemplate="%{text:.0f}")
    return fig


def make_task_runtime_scatter(trace: TraceDict) -> go.Figure:
    """Plot runtime vs wait time for individual task instances."""
    tasks = task_lifecycle_dataFrame(trace)
    fig = go.Figure()
    if tasks.empty:
        fig.update_layout(
            title="Task runtime vs wait (no task data)",
            xaxis_title="Wait time (ms)",
            yaxis_title="Runtime (ms)",
        )
        return fig

    df = tasks.copy()
    df["wait_time_ms"] = df.get("wait_time_ms", df.get("total_wait_time", df.get("totalWaitTime"))).fillna(0)
    df["runtime_ms"] = df.get("runtime_ms").fillna(0)
    df["run_label"] = df["name"]

    fig = px.scatter(
        df,
        x="wait_time_ms",
        y="runtime_ms",
        color="class",
        hover_data=["name", "priority", "dispatch_count", "arrival_ms", "completion_ms"],
        title="Task runtime vs wait time",
        labels={
            "wait_time_ms": "Total wait time (ms)",
            "runtime_ms": "Total runtime (ms)",
            "class": "Task class",
        },
    )
    return fig


__all__ = [
    "load_trace",
    "timeline_dataFrame",
    "task_lifecycle_dataFrame",
    "ticks_dataFrame",
    "counter_dict",
    "summary_dict",
    "config_dict",
    "core_schedule_dataFrame",
    "make_core_timeline_figure",
    "make_cpu_utilization_figure",
    "make_core_idle_bar_figure",
    "make_task_runtime_scatter",
]
