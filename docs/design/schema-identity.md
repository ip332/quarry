# Schema Identity

## Purpose

Breadcrumbs distinguishes between logical identities used by developers and
compact binary identities used by the binary format.

The goal is to allow schemas to evolve while preserving binary compatibility.

This document captures design rationale and architectural decisions. It is not a
normative specification.

---

## Identity Model

Breadcrumbs separates record identity from generated language representation.

## Identity Layers

Breadcrumbs distinguishes three related concepts:

* logical identity, represented by the Fully Qualified Name
* binary identity, represented by `recordId`
* language representation, represented by generated symbols, namespaces,
  packages, modules, or similar language-specific constructs

Only logical identity and binary identity are architectural concepts.
Language-specific names are derived artifacts produced by language generators
and do not define identity.

Records have logical identity and binary identity. Fields do not have logical
identity.

`fieldIndex` is a compiler-generated binary layout identifier only. It is
neither a logical identity nor part of the schema language.

`fieldIndex` must not be confused with `recordId` or with logical identity.

### Logical Identity

The logical identity of a record is its Fully Qualified Name (FQN).

The FQN consists of:

* namespace
* record name

Examples:

```text
breadcrumbs.telemetry.Location
breadcrumbs.vehicle.BatteryStatus
```

The Fully Qualified Name (FQN) is the authoritative logical identity of a record
within a Breadcrumbs schema.

Logical identity is independent of generated code and programming language
representations.

Language generators translate the FQN into the appropriate language-specific
representation, such as C symbol prefixes or C++ namespaces. Those generated
names are not themselves the logical identity.

### Binary Identity

The binary identity of a record is its `recordId`.

`recordId` exists solely to produce compact binary records.

`recordId` is not part of the schema language.

Schema authors never manually assign `recordId` values.

---

## Record ID Assignment

The Schema Compiler assigns `recordId` values.

Schema authors never manually assign `recordId` values.

The compiler preserves previously assigned `recordId` values.

The Schema Compiler maintains persistent state that associates each logical
record identity with its assigned `recordId`. The representation of this
persistent state, for example a manifest, is intentionally left unspecified at
this stage.

This document does not define the assignment algorithm.

---

## Record Renames

A record's Fully Qualified Name (FQN) defines its logical identity.

Changing the FQN is therefore considered a change of logical identity unless
the schema explicitly declares that the new record is a rename of an existing
record.

Record renames are explicit.

The compiler never attempts to infer a rename.

If a record's FQN changes without an explicit rename declaration, the compiler
treats the old record as removed and the new record as added.

An explicit rename instructs the compiler to preserve the existing `recordId`
and associated compiler state.

Illustrative example:

```text
existing record:
breadcrumbs.telemetry.Location

renamed record:
breadcrumbs.telemetry.GpsLocation

explicit rename relationship:
breadcrumbs.telemetry.GpsLocation was renamed from breadcrumbs.telemetry.Location
```

With an explicit rename:

```text
breadcrumbs.telemetry.Location
recordId = 17

becomes:

breadcrumbs.telemetry.GpsLocation
recordId = 17
```

Without an explicit rename, the compiler treats this as:

```text
breadcrumbs.telemetry.Location removed
breadcrumbs.telemetry.GpsLocation added
new recordId allocated for breadcrumbs.telemetry.GpsLocation

illustrative recordId:
breadcrumbs.telemetry.GpsLocation
recordId = 42
```

This example describes the intended relationship only. It does not define final
schema syntax for declaring record renames.

---

## Design Principles

Logical identity and binary identity are intentionally separate concepts.

Schema authors work exclusively with logical identities.

The logical identity of a record is stable across programming languages.
Language generators translate logical identities into language-specific
representations without changing their meaning.

Binary identifiers are compiler-managed implementation details.

Binary identifiers remain stable as schemas evolve.

Generated language bindings never define record identity; they only represent
it.

---

## Future Work

Topics intentionally deferred to future specifications or design documents:

* record rename declaration syntax
* record ID allocation algorithm
* manifest format
* namespace rename semantics
* fieldIndex compatibility rules
* compatibility rules
