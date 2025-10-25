"""
@file generate_sim_data.py
@author Andrew McDaniel
@brief Generates scenario and workload permutations for simulation runs.

Combines template YAML files with matrixed parameter overrides to produce batch-ready input
configurations for the simulator.
"""
#!/usr/bin/env python3
"""
Workload matrix generator.

Usage example:
    python generate_sim_data.py \
        --template data/workloads/workload_1.yml \
        --output-dir data/workloads/generated \
        --set tasks.0.exec_ms.min=2,3 \
        --set tasks.0.exec_ms.max=4,5 \
        --set tasks.3.priority=6,7

`--set` accepts a dotted path into the YAML structure with a comma-separated 
list of values. The script applies every combination of the values in the list
to the template and writes the resulting workloads to the output directory. Multiple
`--set` arguments can be provided to build the Cartesian product of all value lists.

This file can be used with any YML file as a template, not just workload definitions.

"""

import argparse
import copy
import itertools
import math
import re
from pathlib import Path
from typing import Any, Dict, Iterable, List, Sequence

import yaml

# --- Helper functions ---

def parse_scalar(token: str) -> Any:
    lowered = token.strip().lower()
    if lowered in {"true", "yes", "on"}:
        return True
    if lowered in {"false", "no", "off"}:
        return False
    try:
        if lowered.startswith("0x"):
            return int(lowered, 16)
        if lowered.startswith("0b"):
            return int(lowered, 2)
        if lowered.startswith("0o"):
            return int(lowered, 8)
        return int(lowered)
    except ValueError:
        pass
    try:
        value = float(lowered)
        if math.isfinite(value):
            return value
    except ValueError:
        pass
    return token.strip()


def sanitize_token(value: Any) -> str:
    """Convert a value to a filesystem-safe string token."""
    if isinstance(value, float) and value.is_integer():
        value = int(value)
    text = str(value)
    text = text.replace(" ", "_").replace("/", "-")
    text = re.sub(r"[^A-Za-z0-9_.-]", "-", text)
    return text


def ensure_list_size(lst: List[Any], index: int) -> None:
    """Ensure the list is at least index+1 elements long. Useful for path assignment."""
    while len(lst) <= index:
        lst.append({})


def assign_path(data: Any, dotted_path: str, value: Any) -> None:
    """Assign a value into a nested structure using a dotted path."""
    # Split the dotted path (e.g., "tasks.0.exec_ms.min") into individual segments.
    parts = dotted_path.split(".")
    # Start navigating from the root data structure (dict or list).
    current = data
    # Iterate through every segment except the final one to locate the parent container.
    for idx, part in enumerate(parts[:-1]):
        # Peek at the next segment to know whether we need a dict or list.
        next_part = parts[idx + 1]
        # If the current segment is numeric, we expect a list and use it as an index.
        if part.isdigit():
            index = int(part)
            if not isinstance(current, list):
                raise TypeError(f"Expected list while navigating '{part}' in '{dotted_path}'")
            # Grow the list if needed so the target index exists.
            ensure_list_size(current, index)
            # Descend into the list element.
            current = current[index]
        else:
            # Otherwise we expect the current object to be a dictionary.
            if not isinstance(current, dict):
                raise TypeError(f"Expected dict while navigating '{part}' in '{dotted_path}'")
            # Create a placeholder container if the key is missing (list for numeric next part, dict otherwise).
            if part not in current:
                current[part] = [] if next_part.isdigit() else {}
            # Descend into the dictionary value.
            current = current[part]
    # After the loop, the final segment tells us where to place the value.
    final = parts[-1]
    # If the final segment is numeric, we assign into a list element.
    if final.isdigit():
        idx = int(final)
        if not isinstance(current, list):
            raise TypeError(f"Expected list for final segment '{final}' in '{dotted_path}'")
        ensure_list_size(current, idx)
        current[idx] = value
    else:
        # Otherwise we treat the final segment as a dictionary key.
        if not isinstance(current, dict):
            raise TypeError(f"Expected dict for final segment '{final}' in '{dotted_path}'")
        current[final] = value


def parse_set_arguments(entries: Sequence[str]) -> List[tuple[str, List[Any]]]:
    """Parse the --set arguments into a list of (path, values) tuples."""
    result: List[tuple[str, List[Any]]] = []
    for entry in entries:
        if "=" not in entry:
            raise ValueError(f"--set entry '{entry}' is missing '=' separator")
        path, values_str = entry.split("=", 1)
        # Parse the comma-separated values. Double commas produce empty tokens which we ignore.
        values = [parse_scalar(token) for token in values_str.split(",") if token.strip()]
        if not values:
            raise ValueError(f"--set entry '{entry}' does not provide any values")
        result.append((path.strip(), values))
    return result


def build_filename(base: str, suffixes: Iterable[str], index: int) -> str:
    """
    
    Build a filename from the base name, suffixes, and index.
    e.g., base="workload", suffixes=["tasks-0-exec_ms-min-2", "tasks-3-priority-5"], index=1
    produces "workload_tasks-0-exec_ms-min-2_tasks-3-priority-5.yml" 
    or if no suffixes are given: "workload_001.yml"

    """
    suffix_text = "_".join(suffixes)
    if suffix_text:
        return f"{base}_{suffix_text}.yml"
    return f"{base}_{index:03d}.yml"


def generate_workloads(args: argparse.Namespace) -> None:
    """Generate workload combinations from a template YAML file."""
    template_path = Path(args.template).expanduser()
    output_dir = Path(args.output_dir).expanduser()
    output_dir.mkdir(parents=True, exist_ok=True)

    # Load the template YAML data.
    with template_path.open("r", encoding="utf-8") as fh:
        template_data: Dict[str, Any] = yaml.safe_load(fh)

    # Parse the --set arguments into (path, values) tuples.
    variants = parse_set_arguments(args.set or [])
    combinations: Iterable[tuple[Any, ...]]
    if variants:
        combinations = itertools.product(*(values for _, values in variants))
    else:
        combinations = [()]

    # Set the base name for generated files.
    base_name = args.base_name or template_path.stem

    # Generate each workload permutation and write to the output directory.
    for index, combo in enumerate(combinations, start=1):
        workload = copy.deepcopy(template_data)
        suffixes = []
        # Apply each variant value to the workload using the specified path.
        for (path, _values), value in zip(variants, combo):
            assign_path(workload, path, value)
            suffixes.append(f"{path.replace('.', '-')}-{sanitize_token(value)}")
        filename = build_filename(base_name, suffixes, index)
        destination = output_dir / filename
        with destination.open("w", encoding="utf-8") as fh:
            yaml.safe_dump(workload, fh, sort_keys=False)
        print(f"[generated] {destination}")


def parse_args() -> argparse.Namespace:
    """Command-line arguments parser."""
    parser = argparse.ArgumentParser(
        description="Generate simulation data permutations from a template YAML file. This script can be used to create permutations of any YAML-based configuration, not just workload definitions."
        )
    parser.add_argument("--template", required=True, help="Path to the base YAML (e.g. workload_1.yml)")
    parser.add_argument("--output-dir", required=True, help="Directory where generated files will be written")
    parser.add_argument("--base-name", help="Base filename to use for generated files (defaults to template stem)")
    parser.add_argument("--set", action="append",
                        help="Override path and comma separated values (e.g. --set tasks.0.priority=5,6)")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    try:
        generate_workloads(args)
    except Exception as exc:
        raise SystemExit(f"Failed to generate workloads: {exc}") from exc


if __name__ == "__main__":
    main()

