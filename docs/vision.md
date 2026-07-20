# Quarry Vision

## Purpose

Quarry is a schema-driven platform for secure edge-to-cloud systems, with
asset tracking as the first reference application.

The project provides a common architecture for connected devices, local
storage, network transport, cloud ingestion, diagnostics, and tooling. Asset
tracking is the first domain used to validate the architecture because it
exercises identity, telemetry, device lifecycle, fleet operations, security, and
OTA workflows.

---

# Long-Term Vision

Quarry is intended to be a portable foundation for secure connected
systems across embedded Linux, RTOS, bare-metal devices, and cloud services.

The long-term goal is to let teams define structured data once and derive the
runtime, tooling, validation, documentation, and cloud integration needed to use
that data safely across the system.

Quarry should support systems in domains such as:

* asset tracking
* telemetry
* diagnostics
* OTA management
* industrial automation
* robotics
* automotive edge systems
* IoT device fleets

Asset tracking remains the first reference application, not the boundary of the
platform.

---

# Why schemas are central

A schema is the contract between devices, services, tools, and operators.

A schema defines the structure and meaning of a record payload. From that schema,
Quarry can derive generated accessors, validators, binary codecs, inspector
metadata, generated documentation, and language bindings.

This keeps production systems aligned with the stable architecture decisions:

* embedded-first design
* serialized-first records
* schema-driven data modeling
* compile-time knowledge over runtime discovery
* generated artifacts instead of runtime schema reflection

The schema is authored once. Production systems consume generated artifacts from
the schema compiler.

---

# Platform Ecosystem

The long-term Quarry ecosystem includes:

* schema language
* schema compiler
* runtime libraries
* binary record format
* generated language bindings
* cloud services

```text
                 Quarry Platform

          Schema Language (DSL)
                    |
                    v
             Schema Compiler
        +-----------+------------+
        |           |            |
        v           v            v
   Generated     Documentation  Inspector
     APIs          and Specs     Metadata
        |
        v
   Runtime Libraries
        |
        v
    Binary Records
        |
   +----+--------+
   |             |
   v             v
Quarry  Quarry
   Agent       Cloud
   |             |
   +------+------+
          v
  Reference Applications
 (Asset Tracking, ...)
```

These components work together, but they have distinct roles.

The schema language describes records and compatibility rules.

The schema compiler produces generated artifacts for devices, services, SDKs,
tests, documentation, and inspection tools.

Runtime libraries provide the common behavior needed by the Quarry Agent,
Quarry Cloud, and supporting tools.

The binary record format provides the record byte representation used for
transport, local storage, cloud ingestion, and diagnostics.

Generated language bindings expose records to application code without making
hand-written object models the canonical representation.

Cloud services manage device lifecycle, provisioning, trust, ingestion, fleet
operations, and reference application workflows.

---

# Reference Application

Asset tracking is the first reference application for Quarry.

It demonstrates:

* stable device identity
* ownership and fleet assignment
* secure provisioning
* telemetry ingestion
* diagnostics
* OTA metadata
* schema evolution
* cloud operations

The reference application should prove the platform architecture without
hard-coding the platform to one domain.

---

# Non-Goals

Quarry is not intended to be:

* a general-purpose broker
* an RPC framework
* only a binary encoding library
* tied to a single cloud provider
* tied to one programming language or operating system

The project defines a portable architecture that can be implemented consistently
across constrained devices and cloud services.
