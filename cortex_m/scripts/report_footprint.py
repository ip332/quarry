#!/usr/bin/env python3
"""Report Cortex-M cross-build footprint using GNU size.

Reports the linked ELF's headline flash/RAM footprint plus a best-effort
per-object breakdown (generated code vs. smoke-test vs. startup). This is
informational only -- it records numbers for visibility, it does not gate a
build against a baseline. See docs/roadmap.md's "Cortex-M validation" entry
for the follow-up (footprint regression tracking) this intentionally defers.

Usage:
    report_footprint.py --size-tool arm-none-eabi-size \
        --elf cortex_m/build/quarry_cortex_m_smoke.elf \
        --object generated_workload=cortex_m/build/CMakeFiles/.../workload.generated.c.obj \
        --object generated_shared=cortex_m/build/CMakeFiles/.../shared.generated.c.obj \
        --object smoke=cortex_m/build/CMakeFiles/.../workload_smoke.c.obj \
        --object startup=cortex_m/build/CMakeFiles/.../startup_cortex_m4.c.obj \
        --json cortex_m/build/footprint.json

Pass --baseline-json with the JSON report of another build (for example the
same smoke target built with the pull request's base-commit schema compiler,
or the plain smoke ELF when reporting an instrumented variant) to append a
side-by-side comparison of the linked .text, .rodata, .data and .bss sections.
The comparison is informational; it never fails the build.
"""

import argparse
import json
import subprocess
import sys

CAVEATS = [
    ".bss is RAM (zero-initialized at startup); it is not flash payload. "
    "Flash consumption is .text + .rodata + .data.",
    "The shared typed codec is reported separately when its archive is supplied. "
    "Generated descriptors and inline wrappers are compiled into their callers; "
    "include-only generated compatibility objects may therefore be empty.",
    "Stack and heap high-water-mark usage are not measured by this script.",
    "This is a single-schema (benchmark.workload), single build "
    "configuration snapshot. It is not a regression gate.",
]


def run_size(size_tool, path):
    output = subprocess.run(
        [size_tool, "-B", path], check=True, capture_output=True, text=True
    ).stdout
    # Berkeley format: a header line, then "text data bss dec hex filename".
    fields = output.strip().splitlines()[-1].split()
    text, data, bss, dec = (int(value) for value in fields[:4])
    return {"text": text, "data": data, "bss": bss, "dec": dec}


# Linked output sections compared individually (GNU size -A, SysV format).
SECTIONS = (".text", ".rodata", ".data", ".bss")


def run_size_sections(size_tool, path):
    output = subprocess.run(
        [size_tool, "-A", "-d", path], check=True, capture_output=True, text=True
    ).stdout
    sections = {name: 0 for name in SECTIONS}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0] in sections and fields[1].isdigit():
            sections[fields[0]] = int(fields[1])
    return sections


def render_comparison(label, baseline_label, sections, baseline_sections):
    lines = []
    lines.append(f"#### Linked sections: {label} vs. {baseline_label}")
    lines.append("")
    lines.append(f"| Section | {baseline_label} | {label} | Delta |")
    lines.append("| --- | --- | --- | --- |")
    for name in SECTIONS:
        before = baseline_sections.get(name, 0)
        after = sections.get(name, 0)
        lines.append(f"| {name} | {before} | {after} | {after - before:+d} |")
    lines.append("")
    return "\n".join(lines)


def render_markdown(elf_path, totals, objects, sections):
    lines = []
    lines.append("### Cortex-M footprint (benchmark.workload, Cortex-M4)")
    lines.append("")
    lines.append(f"ELF: `{elf_path}`")
    lines.append("")
    lines.append("| Section | Bytes |")
    lines.append("| --- | --- |")
    lines.append(f"| .text (code + .rodata, flash) | {totals['text']} |")
    lines.append(f"| .data (initialized, flash *and* RAM) | {totals['data']} |")
    lines.append(f"| .bss (zero-init, RAM only) | {totals['bss']} |")
    flash = totals["text"] + totals["data"]
    ram = totals["data"] + totals["bss"]
    lines.append(f"| **Flash total** (.text + .data) | **{flash}** |")
    lines.append(f"| **RAM total** (.data + .bss) | **{ram}** |")
    lines.append("")
    lines.append("| Linked section (size -A) | Bytes |")
    lines.append("| --- | --- |")
    for name in SECTIONS:
        lines.append(f"| {name} | {sections[name]} |")
    lines.append("")
    if objects:
        lines.append("#### Per-object breakdown (best effort)")
        lines.append("")
        lines.append("| Object | .text | .data | .bss |")
        lines.append("| --- | --- | --- | --- |")
        for name, sizes in objects.items():
            lines.append(f"| {name} | {sizes['text']} | {sizes['data']} | {sizes['bss']} |")
        lines.append("")
    lines.append("#### Caveats")
    lines.append("")
    for caveat in CAVEATS:
        lines.append(f"- {caveat}")
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size-tool", default="arm-none-eabi-size")
    parser.add_argument("--elf", required=True)
    parser.add_argument(
        "--object",
        action="append",
        default=[],
        metavar="NAME=PATH",
        help="Individual object file to report, as NAME=PATH. May be repeated.",
    )
    parser.add_argument("--json", help="Optional path to also write a JSON report.")
    parser.add_argument("--label", default="this build", help="Name of this build in comparisons.")
    parser.add_argument(
        "--baseline-json",
        help="JSON report (format_version 2) of a build to compare the linked sections against.",
    )
    parser.add_argument("--baseline-label", default="baseline")
    args = parser.parse_args()

    totals = run_size(args.size_tool, args.elf)
    sections = run_size_sections(args.size_tool, args.elf)

    objects = {}
    for spec in args.object:
        if "=" not in spec:
            print(f"error: --object expects NAME=PATH, got: {spec}", file=sys.stderr)
            return 1
        name, path = spec.split("=", 1)
        objects[name] = run_size(args.size_tool, path)

    report = render_markdown(args.elf, totals, objects, sections)
    if args.baseline_json:
        with open(args.baseline_json, encoding="utf-8") as handle:
            baseline = json.load(handle)
        report += "\n" + render_comparison(
            args.label, args.baseline_label, sections, baseline.get("sections", {})
        )
    print(report)

    if args.json:
        payload = {
            "format_version": 2,
            "elf": args.elf,
            "totals": totals,
            "sections": sections,
            "flash_total_bytes": totals["text"] + totals["data"],
            "ram_total_bytes": totals["data"] + totals["bss"],
            "objects": objects,
            "caveats": CAVEATS,
        }
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, sort_keys=True)
            handle.write("\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
