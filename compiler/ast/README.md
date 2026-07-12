# AST

Owns the syntax tree representation produced by the future parser.

## Ownership

The AST is a lightweight value-oriented model. Nodes use standard library
containers and strings. Nested declarations are owned with
`std::unique_ptr<DeclarationSyntax>` so the AST remains tree-shaped and each
node has one owner. Shared ownership is intentionally avoided to prevent
accidental aliasing or mutation through multiple owners.

The AST does not own source buffers. Source text and source paths are owned by
`SourceManager`.

## Syntax Only

AST nodes represent source syntax only. They must not:

* resolve names
* assign compiler-managed IDs
* compute layout
* validate schema semantics
* normalize imports
* construct Schema IR

A type name in the AST is just parsed name syntax. Later passes decide whether
that name refers to a primitive, built-in type, record, enum, or invalid symbol.

## Source Ranges

Declarations, names, and type syntax nodes carry `support::SourceRange` where
practical. AST nodes do not store line or column numbers. Human-readable
locations are derived through `SourceManager`.

## Node Categories

The AST currently models:

* schema file / translation unit
* namespace declarations
* import declarations
* record declarations
* field declarations
* enum declarations
* enum value declarations
* identifier syntax
* qualified name syntax
* type references
* array type syntax
* annotation syntax

Current implementation status:

* the normative `.brd` grammar is defined in `docs/specifications/schema-language.md`
* the current AST still carries legacy fixed-size array representation and
  annotation nodes from the temporary parser
* those nodes are transitional scaffolding and do not redefine the language

## Debug Support

`dump_schema_file` provides a minimal text dump for tests and debugging. It is
not a serialization format and should not be consumed as compiler input.

## Relationship To Compiler Passes

The parser produces ASTs from source text. The import resolver consumes ASTs to
expand the compilation boundary. The namespace builder consumes ASTs to build
symbol information. The semantic validator consumes later symbol structures,
not raw syntax, except where source metadata is needed for diagnostics.

## Dependency Restrictions

Allowed dependencies:

* C++ standard library
* `compiler/support`

AST must not depend on parser, imports, symbols, semantic validation, layout
computation, Schema IR construction, backends, or compiler context.
