# Breadcrumbs Binary Record Format Specification

## Status

Draft

## Version

0.1

## Purpose

This document defines the binary representation of Breadcrumbs top-level records
and payload fragments.

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

A nested record is encoded as a Field Directory and Payload fragment only.

A nested record SHALL NOT contain a Record Header.

Only top-level records contain Record Headers.

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

Nested records use the same body layout but do not include a Record Header.

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

Decoders use `fieldOffset` and `fieldLength` to locate each field value within
the Payload.

Decoders identify fields using `fieldIndex`.

For known `fieldIndex` values, decoders use the declared field type to decode
the corresponding payload bytes.

Unknown `fieldIndex` values are ignored. Decoders skip the corresponding payload
bytes using `fieldOffset` and `fieldLength`.

Unknown fields do not affect decoding of known fields.

---

## Nested Records

Nested records are encoded as Field Directory and Payload fragments.

The type of a nested record is determined exclusively by the parent schema.

This design preserves deterministic parsing, minimizes encoding overhead, and
follows the Breadcrumbs principle of compile-time knowledge over runtime
discovery.

Polymorphic nested records are not supported in v0.1.

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

The binary format encodes byte sequences. The schema defines whether those bytes
are interpreted as text or opaque data.

### string

`string` uses variable-length data encoding.

String data bytes SHALL be valid UTF-8.

UTF-8 validity is a string validation rule, not a different binary layout.

### bytes

`bytes` uses variable-length data encoding.

Bytes data may contain any byte sequence.

No UTF-8 validation applies.

---

## Array Encoding

The array element count SHALL be encoded using the smallest fixed-width unsigned
integer capable of representing the schema-defined `max_elements` value.

The count SHALL NOT exceed `max_elements`.

An array count of zero is valid.

Array elements SHALL be encoded immediately after the count, in index order.

Unbounded arrays are not supported.

---

## Enum Encoding

Enum fields SHALL be encoded using the smallest fixed-width unsigned integer
capable of representing the largest enum value defined by the schema.

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
