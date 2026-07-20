# Compiler Architecture

## Goals

The Quarry schema compiler turns schema source files into validated
compiler models and generated artifacts.

The compiler architecture should:

* preserve source-level import declarations on the legacy AST path as
  compatibility syntax only
* support a normative YAML source frontend while the legacy declaration parser
  remains parser/AST compatibility infrastructure
* validate schema semantics before layout computation
* compute deterministic binary layout metadata
* preserve compiler-managed identifiers such as `recordId` and `fieldIndex`
* provide backend-neutral IR for generated artifacts
* keep runtime and binary artifacts limited to runtime needs

---

## Non-Goals

This document does not define:

* schema language syntax
* binary record encoding details
* manifest protobuf fields
* generated runtime APIs
* compiler implementation algorithms
* backend plugin mechanisms

Those details belong to dedicated specifications or implementation documents.

The normative `.brd` source-language syntax and language semantics are defined
only in `docs/specifications/schema-language.md`.

---

## Design Principles

Every semantic object should know where it came from, but runtime/binary
artifacts should only contain what the runtime needs.

Each compilation stage produces a new IR layer rather than mutating previous
layers.

Earlier layers remain available for diagnostics, debugging, and tooling. This
keeps compiler behavior deterministic and easier to inspect.

Import declarations are source-level syntax only on the legacy AST path. They
are retained for compatibility parsing, but the current compiler does not
resolve them into a document graph.

The legacy declaration-syntax frontend is compatibility infrastructure, not a
supported standalone compiler frontend. The production compiler front end is
the normalized YAML pipeline.

Source metadata supports diagnostics and developer tooling. It is not part of
the binary record format.

Compiler C++ headers and CMake targets currently define source-tree APIs, not
an installed external compiler SDK. `compiler/README.md` owns the supported
source-tree entry-point boundary.

Runtime binary-record mechanics live outside the compiler. Generated C++ schema
artifacts may depend on the small `quarry_runtime` target for byte-level
encoding and structural parsing, but runtime code must not depend on compiler
IRs, YAML, Schema IR protobufs, semantic analysis, layout internals, or backend
code. Generated C++ keeps schema-specific decode policy, including expected
record IDs, field indexes, field types, and enum value sets. Generated C++
also maps schema-specific codec failures, while the runtime owns the
representation-neutral result containers and structural parse/read error enums.
Compatibility optional codec wrappers may discard that detail without changing
the BRF bytes.

Runtime parser hardening is exercised through opt-in Clang/libFuzzer targets.
Those targets are outside the normal debug build and feed arbitrary bytes to
the generic BRF parser and to a representative generated-style decoder. Seed
corpus entries are stored as reviewable hexadecimal byte files under
`fuzz/corpus/brf`.

The installed CMake package exposes the header-only runtime as
`Quarry::runtime` and the schema compiler host executable as
`Quarry::schema_compiler`. Compiler libraries, compiler headers, generated
Schema IR protobuf targets, tests, and fuzzers remain source-tree artifacts and
are not installed as public SDK surfaces. `docs/distribution-model.md` owns the
supported downstream SDK boundary and classifies generated code, generated
protobufs, tools, tests, fuzzers, and examples.
`docs/schema-compiler-tool-distribution.md` records the installed compiler-tool
contract, native imported-target discovery policy, and downstream CMake
integration boundaries.

Generated-output naming belongs to backend-owned planning. The compiler keeps a
single internal generated-output planning model that feeds rendering and the
schema compiler's `--list-outputs` query mode without reimplementing filename
rules outside the backend. Tool-side file writing remains separate, and no
CMake helper, depfile, manifest, or stale-output cleanup policy is implied by
the query mode.

---

## Compilation Pipeline

The compiler pipeline is:

* Legacy declaration-syntax source files flow through the lexer/parser into
  Parsed AST.
* YAML source files flow through `YamlParser`, `YamlDocument`, source-schema
  decoding, and source-schema normalization into the neutral source-schema
  model.
* The production-facing YAML frontend orchestrates the import-free YAML path
  through validated Schema IR by feeding the normalized source schema directly
  into symbol construction, semantic validation, layout computation, and
  Schema IR lowering.
* Legacy declaration-syntax source files flow only through the parser-owned
  AST compatibility surface. Import declarations remain parsed syntax only and
  are not consumed by downstream compiler stages.

Scalar-, enum-, and bounded-array-shaped schemas flow through the complete
downstream pipeline today: YAML decoding, source-schema normalization,
semantic validation, layout computation, Schema IR, and backend code
generation all support them, including arrays of records and cross-namespace
array element references. Nested arrays (an array whose element type is
itself an array) remain rejected by semantic validation as an unsupported
v0.1 construct. The normalized YAML pipeline no longer routes through a
source-schema-to-AST compatibility projection; AST remains owned by the
independent legacy declaration parser and AST tests.

Source loading is intentionally caller-owned: `SourceManager` stores
already-loaded source text and source labels, while the parser consumes one
registered source file at a time. The repository does not yet define a
multi-file declaration-syntax loader or an import-name-to-path mapping.
There is no production declaration-syntax root-source parsing facade yet; a
caller that wants to parse a file path still performs source loading and
registration itself.

During migration, the legacy declaration-syntax parser and the normative YAML
frontend both remain available. Legacy declaration-syntax tests continue to
exercise the temporary lexer/parser surface.

Each stage should consume the previous stage's output and produce a model with
fewer source-syntax concerns and more compiler-owned structure.

---

## IR Layers

The compiler uses separate IR layers so source syntax, semantic validation,
binary layout, and generated artifacts do not collapse into one model.

### Parsed AST

The Parsed AST is source-syntax oriented.

It exists to preserve source syntax and source-level constructs before import
resolution and semantic validation.

It preserves source structure, import declarations, author-written names, and
syntax-level constructs.

The Parsed AST may contain imports because imports are part of source syntax.

### Resolved IR

Resolved IR is the canonical namespace tree and declaration graph that a
future import-resolution stage would produce.

It would resolve imports and names into a canonical declaration graph.

Resolved IR contains fully qualified references.

Resolved IR does not contain import objects.

Imported declarations would appear as normal namespace, record, enum, or
future semantic items in the resolved namespace model.

### Semantic IR

Semantic IR is the validated compiler-facing model.

It exists to capture the meaning of the schema independent of source syntax.

It contains resolved types, records, enums, fields, and semantic attributes.

Semantic IR should be suitable for compatibility analysis, documentation
generation, and layout computation.

### Layout IR

Layout IR is the deterministic binary layout model.

It exists to compute the deterministic binary representation.

Layout IR is derived from Semantic IR.

Layout IR augments semantic objects with computed binary layout information.

Today, the Layout Model computes `recordId` and `fieldIndex` assignments
only. Sizes, offsets, alignment requirements, and field presence metadata are
intended future Layout IR content but are not computed by this pass yet; see
`docs/compiler-passes.md` for the current Layout Computation contract.

Layout IR should not introduce new semantic meaning.

Layout IR should not be treated as an author-editable model.

Layout IR should describe the binary representation without exposing
source-level syntax or import directives.

### Schema IR

Schema IR is the compiler's final resolved state used by backends and tools.

It exists to provide the stable, backend-facing representation of one fully
compiled schema.

Schema IR is backend-neutral.

Schema IR represents one resolved compilation.

Schema IR is not source-control history.

Schema IR may later have a serialized debug or tooling form. That form is
separate from the compact runtime manifest.

---

## Import Resolution Policy

There is no active import-resolution stage in the current repository.
Import declarations remain legacy parser syntax only and are ignored by the
current downstream compiler pipeline.

---

## Source Metadata Policy

Every semantic object should carry source-origin metadata.

This applies to:

* namespaces
* records
* enums
* fields
* future semantic objects

Source metadata should include at least:

* source unit or file
* source span

Prefer source spans over single line/column references.

Source metadata supports:

* diagnostics
* generated documentation
* IDE and LSP features
* debug artifacts
* developer-facing inspection tools

---

## Binary Format Boundary

Source metadata must never be emitted into the binary record format.

Source metadata must not be included in compact runtime or binary manifests.

Source metadata may be included in developer-facing artifacts such as:

* JSON debug manifests
* generated comments
* schema documentation
* IDE metadata
* diagnostic output

Runtime and binary artifacts should contain only the information needed for
runtime behavior.

Compiler metadata exists to support diagnostics, generated documentation,
IDE/LSP features, debug artifacts, and tooling.

Runtime metadata exists only to support runtime operation.

Binary and runtime artifacts should contain runtime metadata only.

---

## Backend Interface

Backends should consume Schema IR rather than reparsing schema source.

Backends may generate:

* runtime bindings
* binary codecs
* validators
* PBTXT manifests
* developer-facing debug manifests
* documentation
* IDE metadata
* test artifacts

Manifest generation is one backend output alongside code generation,
documentation, debug metadata, and tests.

Backends should not depend on import declarations as first-class model objects.

Backends should preserve the compiler's resolved names, semantic decisions,
layout metadata, `recordId`, and `fieldIndex` values.

---

## Open Questions

Open questions for later specifications or implementation documents:

* exact in-memory representation of each IR layer
* exact source span model
* debug manifest format
* backend interface shape
* diagnostic code format
* IDE metadata format
* whether Schema IR should remain an in-memory compiler structure only, or also
  have a serialized debug/tooling representation for documentation generators,
  compatibility checkers, visualization tools, and IDE integrations
