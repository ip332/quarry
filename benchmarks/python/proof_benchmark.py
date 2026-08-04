#!/usr/bin/env python3
"""Python harness for the shared Quarry workload suite."""

from __future__ import annotations

import argparse
import json
import statistics
import time
from pathlib import Path

from benchmark.workload.schema import Status, Workload
from benchmark.workload.shared.schema import Child


def split(value: str, delimiter: str) -> list[str]:
    return value.split(delimiter)


def present(value: str) -> bool:
    return value != "-"


def child(value: str) -> Child:
    identifier, label, payload = split(value, ",")
    return Child(id=int(identifier), label=label, payload=bytes.fromhex(payload))


def record(fields: list[str]) -> Workload:
    values = [int(value) for value in split(fields[8], ",")] if present(fields[8]) else None
    children = [child(value) for value in split(fields[10], ";")] if present(fields[10]) else None
    return Workload(
        sequence=int(fields[0]) if present(fields[0]) else None,
        timestamp=int(fields[1]) if present(fields[1]) else None,
        counter=int(fields[2]) if present(fields[2]) else None,
        ratio=float(fields[3]) if present(fields[3]) else None,
        enabled=fields[4] == "1" if present(fields[4]) else None,
        status=Status(int(fields[5])) if present(fields[5]) else None,
        name=fields[6] if present(fields[6]) else None,
        payload=bytes.fromhex(fields[7]) if present(fields[7]) else None,
        values=values,
        child=child(fields[9]) if present(fields[9]) else None,
        children=children,
    )


def load(path: Path) -> list[Workload]:
    records = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#") and not line.startswith("sequence|"):
            fields = line.split("|")
            if len(fields) != 11:
                raise ValueError("invalid workload dataset row")
            records.append(record(fields))
    if not records:
        raise ValueError("empty workload dataset")
    return records


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--operation", choices=("encode", "decode", "round_trip"), default="round_trip")
    parser.add_argument("--case", default="telemetry")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--samples", type=int, default=5)
    args = parser.parse_args()
    if args.warmup < 0 or args.iterations <= 0 or not 0 < args.samples <= 5:
        raise SystemExit("invalid iteration configuration")
    records = load(args.dataset)
    encoded = [value.encode() for value in records]
    decoded = [Workload.decode(value) for value in encoded]
    if any(value.sequence != original.sequence for value, original in zip(decoded, records)):
        raise SystemExit("workload validation failed")
    checksum = 0

    def operation() -> None:
        nonlocal checksum
        if args.operation in ("encode", "round_trip"):
            checksum ^= sum(len(value.encode()) for value in records)
        if args.operation in ("decode", "round_trip"):
            checksum ^= sum(value.sequence or 0 for value in (Workload.decode(item) for item in encoded))

    for _ in range(args.warmup):
        operation()
    durations = []
    for _ in range(args.samples):
        start = time.perf_counter_ns()
        for _ in range(args.iterations):
            operation()
        durations.append(time.perf_counter_ns() - start)
    operations = len(records) * args.iterations
    latency = statistics.median(durations) / operations
    args.output.write_text(json.dumps({
        "format_version": 2, "benchmark_case": args.case, "backend": "python", "language": "Python",
        "operation": args.operation, "schema_identity": "benchmark.workload.Workload", "dataset_seed": 153,
        "record_count": len(records), "warmup_iterations": args.warmup, "measured_iterations": args.iterations,
        "sample_count": args.samples, "sample_durations_ns": durations,
        "operation_count": operations * args.samples, "latency_ns_per_operation": latency,
        "throughput_operations_per_second": 1e9 / latency, "encoded_byte_size": len(encoded[0]),
        "resources": {"encoded_bytes": len(encoded[0]), "object_size": None,
                       "allocations": None, "allocated_bytes": None},
        "validation_status": "passed", "checksum": checksum,
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
