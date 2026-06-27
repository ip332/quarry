# Breadcrumbs Schema Model

## Status

Draft

## Purpose

This document defines the schema model used by Breadcrumbs records.

The schema model describes how payload data is defined, evolved, validated, and
accessed across devices, cloud services, tools, and SDKs.

The schema is the contract. The binary encoding and generated language APIs are
implementations of that contract.

---

# Design Goals

The Breadcrumbs schema model is designed to support:

* embedded-first implementations
* serialized-first record access
* stable schema references
* compiler-generated field identities
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

# Breadcrumbs Schema Language

Breadcrumbs defines a project-specific schema language.

The Breadcrumbs schema language is a DSL for describing the logical data model
of record payloads. It is not a general-purpose serialization language and is
not tied to a specific binary encoding.

The DSL should be:

* compact
* readable
* code-generation friendly
* embedded-friendly
* explicit about compatibility
* independent from a specific transport protocol

The public language shall not contain author-assigned numeric field identities.
Internal field identities are generated and maintained by the Breadcrumbs schema
compiler.

Authors never assign or reference field identities directly.

The schema language may support import or export formats for tooling, but the
Breadcrumbs DSL is the canonical authoring language.

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

Breadcrumbs uses human-readable schema identity.

A schema has:

* namespace
* record name
* schema version

Example:

```yaml
namespace: breadcrumbs.telemetry
record: Location
version: 1
```

## Schema Name

The schema name identifies the logical record payload type.

It is stable across compatible and incompatible versions of the same conceptual
record family.

Example:

```text
breadcrumbs.telemetry.Location
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
breadcrumbs.telemetry.Location@1
```

A record that carries a serialized payload shall identify the schema reference
needed to interpret that payload.

---

# Schema Definition

A schema defines one record payload type.

Each schema includes:

* namespace
* record name
* schema version
* record type
* field definitions
* compatibility rules

Example:

```yaml
namespace: breadcrumbs.telemetry
record: Location
version: 1
recordType: telemetry

fields:
  latitude:
    type: float64
    required: true
    unit: degrees

  longitude:
    type: float64
    required: true
    unit: degrees

  altitude:
    type: float32
    required: false
    unit: meters
```

---

# Field Model

Each field has:

* stable field name
* compiler-generated field identity
* type
* optionality
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

# Field Identity

Breadcrumbs schemas do not expose author-assigned numeric field identities in
the public language.

Internal field identities are generated and maintained by the Breadcrumbs schema
compiler.

Authors never assign or reference field identities directly.

Field identity must remain stable once a schema version is published. Field
identities shall not be reused. Field names shall not be reused with
incompatible meaning.

When a field is removed, its field identity is reserved by generated metadata.

Example:

```yaml
reserved:
  - field: cellId
    reason: "deprecated; replaced by networkContext"
```

---

# Required Fields

Required fields should be used sparingly.

A field may be required only when the payload is not meaningful without it.

Removing a required field is a breaking change.

Changing a required field to optional is generally compatible.

---

# Optional Fields

Optional fields may be omitted by producers.

Consumers shall tolerate missing optional fields.

Optional fields are preferred for:

* platform-specific data
* sensor-specific data
* future extensions
* partially available measurements

---

# Compatibility Rules

Evolve schemas according to these rules:

* schema references shall uniquely identify schema definitions.
* field identities shall never be reused.
* field names shall not be reused with incompatible meaning.
* field meaning shall not change incompatibly.
* field type shall not change incompatibly.
* new fields shall be optional by default.
* unknown fields shall be ignored when possible.
* removed fields shall remain reserved.
* required fields shall not be removed in compatible versions.

---

# Schema Versioning

Each schema has an explicit version.

The schema version is part of the schema reference.

For v0.1, Breadcrumbs uses simple integer schema versions.

Example:

```yaml
version: 1
```

Future versions may adopt semantic versioning if needed.

---

# Record Relationship

A schema defines payloads.

A record wraps payloads using a common envelope.

The envelope contains routing and indexing metadata, including the schema
reference needed to interpret the payload.

The payload contains schema-specific serialized data.

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

* reading fields directly from serialized storage
* checking field presence
* validating payload structure
* constructing records
* language-specific ergonomic APIs

---

# Embedded Constraints

Design schemas to support constrained devices.

The model should avoid requiring:

* full-message deserialization
* unbounded heap allocation
* recursive parsing without limits
* runtime schema downloads for normal operation
* application-maintained field identities

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

The cloud may deserialize payloads when required, but infrastructure should route
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

* What is the exact syntax of the Breadcrumbs schema language?
* What is the physical binary encoding for Breadcrumbs records?

---

# Relationship to Other Documents

Related architecture documents:

* `data-model.md` defines records, envelopes, and payloads.
* `schema-compiler.md` defines the schema compiler and generated artifacts.
