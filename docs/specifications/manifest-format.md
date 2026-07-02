# Breadcrumbs Manifest Format Specification

## Status

Draft

## Version

0.1

---

## Purpose

The Breadcrumbs manifest is compiler-managed persistent state.

The manifest represents one resolved compilation. Imported schemas are resolved
into a namespace hierarchy before manifest state is written.

The manifest preserves compiler-assigned binary identifiers:

* `recordId` for records
* `fieldIndex` for fields

Schema authors do not edit `recordId` or `fieldIndex` in `.brd` files.

---

## Canonical Format

The canonical human-readable manifest format is protobuf text format (PBTXT).

PBTXT is the format intended to be checked into source control and reviewed by
humans.

Binary protobuf may be generated later as a derived artifact, but it is not the
canonical manifest format.

---

## Information Model

The manifest contains:

* `manifest_version`
* `compiler_version`
* `next_record_id`
* `root_namespace`

`manifest_version` is the version of the manifest file format. It is
independent of any Breadcrumbs schema version.

An empty `compiler_version` means the compiler version was not provided.

Namespaces may be nested recursively.

Records are stored under the namespace that contains them.

A record's Fully Qualified Name (FQN) is derived from the namespace path plus the
record name.

Fields are stored under their containing record.

`schema_hash` is optional compiler metadata used to detect changes to a record
definition. It is not part of record identity or compatibility. An empty
`schema_hash` means the hash was not provided.

Field type is represented as a `FieldType` enum because the manifest is
compiler state, not user-facing schema text. Enum typing is easier for the
compiler to validate and avoids ambiguity between primitive types and
user-defined record or enum types.

`referenced_type_fqn` is empty for primitive field types.

For record and enum fields, `referenced_type_fqn` stores the Fully Qualified
Name of the referenced record or enum.

---

## Identifier Preservation

`record_id` is globally unique within the manifest.

`next_record_id` is monotonically increasing and prevents automatic reuse of
record IDs.

`field_index` is unique only within its containing record.

`next_field_index` is monotonically increasing per record and prevents automatic
reuse of field indexes within that record.

`field_index` is stored as `uint32` in the manifest for protobuf simplicity. The
compiler must enforce that each `fieldIndex` fits in `uint8` for the binary
record format.

`next_field_index` is stored as `uint32`, but must not exceed 256 for valid
records.

---

## Current State Only

The manifest stores current compiler state, not source control history.

The manifest does not store:

* deleted records
* deleted fields
* rename history
* compatibility reports
* diagnostics
* imports
* source file mappings
* generated artifact metadata

Explicit renames are applied during compilation. The manifest stores only the
current resulting mapping.

---

## Example

Illustrative PBTXT snippet:

```text
manifest_version: 1
compiler_version: "0.1"
next_record_id: 3
root_namespace {
  name: "breadcrumbs"
  namespaces {
    name: "telemetry"
    records {
      name: "Location"
      record_id: 1
      next_field_index: 3
      fields { name: "latitude" field_index: 0 type: FIELD_TYPE_F64 }
      fields { name: "longitude" field_index: 1 type: FIELD_TYPE_F64 }
      fields { name: "accuracy" field_index: 2 type: FIELD_TYPE_F32 }
    }
  }
}
```

---

## Compatibility

Exact compatibility rules are defined by the dedicated compatibility rules
specification.
