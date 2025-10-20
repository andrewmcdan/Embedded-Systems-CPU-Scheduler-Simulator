#!/usr/bin/env python3
"""
Workload matrix generator.

Usage example:
    python generate_workloads.py \
        --template data/workloads/workload_1.yml \
        --output-dir data/workloads/generated \
        --set tasks.0.exec_ms.min=2,3 \
        --set tasks.0.exec_ms.max=4,5 \
        --set tasks.3.priority=6,7

`--set` accepts a dotted path into the YAML structure combined with a
comma-separated list of values. The script applies every combination of the
provided value lists (Cartesian product) to the template and writes the
resulting workloads to the output directory.
"""

from __future__ import annotations

import argparse
import copy
import itertools
import math
import re
from pathlib import Path
from typing import Any, Dict, Iterable, List, Sequence

import yaml


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


def slugify(value: Any) -> str:
    if isinstance(value, float) and value.is_integer():
        value = int(value)
    text = str(value)
    text = text.replace(" ", "_").replace("/", "-")
    text = re.sub(r"[^A-Za-z0-9_.-]", "-", text)
    return text


def ensure_list_size(lst: List[Any], index: int) -> None:
    while len(lst) <= index:
        lst.append({})


def assign_path(data: Any, dotted_path: str, value: Any) -> None:
    parts = dotted_path.split(".")
    current = data
    for idx, part in enumerate(parts[:-1]):
        next_part = parts[idx + 1]
        if part.isdigit():
            index = int(part)
            if not isinstance(current, list):
                raise TypeError(f"Expected list while navigating '{part}' in '{dotted_path}'")
            ensure_list_size(current, index)
            current = current[index]
        else:
            if not isinstance(current, dict):
                raise TypeError(f"Expected dict while navigating '{part}' in '{dotted_path}'")
            if part not in current:
                current[part] = [] if next_part.isdigit() else {}
            current = current[part]
    final = parts[-1]
    if final.isdigit():
        idx = int(final)
        if not isinstance(current, list):
            raise TypeError(f"Expected list for final segment '{final}' in '{dotted_path}'")
        ensure_list_size(current, idx)
        current[idx] = value
    else:
        if not isinstance(current, dict):
            raise TypeError(f"Expected dict for final segment '{final}' in '{dotted_path}'")
        current[final] = value


def parse_set_arguments(entries: Sequence[str]) -> List[tuple[str, List[Any]]]:
    result: List[tuple[str, List[Any]]] = []
    for entry in entries:
        if "=" not in entry:
            raise ValueError(f"--set entry '{entry}' is missing '=' separator")
        path, values_str = entry.split("=", 1)
        values = [parse_scalar(token) for token in values_str.split(",") if token.strip()]
        if not values:
            raise ValueError(f"--set entry '{entry}' does not provide any values")
        result.append((path.strip(), values))
    return result


def build_filename(base: str, suffixes: Iterable[str], index: int) -> str:
    suffix_text = "_".join(suffixes)
    if suffix_text:
        return f"{base}_{suffix_text}.yml"
    return f"{base}_{index:03d}.yml"


def generate_workloads(args: argparse.Namespace) -> None:
    template_path = Path(args.template).expanduser()
    output_dir = Path(args.output_dir).expanduser()
    output_dir.mkdir(parents=True, exist_ok=True)

    with template_path.open("r", encoding="utf-8") as fh:
        template_data: Dict[str, Any] = yaml.safe_load(fh)

    variants = parse_set_arguments(args.set or [])
    combinations: Iterable[tuple[Any, ...]]
    if variants:
        combinations = itertools.product(*(values for _, values in variants))
    else:
        combinations = [()]

    base_name = args.base_name or template_path.stem

    for index, combo in enumerate(combinations, start=1):
        workload = copy.deepcopy(template_data)
        suffixes = []
        for (path, _values), value in zip(variants, combo):
            assign_path(workload, path, value)
            suffixes.append(f"{path.replace('.', '-')}-{slugify(value)}")
        filename = build_filename(base_name, suffixes, index)
        destination = output_dir / filename
        with destination.open("w", encoding="utf-8") as fh:
            yaml.safe_dump(workload, fh, sort_keys=False)
        print(f"[generated] {destination}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate workload permutations from a template YAML file.")
    parser.add_argument("--template", required=True, help="Path to the base workload YAML (e.g. workload_1.yml)")
    parser.add_argument("--output-dir", required=True, help="Directory where generated workloads will be written")
    parser.add_argument("--base-name", help="Base filename to use for generated files (defaults to template stem)")
    parser.add_argument("--set", action="append",
                        help="Override path and comma-separated values (e.g. --set tasks.0.priority=5,6)")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    try:
        generate_workloads(args)
    except Exception as exc:
        raise SystemExit(f"Failed to generate workloads: {exc}") from exc


if __name__ == "__main__":
    main()

