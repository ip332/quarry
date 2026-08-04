#!/usr/bin/env python3
"""Run the PR-152 proof case against generated Python code."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics
import time

from benchmark.proof.schema import Sample


def load_dataset(path: Path) -> list[dict[str, object]]:
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#") or line.startswith("sequence|"):
            continue
        sequence, enabled, ratio, label, payload_hex, readings = line.split("|")
        rows.append({
            "sequence": int(sequence),
            "enabled": enabled == "1",
            "ratio": float(ratio),
            "label": label,
            "payload": bytes.fromhex(payload_hex),
            "readings": [int(value) for value in readings.split(",")] if readings else [],
        })
    if not rows:
        raise ValueError("dataset is empty")
    return rows


def make_samples(rows: list[dict[str, object]]) -> list[Sample]:
    return [Sample(**row) for row in rows]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--operation", choices=("encode", "decode", "round_trip"), default="round_trip")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--samples", type=int, default=5)
    args = parser.parse_args()
    if args.warmup < 0 or args.iterations <= 0 or args.samples <= 0:
        parser.error("warmup must be non-negative; iterations and samples must be positive")

    rows = load_dataset(args.dataset)
    objects = make_samples(rows)
    encoded = [value.encode() for value in objects]
    decoded = [Sample.decode(value) for value in encoded]
    for original, value in zip(objects, decoded):
        if (value.sequence != original.sequence or value.enabled != original.enabled or
                abs(value.ratio - original.ratio) > 1e-5 or value.label != original.label or
                value.payload != original.payload or value.readings != original.readings):
            raise ValueError("generated Python round trip validation failed")
    checksum = sum(value.sequence or 0 for value in decoded)

    def operation() -> None:
        nonlocal checksum
        if args.operation == "encode":
            values = [value.encode() for value in objects]
            checksum ^= len(values[0])
        elif args.operation == "decode":
            values = [Sample.decode(value) for value in encoded]
            checksum ^= sum(value.sequence or 0 for value in values)
        else:
            values = [Sample.decode(value.encode()) for value in objects]
            checksum ^= sum(value.sequence or 0 for value in values)

    for _ in range(args.warmup):
        operation()
    durations = []
    for _ in range(args.samples):
        start = time.perf_counter_ns()
        for _ in range(args.iterations):
            operation()
        durations.append(time.perf_counter_ns() - start)

    operations = len(objects) * args.iterations
    median = statistics.median(durations)
    result = {
        "format_version": 1,
        "benchmark_case": "proof",
        "backend": "python",
        "language": "Python",
        "operation": args.operation,
        "schema_identity": "benchmark.proof.Sample",
        "dataset_seed": 152,
        "record_count": len(objects),
        "warmup_iterations": args.warmup,
        "measured_iterations": args.iterations,
        "sample_count": len(durations),
        "sample_durations_ns": durations,
        "operation_count": operations * args.samples,
        "latency_ns_per_operation": median / operations,
        "throughput_operations_per_second": operations * 1_000_000_000 / median,
        "encoded_byte_size": len(encoded[0]),
        "validation_status": "passed",
        "validation_checksum": checksum,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
