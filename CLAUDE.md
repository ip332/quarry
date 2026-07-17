# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Breadcrumbs is an early-stage (pre-1.0, "planning and architecture phase"), open-source,
schema-driven C++ platform for secure edge-to-cloud systems. The repository currently
implements the front half of that vision: a schema compiler (`.brd` YAML schema source →
Schema IR → generated C++) and a small header-only runtime for encoding/decoding the
Binary Record Format (BRF). Device/cloud/protocol/SDK components described in `docs/vision.md`
are largely not implemented yet — their directories (`agent/`, `cloud/`, `protocol/`, `sdk/`)
are placeholders reserved for future work.

Design principles (see `docs/principles.md`): embedded-first, serialized-first (binary
records are the canonical runtime representation), schema-driven, compile-time knowledge
(no runtime schema interpretation in production).

## Build, Test, and Lint Commands

The project uses CMake presets (`CMakePresets.json`). Configure, build, and test dependencies:
protobuf, absl, and libyaml (`brew install protobuf abseil libyaml` on macOS; on CI,
`apt-get install protobuf-compiler libprotobuf-dev libabsl-dev libyaml-dev`).

```sh
# Configure + build + test (standard debug workflow)
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug

# Run a single test binary or filter by name (GoogleTest)
./build/debug/tests/yaml_compiler_test
ctest --preset debug -R yaml_compiler_test

# Debug build with clang-tidy enabled
cmake --preset debug-clang-tidy
cmake --build --preset debug-clang-tidy --parallel

# Fuzzers (Clang + libFuzzer + ASan/UBSan, opt-in, not part of normal debug build)
cmake --preset debug-fuzz
cmake --build --preset debug-fuzz --parallel
./build/debug-fuzz/fuzz/brf_parse_fuzzer fuzz/corpus/brf
python3 fuzz/run_seed_corpus.py <fuzzer-binary>   # replay reviewable hex seed corpus
```

`BREADCRUMBS_WARNINGS_AS_ERRORS` is `ON` by default (`-Wall -Wextra -Wpedantic -Werror` /
`/W4 /WX`) — new code must build warning-clean.

Formatting/linting is enforced via pre-commit (`.pre-commit-config.yaml`):
`clang-format` (LLVM-based style, 100 columns, 4-space indent — see `.clang-format`) runs on
`compiler/**/*.{cpp,hpp}` and `tests/**/*.{cpp,hpp}` at commit time; a full
configure/build/`ctest --preset debug` cycle runs at push time. `.clang-tidy` enables
`clang-analyzer-*`, `bugprone-*`, `performance-*`, `portability-*`, `readability-*` (with a
few readability checks disabled) over `compiler/` and `tests/`.

There is no separate lint-only invocation beyond `debug-clang-tidy` / pre-commit hooks — run
those rather than inventing new tooling.

## Architecture

### Compilation pipeline

The schema compiler is a strict, one-directional pipeline where each pass consumes the
previous stage's output and produces a new, more-constrained representation (earlier layers
are kept around for diagnostics/tooling, never mutated in place):

```
YAML source (.brd)
  -> YamlParser -> YamlDocument -> schema decoder -> source-schema normalization
  -> Symbol Model (namespace builder, compiler/symbols)
  -> Semantic Model (semantic validator, compiler/semantic)
  -> Layout Model (layout computation — recordId/fieldIndex/sizes/offsets, compiler/layout)
  -> Schema IR (schema_ir.proto, backend-neutral, compiler/schema_ir)
  -> Backend (compiler/backend) -> generated C++ files
```

Two frontends coexist during migration:
- **The normative YAML frontend** (`frontend::YamlCompiler::compile`, `compiler/frontend`) is
  the production path. It is import-free and feeds normalized YAML directly through symbols,
  semantic validation, layout, and Schema IR.
- **A legacy declaration-syntax lexer/parser** (`compiler/parser`, `compiler/ast`) exists only
  for parser/AST compatibility testing. It is not wired into the production pipeline, and
  import declarations it parses are syntax only — nothing resolves them.

Full pass-by-pass contracts (purpose, inputs/outputs, invariants, diagnostics) are documented
in `docs/compiler-passes.md`; the IR layer definitions and design rationale are in
`docs/compiler-architecture.md`. Read those before changing pipeline behavior — this repo
treats architecture docs as authoritative "what" and expects code to match them.

### Directory-to-stage mapping (`compiler/`)

| Directory | Role |
| --- | --- |
| `support/` | Source manager, file system abstraction, source locations |
| `diagnostics/` | Diagnostic engine |
| `context/` | `CompilerContext` — shared infra (source manager, diagnostics, options, `recordId` allocation state) passed through every pass |
| `ast/`, `parser/` | Legacy declaration-syntax lexer/parser/AST (compatibility only) |
| `yaml/` | `YamlParser`, `YamlDocument`, schema decoder |
| `source_schema/` | Normalized, neutral source-schema model |
| `symbols/` | Namespace builder → Symbol Model |
| `semantic/` | Semantic validator → Semantic Model |
| `layout/` | Layout computation → Layout Model (binary layout metadata) |
| `schema_ir/` | Schema IR builder/validator (`proto/breadcrumbs/schema_ir.proto`) |
| `frontend/` | `YamlCompiler` — orchestrates YAML → Schema IR |
| `backend/` | Schema IR → generated C++ output planning and rendering |
| `imports/` | Reserved; no active import-resolution pass exists yet |

Each CMake target in `compiler/CMakeLists.txt` corresponds 1:1 to one of these directories
(`breadcrumbs_compiler_<name>`), with dependencies flowing strictly downstream along the
pipeline above.

### Runtime (`runtime/`)

Header-only `breadcrumbs_runtime` target (installed as `Breadcrumbs::runtime`). Owns only
generic BRF byte-level mechanics: header emission/parsing, Field Directory
emission/parsing, LEB128 varuint, big-endian scalars, string/bytes/array/nested-record
codecs, and `EncodeResult`/`DecodeResult` error containers. Generated C++ code supplies all
schema-specific knowledge (record IDs, field indexes, field types, enum values) on top of
these primitives.

**Critical invariant**: runtime code must never depend on compiler libraries, YAML, Schema IR
protobufs, source-schema models, symbols, semantic validation, layout, or backend code — this
is enforced by convention, not currently by a CI check, so watch it when editing
`runtime/binary_record.hpp`.

`kGeneratedCodeApiVersion` (in generated `runtime/version.hpp`, sourced from the single CMake
scalar `BREADCRUMBS_GENERATED_CODE_API_VERSION` in the top-level `CMakeLists.txt`) is a
narrow compile-time compatibility guard between generated code and the runtime header —
it is *not* the package release version and does not imply BRF wire compatibility. Bump it
only when generated-code/runtime compatibility actually changes, and keep it in sync across
CMake, the installed package metadata (`Breadcrumbs_GENERATED_CODE_API_VERSION`), and the
schema compiler's `--print-generated-code-api-version` query.

### `tools/` — `breadcrumbs-schema-compiler`

The CLI entry point (`tools/schema_compiler`) built on the compiler libraries above. It
compiles exactly one `.brd` file through `YamlCompiler` and the backend, and writes generated
files. Supports `--list-outputs` (print planned generated paths without writing),
`--print-generated-code-api-version` (machine-readable compatibility query used by CMake),
`--version`, `--help`. Full CLI contract and exit codes are documented in `tools/README.md` —
treat that file as the source of truth for flag behavior rather than re-deriving it from code.

### Distribution boundary

Only two things are installed/exported: `Breadcrumbs::runtime` (header-only) and
`Breadcrumbs::schema_compiler` (executable), plus CMake package files
(`BreadcrumbsConfig.cmake`, `BreadcrumbsGenerate.cmake`, etc.) and the
`breadcrumbs_generate_cpp()` helper. Everything else under `compiler/` (compiler libraries,
Schema IR protobuf), `tests/`, `fuzz/` is a source-tree implementation detail, not a public
API/ABI surface — don't treat internal compiler headers as a stable contract, and don't add
`install()`/`export()` rules for them without checking `docs/distribution-model.md` and
`docs/schema-compiler-tool-distribution.md` first, which define this boundary deliberately.

`examples/cpp/schema_compiler_cmake` is the canonical example of the supported downstream
`breadcrumbs_generate_cpp()` integration pattern; `examples/cpp/basic_encode_decode` is the
canonical minimal runtime-only consumer.

## Working in this repo

- Architecture documents in `docs/` (`vision.md`, `principles.md`, `architecture/*.md`,
  `docs/compiler-architecture.md`, `docs/compiler-passes.md`, `docs/distribution-model.md`)
  encode deliberate, narrow design decisions (e.g., "no import resolution yet," "one primary
  record per schema file," "source metadata must never reach the binary format"). Several
  READMEs (`compiler/README.md`, `tools/README.md`, `runtime/README.md`) explicitly call out
  what is *intentionally* unsupported — read the relevant doc before assuming a limitation is
  accidental or expanding scope (e.g., adding multi-file input, import resolution, or new
  install targets) without confirming it's in scope.
- Schema IR (`proto/breadcrumbs/schema_ir.proto`) is the stable backend-facing contract;
  changes to it affect every backend and golden test — check `tests/fixtures/schema_ir*` and
  `tests/schema_ir/schema_ir_golden_test.cpp`.
- `.brd` YAML fixtures live under `tests/fixtures/schema_ir_yaml/` (paired with expected
  `.pbtxt` Schema IR) and `tests/fixtures/backend/`; use these as the pattern for new schema
  test cases rather than hand-rolling ad hoc schemas.
- `jira/backlog.md` tracks planned work items for this project if you need current priorities.
