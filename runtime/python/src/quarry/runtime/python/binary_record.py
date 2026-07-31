"""Quarry Python runtime: BRF scalar codec primitives.

Covers bool, fixed-width signed/unsigned integers, float32/float64, varuint
I/O, and whole-record (16-byte header + Field Directory + Payload)
assembly/parsing -- the runtime surface PR-119's scalar field milestone
needs. Mirrors the byte-for-byte wire behavior of
quarry/runtime/binary_record.hpp (C++) and quarry/runtime_c/binary_record.h
(C) for that same subset. Built entirely on the standard library (`struct`
for big-endian packing/range-checked scalar conversion), per this project's
"do not hand-write integer packing if the stdlib already provides it" rule.

No enum, string, bytes, array, or nested-record support exists here yet --
see docs/design/python-backend.md for the roadmap.
"""

import struct

_HEADER_VERSION = 1
_HEADER_SIZE = 16
_HEADER_STRUCT = struct.Struct(">BBBBIII")
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
