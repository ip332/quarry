# Quarry Documentation

This directory contains the project documentation for Quarry.

Quarry is a schema-driven platform for secure edge-to-cloud systems, with
asset tracking as the first reference application.

The documentation records stable architecture decisions and separates them from
future implementation specifications.

---

# Purpose

The documentation exists to:

* explain the long-term platform vision
* define stable architecture decisions
* keep terminology consistent across the project
* provide a reading path for contributors
* identify future specification work

Architecture documents answer:

> What is the system?

Specification documents answer:

> How does a specific part of the system work?

Architecture is intentionally higher level than implementation. Specifications
will define precise formats, APIs, protocols, algorithms, and generated outputs.

---

# Recommended Reading Order

## Project Foundation

1. [`vision.md`](vision.md)
2. [`principles.md`](principles.md)

These documents explain the platform vision and the architectural principles
that guide all other design work.

## Device Architecture

1. [`architecture/device-lifecycle.md`](architecture/device-lifecycle.md)
2. [`architecture/bootstrap.md`](architecture/bootstrap.md)
3. [`architecture/device-identity.md`](architecture/device-identity.md)
4. [`architecture/ownership-model.md`](architecture/ownership-model.md)
5. [`architecture/provisioning-model.md`](architecture/provisioning-model.md)
6. [`architecture/security-architecture.md`](architecture/security-architecture.md)

These documents define the lifecycle of a managed device, how local identity is
created, how ownership is established, how provisioning works, and how trust is
maintained.

## Data Architecture

1. [`architecture/data-model.md`](architecture/data-model.md)
2. [`architecture/schema-model.md`](architecture/schema-model.md)
3. [`architecture/schema-compiler.md`](architecture/schema-compiler.md)
4. [`architecture/language-generators.md`](architecture/language-generators.md)

## Schema Design

1. [`design/schema-identity.md`](design/schema-identity.md)
2. [`design/schema-evolution.md`](design/schema-evolution.md)

These documents define records, schemas, generated artifacts, and the
compile-time model used by production systems.

## Backend Design

1. [`design/c-backend.md`](design/c-backend.md)

This document proposes the architecture and public API for the next
production-quality language backend, building on
[`architecture/language-generators.md`](architecture/language-generators.md)
and [`backend-api.md`](backend-api.md).

---

# Current Architecture Documents

## Device and Trust

* [`architecture/device-lifecycle.md`](architecture/device-lifecycle.md)
* [`architecture/device-identity.md`](architecture/device-identity.md)
* [`architecture/ownership-model.md`](architecture/ownership-model.md)
* [`architecture/bootstrap.md`](architecture/bootstrap.md)
* [`architecture/provisioning-model.md`](architecture/provisioning-model.md)
* [`architecture/security-architecture.md`](architecture/security-architecture.md)

## Data and schemas

* [`architecture/data-model.md`](architecture/data-model.md)
* [`architecture/schema-model.md`](architecture/schema-model.md)
* [`architecture/schema-compiler.md`](architecture/schema-compiler.md)
* [`architecture/language-generators.md`](architecture/language-generators.md)

## Design Notes

* [`design/schema-identity.md`](design/schema-identity.md)
* [`design/schema-evolution.md`](design/schema-evolution.md)
* [`design/c-backend.md`](design/c-backend.md)

## Current Specifications

* [`specifications/schema-language.md`](specifications/schema-language.md)
* [`specifications/binary-record-format.md`](specifications/binary-record-format.md)
* [`specifications/schema-compiler.md`](specifications/schema-compiler.md)
* [`specifications/manifest-format.md`](specifications/manifest-format.md)

---

# Planned Future Specifications

Future specification documents should define implementation details for:

* generated runtime bindings
* runtime library APIs
* transport protocol
* registration API
* provisioning API
* certificate lifecycle
* telemetry ingestion
* OTA metadata records

Schema language syntax and record envelope layout are already covered by
`specifications/schema-language.md` and `specifications/binary-record-format.md`
(see "Current Specifications" above), not planned future work.

These specifications should preserve the architecture decisions documented in
`principles.md` and `architecture/`.

Specification authoring rules and the shared Markdown template are defined in
[`specifications/README.md`](specifications/README.md).

---

# Documentation Structure

```text
docs/

    README.md
    vision.md
    principles.md
    diagnostics.md
    compiler-architecture.md
    compiler-passes.md
    schema-ir.md
    layout-algorithm.md
    backend-api.md
    distribution-model.md
    schema-compiler-tool-distribution.md
    development-environment.md
    decisions.md

    architecture/
        README.md
        language-generators.md
        device-lifecycle.md
        device-identity.md
        ownership-model.md
        bootstrap.md
        provisioning-model.md
        security-architecture.md
        data-model.md
        schema-model.md
        schema-compiler.md

    design/
        schema-identity.md
        schema-evolution.md
        c-backend.md

    specifications/
        README.md
        spec-authoring-guide.md
        schema-language.md
        binary-record-format.md
        schema-compiler.md
        manifest-format.md

    adr/
        0001-schema-ir-references.md

    examples/
        manifest.pbtxt
```

---

# Contributing

When adding or changing documentation:

* keep architecture documents focused on stable "what" decisions
* keep specifications focused on precise "how" details
* use `deviceId`, `record`, `schema`, `envelope`, `payload`, and `schema compiler`
  consistently
* use `Quarry Agent` and `Quarry Cloud` for product components
* avoid duplicating detailed explanations across documents

When two documents need the same concept, keep the full explanation in the most
specific architecture document and add a short reference from the other.
