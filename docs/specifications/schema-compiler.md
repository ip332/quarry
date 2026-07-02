# Schema Compiler

## Status

Draft

## Version

0.1

---

## Purpose

The Breadcrumbs Schema Compiler is the authoritative component responsible for
transforming one or more `.brd` schema files into the artifacts required by
applications, runtimes, tooling, and language bindings.

The Schema Compiler is responsible for validating schemas, computing binary
layouts, and ensuring consistency across generated artifacts.

This specification defines the compiler architecture and responsibilities. It
does not define implementation algorithms, command-line syntax, manifest
encoding, public runtime APIs, or record identity assignment algorithms.

---

## Inputs

The Schema Compiler consumes:

* one or more `.brd` schema files
* imported schemas
* compiler options
* optional compiler state, when required by the selected compiler mode

After import resolution, schemas are treated as a single logical schema model.

Compiler options may select target languages, output locations, validation
profiles, or generated artifact types. This specification does not define
command-line option names or configuration file syntax.

---

## Compilation Model

The conceptual compilation stages are:

1. Parse schema files.
2. Resolve imports.
3. Validate syntax and semantics.
4. Build a unified schema model.
5. Compute binary layouts and serialization metadata.
6. Produce an Intermediate Representation (IR).
7. Invoke language generators.
8. Produce compiler artifacts.

These stages describe the required compiler responsibilities, not a mandatory
implementation algorithm or internal module structure.

The IR is the compiler-owned representation of the resolved schema model,
computed layouts, identities, and generation inputs needed by downstream
artifacts. The IR MAY exist only in memory or MAY be persisted as an
implementation detail.

---

## Outputs

The Schema Compiler may generate:

* language bindings
* manifest
* documentation
* compatibility report
* test vectors
* runtime metadata

Individual compiler implementations MAY generate additional artifacts.

The compiler MAY generate only the artifacts explicitly requested by the user or
build configuration.

Generated artifacts are derived from the resolved schema model. They are not the
authoritative schema definition.

---

## Responsibilities

The Schema Compiler SHALL parse schema files.

The Schema Compiler SHALL resolve imports.

The Schema Compiler SHALL validate schemas.

The Schema Compiler SHALL compute binary layouts.

The Schema Compiler SHALL assign and preserve `fieldIndex` values used by the
binary Field Directory.

The Schema Compiler SHALL produce the IR consumed by generated artifacts and
language generators.

The Schema Compiler SHALL invoke language generators for requested target
languages.

The Schema Compiler SHALL produce compiler artifacts requested by the selected
configuration.

The Schema Compiler SHALL support compatibility analysis involving
`fieldIndex` and type associations.

This specification does not define record identity algorithms.

---

## Non-Responsibilities

The Schema Compiler does not define:

* runtime behavior
* network protocols
* storage formats beyond generated artifacts
* application business logic

Runtime systems, transports, storage engines, and applications consume compiler
artifacts according to their own specifications and architecture documents.

---

## Extensibility

The compiler architecture is designed so that additional language generators and
tooling can be added without changing the schema language or binary record
format.

New language generators SHALL NOT require changes to the schema language or the
binary record format.

Language generators consume the compiler IR and produce target-language
artifacts. They translate compiler-owned schema semantics into language-specific
constructs without changing schema identity, record identity, compatibility
rules, or binary layout.

---

## Deferred Specifications

Detailed behavior is intentionally deferred to dedicated specifications:

* Schema Language
* Binary Record Format
* Record Identity
* Manifest Format
* Runtime API
* Compatibility Rules

This document defines the compiler architecture only. Detailed behavior SHALL be
defined by the dedicated specifications that own those topics.
