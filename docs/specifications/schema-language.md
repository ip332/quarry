# Breadcrumbs Schema Language Specification

## Status

Draft

## Version

0.1

---

# Purpose

This document defines the Breadcrumbs Schema Language (BSL).

BSL is used to describe the logical structure of Breadcrumbs records.

Schemas are the single source of truth from which the Breadcrumbs Schema Compiler generates:

* runtime bindings
* record builders
* binary codecs
* validators
* documentation
* inspector metadata
* testing artifacts

The schema defines the logical data model.

It does **not** define:

* binary encoding
* runtime behavior
* transport protocols

Those are defined by separate specifications.

---

# Design Goals

The Breadcrumbs Schema Language is designed to be:

* human readable
* embedded-first
* deterministic
* compiler friendly
* language independent
* schema driven
* easy to validate
* easy to review in source control

---

# Normative Language

This specification uses the following normative terms:

* SHALL: required behavior.
* SHOULD: recommended behavior with valid exceptions.
* MAY: optional behavior.
* MUST NOT: prohibited behavior.

Use these terms only for normative requirements.

---

# Requirements

## REQ-SL-001

Version 0.1 `.brd` files SHALL use YAML as the source format.

## REQ-SL-002

A `.brd` file SHALL be one YAML document describing one schema.

Version 0.1 schema documents define one source schema unit. A source schema
unit has one namespace path and one primary record. Fields belong to that
record. Enum declarations belong to the same namespace and may be referenced by
the record's fields.

## REQ-SL-003

The document root SHALL be a YAML mapping containing the top-level keys
`namespace`, `record`, `version`, `type`, and `fields`.

## REQ-SL-004

The document root MAY contain the optional keys `imports`, `enums`, and
`annotations`.

## REQ-SL-005

Duplicate top-level keys SHALL be invalid.

## REQ-SL-006

Unknown top-level keys SHALL be invalid.

## REQ-SL-007

The source-language specification SHALL not rely on unsupported YAML features
such as anchors, aliases, merge keys, custom tags, or implementation-specific
extensions.

## REQ-SL-008

The `namespace` property SHALL be a dotted qualified-name string.

## REQ-SL-009

The `record` property SHALL be the logical record name for the schema.

The source language does not define a `records` collection or multiple record
declarations in one schema document.

## REQ-SL-010

The `type` property SHALL identify the logical record type and SHALL be one of
`data`, `command`, `event`, `configuration`, or `diagnostics`.

## REQ-SL-011

Record names SHALL be unique within a namespace.

## REQ-SL-012

The `fields` property SHALL be a YAML mapping from field name to field
definition.

## REQ-SL-013

Field names SHALL be unique within a record.

## REQ-SL-014

Each field definition SHALL be a YAML mapping containing the required `type`
key and MAY contain `max_bytes`, `max_elements`, and `annotations`.

## REQ-SL-015

Unknown keys inside a field definition SHALL be invalid.

## REQ-SL-016

Duplicate keys inside a field definition SHALL be invalid.

## REQ-SL-017

The order of fields in the `fields` mapping SHALL be preserved by the compiler
pipeline.

## REQ-SL-018

The `annotations` field property SHALL be generic string-valued metadata.

## REQ-SL-019

The `max_bytes` field property SHALL be a native YAML integer value and SHALL
not be encoded as a string annotation.

## REQ-SL-020

The `max_elements` field property SHALL be a native YAML integer value and
SHALL not be encoded as a string annotation.

## REQ-SL-021

The `enums` property, when present, SHALL be a YAML mapping from enum name to
enum definition.

## REQ-SL-022

Each enum definition SHALL contain the required `values` key and MAY contain
`annotations`.

## REQ-SL-023

Enum values SHALL be unique within their enum.

## REQ-SL-024

Enum values SHALL use explicit integer literals.

## REQ-SL-025

Enum value order SHALL be preserved by the compiler pipeline.

## REQ-SL-026

Unknown keys inside an enum definition SHALL be invalid.

## REQ-SL-027

The `type` property of a field SHALL accept a canonical primitive name, a
primitive alias, `string`, `bytes`, a qualified named type, or a bounded
variable-length array type ending in `[]`.

## REQ-SL-028

Accepted primitive names and aliases SHALL include `bool`, `i8`/`int8`,
`u8`/`uint8`, `i16`/`int16`, `u16`/`uint16`, `i32`/`int32`, `u32`/`uint32`,
`i64`/`int64`, `u64`/`uint64`, `f32`/`float32`, and `f64`/`float64`.

## REQ-SL-029

Primitive aliases SHALL be semantically equivalent and canonicalized by the
compiler.

## REQ-SL-030

`string` and `bytes` SHALL be distinct built-in kinds.

## REQ-SL-031

Named types SHALL resolve to records or enums in the compiler namespace model.

## REQ-SL-032

Relative and fully qualified references to the same declaration SHALL resolve
to the same canonical target FQN.

## REQ-SL-033

`string` fields SHALL carry a `max_bytes` property.

For `string`, `max_bytes` SHALL be measured in encoded UTF-8 bytes, not Unicode
code points.

## REQ-SL-034

`bytes` fields SHALL carry a `max_bytes` property.

For `bytes`, `max_bytes` SHALL be measured in raw bytes.

## REQ-SL-035

`max_bytes` SHALL be a positive YAML integer.

## REQ-SL-036

`max_bytes` SHALL NOT appear on fields whose `type` is neither `string` nor
`bytes`.

## REQ-SL-037

Version 0.1 arrays SHALL be bounded variable-length arrays.

## REQ-SL-038

The source form of an array field SHALL use a `type` value ending in `[]`.

## REQ-SL-039

An array field SHALL carry exactly one `max_elements` property.

## REQ-SL-040

`max_elements` SHALL be a positive YAML integer.

## REQ-SL-041

`max_elements` SHALL NOT appear on fields whose `type` is not an array type.

## REQ-SL-042

Fixed-size array syntax such as `uint32[64]` SHALL be rejected in version 0.1.

## REQ-SL-043

Nested arrays such as `uint32[][]` SHALL be rejected in version 0.1.

## REQ-SL-044

Arrays of otherwise valid field types SHALL be permitted unless another
normative rule forbids a specific combination.

## REQ-SL-045

Generic annotations SHALL be string-valued metadata under an `annotations`
mapping.

## REQ-SL-046

Typed field properties SHALL be native YAML keys with defined names and value
types.

## REQ-SL-047

Unknown typed field properties SHALL be invalid.

## REQ-SL-048

Duplicate typed field properties SHALL be invalid.

## REQ-SL-049

The YAML parser and source decoder SHALL preserve file, line, and column
information for diagnostics.

## REQ-SL-050

YAML syntax errors SHALL be normalized into Breadcrumbs diagnostics.

## REQ-SL-051

Semantic validation SHALL remain a separate compiler stage from YAML decoding.

## REQ-SL-052

Source order of YAML mappings and sequences SHALL be preserved when schema data
is converted into compiler models.

## REQ-SL-053

The source-language specification SHALL defer binary record format, runtime API
behavior, and transport protocols to separate specifications.

---

# Implementation Status

The current declaration parser and AST are transitional implementation
scaffolding.

Current tests may exercise legacy declaration syntax and fixed-size-array
forms.

Those implementation artifacts do not redefine the normative `.brd` contract.

The frontend is expected to migrate to YAML decoding in a later implementation
increment.

---

# Examples

Examples remain informative only.

```yaml
namespace: breadcrumbs.telemetry
record: Sample
version: 1
type: data
fields:
  samples:
    type: uint32[]
    max_elements: 64
```

---

# Related Documents

* `docs/compiler-architecture.md`
* `docs/compiler-passes.md`
* `docs/schema-ir.md`
* `docs/architecture/schema-compiler.md`
* `docs/specifications/schema-compiler.md`
