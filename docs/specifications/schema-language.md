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

# Authoring Format

Version 0.1 uses YAML syntax.

The Breadcrumbs Schema Compiler validates `.brd` files.

Only language constructs defined by this specification are part of the Breadcrumbs Schema Language.

YAML features not explicitly defined by this specification are considered unsupported.

Unsupported YAML features include:

* anchors
* aliases
* merge keys
* custom tags
* implementation-specific extensions

---

# Schema Structure

Every schema defines exactly one record type.

```yaml
namespace: breadcrumbs.geo

record: Location

version: 1

type: data

imports:

  - breadcrumbs.common.Timestamp

fields:

  latitude:
    type: int32
    unit: degrees
    scale: 1e-7

  longitude:
    type: int32
    unit: degrees
    scale: 1e-7

  altitude:
    type: int32
    unit: meters
    scale: 0.01
```

---

# Namespace

Namespaces organize schemas into logical domains.

Examples:

* breadcrumbs.geo
* breadcrumbs.telemetry
* breadcrumbs.diagnostics
* breadcrumbs.ota
* vendor.company.product

Namespaces are independent of transport protocols.

---

# Record

Each schema defines exactly one record type.

Record names shall be unique within a namespace.

---

# Version

Every schema shall define an integer version.

```yaml
version: 1
```

Schema evolution is defined by the Schema Compatibility specification.

---

# Record Type

The record type identifies the logical purpose of the record.

Version 0.1 defines:

* data
* command
* event
* configuration
* diagnostics

Future specifications may introduce additional record types.

The schema compiler assigns `recordId` metadata used by the binary record
header. Compatible schema evolution keeps the same `recordId`; incompatible
layout or semantic changes require a new `recordId`.

Runtime systems deal with records. Transport protocols carry records but do not
define them.

---

# Fields

Field declarations define the possible fields of a record.

All declared fields are presence-tracked in the binary representation.

The Schema Compiler assigns a hidden `fieldIndex` to each declared field.

Schema authors do not assign or reference `fieldIndex` values.

The schema language does not expose `fieldIndex` syntax.

`fieldIndex` is not a logical identifier for the field.

A field is present in a binary record only when application code sets it through
the generated API.

If the setter is called, the generated setter updates the sparse binary record
directly and the field appears in the Field Directory.

If the setter is not called, no Field Directory entry is written and no value
bytes are encoded for that field.

There is no schema-level field presence category.

A record may declare at most 256 fields because `fieldIndex` is encoded as
`uint8` in the binary record format.

Records needing more than 256 fields should be decomposed into smaller records
using composition.

Example:

```yaml
fields:

  latitude:
    type: int32

  longitude:
    type: int32

  altitude:
    type: int32
```

The binary representation is defined separately.

---

# Presence Semantics

The following states are distinct:

* field absent
* field present with value 0
* field present with another value

Applications shall be able to distinguish these states.

---

# Type System

## Primitive Types

Boolean

* bool

Signed integers

* int8
* int16
* int32
* int64

Unsigned integers

* uint8
* uint16
* uint32
* uint64

Floating point

* float32
* float64

---

## Built-in Types

### string

Represents UTF-8 encoded text.

String fields shall define:

```yaml
max_bytes:
```

which specifies the maximum number of encoded UTF-8 bytes.

### bytes

Represents opaque binary data.

Binary fields shall define:

```yaml
max_bytes:
```

which specifies the maximum number of stored bytes.

---

## Enumerations

Enumerations are first-class language elements.

Example:

```yaml
enum FixType:

  none: 0

  two_d: 1

  three_d: 2
```

---

## Nested Records

Records may reference other records.

Example:

```yaml
location:
  type: breadcrumbs.geo.Location
```

---

## Arrays

Version 0.1 supports bounded variable-length arrays.

Example:

```yaml
satellites:
  type: Satellite[]
  max_elements: 64
```

The number of elements may vary from zero up to `max_elements`.

Fixed-size arrays are intentionally not supported.

Unbounded arrays are not supported.

---

# Field Attribute Categories

Field attributes are grouped by the kind of artifact or behavior they affect.

Runtime attributes:

* type
* scale
* offset

Validation attributes:

* min
* max
* max_bytes
* max_elements
* on_overflow

Documentation attributes:

* unit
* description

---

# Constraint Handling

Some field types define capacity constraints.

Examples include:

* string
* bytes
* arrays

The `on_overflow` schema property defines mandatory producer-side behavior when
input exceeds a schema-defined capacity limit.

Generated builders SHALL enforce the schema-defined overflow behavior
consistently.

Application code SHALL NOT override schema-defined overflow behavior.

Supported values:

* reject
* truncate

If `on_overflow` is omitted, the default behavior is `reject`.

## String Fields

When `on_overflow: truncate` is specified:

* the generated builder SHALL truncate only at a valid UTF-8 boundary
* invalid UTF-8 SHALL never be stored

## Bytes Fields

When `on_overflow: truncate` is specified:

* the generated builder SHALL keep the first `max_bytes` bytes

## Arrays

When `on_overflow: truncate` is specified:

* the generated builder SHALL keep the first `max_elements` elements

## Numeric Types

`on_overflow` SHALL NOT be used with numeric scalar types.

Applications are responsible for handling values violating `min` or `max`.

---

# Numeric Representation

Schemas describe logical values.

Example:

```yaml
latitude:
  type: int32
  unit: degrees
  scale: 1e-7
```

The interpretation is:

```
physical_value = stored_value × scale + offset
```

Fixed-point representation is recommended whenever practical.

---

# Units

Units document the physical meaning of values.

Units do not affect validation, runtime behavior, or binary encoding.

---

# Imports

Schemas may import other schemas.

Example:

```yaml
imports:

  - breadcrumbs.geo.Location

  - breadcrumbs.common.Timestamp
```

Imported schemas may be referenced by fields.

---

# Comments

YAML comments are permitted.

Comments have no semantic meaning.

---

# Compiler Responsibilities

The Breadcrumbs Schema Compiler shall:

* validate schemas
* normalize aliases
* verify compatibility rules
* assign internal field identifiers
* generate runtime bindings
* generate builders
* generate binary codecs
* generate validators
* generate documentation
* generate inspector metadata

Schema authors never assign field identifiers.

---

# Relationship to Other Specifications

This specification defines:

* schema syntax
* schema semantics

It intentionally does not define:

* binary record format
* runtime API behavior
* transport protocols

---

# Future Extensions

Future versions may introduce:

* logical types (UUID, timestamp, IP address, etc.)
* generic types
* unions / variants
* user-defined annotations
* compile-time constants
* reusable field groups
* schema inheritance
* dynamic schemas
