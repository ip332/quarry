# BRF fuzz seed corpus

Corpus entries are stored as whitespace-separated hexadecimal bytes so the
seeds remain reviewable in source control.

Use `fuzz/run_seed_corpus.py` to convert these files to temporary raw inputs and
run a fuzz executable against them.

## QBS parser and configuration identity

`qbs_parse_fuzzer` reuses the existing C++ QBS parser source, compiled with
libFuzzer coverage, ASan and UBSan. It checks the new accessor against the stored
header. Successfully validated images also enter the existing C parser with a
bounded caller-owned workspace; when accepted, the C identity must match. The existing C parser and C
accessor implementations are instrumented too. Other linked compiler/runtime
library sources keep their ordinary build instrumentation policy.

With a Clang/libFuzzer toolchain selected, use the existing fuzz preset and
existing fixtures; keep generated fuzz mutations outside the checkout:

```sh
cmake --preset debug-fuzz -B build/qbs-fuzz
cmake --build build/qbs-fuzz --parallel
qbs_corpus="$(mktemp -d)"
cp tests/fixtures/generic_runtime_conformance/schema.qbs "$qbs_corpus/conformance.qbs"
build/qbs-fuzz/tools/quarry-schema-compiler --emit-qbs "$qbs_corpus/truck-minimal.qbs" examples/truck/schemas/configuration.brd
build/qbs-fuzz/tools/quarry-schema-compiler --emit-qbs "$qbs_corpus/truck-reflective.qbs" --qbs-reflective examples/truck/schemas/configuration.brd
build/qbs-fuzz/fuzz/qbs_parse_fuzzer -max_total_time=30 -max_len=65536 "$qbs_corpus"
```

This is a short development smoke run, not exhaustive fuzzing or hardware
validation. Existing BRF fuzzers and their reviewable hex seeds remain unchanged.
