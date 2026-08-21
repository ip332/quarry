# Quarry Binary Schema (QBS) Specification

## Status

Draft design specification. No QBS encoder, decoder, runtime, or exchange
protocol is implemented by this document.

## Version

QBS format version 1 (proposed).

## Purpose

QBS is a compact, architecture-independent runtime serialization of Quarry's
canonical semantic schema and BRF v2 layout metadata. It is intended to let a
future runtime understand a BRF v2 record without recompiling the source schema
or importing compiler-only Schema IR.

QBS is declarative metadata. It is not executable bytecode, a virtual machine,
a source-schema format, or a replacement for BRF.

The defining relationship is:

```text
Schema source
    -> semantic analysis
    -> canonical BRF Layout IR
    -> QBS serialization
```

QBS SHALL serialize the canonical BRF Layout IR. It SHALL NOT independently
recalculate field offsets, fixed-region sizes, variable classifications, or
nested record sizes.

## Scope

This specification defines:

* the QBS v1 container and byte order;
* schema and format identities;
* canonical table and section ordering;
* record, field, type, enum, and optional name metadata;
* validation and hostile-input requirements;
* forward-compatibility rules;
* representative size estimates and worked examples.

It does not define:

* a QBS compiler backend;
* a QBS parser or generic BRF runtime;
* schema exchange, transport, authentication, or caching protocols;
* schema compatibility policy in full;
* public packed-field syntax;
* unions, variants, fixed arrays, or other schema features not currently
  represented by Quarry's canonical Layout IR.

## Terminology

* **BRF v2** — Quarry's physical binary record representation. See
  [Binary Record Format](binary-record-format.md).
* **Schema IR** — compiler-owned resolved schema data. It is not a runtime
  format. See [Schema IR](../schema-ir.md).
* **BRF Layout IR** — compiler-owned canonical physical layout metadata produced
  by `LayoutComputer`.
* **QBS image** — one serialized QBS container.
* **record_id** — logical record/message identity used in BRF headers. It is not
  a schema identity.
* **schema_id** — identity of one exact canonical semantic/runtime schema.
* **table index** — a zero-based index into a QBS table, never a native pointer.
* **section offset** — a byte offset from the beginning of the QBS image.

## Requirements

### Identity and version separation

REQ-QBS-001: A QBS reader SHALL distinguish `brf_format_version`,
`qbs_format_version`, `record_id`, and `schema_id` as separate values.

REQ-QBS-002: `record_id` SHALL mean only the logical identity of the record or
message type. It SHALL NOT identify a QBS version, exact schema revision,
schema hash, or physical BRF layout.

REQ-QBS-003: A logical record MAY retain its `record_id` while its exact
`schema_id` changes. Changing QBS encoding SHALL NOT require changing
`record_id`.

QBS serializes the `record_id` supplied by the canonical BRF Layout IR; QBS
does not allocate, renumber, or otherwise derive record IDs. The current
compiler's initial Layout IR allocation is position-derived from canonical FQN
order. Consequently, inserting a new record whose FQN sorts before an existing
record can currently change the existing assigned IDs. That is a known
implementation limitation, not a QBS guarantee.

Stable preservation of logical `record_id` values across compatible schema
evolution is an architectural prerequisite for future schema exchange and
protocol-evolution work. A future persistent allocation mechanism must address
that prerequisite before those features rely on IDs surviving recompilation.

REQ-QBS-004: QBS v1 SHALL describe BRF v2. A QBS v1 image SHALL identify this
BRF contract in its canonical schema-identity input and SHALL NOT be used to
interpret a different BRF format without an explicitly defined future QBS
version or BRF binding.

### Runtime meaning

REQ-QBS-005: QBS SHALL contain enough information to locate every declared BRF
field or its fixed-size variable descriptor from the beginning of the complete
BRF record.

REQ-QBS-006: QBS SHALL preserve `byte_offset`, `bit_offset`, and `bit_width`
for every field. Current byte-aligned fields SHALL encode `bit_offset = 0`.

REQ-QBS-007: QBS SHALL preserve the distinction between fixed storage, an
inline fixed-size nested record, and an 8-byte variable descriptor.

REQ-QBS-008: QBS SHALL preserve all schema constraints currently required by
BRF v2 validation, including string/bytes bounds, array bounds, enum allowed
values, nested record references, and encoded widths.

### Encoding independence

REQ-QBS-009: All QBS integers SHALL use explicitly specified widths and
big-endian byte order. Native C/C++ structure layout, host endianness, pointer
width, and alignment SHALL NOT affect an image.

REQ-QBS-010: A QBS image generated from identical canonical Layout IR and
identity metadata SHALL be byte-identical across languages, hosts, and
compiler implementations.

## Versioning and identities

### BRF format version

BRF v2 remains the physical record format. In QBS field metadata:

* a fixed field's `byte_offset` locates encoded field bytes;
* an inline fixed nested field's `byte_offset` locates the child BRF v2 header;
* a variable field's `byte_offset` locates the parent 8-byte descriptor;
* variable descriptor contents are `{uint32 data_offset, uint32 byte_length}`;
* all BRF offsets are relative to the beginning of the containing complete BRF
  record;
* offsets inside a nested child remain relative to that child record.

QBS does not change BRF bytes or make the schema image part of a BRF record.

### QBS format version

`qbs_format_version` identifies the physical QBS container and table encoding.
QBS v1 is the format specified here. A future QBS version may change table
encodings while preserving the same schema identity for the same canonical
runtime schema.

### schema_id

`schema_id` SHALL identify the canonical semantic/runtime schema, not the source
file bytes and not the serialized QBS bytes.

The canonical identity input for QBS v1 SHALL contain, in canonical order:

1. a domain tag identifying Quarry QBS schema identity;
2. the BRF layout contract identifier (`BRF v2`);
3. every record's logical FQN and `record_id`;
4. every record's field indexes in canonical field order;
5. each field's canonical type, encoded width, bounds, storage kind, location,
   presence bit, slot size, and referenced type identity;
6. every enum's logical identity, encoded width, and sorted allowed numeric
   values;
7. array element types and bounds;
8. nested record references.

Field names, optional reflective record/field/enum names, enum value names,
comments, source locations, formatting, import paths, and other debug metadata
SHALL NOT affect `schema_id`. They are not runtime identity and a field rename
that preserves its field index and type is already defined as a compatible
binary change by the current evolution design. Record and enum FQNs remain in
the identity input because they identify the logical declarations being
described.

The canonical identity input SHALL use length-delimited UTF-8 strings and
explicit-width big-endian integers; it SHALL not be the in-memory protobuf or
native object representation.

The mandatory Identity String Section carries the record and enum FQNs needed
by an independent reader to reconstruct this identity input. ISS bytes and
their physical offsets are not themselves hashed; the referenced identity
strings are incorporated as semantic strings in the same canonical identity
encoding used by the compiler.

QBS v1 reserves an 8-bit `schema_id_algorithm` value in the header. Algorithm
`1` is SHA-256 truncated to its first 128 bits over the canonical identity
input described above. A reader SHALL reject an unknown algorithm rather than
silently comparing incompatible IDs.

The digest algorithm and QBS format version are independent: changing the QBS
container does not change the digest input, while changing the canonical
runtime schema does.

## QBS v1 container

### Byte order and integer rules

All multi-byte QBS integers are unsigned unless explicitly stated otherwise and
are encoded big-endian. There is no native padding between fields. All offsets
are byte offsets from image start unless a section explicitly says otherwise.

QBS v1 uses 32-bit section offsets, lengths, and total size. A conforming
implementation SHALL reject an image larger than `UINT32_MAX` and SHALL apply a
smaller configured implementation limit when appropriate for its target.

### Header

The QBS v1 header is exactly 40 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `QBS\0` (`51 42 53 00`) |
| 4 | 1 | `qbs_format_version` | `1` |
| 5 | 1 | `flags` | Defined bits only; v1 requires zero |
| 6 | 2 | `header_size` | `40` |
| 8 | 1 | `schema_id_algorithm` | `1` for the assigned 128-bit digest |
| 9 | 1 | `identity_offset_width` | `1`, `2`, or `4`; global ISS offset width |
| 10 | 2 | `schema_id_size` | `16` |
| 12 | 16 | `schema_id` | 128-bit schema identity |
| 28 | 2 | `section_count` | Number of section directory entries |
| 30 | 2 | reserved | Must be zero |
| 32 | 4 | `section_directory_offset` | Usually `40`; absolute image offset |
| 36 | 4 | `total_size` | Exact image length in bytes |

QBS v1 SHALL use `flags = 0`, `header_size = 40`, `schema_id_size = 16`, and a
section directory beginning at or after the header. `identity_offset_width`
shall be `1` when the ISS payload size is at most 256 bytes, `2` when it is
greater than 256 and at most 65,536 bytes, and `4` otherwise. An empty ISS
uses width `1`. Header extensions are not implicitly skippable; a future
format version must define them.

### Section directory

Each section directory entry is exactly 12 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | `section_kind` |
| 2 | 2 | `section_flags` |
| 4 | 4 | `section_offset` |
| 8 | 4 | `section_size` |

Section offsets and sizes SHALL be checked before any table access. Required
sections SHALL appear exactly once. Unknown sections are governed by the
forward-compatibility rules below.

`section_flags` bit 0 marks an unknown section as ignorable after its bounds
have been validated. All other v1 section flag bits are reserved and zero.

QBS v1 section kinds are:

| Kind | Section | Required |
|---:|---|---|
| 1 | record table | yes |
| 2 | field table | yes |
| 3 | type table | yes |
| 4 | enum table | no, unless an enum type exists |
| 5 | enum values | no, unless an enum exists |
| 6 | identity string section | yes |
| 7 | string table | no; only reflective images need it |
| 0x8000–0xFFFF | extension | defined by a future specification |

Sections SHALL be non-overlapping, lie after the header and directory, and be
listed in increasing `section_kind` order. An empty optional section is omitted
rather than represented by a zero-length directory entry.

### String table

When section kind 7 is present, it has exactly one normative encoding:

```text
StringTable {
    uint32 string_count
    uint32 offsets[string_count + 1]
    byte   data[]
}
```

The offset array and `data` are contained entirely within the section. Each
offset is relative to the beginning of `data`, not to the QBS image or section.
String `i` is the byte range `data[offsets[i] : offsets[i + 1]]`.

`offsets[0]` SHALL be zero; offsets SHALL be monotonically non-decreasing; and
`offsets[string_count]` SHALL equal `data_size`. Empty strings are allowed and
have equal adjacent offsets. Strings have no NUL terminator, and embedded
U+0000 is allowed. Every string SHALL be valid UTF-8. The count, offset array,
data size, and all additions/multiplications SHALL be checked before access.

Strings SHALL be ordered lexicographically by their UTF-8 byte sequences and
identical strings SHALL be deduplicated. A string index is a zero-based
`uint16` index into this canonical table. `0xFFFF` means no string reference.
If any descriptor contains a string index other than `0xFFFF`, section kind 7
SHALL be present and the index SHALL be in range. A minimal image has no string
section and all name indexes are `0xFFFF`.

## Canonical table organization

The record table is ordered by canonical record FQN using the same ordering as
the BRF Layout IR. The field table is a concatenation of record field lists in
record-table order; fields within a record are in schema declaration/field-index
order. `field_start` and `field_count` in a record descriptor address this
contiguous range.

The identity string section (ISS) is a mandatory structural section (kind 6).
It is one contiguous UTF-8 byte blob containing every record and enum FQN,
each followed by one `0x00` byte. Identities are non-empty, contain no NUL,
are valid UTF-8, are valid canonical FQNs, and are strictly lexicographically
ordered and deduplicated. ISS offsets are relative to the first payload byte
and must point to string starts. The final identity is terminated by the
section's final byte; an empty ISS is permitted only for an empty schema.

Record and enum descriptors carry an ISS identity offset using the global
`identity_offset_width`; no identity sentinel exists.

The type table is ordered by the canonical type-identity key defined below.
Identical identities are interned once. This is one canonical order; source
declaration order and first-use order are not alternatives.

The key is a byte string, compared lexicographically as unsigned bytes. It is
encoded in this order:

1. one byte type code;
2. one byte fixed/variable classification (`0` fixed, `1` variable);
3. three big-endian unsigned 32-bit values: encoded width, maximum element
   count, and maximum byte count;
4. a big-endian unsigned 32-bit length followed by the referenced semantic
   identity: the referenced record FQN for record types, the referenced enum
   FQN for enum types, or an empty string for other types;
5. a big-endian unsigned 32-bit enum-value count followed by each enum value as
   an unsigned 64-bit big-endian integer, sorted in strictly ascending order;
6. one byte indicating whether an element type is present (`0` or `1`),
   followed recursively by that element type's identity key when present.

The recursive element identity is semantic and never a QBS table index. The
key therefore includes all structural type properties used by QBS v1,
including bounds, referenced record/enum identity, enum values, and nested
element types. Record and enum references in emitted descriptors use their
canonical table indexes only after sorting and interning by this key.

The enum table is ordered by enum FQN. Each enum's allowed numeric values are
sorted in strictly increasing numeric order in the enum-value table. The string
table, when present, is ordered lexicographically by each string's UTF-8 byte
sequence. Identical strings are deduplicated and appear once. This ordering is
independent of traversal, table references, and serializer implementation.

## Record table

Each record descriptor is exactly 28 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `record_id` |
| 4 | 4 | `field_start` — field-table index |
| 8 | 2 | `field_count` |
| 10 | 2 | `record_flags` |
| 12 | 4 | `presence_bitmap_size` |
| 16 | 4 | `fixed_region_size` |
| 20 | 4 | `complete_fixed_record_size` |
| 24 | `identity_offset_width` | `identity_offset` into ISS |
| `24 + identity_offset_width` | 2 | `name_string_index` or `0xFFFF` |
| `26 + identity_offset_width` | 2 | reserved, zero |

The record descriptor size is `28 + identity_offset_width` bytes.

`record_flags` bit 0 means the record is variable-size. All other v1 bits are
reserved and zero. A fixed-size record has bit 0 clear and a nonzero complete
fixed size equal to `16 + fixed_region_size`; a variable-size record has bit 0
set and `complete_fixed_record_size = 0`.

The record descriptor carries the optional local record name through
`name_string_index`. The record's FQN is part of schema identity and is not
duplicated in the structural descriptor. `presence_bitmap_size` and
`fixed_region_size` are copied from Layout IR; `fixed_region_size` includes the
presence bitmap and is the complete fixed span immediately after the BRF
header.

## Field table

Each field descriptor is exactly 28 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | `field_index` |
| 2 | 2 | `field_flags` |
| 4 | 4 | `byte_offset` from containing BRF record start |
| 8 | 2 | `bit_offset` |
| 10 | 4 | `bit_width` |
| 14 | 2 | `type_index` |
| 16 | 2 | `presence_bit_index` |
| 18 | 2 | reserved, zero |
| 20 | 4 | `slot_size` |
| 24 | 2 | `name_string_index` or `0xFFFF` |
| 26 | 2 | reserved, zero |

`field_flags` v1 definitions:

| Bits | Meaning |
|---:|---|
| 0–1 | storage: `0` fixed, `1` inline fixed nested, `2` variable descriptor |
| 2 | descriptor kind: set for `{data_offset, byte_length}` |
| 3–15 | reserved, zero |

The descriptor-kind bit SHALL be clear for fixed and inline nested fields and
set for variable fields. A variable field SHALL have `slot_size = 8`; a fixed
or inline field SHALL have its canonical Layout IR slot size.
`name_string_index` is `0xFFFF` in a minimal image or an index into the
canonical string table in a reflective image.

The field table explicitly carries `presence_bit_index` even though current
fields are declaration ordered. This prevents a future compatibility-preserved
field index from being confused with its presence-bit position.

## Type table and type codes

Each type descriptor is exactly 16 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | `type_code` |
| 1 | 1 | `type_flags` |
| 2 | 2 | `encoded_width` |
| 4 | 2 | `reference` |
| 6 | 2 | reserved, zero |
| 8 | 4 | `max_elements` |
| 12 | 4 | `max_bytes` |

QBS v1 type codes are:

| Code | Type |
|---:|---|
| 1 | bool |
| 2 | signed 8-bit integer |
| 3 | unsigned 8-bit integer |
| 4 | signed 16-bit integer |
| 5 | unsigned 16-bit integer |
| 6 | signed 32-bit integer |
| 7 | unsigned 32-bit integer |
| 8 | signed 64-bit integer |
| 9 | unsigned 64-bit integer |
| 10 | float32 |
| 11 | float64 |
| 12 | enum |
| 13 | string |
| 14 | bytes |
| 15 | record reference |
| 16 | array |

Codes `0` and `17–127` are reserved and invalid in QBS v1. Codes `128–255`
are extension codes; a v1 reader SHALL reject them unless a recognized
extension section defines them.

`type_flags` bit 0 means fixed-size, bit 1 means variable-size, and all other
bits are reserved. Exactly one of bits 0 and 1 SHALL be set for every type.
`encoded_width` is the canonical fixed width for fixed scalars/enums and zero
for variable-width types. For arrays it is the width of the complete array
object only when that width is schema-determined; otherwise it is zero.

For enum types, `reference` is an enum-table index. For record references,
`reference` is a record-table index. For arrays, `reference` is a type-table
index for the element type. For scalar, string, and bytes types, `reference`
is zero. `max_elements` is meaningful for arrays and zero otherwise;
`max_bytes` is meaningful for strings/bytes and is also zero on the array type
descriptor because a string/bytes array obtains its per-element bound from its
element type.

## Enum representation

Each enum descriptor has a 16-byte base layout plus the global
`identity_offset_width` bytes described below:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | `encoded_width` |
| 2 | 2 | `enum_flags`, reserved zero in v1 |
| 4 | 4 | `value_start` in enum-value table |
| 8 | 4 | `value_count` |
| 12 | `identity_offset_width` | `identity_offset` into ISS |
| `12 + identity_offset_width` | 2 | `name_string_index` or `0xFFFF` |
| `14 + identity_offset_width` | 2 | reserved, zero |

The enum descriptor size is `16 + identity_offset_width` bytes.

Each enum value is one unsigned 64-bit big-endian integer. QBS v1 uses the
nonnegative enum values currently accepted by BRF v2. Values SHALL be sorted,
unique, and within the declared encoded width. The generic validator SHALL
reject an encoded enum number not present in this table.

The shared table avoids repeating an allowed-value set when an enum is used by
many fields. Delta encoding and range compression were considered but are not
part of v1; direct values keep validation simple and make malformed-input
checking straightforward.

## Arrays

An array type descriptor references another type descriptor and carries
`max_elements`. QBS does not encode a runtime element count; BRF v2 carries
that count in the variable array object.

The referenced element type determines the framing class:

* fixed-width primitive or enum: varuint count followed by tightly packed
  elements;
* variable-width string or bytes: varuint count followed by each element's
  varuint byte length and raw bytes;
* fixed-size record: varuint count followed by complete fixed-size child BRF
  records with no per-element length;
* variable-size record: varuint count followed by a varuint byte length and a
  complete child BRF record for each element.

The QBS type graph SHALL preserve the element type, its encoded width or
classification, its enum reference where applicable, its nested record
reference where applicable, and its bounds. A QBS reader SHALL not infer array
framing from a source-language type name.

## Nested records

Record types reference the record table by index. A fixed-size nested record is
inline as a complete BRF v2 child record, including its 16-byte header and
presence information. A variable-size nested record is represented in the
parent by the variable descriptor and its complete child record is in the
parent variable tail.

The parent field's storage flag and the referenced record descriptor together
determine which representation is valid. A parent presence bit SHALL be
checked before an inline absent slot is interpreted as a child record. When
present, the child record's version, `record_id`, sizes, fields, and nested
references SHALL be validated according to BRF v2.

Recursive record references are allowed only where the BRF Layout IR permits
them. A QBS validator SHALL detect cycles in by-value fixed dependencies and
shall use an explicit work stack or an equivalent bounded traversal for all
externally controlled graph walks.

## Bit-precision provision

QBS v1 carries `byte_offset`, `bit_offset`, and `bit_width` in every field
descriptor. Current generated layouts use byte-aligned fields. The v1 physical
format reserves the representation needed for future packed fields but does
not define public packed-field syntax or new BRF bit-spanning rules.

`bit_offset` is relative to the field's containing byte location and
`bit_width` is the logical field width. Variable descriptors and variable
payloads remain byte-aligned. Bit numbering and byte endianness remain governed
by BRF v2; QBS does not create a second bit-order convention.

## Optional names and reflection

QBS v1 supports two profiles using the same container:

* **minimal** — no string section; all `name_string_index` values are
  `0xFFFF`;
* **reflective** — a string section contains optional record, field, and enum
  names.

The structural tables are identical in both profiles. A runtime that only
needs lookup, validation, decode, or forwarding MAY reject or discard the
optional string section after checking its bounds. A tooling or inspection
runtime MAY use it for diagnostics and human-readable output.

Names are descriptive metadata, not identity. Their presence, contents, and
string-table indexes SHALL NOT change `schema_id`. String contents SHALL be
valid UTF-8, SHALL be length-bounded by the image limit, and SHALL not be
interpreted as code.

## Validation Rules

REQ-QBS-011: A QBS reader SHALL validate the complete header before using any
section offset, table count, or reference.

REQ-QBS-012: A reader SHALL check every `offset + size` operation with checked
arithmetic and SHALL reject a section or table range outside `total_size`.

REQ-QBS-013: A reader SHALL reject unknown required flags, duplicate required
sections, overlapping sections, truncated descriptors, invalid table strides,
and a `total_size` different from the received image length.

REQ-QBS-014: A reader SHALL validate record-table indexes, field-table ranges,
type indexes, enum indexes, record references, string offsets, and enum-value
ranges before dereferencing them.

REQ-QBS-014a: If a string section is present, a reader SHALL validate its
`string_count`, offset-table size, checked data bounds, `offsets[0] = 0`,
monotonic offsets, and final offset equal to `data_size`. Each string SHALL be
valid UTF-8, and string entries SHALL be lexicographically ordered and
deduplicated.

REQ-QBS-014b: A `name_string_index` of `0xFFFF` SHALL mean no name. Any other
name index SHALL require a valid string section and an in-range string index.
Minimal images SHALL use only the sentinel.

REQ-QBS-015: A reader SHALL validate field locations against the referenced BRF
v2 record: field slots SHALL lie in the fixed region, storage and slot size
SHALL agree, presence bits SHALL be within the bitmap, and fields SHALL be
non-overlapping and declaration ordered.

REQ-QBS-016: A reader SHALL validate fixed/variable classification recursively.
An array or nested record whose referenced type is variable SHALL not be
described as fixed.

REQ-QBS-017: A reader SHALL validate enum widths, sorted unique allowed values,
array bounds, string/bytes bounds, and the consistency of encoded widths.

REQ-QBS-018: A reader SHALL reject unsupported type codes and unsupported
extension requirements before attempting to interpret a BRF record.

REQ-QBS-019: A reader SHALL detect prohibited by-value cycles, excessive graph
depth, excessive record/field/object counts, and allocation sizes above its
configured limits without using unbounded native recursion.

REQ-QBS-020: Validation SHALL complete before QBS-derived offsets or indexes
are used to access externally received BRF data.

Additional BRF validation remains required after QBS validation, including BRF
header checks, descriptor bounds, variable-tail ordering, presence semantics,
array framing, nested record sizes, and payload content validation.

### Recommended implementation limits

The format's 32-bit fields are not a requirement to allocate 4 GiB. Embedded
implementations SHOULD configure substantially smaller limits for image size,
section size, records, fields, enum values, string bytes, and nesting depth.
Those limits SHOULD be checked before allocation and exposed as diagnostics.

## Forward compatibility

QBS v1 readers SHALL reject a newer `qbs_format_version` because the header and
table interpretation may be incompatible. They MAY recognize a future version
through an explicitly negotiated capability outside this format.

Unknown flag bits in the v1 header or required sections are errors. Unknown
optional sections are skippable only when their section flags explicitly mark
them as ignorable and their bounds have been validated. An unknown type code or
unknown extension required by a field is never safely skippable: the reader
SHALL reject the image.

Future extensions SHALL use new section kinds or a new QBS format version and
SHALL define whether they affect the canonical schema identity. They SHALL NOT
reinterpret an existing v1 field descriptor in place.

## Security and hostile-input model

QBS may eventually be received from another node or untrusted storage. A
validator SHALL therefore:

* treat all bytes, counts, offsets, names, and references as untrusted;
* use checked arithmetic for every multiplication and addition;
* validate before indexing or allocating;
* avoid native pointers, function pointers, opcodes, and executable content;
* use explicit work stacks for schema graph traversal;
* enforce image, table, string, enum, and nesting limits;
* prevent duplicate/overlapping sections and invalid cross-references;
* avoid trusting names for identity or authorization decisions.

QBS v1 contains no instructions and does not grant a schema image permission to
execute code.

## Lookup model

The minimal lookup path is intentionally simple:

1. locate a record by `record_id` in the canonical record table;
2. locate a field by scanning that record's contiguous field range, or use a
   runtime-built index;
3. use the field's absolute `byte_offset` and storage flags to access BRF v2.

The serialized format does not require a hash table or a second lookup index.
For small embedded schemas a linear scan is bounded by the record's declared
field count. Larger runtimes MAY build an in-memory sorted or hashed index after
validation without changing QBS bytes.

Record-table lookup by `record_id` MAY similarly use binary search or a runtime
index. The canonical table remains FQN ordered, not record-ID ordered, so
record identity changes do not silently alter serialization order.

## Compactness study

The following estimates use the proposed fixed widths:

* 40-byte header;
* 12-byte section directory entry;
* `28 + identity_offset_width`-byte record descriptor;
* 28-byte field descriptor;
* 16-byte type descriptor;
* `16 + identity_offset_width`-byte enum descriptor;
* 8 bytes per enum value;
* string bytes plus 4-byte section offsets when reflective names are enabled.

They exclude alignment padding because QBS tables are packed byte sequences.

| Representative schema | Records | Fields | Unique types | Enum values | Minimal estimate |
|---|---:|---:|---:|---:|---:|
| Tiny, 3 scalar fields | 1 | 3 | 3 | 0 | ~232 bytes |
| `Example` below | 1 | 4 | 4 | 0 | ~301 bytes |
| Two-level nested records | 2 | 6 | 5 | 0 | ~380 bytes |
| Enum-heavy, one shared enum | 1 | 10 | 2 | 5 | ~480 bytes |
| Array-heavy, five array fields | 1 | 5 | 8 | 0 | ~372 bytes |
| Larger schema, 10 records/60 fields/6 enums/50 values | 10 | 60 | 20 | 50 | ~2.8 KiB |

The estimate includes four to seven required sections depending on whether enum
tables are present and whether reflective names are enabled. Reflective names add the UTF-8 string bytes and do not
change structural descriptor sizes; the examples above would typically gain
roughly 30–300 bytes of names, plus the string-table framing and one section
directory entry. The main fixed cost is the 28-byte field
descriptor. It buys direct schema-known BRF offsets, presence positions, slot
sizes, and future bit locations without backend-specific decoding.

A future implementation should measure actual schemas before changing widths.
QBS v1 intentionally uses 32-bit image offsets and 16-bit table indexes rather
than introducing separate QBS16/QBS32 profiles. The format can support up to
65,535 records, fields per referenced table, and type/enum references while
keeping total image bounds independently 32-bit.

## Alternative representations considered

### A. Fixed-size field descriptors

One fixed descriptor per field makes random access, bounds checks, independent
parsing, and generated/runtime parity straightforward. It costs space for
fields that do not use every auxiliary concept, and widening a descriptor later
requires a format version or extension.

### B. Variable-length tagged descriptors

Tagged descriptors can omit unused fields and may be smaller for simple
schemas. They require more branches, more bounds checks, and either a field
index or an auxiliary offset table for random access. Malformed tags and
unknown required tags also complicate independent implementations and embedded
validation.

### C. Hybrid fixed tables with referenced extensions

The proposed design uses a hybrid: fixed-size 16-bit-word-oriented common
descriptors, compact table indexes for complex types, and optional sections for
enum values and names. Common field metadata remains directly indexable while
arrays, enums, and reflection data are stored once and referenced.

The design is inspired by the useful property of compact word-oriented schema
formats—small common descriptors—not by any particular historical syntax or
native offset convention. QBS offsets are BRF record-relative and are never C
struct offsets.

**Recommendation: C. Adopt the hybrid descriptor model.** It is the best
balance for embedded validation, direct field lookup, shared enum metadata,
future bit offsets, and a format that an independent implementation can parse
without reproducing compiler decisions.

## Worked examples

### `Example` record

Given:

```text
record Example {
    uint32 timestamp;
    string name;
    uint16 state;
    uint16[] samples;
}
```

The canonical BRF v2 layout is:

```text
header                   0..15
presence bitmap          16, size 1
timestamp                17, slot 4
name descriptor          21, slot 8
state                    29, slot 2
samples descriptor       31, slot 8
fixed_region_size        23
complete fixed size      not applicable (variable record)
```

The canonical QBS table assignment is:

```text
record table index 0: Example, record_id 1
type table index 0:  uint16, fixed, width 2
type table index 1:  uint32, fixed, width 4
type table index 2:  string, variable, max_bytes = schema bound
type table index 3:  array, variable, element type index 0,
                      max_elements = schema bound
```

The record descriptor fields are:

```text
record_id                  1
field_start                0
field_count                4
record_flags               variable
presence_bitmap_size       1
fixed_region_size          23
complete_fixed_record_size 0
name_string_index          0xFFFF (minimal profile)
```

The four 28-byte field descriptors are:

| Field | Index | Flags | Byte offset | Bit offset/width | Type | Presence | Slot |
|---|---:|---|---:|---|---:|---:|---:|
| timestamp | 0 | fixed | 17 | 0 / 32 | 1 | 0 | 4 |
| name | 1 | descriptor | 21 | 0 / 0 | 2 | 1 | 8 |
| state | 2 | fixed | 29 | 0 / 16 | 0 | 2 | 2 |
| samples | 3 | descriptor | 31 | 0 / 0 | 3 | 3 | 8 |

The mandatory ISS is the payload `Example\0`, so its size is 8 bytes, its
offset width is 1, and the record's identity offset is 0. For a minimal image,
every `name_string_index` is `0xFFFF`. The fixed table sizes are:

```text
header                         40
section directory (4 sections) 48
record table                    29
field table                    112
type table                      64
identity string section          8
-----------------------------------
minimum structural image       301 bytes
```

This minimal image is 301 bytes before optional names. A reflective image adds
one section-directory entry (12 bytes) and a string table, for 373 bytes. With the five names
`Example`, `timestamp`, `name`, `state`, and `samples`, the string table is:

```text
string_count       5
offsets            0, 7, 11, 18, 23, 32
data               Example | name | samples | state | timestamp
```

The strings are shown separated for readability; the actual `data` bytes are
concatenated UTF-8 bytes with no separators or NUL terminators. This adds
`4 + 6 * 4 + 32 = 60` bytes for the string section and 12 bytes for its
directory entry, making the reflective estimate 373 bytes. The name indexes
are assigned by the canonical lexicographic order, not by field traversal.

The runtime locates `state` by finding record-table entry 0, reading field-table
entry `field_start + 2`, and adding `29` to the BRF record address. It locates
`name` at `record + 21`, reads the big-endian descriptor there, then follows
that descriptor's record-relative `data_offset` and `byte_length` into the
variable tail.

For an implementation that includes the optional string table, the structural
bytes remain identical and only section-directory/string bytes are added.

### Nested record example

```text
record Location {
    int32 latitude;
    int32 longitude;
}

record Packet {
    Location location;
    string note;
}
```

`Location` is fixed-size, so its QBS record descriptor has a complete fixed
size and `Packet.location` has storage `inline fixed nested`, a byte offset to
the child header, and a type index whose reference is Location's record-table
index. `Packet.note` has storage `variable descriptor`. QBS contains one record
descriptor per type and does not duplicate Location's fields inside Packet.

If Location later becomes variable-size, the canonical Layout IR changes the
Packet field storage to `variable descriptor`; the schema identity changes,
while `record_id` remains a logical identity. The BRF bytes and QBS metadata
must then be interpreted under the new exact `schema_id`.

### Enum example

```text
enum State {
    Off = 0
    On  = 1
}

record Device {
    State state;
    State[] history;
}
```

The enum table contains one descriptor with the canonical encoded width and a
value range of two entries in the enum-value table: `0`, then `1`. The scalar
State type and the array element type both reference that one enum descriptor.
The validator accepts only `00` and `01` for the scalar encoded width and
applies the same allowed-value table to every array element.

## Schema evolution implications

QBS describes one exact schema; it is not itself a compatibility negotiation
mechanism. Adding a field generally changes the canonical schema input and
therefore `schema_id`, even when existing field locations remain unchanged.
Removing, retyping, or changing fixed/variable classification likewise changes
the exact identity. A compatible reader may still choose to support both IDs
under an external evolution policy.

Keeping field indexes and canonical layout metadata stable makes direct access
and compatibility analysis easier, but it does not make a QBS reader accept
unknown field types or changed validation rules automatically. Those rules
belong to a future schema-evolution specification.

## Future schema exchange (non-normative)

Transport is out of scope. A future exchange system may use a flow such as:

```text
node receives BRF data with unknown schema_id
        -> requests QBS(schema_id)
        -> validates and caches QBS
        -> uses QBS for BRF structural interpretation
```

Authentication, negotiation, discovery, retries, cache policy, and transport
framing are deliberately not defined here.

## Open Questions

1. Define and implement stable persistent `record_id` allocation; the current
   position-derived Layout IR allocation is insufficient for schema exchange.
2. Confirm the production maximums for QBS image size and 16-bit table indexes
   using actual schemas and embedded targets.
3. Measure whether a future optional compressed enum-value section materially
   benefits real schemas without weakening validation simplicity.
4. Define the external compatibility policy for accepting more than one
   `schema_id` for a logical `record_id`.

## Proposed implementation sequence

The following work is intentionally outside this specification-only PR:

```text
PR 8   QBS serialization model / compiler Layout IR adapter
PR 9   QBS backend serializer
PR 10  QBS parser and validator
PR 11  generic record/field lookup and inspection runtime
PR 12  generic BRF decode/print support
PR 13  ROM, RAM, size, and performance evaluation
PR 14  schema_id implementation and canonical identity tests
later  stable record-ID allocation, schema exchange, and protocol evolution
```

The QBS backend SHALL consume the existing canonical BRF Layout IR. It SHALL
not introduce a second offset, width, nesting, or fixed/variable calculation
path.
