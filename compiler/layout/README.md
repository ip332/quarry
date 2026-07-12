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

Current layout behavior:

* records are processed in canonical fully qualified name order
* the initial `recordId` allocation starts at `1` and increments monotonically
* fields are assigned `fieldIndex` values in declaration order starting at `0`
* a record may contain at most `256` declared fields because `fieldIndex` is
  stored as `uint8` in the binary record format
* the Layout Model stores canonical record FQNs so later passes can locate the
  correct layout without depending on traversal order

Current implementation status:

* the normative YAML source contract is defined in
  `docs/specifications/schema-language.md`
* bounded-array frontend support is not implemented by this documentation PR
