# AST

Owns syntax tree representation for parsed schema source.

Responsibilities:

* represent syntax-level declarations
* carry source locations on syntax nodes
* preserve source structure needed by diagnostics
* avoid semantic resolution

Allowed dependencies:

* `compiler/support`
* `compiler/diagnostics` only where needed

This layer must not depend on parser, imports, symbols, semantic, layout,
Schema IR, or backend code.
