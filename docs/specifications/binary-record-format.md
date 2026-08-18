# Quarry Binary Record Format Specification

## Status

Draft

## Version

BRF v1 (the current v0.1 format) and BRF v2 (draft).

## Purpose

This document defines the binary representation of Quarry records and
field payloads for BRF v1 and the proposed BRF v2 layout.

The binary record format defines how record headers, field directories,
payloads, nested records, scalar values, arrays, strings, bytes, and enum values
are encoded.

This document does not define schema syntax, transport protocols, application
semantics, cloud APIs, or the future QBS representation.

## Format Versions and Identities

BRF physical format version, logical record identity, and exact schema identity
are independent concepts:

```text
brf_format_version
    The physical byte representation and validation rules.

record_id
    The logical record or message identity.

schema_id
    The identity of an exact schema definition and version.
```

`record_id` SHALL mean only the logical identity of the record or message type.
It SHALL NOT identify a BRF physical format version, an exact schema revision,
an exact physical layout, a schema hash, or a schema evolution version. A
logical record MAY retain the same `record_id` across BRF v1 and BRF v2,
compatible schema evolution, and changes to a future exact `schema_id`. A new
`record_id` is required only when the logical record or message identity itself
changes such that it is treated as a different logical record type.

A future `schema_id` MAY be carried by an outer protocol, a schema registry,
QBS, or a future header extension; BRF v2 does not define the schema identity
mechanism.

The first header byte is the BRF format discriminator in both versions:

* BRF v1 uses value `1`;
* BRF v2 uses value `2`.

A decoder SHALL select the physical parser from this discriminator before
interpreting the remaining header or record body. BRF v1 and BRF v2 records
MUST NOT be confused.

---

## BRF v1 (Current v0.1 Format)

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

A `recordId` is the compiler-generated binary identifier of the logical record
or message type. It is not a schema-version, schema-hash, physical-layout, or
BRF-format identifier.

A logical record MAY retain the same `recordId` through compatible schema
evolution and across BRF format versions. A physical BRF layout change SHALL
be identified by `brf_format_version` and MUST NOT require a new `recordId`
solely because the physical format changed.

A new `recordId` is required only when the logical record or message identity
changes such that it is treated as a different logical record type.

Backward compatibility is provided by schema evolution rules, the selected BRF
format version, and generated readers. `record_id` is not a physical format
version field.

Older readers MAY read the fields they know and ignore unknown Field Directory
entries and their referenced payload bytes when compatibility rules allow.

Detailed schema compatibility, field evolution, and exact schema identity rules
are outside this BRF specification and belong to future schema/QBS evolution
work.

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

Arrays of records are supported; see Array Encoding below. Nested arrays
(arrays of arrays) remain unsupported by the generated C++ codecs in this
revision.

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

---

# BRF v2 Draft

## BRF v2 Scope

BRF v2 changes the physical record layout so that every declared field has
either a schema-determined fixed location or a schema-determined fixed-size
descriptor location. Runtime values and lengths of preceding fields SHALL NOT
change those locations.

BRF v2 is a physical serialization format. It is not QBS and does not define
schema exchange, exact schema identity, or a generic schema interpreter.

## BRF v2 Header

The BRF v2 header is exactly 16 bytes:

```text
offset  size  field
0       1     brf_format_version
1       1     flags
2       2     header_size
4       4     record_id
8       4     fixed_region_size
12      4     record_size
```

The fields use big-endian encoding. BRF v2 requires:

```text
brf_format_version = 2
flags = 0
header_size = 16
```

`record_size` is the complete encoded record size, including the header. It
MUST be at least `header_size + fixed_region_size` and MUST fit in `uint32`.

`fixed_region_size` is the number of bytes immediately following the header
that belong to the fixed region. It includes the presence bitmap and every
fixed field slot or variable-field descriptor slot. It does not include any
variable-data payload.

The variable region begins at:

```text
variable_region_start = header_size + fixed_region_size
```

The variable region ends at `record_size`. A record with no variable payload
has `record_size == variable_region_start`.

The v2 header does not contain `schema_id`. An exact schema identity MAY be
carried by an outer protocol, QBS, a registry, or a future header extension.

## BRF v2 Presence Bitmap

The presence bitmap is the first object in the fixed region. Its size is:

```text
presence_bitmap_size = ceil(declared_field_count / 8)
```

Fields are assigned bitmap positions in schema declaration order. Bitmap bit
zero is the least-significant bit of the first bitmap byte; subsequent field
positions increase toward more significant bits and then continue in the next
byte.

Unused high bits in the final bitmap byte MUST be zero.

A set bit means that the corresponding field is present. A clear bit means
that it is absent. Presence is independent from the field payload.

Every declared field has a fixed-region slot even when it is absent. For a
canonical encoding:

* an absent fixed field's slot bytes MUST be zero;
* an absent variable field's descriptor bytes MUST be zero;
* an absent nested field's inline bytes, when it is fixed-size, MUST be zero.

An absent inline fixed-size nested-record slot is canonical zero storage and
SHALL NOT be interpreted or validated as an embedded BRF v2 record unless the
corresponding parent presence bit is set. When the parent presence bit is set,
the inline nested-record slot SHALL contain a complete valid BRF v2 record and
SHALL be validated according to the normal nested-record rules.

A present zero-length string, bytes value, or variable array is distinct from
an absent field.

## BRF v2 Canonical Fixed Region

After the presence bitmap, field slots occur in schema declaration order.

The compiler SHALL determine the size and location of every slot. Backends
MUST consume this canonical layout rather than calculate BRF offsets from
native structures or independently from one another.

The fixed region may contain:

* fixed-width scalar slots;
* enum slots;
* fixed-size array slots when fixed arrays are added to the schema language;
* complete inline fixed-size nested records;
* fixed-size variable-field descriptor slots.

BRF v2 is packed. No implicit or native ABI padding is inserted between slots.

The canonical physical order is schema declaration order. Implementations MUST
NOT reorder fields based on compiler, CPU, or native structure alignment.

## BRF v2 Field Locations

The compiler's internal layout model SHALL represent a field location as:

```text
FieldLocation {
    byte_offset
    bit_offset
    bit_width
}
```

For ordinary BRF v2 fields:

```text
bit_offset = 0
bit_width = encoded_byte_width * 8
```

`byte_offset` is relative to the beginning of the complete BRF record. A
variable field's `byte_offset` points to its descriptor, not to its variable
payload.

BRF v2 does not introduce public packed-field or bit-field schema syntax.
All currently encodable fields, descriptors, and variable payloads are
byte-aligned.

The location abstraction is intentionally capable of representing future
packed fields without a fundamental BRF redesign. If packed fields are added,
the specification MUST define bit numbering separately from byte endianness.
The reserved direction is least-significant-bit first within each addressed
byte (`bit_offset = 0` identifies that byte's least-significant bit).

## BRF v2 Variable-Field Descriptor

Every variable-size field has an 8-byte descriptor in its schema-determined
fixed-region slot:

```text
uint32 data_offset
uint32 byte_length
```

Both values use big-endian encoding.

`data_offset` is an absolute byte offset from the beginning of the complete
BRF record. It MUST point into the variable region, or to its end for a
zero-length object.

`byte_length` is the complete number of bytes occupied by the variable object.

The descriptor does not contain an element count. Counts are represented only
where required by the variable object's encoding:

* strings and bytes have no internal count;
* variable arrays begin with an encoded element count;
* fixed-size arrays derive their count from the schema;
* nested records have no array count.

This common descriptor is used for:

* strings;
* bytes;
* variable arrays;
* variable-size nested records;
* arrays of records.

## BRF v2 Variable Region

Variable objects are stored after the complete fixed region. Their physical
order is the order of their corresponding fields in schema declaration order.

Variable objects MUST NOT overlap. Zero-length objects do not overlap any
other object and MAY have the same offset as another zero-length object.

The variable region contains no implicit padding. All variable objects begin
at byte boundaries.

### Strings and bytes

The descriptor's `byte_length` is the length of the raw object bytes.

Strings contain valid UTF-8 bytes and have no NUL terminator or internal length
prefix. Bytes contain arbitrary bytes and have no internal length prefix.

### Variable arrays

A variable array object begins with an unsigned LEB128 `varuint` element count:

```text
varuint element_count
array elements
```

The descriptor's `byte_length` includes the count prefix and every element.

For fixed-width elements, elements follow tightly with no padding:

```text
element[0]
element[1]
...
```

For fixed-size nested-record elements, complete embedded BRF v2 records follow
tightly in schema-known element-size slots. No per-element byte length is
required because the element size is determined recursively from the schema.

For variable-size leaf elements and variable-size record elements, each element
is length-delimited:

```text
varuint element_byte_length
element bytes
```

The array count is required at runtime and is therefore retained inside the
array object rather than duplicated in the common field descriptor.

A present empty array has `element_count = 0` and a canonical one-byte array
object containing `00`. An absent array has a clear presence bit and a zeroed
descriptor.

### Nested records

A variable-size nested record's variable object is a complete BRF v2 record,
including its own header, presence bitmap, fixed region, and variable region.
The parent descriptor's `byte_length` covers the complete child record.

## BRF v2 Fixed/Variable Classification

Classification is recursive.

A record is fixed-size when all of its fields are fixed-size, including all
nested record fields and all array element types, and when any future fixed
array counts are schema-defined.

A record is variable-size when it contains any variable-size field or any
nested record that is variable-size.

The following rules apply:

* fixed-width scalar and enum fields are inline fixed slots;
* fixed-size nested records are inline complete BRF v2 records;
* variable-size nested records use an 8-byte descriptor and trailing child
  record;
* variable arrays use an 8-byte descriptor and a trailing array object;
* arrays of fixed-size nested records use their schema-known element size;
* arrays of variable-size nested records use per-element byte lengths.

An inline fixed-size nested record retains its complete BRF representation,
including its own header and presence bitmap. A future optimization MAY
investigate a more compact embedded representation when the nested type is
already known from the parent schema, but that optimization is not part of
BRF v2.

## BRF v2 Nested Record Access

For an inline fixed-size nested record, the parent field location points to the
child's complete BRF v2 header when the parent presence bit is set. If the
parent presence bit is clear, the slot is canonical zero storage and the child
header MUST NOT be read or validated. When present, the child has its own
`record_id`, fixed-region size, record size, presence bitmap, and field slots,
all of which SHALL be validated according to the normal nested-record rules.

For a variable-size nested record, the parent field location points to the
child descriptor. The descriptor points to the complete child record in the
parent variable region.

At every nesting level, offsets are relative to the beginning of the complete
record containing the descriptor. A nested record's own descriptors are
relative to the nested record's beginning, not the parent's beginning.

## BRF v2 Byte Order and Alignment

All fixed-width integer fields, floating-point fields, header fields, presence
metadata, and variable descriptors use the canonical BRF byte order: big-endian
for multi-byte values.

Unsigned LEB128 `varuint` values retain their byte-oriented LEB128 encoding;
they are not native-endian values.

BRF v2 does not use native C/C++ structure layout, `offsetof()`, compiler
bit-fields, or native alignment as wire-format rules. Implementations MUST use
explicit byte-wise encoding and decoding that is safe for unaligned storage.

## BRF v2 Validation Rules

A v2 decoder SHALL reject a record when any of the following is true:

* `brf_format_version` is not `2`;
* `header_size` is not supported or is smaller than the v2 header;
* `record_size` is smaller than `header_size + fixed_region_size`;
* the input does not contain exactly `record_size` bytes;
* `fixed_region_size` causes the variable-region start to overflow;
* the presence bitmap is shorter than the schema requires;
* unused presence-bitmap bits are nonzero;
* a fixed-region slot lies outside the declared fixed region;
* an absent fixed slot or descriptor is not canonical zero when canonical input
  is required;
* a descriptor is truncated or lies outside the fixed region;
* `data_offset` is before the variable region;
* `data_offset` is greater than `record_size`;
* `byte_length` is greater than `record_size - data_offset`;
* variable objects overlap, except that zero-length objects may share offsets;
* an array count exceeds the schema or implementation limit;
* array count framing is truncated or malformed;
* fixed-width array count multiplication overflows;
* an array's consumed bytes do not equal its descriptor `byte_length`;
* a variable array element length exceeds the remaining array object;
* a present inline nested record is truncated or extends beyond its fixed slot;
* a present variable nested record is truncated or extends beyond its
  descriptor range;
* a nested record uses an unsupported BRF format version;
* a nested record has an unexpected logical `record_id`;
* a scalar, enum, string, or bytes value violates its schema-level encoding
  rules;
* a value exceeds the schema's `max_bytes` or `max_elements` bound.

All offset and length calculations MUST use checked arithmetic. The `uint32`
record-size fields impose a maximum encoded BRF v2 record size of
`UINT32_MAX` bytes, subject to smaller implementation or transport limits.
Implementations MAY impose a smaller configured maximum, but MUST reject
records exceeding that maximum before allocation or pointer formation.

## BRF v2 Examples

For the schema:

```text
record Example {
    uint32 timestamp;
    string name;
    uint16 state;
    uint16[] samples;
}
```

with values `timestamp = 1`, `name = "abc"`, `state = 2`, and
`samples = [10, 20]`, assume one presence byte and 8-byte descriptors.

The fixed region is:

```text
presence bitmap       1 byte
timestamp             4 bytes
name descriptor       8 bytes
state                 2 bytes
samples descriptor    8 bytes
```

The fixed region is 23 bytes. The variable region is:

```text
61 62 63                         name
02 00 0a 00 14                   count=2, samples
```

The header is conceptually:

```text
brf_format_version = 2
flags = 0
header_size = 16
record_id = 1
fixed_region_size = 23
record_size = 47
```

The descriptor for `name` points to the first variable byte and has
`byte_length = 3`. The descriptor for `samples` points to the array count and
has `byte_length = 5`.

For a fixed nested record:

```text
record Child {
    uint16 level;
    uint32 code;
}

record Parent {
    uint32 timestamp;
    Child child;
    uint16 state;
}
```

The complete fixed-size child occupies:

```text
16-byte child header
1-byte child presence bitmap
2-byte level
4-byte code
= 23 bytes
```

The child is placed inline in the parent's fixed region. No parent descriptor
is emitted.

If `Child` instead contains a `string label`, the child is variable-size. The
parent receives an 8-byte child descriptor, and the complete child BRF v2
record is placed in the parent's variable region.

## BRF v2 Compatibility Rules

BRF v1 and BRF v2 are distinct physical formats selected by
`brf_format_version`. A v1 decoder MUST NOT interpret a v2 record as v1, and a
v2 decoder MUST NOT interpret a v1 record as v2.

Changing from BRF v1 to BRF v2 does not, by itself, change `record_id`.
`record_id` changes only when the logical record or message identity itself
changes such that it is treated as a different logical record type.

An exact schema change MAY change `schema_id` while retaining `record_id`.
The schema identity mechanism and schema compatibility rules are outside this
specification.

Changing a field from fixed to variable, or variable to fixed, is a physical
layout change and requires a distinct schema/layout interpretation. It does
not by itself require a new logical `record_id`.

## BRF v2 Open Questions

The following are intentionally outside the first v2 implementation contract:

* the transport or container location of an exact `schema_id`;
* a schema hash algorithm and schema identity registry;
* public packed-field syntax;
* the complete bit-spanning encoding rules for future packed fields;
* whether a smaller BRF16 profile is justified by measurements;
* preservation of unknown fields during generated re-encoding;
* the future compact embedded representation optimization for fixed nested
  records.

None of these questions changes the v2 fixed-region, 8-byte descriptor,
record-relative-offset, or independent-identity decisions in this draft.
