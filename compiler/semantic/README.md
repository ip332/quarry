# Semantic

Owns the first semantic-analysis pass over parsed ASTs.

## Responsibilities

The semantic layer validates named type references used by schema declarations
and reports semantic errors through the diagnostics framework.

It:

* walks the parsed AST
* resolves type references against the symbol table
* canonicalizes builtin scalar aliases into resolved semantic types
* preserves distinct string and bytes semantic kinds together with validated
  `max_bytes`
* records canonical record and enum reference FQNs
* carries normalized record metadata such as schema version and logical record
  type when the source model provides it
* carries validated `max_elements` for bounded variable-length arrays
* validates type-bearing syntax in the current transitional declaration-parser
  AST and the normalized source-schema model
* reports unresolved type references
* reports declarations that resolve successfully but are invalid in type
  position
* reports unsupported fixed-size arrays in the current transitional parser
  path

## Ownership Model

Semantic analysis does not mutate the AST and does not build a second symbol
table.

This pass is validation-only for now. It does not cache resolved symbols inside
AST nodes and does not introduce a semantic side table because no downstream
consumer needs one yet.

The layer consumes `SymbolTable` to resolve names but does not own symbol
collection. The normalized source-schema path uses the same semantic policy as
the legacy AST path.

Invalid field types do not enter the returned `SemanticModel`. Semantic
diagnostics may still be emitted for later fields and declarations in the same
compilation unit.

Semantic fields carry resolved semantic types. Semantic field types are
backend-neutral values. They do not contain AST nodes, unresolved source names,
`recordId`, `fieldIndex`, sizes, offsets, or encoding classifications. Those
remain layout responsibilities.

## Current Implementation Status

The normative `.brd` YAML contract is defined in
`docs/specifications/schema-language.md`.

The current semantic implementation still consumes the transitional
declaration-parser AST and also accepts the normalized source-schema model.

The production YAML frontend now passes normalized YAML source schema directly
into symbol construction and semantic validation. The production YAML frontend
now also passes the normalized source schema directly into `SchemaIrBuilder`,
so the compatibility AST projection is retained only for legacy AST-based
`SchemaIrBuilder` callers and tests.

Direct source-schema-to-SchemaIrBuilder migration has now happened for the
production YAML path; the legacy AST path remains available temporarily.

## Dependency Restrictions

Allowed dependencies:

* `compiler/ast`
* `compiler/symbols`
* `compiler/source_schema`
* `compiler/diagnostics`
* `compiler/support`

The semantic layer must not compute binary layout, assign layout identifiers,
or introduce Schema IR objects.
