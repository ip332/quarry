# Schema Identity

## Purpose

Quarry distinguishes between logical identities used by developers and
compact binary identities used by the binary format.

The goal is to allow schemas to evolve while preserving binary compatibility.

This document captures design rationale and architectural decisions. It is not a
normative specification.

---

## Identity Model

Quarry separates record identity from generated language representation.

## Identity Layers

Quarry distinguishes three related concepts:

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
quarry.telemetry.Location
quarry.vehicle.BatteryStatus
```

The Fully Qualified Name (FQN) is the authoritative logical identity of a record
within a Quarry schema.

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

The current assignment is hierarchical and derived from the complete
configuration, without persistent state: `recordId = (domain_id << 16) |
local_record_id`, where domains are the record-declaring namespaces in sorted
order and local IDs the records of each namespace in sorted name order. The
algorithm and the reserved ranges are specified in
`docs/specifications/binary-record-format.md` ("recordId Structure and
Reserved Ranges") and `docs/layout-algorithm.md`; the rationale is in
`docs/design/record-identity-and-schema-discovery.md`.

Earlier drafts of this document assumed that the compiler would preserve
previously assigned `recordId` values in persistent state, such as a manifest.
That is not implemented and is not part of the current design: `recordId`
values are consistent within one configuration, and stability across
configuration changes, if it is needed, is future compatibility work.

---

## Record Renames

Stage A has no rename declaration or preserved allocation state. Renaming a record
changes its FQN and can shift the sorted local IDs; renaming a namespace can shift
domain IDs. Recompile the complete configuration and assess compatibility rather
than assuming the previous ID survives. Explicit rename/evolution policy is future
work, not accepted schema syntax.

## Configuration and generated identity

The complete configuration has a 128-bit QBS `schema_id` (algorithm 1). It covers
canonical wire definitions and record/enum FQNs, but excludes field/member names,
unused enums, source paths, compiler versions and source naming options.
C/C++ namespaces additionally have a format-1 `gid` over normalized generated
definitions and dependency identities. API epochs check the generator/runtime
contract independently. None is an authentication mechanism or a native ABI proof.
See the [normative identity contract](../specifications/configuration-and-code-identity.md)
and [consumer migration guide](../generated-binding-compatibility.md).

---

## Design Principles

Logical identity and binary identity are intentionally separate concepts.

Schema authors work exclusively with logical identities.

The logical identity of a record is stable across programming languages.
Language generators translate logical identities into language-specific
representations without changing their meaning.

Binary identifiers are compiler-managed implementation details.

Binary identifiers are stable for the same configuration mapping, not across arbitrary schema evolution.

Generated language bindings never define record identity; they only represent
it.

---

## Future Work

Topics intentionally deferred to future specifications or design documents:

* record rename declaration syntax
* namespace rename semantics
* fieldIndex compatibility rules
* compatibility rules
