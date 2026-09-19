# Quarry v0.1.30-rc.3

Quarry v0.1.30-rc.3 is a release candidate for the deterministic,
language-neutral schema compiler and BRF serialization framework. Relative to
RC2, this candidate completes the host-side QBS/BRF inspection workflow. It is
an additive tooling and reflection update; it does not introduce a new
serialization architecture.

## Highlights since RC2

- Added public generic-C access to validated QBS strings, record names, field
  names, enum type names, and canonical record identities.
- Added the streaming generic BRF printer, `quarry_brf_print()`, built on the
  existing generic traversal API.
- Added the installed `quarry-brf-inspect` host tool for schema-independent
  inspection of validated BRF records.
- Added reflective QBS generation through `--qbs-reflective`.
- Added QBS record discovery through `--list-records`.
- Added actionable inspector diagnostics for input files, selectors,
  malformed data, output limits, and resource failures.

PR #74 clarified the established release-version policy; it does not change
runtime behavior.

## Reflective QBS

Minimal QBS remains the default and remains suitable for runtime selection and
inspection. To include optional record, field, and enum-type display names,
request the existing reflective QBS profile:

```text
quarry-schema-compiler \
  --emit-qbs schema.qbs \
  --qbs-reflective \
  schema.brd
```

Reflective QBS adds display-name metadata already supported by QBS v1.
Canonical record identities remain mandatory independently of reflection.
QBS v1 does not contain symbolic enum-value names; enum values are therefore
shown numerically.

## Generic BRF printing

The public generic C API now provides `quarry_brf_print()`. It consumes the
existing `quarry_brf_traverse()` path and produces deterministic,
human-readable diagnostic output. The printer uses a callback-based streaming
writer, caller-owned bounded workspace, and configurable output, work, and
depth limits. It performs no heap allocation in the generic runtime path and
is not a serialization or JSON format.

## BRF inspection workflow

The installed host tool can discover records without a BRF input:

```text
quarry-brf-inspect \
  --qbs schema.qbs \
  --list-records
```

Inspect a record by its serialized ID:

```text
quarry-brf-inspect \
  --qbs schema.qbs \
  --brf record.brf \
  --record-id 1
```

Inspect a record by its canonical identity:

```text
quarry-brf-inspect \
  --qbs schema.qbs \
  --brf record.brf \
  --record-name benchmark.workload.Workload
```

Use `--brf -` to read the BRF record from standard input.

Canonical identity and display name are distinct. For example, discovery from
the workload schema produces:

Minimal QBS:

```text
1 benchmark.workload.Workload
2 benchmark.workload.shared.Child
```

Reflective QBS:

```text
1 benchmark.workload.Workload (Workload)
2 benchmark.workload.shared.Child (Child)
```

The canonical identity is the value accepted by `--record-name`; the optional
parenthesized value is only a reflective display name. Minimal QBS therefore
supports record listing, `--record-id`, and canonical `--record-name` without
requiring reflection. QBS has no runtime root-record concept, so discovery
lists every record present in the validated QBS image.

## Public QBS reflection APIs

The additive generic C API exposes validated, non-owning string views through
these public operations:

```text
quarry_qbs_get_string(...)
quarry_qbs_record_name(...)
quarry_qbs_field_name(...)
quarry_qbs_enum_name(...)
quarry_qbs_record_identity(...)
```

Returned views refer to the validated QBS input storage. Symbolic enum-value
names remain unsupported because QBS v1 does not store them.

## Diagnostics

`quarry-brf-inspect` now identifies input roles and paths, requested selectors,
malformed QBS/BRF data, output-limit exhaustion, and resource failures. For
example:

```text
quarry-brf-inspect: unable to read QBS file 'schema.qbs': No such file or directory
quarry-brf-inspect: record ID 99 not found
quarry-brf-inspect: output exceeded --max-output-bytes 1024
```

## Compatibility

- C++ generated-code API epoch: `3`.
- C generated-code API epoch: `2`.
- Python generated-code API epoch: `1`.
- Schema IR version: `1`.
- QBS format version: `1`.
- BRF format version: `2`.

The post-RC2 changes do not alter these compatibility identities, the QBS
format, the BRF format, or generated-code epochs.

### Important BRF compatibility warning

RC2 and RC3 use BRF v2. RC1 used BRF v1. BRF v1 artifacts from
`v0.1.30-rc.1` must not be assumed wire-compatible with the BRF v2 artifacts
in RC2 or RC3.

Generated code and runtime components should be taken from the same Quarry
release and matching generated-code epoch. Unchanged generated-code epochs do
not by themselves define generic runtime ABI compatibility.

## Benchmark baseline

The benchmark baseline from RC2 remains unchanged. Its measurements are
platform-specific and advisory. The comparison of generated-object and QBS
artifact sizes is not a complete application-footprint comparison because both
approaches also require runtime/support code.

## Known limitations

- Nested arrays are unsupported.
- Recursive by-value records are unsupported.
- Maps, unions/variants, and bit-fields are outside the current schema model.
- Symbolic enum-value names are not present in QBS v1.
- Protocol Buffers translation remains limited to the documented supported
  subset.
- Benchmark timing is advisory and platform-specific.

## Release version policy

Normal Git checkouts use the commit-count-derived development version. Official
release artifacts use the numeric version supplied by the release tag through
archive fallback metadata. Accordingly, `v0.1.30-rc.3` artifacts report numeric
version `0.1.30`; the `-rc.3` suffix identifies the release tag and is not part
of the numeric package version.

## Release and packaging

The release process builds deterministic source tar and zip archives, Python
wheel and sdist artifacts, release notes, and a SHA-256 checksum manifest.
Official RC3 artifact sizes and checksums will be reported only after the exact
release commit has been tagged and independently validated. RC3 artifacts are
not published to PyPI by this release process.

## Validation status

Current main has passed the available native, generic-runtime, interoperability,
strict-C99, packaging, installed-tool, and complete CTest validation, with
CTest currently at `53/53`. The exact RC3 release commit still requires the
separate release-artifact reconstruction, checksum, archive, install, and
publication-readiness audit. Those results are not claimed by these notes.
