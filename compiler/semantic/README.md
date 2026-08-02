# Semantic

Owns the first semantic-analysis pass over normalized source schemas.

## Responsibilities

The semantic layer validates named type references used by schema declarations
and reports semantic errors through the diagnostics framework.

It:

* walks the normalized source-schema model
* resolves type references against the symbol table
* canonicalizes builtin scalar aliases into resolved semantic types
* preserves distinct string and bytes semantic kinds together with validated
  `max_bytes`
* records canonical record and enum reference FQNs
* carries normalized record metadata such as schema version and logical record
  type when the source model provides it
* carries validated `max_elements` for bounded variable-length arrays
* validates type-bearing normalized source-schema fields
* reports unresolved type references, including unknown qualified namespaces
  and declarations
* reports declarations that resolve successfully but are invalid in type
  position
* reports unsupported nested arrays in the normalized source-schema path

## Ownership Model

Semantic analysis does not mutate the source-schema model and does not build a
second symbol table.

This pass is validation-only for now. It does not cache resolved symbols inside
AST nodes and does not introduce a semantic side table because no downstream
consumer needs one yet.

The layer consumes `SymbolTable` to resolve names but does not own symbol
collection.

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

The semantic implementation consumes one or more normalized source-schema
documents. The
legacy declaration-parser AST overload was removed in PR-049 after the
remaining AST-derived semantic tests were migrated or retired.

The production YAML frontend now passes all normalized documents retained by
the CompilerContext into compiler-wide symbol construction and semantic
validation. The production YAML frontend
also passes the normalized source schema directly into `SchemaIrBuilder`, so
the YAML pipeline no longer uses a source-schema-to-AST compatibility
projection.

## Dependency Restrictions

Allowed dependencies:

* `compiler/symbols`
* `compiler/source_schema`
* `compiler/diagnostics`
* `compiler/support`

The semantic layer must not compute binary layout, assign layout identifiers,
or introduce Schema IR objects.
