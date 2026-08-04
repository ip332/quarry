#!/usr/bin/env python3
"""Focused tests for deterministic benchmark report generation."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile

from test_baseline_bundle import files, make_runs


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "benchmarks/scripts/generate_benchmark_report.py"


def run(runs: list[Path], output: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), "--input", *map(str, runs), "--output", str(output), *extra],
        text=True,
        capture_output=True,
    )


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="quarry-report-test-") as temporary:
        root = Path(temporary)
        runs = make_runs(root / "runs")
        first = run(runs, root / "report-a")
        assert first.returncode == 0, first.stderr

        output = root / "report-a"
        assert (output / "manifest.json").is_file()
        assert (output / "results.json").is_file()
        assert (output / "summary.md").is_file()
        assert (output / "charts" / "telemetry-round_trip-throughput.svg").is_file()
        summary = (output / "summary.md").read_text(encoding="utf-8")
        for heading in ("# Benchmark Report", "## Environment", "## Methodology",
                        "## Benchmark Scope", "## How to Interpret These Results",
                        "## Backend Summary", "## Telemetry", "## Resource Measurements",
                        "## Charts", "## Appendix"):
            assert heading in summary
        assert "Public API Benchmark" in summary
        report_manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        assert report_manifest["measurement_model"] == "public-api-v1"
        assert "owning" in report_manifest["ownership_models"]["cpp"]
        assert "no ranking" in " ".join(path.read_text(encoding="utf-8") for path in (output / "charts").glob("*.svg"))
        assert "timestamp" not in summary.lower()

        second = run(runs, root / "report-b")
        assert second.returncode == 0, second.stderr
        assert files(root / "report-a") == files(root / "report-b")

        validation = run(runs, root / "unused", "--validation-only")
        assert validation.returncode == 0, validation.stderr

        bundle_only = run(runs, root / "bundle-only", "--bundle-only")
        assert bundle_only.returncode == 0, bundle_only.stderr
        assert not (root / "bundle-only" / "charts").exists()

        charts_only = subprocess.run(
            [sys.executable, str(TOOL), "--output", str(root / "bundle-only"), "--charts-only"],
            text=True,
            capture_output=True,
        )
        assert charts_only.returncode == 0, charts_only.stderr
        assert (root / "bundle-only" / "charts").is_dir()

        report_only = subprocess.run(
            [sys.executable, str(TOOL), "--output", str(root / "bundle-only"), "--report-only"],
            text=True,
            capture_output=True,
        )
        assert report_only.returncode == 0, report_only.stderr
        assert (root / "bundle-only" / "summary.md").is_file()

        malformed = make_runs(root / "malformed")
        result_path = malformed[0] / "telemetry-cpp-round_trip.json"
        result_path.write_text("not json\n", encoding="utf-8")
        failed = run(malformed, root / "bad")
        assert failed.returncode != 0
        assert "invalid result" in failed.stderr

        # The generated report is machine-readable as well as human-readable.
        aggregate = json.loads((output / "results.json").read_text(encoding="utf-8"))
        assert aggregate["result_count"] == 1
    print("benchmark report checks passed")


if __name__ == "__main__":
    main()
