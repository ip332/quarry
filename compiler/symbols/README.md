# Symbols

Owns the compiler's namespace hierarchy and symbol tables.

## Responsibilities

The symbols layer collects named declarations from either the legacy parsed
AST or the normalized source-schema model and builds a tree of lexical scopes.
It is the groundwork for later name resolution and semantic analysis.

It:

* registers namespaces, records, and enums
* constructs global and namespace scopes
* preserves nested namespace structure
* detects duplicate declarations in the same scope
* provides `lookup` APIs for unqualified and qualified names
* reports unresolved names when asked to resolve them explicitly

## Ownership Model

`SymbolTable` owns the scope tree and the symbol records it contains. Symbols
carry canonical fully qualified names, source ranges, and child namespace
scopes, but they no longer retain AST declaration pointers.

Scopes own symbol records and nested child scopes.

`NamespaceBuilder::build` can consume the parsed AST or the normalized
source-schema model and constructs the same scope tree from either input.
Schema IR no longer consumes `SymbolTable` directly; the normalized
source-schema path feeds Schema IR construction without a symbol-table input.

## Name Model

The AST's `QualifiedNameSyntax` and the normalized source-schema qualified name
both feed the same canonical lookup rules. The symbols layer does not build an
alternate fully qualified name hierarchy or concatenate names into a separate
identity string beyond the stored canonical FQN on each symbol.

Unqualified lookup walks the current scope and enclosing scopes. Qualified
lookup resolves the first component with unqualified lookup and then walks
child namespace scopes component by component.

## Diagnostics

The symbols layer emits diagnostics for:

* duplicate declaration in the same scope (`BC4001`)
* unresolved name when resolution is explicitly requested (`BC4002`)

Duplicate declaration diagnostics point at the later declaration and include
the previous declaration as related source context.

## Dependency Restrictions

Allowed dependencies:

* `compiler/ast`
* `compiler/source_schema`
* `compiler/diagnostics`
* `compiler/support`

The symbols layer must not perform semantic validation, layout computation, or
Schema IR construction.
