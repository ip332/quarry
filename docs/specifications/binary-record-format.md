# Breadcrumbs Binary Record Format Specification

## Status

Draft

## Version

0.1

## Purpose

This document defines the binary representation of Breadcrumbs top-level records
and payload fragments.

The binary record format defines how record headers, payloads, nested records,
presence information, scalar values, arrays, strings, bytes, and enum values are
encoded.

This document does not define schema syntax, transport protocols, application
semantics, or cloud APIs.

---

## Record Layout

A top-level record is encoded as:

```text
Record Header + Record Payload
```

A nested record is encoded as a payload fragment only.

A nested record SHALL NOT contain a Record Header.

Only top-level records contain Record Headers.

There is no footer or trailer in v0.1.

---

## Record Header v0.1

```text
headerVersion   uint8
flags           uint8
reserved        uint16
recordId        uint32
sequenceNumber  uint32
payloadLength   uint32
```

The Record Header is fixed-size in v0.1.

Header fields SHALL NOT use varint encoding.

All flag bits SHALL be zero in v0.1.

The reserved header field SHALL be zero.

The Record Header does not contain:

* timestamps
* deviceId
* checksum or CRC
* compression metadata
* encryption metadata
* footer or trailer metadata

---

## recordId and Compatibility

A `recordId` identifies a compatible evolution line, not a single schema file
revision.

Compatible schema evolution SHALL keep the same `recordId`.

Incompatible layout or semantic changes SHALL require a new `recordId`.

Backward compatibility is provided by schema evolution rules and generated
readers, not by a record version field in the Record Header.

Older readers MAY read the fields they know and ignore trailing payload bytes
for fields added after the version they know when compatibility rules allow.

Within the same `recordId`, existing field order, type, encoding, and meaning
SHALL NOT change.

---

## Payload Layout

The Record Payload begins immediately after the Record Header for top-level
records.

The payload begins with a presence bitmap if and only if the schema has optional
fields.

Fields are encoded in schema definition order.

Required fields are always encoded.

Optional fields are encoded only when present.

Nested records use the same payload layout but do not include a Record Header.

---

## Presence Bitmap

The presence bitmap is present if and only if the schema has at least one
optional field.

The presence bitmap contains one bit per optional field.

Optional-field order follows schema definition order.

A bit value of `1` means the optional field is present.

A bit value of `0` means the optional field is absent.

The bitmap size is:

```text
ceil(optional_field_count / 8) bytes
```

Unused bits in the final byte SHALL be zero.

---

## Nested Records

Nested records are encoded as payload fragments.

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

Fields SHALL be encoded back-to-back in schema definition order.

The binary layout is independent of host CPU alignment rules and programming
language structure layout.

Generated accessors and builders are responsible for reading and writing fields
at their encoded byte offsets.

Implementations SHALL NOT assume encoded fields are naturally aligned in memory.

---

## Integer Encoding

Signed integers SHALL use two's-complement representation.

`int8` and `uint8` are encoded as one byte.

All multi-byte integers SHALL use big-endian byte order.

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

Variable-length byte sequences are encoded as:

```text
length prefix + data bytes
```

The length prefix size SHALL use the smallest fixed-width unsigned integer
capable of representing the schema-defined `max_bytes` value.

The encoded byte length SHALL NOT exceed `max_bytes`.

The length value represents the number of encoded bytes, not characters or
elements.

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
* the payload is shorter or longer than `payloadLength`.
* the presence bitmap is malformed.
* unused presence bitmap bits are not zero.
* a required field is missing or truncated.
* an optional field is present but truncated.
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
