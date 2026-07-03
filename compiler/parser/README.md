# Parser

Owns lexical analysis and parsing.

Responsibilities:

* tokenize schema source text
* parse source text into AST
* emit parser diagnostics
* avoid semantic validation

Allowed dependencies:

* `compiler/ast`
* `compiler/diagnostics`
* `compiler/support`

This layer produces AST only.
