# Schema Compiler

## Status

Implemented Stage A responsibilities, with historical future compatibility
requirements explicitly identified below. No manifest, profile-selection or
historical schema-comparison mode is implemented.

## Version

0.1

---

## Purpose

The Quarry Schema Compiler turns `.brd` schema files into generated
artifacts for applications, runtimes, tooling, and language bindings.

The `.brd` source-language grammar and semantics are defined by
`docs/specifications/schema-language.md`. This specification defines compiler
responsibilities and architecture only.

---

## Scope

Production YAML configuration compilation, namespace output selection and the
existing generation/identity contracts. Legacy declaration syntax is parser-test
compatibility infrastructure. Stage B discovery and historical schema evolution
are not available production modes.

## Terminology

A root is the one CLI input; its complete relative-import closure is the
configuration. A namespace is the generation unit; a domain is a namespace that
declares records. Wire schema_id, native gid and runtime epochs are defined in
[the identity contract](configuration-and-code-identity.md).

## Requirements

### REQ-SC-001

The Schema Compiler SHALL parse schema files.

### REQ-SC-002

The production YAML compiler SHALL load the root and its complete transitive
relative-import closure. Legacy declaration-syntax imports SHALL remain parser
compatibility syntax only.

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

### REQ-SC-009 (future compatibility work, not implemented in Stage A)

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

The production CLI consumes exactly one YAML root, its transitive relative-import
closure, target language/storage/naming options and emission selections. Internal
passes consume normalized source-schema models. Compatibility metadata, reserved
field mappings, persistent manifests and selectable target profiles are historical
future proposals, not supported CLI inputs.

---

## Outputs

Current outputs are C, C++ owning, C++/C-storage, Python or Go bindings for
selected namespaces, explicit minimal/reflective QBS images, and read-only CLI
inventories/identity/description text. Generated files include accessors,
validators and codecs. Schema IR remains compiler-owned tooling data.
Compatibility reports, persistent schema manifests and generated documentation
are not implemented outputs. Generation never deletes stale files.

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

## Implemented configuration generation

REQ-SC-022: Configuration preparation SHALL compute the canonical BRF layout and
configuration identity once for generation/identity queries, then pass them to
backends and optional QBS emission. Output listing SHALL use backend planning and
SHALL NOT require identity hashing. Identity and selection behavior SHALL follow
[REQ-ID-001 through REQ-ID-010](configuration-and-code-identity.md#requirements).

REQ-SC-023: Repeated generation with identical inputs and effective options SHALL
produce identical output files. Import reordering SHALL preserve record IDs,
schema_id and QBS bytes; source declaration ordering is not guaranteed invariant
(issue #90). `--generate` SHALL change emission selection only.

REQ-SC-024: `--name-prefix` SHALL change source names and paths, not canonical
Schema IR names, record IDs, schema_id or BRF/QBS bytes. Storage-selection FQNs
SHALL remain canonical and unprefixed. Effective C host-storage policies SHALL
apply to unselected dependencies when computing native generated identity.

CLI argument rules, diagnostics and exits are defined in
[Tools](../../tools/README.md); executable examples and checks are in the
[Stage A CLI guide](../stage-a-cli.md).

## Examples

The root `examples/truck/schemas/configuration.brd` imports eight record units.
Selecting `truck.engine` emits the complete engine namespace while its dependencies
retain the full root's IDs/identity. Compiling `engine.brd` independently instead
creates a different configuration. [Executable examples](../stage-a-cli.md) cover
selection, prefixes, storage, QBS modes and query diagnostics.

## Related Documents

- [Schema language](schema-language.md)
- [Configuration and generated-code identity](configuration-and-code-identity.md)
- [Compiler architecture](../compiler-architecture.md)
- [Tools](../../tools/README.md)
