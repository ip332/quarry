#!/usr/bin/env python3
"""Emit the deterministic, language-neutral PR-152 proof dataset."""

from __future__ import annotations

import argparse
from pathlib import Path


def next_value(state: int) -> int:
    state ^= (state << 13) & 0xFFFFFFFF
    state ^= state >> 17
    state ^= (state << 5) & 0xFFFFFFFF
    return state & 0xFFFFFFFF


def build_dataset(seed: int, count: int) -> list[tuple[int, int, float, str, str, str]]:
    state = seed & 0xFFFFFFFF
    rows = []
    for index in range(count):
        state = next_value(state)
        sequence = (index << 16) | (state & 0xFFFF)
        enabled = state & 1
        ratio = ((state >> 8) % 10000) / 100.0
        label = f"node-{index:03d}"
        payload_length = state % 17
        payload = bytes((state >> (8 * (offset % 4))) & 0xFF for offset in range(payload_length))
        reading_count = state % 5
        readings = ",".join(str((state >> (offset * 3)) & 0xFFFF) for offset in range(reading_count))
        rows.append((sequence, enabled, ratio, label, payload.hex(), readings))
    return rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=152)
    parser.add_argument("--count", type=int, default=16)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.count <= 0:
        parser.error("--count must be positive")
    rows = build_dataset(args.seed, args.count)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        output.write("# quarry-benchmark-dataset=1\n")
        output.write("# case=proof\n")
        output.write("# schema=benchmark.proof.Sample\n")
        output.write(f"# seed={args.seed}\n")
        output.write(f"# record_count={args.count}\n")
        output.write("sequence|enabled|ratio|label|payload_hex|readings\n")
        for row in rows:
            output.write("|".join(str(value) for value in row) + "\n")


if __name__ == "__main__":
    main()
