# Schema Evolution

## Purpose

Breadcrumbs schemas are expected to evolve over time while preserving binary
compatibility where possible.

Schema evolution is based on the distinction between record identity and record
structure.

This document captures design rationale and architectural principles. It is not
a normative specification.

---

## Core Principle

Records are semantic entities. Fields are structural elements.

Only records have identities.

Fields do not have independent logical identity, author-assigned identifiers, or
fully qualified names.

Field names exist for generated APIs, documentation, and human readability. They
do not define binary identity.

---

## Sparse Record Model

A Breadcrumbs record is a sparse binary record, not a copy of application state.

All fields are presence-tracked.

Fields do not have required or optional semantics in the core schema model.
Field declarations describe values that may appear in a record.

Each declared field has a compiler-generated `fieldIndex`. The `fieldIndex` is
used by the binary Field Directory, but it is not a logical identifier for the
field.

Presence is represented by Field Directory entries. If application code calls a
field setter, the generated setter updates the sparse binary record directly by
adding or updating the field's directory entry and payload bytes.

If application code does not call the setter, the field is absent, no directory
entry is written for that field, and no value bytes are encoded for that field.

Setters may be called in any order. Generated setters update the binary record
directly, and the binary record is the primary data structure.

Directory entries are sorted by `fieldIndex` to provide a canonical Field
Directory. Payload bytes are addressed by `fieldOffset` and `fieldLength`, so
payload values may be stored in append order or another implementation-defined
order. Payload order is not semantically significant.

Because `fieldIndex` is encoded as one byte, a record may contain at most 256
declared fields. Records needing more fields should be decomposed into smaller
records using composition.

Validation, if introduced later, belongs to application logic or higher-level
tooling, not to the binary format itself.

---

## Field Evolution

Field compatibility is structural.

Fields still do not have independent logical identity. However, each field has a
compiler-generated binary `fieldIndex`.

`fieldIndex` is a binary layout detail, not a schema-level identity.

Field compatibility depends on preserving `fieldIndex` and type associations,
not just declaration position.

Renaming a field while preserving its `fieldIndex` and type is an API and
documentation change, not a binary compatibility break.

Reordering field declarations should not necessarily change binary
compatibility if `fieldIndex` assignments are preserved by compiler state.

Changing a field type for an existing `fieldIndex` is incompatible.

Removing a field leaves its `fieldIndex` unavailable for automatic reuse unless
a future compatibility specification says otherwise.

Appending a new field gets a new `fieldIndex`.

Original:

```text
record Location {
    latitude: f64;
    longitude: f64;
}
```

Renamed field:

```text
record Location {
    lat: f64;
    longitude: f64;
}
```

This preserves binary layout when `lat` keeps the same `fieldIndex` and type as
`latitude`, and `longitude` keeps its existing `fieldIndex` and type.

---

## Structural Compatibility

Compatibility is evaluated in terms of record structure and generated reader
behavior.

Generally compatible changes include:

* appending a new field
* renaming a field without changing its `fieldIndex` or type
* adding documentation or metadata

Generally incompatible changes include:

* changing a field type for an existing `fieldIndex`
* reusing a removed field's `fieldIndex`
* removing a field that existing readers expect
* changing field presence or validation expectations in a way that breaks
  existing readers

This document does not define detailed compatibility rules. Exact rules belong
to the Compatibility Rules specification.

---

## Record Evolution

Record identity is based on the Fully Qualified Name and associated `recordId`,
as described in [Schema Identity](schema-identity.md).

Changing a record's FQN is a new logical identity unless an explicit rename is
declared.

Record renames preserve `recordId` only when explicitly declared.

---

## Non-Goals

Breadcrumbs does not attempt to infer field renames.

Breadcrumbs does not support author-assigned field identifiers.

Breadcrumbs does not support record inheritance.

Shared structure should be represented with composition, not inheritance.

Example:

```text
record Location {
    latitude: f64;
    longitude: f64;
}

record GpsLocation {
    location: Location;
    altitude: f32;
}
```

---

## Future Work

Topics intentionally deferred to future specifications or design documents:

* detailed compatibility rules
* field presence semantics
* default values
* deprecation markers
* schema migration tooling
* compatibility report format
