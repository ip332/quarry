# Layout Algorithm

# Goals

Layout computation transforms a validated Semantic Model into a Layout Model.

The Layout Computation pass:

* derives binary layout metadata from semantic meaning
* computes compiler-managed layout identifiers
* guarantees deterministic layouts
* validates layout constraints
* produces the Layout Model consumed by the Schema IR Builder

This document specifies algorithms and invariants. It does not define C++
classes, APIs, storage formats, or backend implementation details.

---

# Inputs

Layout computation consumes the validated Semantic Model.

The Semantic Model provides:

* records
* fields
* resolved field types
* arrays
* strings
* bytes
* references
* annotations that affect layout
* source metadata for diagnostics only

All references have already been resolved before layout computation begins.

Stage A computes identifiers afresh from the complete configuration; no preserved
compiler-state or manifest input is supported.

Layout computation does not resolve names, validate schema language semantics,
or recover from invalid Semantic Model state.

---

# Outputs

Layout computation produces the Layout Model.

The Layout Model contains:

* compiler-managed `recordId`
* compiler-managed `fieldIndex`
* field encoding metadata
* fixed/variable classification
* sparse directory metadata
* layout limits
* compatibility metadata where applicable

The Layout Model contains no unresolved semantic information.

The Schema IR Builder consumes the Layout Model together with the Semantic Model
to produce Schema IR.

---

# Record Ordering

Layout computation operates on a deterministic ordering of records.

Records are processed in canonical fully qualified name order.

Canonical fully qualified name order is based on the record's namespace path and
record name after namespace construction.

Deterministic record ordering is required so identical Semantic Models and
identical compiler state produce identical Layout Models. It also makes
diagnostics, generated artifacts, regression tests, and compatibility checks
reproducible.

---

# Record ID Assignment

`recordId` is compiler-managed. Schema authors never assign `recordId` values.

The assignment algorithm has these goals:

* uniqueness
* deterministic assignment
* no user-authored record IDs

## Initial Compilation

The layout pass is the single authority for `recordId` values. Its input is the
Semantic Model of the complete compilation root (the root schema and its whole
import closure), so every record of the configuration, referenced or not, is
assigned exactly once. Schema IR, QBS, and every backend copy the assigned
values; none computes IDs itself.

IDs are hierarchical, `recordId = (domain_id << 16) | local_record_id`:

1. Group records by their canonical namespace (`SemanticRecord::namespace_fqn`,
   taken from the schema's namespace declaration, not parsed out of the FQN).
   Several source files may contribute to one namespace.
2. Number the namespaces from `0x0001` in ascending bytewise order of their
   FQN. The root (empty) namespace sorts first.
3. Within each namespace, number the records from `0x0001` in ascending
   bytewise order of their local name.

The result does not depend on import order, declaration order, or file
boundaries. The same configuration always produces the same `recordId`
assignments.

Limits and reserved values (see "recordId Structure and Reserved Ranges" in
`docs/specifications/binary-record-format.md`):

* application domains `0x0001`–`0xFEFF`; more namespaces declaring records is
  `BC7004`;
* local IDs `0x0001`–`0xFFFE`; more records in one namespace is `BC7005`;
* domains `0x0000` and `0xFF00`–`0xFFFF`, and locals `0x0000` and `0xFFFF`,
  are never assigned to application records.

The layout pass also verifies that each record's FQN is its namespace FQN and
local name joined by `.` (`BC7006`), so the grouping and the canonical identity
cannot disagree.

## Recompilation and deletion

Every compilation recomputes the sorted mapping. Deleted records disappear;
insertions or removals can renumber later locals or domains. No persisted ID reuse,
rename mapping or compatibility-policy mode is implemented.

---

# Field Index Assignment

`fieldIndex` is compiler-managed. Schema authors never assign `fieldIndex`
values.

`fieldIndex` identifies a field value within a record's sparse binary layout. It
is not a logical field identity and is not visible in the schema language.

## Deterministic Assignment

Within each record, field processing is deterministic.

During initial compilation, fields are assigned `fieldIndex` values according to
the record's deterministic field order.

The deterministic field order is derived from the validated Semantic Model and
must not depend on map iteration order, file system ordering, or backend
behavior.

## Recompilation and removed fields

Field indexes are recomputed from declaration order. Removed fields disappear;
there is no persisted field allocation state or automatic schema evolution policy.

## Limits

The compiler validates that every `fieldIndex` fits within the limits of the
binary record format.

A record with more fields than the format can represent is rejected by layout
computation.

---

# Field Classification

Layout computation classifies each field into layout categories.

This classification describes how the field participates in layout. It does not
define the byte-level encoding.

## Fixed-Size Values

Fixed-size values have a statically known encoded size for each present field
value.

Primitive numeric and boolean field types are fixed-size values.

Bounded variable-length arrays are variable-size values because their encoded
size depends on the present element count.

## Variable-Size Values

Variable-size values have encoded sizes that depend on the field value.

Strings and bytes are variable-size values.

Arrays are variable-size values when the schema language defines them as
bounded variable-length arrays.

## References

Record and enum references use resolved Semantic Model references as input to
layout classification.

Layout computation records the reference metadata needed by later encoding and
backend generation.

## Arrays

Array layout classification depends on the array count and element type.

For bounded arrays, the layout classification records the element
classification and the validated maximum element count.

## Strings

String layout classification records that the field is variable-size text data
and includes the resolved size bounds needed by later encoding and validation.

## Bytes

Bytes layout classification records that the field is variable-size opaque byte
data and includes the resolved size bounds needed by later encoding and
validation.

---

# Layout Metadata

Layout computation produces metadata for every record.

Record layout metadata includes:

* field ordering
* field indices
* encoding kind
* sparse directory metadata
* fixed/variable metadata
* reference metadata

Field ordering in the Layout Model is deterministic. It exists for compiler and
backend consistency and does not imply that runtime payload bytes are stored in
the same order.

Sparse directory metadata describes how present fields are discoverable in the
record representation. It includes the information needed for generated
builders, readers, validators, and tooling to agree on field presence and field
lookup.

Fixed/variable metadata records whether each field has a statically known
layout size or a value-dependent layout size.

Reference metadata records resolved relationships to records or enums without
reintroducing unresolved names.

---

# Determinism

Identical Semantic Models must always produce
identical Layout Models.

Determinism is achieved by:

* deterministic record ordering
* deterministic field ordering
* compiler-managed identifier allocation
* deterministic diagnostic ordering
* avoiding dependence on file system order, hash map order, or backend order

Deterministic layout is required for reproducible builds, stable generated
artifacts, compatibility analysis, and predictable diagnostics.

---

# Compatibility

Stage A validates one resolved configuration; it does not compare historical
schemas or apply a rename/evolution policy. Changes to IDs or layouts require
regeneration and rebuilding. BRF v2 alone does not guarantee historical data
compatibility. See [the migration guide](generated-binding-compatibility.md).

---

# Diagnostics

Layout-specific diagnostics use the compiler diagnostics framework.

Layout computation may emit diagnostics such as:

* too many fields
* layout overflow
* unsupported layout construct
* invalid compiler-managed identifiers

Diagnostics should use source metadata from the Semantic Model when reporting
user-facing schema problems.

Internal consistency failures indicate compiler defects rather than schema
author errors.

---

# Design Principles

Semantic meaning is independent of layout.

Layout is deterministic.

Identifiers are compiler-managed.

Layout metadata is backend-independent.

Layout computation never performs semantic analysis.

Binary encoding is derived only from the Layout Model.

Layout computation does not emit runtime bytes.

Layout computation preserves source metadata only for diagnostics and tooling.
