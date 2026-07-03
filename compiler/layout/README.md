# Layout

Owns deterministic layout computation.

Responsibilities:

* produce the Layout Model from the Semantic Model
* assign compiler-managed `recordId` values
* assign compiler-managed `fieldIndex` values
* compute sparse record layout metadata
* validate layout constraints

Allowed dependencies:

* `compiler/semantic`
* `compiler/diagnostics`
* `compiler/support`

This layer must not perform semantic analysis or backend generation.
