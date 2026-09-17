# Quarry v0.1.30-rc.2

Quarry v0.1.30-rc.2 is a release candidate for the deterministic,
language-neutral schema compiler and BRF serialization framework. This
candidate completes the current generic C/QBS runtime milestone and adds a
reproducible release-artifact workflow.

## Highlights

- Added deterministic QBS emission directly from `.brd` schemas.
- Completed generic C encoding and decoded-value access for the currently
  supported schema model, including nested records, record arrays, and
  string/bytes arrays.
- Fixed generated-C record identity across imported schemas.
- Fixed generic-C record-array decoded views so public element access preserves
  validated relationships.
- Added a same-schema QBS-versus-generated-C benchmark baseline.

## Compiler and QBS

The schema compiler now supports:

```text
quarry-schema-compiler --emit-qbs output.qbs input.brd
```

The output is a deterministic binary QBS image containing the resolved schema,
including imported metadata, and is consumable by the generic C runtime.
Existing C, C++, and Python generation remains unchanged unless QBS emission is
explicitly requested.

## Generic C runtime

Generic C now has release-confidence coverage for scalar values, enums,
strings, bytes, primitive and enum arrays, string and bytes arrays, nested
records, record arrays, presence/absence, and bounded validation/access. Public
decoded-value access includes nested records and record-array elements. Unknown
fields continue to be skipped according to the compatibility contract.

## Generated C and interoperability

Generated C now propagates canonical resolved-schema record identities across
imported nested records and record arrays while preserving existing
application-facing codec entry points. Generated C and QBS-driven generic C
produce matching canonical BRF for the benchmark workload, with cross-runtime
validation coverage.

## Benchmark baseline

The benchmark uses `benchmarks/schemas/workload.brd` and one logical workload
for both implementations. Measured local optimized results are:

- QBS image: 881 bytes.
- Generated schema-specific object artifacts: 13,464 bytes.
- BRF payload: 252 bytes for both paths.
- QBS-driven encode: approximately 3.2x generated C.
- QBS-driven post-validation access: approximately 2.8x generated C.

These are platform-specific baseline measurements, not universal performance
claims. The object/QBS artifact ratio compares schema-specific artifacts only;
it is not a complete application-footprint comparison because both approaches
also require runtime/support code.

## Release and packaging

Release tooling now builds deterministic source tar and zip archives, Python
wheel and sdist artifacts, release notes, and a SHA-256 checksum manifest.
Source archives carry version fallback metadata and can be reconstructed,
configured, built, installed, and tested without `.git` metadata. Artifact
validation checks required contents, package metadata, and development-file
contamination.

## Compatibility

- C++ generated-code API epoch: `3`.
- C generated-code API epoch: `2`.
- Python generated-code API epoch: `1`.
- Schema IR version: `1`.
- QBS format version: `1`.
- BRF format version: `2`.

BRF v2 was introduced after `v0.1.30-rc.1` and is the current BRF contract;
RC1 BRF v1 artifacts must not be assumed wire-compatible with this candidate.
QBS format 1 records the BRF format used by its resolved schema metadata.
Generated code and runtime components should be taken from the same Quarry
release and matching generated-code epoch.

## Known limitations

- Nested arrays are unsupported.
- Recursive by-value records are unsupported.
- Maps, unions/variants, and bit-fields are outside the current schema model.
- Protocol Buffers translation remains limited to the documented supported
  subset.
- Benchmark timing is advisory and platform-specific.

## Validation

Current main passed native build and test, clang-tidy, coverage baseline,
Cortex-M cross-build, benchmark workflow, strict C99/interoperability coverage,
and complete CTest (`52/52`). Release artifact reconstruction passed configure,
build, install, and CTest; wheel installation/import passed. The sdist build
requires network access to obtain isolated build dependencies.
