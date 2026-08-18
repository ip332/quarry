# Layout

Owns deterministic layout computation.

Responsibilities:

* produce the Layout Model from the Semantic Model
* assign compiler-managed `recordId` values
* assign compiler-managed `fieldIndex` values
* validate layout constraints

Allowed dependencies:

* `compiler/semantic`
* `compiler/diagnostics`
* `compiler/support`
* `proto/quarry/schema_ir.proto` through the generated Schema IR library

This layer must not perform semantic analysis or backend generation.

Current layout behavior:

* records are processed in canonical fully qualified name order
* the initial `recordId` allocation starts at `1` and increments monotonically
* fields are assigned `fieldIndex` values in declaration order starting at `0`
* a record may contain at most `256` declared fields because `fieldIndex` is
  stored as `uint8` in the binary record format
* the Layout Model stores canonical record FQNs so later passes can locate the
  correct layout without depending on traversal order
* the legacy Semantic Model overload does not own schema version, logical
  record type, `max_bytes`, or `max_elements`; the BRF v2 Schema IR overload
  carries validated bounds into its canonical type metadata

Current implementation status:

* the normative YAML source contract is defined in
  `docs/specifications/schema-language.md`
* bounded-variable arrays are now carried through Semantic Model and Schema IR;
  layout remains focused on `recordId` and `fieldIndex` ownership and does not
  duplicate the bound metadata

BRF v2 layout artifact:

* `LayoutComputer::compute(SchemaIR, diagnostics)` produces the compiler-owned
  BRF v2 layout artifact used by future backends; the existing semantic-model
  overload remains the legacy record/field identity pass for current backends
* BRF v2 locations are absolute byte offsets from the beginning of the complete
  record, with `bit_offset` and `bit_width` retained for future packed fields
* the artifact models the 16-byte header, presence bitmap, declaration-ordered
  fixed slots, 8-byte variable descriptors, recursive fixed/variable
  classification, and nested/array element metadata
* static offsets and sizes use checked 32-bit arithmetic; descriptor contents
  such as runtime payload offsets are deliberately not calculated here
