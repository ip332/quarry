# Schema IR

## Overview

Schema IR is the compiler's stable, backend-facing representation of one fully
compiled schema.

Schema IR is:

* the final compiler-owned representation
* backend-neutral
* consumed by all compiler backends
* independent of source syntax
* independent of parser implementation
* independent of runtime binary encoding

Schema IR may be serialized for debugging or tooling, but it is not the runtime
manifest and it is not the binary record format.

---

## Why Protobuf

Schema IR is defined as a protobuf model to provide a stable, language-neutral
contract between the compiler core and compiler backends.

Using protobuf keeps the IR independent of any single compiler implementation
language. It also allows Schema IR to be serialized later for debug or tooling
workflows without redefining the object model.

The protobuf model is still a compiler design artifact. It does not imply that
runtime systems consume Schema IR.

---

## Design Principles

Schema IR represents one fully compiled schema.

Schema IR uses a hierarchical namespace tree.

Schema IR contains no import objects.

Schema IR contains only resolved references.

Schema IR carries compiler-only source metadata.

Schema IR must never be used as the runtime binary format.

Schema IR should evolve independently from the runtime manifest.

---

## Object Model

The initial protobuf model defines:

* `SchemaIR`
* `NamespaceIR`
* `RecordIR`
* `EnumIR`
* `EnumValueIR`
* `FieldIR`
* `FieldType`
* `SourceOrigin`
* `SourceSpan`

`SchemaIR`, `NamespaceIR`, `RecordIR`, `EnumIR`, `EnumValueIR`, and `FieldIR`
are semantic IR objects.

`FieldType` is a value carried by `FieldIR`, not a top-level semantic object.

### SchemaIR

`SchemaIR` is the root object for one fully compiled schema.

It owns the root namespace and compiler-wide metadata needed by backends.

### NamespaceIR

`NamespaceIR` represents one namespace node in the resolved namespace tree.

It owns child namespaces, records, and enums declared in that namespace.

### RecordIR

`RecordIR` represents one resolved record declaration.

It owns its fields.

`RecordIR.record_id` is a compiler-assigned binary/runtime identifier copied
from the Layout Model. It is distinct from `RecordIR.ir_id`, which remains a
compiler-internal object handle used for references inside Schema IR.

### EnumIR

`EnumIR` represents one resolved enum declaration.

It owns its enum values.

### EnumValueIR

`EnumValueIR` represents one resolved enum value.

### FieldIR

`FieldIR` represents one resolved field owned by a record.

It owns a `FieldType` value.

### FieldType

`FieldType` represents a resolved field type.

`FieldType` is a value object, not an IR node.

`FieldType` has no independent identity and no source metadata.

Field types are represented by protobuf enums and `oneof` variants, not
free-form strings.

Conceptual protobuf shape:

```proto
message FieldIR {
  string name = 1;
  uint32 field_index = 2;
  FieldType type = 3;
  SourceOrigin source_origin = 20;
}

message FieldType {
  oneof kind {
    PrimitiveType primitive = 1;
    RecordRef record = 2;
    EnumRef enum_type = 3;
    ArrayType array = 4;
    BytesType bytes = 5;
    StringType string = 6;
  }
}

message ArrayType {
  FieldType element_type = 1;
  uint32 count = 2;
}
```

### SourceOrigin and SourceSpan

`SourceOrigin` and `SourceSpan` describe where a semantic object came from.

Source metadata is compiler-only.

---

## Ownership Model

Schema IR uses hierarchical ownership:

```text
SchemaIR
  `-- NamespaceIR
      |-- NamespaceIR
      |-- RecordIR
      `-- EnumIR
```

Records own fields.

Enums own enum values.

Fields own `FieldType` values.

`FieldType` may contain references to records or enums by stable compiler
identifiers rather than by unresolved names.

---

## Type System

Schema IR avoids string-encoded types.

Field types are represented by `FieldType`.

`FieldType` uses protobuf `oneof`.

The initial type system supports:

* primitive scalar types
* record references
* enum references
* bounded variable-length arrays
* bytes
* strings

Primitive scalar types are represented by a protobuf enum.

Record references use resolved references to `RecordIR` objects.

Enum references use resolved references to `EnumIR` objects.

Array types represent bounded variable-length arrays from the source-language
contract. They recursively contain a `FieldType` element type and carry the
validated maximum element count as compiler metadata.

String and bytes remain the bounded variable-length types when supported.

---

## References

Schema IR contains no unresolved names.

References use stable compiler identifiers where possible.

The protobuf model distinguishes ownership from references:

* namespaces own namespaces, records, and enums
* records own fields
* fields own `FieldType` values
* record and enum type references point to existing compiler IR objects

Backends should not perform name resolution.

---

## Source Metadata

Every semantic object should contain `SourceOrigin` when source information is
available.

Source metadata should include:

* source unit
* file
* source span

Source spans are preferred over single line/column locations.

Source metadata supports diagnostics, generated documentation, IDE/LSP features,
debug artifacts, and developer-facing inspection tools.

Source metadata is compiler-only and must not be emitted into runtime artifacts.

---

## Compiler Metadata

Schema IR may contain compiler-only IDs.

Compiler-only IDs support stable references inside Schema IR and between
backends.

Compiler metadata is never emitted directly into runtime artifacts unless a
runtime specification explicitly defines a corresponding runtime field.

`recordId` and `fieldIndex` remain separate binary/runtime concepts defined by
the compiler and binary record model. Schema IR references should not rely on
unresolved text names when a compiler identifier is available.

`recordId` values are assigned by the Layout Model, copied into `RecordIR`, and
validated to be nonzero and globally unique within Schema IR.

---

## Relationship to Layout IR

Layout IR computes deterministic binary layout information.

Schema IR is independent of runtime binary encoding, but it may be produced
after layout computation so backends have access to compiler decisions needed
for generated artifacts.

Layout-specific details should remain clearly separated from semantic objects.

If a backend needs binary layout metadata, that metadata should be represented
as compiler-owned metadata rather than as source syntax.

---

## Relationship to Runtime Manifest

Schema IR is not the runtime manifest.

The runtime manifest is a generated artifact/backend output described by the
manifest format specification.

Schema IR may contain source metadata, debug metadata, and compiler-only object
identifiers that do not belong in the runtime manifest.

The runtime manifest stores only the compiler-managed state required by its
specification.

---

## Invariants

Schema IR guarantees:

* imports have been removed
* all references are resolved
* no unresolved names remain
* namespaces form an acyclic tree
* records own their fields
* fields own resolved `FieldType` values
* record references target existing `RecordIR` objects
* enum references target existing `EnumIR` objects
* record IDs are nonzero and globally unique
* semantic validation is complete
* source metadata is compiler-only

---

## Example Object Graph

Illustrative Schema IR object graph:

```text
SchemaIR
  root_namespace: breadcrumbs
    namespace: telemetry
      record: Location
        field: latitude
          type: primitive f64
        field: longitude
          type: primitive f64
        field: accuracy
          type: primitive f32
      enum: FixQuality
        value: none
        value: two_dimensional
        value: three_dimensional
```

---

## Future Extension Strategy

Schema IR should be extended by adding protobuf fields and messages without
changing existing field meaning.

Future extensions should preserve the separation between:

* source syntax
* semantic model
* binary layout metadata
* runtime manifest artifacts
* developer-facing debug metadata

---

## Open Questions

Open questions intentionally left for later design work:

* exact compiler ID allocation rules
* exact `FieldType` protobuf field numbering
* whether Schema IR should have a stable serialized debug format
* whether annotations are represented as first-class IR objects
* how generic types should be represented if introduced
* whether services belong in Schema IR
* enum value metadata shape
* IDE metadata shape
* how much layout metadata should be copied into Schema IR versus referenced
  from Layout IR

Source-language array syntax and bounds are defined by
`docs/specifications/schema-language.md` and are not reopened here.
