#!/usr/bin/env python3
"""Normalize native/Python coverage reports and enforce a small baseline delta."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Dict


def gcovr_summary(document: Dict[str, Any]) -> Dict[str, float]:
    summary = document.get("gcovr/summary", document.get("summary", document))
    return {
        "line": float(summary["line_percent"]),
        "function": float(summary["function_percent"]),
        "branch": float(summary.get("branch_percent", 0.0)),
    }


def python_summary(document: Dict[str, Any]) -> Dict[str, float]:
    totals = document["totals"]
    branch_total = float(totals.get("num_branches", 0.0))
    branch_covered = float(totals.get("covered_branches", 0.0))
    return {
        "line": float(totals["percent_covered"]),
        "branch": (100.0 * branch_covered / branch_total
                   if branch_total else 100.0),
    }


def load_current(native: Path, python: Path) -> Dict[str, Dict[str, float]]:
    return {
        "native": gcovr_summary(json.loads(native.read_text(encoding="utf-8"))),
        "python": python_summary(json.loads(python.read_text(encoding="utf-8"))),
    }


def render(metrics: Dict[str, Dict[str, float]]) -> str:
    lines = []
    for component, values in metrics.items():
        summary = f"{component}: line {values['line']:.2f}%"
        if "function" in values:
            summary += f" | function {values['function']:.2f}%"
        summary += f" | branch {values['branch']:.2f}%"
        lines.append(summary)
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--python", required=True, type=Path)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--write", type=Path)
    parser.add_argument("--tolerance", type=float, default=1.0)
    args = parser.parse_args()

    current = load_current(args.native, args.python)
    if args.write:
        args.write.parent.mkdir(parents=True, exist_ok=True)
        args.write.write_text(json.dumps(current, indent=2, sort_keys=True) + "\n",
                               encoding="utf-8")

    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    failures = []
    for component, metrics in current.items():
        for metric, value in metrics.items():
            expected = float(baseline[component][metric])
            if value + args.tolerance < expected:
                failures.append(
                    f"{component} {metric} coverage fell from {expected:.2f}% "
                    f"to {value:.2f}% (tolerance {args.tolerance:.2f} points)"
                )

    print(render(current))
    if failures:
        print("Coverage regression:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print(f"Coverage baseline guard passed (tolerance {args.tolerance:.2f} percentage points).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
