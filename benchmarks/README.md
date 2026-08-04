# Quarry benchmark workload suite

PR-155 extends the PR-152/153 harness with a separately labeled Protocol
Buffers comparison. It reports measurements only; it does not rank backends,
set performance thresholds, or make wire-compatibility claims.

## Workloads

All cases use the same generated `benchmark.workload.Workload` type. The
deterministic dataset changes presence and payload profiles so each backend
processes the same logical records.

* `telemetry` — populated integer, floating-point, boolean, enum, timestamp,
  and counter fields with small payloads and high operation count;
* `configuration` — sparse presence pattern with bounded strings, bytes,
  enum values, and small arrays;
* `nested` — cross-namespace nested record and repeated-record fields;
* `large` — bounded strings and bytes, larger scalar arrays, and repeated
  nested records within fixed capacities;
* `stress` — a tiny mostly-scalar message with a high record count to expose
  fixed per-operation overhead.

The schema includes a local enum and an imported `Child` record. The dependency
root is generated separately into the same output tree, as required by the
one-explicit-root compiler workflow.

## Build

Benchmark targets are opt-in, release-optimized, and excluded from normal
debug, test, and coverage builds:

```sh
cmake --preset benchmark
cmake --build --preset benchmark --target quarry_benchmark_cpp quarry_benchmark_c \
  quarry_benchmark_protobuf_cpp quarry_benchmark_protobuf_cpp_arena --parallel
```

The Protocol Buffers targets use the repository's configured `protoc` and
`libprotobuf`. The Python comparison additionally requires the matching Python
`protobuf` runtime; the Docker development image installs it as
`python3-protobuf`.

For Linux/GCC use a clean container build tree:

```sh
docker compose run --rm dev cmake --preset docker-benchmark
docker compose run --rm dev cmake --build --preset docker-benchmark \
  --target quarry_benchmark_cpp quarry_benchmark_c --parallel
```

## Run and summarize

The runner accepts one case or all cases, one backend or all backends, and one
operation or all operations. Use `--backend protobuf` for the three protobuf
implementations, or `--backend all` for Quarry C++, C, Python, protobuf C++,
protobuf C++ Arena, and protobuf Python. It generates the same datasets, runs
validation before timing, writes one JSON result per case/backend/operation,
and enforces encoded-size parity only within one wire format.

```sh
python3 benchmarks/scripts/run_benchmarks.py \
  --build-dir build/benchmark \
  --case all --backend all --operation round_trip \
  --output-dir benchmark-results

python3 benchmarks/scripts/summarize_results.py \
  --result-dir benchmark-results \
  --json benchmark-results/summary.json \
  --markdown benchmark-results/summary.md
```

Use `--operation encode` or `decode` for a single operation. The runner also
supports `--case telemetry`, `configuration`, `nested`, `large`, or `stress`,
and `--backend cpp`, `c`, `python`, `protobuf-cpp`, `protobuf-cpp-arena`, or
`protobuf-python`. `--warmup`, `--iterations`, `--samples`, and `--seed` are
configurable; generated datasets remain deterministic for a given seed and
case.

Each result directory also contains a machine-readable `manifest.json` with
toolchain, compiler, machine, seed, schema, and implementation provenance.
Absolute paths are intentionally omitted. The manifest date identifies the
execution day; timing and resource results remain local measurements.

## Measurement contract

Datasets use a fixed integer transition function, stable field presence, stable
strings and bytes, and stable record order. No wall clock or platform random
source contributes to logical input data.

Each harness prepares records and decode inputs before timing, performs separate
warm-up iterations, uses a monotonic high-resolution clock, records raw sample
durations, and reports median latency and throughput. Encoded size is checked
across C++, strict-C99 C, and Python. Generated source and benchmark executable
sizes are recorded as informational build metrics.

C uses fixed caller-owned buffers. C++ uses the generated value API and its
current owning return values. Python includes interpreter and object-allocation
overhead. These are intentional runtime-model differences, not timing claims
about language or backend quality. Run on an otherwise idle, stable machine
when measurements are intended to be compared.

The version 2 JSON format retains the PR-152 identity and timing fields and adds workload,
generated-source-size, generated-file-count, object-size, binary-size,
runtime-size, and allocation metadata. The `resources` object uses `null` for
metrics that do not have an equivalent portable measurement. Native C++ and C
object sizes use `sizeof`; Python object size and allocation counters are not
treated as equivalent values. Quarry's native runtime is header-only, so its
runtime library size is reported as deterministic zero; generated executable
size is reported separately. C allocation count and bytes are zero for this
harness because its path uses static/caller-owned storage without allocation.
The Markdown summary presents measurements only and deliberately does not rank
backends.

Resource values are expected to be stable for the same build and generated
sources. Timing values are not. Binary sizes depend on the selected release
toolchain and platform, so they are build measurements rather than universal
claims.

BRF and protobuf encoded sizes are reported independently. They are different
wire formats and must not be compared for byte equality. The logical records,
field presence, dataset seed, and validation rules are shared. Protobuf C++
normal allocation and Arena allocation are separate implementations and are
never merged into one result.

The comparison includes Quarry C++/C/Python, protobuf C++/C++ Arena/Python,
and no protobuf C row: Protocol Buffers has no official strict-C99 runtime with
the same fixed-capacity/no-heap contract as Quarry C.

## Validation

```sh
python3 benchmarks/scripts/test_infrastructure.py
python3 benchmarks/scripts/validate_results.py benchmark-results/*.json
```

The smoke tests check deterministic registration/datasets, invalid benchmark
selection, result structure, summary generation, and encoded-size parity. They
do not assert latency or throughput values. Full benchmark execution remains
manual/release-oriented and outside normal CTest and coverage.

Future work is PR-156 for performance reporting and regression analysis. RSS, stack usage,
cache behavior, CPU cycles, perf counters, and allocator profilers remain
intentionally deferred.
