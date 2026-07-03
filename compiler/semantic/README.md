# Semantic

Owns semantic validation and the resolved semantic model.

Responsibilities:

* resolve type and record references
* enforce schema language rules
* reject unsupported constructs
* produce the Semantic Model consumed by layout computation

Allowed dependencies:

* `compiler/symbols`
* `compiler/diagnostics`
* `compiler/support`

This layer must not compute binary layout or assign layout identifiers.
