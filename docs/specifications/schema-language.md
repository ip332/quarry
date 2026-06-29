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

The schema defines the logical model.

It does **not** define the binary encoding.

---

# Design Goals

The language is designed to be:

* human readable
* embedded-first
* deterministic
* schema-driven
* compiler friendly
* language independent
* easy to validate
* easy to review in source control

---

# Authoring Format

Version 0.1 uses YAML syntax.

Only the language constructs described in this specification are part of the Breadcrumbs Schema Language.

YAML features not explicitly defined by this specification shall be considered unsupported.

The goal is deterministic parsing.

---

# Schema Structure

Every schema contains:

```yaml
namespace:
record:
version:
type:
fields:
```

Example:

```yaml
namespace: breadcrumbs.geo

record: Location

version: 1

type: data

fields:

  latitude:
    type: int32
    unit: degrees
    scale: 1e-7

  longitude:
    type: int32
    unit: degrees
    scale: 1e-7

  altitude?:
    type: int32
    unit: meters
    scale: 0.01
```

---

# Namespace

Namespaces organize schemas into logical domains.

Examples:

```text
breadcrumbs.geo

breadcrumbs.telemetry

breadcrumbs.diagnostics

breadcrumbs.ota

vendor.company.product
```

Namespaces describe logical ownership.

They are independent of transport protocols.

---

# Record

Each schema defines exactly one record.

Record names shall be unique within a namespace.

---

# Version

Every schema defines an integer version.

```yaml
version: 1
```

Versioning rules are defined in the Schema Compatibility specification.

---

# Record Type

Record type identifies the logical purpose of the record.

Initial record types:

* data
* event
* command
* configuration
* diagnostics

Future specifications may introduce additional record types.

---

# Fields

Each field has a unique name.

Example:

```yaml
latitude:
  type: int32
```

---

# Required and Optional Fields

Fields are **required by default**.

Optional fields are indicated by appending `?` to the field name.

Example:

```yaml
fields:

  latitude:
    type: int32

  longitude:
    type: int32

  altitude?:
    type: int32
```

The schema defines **semantics**, not encoding.

The schema compiler determines how optional fields are represented in the binary format.

---

# Presence Semantics

The following states are distinct:

* field absent
* field present with value 0
* field present with another value

Applications shall be able to distinguish these states.

The Binary Record Format specification defines the physical representation.

---

# Primitive Types

Version 0.1 supports:

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

Other

* string
* bytes

---

# Enumerations

Enumerations are first-class language elements.

Example:

```yaml
enum FixType:

  none: 0

  two_d: 1

  three_d: 2

  dead_reckoning: 3
```

Fields may reference enum types.

---

# Nested Records

Records may reference other records.

Example:

```yaml
location:
  type: breadcrumbs.geo.Location
```

Nested records are embedded logically within the parent record.

Their binary representation is defined separately.

---

# Arrays

Version 0.1 supports **bounded variable-length arrays**.

Example:

```yaml
satellites:
  type: GnssSatellite[]
  max_count: 64
```

The number of elements present in a record may vary from zero up to `max_count`.

Unbounded arrays are not supported.

Fixed-size arrays are intentionally omitted because the schema language models logical data rather than memory layout.

---

# Field Attributes

# Field Attributes

## Runtime

- type
- scale
- offset

## Validation

- min
- max
- max_count

## Documentation

- unit
- description

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
physical_value = stored_value × scale
```

Fixed-point representation is recommended whenever practical.

---

# Units

Units describe the physical meaning of a value.

Units do not define binary encoding.

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

# Field Attribute Categories
Runtime attributes:
- type
- scale
- offset

Validation attributes:
- min
- max
- max_count

Documentation attributes:
- unit
- description

# Comments

YAML comments are permitted.

Comments have no semantic meaning.

---

# Compiler Responsibilities

The Breadcrumbs Schema Compiler shall:

* validate schemas
* assign internal field identities
* verify compatibility rules
* generate runtime bindings
* generate record builders
* generate binary codecs
* generate validators
* generate documentation
* generate inspector metadata

Schema authors never assign field identities.

---

# Relationship to Other Specifications

This specification defines:

* schema syntax
* schema semantics

It does not define:

* binary encoding
* runtime APIs
* transport protocols

---

# Future Extensions

Possible future extensions include:

* generic types
* unions / variants
* user-defined annotations
* computed fields
* schema inheritance
* dynamic schemas
* compile-time constants
* reusable field groups
