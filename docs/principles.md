# Quarry Architectural Principles

## Status

Draft

## Purpose

This document defines the core architectural principles used across
Quarry.

Quarry is designed for systems that span constrained devices, local
storage, network transport, cloud ingestion, and operations tooling. The same
architecture must work for Linux, RTOS, and bare-metal implementations.

---

# Core Principles

## Embedded-First

Quarry starts from the constraints of embedded systems.

Architectural decisions should favor:

* deterministic execution
* bounded memory usage
* bounded latency
* limited heap allocation
* predictable failure behavior
* portability across Linux, RTOS, and bare-metal targets

Cloud services and developer tools may provide richer behavior, but they should
build on the same model rather than requiring a separate representation that
cannot run on constrained devices.

## Serialized-First

Binary records are the canonical runtime representation.

The binary representation is not only a transport concern. The same record byte
format is used for:

* local persistence
* network transfer
* cloud ingestion
* diagnostics
* audit trails
* OTA metadata

Production systems should keep records in binary form for most of their
lifecycle.
Components should inspect the envelope when generic routing, storage, indexing,
or filtering is sufficient. Components should interpret payloads only when
business logic requires schema-specific access.

## Schema-Driven

Every record payload is defined by a schema.

The schema defines:

* schema name
* schema version
* schema reference
* field names
* compiler-generated field indexes
* field types
* field presence semantics
* compatibility rules

Programming language structs, classes, database tables, and API DTOs are derived
views. They do not define the canonical model.

## Compile-Time Knowledge

Production systems consume generated artifacts rather than interpreting schemas
at runtime.

The schema compiler turns schemas into:

* generated accessors
* validators
* binary codecs
* inspector metadata
* generated documentation
* language-specific SDK artifacts

Runtime components should not depend on dynamic schema interpretation, runtime
schema downloads, or dynamic field discovery for normal operation. Compile
schema knowledge into the firmware, application, service, or tool that needs it.

Generated codecs should keep schema-specific decisions in generated code and
delegate only representation-neutral byte mechanics and structural record
parsing to runtime libraries.

## Immutable Records

All records are immutable.

Once created, a record shall not be modified. State changes are represented by
new records.

Immutable records simplify:

* synchronization
* local persistence
* cloud ingestion
* replay
* diagnostics
* auditing

---

# Data Model Principles

The record is the central architectural abstraction in Quarry.

Each record contains:

* an envelope with generic metadata
* a payload with schema-specific binary data

The envelope allows infrastructure to route and store records without knowing
the payload schema. The payload is accessed through generated accessors produced
by the schema compiler.

---

# Identity Principles

Device identity uses a stable deviceId.

The deviceId identifies the device across provisioning, ownership, certificate
lifecycle, telemetry, and fleet operations. It shall not change during the
device lifetime.

---

# Relationship to Architecture Documents

Detailed architecture decisions are defined in:

* `docs/architecture/data-model.md`
* `docs/architecture/schema-model.md`
* `docs/architecture/schema-compiler.md`
* `docs/architecture/device-identity.md`
* `docs/architecture/provisioning-model.md`
* `docs/architecture/security-architecture.md`
