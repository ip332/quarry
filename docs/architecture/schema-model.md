# Quarry Schema Model

## Status

Draft

## Purpose

This document defines the schema model used by Quarry records.

The schema model describes how payload data is defined, evolved, validated, and
accessed across devices, cloud services, tools, and SDKs.

The schema is the contract. The binary encoding and generated language APIs are
implementations of that contract.

---

# Design Goals

The Quarry schema model is designed to support:

* embedded-first implementations
* serialized-first record access
* stable schema references
* compiler-generated field indexes
* generated accessors
* generated validators
* generated binary codecs
* generated inspector metadata
* forward and backward compatibility
* deterministic parsing
* low memory overhead
* long-term schema evolution
* cloud indexing and analytics

---

# Quarry Schema Language

Quarry defines a project-specific schema language.

The Quarry schema language is a DSL for describing the logical data model
of record payloads. It is not a general-purpose serialization language and is
not tied to a specific binary encoding.

The DSL should be:

* compact
* readable
* code-generation friendly
* embedded-friendly
* explicit about compatibility
* independent from a specific transport protocol

The public language shall not contain author-assigned numeric field indexes.
Internal field indexes are generated and maintained by the Quarry schema
compiler.

Authors never assign or reference field indexes directly.

The schema language may support import or export formats for tooling, but the
Quarry DSL is the canonical authoring language.

---

# Encoding Independence

The schema model is independent from the physical binary encoding.

A schema may be encoded using:

* a custom compact binary format
* a Flatware-derived encoding
* FlatBuffers
* Protobuf
* CBOR
* another embedded-friendly format

The schema language shall not expose encoding details unless they are required
for compatibility.

---

# Schema Identity

Quarry uses human-readable schema identity.

A schema has:

* namespace
* record name
* schema version

Example:

```yaml
namespace: quarry.telemetry
record: Location
version: 1
```

## Schema Name

The schema name identifies the logical record payload type.

It is stable across compatible and incompatible versions of the same conceptual
record family.

Example:

```text
quarry.telemetry.Location
```

## Schema Version

The schema version identifies a specific definition of a schema.

Different versions may have different field sets, compatibility rules, or
decoding behavior.

Examples: `1`, `2`.

## Schema Reference

The schema reference combines schema name and schema version.

Format:

```text
<namespace>.<record>@<version>
```

Example:

```text
quarry.telemetry.Location@1
```

A record that carries a binary payload shall identify the schema reference
needed to interpret that payload.

---

# Schema Definition

A schema defines exactly one record type.

The schema compiler assigns `recordId` metadata used by the binary record
header. Runtime systems use `recordId` to identify the record type and its
compatible evolution line.

Compatible schema evolution keeps the same `recordId`. Incompatible layout or
semantic changes require a new `recordId`.

Each schema includes:

* namespace
* record name
* schema version
* record type
* field definitions
* compatibility rules

Example:

```yaml
namespace: quarry.telemetry
record: Location
version: 1
recordType: data

fields:
  latitude:
    type: float64
    unit: degrees

  longitude:
    type: float64
    unit: degrees

  altitude:
    type: float32
    unit: meters
```

---

# Field Model

Each field has:

* stable field name
* compiler-generated fieldIndex
* type
* units, when applicable
* semantic description

Supported field categories may include:

* integer
* unsigned integer
* floating point
* boolean
* enum
* timestamp
* string
* bytes
* nested record
* array

---

# Field Index

Quarry schemas do not expose author-assigned numeric field indexes in
the public language.

Internal field indexes are generated and maintained by the Quarry schema
compiler.

Authors never assign or reference field indexes directly.

The compiler-generated `fieldIndex` is used by the binary Field Directory to
identify present field values.

`fieldIndex` is not a logical identifier for the field.

`fieldIndex` must remain stable once a schema version is published. Field
indexes shall not be reused. Field names shall not be reused with incompatible
meaning.

When a field is removed, its field index is reserved by generated metadata.

Example:

```yaml
reserved:
  - field: cellId
    reason: "deprecated; replaced by networkContext"
```

---

# Field Presence

Quarry records are sparse binary records.

Every declared field has associated presence information.

A field is present in a binary record only when application code sets it through
the generated API.

Field presence is represented by a Field Directory entry. Generated setters
update the sparse binary record directly.

The Field Directory is sorted by `fieldIndex`. Payload values are located using
directory offsets and lengths, so payload storage order is not semantically
significant.

Field declarations describe values that may appear in a record. They do not
classify fields into schema-level presence categories.

Consumers should tolerate absent fields according to application expectations
and generated accessor behavior.

A record may contain at most 256 declared fields because `fieldIndex` is encoded
as `uint8`.

Records needing more than 256 fields should be decomposed into smaller records
using composition.

---

# Compatibility Rules

Evolve schemas according to these rules:

* schema references shall uniquely identify schema definitions.
* field indexes shall never be reused.
* field names shall not be reused with incompatible meaning.
* field meaning shall not change incompatibly.
* field type shall not change incompatibly.
* new fields may be appended when compatibility rules allow.
* unknown fields shall be ignored when possible.
* removed fields shall remain reserved.

---

# Schema Versioning

Each schema has an explicit version.

The schema version is part of the schema reference.

For v0.1, Quarry uses simple integer schema versions.

Example:

```yaml
version: 1
```

Future versions may adopt semantic versioning if needed.

---

# Record Relationship

A schema defines exactly one record type and its payload.

A record wraps payloads using a common envelope.

The envelope contains routing and indexing metadata, including `recordId` and
other metadata needed to interpret the payload.

The payload contains schema-specific binary data.

Runtime systems deal with records. Transport protocols carry records but do not
define them.

---

# Generated artifacts

For each schema, the schema compiler produces generated artifacts.

Generated artifacts may include:

* generated accessors
* validators
* binary codecs
* inspector metadata
* generated documentation
* language-specific SDK files

Generated accessors should support:

* reading fields directly from binary record storage
* checking field presence
* validating payload structure
* constructing records
* language-specific ergonomic APIs

---

# Embedded Constraints

Design schemas to support constrained devices.

The model should avoid requiring:

* full-record materialization
* unbounded heap allocation
* recursive parsing without limits
* runtime schema downloads for normal operation
* application-maintained field indexes

Embedded implementations should be able to compile schema knowledge into
generated code.

---

# Cloud Constraints

Cloud implementations should support:

* schema registry
* payload validation
* metadata extraction
* indexing selected fields
* schema-aware analytics
* backward-compatible ingestion

The cloud may interpret payloads when required, but infrastructure should route
and store records using envelope metadata whenever possible. Production cloud
services should consume generated artifacts from the schema compiler for normal
operation.

---

# Schema Registry

The schema registry is an optional development component used to distribute
schema definitions and generated artifacts.

The registry may store or publish:

* schema definitions
* schema versions
* schema references
* compatibility status
* generated artifacts
* documentation

Production applications should not depend on registry access. Devices, services,
and tools should consume generated artifacts produced by the schema compiler.

---

# Open Questions

The following questions remain open and should be resolved in later
specification documents.

* What is the exact syntax of the Quarry schema language?
* What is the physical binary encoding for Quarry records?

---

# Relationship to Other Documents

Related architecture documents:

* `data-model.md` defines records, envelopes, and payloads.
* `schema-compiler.md` defines the schema compiler and generated artifacts.
