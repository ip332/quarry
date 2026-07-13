# Source Schema

This module owns the neutral Breadcrumbs source-schema model and the
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

It does not parse YAML text, build AST nodes, resolve symbols, perform semantic
validation, compute layout, or construct Schema IR.

## Compatibility Projection

The module also owns the temporary compatibility projection from the normalized
source-schema model into the legacy AST shape used by `SchemaIrBuilder`.
That projection remains available only for legacy AST-based callers and tests;
the production YAML frontend now lowers normalized source schema directly into
Schema IR.

## Dependencies

Allowed direct dependencies:

* `compiler/diagnostics`
* `compiler/support`

The source-schema layer must not depend on parser, symbols, semantic
validation, layout computation, Schema IR, or backend code.
