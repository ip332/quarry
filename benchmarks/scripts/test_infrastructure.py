#!/usr/bin/env python3
"""Small structural smoke tests for benchmark registration and reporting."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

from generate_dataset import CASES
from validate_results import validate


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="quarry-benchmark-test-") as directory:
        root = Path(directory)
        for case in CASES:
            first = root / f"{case}-a.dataset"
            second = root / f"{case}-b.dataset"
            command = [sys.executable, str(ROOT / "benchmarks/scripts/generate_dataset.py"), "--case", case, "--output"]
            subprocess.run(command + [str(first)], check=True)
            subprocess.run(command + [str(second)], check=True)
            if first.read_bytes() != second.read_bytes():
                raise AssertionError(f"dataset generation is not deterministic for {case}")
        result = root / "telemetry-cpp-round_trip.json"
        result.write_text(json.dumps({
            "format_version": 2, "benchmark_case": "telemetry", "backend": "cpp", "language": "C++", "wire_format": "brf",
            "operation": "round_trip", "schema_identity": "benchmark.workload.Workload", "dataset_seed": 153,
            "record_count": 2, "warmup_iterations": 1, "measured_iterations": 2, "sample_count": 1,
            "sample_durations_ns": [10], "operation_count": 4, "latency_ns_per_operation": 2.5,
            "throughput_operations_per_second": 400000000, "encoded_byte_size": 10,
            "resources": {"encoded_bytes": 10, "generated_source_bytes": 100,
                           "generated_files": 2, "object_size": 64, "binary_size": 1000,
                           "runtime_size": 0, "allocations": None, "allocated_bytes": None},
            "validation_status": "passed",
        }) + "\n", encoding="utf-8")
        validate(result)
        protobuf_result = json.loads(result.read_text(encoding="utf-8"))
        protobuf_result.update({"backend": "protobuf-cpp", "wire_format": "protobuf", "encoded_byte_size": 11})
        protobuf_result["resources"]["encoded_bytes"] = 11
        (root / "telemetry-protobuf-cpp-round_trip.json").write_text(
            json.dumps(protobuf_result) + "\n", encoding="utf-8")
        manifest = {"manifest_version": 1, "benchmark_version": "0.1.0",
                    "protobuf_version": "libprotoc 35.1", "dataset_seed": 153}
        (root / "manifest.json").write_text(json.dumps(manifest) + "\n", encoding="utf-8")
        subprocess.run([sys.executable, str(ROOT / "benchmarks/scripts/summarize_results.py"),
                         "--result-dir", str(root), "--json", str(root / "summary.json"),
                         "--markdown", str(root / "summary.md")], check=True,
                       stdout=subprocess.DEVNULL)
        result.unlink()
        (root / "telemetry-protobuf-cpp-round_trip.json").unlink()
        try:
            subprocess.run([sys.executable, str(ROOT / "benchmarks/scripts/run_benchmarks.py"),
                            "--build-dir", str(root / "missing"), "--case", "unknown"],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.CalledProcessError:
            pass
        else:
            raise AssertionError("invalid benchmark case was accepted")
    print("benchmark infrastructure checks passed")


if __name__ == "__main__":
    main()
