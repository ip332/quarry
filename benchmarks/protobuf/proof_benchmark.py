#!/usr/bin/env python3
"""Protocol Buffers benchmark using the shared Quarry logical datasets."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics
import time

import google.protobuf
import workload_pb2


def present(value: str) -> bool:
    return value != "-"


def load(path: Path) -> list[list[str]]:
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#") or line.startswith("sequence|"):
            continue
        fields = line.split("|")
        if len(fields) != 11:
            raise ValueError("invalid workload dataset row")
        rows.append(fields)
    if not rows:
        raise ValueError("empty workload dataset")
    return rows


def bytes_value(value: str) -> bytes:
    return bytes.fromhex(value) if present(value) else b""


def make_record(fields: list[str]) -> workload_pb2.Workload:
    record = workload_pb2.Workload()
    if present(fields[0]): record.sequence = int(fields[0])
    if present(fields[1]): record.timestamp = int(fields[1])
    if present(fields[2]): record.counter = int(fields[2])
    if present(fields[3]): record.ratio = float(fields[3])
    if present(fields[4]): record.enabled = fields[4] == "1"
    if present(fields[5]): record.status = int(fields[5])
    if present(fields[6]): record.name = fields[6]
    if present(fields[7]): record.payload = bytes_value(fields[7])
    if present(fields[8]): record.values.extend(int(value) for value in fields[8].split(","))
    if present(fields[9]):
        child = record.child
        child_fields = fields[9].split(",")
        child.id, child.label, child.payload = int(child_fields[0]), child_fields[1], bytes_value(child_fields[2])
    if present(fields[10]):
        for value in fields[10].split(";"):
            child_fields = value.split(",")
            child = record.children.add()
            child.id, child.label, child.payload = int(child_fields[0]), child_fields[1], bytes_value(child_fields[2])
    return record


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--case", default="telemetry")
    parser.add_argument("--operation", choices=("encode", "decode", "round_trip"), default="round_trip")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--samples", type=int, default=5)
    args = parser.parse_args()
    records = [make_record(fields) for fields in load(args.dataset)]
    encoded = [record.SerializeToString() for record in records]
    for record, value in zip(records, encoded):
        decoded = workload_pb2.Workload.FromString(value)
        if decoded.sequence != record.sequence:
            raise ValueError("protobuf validation failed")

    checksum = 0
    def operation() -> None:
        nonlocal checksum
        if args.operation in ("encode", "round_trip"):
            for record in records:
                checksum ^= len(record.SerializeToString())
        if args.operation in ("decode", "round_trip"):
            for value in encoded:
                checksum ^= workload_pb2.Workload.FromString(value).sequence

    for _ in range(args.warmup): operation()
    durations = []
    for _ in range(args.samples):
        start = time.perf_counter_ns()
        for _ in range(args.iterations): operation()
        durations.append(time.perf_counter_ns() - start)
    operations = len(records) * args.iterations
    latency = statistics.median(durations) / operations
    args.output.write_text(json.dumps({
        "format_version": 2, "benchmark_case": args.case, "backend": "protobuf-python",
        "language": "Python", "wire_format": "protobuf", "operation": args.operation,
        "schema_identity": "benchmark.workload.Workload", "dataset_seed": 153,
        "record_count": len(records), "warmup_iterations": args.warmup,
        "measured_iterations": args.iterations, "sample_count": args.samples,
        "sample_durations_ns": durations, "operation_count": operations * args.samples,
        "latency_ns_per_operation": latency, "throughput_operations_per_second": 1e9 / latency,
        "encoded_byte_size": len(encoded[0]),
        "resources": {"encoded_bytes": len(encoded[0]), "object_size": None,
                       "allocations": None, "allocated_bytes": None},
        "protobuf_runtime_version": google.protobuf.__version__,
        "arena_mode": False, "validation_status": "passed", "checksum": checksum,
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
