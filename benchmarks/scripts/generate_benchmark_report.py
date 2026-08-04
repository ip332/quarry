#!/usr/bin/env python3
"""Generate release-ready benchmark bundles, reports, and deterministic SVG charts."""

from __future__ import annotations

import argparse
import html
import json
from pathlib import Path

from create_baseline_bundle import BundleError, aggregate, collect, write_bundle


COLORS = {
    "cpp": "#3366cc", "c": "#109618", "python": "#ff9900",
    "protobuf-cpp": "#dc3912", "protobuf-cpp-arena": "#990099",
    "protobuf-python": "#0099c6",
}
DISPLAY_NAMES = {
    "cpp": "Quarry C++", "c": "Quarry C", "python": "Quarry Python",
    "protobuf-cpp": "Protobuf C++", "protobuf-cpp-arena": "Protobuf Arena",
    "protobuf-python": "Protobuf Python",
}


def display(value: object) -> str:
    return "N/A" if value is None else str(value)


def chart_svg(title: str, values: list[tuple[str, object]], unit: str) -> str:
    width, height = 960, 460
    left, top, chart_width, chart_height = 90, 62, 820, 300
    numeric = [float(value) for _, value in values if isinstance(value, (int, float))]
    maximum = max(numeric, default=1.0) or 1.0
    bar_width = min(90.0, chart_width / max(len(values), 1) * 0.62)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="white"/>',
             f'<text x="{width / 2:.1f}" y="30" text-anchor="middle" font-family="sans-serif" font-size="20" font-weight="bold">{html.escape(title)}</text>',
             f'<text x="20" y="{top + chart_height / 2:.1f}" transform="rotate(-90 20 {top + chart_height / 2:.1f})" text-anchor="middle" font-family="sans-serif" font-size="12">{html.escape(unit)}</text>',
             f'<line x1="{left}" y1="{top + chart_height}" x2="{left + chart_width}" y2="{top + chart_height}" stroke="#333"/>',
             f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + chart_height}" stroke="#333"/>']
    for index, (backend, value) in enumerate(values):
        x = left + (index + 0.5) * chart_width / max(len(values), 1)
        if isinstance(value, (int, float)):
            bar_height = float(value) / maximum * chart_height
            y = top + chart_height - bar_height
            parts.append(f'<rect x="{x - bar_width / 2:.1f}" y="{y:.1f}" width="{bar_width:.1f}" height="{bar_height:.1f}" fill="{COLORS.get(backend, "#666666")}"/>')
            parts.append(f'<text x="{x:.1f}" y="{max(y - 6, top - 2):.1f}" text-anchor="middle" font-family="sans-serif" font-size="10">{float(value):.2f}</text>')
        else:
            parts.append(f'<text x="{x:.1f}" y="{top + chart_height / 2:.1f}" text-anchor="middle" font-family="sans-serif" font-size="11">N/A</text>')
        parts.append(f'<text x="{x:.1f}" y="{top + chart_height + 24}" text-anchor="middle" font-family="sans-serif" font-size="11">{html.escape(DISPLAY_NAMES.get(backend, backend))}</text>')
    parts.append(f'<text x="{width - 12}" y="{height - 10}" text-anchor="end" font-family="sans-serif" font-size="10">Measurements only; no ranking</text>')
    parts.append("</svg>\n")
    return "\n".join(parts)


def grouped(results: list[dict[str, object]], case: str, operation: str) -> list[dict[str, object]]:
    return sorted((result for result in results
                   if result["benchmark_case"] == case and result["operation"] == operation),
                  key=lambda result: result["backend"])


def resource_results(results: list[dict[str, object]], case: str) -> list[dict[str, object]]:
    selected = {}
    for result in results:
        if result["benchmark_case"] == case:
            selected.setdefault(result["backend"], result)
    return [selected[key] for key in sorted(selected)]


def write_charts(output: Path, aggregate_result: dict[str, object]) -> list[str]:
    charts = output / "charts"
    charts.mkdir(parents=True, exist_ok=True)
    results = aggregate_result["results"]
    cases = sorted({result["benchmark_case"] for result in results})
    names: list[str] = []
    for case in cases:
        for operation in ("encode", "decode", "round_trip"):
            values = [(result["backend"], result["timing"]["throughput_operations_per_second"]["median"])
                      for result in grouped(results, case, operation)]
            filename = f"{case}-{operation}-throughput.svg"
            (charts / filename).write_text(chart_svg(f"{case}: {operation} throughput", values, "operations/second"), encoding="utf-8")
            names.append(filename)
        for wire_format in sorted({result["wire_format"] for result in resource_results(results, case)}):
            values = [(result["backend"], result["deterministic_metrics"]["encoded_byte_size"])
                      for result in resource_results(results, case) if result["wire_format"] == wire_format]
            filename = f"{case}-encoded-size-{wire_format}.svg"
            (charts / filename).write_text(chart_svg(f"{case}: encoded size ({wire_format})", values, "bytes"), encoding="utf-8")
            names.append(filename)
        for metric, label, unit in (("generated_source_bytes", "generated source size", "bytes"),
                                    ("binary_size", "binary size", "bytes"),
                                    ("runtime_size", "runtime size", "bytes"),
                                    ("object_size", "object size", "bytes"),
                                    ("allocations", "allocations", "allocations")):
            values = [(result["backend"], result["deterministic_metrics"][metric])
                      for result in resource_results(results, case)]
            filename = f"{case}-{metric}.svg"
            (charts / filename).write_text(chart_svg(f"{case}: {label}", values, unit), encoding="utf-8")
            names.append(filename)
    return names


def table(results: list[dict[str, object]], case: str, operation: str) -> str:
    lines = ["| Backend | Wire format | Median | Mean | Minimum | Maximum |",
             "| --- | --- | ---: | ---: | ---: | ---: |"]
    for result in grouped(results, case, operation):
        timing = result["timing"]["throughput_operations_per_second"]
        lines.append(f"| {DISPLAY_NAMES.get(result['backend'], result['backend'])} | {result['wire_format']} | "
                     f"{timing['median']:.2f} | {timing['mean']:.2f} | {timing['minimum']:.2f} | {timing['maximum']:.2f} |")
    return "\n".join(lines)


def resource_table(results: list[dict[str, object]], case: str, metric: str) -> str:
    lines = ["| Backend | Wire format | Value |", "| --- | --- | ---: |"]
    for result in resource_results(results, case):
        lines.append(f"| {DISPLAY_NAMES.get(result['backend'], result['backend'])} | {result['wire_format']} | "
                     f"{display(result['deterministic_metrics'][metric])} |")
    return "\n".join(lines)


def write_report(output: Path, manifest: dict[str, object], aggregate_result: dict[str, object], chart_names: list[str]) -> None:
    results = aggregate_result["results"]
    cases = sorted({result["benchmark_case"] for result in results})
    lines = ["# Benchmark Report", "", "## Environment", "",
             f"- Operating system: `{manifest['operating_system']}`",
             f"- Architecture: `{manifest['architecture']}`",
             f"- CPU: `{manifest['cpu_model']}`",
             f"- Compiler: `{manifest['compiler_version']}`",
             f"- Protobuf: `{manifest['protobuf_version']}` / protoc `{manifest['protoc_version']}`", "",
             "## Methodology", "",
             f"- Methodology version: `{manifest['benchmark_methodology_version']}`",
             f"- Suite version: `{manifest['suite_version']}`",
             f"- Dataset/schema versions: `{manifest['dataset_version']}` / `{manifest['schema_version']}`",
             f"- Executions: `{manifest['execution_count']}`",
             "- Timing values are advisory measurements; no implementations are ranked.",
             "- BRF and protobuf sizes are separate wire-format measurements.", "",
             "## Benchmark Suite", "",
             "The suite uses deterministic datasets for telemetry, configuration, nested device state, large payload, and small-message stress workloads.", "",
             "## Backend Summary", ""]
    for backend in manifest["implementations"]:
        lines.append(f"- {DISPLAY_NAMES.get(backend, backend)}")
    for case in cases:
        lines.extend(["", f"## {case.replace('_', ' ').title()}", "",
                      "### Encode throughput", "", table(results, case, "encode"), "",
                      "### Decode throughput", "", table(results, case, "decode"), "",
                      "### Round-trip throughput", "", table(results, case, "round_trip"), "",
                      "### Resource measurements", ""])
        for metric, title in (("encoded_byte_size", "Encoded size"), ("generated_source_bytes", "Generated source size"),
                              ("binary_size", "Binary size"), ("runtime_size", "Runtime size"),
                              ("object_size", "Object size"), ("allocations", "Allocations")):
            lines.extend([f"#### {title}", "", resource_table(results, case, metric), ""])
    lines.extend(["## Resource Measurements", "",
                  "Deterministic resource values are expected to match across repeated executions. N/A means that a portable equivalent is unavailable.", "",
                  "## Charts", ""])
    for name in chart_names:
        lines.append(f"- [{name}](charts/{name})")
    lines.extend(["", "## Appendix", "", "Raw JSON executions are preserved under `raw/`; `results.json` contains aggregate statistics and raw-run references.", ""])
    (output / "summary.md").write_text("\n".join(lines), encoding="utf-8")


def load_existing(output: Path) -> tuple[dict[str, object], dict[str, object]]:
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    aggregate_result = json.loads((output / "results.json").read_text(encoding="utf-8"))
    return manifest, aggregate_result


def main() -> int:
    parser = argparse.ArgumentParser(prog="quarry-benchmark-report")
    parser.add_argument("--input", nargs="*", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--validation-only", action="store_true")
    modes.add_argument("--bundle-only", action="store_true")
    modes.add_argument("--report-only", action="store_true")
    modes.add_argument("--charts-only", action="store_true")
    args = parser.parse_args()
    try:
        if args.report_only or args.charts_only:
            manifest, aggregate_result = load_existing(args.output)
            chart_names = write_charts(args.output, aggregate_result) if args.charts_only else []
            if args.report_only:
                write_report(args.output, manifest, aggregate_result, sorted(path.name for path in (args.output / "charts").glob("*.svg")))
            elif not chart_names:
                raise BundleError("no charts generated")
            return 0
        if not args.input:
            raise BundleError("--input is required for validation or bundle generation")
        reference, runs = collect([path.resolve() for path in args.input])
        manifests = [json.loads((path.resolve() / "manifest.json").read_text(encoding="utf-8")) for path in args.input]
        aggregate_result = aggregate(reference, runs)
        if args.validation_only:
            print(f"validated {len(runs)} benchmark runs")
            return 0
        if args.output.exists():
            raise BundleError(f"output directory already exists: {args.output}")
        write_bundle(args.output.resolve(), reference, manifests, runs, aggregate_result)
        if not args.bundle_only:
            chart_names = write_charts(args.output, json.loads((args.output / "results.json").read_text(encoding="utf-8")))
            manifest, aggregate_result = load_existing(args.output)
            write_report(args.output, manifest, aggregate_result, chart_names)
        print(f"generated benchmark report in {args.output}")
        return 0
    except (BundleError, OSError, json.JSONDecodeError) as error:
        parser.exit(1, f"quarry-benchmark-report: error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
