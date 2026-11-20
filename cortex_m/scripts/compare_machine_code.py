#!/usr/bin/env python3
"""Compare the machine code and data of two Cortex-M cross builds.

Compares every linked ELF (*.elf in the build directory) and every compiled
object (*.obj under CMakeFiles/) present in both builds, section by section,
with GNU binutils:

* allocatable sections (code, read-only data, initialized data, .bss): type,
  size and, for sections with contents, every byte;
* relocations of object files that apply to allocatable sections (offset,
  type and symbol of every entry);
* defined symbols (name, type and size).

Non-allocatable sections never reach flash or RAM. Their sizes are reported
separately as debug information (.debug_*), symbol tables (.symtab, .strtab)
and other metadata (.comment, .ARM.attributes, ...), so a change there is not
mistaken for a change in machine code. Relocations that apply to debug
sections (for example .rel.debug_line, which moves when header line numbers
change) are reported with the debug information.

Section sizes alone do not show that two builds are identical: this script
compares contents. It is informational and exits 0 unless --strict is given
and an allocatable byte, relocation or defined symbol differs.

Usage:
    compare_machine_code.py --base BUILD_DIR --candidate BUILD_DIR \
        [--label NAME] [--base-label NAME] [--candidate-label NAME] [--strict]
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

SECTION_LINE = re.compile(
    r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)"
    r"\s+[0-9a-fA-F]+\s+([A-Za-z]*)\s"
)


def run(tool, *arguments):
    return subprocess.run(
        [tool, *arguments], check=True, capture_output=True, text=True
    ).stdout


def sections(tools, path):
    """name -> (type, size, flags) from readelf -S -W."""
    found = {}
    for line in run(tools.readelf, "-S", "-W", path).splitlines():
        match = SECTION_LINE.match(line)
        if match and match.group(1) != "NULL":
            name, kind, size, flags = match.groups()
            found[name] = (kind, int(size, 16), flags)
    return found


def section_bytes(tools, path, name):
    with tempfile.TemporaryDirectory() as directory:
        out = os.path.join(directory, "section.bin")
        copy = os.path.join(directory, "copy")
        subprocess.run(
            [tools.objcopy, "--dump-section", f"{name}={out}", path, copy],
            check=True,
            capture_output=True,
        )
        with open(out, "rb") as handle:
            return handle.read()


def relocations(tools, path):
    """Relocation section -> [(offset, type, symbol)] from readelf -r -W."""
    found = {}
    current = None
    for line in run(tools.readelf, "-r", "-W", path).splitlines():
        header = re.match(r"^Relocation section '([^']+)'", line)
        if header:
            current = header.group(1)
            found[current] = []
            continue
        fields = line.split()
        if current and len(fields) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            symbol = fields[4] if len(fields) >= 5 else ""
            found[current].append((fields[0], fields[2], symbol))
    return found


def defined_symbols(tools, path):
    found = set()
    for line in run(tools.nm, "--defined-only", "-S", path).splitlines():
        fields = line.split()
        if len(fields) == 4:
            found.add((fields[3], fields[2], fields[1]))
        elif len(fields) == 3:
            found.add((fields[2], fields[1], ""))
    return found


def footprint(section_table):
    totals = {"flash": 0, "ram": 0, "debug": 0, "symbols": 0, "other": 0}
    for name, (kind, size, flags) in section_table.items():
        if "A" in flags:
            if kind == "NOBITS":
                totals["ram"] += size
            elif "W" in flags:
                totals["flash"] += size  # initialized data: stored in flash,
                totals["ram"] += size  # copied to RAM at startup
            else:
                totals["flash"] += size
        elif name.startswith(".debug"):
            totals["debug"] += size
        elif name in (".symtab", ".strtab"):
            totals["symbols"] += size
        elif kind not in ("REL", "RELA"):
            totals["other"] += size
    return totals


def compare_file(tools, base, candidate):
    base_sections = sections(tools, base)
    candidate_sections = sections(tools, candidate)
    differences = []
    names = sorted(
        name
        for name in set(base_sections) | set(candidate_sections)
        if "A" in (base_sections.get(name) or candidate_sections.get(name))[2]
    )
    compared = 0
    for name in names:
        if name not in base_sections or name not in candidate_sections:
            differences.append(f"section {name} only in {'base' if name in base_sections else 'candidate'}")
            continue
        base_kind, base_size, _ = base_sections[name]
        kind, size, _ = candidate_sections[name]
        if (base_kind, base_size) != (kind, size):
            differences.append(f"section {name}: {base_kind} {base_size} B -> {kind} {size} B")
            continue
        if kind != "NOBITS" and size and section_bytes(tools, base, name) != section_bytes(
            tools, candidate, name
        ):
            differences.append(f"section {name}: contents differ ({size} B)")
            continue
        compared += 1
    base_relocations = relocations(tools, base)
    candidate_relocations = relocations(tools, candidate)
    debug_notes = []
    for name in sorted(set(base_relocations) | set(candidate_relocations)):
        if base_relocations.get(name) == candidate_relocations.get(name):
            continue
        target = re.sub(r"^\.rela?", "", name)
        target_flags = (base_sections.get(target) or candidate_sections.get(target) or ("", 0, ""))[2]
        if "A" in target_flags:
            differences.append(f"relocations differ in {name}")
        else:
            debug_notes.append(f"relocations of non-allocatable {target} differ")
    base_symbols = defined_symbols(tools, base)
    candidate_symbols = defined_symbols(tools, candidate)
    if base_symbols != candidate_symbols:
        only_base = sorted(name for name, *_ in base_symbols - candidate_symbols)
        only_candidate = sorted(name for name, *_ in candidate_symbols - base_symbols)
        differences.append(
            f"defined symbols differ (base only: {', '.join(only_base) or '-'}; "
            f"candidate only: {', '.join(only_candidate) or '-'})"
        )
    return {
        "compared_sections": compared,
        "allocatable_sections": len(names),
        "differences": differences,
        "debug_notes": debug_notes,
        "base": footprint(base_sections),
        "candidate": footprint(candidate_sections),
    }


def artifacts(build):
    found = []
    for directory, _, files in os.walk(build):
        relative_directory = os.path.relpath(directory, build)
        if re.search(r"(^|/)CMakeFiles/\d+\.\d+", relative_directory):
            continue  # CMake compiler identification objects
        for name in files:
            relative = os.path.normpath(os.path.join(relative_directory, name))
            if name.endswith(".elf") and relative_directory == "." or name.endswith(".obj"):
                found.append(relative)
    return set(found)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", required=True, help="Baseline build directory.")
    parser.add_argument("--candidate", required=True, help="Candidate build directory.")
    parser.add_argument("--label", default="build", help="Name of the build configuration.")
    parser.add_argument("--base-label", default="base")
    parser.add_argument("--candidate-label", default="candidate")
    parser.add_argument("--strict", action="store_true", help="Exit 1 on any difference.")
    parser.add_argument("--readelf", default="arm-none-eabi-readelf")
    parser.add_argument("--objcopy", default="arm-none-eabi-objcopy")
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    tools = parser.parse_args()

    base_artifacts = artifacts(tools.base)
    candidate_artifacts = artifacts(tools.candidate)
    common = sorted(base_artifacts & candidate_artifacts)
    unmatched = sorted(base_artifacts ^ candidate_artifacts)

    print(f"### Machine code: {tools.candidate_label} vs {tools.base_label} ({tools.label})\n")
    print(
        "| Artifact | Allocatable sections identical | Flash B | RAM B | Debug B | "
        "Symbol tables B | Other metadata B |"
    )
    print("|---|---|---|---|---|---|---|")
    identical = 0
    reports = {}
    for relative in common:
        report = compare_file(
            tools, os.path.join(tools.base, relative), os.path.join(tools.candidate, relative)
        )
        reports[relative] = report
        same = not report["differences"]
        identical += same
        cells = []
        for key in ("flash", "ram", "debug", "symbols", "other"):
            before, after = report["base"][key], report["candidate"][key]
            cells.append(f"{before}" if before == after else f"{before} → {after} ({after - before:+d})")
        verdict = (
            f"yes ({report['compared_sections']}/{report['allocatable_sections']})"
            if same
            else f"**no** ({report['compared_sections']}/{report['allocatable_sections']})"
        )
        print(f"| `{relative}` | {verdict} | " + " | ".join(cells) + " |")
    print()
    print(
        f"**{identical}/{len(common)}** artifacts have byte-identical allocatable sections, "
        "relocations and defined symbols."
    )
    for relative, report in reports.items():
        for difference in report["differences"]:
            print(f"- `{relative}`: {difference}")
        for note in report["debug_notes"]:
            print(f"- `{relative}` (debug information only): {note}")
    for relative in unmatched:
        side = tools.base_label if relative in base_artifacts else tools.candidate_label
        print(f"- `{relative}`: only in {side}")
    print()
    if tools.strict and (identical != len(common) or unmatched):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
