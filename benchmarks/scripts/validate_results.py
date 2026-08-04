#!/usr/bin/env python3
"""Validate structural parity of Quarry benchmark result JSON files."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


REQUIRED = {
    "format_version", "benchmark_case", "backend", "language", "operation",
    "schema_identity", "dataset_seed", "record_count", "warmup_iterations",
    "measured_iterations", "sample_count", "sample_durations_ns",
    "operation_count", "latency_ns_per_operation",
    "throughput_operations_per_second", "encoded_byte_size", "validation_status",
}


def validate(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    missing = REQUIRED - value.keys()
    if missing:
        raise ValueError(f"{path}: missing keys: {', '.join(sorted(missing))}")
    if value["format_version"] != 1 or value["validation_status"] != "passed":
        raise ValueError(f"{path}: unsupported format or failed validation status")
    for key in ("record_count", "warmup_iterations", "measured_iterations", "sample_count", "operation_count", "encoded_byte_size"):
        if not isinstance(value[key], int) or value[key] < 0:
            raise ValueError(f"{path}: {key} must be a non-negative integer")
    durations = value["sample_durations_ns"]
    if not isinstance(durations, list) or len(durations) != value["sample_count"] or not durations:
        raise ValueError(f"{path}: sample duration count is inconsistent")
    if any(not isinstance(duration, (int, float)) or not math.isfinite(duration) or duration < 0 for duration in durations):
        raise ValueError(f"{path}: sample durations must be finite and non-negative")
    for key in ("latency_ns_per_operation", "throughput_operations_per_second"):
        if not isinstance(value[key], (int, float)) or not math.isfinite(value[key]) or value[key] < 0:
            raise ValueError(f"{path}: {key} must be finite and non-negative")
    expected_operations = value["record_count"] * value["measured_iterations"] * value["sample_count"]
    if value["operation_count"] != expected_operations:
        raise ValueError(f"{path}: operation_count does not match configured iterations")
    return value


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("results", nargs="+", type=Path)
    args = parser.parse_args()
    values = [validate(path) for path in args.results]
    first = values[0]
    for value in values[1:]:
        for key in ("benchmark_case", "operation", "schema_identity", "dataset_seed", "record_count", "encoded_byte_size"):
            if value[key] != first[key]:
                raise ValueError(f"result identity mismatch for {key}")
    if len({value["backend"] for value in values}) != len(values):
        raise ValueError("duplicate backend result")
    print(f"validated {len(values)} result files for {first['benchmark_case']} / {first['operation']}")


if __name__ == "__main__":
    main()
