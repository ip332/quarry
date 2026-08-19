"""Quarry Python runtime: BRF codec primitives.

Covers bool, fixed-width signed/unsigned integers, float32/float64, enums,
strings, bytes, fixed-width arrays of scalar and enum values, and
length-delimited arrays of strings and bytes, plus varuint I/O and whole-record
(BRF v1 compatibility and BRF v2 fixed-region/variable-tail) assembly/parsing. Mirrors the
byte-for-byte wire behavior of
quarry/runtime/binary_record.hpp (C++) and quarry/runtime_c/binary_record.h
(C) for that same subset. Built entirely on the standard library (`struct`
for big-endian packing/range-checked scalar conversion), per this project's
"do not hand-write integer packing if the stdlib already provides it" rule.

Nested-record fields and record arrays are composed by generated codecs using
the schema-neutral v2 layout helpers; nested arrays remain a backend limitation.
The legacy v1 record helpers remain available for old data and compatibility
tests. See docs/design/python-backend.md for the supported surface.
"""

import struct

_HEADER_VERSION = 1
_HEADER_SIZE = 16
_HEADER_STRUCT = struct.Struct(">BBBBIII")
_BRF_V2_VERSION = 2
_BRF_V2_HEADER_STRUCT = struct.Struct(">BBHIII")
_BRF_V2_HEADER_SIZE = 16
_BRF_V2_DESCRIPTOR_SIZE = 8
_BRF_V2_STORAGE_FIXED = 0
_BRF_V2_STORAGE_INLINE_FIXED_NESTED = 1
_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR = 2
_MAX_DIRECTORY_ENTRIES = 255
_MAX_VARUINT_BYTES = 10  # ceil(64 / 7), matches the C++/C runtimes' own bound


class EncodeError(Exception):
    """Raised when a value or record cannot be encoded as BRF bytes."""


class DecodeError(Exception):
    """Raised when bytes cannot be decoded as a valid BRF record or field value."""


_SCALAR_STRUCT_FORMATS = {
    "int8": "b",
    "uint8": "B",
    "int16": "h",
    "uint16": "H",
    "int32": "i",
    "uint32": "I",
    "int64": "q",
    "uint64": "Q",
    "float32": "f",
    "float64": "d",
}


def pack_scalar(type_name: str, value) -> bytes:
    """Encodes `value` as big-endian BRF bytes for the given scalar type name.

    `type_name` is one of "bool", "int8", "uint8", ..., "float64". Raises
    EncodeError if `value` does not fit the type (e.g. an out-of-range
    integer, or a non-bool for "bool") -- Python's int/float have no fixed
    width of their own, unlike C++/C's native scalar types, so this check
    is the Python-specific equivalent of what those languages' type
    systems enforce structurally at the call site.
    """
    if type_name == "bool":
        if not isinstance(value, bool):
            raise EncodeError(f"expected bool, got {type(value).__name__}: {value!r}")
        return bytes([1 if value else 0])

    fmt = _SCALAR_STRUCT_FORMATS.get(type_name)
    if fmt is None:
        raise EncodeError(f"unsupported scalar type: {type_name}")
    try:
        return struct.pack(">" + fmt, value)
    except struct.error as error:
        raise EncodeError(f"value {value!r} does not fit {type_name}: {error}") from error


def unpack_scalar(type_name: str, data: bytes):
    """Decodes big-endian BRF bytes for the given scalar type name.

    Raises DecodeError if `data` is not exactly the expected byte width
    for the type, or (bool only) is a byte other than 0x00/0x01.
    """
    if type_name == "bool":
        if len(data) != 1:
            raise DecodeError(f"bool field must be exactly 1 byte, got {len(data)}")
        if data[0] == 0:
            return False
        if data[0] == 1:
            return True
        raise DecodeError(f"invalid bool byte: {data[0]!r}")

    fmt = _SCALAR_STRUCT_FORMATS.get(type_name)
    if fmt is None:
        raise DecodeError(f"unsupported scalar type: {type_name}")
    expected_size = struct.calcsize(fmt)
    if len(data) != expected_size:
        raise DecodeError(
            f"{type_name} field must be exactly {expected_size} byte(s), got {len(data)}")
    (value,) = struct.unpack(">" + fmt, data)
    return value


def pack_enum(enum_cls, value, type_name: str) -> bytes:
    """Validates that `value` is a member of `enum_cls` (constructing
    `enum_cls(value)` also accepts a raw int matching one of its members,
    which is how a plain-int field value ends up validated) and encodes its
    underlying integer via pack_scalar. `enum_cls` is expected to be an
    `enum.IntEnum` subclass; `type_name` is the smallest unsigned scalar
    type wide enough for the enum's declared values (e.g. "uint8"),
    matching the BRF spec's Enum Encoding rule and the C++/C backends'
    identical width computation. Raises EncodeError -- not the stdlib's
    ValueError -- for a value that is not a defined member, so callers only
    ever need to catch EncodeError/DecodeError from generated code.
    """
    try:
        member = enum_cls(value)
    except ValueError as error:
        raise EncodeError(f"{value!r} is not a valid {enum_cls.__name__} value") from error
    return pack_scalar(type_name, int(member))


def unpack_enum(enum_cls, type_name: str, data: bytes):
    """Decodes an underlying integer via unpack_scalar and constructs
    `enum_cls` from it. Raises DecodeError -- matching the BRF spec's
    requirement that a decoded enum value the schema does not define is a
    decode failure -- for a decoded integer that is not a defined member of
    `enum_cls`.
    """
    raw_value = unpack_scalar(type_name, data)
    try:
        return enum_cls(raw_value)
    except ValueError as error:
        raise DecodeError(f"{raw_value!r} is not a valid {enum_cls.__name__} value") from error


def pack_string(value: str, max_bytes: int) -> bytes:
    """Encodes `value` as UTF-8 BRF field bytes, per the BRF spec's
    "Variable-Length Data Encoding" / "string" sections: no internal length
    prefix (the Field Directory's own fieldLength supplies the byte
    length), UTF-8 required, embedded U+0000 valid.

    Deliberately reuses Python's own `str.encode("utf-8")` for UTF-8
    validation rather than hand-rolling a validator the way the C++/C
    runtimes must -- Python's `str` type already guarantees well-formed
    Unicode text, and `encode()` itself raises `UnicodeEncodeError` for the
    one remaining edge case (a lone surrogate code point, which can exist
    in a Python `str` but has no UTF-8 encoding). Raises EncodeError (not
    the stdlib's UnicodeEncodeError) for that case, and for an encoded
    length exceeding `max_bytes`, so callers only ever need to catch
    EncodeError/DecodeError from generated code.
    """
    if not isinstance(value, str):
        raise EncodeError(f"expected str, got {type(value).__name__}: {value!r}")
    try:
        encoded = value.encode("utf-8")
    except UnicodeEncodeError as error:
        raise EncodeError(f"value is not valid UTF-8 text: {error}") from error
    if len(encoded) > max_bytes:
        raise EncodeError(
            f"encoded string length {len(encoded)} exceeds max_bytes={max_bytes}")
    return encoded


def unpack_string(data: bytes, max_bytes: int) -> str:
    """Decodes UTF-8 BRF field bytes back into a `str`.

    Checks `len(data)` against `max_bytes` first, then decodes -- mirroring
    the C++/C runtimes' own bounds-check-before-content-validate ordering.
    Reuses Python's own `bytes.decode("utf-8")` for UTF-8 validation (no
    hand-rolled validator): its strict-mode rejection of overlong
    encodings, lone continuation bytes, truncated sequences, and encoded
    surrogate halves already matches exactly what the BRF spec and the
    C++/C runtimes' custom validators require. Raises DecodeError (not the
    stdlib's UnicodeDecodeError) for malformed UTF-8, and for a length
    exceeding `max_bytes`.
    """
    if len(data) > max_bytes:
        raise DecodeError(f"string field length {len(data)} exceeds max_bytes={max_bytes}")
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise DecodeError(f"field is not valid UTF-8: {error}") from error


def pack_bytes(value: bytes, max_bytes: int) -> bytes:
    """Encodes `value` as BRF field bytes for a `bytes` field: the raw
    content verbatim, no UTF-8 validation (per the BRF spec's "bytes"
    section: "Bytes data may contain any byte sequence... No UTF-8
    validation applies"). Raises EncodeError for a non-bytes-like value or
    a length exceeding `max_bytes`.
    """
    if not isinstance(value, (bytes, bytearray)):
        raise EncodeError(f"expected bytes, got {type(value).__name__}: {value!r}")
    if len(value) > max_bytes:
        raise EncodeError(f"bytes length {len(value)} exceeds max_bytes={max_bytes}")
    return bytes(value)


def unpack_bytes(data: bytes, max_bytes: int) -> bytes:
    """Decodes BRF field bytes for a `bytes` field: the raw content
    verbatim, no UTF-8 validation. Raises DecodeError for a length
    exceeding `max_bytes`.
    """
    if len(data) > max_bytes:
        raise DecodeError(f"bytes field length {len(data)} exceeds max_bytes={max_bytes}")
    return bytes(data)


def append_varuint(buffer: bytearray, value: int) -> None:
    """Appends `value` (a non-negative int) to `buffer` as unsigned LEB128,
    matching the BRF spec's Varuint Encoding section exactly."""
    if value < 0:
        raise EncodeError(f"varuint value must be non-negative, got {value}")
    while True:
        byte = value & 0x7F
        value >>= 7
        if value != 0:
            byte |= 0x80
        buffer.append(byte)
        if value == 0:
            break


def read_varuint(data: bytes, offset: int) -> tuple[int, int]:
    """Reads one unsigned LEB128 varuint from `data` starting at `offset`.

    Returns (value, new_offset). Raises DecodeError on truncated or
    overlong (more than 10 bytes / 64 bits) input, mirroring the C++/C
    runtimes' identical 10-byte bound.
    """
    value = 0
    shift = 0
    for _ in range(_MAX_VARUINT_BYTES):
        if offset >= len(data):
            raise DecodeError("truncated varuint")
        byte = data[offset]
        offset += 1
        payload = byte & 0x7F
        if shift == 63 and payload > 1:
            raise DecodeError("varuint overflows 64 bits")
        value |= payload << shift
        if (byte & 0x80) == 0:
            return value, offset
        shift += 7
    raise DecodeError("malformed varuint (exceeds 10 bytes)")


def _scalar_width(type_name: str) -> int:
    """Byte width of one fixed-width scalar element, used to validate an
    array payload's exact remaining length after its count varuint."""
    if type_name == "bool":
        return 1
    return struct.calcsize(_SCALAR_STRUCT_FORMATS[type_name])


def pack_array_of_scalar(type_name: str, values: list, max_elements: int) -> bytes:
    """Encodes a bounded array of scalar values, per the BRF spec's Array
    Encoding section: an unsigned LEB128 element count, followed by each
    element tightly packed via pack_scalar with no padding or per-element
    framing (fixed-width elements need none). Raises EncodeError if
    len(values) exceeds max_elements, or if any element itself fails to
    encode (propagated from pack_scalar unchanged).
    """
    if len(values) > max_elements:
        raise EncodeError(
            f"array length {len(values)} exceeds max_elements={max_elements}")
    buffer = bytearray()
    append_varuint(buffer, len(values))
    for value in values:
        buffer.extend(pack_scalar(type_name, value))
    return bytes(buffer)


def unpack_array_of_scalar(type_name: str, data: bytes, max_elements: int) -> list:
    """Decodes a bounded array of scalar values.

    Rejects a count exceeding max_elements before materializing the list,
    and rejects a payload whose remaining bytes (after the count varuint)
    are not exactly count * element_width -- both per the BRF spec's Array
    Encoding section's decoder requirements. This single length check
    catches truncated payloads, malformed counts, and trailing bytes alike
    (any of those makes the remaining length disagree with the expected
    count * element_width).
    """
    count, offset = read_varuint(data, 0)
    if count > max_elements:
        raise DecodeError(f"array count {count} exceeds max_elements={max_elements}")
    element_width = _scalar_width(type_name)
    remaining = data[offset:]
    expected_length = count * element_width
    if len(remaining) != expected_length:
        raise DecodeError(
            f"array payload has {len(remaining)} byte(s) after the count, expected "
            f"exactly {expected_length} for {count} {type_name} element(s)")
    values = []
    for index in range(count):
        start = index * element_width
        values.append(unpack_scalar(type_name, remaining[start:start + element_width]))
    return values


def pack_array_of_enum(enum_cls, type_name: str, values: list, max_elements: int) -> bytes:
    """Encodes a bounded array of enum values -- identical framing to
    pack_array_of_scalar, delegating each element to pack_enum (which
    already validates membership, so an invalid element value raises
    EncodeError with no extra code needed here).
    """
    if len(values) > max_elements:
        raise EncodeError(
            f"array length {len(values)} exceeds max_elements={max_elements}")
    buffer = bytearray()
    append_varuint(buffer, len(values))
    for value in values:
        buffer.extend(pack_enum(enum_cls, value, type_name))
    return bytes(buffer)


def unpack_array_of_enum(enum_cls, type_name: str, data: bytes, max_elements: int) -> list:
    """Decodes a bounded array of enum values -- identical framing and
    length validation to unpack_array_of_scalar, delegating each element
    to unpack_enum (which already raises DecodeError for a decoded integer
    the enum does not define, with no extra code needed here).
    """
    count, offset = read_varuint(data, 0)
    if count > max_elements:
        raise DecodeError(f"array count {count} exceeds max_elements={max_elements}")
    element_width = _scalar_width(type_name)
    remaining = data[offset:]
    expected_length = count * element_width
    if len(remaining) != expected_length:
        raise DecodeError(
            f"array payload has {len(remaining)} byte(s) after the count, expected "
            f"exactly {expected_length} for {count} {type_name} element(s)")
    values = []
    for index in range(count):
        start = index * element_width
        values.append(unpack_enum(enum_cls, type_name, remaining[start:start + element_width]))
    return values


def _read_array_varuint(data: bytes, offset: int, label: str) -> tuple[int, int]:
    """Reads an array count/element length while retaining its byte offset."""
    try:
        return read_varuint(data, offset)
    except DecodeError as error:
        raise DecodeError(f"malformed {label} varuint at byte offset {offset}: {error}") from error


def pack_array_of_string(values: list[str], max_elements: int, max_bytes: int) -> bytes:
    """Encodes a bounded array of UTF-8 string elements.

    The array count and every element length are varuints; each element's
    bytes are produced by pack_string, so UTF-8 and encoded-byte bounds remain
    centralized in the existing string helper.
    """
    if len(values) > max_elements:
        raise EncodeError(
            f"array length {len(values)} exceeds max_elements={max_elements}")
    buffer = bytearray()
    append_varuint(buffer, len(values))
    for index, value in enumerate(values):
        try:
            encoded = pack_string(value, max_bytes)
        except EncodeError as error:
            raise EncodeError(f"array element {index}: {error}") from error
        append_varuint(buffer, len(encoded))
        buffer.extend(encoded)
    return bytes(buffer)


def unpack_array_of_string(data: bytes, max_elements: int, max_bytes: int) -> list[str]:
    """Decodes a bounded array of length-delimited UTF-8 string elements."""
    count, offset = _read_array_varuint(data, 0, "array count")
    if count > max_elements:
        raise DecodeError(f"array count {count} exceeds max_elements={max_elements}")
    values = []
    for index in range(count):
        length_offset = offset
        element_length, offset = _read_array_varuint(data, offset, "element length")
        if element_length > max_bytes:
            raise DecodeError(
                f"array element {index} length {element_length} exceeds max_bytes={max_bytes} "
                f"at byte offset {length_offset}")
        if element_length > len(data) - offset:
            raise DecodeError(
                f"truncated array element {index} payload at byte offset {offset}: "
                f"declared {element_length} byte(s), only {len(data) - offset} remain")
        element_end = offset + element_length
        try:
            values.append(unpack_string(data[offset:element_end], max_bytes))
        except DecodeError as error:
            raise DecodeError(
                f"array element {index} at byte offset {offset}: {error}") from error
        offset = element_end
    if offset != len(data):
        raise DecodeError(f"trailing bytes in array payload at byte offset {offset}")
    return values


def pack_array_of_bytes(values: list[bytes], max_elements: int, max_bytes: int) -> bytes:
    """Encodes a bounded array of arbitrary byte-string elements."""
    if len(values) > max_elements:
        raise EncodeError(
            f"array length {len(values)} exceeds max_elements={max_elements}")
    buffer = bytearray()
    append_varuint(buffer, len(values))
    for index, value in enumerate(values):
        try:
            encoded = pack_bytes(value, max_bytes)
        except EncodeError as error:
            raise EncodeError(f"array element {index}: {error}") from error
        append_varuint(buffer, len(encoded))
        buffer.extend(encoded)
    return bytes(buffer)


def unpack_array_of_bytes(data: bytes, max_elements: int, max_bytes: int) -> list[bytes]:
    """Decodes a bounded array of arbitrary byte-string elements."""
    count, offset = _read_array_varuint(data, 0, "array count")
    if count > max_elements:
        raise DecodeError(f"array count {count} exceeds max_elements={max_elements}")
    values = []
    for index in range(count):
        length_offset = offset
        element_length, offset = _read_array_varuint(data, offset, "element length")
        if element_length > max_bytes:
            raise DecodeError(
                f"array element {index} length {element_length} exceeds max_bytes={max_bytes} "
                f"at byte offset {length_offset}")
        if element_length > len(data) - offset:
            raise DecodeError(
                f"truncated array element {index} payload at byte offset {offset}: "
                f"declared {element_length} byte(s), only {len(data) - offset} remain")
        element_end = offset + element_length
        try:
            values.append(unpack_bytes(data[offset:element_end], max_bytes))
        except DecodeError as error:
            raise DecodeError(
                f"array element {index} at byte offset {offset}: {error}") from error
        offset = element_end
    if offset != len(data):
        raise DecodeError(f"trailing bytes in array payload at byte offset {offset}")
    return values


def encode_record(record_id: int, fields: list[tuple[int, bytes]]) -> bytes:
    """Encodes a complete BRF record: 16-byte header + Field Directory + Payload.

    `fields` is an iterable of (field_index, value_bytes) pairs for
    present fields only. Field order does not need to already be sorted
    by field_index -- this function sorts and validates uniqueness itself,
    mirroring quarry::runtime::encode_record_result's own defensive
    behavior, even though generated code always calls this with fields
    already in increasing field_index order.
    """
    ordered = sorted(fields, key=lambda item: item[0])
    for index in range(1, len(ordered)):
        if ordered[index][0] == ordered[index - 1][0]:
            raise EncodeError(f"duplicate field index: {ordered[index][0]}")
    if len(ordered) > _MAX_DIRECTORY_ENTRIES:
        raise EncodeError(
            f"too many present fields ({len(ordered)}); at most "
            f"{_MAX_DIRECTORY_ENTRIES} are representable in one BRF record")

    directory = bytearray()
    payload = bytearray()
    for field_index, value_bytes in ordered:
        if not 0 <= field_index <= 0xFF:
            raise EncodeError(f"field index out of range: {field_index}")
        field_offset = len(payload)
        directory.append(field_index)
        append_varuint(directory, field_offset)
        append_varuint(directory, len(value_bytes))
        payload.extend(value_bytes)

    payload_length = len(directory) + len(payload)
    if payload_length > 0xFFFFFFFF:
        raise EncodeError("encoded record payload exceeds the 32-bit payloadLength bound")

    try:
        header = _HEADER_STRUCT.pack(_HEADER_VERSION, 0, len(ordered), 0, record_id, 0,
                                     payload_length)
    except struct.error as error:
        raise EncodeError(f"invalid record header values: {error}") from error

    return bytes(header) + bytes(directory) + bytes(payload)


def parse_record(data: bytes) -> tuple[int, dict[int, bytes]]:
    """Parses and structurally validates a complete BRF record.

    Returns (record_id, fields) where `fields` maps field_index -> bytes
    for each present field. Raises DecodeError for any structural
    violation the BRF spec's "Validation Rules" section requires decoders
    to reject: truncated header, unsupported version, nonzero
    flags/reserved fields, a payload-length mismatch (including trailing
    bytes after a valid record), a truncated or malformed Field Directory,
    an unsorted or duplicate field index, a field range outside the
    payload, or overlapping field ranges.
    """
    if len(data) < _HEADER_SIZE:
        raise DecodeError("truncated record header")

    (header_version, flags, directory_entry_count, reserved0, record_id, reserved1,
     payload_length) = _HEADER_STRUCT.unpack(data[:_HEADER_SIZE])

    if header_version != _HEADER_VERSION:
        raise DecodeError(f"unsupported header version: {header_version}")
    if flags != 0:
        raise DecodeError(f"unsupported flags: {flags}")
    if reserved0 != 0:
        raise DecodeError("reserved0 header field must be zero")
    if reserved1 != 0:
        raise DecodeError("reserved1 header field must be zero")

    body = data[_HEADER_SIZE:]
    if payload_length != len(body):
        raise DecodeError(
            f"payloadLength ({payload_length}) does not match remaining input "
            f"({len(body)} bytes) -- truncated or trailing bytes")

    offset = 0
    entries = []
    for _ in range(directory_entry_count):
        if offset >= len(body):
            raise DecodeError("truncated field directory")
        field_index = body[offset]
        offset += 1
        field_offset, offset = read_varuint(body, offset)
        field_length, offset = read_varuint(body, offset)
        if entries:
            if entries[-1][0] == field_index:
                raise DecodeError(f"duplicate field index: {field_index}")
            if entries[-1][0] > field_index:
                raise DecodeError("field directory entries are not sorted by field index")
        entries.append((field_index, field_offset, field_length))

    payload = body[offset:]
    fields = {}
    ranges = []
    for field_index, field_offset, field_length in entries:
        end = field_offset + field_length
        if field_offset > len(payload) or field_length > len(payload) or end > len(payload):
            raise DecodeError(
                f"field {field_index} range [{field_offset}, {end}) is outside the "
                f"{len(payload)}-byte payload")
        ranges.append((field_offset, end))
        fields[field_index] = payload[field_offset:end]

    sorted_ranges = sorted(ranges)
    for index in range(1, len(sorted_ranges)):
        if sorted_ranges[index - 1][1] > sorted_ranges[index][0]:
            raise DecodeError("overlapping field payload ranges")

    return record_id, fields


def _brf_v2_presence_offset(field_index: int) -> tuple[int, int]:
    if field_index < 0:
        raise ValueError(f"field index must be non-negative: {field_index}")
    return field_index // 8, field_index % 8


def _brf_v2_set_presence(bitmap: bytearray, field_index: int) -> None:
    byte_index, bit_index = _brf_v2_presence_offset(field_index)
    if byte_index >= len(bitmap):
        raise EncodeError(f"field index {field_index} is outside the presence bitmap")
    bitmap[byte_index] |= 1 << bit_index


def _brf_v2_is_present(bitmap: bytes, field_index: int) -> bool:
    byte_index, bit_index = _brf_v2_presence_offset(field_index)
    return byte_index < len(bitmap) and (bitmap[byte_index] & (1 << bit_index)) != 0


def _brf_v2_validate_field_metadata(field_metadata, fixed_end: int) -> None:
    previous_index = -1
    for field_index, byte_offset, slot_size, storage, presence_bit in field_metadata:
        if field_index <= previous_index:
            raise DecodeError("BRF v2 field metadata is not declaration ordered")
        previous_index = field_index
        if byte_offset < _BRF_V2_HEADER_SIZE or slot_size < 0:
            raise DecodeError(f"invalid BRF v2 field slot for field {field_index}")
        if byte_offset > fixed_end or slot_size > fixed_end - byte_offset:
            raise DecodeError(f"BRF v2 field {field_index} slot is outside the fixed region")
        if storage not in (_BRF_V2_STORAGE_FIXED, _BRF_V2_STORAGE_INLINE_FIXED_NESTED,
                           _BRF_V2_STORAGE_VARIABLE_DESCRIPTOR):
            raise DecodeError(f"invalid BRF v2 storage kind for field {field_index}")
        if presence_bit < 0:
            raise DecodeError(f"invalid BRF v2 presence bit for field {field_index}")
        if storage == _BRF_V2_STORAGE_VARIABLE_DESCRIPTOR and slot_size != _BRF_V2_DESCRIPTOR_SIZE:
            raise DecodeError(f"BRF v2 variable field {field_index} has an invalid descriptor size")


def encode_record_v2(record_id: int, fixed_region_size: int, presence_bitmap_size: int,
                     fields) -> bytes:
    """Builds a canonical BRF v2 record from layout-described field payloads.

    Each field is ``(field_index, absolute_byte_offset, slot_size, storage_kind,
    presence_bit, payload)``.
    Layout metadata supplies every fixed location; this function only appends variable payloads
    in declaration order and fills their descriptors.
    """
    if not 0 <= record_id <= 0xFFFFFFFF or not 0 <= fixed_region_size <= 0xFFFFFFFF:
        raise EncodeError("BRF v2 header value is outside its 32-bit range")
    if presence_bitmap_size < 0:
        raise EncodeError("presence bitmap size must be non-negative")
    # The canonical Layout IR's fixed_region_size includes the presence bitmap:
    # it is the complete byte span immediately following the 16-byte header.
    fixed_end = _BRF_V2_HEADER_SIZE + fixed_region_size
    if fixed_end > 0xFFFFFFFF:
        raise EncodeError("BRF v2 fixed-region arithmetic overflow")
    ordered = list(fields)
    metadata = [(item[0], item[1], item[2], item[3], item[4]) for item in ordered]
    _brf_v2_validate_field_metadata(metadata, fixed_end)
    fixed = bytearray(fixed_end - _BRF_V2_HEADER_SIZE)
    bitmap = bytearray(presence_bitmap_size)
    tail = bytearray()
    previous_index = -1
    for field_index, byte_offset, slot_size, storage, presence_bit, payload in ordered:
        if field_index <= previous_index:
            raise EncodeError("BRF v2 fields must be declaration ordered")
        previous_index = field_index
        if not isinstance(payload, (bytes, bytearray)):
            raise EncodeError(f"field {field_index} payload must be bytes")
        payload = bytes(payload)
        slot = byte_offset - _BRF_V2_HEADER_SIZE
        if storage in (_BRF_V2_STORAGE_FIXED, _BRF_V2_STORAGE_INLINE_FIXED_NESTED):
            if len(payload) != slot_size:
                raise EncodeError(f"field {field_index} payload has wrong fixed slot size")
            fixed[slot:slot + slot_size] = payload
        else:
            data_offset = fixed_end + len(tail)
            if data_offset > 0xFFFFFFFF or len(payload) > 0xFFFFFFFF:
                raise EncodeError("BRF v2 variable payload exceeds 32-bit bounds")
            fixed[slot:slot + _BRF_V2_DESCRIPTOR_SIZE] = struct.pack(
                ">II", data_offset, len(payload))
            tail.extend(payload)
        _brf_v2_set_presence(bitmap, presence_bit)
    record_size = fixed_end + len(tail)
    if record_size > 0xFFFFFFFF:
        raise EncodeError("BRF v2 record exceeds the 32-bit record-size bound")
    header = _BRF_V2_HEADER_STRUCT.pack(_BRF_V2_VERSION, 0, _BRF_V2_HEADER_SIZE,
                                        record_id, fixed_region_size, record_size)
    return header + bytes(bitmap) + bytes(fixed[presence_bitmap_size:]) + bytes(tail)


def parse_record_v2(data: bytes, expected_record_id: int, fixed_region_size: int,
                    presence_bitmap_size: int, field_metadata) -> dict[int, bytes]:
    """Validates and extracts fields from a canonical BRF v2 record.

    ``field_metadata`` contains ``(field_index, absolute_byte_offset, slot_size,
    storage_kind, presence_bit)`` entries in schema declaration order. Variable descriptors are
    checked for contiguous declaration-ordered tail spans; zero-length values may
    share the current tail offset.
    """
    if len(data) < _BRF_V2_HEADER_SIZE:
        raise DecodeError("truncated BRF v2 record header")
    version, flags, header_size, record_id, encoded_fixed_size, record_size = \
        _BRF_V2_HEADER_STRUCT.unpack(data[:_BRF_V2_HEADER_SIZE])
    if version != _BRF_V2_VERSION:
        raise DecodeError(f"unsupported BRF v2 header version: {version}")
    if flags != 0:
        raise DecodeError(f"unsupported BRF v2 flags: {flags}")
    if header_size != _BRF_V2_HEADER_SIZE:
        raise DecodeError(f"unsupported BRF v2 header size: {header_size}")
    if record_id != expected_record_id:
        raise DecodeError(f"unexpected record id: {record_id} (expected {expected_record_id})")
    if encoded_fixed_size != fixed_region_size:
        raise DecodeError("BRF v2 fixed-region size does not match the schema layout")
    if record_size != len(data):
        raise DecodeError("BRF v2 record size does not match the input length")
    fixed_end = _BRF_V2_HEADER_SIZE + fixed_region_size
    if fixed_end > record_size:
        raise DecodeError("BRF v2 fixed region exceeds the record")
    bitmap = data[_BRF_V2_HEADER_SIZE:_BRF_V2_HEADER_SIZE + presence_bitmap_size]
    if presence_bitmap_size and len(bitmap) > 0:
        declared_fields = len(field_metadata)
        unused = presence_bitmap_size * 8 - declared_fields
        if unused > 0 and bitmap[-1] & ((0xFF << (8 - unused)) & 0xFF):
            raise DecodeError("unused BRF v2 presence bits must be zero")
    _brf_v2_validate_field_metadata(field_metadata, fixed_end)
    variable_region_start = fixed_end
    result = {}
    cursor = variable_region_start
    for field_index, byte_offset, slot_size, storage, presence_bit in field_metadata:
        slot = data[byte_offset:byte_offset + slot_size]
        present = _brf_v2_is_present(bitmap, presence_bit)
        if not present:
            if any(slot):
                raise DecodeError(f"absent BRF v2 field {field_index} has nonzero storage")
            continue
        if storage in (_BRF_V2_STORAGE_FIXED, _BRF_V2_STORAGE_INLINE_FIXED_NESTED):
            result[field_index] = bytes(slot)
            continue
        data_offset, byte_length = struct.unpack(">II", slot)
        if data_offset != cursor:
            raise DecodeError("BRF v2 variable objects are not contiguous and declaration ordered")
        if data_offset > record_size or byte_length > record_size - data_offset:
            raise DecodeError(f"BRF v2 field {field_index} variable range is outside the record")
        result[field_index] = data[data_offset:data_offset + byte_length]
        cursor = data_offset + byte_length
    if cursor != record_size:
        raise DecodeError("unexpected BRF v2 trailing bytes after the variable region")
    return result
