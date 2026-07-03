# Symbols

Owns namespace hierarchy and symbol tables.

Responsibilities:

* construct namespace hierarchy
* register declarations
* assign fully qualified declaration names
* detect duplicate and conflicting declarations
* produce the Symbol Model

Allowed dependencies:

* `compiler/ast`
* `compiler/imports`
* `compiler/diagnostics`
* `compiler/support`

This layer must not perform semantic validation or layout computation.
