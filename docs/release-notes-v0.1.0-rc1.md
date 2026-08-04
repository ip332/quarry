# Quarry v0.1.0-rc1

Quarry v0.1.0-rc1 is a release candidate for the language-neutral schema
compiler and BRF serialization system.

## Highlights

* YAML `.brd` schemas compile deterministically through a compiler-owned import
  graph and output plan.
* Production C++, strict-C99 C, and Python backends generate BRF-compatible
  code.
* Imported records and enums are supported across namespaces in all three
  backends.
* Installed CMake consumers, strict-C99 consumers, Python packages, and
  cross-language interoperability are validated by the project test suites.
* The isolated protobuf translator accepts descriptor sets and emits bounded
  Quarry BRD source with enum and migration metadata.
* Quarry custom protobuf bounds options, external bounds files, and the narrow
  Nanopb `max_size`/`max_count` compatibility adapter are available.
* Public C++, C, Python, cross-namespace, and C++→Python interoperability
  examples are included.
* The benchmark framework provides deterministic workloads, provenance, JSON
  results, SVG charts, and a documented Public API Benchmark measurement model.

## Compatibility

The C++ generated-code API epoch is `3`, the C epoch is `2`, and the Python
epoch is `1`. These epochs are independent of the `0.1.0` package version and
the BRF wire-format version. Generated code should use the runtime from the
same Quarry release, and compatibility guards diagnose mismatched generated
runtime epochs.

BRF interoperability is validated across C++, C, and Python for the supported
schema subset. The protobuf translator does not preserve or decode protobuf
wire bytes; it translates logical declarations into Quarry BRD source.

## Known limitations

* Each schema root is compiled explicitly. Imported dependency roots must be
  generated separately into the same output directory.
* Source-unit import cycles and recursive by-value record graphs are rejected.
* Nested arrays are unsupported.
* The protobuf translator consumes descriptor sets, requires explicit bounds,
  and rejects unsupported protobuf constructs rather than silently dropping
  semantics.
* Nanopb compatibility supports exact field paths and bounds mapping only;
  wildcards and options without BRD meaning are rejected.
* Benchmark timing is advisory and measures the documented public APIs, whose
  ownership and allocation models differ across languages.
* The Python runtime and protobuf translator are distributed through the
  documented source/wheel workflows; publication automation and PyPI
  publication are not part of this release candidate.

## Validation

The release candidate is qualified by native and Docker/Linux/GCC builds and
tests, strict-C99 validation, installed consumers, packaging, deterministic
generation, cross-language interoperability, translator follow-through, and
coverage regression checks. See the repository report and documentation for
the exact supported workflows.

Feedback and issue reports are welcome while the release candidate is being
evaluated.
