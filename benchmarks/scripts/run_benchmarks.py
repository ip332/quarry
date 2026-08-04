#!/usr/bin/env python3
"""Build and run the PR-152 proof harnesses without performance comparison."""

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


BACKENDS = ("cpp", "c", "python")
EPOCHS = {"cpp": 3, "c": 2, "python": 1}


def run(command: list[str], cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    subprocess.run(command, cwd=cwd, env=env, check=True)


def find_file(root: Path, name: str) -> Path:
    matches = list(root.rglob(name))
    if len(matches) != 1:
        raise RuntimeError(f"expected one {name} below {root}, found {len(matches)}")
    return matches[0]


def version(command: list[str]) -> str:
    try:
        return subprocess.run(command, check=True, capture_output=True, text=True).stdout.splitlines()[0]
    except (OSError, subprocess.CalledProcessError, IndexError):
        return "unknown"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--backend", choices=BACKENDS + ("all",), default="all")
    parser.add_argument("--operation", choices=("encode", "decode", "round_trip"), default="round_trip")
    parser.add_argument("--output-dir", type=Path, default=Path("benchmark-results"))
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--seed", type=int, default=152)
    parser.add_argument("--count", type=int, default=16)
    args = parser.parse_args()
    if args.warmup < 0 or args.iterations <= 0 or args.samples <= 0:
        parser.error("warmup must be non-negative; iterations and samples must be positive")
    if args.samples > 5:
        parser.error("the proof harness supports at most five samples")

    source = args.source_dir.resolve()
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="quarry-benchmark-") as temporary:
        dataset = Path(temporary) / "proof.dataset"
        run([sys.executable, str(source / "benchmarks/scripts/generate_dataset.py"),
             "--seed", str(args.seed), "--count", str(args.count), "--output", str(dataset)])
        run(["cmake", "--build", str(build), "--target", "quarry_benchmark_cpp",
             "quarry_benchmark_c", "--parallel"])
        executable = {
            "cpp": find_file(build, "quarry_benchmark_cpp"),
            "c": find_file(build, "quarry_benchmark_c"),
        }
        generated = build / "benchmarks/generated"
        selected = BACKENDS if args.backend == "all" else (args.backend,)
        result_paths = []
        for backend in selected:
            result_path = output / f"proof-{backend}-{args.operation}.json"
            command = [str(executable[backend])] if backend != "python" else [sys.executable, str(source / "benchmarks/python/proof_benchmark.py")]
            command += ["--dataset", str(dataset), "--operation", args.operation,
                        "--output", str(result_path), "--warmup", str(args.warmup),
                        "--iterations", str(args.iterations), "--samples", str(args.samples)]
            environment = None
            if backend == "python":
                environment = dict(os.environ)
                environment["PYTHONPATH"] = str(generated) + ":" + str(source / "runtime/python/src")
            run(command, env=environment)
            value = json.loads(result_path.read_text(encoding="utf-8"))
            value.update({
                "quarry_version": "0.1.0",
                "generated_code_api_epoch": EPOCHS[backend],
                "compiler_or_interpreter_version": version([sys.executable, "--version"] if backend == "python" else ["c++", "--version"]),
                "build_mode": "Release",
                "operating_system": platform.system(),
                "architecture": platform.machine(),
                "cpu_model": platform.processor() or "unknown",
                "execution_timestamp_utc": datetime.now(timezone.utc).isoformat(),
                "git_commit": version(["git", "rev-parse", "HEAD"]),
                "buffer_reuse_policy": "prepared dataset and reusable decode input; backend object policy is documented",
                "validation_mode": "round-trip and generated-code validation before timing",
            })
            result_path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
            result_paths.append(result_path)
        run([sys.executable, str(source / "benchmarks/scripts/validate_results.py"), *map(str, result_paths)])
        print("benchmark results:")
        for path in result_paths:
            print(f"  {path}")


if __name__ == "__main__":
    main()
