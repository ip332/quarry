# Semantic

Owns the first semantic-analysis pass over parsed ASTs.

## Responsibilities

The semantic layer validates named type references used by schema declarations
and reports semantic errors through the diagnostics framework.

It:

* walks the parsed AST
* resolves type references against the symbol model
* accepts builtin scalar types directly
* validates type-bearing syntax in records and arrays
* reports unresolved type references
* reports declarations that resolve successfully but are invalid in type
  position
* reports unsupported fixed-size arrays

## Ownership Model

Semantic analysis does not mutate the AST and does not build a second symbol
table.

This pass is validation-only for now. It does not cache resolved symbols inside
AST nodes and does not introduce a semantic side table because no downstream
consumer needs one yet.

## Dependency Restrictions

Allowed dependencies:

* `compiler/ast`
* `compiler/symbols`
* `compiler/diagnostics`
* `compiler/support`

The semantic layer must not compute binary layout, assign layout identifiers,
or introduce Schema IR objects.
