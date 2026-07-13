# YAML Syntax Layer

This module owns the generic YAML syntax layer for Breadcrumbs source files.
It is a migration step toward the normative YAML `.brd` format defined in
`docs/specifications/schema-language.md`.

## Responsibility

The YAML layer parses `SourceManager`-owned text into an ordered,
source-located YAML document model. It preserves:

* scalar nodes
* sequence nodes
* mapping nodes
* mapping entry order
* sequence element order
* source ranges derived from parser marks

It does not decode Breadcrumbs schema vocabulary, build AST nodes, resolve
names, validate schema semantics, compute layout, or construct Schema IR.

## Source Schema Decoder

The YAML module also contains the schema-specific decoder that turns a
`YamlDocument` into a Breadcrumbs source schema model.

That source schema model preserves:

* namespace spelling
* record name, version, and logical record type spelling
* imports as source YAML
* ordered fields and enum declarations
* ordered enum values
* string-valued annotations
* field and enum source ranges

The decoder performs structural YAML-to-schema decoding only. It does not
perform semantic validation, name resolution, layout computation, or Schema IR
construction.

The migration bridge currently supports a fully traversable scalar-, enum-, and
bounded-array-shaped YAML subset. Bounded variable-length arrays are preserved
through YAML decoding and source-schema lowering so later compiler stages can
validate and lower the carried bounds.

## Source Schema Lowerer

The YAML module also contains the source-schema lowering pass that turns a
decoded Breadcrumbs source schema model into the existing syntax AST used by
the legacy compiler pipeline.

That lowering pass preserves:

* namespace spelling
* record names, versions, and logical record type spellings
* ordered fields, enums, and enum values
* field `max_bytes` and `max_elements` metadata
* source ranges from the YAML document model

The lowering pass does not perform schema semantics, symbol resolution, layout
computation, or Schema IR construction. It is a migration bridge from the
normative YAML frontend into the existing AST-based compiler stages.

Its current boundary is explicit: bounded variable-length arrays remain
represented in the lowered AST, and the existing Semantic, Layout, and Schema
IR stages now carry the validated bounds forward.

The production-facing `compiler/frontend` orchestration layer runs this YAML
pipeline through validated Schema IR, but it still uses the transitional AST
boundary internally. Direct `SourceSchemaDocument`-to-SemanticModel migration
has not happened yet.

## Restricted YAML Profile

The parser accepts one YAML document and rejects unsupported profile features
at syntax time, including:

* multiple documents
* anchors
* aliases
* custom tags
* merge keys

Unknown keys are preserved as ordinary YAML mappings. Duplicate mapping entries
are preserved in input order.

## Dependency Boundary

Allowed direct Breadcrumbs dependencies:

* `compiler/support`
* `compiler/diagnostics`
* `compiler/ast` for source-schema lowering

The implementation uses libyaml privately through the event API. Public
headers do not expose libyaml types.

## Migration Status

The legacy declaration lexer/parser remains temporarily supported while the
repository migrates toward YAML decoding. That legacy frontend is still used by
existing `.brd` tests and does not define the normative language contract.
The normative YAML frontend lowers through this module into the existing AST
pipeline during the migration period.

This module is the first step toward a later YAML-to-Breadcrumbs schema
pipeline that will eventually feed semantic validation and later compiler
stages.
