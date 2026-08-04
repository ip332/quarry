#!/usr/bin/env python3
"""Generate deterministic logical records for the Quarry workload suite."""

from __future__ import annotations

import argparse
from pathlib import Path


CASES = ("telemetry", "configuration", "nested", "large", "stress")
DEFAULT_COUNTS = {
    "telemetry": 128,
    "configuration": 64,
    "nested": 32,
    "large": 16,
    "stress": 1000,
}


def next_value(value: int) -> int:
    value &= 0xFFFFFFFF
    value ^= (value << 13) & 0xFFFFFFFF
    value ^= value >> 17
    value ^= (value << 5) & 0xFFFFFFFF
    return value & 0xFFFFFFFF


def hex_bytes(seed: int, length: int) -> str:
    value = seed or 1
    output = bytearray()
    for _ in range(length):
        value = next_value(value)
        output.append(value & 0xFF)
    return output.hex()


def child(seed: int, index: int, length: int = 8) -> str:
    return f"{index + 1},child-{index:03d},{hex_bytes(seed ^ index, length)}"


def row(case: str, seed: int, index: int) -> str:
    value = next_value(seed + index * 97 + 1)
    sequence = index
    timestamp = 1_700_000_000 + index
    counter = value * 3
    ratio = f"{(value % 10000) / 1000.0:.3f}"
    enabled = "1" if index % 3 else "0"
    status = str(index % 3)
    name = f"{case}-node-{index:04d}"
    payload = hex_bytes(value, {"telemetry": 8, "configuration": 24,
                                "nested": 12, "large": 768, "stress": 0}[case])
    values_count = {"telemetry": 2, "configuration": 5,
                    "nested": 4, "large": 48, "stress": 0}[case]
    values = ",".join(str((value + offset * 17) & 0xFFFFFFFF)
                       for offset in range(values_count)) or "-"
    child_value = child(value, index, 16 if case == "large" else 6)
    children_count = {"telemetry": 0, "configuration": 0,
                      "nested": 3, "large": 8, "stress": 0}[case]
    children = ";".join(child(value + offset, index + offset, 12)
                         for offset in range(children_count)) or "-"
    if case == "configuration" and index % 3:
        return "|".join(map(str, (sequence, "-", "-", "-", "-", "-",
                                  "-", "-", "-", "-", "-")))
    if case == "stress":
        return "|".join(map(str, (sequence, "-", counter, "-", enabled, "-",
                                  "-", "-", "-", "-", "-")))
    if case == "telemetry":
        name = "-"
        payload = "-"
        values = "-"
        child_value = "-"
    if case == "configuration":
        timestamp = "-"
        counter = "-"
        ratio = "-"
        enabled = "-" if index % 4 else enabled
        status = "-" if index % 5 else status
        child_value = "-"
        children = "-"
    if case == "nested":
        payload = "-"
        values = "-"
        timestamp = "-"
        counter = "-"
        ratio = "-"
        enabled = "-"
        status = "-"
        name = f"state-{index:04d}"
    if case == "large":
        timestamp = "-"
        counter = "-"
        ratio = "-"
        enabled = "-"
        status = "-"
        name = f"large-{index:04d}-" + "x" * 180
    return "|".join(map(str, (sequence, timestamp, counter, ratio, enabled,
                              status, name, payload, values, child_value,
                              children)))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", choices=CASES, default="telemetry")
    parser.add_argument("--seed", type=int, default=153)
    parser.add_argument("--count", type=int)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    count = args.count if args.count is not None else DEFAULT_COUNTS[args.case]
    if count <= 0:
        parser.error("count must be positive")
    lines = [
        "# quarry-benchmark-dataset=2",
        f"# case={args.case}",
        "# schema=benchmark.workload.Workload",
        f"# seed={args.seed}",
        f"# record_count={count}",
        "sequence|timestamp|counter|ratio|enabled|status|name|payload_hex|values|child|children",
    ]
    lines.extend(row(args.case, args.seed, index) for index in range(count))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
