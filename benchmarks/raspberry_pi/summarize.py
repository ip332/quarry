#!/usr/bin/env python3
"""Summarize repeated Pi public API measurements (median of run medians)."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from validate_results import validate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.results / 'manifest.json').read_text())
    if manifest['smoke']:
        parser.error('Smoke results are correctness checks, not performance measurements')
    groups = defaultdict(list)
    for path in sorted(args.results.glob('run-*/*.json')):
        value = validate(path)
        groups[value['benchmark_case'], value['backend'], value['operation']].append(value)
    backends = list(manifest['binary_bytes'])
    expected = len(backends) * 5 * 3
    if len(groups) != expected or any(len(v) != manifest['runs'] for v in groups.values()):
        raise SystemExit(f'Expected all {expected} measurements from every repetition')
    aggregate = []
    for (case, backend, operation), values in sorted(groups.items()):
        timings = [v['latency_ns_per_operation'] for v in values]
        if len({(v['record_count'], v['encoded_byte_size']) for v in values}) != 1:
            raise SystemExit('Dataset/resource identity changed between runs')
        aggregate.append({'case': case, 'backend': backend, 'operation': operation,
                          'median_ns': statistics.median(timings), 'min_ns': min(timings),
                          'max_ns': max(timings), 'run_medians_ns': timings,
                          'encoded_first_record_bytes': values[0]['encoded_byte_size'],
                          'record_count': values[0]['record_count']})
    (args.results / 'summary.json').write_text(json.dumps(aggregate, indent=2) + '\n')
    lookup = {(v['case'], v['backend'], v['operation']): v for v in aggregate}
    lines = ['# Raspberry Pi 3 public API benchmark', '',
             f"Quarry source commit: `{manifest['quarry_commit']}`. See manifest for tracked changes and exact source hashes.",
             f"{manifest['runs']} runs; each result uses 2 warmups, 10 iterations, 5 samples. CPU affinity: {manifest['cpu_affinity']}.",
             'ARMv7 hard-float, static linking, -O3; existing benchmark workloads and allocation policies.',
             'Timings below are the median of the per-run median nanoseconds per record, converted to microseconds.',
             'The governor is unchanged; inspect system-before/after snapshots for frequency and thermal state.',
             '', '| Workload | Backend | Encode µs | Decode µs | Round-trip µs | First record bytes |',
             '|---|---|---:|---:|---:|---:|']
    for case in sorted({v['case'] for v in aggregate}):
        for backend in backends:
            data = [lookup[case, backend, op] for op in ('encode', 'decode', 'round_trip')]
            lines.append(f"| {case} | {backend} | {data[0]['median_ns']/1000:.3f} | "
                         f"{data[1]['median_ns']/1000:.3f} | {data[2]['median_ns']/1000:.3f} | "
                         f"{data[0]['encoded_first_record_bytes']} |")
    lines += ['', 'Encoded size is the first record in each dataset, not a dataset average.',
              'Quarry C/C++ use BRF v2; Protobuf uses its own wire format. Payload sizes are not expected to match across formats.',
              'C and the C++ C-storage wrapper use default presence storage. Compact presence is not included.', '',
              '## Executable sizes', '', 'Fully static sizes include library code; these are not codec-only footprints.', '',
              '| Backend | Bytes |', '|---|---:|']
    lines += [f'| {backend} | {size} |' for backend, size in manifest['binary_bytes'].items()]
    lines += ['', '## Toolchain', '', '```text', manifest['toolchain'].strip(), '```', '',
              'Raw JSON results, input/source/binary hashes, and before/after target snapshots are retained alongside this report.']
    (args.results / 'report.md').write_text('\n'.join(lines) + '\n')
    print(args.results / 'report.md')


if __name__ == '__main__':
    main()
