# Raspberry Pi CPU sampling

`sample.c` uses Linux `perf_event_open` directly, so the target needs neither
installed `perf` nor a compiler. It samples user-space instruction pointers at
997 Hz using CPU cycles (falls back to software CPU clock if opening cycles
fails). The program runs on one thread; inheritance is disabled to allow the
per-task mmap ring buffer. No sysctl or CPU clock changes are needed.

Build after the normal Pi runner has populated `/out`:

```sh
sh /src/benchmarks/raspberry_pi/profiling/build.sh
```

Deploy `sample`, the two symbolized benchmark executables, `pin_cpu`, and the
dataset into a target temporary directory. Example from that directory:

```sh
./pin_cpu 0 ./sample -o wrapper-encode.tsv ./pin_cpu 2 \
  ./quarry_benchmark_cpp_c_storage --dataset telemetry.dataset --case telemetry \
  --operation encode --output wrapper-encode.json \
  --warmup 2 --iterations 30000 --samples 1
```

Repeat with `decode` and `quarry_benchmark_protobuf_cpp`. Each telemetry run
measures 3,840,000 operations. Retrieve the TSV and JSON files, then resolve
addresses using the exact corresponding executable:

```sh
python3 summarize.py wrapper-encode.tsv quarry_benchmark_cpp_c_storage \
  --output wrapper-encode.summary.json
```

Run symbolization in the cross-toolchain container if host `addr2line` does not
support ARM. The executable is static/non-PIE, so sampled addresses need no
relocation. `build.sh` also extracts baseline/profile `.text` for comparison.
Adding `-g1` preserved Protobuf `.text` exactly in the initial run. Quarry had
only linker Thumb-veneer ordering/reference differences in libc; the codec and
benchmark instructions were identical by disassembly comparison.

Percentages are **self instruction-pointer sample shares**, not inclusive
call-tree costs or exact per-function timings. Inlined functions may receive
separate source attribution. Sampling covers the process, including startup,
validation, warmup and output; the long measured loop dominates. Check the
reported lost-event count. Sampled run timings are diagnostic, not replacements
for the ordinary repeated timing benchmark. No timing instrumentation or frame
pointer changes are added to the codecs.
