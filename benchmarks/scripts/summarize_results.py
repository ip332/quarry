#!/usr/bin/env python3
"""Create machine-readable and human-readable summaries of benchmark results."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from collections import defaultdict

from validate_results import validate


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--result-dir", type=Path, required=True)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--markdown", type=Path, required=True)
    args = parser.parse_args()
    values = [validate(path) for path in sorted(args.result_dir.glob("*.json"))]
    if not values:
        raise SystemExit("no benchmark result files found")
    values.sort(key=lambda value: (value["benchmark_case"], value["backend"], value["operation"]))
    groups = defaultdict(list)
    for value in values:
        groups[(value["benchmark_case"], value["operation"])].append(value)
    for identity, group in groups.items():
        if len({value["encoded_byte_size"] for value in group}) != 1:
            raise SystemExit(f"encoded-size mismatch for {identity[0]} / {identity[1]}")
    summary = {
        "format_version": 1,
        "result_count": len(values),
        "results": [{
            "benchmark_case": value["benchmark_case"], "backend": value["backend"],
            "operation": value["operation"], "record_count": value["record_count"],
            "encoded_byte_size": value["encoded_byte_size"],
            "encoded_bytes_per_record": value["encoded_byte_size"] / value["record_count"],
            "latency_ns_per_operation": value["latency_ns_per_operation"],
            "throughput_operations_per_second": value["throughput_operations_per_second"],
            "generated_source_bytes": value.get("generated_source_bytes", 0),
            "benchmark_binary_bytes": value.get("benchmark_binary_bytes", 0),
        } for value in values],
    }
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    lines = [
        "# Quarry benchmark measurements", "",
        "These measurements are reported without ranking backends or applying timing thresholds.", "",
        "| Case | Backend | Operation | Encoded bytes/record | Median ns/op | Throughput op/s | Generated source bytes | Binary bytes |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for value in summary["results"]:
        lines.append("| {benchmark_case} | {backend} | {operation} | {encoded_bytes_per_record:.2f} | {latency_ns_per_operation:.2f} | {throughput_operations_per_second:.2f} | {generated_source_bytes} | {benchmark_binary_bytes} |".format(**value))
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    args.markdown.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"summarized {len(values)} benchmark result files")


if __name__ == "__main__":
    main()
