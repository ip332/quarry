#!/usr/bin/env python3
"""Validate and bundle repeated benchmark executions without comparing them."""

from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import tempfile

from validate_results import validate


MEASUREMENT_MODEL = "public-api-v1"
OWNERSHIP_MODELS = {
    "cpp": "owning vector encode output; owning decoded object; allocation included",
    "c": "caller-owned fixed-capacity buffer and record; no heap allocation in the measured path",
    "python": "Python object model; interpreter and object allocation included",
    "protobuf-cpp": "owning protobuf message and serialized output; allocation included",
    "protobuf-cpp-arena": "Arena-backed protobuf message; arena allocation included",
    "protobuf-python": "Python protobuf object model; interpreter and object allocation included",
}


MINIMUM_RUNS = 5
MANIFEST_COMPATIBILITY_KEYS = (
    "benchmark_version", "suite_version", "dataset_version", "schema_version",
    "dataset_seed", "implementations", "cases", "operations", "quarry_commit",
    "protobuf_version", "protoc_version", "compiler_version", "build_mode",
    "optimization_flags", "cpu_model", "architecture", "operating_system",
)
DETERMINISTIC_RESULT_KEYS = (
    "encoded_byte_size", "generated_source_bytes", "generated_files", "object_size",
    "binary_size", "runtime_size", "allocations", "allocated_bytes",
)


class BundleError(RuntimeError):
    pass


def load_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BundleError(f"unable to read JSON '{path}': {error}") from error
    if not isinstance(value, dict):
        raise BundleError(f"JSON document '{path}' must be an object")
    return value


def result_paths(directory: Path) -> list[Path]:
    return sorted(path for path in directory.glob("*.json")
                  if path.name not in ("manifest.json", "summary.json"))


def manifest_for(directory: Path) -> dict[str, object]:
    path = directory / "manifest.json"
    if not path.is_file():
        raise BundleError(f"input run '{directory}' is missing manifest.json")
    manifest = load_json(path)
    missing = [key for key in MANIFEST_COMPATIBILITY_KEYS if key not in manifest]
    if missing:
        raise BundleError(f"{path}: missing manifest keys: {', '.join(missing)}")
    return manifest


def compatible_manifest(reference: dict[str, object], candidate: dict[str, object], run: int) -> None:
    for key in MANIFEST_COMPATIBILITY_KEYS:
        if reference.get(key) != candidate.get(key):
            raise BundleError(f"run {run:03d}: manifest field '{key}' is incompatible")


def stats(values: list[float | int]) -> dict[str, float]:
    numeric = [float(value) for value in values]
    return {"minimum": min(numeric), "median": statistics.median(numeric),
            "mean": statistics.mean(numeric), "maximum": max(numeric)}


def finite(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(float(value))


def collect(input_directories: list[Path]) -> tuple[dict[str, object], list[list[tuple[Path, dict[str, object]]]]]:
    if len(input_directories) < MINIMUM_RUNS:
        raise BundleError(f"at least {MINIMUM_RUNS} independent input runs are required")
    manifests = [manifest_for(directory) for directory in input_directories]
    reference = manifests[0]
    for index, manifest in enumerate(manifests[1:], start=2):
        compatible_manifest(reference, manifest, index)

    expected = {(case, backend, operation)
                for case in reference["cases"]
                for backend in reference["implementations"]
                for operation in reference["operations"]}
    all_runs: list[list[tuple[Path, dict[str, object]]]] = []
    for index, directory in enumerate(input_directories, start=1):
        paths = result_paths(directory)
        if not paths:
            raise BundleError(f"run {index:03d}: no result JSON files found")
        run_values: list[tuple[Path, dict[str, object]]] = []
        identities = set()
        for path in paths:
            try:
                value = validate(path)
            except (OSError, json.JSONDecodeError, ValueError) as error:
                raise BundleError(f"run {index:03d}: invalid result '{path.name}': {error}") from error
            identity = (value["benchmark_case"], value["backend"], value["operation"])
            if identity in identities:
                raise BundleError(f"run {index:03d}: duplicate result {identity}")
            identities.add(identity)
            run_values.append((path, value))
        if identities != expected:
            missing = sorted(expected - identities)
            extra = sorted(identities - expected)
            raise BundleError(f"run {index:03d}: result set mismatch; missing={missing}, extra={extra}")
        all_runs.append(sorted(run_values, key=lambda item: (item[1]["benchmark_case"],
                                                               item[1]["backend"], item[1]["operation"])))
    return reference, all_runs


def aggregate(reference: dict[str, object], runs: list[list[tuple[Path, dict[str, object]]]]) -> dict[str, object]:
    grouped: dict[tuple[object, object, object], list[tuple[int, Path, dict[str, object]]]] = {}
    for run_index, run in enumerate(runs, start=1):
        for path, value in run:
            identity = (value["benchmark_case"], value["backend"], value["operation"])
            grouped.setdefault(identity, []).append((run_index, path, value))
    results = []
    for identity in sorted(grouped):
        entries = grouped[identity]
        deterministic: dict[str, object] = {}
        for key in DETERMINISTIC_RESULT_KEYS:
            values = [entry[2]["encoded_byte_size"] if key == "encoded_byte_size"
                      else entry[2]["resources"][key] for entry in entries]
            if len(set(json.dumps(value, sort_keys=True) for value in values)) != 1:
                raise BundleError(f"deterministic metric '{key}' differs for {identity}")
            deterministic[key] = values[0]
        latency = [entry[2]["latency_ns_per_operation"] for entry in entries]
        throughput = [entry[2]["throughput_operations_per_second"] for entry in entries]
        if not all(finite(value) for value in latency + throughput):
            raise BundleError(f"non-finite timing metric for {identity}")
        results.append({
            "benchmark_case": identity[0], "backend": identity[1], "operation": identity[2],
            "wire_format": entries[0][2]["wire_format"], "record_count": entries[0][2]["record_count"],
            "deterministic_metrics": deterministic,
            "timing": {"latency_ns_per_operation": stats(latency),
                        "throughput_operations_per_second": stats(throughput),
                        "raw_sample_durations_ns": [duration for entry in entries
                                                     for duration in entry[2]["sample_durations_ns"]]},
            "raw_runs": [{"run": f"run-{entry[0]:03d}",
                          "file": f"raw/run-{entry[0]:03d}/{entry[1].name}"} for entry in entries],
        })
    return {"bundle_format_version": 1, "result_count": len(results), "results": results}


def stable_timestamp(manifests: list[dict[str, object]]) -> str:
    dates = [str(manifest.get("benchmark_date", "1970-01-01")) for manifest in manifests]
    return max(dates) + "T00:00:00Z"


def render_markdown(manifest: dict[str, object], aggregate_result: dict[str, object]) -> str:
    lines = ["# Quarry benchmark baseline", "",
             "This bundle contains repeated measurements without regression interpretation.", "",
             "## Provenance", "",
             f"- Methodology: `{manifest['benchmark_version']}`",
             f"- Suite: `{manifest['suite_version']}`",
             f"- Dataset/schema: `{manifest['dataset_version']}` / `{manifest['schema_version']}`",
             f"- Runs: `{manifest['execution_count']}`",
             f"- Implementations: {', '.join(manifest['implementations'])}", "",
             "Deterministic metrics are required to match across runs. Timing values are advisory measurements.", "",
             "## Measurements", "",
             "| Case | Backend | Wire format | Operation | Encoded bytes | Source bytes | Object bytes | Binary bytes | Latency median (ns/op) | Latency range (ns/op) |",
             "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for result in aggregate_result["results"]:
        metrics = result["deterministic_metrics"]
        timing = result["timing"]["latency_ns_per_operation"]
        def display(value: object) -> str:
            return "n/a" if value is None else str(value)
        lines.append(f"| {result['benchmark_case']} | {result['backend']} | {result['wire_format']} | {result['operation']} | "
                     f"{display(metrics['encoded_byte_size'])} | {display(metrics['generated_source_bytes'])} | "
                     f"{display(metrics['object_size'])} | {display(metrics['binary_size'])} | {timing['median']:.2f} | "
                     f"{timing['minimum']:.2f}–{timing['maximum']:.2f} |")
    return "\n".join(lines) + "\n"


def write_bundle(output: Path, reference: dict[str, object], manifests: list[dict[str, object]],
                 runs: list[list[tuple[Path, dict[str, object]]]], aggregate_result: dict[str, object]) -> None:
    if output.exists():
        raise BundleError(f"output directory already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    base_manifest = {
        "bundle_format_version": 1, "benchmark_methodology_version": reference["benchmark_version"],
        "benchmark_harness_version": reference.get("benchmark_harness_version", 3),
        "benchmark_version": reference["benchmark_version"], "suite_version": reference["suite_version"],
        "dataset_version": reference["dataset_version"], "schema_version": reference["schema_version"],
        "quarry_commit": reference["quarry_commit"], "protobuf_version": reference["protobuf_version"],
        "protoc_version": reference["protoc_version"], "compiler_version": reference["compiler_version"],
        "build_mode": reference["build_mode"], "optimization_flags": reference["optimization_flags"],
        "operating_system": reference["operating_system"], "architecture": reference["architecture"],
        "cpu_model": reference["cpu_model"], "benchmark_cases": reference["cases"],
        "operations": reference["operations"], "implementations": reference["implementations"],
        "execution_count": len(runs), "dataset_seeds": sorted({manifest["dataset_seed"] for manifest in manifests}),
        "execution_dates": sorted({manifest.get("benchmark_date", "unknown") for manifest in manifests}),
        "generation_timestamp_utc": stable_timestamp(manifests),
        "measurement_model": MEASUREMENT_MODEL,
        "ownership_models": {backend: OWNERSHIP_MODELS.get(backend, "unspecified")
                             for backend in reference["implementations"]},
        "raw_run_ids": [f"run-{index:03d}" for index in range(1, len(runs) + 1)],
    }
    digest = hashlib.sha256()
    digest.update(json.dumps(base_manifest, sort_keys=True, separators=(",", ":")).encode())
    for index, run in enumerate(runs, start=1):
        for path, _ in run:
            digest.update(f"run-{index:03d}/{path.name}".encode())
            digest.update(path.read_bytes())
    base_manifest["bundle_identifier"] = "sha256:" + digest.hexdigest()
    temporary = Path(tempfile.mkdtemp(prefix="quarry-baseline-", dir=output.parent))
    try:
        raw = temporary / "raw"
        for index, run in enumerate(runs, start=1):
            run_directory = raw / f"run-{index:03d}"
            run_directory.mkdir(parents=True)
            for path, _ in run:
                shutil.copyfile(path, run_directory / path.name)
        (temporary / "manifest.json").write_text(json.dumps(base_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        (temporary / "results.json").write_text(json.dumps(aggregate_result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        (temporary / "summary.md").write_text(render_markdown(base_manifest, aggregate_result), encoding="utf-8")
        os.replace(temporary, output)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", nargs="+", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--validation-only", action="store_true")
    args = parser.parse_args()
    try:
        directories = [path.resolve() for path in args.input]
        if len(set(directories)) != len(directories):
            raise BundleError("input run directories must be distinct")
        reference, runs = collect(directories)
        manifests = [manifest_for(directory) for directory in directories]
        aggregate_result = aggregate(reference, runs)
        if not args.validation_only:
            write_bundle(args.output.resolve(), reference, manifests, runs, aggregate_result)
        print(f"validated {len(runs)} benchmark runs" + ("" if args.validation_only else f" into {args.output}"))
        return 0
    except BundleError as error:
        parser.exit(1, f"quarry-benchmark-bundle: error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
