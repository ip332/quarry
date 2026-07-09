# Symbols

Owns the compiler's namespace hierarchy and symbol tables.

## Responsibilities

The symbols layer collects named declarations from the parsed AST and builds a
tree of lexical scopes. It is the groundwork for later name resolution and
semantic analysis.

It:

* registers namespaces, records, and enums
* constructs global and namespace scopes
* preserves nested namespace structure
* detects duplicate declarations in the same scope
* provides `lookup` APIs for unqualified and qualified names
* reports unresolved names when asked to resolve them explicitly

## Ownership Model

Symbols are non-owning views over the existing AST. They refer back to AST
declarations through raw pointers because the AST owns the declaration tree.

Scopes own symbol records and nested child scopes, but they do not own the AST
nodes they describe.

`SymbolTable` is the public name for the symbol model. It owns the scope tree
and exposes lookup behavior directly to later passes. `NamespaceBuilder::build`
consumes the parsed AST and constructs the scope tree from that syntax tree.

## Name Model

The AST's `QualifiedNameSyntax` is used directly for lookup. The symbols layer
does not build an alternate fully qualified name hierarchy or concatenate names
into a separate identity string.

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
* `compiler/diagnostics`
* `compiler/support`

The symbols layer must not perform semantic validation, layout computation, or
Schema IR construction.
