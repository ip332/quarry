# Schema Compiler

## Status

Draft

## Version

0.1

---

## Purpose

The Breadcrumbs Schema Compiler turns `.brd` schema files into generated
artifacts for applications, runtimes, tooling, and language bindings.

The `.brd` source-language grammar and semantics are defined by
`docs/specifications/schema-language.md`. This specification defines compiler
responsibilities and architecture only.

---

## Requirements

### REQ-SC-001

The Schema Compiler SHALL parse schema files.

### REQ-SC-002

The Schema Compiler SHALL accept legacy import declarations as syntax only and
SHALL NOT require an import-resolution stage to compile the current supported
schema pipeline.

### REQ-SC-003

The Schema Compiler SHALL validate schemas.

### REQ-SC-004

The Schema Compiler SHALL compute binary layouts.

### REQ-SC-005

The Schema Compiler SHALL assign and preserve `fieldIndex` values used by the
binary Field Directory.

### REQ-SC-006

The Schema Compiler SHALL produce the compiler IR consumed by generated
artifacts and language generators.

### REQ-SC-007

The Schema Compiler SHALL invoke language generators for requested target
languages.

### REQ-SC-008

The Schema Compiler SHALL produce compiler artifacts requested by the selected
configuration.

### REQ-SC-009

The Schema Compiler SHALL support compatibility analysis involving `fieldIndex`
and type associations.

### REQ-SC-010

Generated runtime bindings SHALL distinguish absent fields from present fields.

### REQ-SC-011

Generated binary codecs SHALL preserve compiler-generated field indexes.

### REQ-SC-012

Generated binary codecs SHALL support deterministic encoding.

### REQ-SC-013

Generated binary codecs SHALL support bounded parsing.

### REQ-SC-014

Generated binary codecs SHALL avoid unnecessary allocation.

### REQ-SC-015

Generated binary codecs SHALL support unknown-field handling when allowed by
the encoding.

### REQ-SC-016

Generated binary codecs SHALL expose profile-specific limits for embedded
targets.

### REQ-SC-017

Application code SHALL NOT hand-maintain payload encoding logic.

### REQ-SC-018

New language generators SHALL NOT require changes to the schema language or the
binary record format.

### REQ-SC-019

The compiler architecture MAY allow additional language generators and tooling
without changing the schema language or binary record format.

### REQ-SC-020

The compiler MAY generate only the artifacts explicitly requested by the user
or build configuration.

### REQ-SC-021

The IR MAY exist only in memory or MAY be persisted as an implementation
detail.

---

## Inputs

The Schema Compiler consumes:

* `.brd` schema files
* parsed AST documents that may contain legacy import declarations
* compatibility metadata
* reserved field metadata
* target language configuration
* target profile configuration

Target profiles may describe constraints such as embedded memory limits,
supported field types, maximum nesting depth, and enabled generated outputs.

---

## Outputs

The Schema Compiler may produce:

* generated accessors
* generated validators
* generated binary codecs
* generated inspector metadata
* generated documentation
* compatibility reports
* schema manifests
* language-specific SDK files

Generated outputs are build artifacts. Production systems consume these
artifacts directly.

---

## Compilation Model

The conceptual compilation stages are:

1. Parse schema files.
2. Validate syntax and semantics.
3. Build a unified schema model.
4. Compute binary layouts and serialization metadata.
5. Produce an Intermediate Representation (IR).
6. Invoke language generators.
7. Produce compiler artifacts.

These stages describe the required compiler responsibilities, not a mandatory
implementation algorithm or internal module structure.

---

## Non-Responsibilities

The Schema Compiler does not define runtime behavior, network protocols, or
storage formats beyond generated artifacts.

Runtime systems, transports, storage engines, and applications consume
compiler artifacts according to their own specifications and architecture
documents.

---

## Extensibility

The compiler architecture is designed so that additional language generators
and tooling can be added without changing the schema language or binary record
format.

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

This document defines compiler architecture only. Detailed behavior is defined
by the dedicated specifications that own those topics.
