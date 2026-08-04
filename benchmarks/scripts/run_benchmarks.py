#!/usr/bin/env python3
"""Run the deterministic Quarry workload suite without performance comparison."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile


QUARRY_BACKENDS = ("cpp", "c", "python")
PROTOBUF_BACKENDS = ("protobuf-cpp", "protobuf-cpp-arena", "protobuf-python")
BACKENDS = QUARRY_BACKENDS + PROTOBUF_BACKENDS
CASES = ("telemetry", "configuration", "nested", "large", "stress")
EPOCHS = {"cpp": 3, "c": 2, "python": 1}


def run(command: list[str], cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    subprocess.run(command, cwd=cwd, env=env, check=True)


def find_file(root: Path, name: str) -> Path:
    matches = list(root.rglob(name))
    if len(matches) != 1:
        raise RuntimeError(f"expected one {name} below {root}, found {len(matches)}")
    return matches[0]


def generated_file_metrics(root: Path, backend: str) -> tuple[int, int]:
    paths = {
        "cpp": (root / "benchmark/workload.generated.hpp", root / "benchmark/workload/shared.generated.hpp"),
        "c": (root / "benchmark/workload.generated.h", root / "benchmark/workload.generated.c",
              root / "benchmark/workload/shared.generated.h", root / "benchmark/workload/shared.generated.c"),
        "python": (root / "benchmark/workload/schema.py", root / "benchmark/workload/shared/schema.py"),
        "protobuf-cpp": (root / "workload.pb.h", root / "workload.pb.cc"),
        "protobuf-cpp-arena": (root / "workload.pb.h", root / "workload.pb.cc"),
        "protobuf-python": (root / "workload_pb2.py",),
    }[backend]
    existing = [path for path in paths if path.exists()]
    return len(existing), sum(path.stat().st_size for path in existing)


def version(command: list[str]) -> str:
    try:
        return subprocess.run(command, check=True, capture_output=True, text=True).stdout.splitlines()[0]
    except (OSError, subprocess.CalledProcessError, IndexError):
        return "unknown"


def cpu_model() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if line.lower().startswith(("model name", "hardware")) and ":" in line:
                value = line.split(":", 1)[1].strip()
                if value:
                    return value
    except (OSError, UnicodeError):
        pass
    try:
        value = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                               check=True, capture_output=True, text=True).stdout.strip()
        if value:
            return value
    except (OSError, subprocess.CalledProcessError):
        pass
    return platform.processor() or "unknown"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--backend", choices=BACKENDS + ("protobuf", "all"), default="all")
    parser.add_argument("--case", choices=CASES + ("all",), default="all")
    parser.add_argument("--operation", choices=("encode", "decode", "round_trip", "all"), default="round_trip")
    parser.add_argument("--output-dir", type=Path, default=Path("benchmark-results"))
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--seed", type=int, default=153)
    parser.add_argument("--count", type=int)
    args = parser.parse_args()
    if args.warmup < 0 or args.iterations <= 0 or args.samples <= 0:
        parser.error("warmup must be non-negative; iterations and samples must be positive")
    if args.samples > 5:
        parser.error("benchmark harnesses support at most five samples")

    source = args.source_dir.resolve()
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="quarry-benchmark-") as temporary:
        cases = CASES if args.case == "all" else (args.case,)
        operations = ("encode", "decode", "round_trip") if args.operation == "all" else (args.operation,)
        build_targets = []
        if args.backend in ("all", "cpp", "c", "python"):
            build_targets += ["quarry_benchmark_cpp", "quarry_benchmark_c"]
        if args.backend in ("all", "protobuf", "protobuf-cpp", "protobuf-cpp-arena", "protobuf-python"):
            build_targets += ["quarry_benchmark_protobuf_cpp", "quarry_benchmark_protobuf_cpp_arena"]
        run(["cmake", "--build", str(build), "--target", *dict.fromkeys(build_targets), "--parallel"])
        compiler = find_file(build, "quarry-schema-compiler")
        compiler_version = version([str(compiler), "--version"])
        quarry_version = compiler_version.split()[1] if len(compiler_version.split()) > 1 else compiler_version
        executable = {}
        if args.backend in ("all", "cpp", "c", "python"):
            executable.update({"cpp": find_file(build, "quarry_benchmark_cpp"),
                               "c": find_file(build, "quarry_benchmark_c")})
        if args.backend in ("all", "protobuf", "protobuf-cpp", "protobuf-cpp-arena", "protobuf-python"):
            executable.update({"protobuf-cpp": find_file(build, "quarry_benchmark_protobuf_cpp"),
                               "protobuf-cpp-arena": find_file(build, "quarry_benchmark_protobuf_cpp_arena")})
        generated = build / "benchmarks/generated"
        protobuf_generated = build / "benchmarks/protobuf_generated"
        if args.backend == "all":
            selected = BACKENDS
        elif args.backend == "protobuf":
            selected = PROTOBUF_BACKENDS
        else:
            selected = (args.backend,)
        manifest = {
            "manifest_version": 1, "benchmark_version": "0.1.0", "suite_version": "153",
            "quarry_version": quarry_version,
            "dataset_version": 2, "schema_version": 1, "dataset_seed": args.seed,
            "benchmark_date": datetime.now(timezone.utc).date().isoformat(),
            "quarry_commit": version(["git", "rev-parse", "HEAD"]),
            "protobuf_version": version(["protoc", "--version"]),
            "protoc_version": version(["protoc", "--version"]),
            "compiler_version": version(["c++", "--version"]),
            "build_mode": "Release", "optimization_flags": "CMAKE_BUILD_TYPE=Release",
            "cpu_model": cpu_model(), "architecture": platform.machine(),
            "operating_system": platform.system(),
            "implementations": list(selected), "cases": list(cases), "operations": list(operations),
        }
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        for case in cases:
            dataset = Path(temporary) / f"{case}.dataset"
            command = [sys.executable, str(source / "benchmarks/scripts/generate_dataset.py"),
                       "--case", case, "--seed", str(args.seed), "--output", str(dataset)]
            if args.count is not None:
                command += ["--count", str(args.count)]
            run(command)
            for operation in operations:
                result_paths = []
                for backend in selected:
                    result_path = output / f"{case}-{backend}-{operation}.json"
                    python_backend = backend in ("python", "protobuf-python")
                    if backend == "python":
                        command = [sys.executable, str(source / "benchmarks/python/proof_benchmark.py")]
                    elif backend == "protobuf-python":
                        command = [sys.executable, str(source / "benchmarks/protobuf/proof_benchmark.py")]
                    else:
                        command = [str(executable[backend])]
                    command += ["--dataset", str(dataset), "--case", case, "--operation", operation,
                                "--output", str(result_path), "--warmup", str(args.warmup),
                                "--iterations", str(args.iterations), "--samples", str(args.samples)]
                    environment = None
                    if python_backend:
                        environment = dict(os.environ)
                        python_generated = protobuf_generated if backend == "protobuf-python" else generated
                        python_path = str(python_generated)
                        if backend == "python": python_path += ":" + str(source / "runtime/python/src")
                        environment["PYTHONPATH"] = python_path
                    run(command, env=environment)
                    value = json.loads(result_path.read_text(encoding="utf-8"))
                    metrics_root = protobuf_generated if backend.startswith("protobuf-") else generated
                    source_file_count, source_bytes = generated_file_metrics(metrics_root, backend)
                    value.update({
                        "quarry_version": quarry_version, "generated_code_api_epoch": EPOCHS.get(backend),
                        "compiler_or_interpreter_version": version([sys.executable, "--version"] if python_backend else ["c++", "--version"]),
                        "wire_format": "protobuf" if backend.startswith("protobuf-") else "brf",
                        "protobuf_version": version(["protoc", "--version"]) if backend.startswith("protobuf-") else None,
                        "protoc_version": version(["protoc", "--version"]) if backend.startswith("protobuf-") else None,
                        "benchmark_harness_version": 3, "benchmark_suite_version": "153",
                        "dataset_version": 2, "schema_version": 1,
                        "arena_mode": backend == "protobuf-cpp-arena",
                        "build_mode": "Release", "operating_system": platform.system(),
                        "architecture": platform.machine(), "cpu_model": cpu_model(),
                        "execution_timestamp_utc": datetime.now(timezone.utc).isoformat(),
                        "git_commit": version(["git", "rev-parse", "HEAD"]),
                        "generated_source_bytes": source_bytes,
                        "benchmark_binary_bytes": executable[backend].stat().st_size if not python_backend else None,
                        "buffer_reuse_policy": "prepared dataset and reusable decode input; backend object policy is documented",
                        "validation_mode": "round-trip and generated-code validation before timing",
                    })
                    value["dataset_seed"] = args.seed
                    value["resources"].update({
                        "generated_source_bytes": source_bytes,
                        "generated_files": source_file_count,
                        "binary_size": value["benchmark_binary_bytes"],
                        "runtime_size": 0 if backend in ("cpp", "c") else None,
                    })
                    result_path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
                    result_paths.append(result_path)
                run([sys.executable, str(source / "benchmarks/scripts/validate_results.py"), *map(str, result_paths)])
                print(f"benchmark results for {case} / {operation}:")
                for path in result_paths:
                    print(f"  {path}")


if __name__ == "__main__":
    main()
