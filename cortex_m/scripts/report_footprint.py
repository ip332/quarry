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
"""

import argparse
import json
import subprocess
import sys

CAVEATS = [
    ".bss is RAM (zero-initialized at startup); it is not flash payload. "
    "Flash consumption is .text + .rodata + .data.",
    "quarry_runtime_c is header-only and its functions are 'static inline': "
    "its code is compiled directly into whichever object file calls it "
    "(generated code, in this case) and cannot be isolated as its own "
    "object-level entry in the breakdown below.",
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


def render_markdown(elf_path, totals, objects):
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
    args = parser.parse_args()

    totals = run_size(args.size_tool, args.elf)

    objects = {}
    for spec in args.object:
        if "=" not in spec:
            print(f"error: --object expects NAME=PATH, got: {spec}", file=sys.stderr)
            return 1
        name, path = spec.split("=", 1)
        objects[name] = run_size(args.size_tool, path)

    report = render_markdown(args.elf, totals, objects)
    print(report)

    if args.json:
        payload = {
            "format_version": 1,
            "elf": args.elf,
            "totals": totals,
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
