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

Layout computation may also consume preserved compiler state when the selected
compiler mode provides it. Preserved compiler state is an optional compiler
input used to reuse existing compiler-managed identifiers. It is not semantic
input and does not change the meaning of the Semantic Model.

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
record name after import resolution and namespace construction.

Deterministic record ordering is required so identical Semantic Models and
identical compiler state produce identical Layout Models. It also makes
diagnostics, generated artifacts, regression tests, and compatibility checks
reproducible.

---

# Record ID Assignment

`recordId` is compiler-managed. Schema authors never assign `recordId` values.

The assignment algorithm has these goals:

* uniqueness
* stable assignment when preserved compiler state supplies an existing mapping
* deterministic assignment
* no user-authored record IDs

## Initial Compilation

During an initial compilation, records are processed in deterministic record
order.

Each record receives the next available `recordId` from the compiler-managed
allocation state.

The same input Semantic Model and same initial allocation state must produce the
same `recordId` assignments.

## Recompilation With Preserved Compiler State

When preserved compiler state is available, the compiler may reuse existing
`recordId` assignments according to the selected compatibility policy.

Identifier preservation is policy applied by the compiler before or during
layout computation. Layout computation consumes the resulting identifier mapping
and validates that it is usable for the current Semantic Model.

New records receive new `recordId` values from the compiler-managed allocation
state after preserved assignments have been applied.

Rules for record renames, removed records, and long-term identifier reuse belong
to a future compatibility specification.

## Deleted Records

Deleted records do not appear in the Layout Model.

When preserved compiler state is available, deleted record handling is governed
by compatibility policy. Layout computation does not define long-term record ID
reuse policy.

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

## Recompilation With Preserved Compiler State

When preserved compiler state is available, the compiler may preserve existing
`fieldIndex` assignments according to the selected compatibility policy.

Layout computation consumes the resulting field index mapping and validates that
it is usable for the current record layout.

New fields receive new `fieldIndex` values from the record's compiler-managed
field allocation state after preserved assignments have been applied.

## Removed Fields

Removed fields do not appear in the Layout Model.

When preserved compiler state is available, removed field handling is governed
by compatibility policy. Layout computation does not define long-term
`fieldIndex` reuse policy.

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

Fixed-size arrays are fixed-size values when their element type has fixed-size
layout and the array count is known.

## Variable-Size Values

Variable-size values have encoded sizes that depend on the field value.

Strings and bytes are variable-size values.

Arrays are variable-size values if a future schema version supports
variable-size arrays.

## References

Record and enum references use resolved Semantic Model references as input to
layout classification.

Layout computation records the reference metadata needed by later encoding and
backend generation.

## Arrays

Array layout classification depends on the array count and element type.

For fixed-size arrays, the layout classification records the element
classification and array count.

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

Identical Semantic Models and identical compiler state must always produce
identical Layout Models.

Determinism is achieved by:

* deterministic record ordering
* deterministic field ordering
* compiler-managed identifier allocation
* consistent application of preserved identifier mappings supplied by compiler
  state
* deterministic diagnostic ordering
* avoiding dependence on file system order, hash map order, or backend order

Deterministic layout is required for reproducible builds, stable generated
artifacts, compatibility analysis, and predictable diagnostics.

---

# Compatibility

Layout computation may evaluate compatibility constraints against preserved
compiler state when that state is available.

The layout algorithm itself does not define schema evolution policy. It applies
the compatibility policy selected by the compiler and reports layout-level
conflicts.

Changes that compatibility policy may allow include:

* preserving a record's logical identity
* preserving an existing `recordId`
* preserving an existing field's `fieldIndex`
* preserving the field type associated with an existing `fieldIndex`
* appending a new field with a new `fieldIndex`

Changes that compatibility policy may reject or require updated layout metadata
for include:

* changing a record's logical identity without an explicit rename already
  applied before layout computation
* changing the type associated with an existing `fieldIndex`
* exceeding field count limits
* attempting to reuse removed compiler-managed identifiers
* conflicting with preserved compiler state

This section describes layout-level compatibility concepts only. Detailed
schema evolution, rename, identifier reuse, and binary compatibility rules
belong to a future compatibility specification.

---

# Diagnostics

Layout-specific diagnostics use the compiler diagnostics framework.

Layout computation may emit diagnostics such as:

* too many fields
* layout overflow
* unsupported layout construct
* incompatible preserved compiler state
* invalid compiler-managed identifiers
* missing preserved identifier state required by the selected compiler mode
* field layout incompatible with preserved `fieldIndex`

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
