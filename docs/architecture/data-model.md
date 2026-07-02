# Breadcrumbs Data Model

## Status

Draft

## Purpose

This document defines the logical data model used throughout Breadcrumbs.

The central architectural abstraction is the **record**.

A record is the canonical representation of information exchanged between
devices, stored locally, transmitted over the network, processed in the cloud,
and inspected by tools.

---

# Record-Centered Architecture

Every meaningful interaction in Breadcrumbs is represented as a record.

Examples include:

* telemetry record
* event record
* command record
* configuration record
* diagnostics record
* OTA metadata record

A record is:

* immutable
* schema-defined
* serialized-first
* portable across implementation languages
* suitable for embedded and cloud systems

State changes are represented by creating new records rather than mutating
existing records.

---

# Record Lifecycle

Keep records in binary form for most of their lifecycle.

Typical lifecycle:

```text
Producer
    ↓
record creation
    ↓
Local Queue or Storage
    ↓
Transport
    ↓
Cloud Ingestion
    ↓
Routing, Indexing, or Storage
    ↓
schema-specific processing
```

Infrastructure components should operate on binary records whenever
possible. Full payload interpretation should be limited to components that need
schema-specific business logic.

---

# Record Structure

Every record consists of two logical components:

* envelope
* payload

## Envelope

The envelope contains metadata independent of the payload type.

Typical envelope fields include:

* record type
* schema reference
* deviceId
* timestamp
* sequence number
* flags
* payload length

The envelope allows generic routing, storage, indexing, filtering, and
diagnostics without interpreting the payload.

## Payload

The payload contains schema-specific binary data.

The payload is defined by a schema and accessed through generated accessors.
Only components with the generated artifacts for that schema need to interpret
the payload.

---

# Schemas

A schema defines one record payload type.

A schema defines:

* schema name
* schema version
* schema reference
* field names
* compiler-generated field indexes
* field types
* field presence semantics
* compatibility rules

Author schemas in the Breadcrumbs schema language and compile them with the
schema compiler.

---

# Generated accessors

Applications interact with binary records using generated accessors produced
by the schema compiler.

Example:

```cpp
float temperature = telemetry.temperature();
```

instead of copying fields into a separate language-native mirror.

Use generated accessors directly on binary record storage whenever practical.
They may expose language-specific APIs, but they do not replace the binary record
as the canonical representation.

---

# Local Storage

The Breadcrumbs Agent stores records directly in binary form.

Local persistence should avoid format conversion whenever practical.

Benefits include:

* lower CPU utilization
* lower memory usage
* fewer memory copies
* deterministic execution
* simplified persistence

---

# Cloud Ingestion

Cloud services should ingest records without requiring immediate payload
interpretation.

Infrastructure components may route, filter, compress, and store records using
envelope metadata. Components should interpret payloads only when required by
business logic, validation, indexing, analytics, or inspection.

Cloud services also consume generated artifacts from the schema compiler rather
than interpreting schemas dynamically during normal operation.

---

# Schema Evolution

Expect schemas to evolve throughout the lifetime of the platform.

Rules:

* schema references shall uniquely identify schema definitions.
* compiler-generated field indexes shall never be reused.
* field names shall not be reused with incompatible meaning.
* new fields should be appended when compatibility rules allow.
* removed fields shall remain reserved.
* unknown fields shall be ignored when possible.

Backward compatibility should be preserved whenever practical.

---

# Relationship to Other Documents

Related architecture documents:

* `schema-model.md` defines schema, schema name, schema version, schema
  reference, and field index rules.
* `schema-compiler.md` defines generated artifacts, generated accessors,
  validators, codecs, documentation, and inspector metadata.
* `device-identity.md` defines deviceId.
* `provisioning-model.md` defines how devices become trusted managed devices.
* `security-architecture.md` defines certificate and trust behavior.
