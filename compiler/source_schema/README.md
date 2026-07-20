# Source Schema

This module owns the neutral Quarry source-schema model and the
normalization pass that turns decoded YAML schema data into structured,
compiler-owned identifiers and type references.

## Responsibility

The source-schema layer sits between YAML decoding and the rest of the
compiler pipeline.

It:

* represents the raw decoded `.brd` schema structure
* normalizes identifiers and qualified names
* normalizes bounded type spellings into structured source-schema types
* preserves source ranges and declaration order
* rejects unsupported source-schema imports

The model intentionally mirrors the current YAML schema-unit boundary. A
document owns one namespace path and one primary record, plus ordered field,
enum, enum-value, and annotation collections. It does not model a collection of
records, a collection of namespace roots, or a multi-document import graph.

It does not parse YAML text, build AST nodes, resolve symbols, perform semantic
validation, compute layout, or construct Schema IR.

The YAML decoder returns these raw source-schema model types directly. The
YAML module does not own aliases or adapter models for source-schema data.

The normalized source-schema model is the production-facing YAML representation
that downstream compiler stages now consume directly. The legacy declaration
parser remains available for parser/AST compatibility tests, but this module
no longer provides a source-schema-to-AST compatibility bridge.

## Dependencies

Allowed direct dependencies:

* `compiler/diagnostics`
* `compiler/support`

The source-schema layer must not depend on parser, symbols, semantic
validation, layout computation, Schema IR, or backend code.
