#!/usr/bin/env python3
"""Small non-performance checks for the benchmark infrastructure."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    generator = root / "benchmarks/scripts/generate_dataset.py"
    validator = root / "benchmarks/scripts/validate_results.py"
    with tempfile.TemporaryDirectory(prefix="quarry-benchmark-test-") as directory:
        first = Path(directory) / "first.dataset"
        second = Path(directory) / "second.dataset"
        subprocess.run([sys.executable, str(generator), "--output", str(first)], check=True)
        subprocess.run([sys.executable, str(generator), "--output", str(second)], check=True)
        if first.read_bytes() != second.read_bytes():
            raise AssertionError("dataset generation is not deterministic")

        valid = Path(directory) / "valid.json"
        value = {
            "format_version": 1, "benchmark_case": "proof", "backend": "test",
            "language": "test", "operation": "round_trip", "schema_identity": "benchmark.proof.Sample",
            "dataset_seed": 152, "record_count": 1, "warmup_iterations": 0,
            "measured_iterations": 1, "sample_count": 1, "sample_durations_ns": [1],
            "operation_count": 1, "latency_ns_per_operation": 1.0,
            "throughput_operations_per_second": 1.0, "encoded_byte_size": 1,
            "validation_status": "passed",
        }
        valid.write_text(json.dumps(value), encoding="utf-8")
        subprocess.run([sys.executable, str(validator), str(valid)], check=True)
        invalid = Path(directory) / "invalid.json"
        value.pop("encoded_byte_size")
        invalid.write_text(json.dumps(value), encoding="utf-8")
        rejected = subprocess.run([sys.executable, str(validator), str(invalid)], stderr=subprocess.DEVNULL)
        if rejected.returncode == 0:
            raise AssertionError("invalid result was accepted")
    print("benchmark infrastructure checks passed")


if __name__ == "__main__":
    main()
