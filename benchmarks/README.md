# Quarry benchmark harness

This directory contains the common infrastructure for future Quarry C++, C,
and Python measurements. PR-152 provides one proof case and verifies the
runner, deterministic dataset format, generated consumers, timing mechanics,
and JSON result validation. It does not publish performance claims, compare
backends, or include Protocol Buffers.

## Layout

* `schemas/` — checked-in BRD benchmark inputs;
* `scripts/generate_dataset.py` — fixed-seed language-neutral dataset generator;
* `cpp/`, `c/`, and `python/` — generated-code proof harnesses;
* `scripts/run_benchmarks.py` — release-build runner;
* `scripts/validate_results.py` — structural result validator;
* `scripts/test_infrastructure.py` — deterministic/invalid-result smoke checks.

Generated files, executables, and results are build or temporary artifacts and
are not checked in.

## Configure and build

The benchmark preset is separate from normal debug, coverage, and Docker build
trees and enables release optimization:

```sh
cmake --preset benchmark
cmake --build --preset benchmark --target quarry_benchmark_cpp quarry_benchmark_c --parallel
```

The target invokes the supported Quarry compiler for C++, strict-C99 C, and
Python into `build/benchmark/benchmarks/generated`. Python is run by the
runner rather than built as a native executable. No schema generation or
process startup is included in timed operations.

## Run the proof case

```sh
python3 benchmarks/scripts/run_benchmarks.py \
  --build-dir build/benchmark \
  --output-dir benchmark-results \
  --operation round_trip
```

Choose `--backend cpp`, `c`, or `python` to run one backend, and choose
`--operation encode`, `decode`, or `round_trip`. `--warmup`, `--iterations`,
`--samples`, `--seed`, and `--count` are configurable. The default proof case
uses seed 152, 16 records, two warm-up iterations, ten measured iterations,
and five samples.

The runner prepares records and decode inputs before timing, warms up
separately, uses a monotonic clock, retains a validation checksum, and writes
one JSON result per backend. C uses fixed caller-owned buffers. C++ uses the
generated value API, whose current codec returns owning vectors; this backend
behavior is recorded as a methodology difference rather than hidden. Python
uses the generated package and installed-equivalent runtime.

## Dataset contract

The proof dataset is a deterministic pipe-delimited text fixture with metadata
for format, case, schema, seed, and record count. Values include a sequence,
boolean, float, bounded string, bounded bytes as hexadecimal, and a bounded
`uint32` array. The generator uses a fixed integer transition function rather
than platform-dependent wall-clock or random-device state. The same file is
consumed by all three harnesses.

## Result format

Results use format version 1 and include the case/backend/language, operation,
schema and dataset identity, warm-up and measured counts, raw sample durations,
operation count, latency, throughput, encoded size, validation status, and
runtime/build metadata. The runner adds compiler/interpreter version, Quarry
version, generated-code API epoch, release mode, operating system, architecture,
CPU description, UTC execution time, and git commit. Absolute paths are not
written into JSON.

`validate_results.py` checks required keys, finite non-negative numeric values,
operation-count arithmetic, successful validation, unique backends, and proof
case identity/encoded-size parity. It never compares timing values.

```sh
python3 benchmarks/scripts/test_infrastructure.py
python3 benchmarks/scripts/validate_results.py benchmark-results/proof-cpp-round_trip.json \
  benchmark-results/proof-c-round_trip.json \
  benchmark-results/proof-python-round_trip.json
```

Run benchmarks on an otherwise idle, stable machine. Python interpreter
overhead is part of the measured Python environment. C's fixed-capacity,
caller-owned/no-heap model and C++'s generated value ownership are different
contracts, so results must not be interpreted solely as backend quality or as
a universal language ranking. PR-152 has no timing threshold or published
performance baseline.

The planned follow-ups are PR-153 for the full Quarry schema suite, PR-154 for
optional resource measurements, PR-155 for a separately labeled Protocol
Buffers comparison, and PR-156 for scheduled/manual performance reporting and
regression analysis.
