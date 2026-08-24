"""Native, schema-driven read-only QBS/BRF runtime.

This module deliberately does not use generated codecs.  QBS metadata is
decoded once and validated BRF views retain the caller's backing buffers.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import math
import struct
from typing import Iterator, Optional, Union


class GenericRuntimeError(Exception):
    """Base class for generic runtime failures."""


class QbsError(GenericRuntimeError):
    pass


class BrfError(GenericRuntimeError):
    pass


class ResourceLimitError(BrfError):
    pass


class TypeAccessError(GenericRuntimeError):
    pass


class TypeCode(IntEnum):
    BOOL = 1
    I8 = 2
    U8 = 3
    I16 = 4
    U16 = 5
    I32 = 6
    U32 = 7
    I64 = 8
    U64 = 9
    F32 = 10
    F64 = 11
    ENUM = 12
    STRING = 13
    BYTES = 14
    RECORD = 15
    ARRAY = 16


def _u16(b, p): return struct.unpack_from(">H", b, p)[0]
def _u32(b, p): return struct.unpack_from(">I", b, p)[0]
def _u64(b, p): return struct.unpack_from(">Q", b, p)[0]


def _utf8(data: bytes) -> str:
    try:
        return data.decode("utf-8", "strict")
    except UnicodeDecodeError as exc:
        raise QbsError("invalid UTF-8") from exc


@dataclass(frozen=True)
class QbsType:
    code: TypeCode
    fixed_size: bool
    encoded_width: int
    reference: int
    max_elements: int
    max_bytes: int

    @property
    def is_array(self): return self.code is TypeCode.ARRAY


@dataclass(frozen=True)
class QbsField:
    index: int
    type_index: int
    byte_offset: int
    bit_offset: int
    bit_width: int
    presence_bit: int
    slot_size: int
    storage: int
    descriptor_kind: int
    name: Optional[str]
    owner: "QbsRecord"

    @property
    def type(self): return self.owner.schema.types[self.type_index]


@dataclass(frozen=True)
class QbsRecord:
    schema: "QbsSchema"
    index: int
    record_id: int
    variable_size: bool
    presence_bitmap_size: int
    fixed_region_size: int
    complete_fixed_record_size: int
    identity: str
    name: Optional[str]
    field_start: int
    field_count: int

    @property
    def fields(self):
        return tuple(self.schema.fields[self.field_start:self.field_start + self.field_count])

    def field(self, key: Union[int, str]) -> QbsField:
        for field in self.fields:
            if (isinstance(key, int) and field.index == key) or (isinstance(key, str) and field.name == key):
                return field
        raise KeyError(key)


class QbsSchema:
    """Validated QBS v1 metadata retaining the original bytes."""

    def __init__(self, source, records, fields, types, enums, reflective):
        self.source = source
        self.records = tuple(records)
        self.fields = tuple(fields)
        self.types = tuple(types)
        self.enums = tuple(enums)
        self.reflective = reflective
        self._by_id = {r.record_id: r for r in records}
        self._by_name = {r.name: r for r in records if r.name is not None}

    @classmethod
    def from_bytes(cls, source, *, max_image_size=64 * 1024 * 1024):
        raw = bytes(source)
        if len(raw) < 40 or len(raw) > max_image_size or raw[:4] != b"QBS\0":
            raise QbsError("invalid QBS header")
        if raw[4:6] != b"\x01\x00" or _u16(raw, 6) != 40 or raw[8] != 1 or raw[9] not in (1, 2, 4) or raw[10] != 0 or raw[11] != 16 or _u16(raw, 30) != 0:
            raise QbsError("unsupported or invalid QBS header")
        total, count = _u32(raw, 36), _u16(raw, 28)
        if total != len(raw) or 40 + count * 12 > total:
            raise QbsError("invalid QBS section directory")
        sections = {}
        previous = 40 + count * 12
        for i in range(count):
            at = 40 + i * 12
            kind, flags, off, size = _u16(raw, at), _u16(raw, at + 2), _u32(raw, at + 4), _u32(raw, at + 8)
            if kind in sections or flags or kind == 0 or kind > 7 or off < previous or off + size > total:
                raise QbsError("invalid QBS section directory")
            sections[kind] = (off, size)
            previous = off + size
        if not all(k in sections for k in (1, 2, 3, 6)) or ((4 in sections) != (5 in sections)):
            raise QbsError("QBS missing required section")
        width = raw[9]
        if sections[6][1] and (sections[6][1] <= 256 and width != 1 or 256 < sections[6][1] <= 65536 and width != 2 or sections[6][1] > 65536 and width != 4):
            raise QbsError("non-canonical QBS identity width")
        iss_off, iss_size = sections[6]
        identity_starts, identities = set(), []
        p = 0
        while p < iss_size:
            start = p
            end = raw.find(b"\0", iss_off + p, iss_off + iss_size)
            if end < 0 or end == iss_off + p: raise QbsError("invalid QBS identity section")
            value = _utf8(raw[iss_off + p:end])
            identity_starts.add(start); identities.append(value); p = end - iss_off + 1
        def identity(off):
            if off not in identity_starts: raise QbsError("invalid QBS identity reference")
            q = raw.find(b"\0", iss_off + off, iss_off + iss_size)
            return _utf8(raw[iss_off + off:q])
        string_count = None
        string_values = []
        if 7 in sections:
            so, ss = sections[7]
            if ss < 4: raise QbsError("truncated QBS string table")
            string_count = _u32(raw, so)
            if 4 + 4 * (string_count + 1) > ss: raise QbsError("invalid QBS string table")
            data = so + 4 + 4 * (string_count + 1)
            for i in range(string_count):
                a, z = _u32(raw, so + 4 + i * 4), _u32(raw, so + 4 + (i + 1) * 4)
                if a > z or data + z > so + ss: raise QbsError("invalid QBS string range")
                string_values.append(_utf8(raw[data + a:data + z]))
        def name(index):
            if index == 0xffff: return None
            if string_count is None or index >= string_count: raise QbsError("invalid QBS string reference")
            return string_values[index]
        ro, rs = sections[1]; fo, fs = sections[2]; to, ts = sections[3]
        rstride, fstride, tstride = 28 + width, 28, 16
        if rs % rstride or fs % fstride or ts % tstride: raise QbsError("inconsistent QBS table size")
        records, fields, types = [], [], []
        for i in range(rs // rstride):
            at = ro + i * rstride; flags = _u16(raw, at + 10)
            field_start, field_count = _u32(raw, at + 4), _u16(raw, at + 8)
            if field_start + field_count > fs // fstride: raise QbsError("invalid QBS record fields")
            records.append((i, _u32(raw, at), field_start, field_count, bool(flags & 1), _u32(raw, at + 12), _u32(raw, at + 16), _u32(raw, at + 20), identity(int.from_bytes(raw[at + 24:at + 24 + width], "big")), name(_u16(raw, at + 24 + width))))
        for i in range(fs // fstride):
            at = fo + i * fstride; flags = _u16(raw, at + 2)
            fields.append([_u16(raw, at), _u16(raw, at + 14), _u32(raw, at + 4), _u16(raw, at + 8), _u32(raw, at + 10), _u16(raw, at + 16), _u32(raw, at + 20), flags & 3, (flags >> 2) & 1, name(_u16(raw, at + 24)), None])
        for i in range(ts // tstride):
            at = to + i * tstride; code = raw[at]
            if code == 0 or code > 16 or raw[at + 1] not in (1, 2): raise QbsError("invalid QBS type")
            types.append(QbsType(TypeCode(code), raw[at + 1] == 1, _u16(raw, at + 2), _u16(raw, at + 4), _u32(raw, at + 8), _u32(raw, at + 12)))
        enum_values = []
        enums = []
        if 4 in sections:
            eo, es = sections[4]; vo, vs = sections[5]; estride = 16 + width
            if es % estride or vs % 8: raise QbsError("invalid QBS enum table")
            all_values = [_u64(raw, vo + i * 8) for i in range(vs // 8)]
            for i in range(es // estride):
                at = eo + i * estride; start, n = _u32(raw, at + 4), _u32(raw, at + 8)
                if start + n > len(all_values): raise QbsError("invalid QBS enum values")
                enums.append((tuple(all_values[start:start+n]), identity(int.from_bytes(raw[at+12:at+12+width], "big")), name(_u16(raw, at+12+width))))
        schema = cls(raw, [], [], types, enums, string_count is not None)
        schema.records = tuple(QbsRecord(schema, *r[0:1], r[1], r[4], r[5], r[6], r[7], r[8], r[9], r[2], r[3]) for r in records)
        for f in fields:
            f[-1] = schema.records[next(i for i, r in enumerate(schema.records) if r.field_start <= len(fields) and len(fields) < r.field_start + r.field_count)] if False else None
        schema.fields = tuple(QbsField(*f[:-1], schema.records[0]) for f in fields) if schema.records else ()
        # Attach the owning record without copying wire metadata.
        mutable = []
        for r in schema.records:
            mutable.extend(QbsField(*fields[j][:-1], r) for j in range(r.field_start, r.field_start + r.field_count))
        schema.fields = tuple(mutable)
        # Records refer to global field indexes; rebuild field_start/count remain valid only if
        # the global order is retained, which it is above.
        schema._by_id = {r.record_id: r for r in schema.records}; schema._by_name = {r.name: r for r in schema.records if r.name}
        return schema

    def record_by_id(self, record_id):
        try: return self._by_id[record_id]
        except KeyError as exc: raise KeyError(record_id) from exc

    def record(self, name):
        try: return self._by_name[name]
        except KeyError as exc: raise KeyError(name) from exc


def load_qbs(qbs_bytes) -> QbsSchema:
    return QbsSchema.from_bytes(qbs_bytes)


@dataclass(frozen=True)
class BrfLimits:
    max_record_bytes: int = 64 * 1024 * 1024
    max_work_items: int = 1 << 20
    max_nested_records: int = 1024
    max_array_elements: int = 1 << 20


class ValidatedRecord:
    """Validated BRF record view.  Field states are per-record and stable."""
    def __init__(self, schema, record_schema, source, states, children):
        self.schema, self.record_schema, self.source = schema, record_schema, source
        self._states, self._children = states, children

    def is_present(self, field): return self._states[field.index][0]
    def field(self, key):
        f = self.record_schema.field(key)
        present, value = self._states[f.index]
        return value if present else None
    def field_view(self, key):
        f = self.record_schema.field(key)
        present, value = self._states[f.index]
        return FieldValue(f, present, value)
    def __getitem__(self, key): return self.field(key)
    def fields(self):
        for f in self.record_schema.fields:
            present, value = self._states[f.index]
            yield FieldValue(f, present, value)


@dataclass(frozen=True)
class FieldValue:
    field: QbsField
    present: bool
    value: object


class PrimitiveArrayView:
    """Lazy view over a validated fixed-width primitive array payload."""
    def __init__(self, schema, element_type, payload, count, data_offset):
        self._schema = schema
        self.element_type = element_type
        self._payload = payload
        self._count = count
        self._data_offset = data_offset

    def __len__(self): return self._count

    def __getitem__(self, index):
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(self._count))]
        if index < 0: index += self._count
        if index < 0 or index >= self._count: raise IndexError(index)
        width = self.element_type.encoded_width
        start = self._data_offset + index * width
        return _decode_scalar(self._schema, self.element_type, self._payload[start:start + width])

    def __iter__(self):
        for i in range(self._count): yield self[i]


class RecordArrayView:
    """Read-only sequence of already validated record elements."""
    def __init__(self, elements): self._elements = tuple(elements)
    def __len__(self): return len(self._elements)
    def __getitem__(self, index):
        if isinstance(index, slice): return list(self._elements[index])
        return self._elements[index]
    def __iter__(self): return iter(self._elements)


def _varuint(data, p):
    value = 0
    for i in range(10):
        if p >= len(data): raise BrfError("truncated array count")
        byte = data[p]; p += 1; value |= (byte & 0x7f) << (7 * i)
        if not byte & 0x80:
            if i and value < (1 << (7 * i)):
                raise BrfError("noncanonical varuint")
            return value, p
    raise BrfError("array count overflow")


def _decode_scalar(schema, t, data):
    if t.code is TypeCode.BOOL:
        if len(data) != 1 or data[0] > 1: raise BrfError("invalid bool")
        return bool(data[0])
    if t.code in (TypeCode.I8, TypeCode.I16, TypeCode.I32, TypeCode.I64): return int.from_bytes(data, "big", signed=True)
    if t.code is TypeCode.ENUM:
        value = int.from_bytes(data, "big")
        if t.reference >= len(schema.enums) or value not in schema.enums[t.reference][0]:
            raise BrfError("invalid enum")
        return value
    if t.code in (TypeCode.U8, TypeCode.U16, TypeCode.U32, TypeCode.U64): return int.from_bytes(data, "big")
    if t.code is TypeCode.F32: return struct.unpack(">f", data)[0]
    if t.code is TypeCode.F64: return struct.unpack(">d", data)[0]
    if t.code is TypeCode.STRING:
        try: return bytes(data).decode("utf-8", "strict")
        except UnicodeDecodeError as exc: raise BrfError("invalid UTF-8") from exc
    if t.code is TypeCode.BYTES: return memoryview(data)
    return data


@dataclass
class _Frame:
    schema: QbsSchema
    record_type: QbsRecord
    span: memoryview
    states: dict
    children: dict
    tail: int = 0
    cursor: int = 0
    pending: object = None


def _check_record_header(frame):
    data, record_type = frame.span, frame.record_type
    if len(data) < 16 or data[0] != 2 or data[1] != 0 or _u16(data, 2) != 16:
        raise BrfError("invalid BRF record header")
    if _u32(data, 4) != record_type.record_id or _u32(data, 12) != len(data):
        raise BrfError("invalid nested record identity or size")
    fixed = _u32(data, 8)
    if fixed != record_type.fixed_region_size or fixed > len(data) - 16 or record_type.presence_bitmap_size > fixed:
        raise BrfError("invalid nested fixed region")
    if record_type.presence_bitmap_size:
        used = 0
        for field in record_type.fields: used |= 1 << field.presence_bit
        bits = int.from_bytes(data[16:16 + record_type.presence_bitmap_size], "little")
        if bits & ~used: raise BrfError("invalid presence bits")
    frame.tail = 16 + fixed


def _child_spans(schema, element_type, payload, count, cursor, limits):
    """Return child spans while consuming array framing exactly once."""
    result = []
    child = schema.records[element_type.reference]
    variable = child.variable_size
    for _ in range(count):
        if variable:
            length, cursor = _varuint(payload, cursor)
            if length == 0 or length > len(payload) - cursor: raise BrfError("invalid variable array element")
            end = cursor + length; result.append(payload[cursor:end]); cursor = end
        else:
            length = child.complete_fixed_record_size
            if not length or length > len(payload) - cursor: raise BrfError("truncated fixed array element")
            result.append(payload[cursor:cursor + length]); cursor += length
    if cursor != len(payload): raise BrfError("record array has trailing bytes")
    return result


def validate_brf(schema: QbsSchema, record_type: QbsRecord, brf_bytes, limits=BrfLimits()):
    """Validate the complete nested BRF graph using an explicit frame stack."""
    root_source = memoryview(brf_bytes)
    if len(root_source) > limits.max_record_bytes: raise ResourceLimitError("BRF record limit exceeded")
    stack = [_Frame(schema, record_type, root_source, {}, {})]
    completed = {}
    work = 0
    while stack:
        frame = stack[-1]
        if frame.cursor == 0:
            _check_record_header(frame)
            frame.cursor = 1  # Header/presence are complete for this frame.
        if frame.pending is not None and frame.pending[3] == "record_array":
            key, element, spans, _ = frame.pending
            if spans:
                frame.pending = (key, element, spans[1:], "record_array")
                stack.append(_Frame(schema, schema.records[element.reference], spans[0], {}, {}))
                completed[id(stack[-1])] = (frame, key, "record_array")
            else:
                frame.pending = None
                values = frame.children.pop(key, [])
                frame.states[key] = (True, RecordArrayView(values))
            continue
        if frame.pending is not None:
            key, child_type, child_span, destination = frame.pending
            frame.pending = None
            stack.append(_Frame(schema, child_type, child_span, {}, {}))
            completed[id(stack[-1])] = (frame, key, destination)
            continue
        if frame.cursor == 1:
            if frame.cursor > 1: raise AssertionError("invalid validation phase")
            frame.cursor = 2
        if frame.cursor >= 2 and frame.cursor - 2 < frame.record_type.field_count:
            ordinal = frame.cursor - 2
            frame.cursor += 1
            work += 1
            if work > limits.max_work_items: raise ResourceLimitError("BRF work limit exceeded")
            field = frame.record_type.fields[ordinal]
            data = frame.span
            present = bool(data[16 + field.presence_bit // 8] & (1 << (field.presence_bit % 8)))
            # QBS byte_offset is relative to the complete BRF record span;
            # presence bits are the only fixed-region metadata addressed from
            # the header boundary.
            slot_start = field.byte_offset
            slot = data[slot_start:slot_start + field.slot_size]
            if len(slot) != field.slot_size: raise BrfError("field slot outside fixed region")
            if not present:
                if any(slot): raise BrfError("absent field storage is not zero")
                frame.states[field.index] = (False, None)
                continue
            value_data = slot
            if field.storage == 2:
                if len(slot) != 8: raise BrfError("invalid variable descriptor")
                off, size = _u32(slot, 0), _u32(slot, 4)
                if off != frame.tail or size > len(data) - off: raise BrfError("invalid variable range")
                value_data = data[off:off + size]; frame.tail += size
            typ = field.type
            if typ.code is TypeCode.RECORD:
                if len(stack) > limits.max_nested_records: raise ResourceLimitError("nested record limit exceeded")
                child = schema.records[typ.reference]
                if field.storage == 2 and not child.variable_size: raise BrfError("fixed child uses variable storage")
                if field.storage != 2 and (child.variable_size or field.slot_size != child.complete_fixed_record_size): raise BrfError("invalid fixed child slot")
                frame.pending = (field.index, child, value_data, "record")
                continue
            if typ.code is TypeCode.ARRAY:
                count, p = _varuint(value_data, 0)
                if count > typ.max_elements or count > limits.max_array_elements: raise ResourceLimitError("array element limit exceeded")
                element = schema.types[typ.reference]
                if element.code is TypeCode.RECORD:
                    spans = _child_spans(schema, element, value_data, count, p, limits)
                    frame.pending = (field.index, element, spans, "record_array")
                    frame.states[field.index] = (True, [])
                    # Array children are represented by a small pending list handled below.
                    frame.pending = (field.index, element, spans, "record_array")
                    continue
                width = element.encoded_width
                if p + count * width != len(value_data): raise BrfError("array payload has inconsistent length")
                for i in range(count): _decode_scalar(schema, element, value_data[p + i * width:p + (i + 1) * width])
                frame.states[field.index] = (True, PrimitiveArrayView(schema, element, value_data, count, p))
                continue
            frame.states[field.index] = (True, _decode_scalar(schema, typ, value_data))
            continue
        if frame.cursor >= 2 and frame.cursor - 2 >= frame.record_type.field_count:
            if frame.tail != len(frame.span): raise BrfError("noncanonical variable tail")
            value = ValidatedRecord(schema, frame.record_type, root_source, frame.states, frame.children)
            stack.pop()
            if not stack: return value
            parent, key, destination = completed.pop(id(frame))
            if destination == "record":
                parent.children[key] = value; parent.states[key] = (True, value)
            else:
                # record-array children are currently finalized by the array branch below.
                parent.children.setdefault(key, []).append(value)
            continue
    raise BrfError("validation did not complete")
