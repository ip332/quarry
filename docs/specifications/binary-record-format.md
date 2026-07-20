# Quarry Binary Record Format Specification

## Status

Draft

## Version

0.1

## Purpose

This document defines the binary representation of Quarry records and
field payloads.

The binary record format defines how record headers, field directories,
payloads, nested records, scalar values, arrays, strings, bytes, and enum values
are encoded.

This document does not define schema syntax, transport protocols, application
semantics, or cloud APIs.

---

## Record Layout

A top-level record is encoded as:

```text
Record Header + Field Directory + Payload
```

A nested record field payload contains a complete embedded record:

```text
Record Header + Field Directory + Payload
```

The parent Field Directory entry length bounds the complete embedded record
byte sequence.

There is no footer or trailer in v0.1.

---

## Record Header v0.1

```text
headerVersion   uint8
flags           uint8
directoryEntryCount uint8
reserved0       uint8
recordId        uint32
reserved1       uint32
payloadLength   uint32
```

The Record Header is fixed-size in v0.1.

`headerVersion` SHALL be `1` for Binary Record Format v0.1.

Header fields SHALL NOT use varint encoding.

All flag bits SHALL be zero in v0.1.

The reserved header fields SHALL be zero.

`directoryEntryCount` is the number of present fields encoded in the Field
Directory.

`directoryEntryCount` is encoded as `uint8`.

`payloadLength` is the number of bytes after the Record Header, including the
Field Directory and Payload.

The Record Header does not contain:

* timestamps
* deviceId
* checksum or CRC
* compression metadata
* encryption metadata
* footer or trailer metadata

---

## recordId and Compatibility

A `recordId` is the compiler-generated binary identifier of a record. It
identifies a compatible evolution line, not a single schema file revision.

Compatible schema evolution SHALL keep the same `recordId`.

Incompatible layout or semantic changes SHALL require a new `recordId`.

Backward compatibility is provided by schema evolution rules and generated
readers, not by a record version field in the Record Header.

Older readers MAY read the fields they know and ignore unknown Field Directory
entries and their referenced payload bytes when compatibility rules allow.

Within the same `recordId`, existing `fieldIndex`, type, encoding, and meaning
associations SHALL NOT change.

---

## Record Body Layout

The record body begins immediately after the Record Header for top-level
records.

The record body is encoded as:

```text
Field Directory
Payload
```

A field is encoded only when present.

Absence means the application did not set the field through the generated API.

Each declared field has a compiler-generated `fieldIndex`.

The `fieldIndex` identifies a present field in the Field Directory.

Generated setters update record bytes directly. The binary record is the primary
data structure.

Nested records use the same complete record layout as top-level records when
they are encoded as field payloads.

---

## Field Index

Each declared field has a compiler-generated `fieldIndex`.

The `fieldIndex`:

* is assigned by the Schema Compiler
* is hidden from schema authors
* is not a logical identifier for the field
* identifies a present field in the Field Directory

`fieldIndex` is encoded as `uint8`.

A record may contain at most 256 declared fields.

Records needing more than 256 fields should be decomposed into smaller records
using composition.

---

## Field Directory

The Field Directory contains one entry per present field.

Only present fields appear in the Field Directory.

Field absence is represented by the absence of a directory entry.

Each directory entry is encoded as:

```text
fieldIndex  : u8
fieldOffset : varuint
fieldLength : varuint
```

`fieldIndex` identifies the field value.

`fieldOffset` specifies the byte offset of the field value relative to the start
of the Payload.

`fieldLength` specifies the number of payload bytes occupied by that field
value.

`fieldOffset` and `fieldLength` are encoded as unsigned variable-length
integers.

Zero-length field values are valid. For example, an empty string or empty bytes
field may have `fieldLength` equal to zero.

Field absence is not represented by a zero `fieldLength`.

Directory entries SHALL be sorted by `fieldIndex`.

`fieldOffset` allows the Field Directory to remain sorted while payload values
are stored independently of `fieldIndex` order.

### Directory Validation

A valid record SHALL satisfy all of the following:

* every `fieldIndex` appears at most once
* `fieldIndex` values are strictly increasing
* `fieldOffset` is relative to the beginning of the Payload
* `fieldLength` is encoded as `varuint`
* `fieldOffset + fieldLength` SHALL NOT exceed the Payload size
* payload ranges referenced by directory entries SHALL NOT overlap

A decoder SHALL reject records that violate these rules.

---

## Payload

The Payload contains field value bytes referenced by Field Directory entries.

Payload order is not semantically significant.

Each field value is encoded according to its declared field type.

Payload values may be stored in append order or another implementation-defined
order.

No field value bytes are encoded for absent fields.

---

## Decoding

Decoders read the Field Directory before decoding field values.

For top-level v0.1 records, decoders determine the Field Directory length by
reading exactly `directoryEntryCount` entries. Each entry contains one
`fieldIndex` byte followed by one `fieldOffset` varuint and one `fieldLength`
varuint. The Payload begins immediately after the last declared Field Directory
entry and extends to the end of the Record Header's `payloadLength`.

Decoders use `fieldOffset` and `fieldLength` to locate each field value within
the Payload.

Decoders identify fields using `fieldIndex`.

For known `fieldIndex` values, decoders use the declared field type to decode
the corresponding payload bytes.

Unknown `fieldIndex` values are ignored. Decoders skip the corresponding payload
bytes using `fieldOffset` and `fieldLength`.

Unknown fields do not affect decoding of known fields.

The generated C++ decoder materializes generated record values for the same
subset as the generated encoder: `bool`, fixed-width signed and unsigned
integers, `float32`, `float64`, `string`, `bytes`, enum references whose
declared values are all non-negative, supported arrays, and nested record
fields. A present known field with a type outside that subset causes generated
decoding to fail. An absent unsupported known field does not affect decoding.

---

## Nested Records

A nested record field payload is a complete Binary Record Format record,
including its own 16-byte Record Header, Field Directory, and Payload.

The parent Field Directory `fieldLength` for a nested record field SHALL equal
the complete embedded record byte count.

Nested `recordId` values are encoded in the embedded Record Header. Generated
decoders verify the embedded `recordId` by calling the referenced record's
generated decoder. A wrong nested record type causes parent decoding to fail.

Nested records use the same `headerVersion`, `flags`, reserved-field, and
`payloadLength` validation rules as top-level records. Unsupported nested
versions, nonzero flags, nonzero reserved fields, malformed nested payload
lengths, and trailing bytes inside the parent field span cause parent decoding
to fail.

An absent nested field has no parent Field Directory entry. A present empty
nested record has a parent entry whose payload is a complete embedded record
with zero nested fields. These states are distinct.

Unknown fields inside nested records are ignored by generated decoders after
normal structural validation. Unknown fields are not preserved for re-encoding.

Polymorphic nested records are not supported in v0.1.

Arrays of records and nested arrays remain unsupported by the generated C++
codecs in this revision.

---

## Byte Order

All multi-byte integer and floating-point fields SHALL be encoded in big-endian
byte order.

The format uses a single fixed byte order.

Implementations SHALL NOT emit host-endian records.

---

## Padding and Alignment

The Binary Record Format SHALL NOT insert padding bytes between fields.

Directory entries SHALL be encoded back-to-back.

Payload values SHALL be encoded back-to-back.

The binary layout is independent of host CPU alignment rules and programming
language structure layout.

Generated accessors and builders are responsible for reading and writing fields
using the encoded Field Directory and Payload.

Implementations SHALL NOT assume encoded fields are naturally aligned in memory.

---

## Integer Encoding

Signed integers SHALL use two's-complement representation.

`int8` and `uint8` are encoded as one byte.

All multi-byte integers SHALL use big-endian byte order.

---

## Varuint Encoding

`varuint` is an unsigned variable-length integer encoding.

`varuint` SHALL use unsigned LEB128 encoding: each byte carries seven payload
bits in the low bits, and the high bit is set when another byte follows. The
least significant group is encoded first. The value zero is encoded as the
single byte `0x00`.

The Binary Record Format uses `varuint` for Field Directory `fieldOffset` and
`fieldLength` values.

`fieldLength` represents a byte count.

`fieldOffset` represents a byte offset relative to the start of the Payload.

`fieldOffset` and `fieldLength` SHALL NOT represent characters, elements, or
field presence.

---

## Boolean Encoding

`bool` SHALL be encoded as one byte.

`0x00` represents false.

`0x01` represents true.

Any other value SHALL be treated as invalid by validators and decoders.

---

## Floating Point Encoding

`float32` SHALL use IEEE 754 binary32.

`float64` SHALL use IEEE 754 binary64.

Multi-byte floating-point values SHALL use the same byte order as all other
multi-byte values in the format.

Implementations SHALL NOT use host-specific floating-point layouts.

---

## Variable-Length Data Encoding

Variable-length byte sequences are encoded as data bytes in the Payload.

The byte length is supplied by the Field Directory `fieldLength` value for the
field.

The encoded byte length SHALL NOT exceed `max_bytes`.

The `fieldLength` value represents the number of encoded bytes, not characters
or elements.

No NUL terminator is encoded.

No internal length prefix is encoded. The Field Directory is the only length
source for `string` and `bytes` field values.

The binary format encodes byte sequences. The schema defines whether those bytes
are interpreted as text or opaque data.

### string

`string` uses variable-length data encoding.

String data bytes SHALL be valid UTF-8.

UTF-8 validity is a string validation rule, not a different binary layout.

The `max_bytes` bound for a `string` is measured in encoded UTF-8 bytes, not
Unicode code points.

Embedded U+0000 is valid string data.

No Unicode normalization is performed by the Binary Record Format.

### bytes

`bytes` uses variable-length data encoding.

Bytes data may contain any byte sequence.

No UTF-8 validation applies.

The `max_bytes` bound for `bytes` is measured in raw bytes.

---

## Array Encoding

Array field payloads SHALL begin with an element count encoded as unsigned
LEB128 `varuint`.

The count SHALL NOT exceed `max_elements`.

The Field Directory `fieldLength` SHALL cover the complete array payload,
including the encoded element count and all encoded element data.

An array count of zero is valid. A present empty array SHALL be encoded as a
Field Directory entry whose payload is the canonical zero count byte `0x00`.
An absent array SHALL have no Field Directory entry.

For fixed-width element types, array elements SHALL be encoded immediately after
the count, tightly packed, in index order, with no padding or alignment bytes.
Each element SHALL use the same byte representation as the equivalent standalone
field value.

For variable-length leaf element types, array elements SHALL be encoded
immediately after the count in index order. Each element SHALL begin with an
unsigned LEB128 `varuint` `elementLength`, followed by exactly `elementLength`
bytes of element data. `elementLength` counts only the element data bytes; it
does not include the length prefix. There is no array-level internal byte length,
no offset table, no string terminator, and no padding or alignment bytes.

For `array<string>`, each element data region is raw UTF-8 bytes. Every element
SHALL be valid UTF-8 on encode and decode. The per-element `max_bytes` bound is
measured in encoded UTF-8 bytes. Embedded U+0000 is valid, and no Unicode
normalization is performed.

For `array<bytes>`, each element data region is arbitrary raw bytes. The
per-element `max_bytes` bound is measured in raw bytes. No UTF-8 validation
applies.

For `array<record>`, each element SHALL begin with an unsigned LEB128 `varuint`
`elementLength`, followed by exactly `elementLength` bytes containing one
complete embedded Binary Record Format record. The element length covers the
complete embedded record, including its 16-byte Record Header, Field Directory,
and Payload.

Every record-array element encodes its own `recordId`, `headerVersion`, flags,
reserved fields, Field Directory, and `payloadLength`. Generated decoders SHALL
validate every element by passing the isolated element bytes to the referenced
record's generated decoder. Wrong element `recordId` values, unsupported
versions, nonzero flags, nonzero reserved fields, malformed embedded payload
lengths, malformed embedded Field Directories, malformed known element fields,
and trailing bytes inside an element cause decoding of the containing array to
fail.

Record-array elements are independently decodable: an element byte sequence
extracted from the array can be decoded by the referenced record's generated
decoder without runtime Schema IR.

The empty array payload is exactly `00`. For length-delimited `array<string>`
and `array<bytes>` payloads, an array containing one empty element is encoded
as `01 00`: element count one followed by an element length of zero. These are
distinct from an absent array, which has no Field Directory entry. For record
arrays, an array containing one empty record is distinct again: it is encoded
as element count one, the embedded empty record length, and the complete
embedded empty record bytes.

Generated C++ codecs currently support arrays of:

* `bool`
* fixed-width signed and unsigned integers
* `f32`
* `f64`
* enum references whose declared values are all non-negative and whose decoded
  numeric values are declared by the enum
* `string`
* `bytes`
* record references

Generated decoders SHALL reject fixed-width array payloads unless the bytes
remaining after the count are exactly `element_count * element_width`, computed
with checked arithmetic. Decoders SHALL reject counts greater than
`max_elements` before allocating the materialized array.

Generated decoders SHALL reject variable-length array payloads unless exactly
`element_count` length-delimited elements are consumed and no trailing bytes
remain. Decoders SHALL reject counts greater than `max_elements` before
allocating the materialized array and SHALL reject each element whose
`elementLength` exceeds the schema-defined per-element `max_bytes` bound before
copying element data.

Generated decoders SHALL reject record-array payloads unless exactly
`element_count` length-delimited complete embedded records are consumed and no
trailing bytes remain. Decoders SHALL reject counts greater than `max_elements`
before allocating the materialized array. Unknown fields inside record elements
are ignored under the normal generated decode policy after structural
validation.

Nested arrays remain unsupported by the generated C++ codecs in this revision.

Unbounded arrays are not supported.

The homogeneous-envelope optimization investigated for record arrays is not
part of BRF v0.1. Record-array elements in v0.1 are length-delimited complete
embedded records.

---

## Enum Encoding

Enum fields SHALL be encoded using the smallest fixed-width unsigned integer
capable of representing the largest enum value defined by the schema.

The first generated C++ encoder supports enum fields only when every value
defined by the referenced enum is non-negative. Encoding negative enum values
remains deferred until the enum wire rule is extended beyond unsigned widths.

Decoders MAY expose unknown enum numeric values.

Validators SHALL report enum values not defined by the schema as invalid unless
the schema compatibility rules explicitly allow them.

Generated APIs SHOULD preserve the raw numeric enum value when an unknown value
is encountered.

---

## Validation Rules

Validators and decoders SHALL report invalid records when:

* `headerVersion` is unsupported.
* any reserved header bit or byte is not zero.
* `payloadLength` exceeds the active `maxPayloadLength` profile.
* `recordId` is unknown.
* `directoryEntryCount` exceeds the schema's declared field count.
* the record body is shorter or longer than `payloadLength`.
* the Field Directory is malformed.
* the Field Directory contains more entries than declared by `directoryEntryCount`.
* the Field Directory contains fewer entries than declared by `directoryEntryCount`.
* Field Directory entries are not sorted by `fieldIndex`.
* a `fieldIndex` appears more than once in the same payload.
* a `fieldOffset` points outside the Payload.
* `fieldOffset + fieldLength` exceeds the Payload size.
* payload ranges referenced by directory entries overlap.
* a present field value is truncated.
* a bool value is not `0x00` or `0x01`.
* an array count exceeds `max_elements`.
* variable-length byte length exceeds `max_bytes`.
* string data is not valid UTF-8.
* an enum value is not defined by the schema unless compatibility rules allow it.

---

## Explicit Omissions

The v0.1 Binary Record Format does not include:

* timestamps in the Record Header
* deviceId in the Record Header
* checksum or CRC fields
* compression metadata
* encryption metadata
* footer or trailer data
* varint header fields
* padding or alignment bytes
