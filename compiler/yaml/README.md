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
`YamlDocument` into the raw Breadcrumbs source schema model.

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
through YAML decoding and source-schema normalization so later compiler stages
can validate and lower the carried bounds.

## Source Schema Normalization

The source-schema normalization layer lives in `compiler/source_schema` and
turns the decoded schema into structured identifiers, qualified names, and
bounded type references.

The YAML module keeps a thin compatibility wrapper for the legacy AST bridge.
That wrapper preserves:

* namespace spelling
* record names, versions, and logical record type spellings
* ordered fields, enums, and enum values
* field `max_bytes` and `max_elements` metadata
* source ranges from the YAML document model

The compatibility wrapper does not perform schema semantics, symbol
resolution, layout computation, or Schema IR construction. It exists only so
the remaining AST-based Schema IR builder can be reached during migration.

The production-facing `compiler/frontend` orchestration layer now builds the
symbol table and semantic model directly from the normalized source schema
and lowers directly into `SchemaIrBuilder` without the compatibility AST hop.
The compatibility projection remains only for legacy AST-based
`SchemaIrBuilder` callers and tests.

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
repository migrates toward YAML decoding. That legacy frontend is still used
by existing `.brd` tests and does not define the normative language contract.
The normative YAML frontend now normalizes into the neutral source-schema
module before entering the remaining compiler pipeline, and the production
YAML path now reaches Schema IR without projecting back to the legacy AST.

This module is the first step toward a later YAML-to-Breadcrumbs schema
pipeline that will eventually feed semantic validation and later compiler
stages.
