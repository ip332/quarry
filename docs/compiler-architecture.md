# Compiler Architecture

## Goals

The Breadcrumbs schema compiler turns schema source files into validated
compiler models and generated artifacts.

The compiler architecture should:

* resolve imports into a canonical namespace model
* support a normative YAML source frontend during migration to the legacy
  declaration-syntax compiler pipeline
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

Imports are source-level compiler directives only.

After import resolution, imported declarations are represented as normal
namespace, record, enum, or future semantic items with fully qualified names.

Source metadata supports diagnostics and developer tooling. It is not part of
the binary record format.

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
* AST flows into import resolution, legacy symbol construction, legacy
  semantic validation, and layout computation.

The current migration boundary is intentionally uneven: scalar- and
enum-shaped schemas can flow through the existing downstream pipeline today,
and bounded-variable arrays are preserved through YAML decoding,
source-schema normalization, semantic validation, and Schema IR. The
remaining work is downstream policy and runtime support rather than
representation. The normalized YAML pipeline no longer routes through a
source-schema-to-AST compatibility projection; AST remains owned by the
independent legacy declaration-syntax pipeline.

During migration, the legacy declaration-syntax frontend and the normative YAML
frontend both remain available. Legacy declaration-syntax tests continue to
exercise the temporary lexer/parser frontend.

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

Resolved IR is the canonical namespace tree and declaration graph after import
resolution.

It exists to resolve imports and names into a canonical declaration graph.

Resolved IR contains fully qualified references.

Resolved IR does not contain import objects.

Imported declarations appear as normal namespace, record, enum, or future
semantic items in the resolved namespace model.

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

It contains computed binary layout metadata such as sizes, offsets, alignment
requirements, `fieldIndex` assignments, and field presence metadata.

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

Imports are source-level compiler directives only.

Imports must not survive as first-class entities in:

* Resolved IR
* Semantic IR
* Layout IR
* Schema IR
* the binary record format

After import resolution, imported declarations are represented as normal
namespace/record/enum items with fully qualified names.

Backends should not need to understand source imports in order to generate
artifacts.

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
