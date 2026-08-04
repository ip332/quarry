#!/usr/bin/env python3
"""Focused tests for deterministic benchmark baseline bundles."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "benchmarks/scripts/create_baseline_bundle.py"


def result(timing: float = 10.0) -> dict[str, object]:
    return {
        "format_version": 2, "benchmark_case": "telemetry", "backend": "cpp",
        "language": "C++", "wire_format": "brf", "operation": "round_trip",
        "schema_identity": "benchmark.workload.Workload", "dataset_seed": 153,
        "record_count": 2, "warmup_iterations": 1, "measured_iterations": 2,
        "sample_count": 1, "sample_durations_ns": [timing], "operation_count": 4,
        "latency_ns_per_operation": timing, "throughput_operations_per_second": 1e9 / timing,
        "encoded_byte_size": 10, "validation_status": "passed",
        "resources": {"encoded_bytes": 10, "generated_source_bytes": 100,
                       "generated_files": 2, "object_size": 64, "binary_size": 1000,
                       "runtime_size": 0, "allocations": None, "allocated_bytes": None},
    }


def make_runs(root: Path, count: int = 5) -> list[Path]:
    root.mkdir(parents=True)
    runs = []
    for index in range(count):
        directory = root / f"run-{index + 1}"
        directory.mkdir()
        manifest = {
            "manifest_version": 1, "benchmark_version": "0.1.0", "suite_version": "153",
            "dataset_version": 2, "schema_version": 1, "dataset_seed": 153,
            "benchmark_date": "2026-08-04", "quarry_commit": "abc",
            "protobuf_version": "none", "protoc_version": "none", "compiler_version": "clang",
            "build_mode": "Release", "optimization_flags": "Release",
            "cpu_model": "test", "architecture": "test", "operating_system": "test",
            "implementations": ["cpp"], "cases": ["telemetry"], "operations": ["round_trip"],
        }
        (directory / "manifest.json").write_text(json.dumps(manifest, sort_keys=True) + "\n", encoding="utf-8")
        (directory / "telemetry-cpp-round_trip.json").write_text(
            json.dumps(result(10.0 + index), sort_keys=True) + "\n", encoding="utf-8")
        runs.append(directory)
    return runs


def run_bundle(runs: list[Path], output: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, str(TOOL), "--input", *map(str, runs),
                           "--output", str(output), *extra], text=True,
                          capture_output=True)


def files(root: Path) -> dict[str, bytes]:
    return {str(path.relative_to(root)): path.read_bytes() for path in root.rglob("*") if path.is_file()}


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="quarry-baseline-test-") as temporary:
        root = Path(temporary)
        runs = make_runs(root / "runs")
        first = run_bundle(runs, root / "baseline-a")
        assert first.returncode == 0, first.stderr
        second = run_bundle(runs, root / "baseline-b")
        assert second.returncode == 0, second.stderr
        assert files(root / "baseline-a") == files(root / "baseline-b")
        validation = run_bundle(runs, root / "unused", "--validation-only")
        assert validation.returncode == 0, validation.stderr

        incompatible = make_runs(root / "incompatible")
        manifest_path = incompatible[-1] / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["suite_version"] = "different"
        manifest_path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")
        assert run_bundle(incompatible, root / "bad-methodology").returncode != 0

        inconsistent = make_runs(root / "inconsistent")
        result_path = inconsistent[-1] / "telemetry-cpp-round_trip.json"
        value = json.loads(result_path.read_text(encoding="utf-8"))
        value["resources"]["generated_source_bytes"] = 101
        result_path.write_text(json.dumps(value) + "\n", encoding="utf-8")
        assert run_bundle(inconsistent, root / "bad-resource").returncode != 0
    print("benchmark baseline bundle checks passed")


if __name__ == "__main__":
    main()
