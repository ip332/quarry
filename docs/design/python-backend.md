# Python Backend

## Implementation Status

PR-118 established the Python backend architecture. `compiler/backend_python/` is a real,
independent CMake target that consumes Schema IR and produces real,
importable Python packages through the same `Backend`/`CodegenOptions`/
`plan()`/`generate()` shape and CLI (`--language python`) integration the C
and C++ backends already use. It supported zero-field records only, with
The implementation now covers the planned core field set described below;
historical milestone notes remain for traceability.

PR-119 implemented the first **functional** Python backend milestone:
scalar field support. Records may now declare any of the eleven supported
scalar types (`bool`, `int8`/`uint8`/`int16`/`uint16`/`int32`/`uint32`/
`int64`/`uint64`, `float32`/`float64`); private `_quarry_` helpers perform
real BRF encode/decode via a new runtime
module, `quarry.runtime.python.binary_record`, built entirely on the
standard library's `struct` module. Verified byte-for-byte wire-compatible
with the C and C++ backends for the same field values (see
`tests/interop/python_cpp_c_codec_interop_test.cpp`). At that milestone,
enum, string, bytes, and supported fixed-width scalar/enum array fields were
implemented; later milestones added nested records and record arrays.

PR-120 added enum field support. A field may now reference a
same-namespace enum whose declared values are all non-negative; the enum
renders as a real `enum.IntEnum` subclass, and `pack_enum`/`unpack_enum`
(two small additions to `binary_record.py`) validate membership and
delegate to the existing scalar pack/unpack for the enum's wire width.
An enum-only namespace (no records) now emits a file too, resolving a
limitation PR-118/PR-119 had documented. String, bytes, nested-record, and
supported array fields are implemented; nested arrays and cross-namespace
enum/record references remain unsupported and fail generation with a
diagnostic.

PR-121 added string and bytes field support, using the BRF spec's
existing "Variable-Length Data Encoding" rules unchanged: bounded
(`max_bytes`-limited) `str` and `bytes` dataclass fields, encoded/decoded
via four new `binary_record.py` functions (`pack_string`/`unpack_string`/
`pack_bytes`/`unpack_bytes`) that deliberately reuse Python's own
`str.encode("utf-8")`/`bytes.decode("utf-8")` for UTF-8 validation rather
than hand-rolling a validator the way the C++/C runtimes must. Nested
records and unsupported array element categories are historical milestones;
the current supported subset is described below.

PR-123 added bounded arrays of bounded `string` and `bytes` elements. The
existing BRF variable-width array framing is reused unchanged, and the
Python runtime delegates element validation to the existing string/bytes
helpers. The current source-schema frontend does not expose per-element
`max_bytes` for array fields, so these element shapes are exercised from the
validated Schema IR/backend boundary until that separate frontend contract is
addressed; no compiler-pipeline or Schema IR change is part of PR-123.

PR-124 added same-namespace nested record fields. Generated dataclasses use
the referenced generated record type, and generated record helpers compose
the child's existing encode/decode helpers. Records are rendered in
dependency order; same-namespace record arrays use the existing variable-width
array framing and child helpers. Nested arrays and cross-namespace record
references remain unsupported. No runtime helper was needed.

This document supersedes, for implementation purposes, the investigation
notes captured in this repository's PR-117 (native Python backend
architecture) and PR-118A (encode/decode API boundary) working reports;
where this document and those investigations agree, this document is
authoritative going forward.

---

## Purpose

Prove that Quarry's backend architecture generalizes to a third,
independent language backend without any compiler-pipeline or Schema IR
change, and without depending on either existing backend
(`compiler/backend/`, the C++ backend; `compiler/backend_c/`, the C
backend). `compiler/backend_python/` depends only on
`quarry_compiler_schema_ir` and `quarry_project_options`, exactly like
`backend_c` -- confirming `docs/backend-api.md`'s guarantee that "new
backends may be added without modifying the compiler pipeline" for a third,
structurally different language, not just a second one.

---

## Architecture

### Backend shape

`compiler/backend_python/backend_python.hpp` mirrors `backend_c.hpp`'s
shape, adapted for Python's simpler, single-file-per-namespace model
(closer to the C++ backend's single-`relative_output_path`
`PlannedGeneratedFile` than to C's header/source pair, since Python has
neither a header/source split nor an `#include` mechanism):

```cpp
struct CodegenOptions {
    std::string output_directory = "generated";
    std::string root_module_stem = "schema";
};

struct GeneratedFile { std::string path; std::string content; };
struct PlannedGeneratedFile { std::string relative_output_path; };
struct GenerationPlan { std::vector<PlannedGeneratedFile> files; };
struct PlanResult { bool success = true; std::string error_message; GenerationPlan plan; };
struct CodegenResult { bool success = true; std::string error_message; std::vector<GeneratedFile> files; };

class Backend {
public:
    PlanResult plan(const schema_ir::SchemaIrModel&, const CodegenOptions&) const;
    CodegenResult generate(const schema_ir::SchemaIrModel&, const CodegenOptions&) const;
};
```

`Backend::plan()` and `Backend::generate()` share one internal
`build_generation_plan()`, the same discipline `compiler/backend/backend.cpp`
and `compiler/backend_c/backend_c.cpp` already document, so the two modes
cannot diverge.

### Namespace mapping -- the one structurally different piece

C's file-planning model is deliberately *flat*: a namespace FQN's segments
are joined with `/` to form a single file's relative path (e.g.
`quarry.telemetry` -> `quarry/telemetry.generated.h`), where the last
segment doubles as the file name. This works because C has no real
directory-based package/import mechanism to satisfy -- `/` in the path is
purely cosmetic grouping.

Python's import system has no such luxury: a dotted namespace can only
become a real, importable Python package if every segment is backed by an
actual directory containing its own `__init__.py`. So the Python backend's
namespace mapping is genuinely different from both existing backends, not
an incidental implementation detail:

* Namespace FQN `acme.telemetry` -> directories `acme/__init__.py`,
  `acme/telemetry/__init__.py`, **plus** that namespace's own generated
  module *one level further in*: `acme/telemetry/schema.py` (using
  `CodegenOptions::root_module_stem`, default `"schema"`) -- an extra
  segment beyond the namespace path itself, so a namespace's own records
  never collide with a child namespace's own package directory.
* The root (synthetic, unnamed) namespace's module lives directly under
  the output directory with no wrapping package at all: `schema.py`,
  mirroring the root-namespace-has-no-directory-prefix precedent both
  existing backends already establish for their own root file stems.
* Sibling/cousin namespaces sharing a common ancestor (e.g.
  `acme.telemetry` and `acme.control`) legitimately contribute the *same*
  ancestor `__init__.py` path more than once; these are collected into a
  `std::set<std::string>` so expected overlap is silently deduplicated.
  Two different namespaces producing the same *module* path would instead
  be a genuine error (defensively checked, mirroring `backend_c`'s own
  duplicate-output-path guard), though this cannot currently happen since
  namespace FQNs are unique and the module path is a pure function of the
  FQN.
* Record class names use the bare record name (e.g. `Sample`), not
  namespace-prefixed like C's `quarry_telemetry_Sample_t` -- Python's own
  package/module structure already disambiguates.

### Scope: scalar, enum, string, bytes, arrays, and nested record fields

`namespace_emits_file()` for Python is `ns.records_size() > 0 ||
ns.enums_size() > 0`: a namespace emits a module if it owns records,
enums, or both. Bounded arrays of scalar, same-namespace non-negative-valued
enum, bounded string, bounded bytes, and same-namespace record elements are
supported, as are same-namespace nested record fields; nested arrays remain
unsupported.

### Scalar field lowering

`lower_scalar_field_type()` independently re-derives the same eleven
scalar-primitive mapping `compiler/backend_c/backend_c.cpp`'s own
`lower_scalar_field_type()` uses (not shared, per this backend's
established convention), producing a `runtime_type_name` string (e.g.
`"uint32"`, passed directly to `binary_record.pack_scalar()`/
`unpack_scalar()`) and a `python_type_hint` (`"bool"`, `"int"`, or
`"float"`, used for the dataclass field's type annotation) per field. Any
field whose type is not one of these eleven, and not a supported enum
reference (see below), fails generation with a diagnostic naming the
record and field:

```
backend_python: field '<record-fqn>.<field-name>' has a type the Python
backend does not support yet -- only bool, fixed-width signed/unsigned
integer, f32/f64 scalar fields, same-namespace non-negative-valued enum
fields, bounded string/bytes fields, bounded arrays of scalar, enum, string,
or bytes elements, same-namespace nested record fields, and same-namespace
record arrays are supported (see docs/design/python-backend.md); nested arrays
remain unsupported
```

A record/class that silently dropped an unsupported field would be
partial, misleading output; this backend follows the project's existing
"do not emit partial code" convention instead. A record with **zero**
fields remains fully supported (and, since PR-119, has genuinely working
-- not stubbed -- encode/decode: an empty field list and a decode that
expects no known field indices).

### Enum field lowering

PR-120 scope: a field may reference an enum declared in the **same
namespace** as the referencing record, and only if every value the enum
declares is non-negative -- matching the BRF spec's Enum Encoding rule and
the C++ backend's own field-support boundary (`compiler/backend/
backend.cpp`'s `runtime_enum_encoding`). Unlike `compiler/backend_c/
backend_c.cpp`'s enum catalog, Python's does **not** also cap declared
values to the 32-bit signed integer range -- that cap is backend_c's own
narrower implementation choice, not a BRF-wide restriction, and Python's
width bucketing (`enum_width_type_name_for_max_value`, mirroring both
existing backends' identical `enum_width_for_max_value`) already covers
every value up to `uint64::max` cleanly. A whole-schema `EnumCatalog`
(built once via `collect_enum_catalog`, mirroring `backend_c`'s identical
in spirit but independently-implemented catalog) resolves each enum-typed
field's target by `ir_id`, recording the enum's bare class name (e.g.
`"Status"` -- no namespace prefix, unlike C's symbol-prefixed constants,
since Python's own package/module structure already disambiguates) and
its wire width type name (`"uint8"`/`"uint16"`/`"uint32"`/`"uint64"`,
passed to `pack_enum`/`unpack_enum`).

A cross-namespace enum reference fails generation:

```
backend_python: field '<record-fqn>.<field-name>' references enum
'<EnumName>' declared in a different namespace ('<other-namespace>');
cross-namespace enum field references are not yet supported (see
docs/design/python-backend.md)
```

An enum with any negative declared value fails generation for any field
referencing it:

```
backend_python: field '<record-fqn>.<field-name>' references an enum with
a negative declared value; enum fields are only supported when every
declared value is non-negative, matching the BRF spec's Enum Encoding rule
(see docs/design/python-backend.md)
```

### String/bytes field lowering

PR-121 scope: `string` and `bytes` fields, using the BRF spec's existing
"Variable-Length Data Encoding" rules unchanged -- no new wire-format
decisions were needed. Schema validation
(`compiler/semantic/semantic.cpp`'s `validate_positive_u32`) already
guarantees every string/bytes field's `max_bytes` is present, positive,
and fits `uint32_t` before `backend_python` ever sees it, exactly as both
existing backends already rely on -- nothing is re-validated at
lowering time. A `string` field becomes `python_type_hint = "str"`; a
`bytes` field becomes `python_type_hint = "bytes"`. Both carry their
`max_bytes` bound through to the generated `pack_string`/`unpack_string`/
`pack_bytes`/`unpack_bytes` calls (see "Generated API" and "Runtime
boundary" below) -- there is no wire-level length prefix specific to
these field types to plan around (the Field Directory's own `fieldLength`
already supplies the byte count, per the BRF spec).

### Array field lowering

PR-122 supports bounded arrays whose elements are fixed-width scalar
primitives or same-namespace, non-negative-valued enum references. PR-123
extends the same lowering to bounded `string` and `bytes` element types. The
element lowering reuses the scalar and enum lowering decisions, including
wire width, Python type hint, namespace restrictions, and enum membership
validation, while string/bytes element `max_bytes` is carried by the element
Schema IR node. Array `max_elements` is already validated by the semantic
layer as a positive `uint32`, so the backend passes it directly to the
generated runtime calls.

Arrays render as `Optional[list[T]]`. A present array, including an empty
array, is encoded as a Field Directory field whose payload starts with an
unsigned LEB128 element count. Fixed-width arrays then use tightly packed
element bytes. String and bytes arrays add one unsigned LEB128 byte length and
exactly that many raw element bytes per element. The runtime rejects counts
above `max_elements`, per-element lengths above `max_bytes`, malformed or
truncated varuint/payload data, malformed UTF-8, and trailing bytes.

---

## Generated API

For a record with scalar fields, the backend emits (illustrated with one
`bool` and one `uint32` field; a single blank line separates the import
from the class and separates each method within the class; two blank
lines, PEP8's top-level-definition convention, separate the class from the
first module-level helper and separate each helper from the next):

```python
from dataclasses import dataclass
from typing import Optional

@dataclass
class Sample:
    active: Optional[bool] = None
    count: Optional[int] = None

    def encode(self):
        return _encode_sample(self)

    @classmethod
    def decode(cls, data):
        return _decode_sample(data)

    def encoded_size(self):
        return _encoded_size_sample(self)


def _encode_sample(value):
    fields = []
    if value.active is not None:
        fields.append((0, _brf.pack_scalar("bool", value.active)))
    if value.count is not None:
        fields.append((1, _brf.pack_scalar("uint32", value.count)))
    return _brf.encode_record(1, fields)


def _decode_sample(data):
    record_id, fields = _brf.parse_record(data)
    if record_id != 1:
        raise _brf.DecodeError(
            f"unexpected record id: {record_id} (expected 1)")
    active = None
    if 0 in fields:
        active = _brf.unpack_scalar("bool", fields[0])
    count = None
    if 1 in fields:
        count = _brf.unpack_scalar("uint32", fields[1])
    return Sample(active=active, count=count)


def _encoded_size_sample(value):
    return len(_encode_sample(value))
```

(`_brf` is `quarry.runtime.python.binary_record`, imported once per module
-- see "Runtime boundary" below.) A zero-field record emits the same
shape with an empty field list and no field-checking lines: `_encode_x`
becomes `fields = []` followed directly by `return
_brf.encode_record(<id>, fields)`, and `_decode_x` becomes the record-id
check followed directly by `return X()` -- genuinely working code, not a
stub, since encoding/decoding zero fields is just the empty case of the
same real logic.

The public API is the three dataclass methods; the leading-underscore
module-level functions are implementation details, per PR-118A's
methods-with-internal-free-function-delegation recommendation, wired end
to end: each method's single line of implementation is a delegating call
to its corresponding helper (`return _encode_sample(self)`, etc.). Every
scalar field becomes an `Optional[<hint>] = None` dataclass attribute
(PR-117's decided absent/present-via-`None` representation) -- `hint` is
`bool`, `int`, or `float` depending on the field's declared scalar type.
`_quarry_encoded_size_<name>` is implemented as
`len(_quarry_encode_<name>(value))`:
always exactly correct by construction, at the cost of a full encode to
learn a size (unlike the C/C++ backends' size-only computation) -- an
acceptable simplicity/performance tradeoff for this first functional
milestone, revisitable later without changing the public API.

Helper function names use a PascalCase -> snake_case conversion (`Sample`
-> `sample`, `SensorReading` -> `sensor_reading`): an underscore is
inserted before each uppercase letter immediately following a lowercase
letter or digit, then the whole string is lowercased. This is a
deliberately simple heuristic -- see "Known limitations" for its
acronym-handling gap.

A namespace with multiple records repeats this block once per record,
separated by the same single blank line used within the template (a
judgment call for a case the literal single-record example doesn't show).

### Array fields

PR-122 supports arrays of fixed-width scalar and same-namespace enum
elements. They render as `Optional[list[T]] = None` and use the BRF array
count-prefix encoding:

```python
@dataclass
class Sample:
    readings: Optional[list[float]] = None
    statuses: Optional[list[Status]] = None

def _encode_sample(value):
    fields = []
    if value.readings is not None:
        fields.append((0, _brf.pack_array_of_scalar(
            "float32", value.readings, 4)))
    if value.statuses is not None:
        fields.append((1, _brf.pack_array_of_enum(
            Status, "uint8", value.statuses, 3)))
```

The payload begins with an unsigned LEB128 element count followed by
tightly packed fixed-width element bytes. Empty arrays are present values
and encode as a zero count; `None` continues to mean absent. Decoding checks
the count against `max_elements` before creating the result list and requires
exact payload consumption.

### Enum fields

An enum-typed field renders as `enum.IntEnum`, one class per declared
enum, always emitted **before** any record in the same file:

```python
from enum import IntEnum

class Status(IntEnum):
    OK = 0
    WARNING = 1
    ERROR = 2


@dataclass
class Sample:
    status: Optional[Status] = None
    ...

def _encode_sample(value):
    fields = []
    if value.status is not None:
        fields.append((0, _brf.pack_enum(Status, value.status, "uint8")))
    ...

def _decode_sample(data):
    ...
    status = None
    if 0 in fields:
        status = _brf.unpack_enum(Status, "uint8", fields[0])
    return Sample(status=status, ...)
```

The enum must be rendered before the record: a dataclass field annotation
(`Optional[Status] = None`) evaluates eagerly when the class body
executes (there is no `from __future__ import annotations` here), so
`Status` must already exist in the module namespace at that point. This
needs no topological sort the way nested records need in the C/C++
backends -- an enum is a simple leaf value collection, never itself
embedding a field, so there is no cycle to detect and same-namespace
scope keeps every reference resolvable within one file. If a namespace
declares only enums (no records), the module contains just the enum
class(es) and none of the `dataclass`/`typing` imports or record
machinery.

`from enum import IntEnum` is emitted only when the file declares at
least one enum (unlike `from typing import Optional`, which is always
emitted regardless of whether any field needs it, since every dataclass
field -- scalar or enum -- uses `Optional[...]`).

### String/bytes fields

A `string` field becomes `Optional[str] = None`; a `bytes` field becomes
`Optional[bytes] = None`. Both encode/decode helpers carry the field's
declared `max_bytes` bound as a literal argument:

```python
@dataclass
class Sample:
    label: Optional[str] = None
    blob: Optional[bytes] = None
    ...

def _encode_sample(value):
    fields = []
    if value.label is not None:
        fields.append((0, _brf.pack_string(value.label, 16)))
    if value.blob is not None:
        fields.append((1, _brf.pack_bytes(value.blob, 16)))
    ...

def _decode_sample(data):
    ...
    label = None
    if 0 in fields:
        label = _brf.unpack_string(fields[0], 16)
    blob = None
    if 1 in fields:
        blob = _brf.unpack_bytes(fields[1], 16)
    return Sample(label=label, blob=blob, ...)
```

No new imports are needed for string/bytes fields -- `str` and `bytes`
are Python builtins, unlike `Optional` or `IntEnum`.

### String/bytes arrays

PR-123 arrays use the same public dataclass shape, with the element bound
carried in the generated helper call:

```python
@dataclass
class Sample:
    labels: Optional[list[str]] = None
    blobs: Optional[list[bytes]] = None

def _encode_sample(value):
    fields = []
    if value.labels is not None:
        fields.append((0, _brf.pack_array_of_string(value.labels, 3, 8)))
    if value.blobs is not None:
        fields.append((1, _brf.pack_array_of_bytes(value.blobs, 2, 4)))
    ...
```

`None` omits the Field Directory entry; `[]` emits the present zero-count
payload `b"\x00"`. Each string element is encoded with `str.encode("utf-8")`
and bounded by encoded byte length. Bytes elements are copied verbatim.

### Nested record fields

PR-124 nested fields use the referenced generated dataclass directly:

```python
@dataclass
class Parent:
    child: Optional[Child] = None
    count: Optional[int] = None

def _encode_parent(value):
    fields = []
    if value.child is not None:
        fields.append((0, _encode_child(value.child)))
    ...

def _decode_parent(data):
    ...
    if 0 in fields:
        child = _decode_child(fields[0])
```

The child value is an embedded complete BRF record. `None` omits the field;
an empty child record is present and remains distinguishable. The planner
topologically orders same-namespace record declarations so annotations refer
to already-defined dataclasses. Child `parse_record` validation rejects
truncated, malformed, wrong-record, and trailing-byte nested payloads.

---

## Runtime boundary and compatibility epoch

`runtime/python/` is a small, independently pip-installable package
(`quarry-runtime-python` on PyPI; `pyproject.toml` + `src/quarry/runtime/
python/`), importable as `quarry.runtime.python`. `__init__.py` exposes
exactly one symbol:

```python
QUARRY_GENERATED_CODE_API_VERSION_PYTHON = 1
```

Since PR-119, a sibling module,
`quarry/runtime/python/binary_record.py`, implements the real BRF codec
mechanics generated code needs -- the Python analog of
`quarry/runtime/binary_record.hpp` (C++) and `quarry/runtime_c/
binary_record.h` (C), covering the same scalar-field subset:

* `pack_scalar(type_name, value)` / `unpack_scalar(type_name, data)` --
  big-endian encode/decode for `"bool"` and the ten fixed-width
  integer/float type names, built on the standard library's `struct`
  module rather than hand-rolled bit shifting (`struct.pack`/`unpack`
  with an explicit `">"` big-endian format code). `struct.pack`'s own
  range checking on a fixed-width format code (e.g. `"B"` for `uint8`) is
  reused directly as the Python-specific scalar range check C++/C get for
  free from their native fixed-width types.
* `append_varuint(buffer, value)` / `read_varuint(data, offset)` --
  unsigned LEB128, matching the BRF spec's Varuint Encoding section byte
  for byte.
* `encode_record(record_id, fields)` / `parse_record(data)` -- whole-record
  (16-byte header + Field Directory + Payload) assembly/parsing, with the
  same structural validation the C++/C runtimes perform: header version,
  zero flags/reserved fields, exact payload-length match (catching both
  truncation and trailing bytes), Field Directory sort/duplicate/overlap
  checks, and field-range-within-payload checks.
* `EncodeError` / `DecodeError` -- plain `Exception` subclasses raised for
  any encode/decode failure. Generated code and callers alike catch these
  directly; there is no C++/C-style `CodecResult<T, E>` value type, since
  Python's own exception mechanism is the idiomatic fit PR-118A's
  API-boundary investigation already anticipated.

Since PR-120, two more small functions cover enum fields -- deliberately
kept minimal (validate-then-delegate, no new wire-format logic, no new
`struct` usage), per this PR's "do not expand the runtime unless
genuinely required" instruction:

* `pack_enum(enum_cls, value, type_name)` -- validates `value` is a
  member of `enum_cls` by constructing `enum_cls(value)` (which also
  accepts a raw int matching a defined member, using the stdlib's own
  `IntEnum` constructor as the validation, per PR-117 §8's finding that it
  already raises `ValueError` for undefined values with no extra
  validation code needed), converting that `ValueError` to `EncodeError`,
  then delegating to `pack_scalar` for the actual big-endian encoding.
* `unpack_enum(enum_cls, type_name, data)` -- delegates to `unpack_scalar`
  to decode the underlying integer, then constructs `enum_cls` from it,
  converting a `ValueError` (a decoded integer the enum does not define)
  to `DecodeError` -- matching the BRF spec's requirement that an
  undefined decoded enum value is a decode failure.

Since PR-121, four more functions cover `string`/`bytes` fields, per the
BRF spec's "Variable-Length Data Encoding" / "string" / "bytes" sections
(no internal length prefix -- the Field Directory's own `fieldLength`
supplies the byte count):

* `pack_string(value, max_bytes)` -- encodes `value` via Python's own
  `str.encode("utf-8")` (no hand-rolled UTF-8 validator, unlike the
  C++/C runtimes, per this PR's explicit "do not duplicate UTF-8
  validation already provided by Python" instruction), converting the one
  remaining failure mode (`UnicodeEncodeError`, raised for a lone
  surrogate code point a Python `str` can hold but that has no UTF-8
  encoding) to `EncodeError`, then checks the encoded length against
  `max_bytes`, raising `EncodeError` if it is exceeded.
* `unpack_string(data, max_bytes)` -- checks `len(data)` against
  `max_bytes` first (mirroring the C++/C runtimes' own
  bounds-check-before-content-validate ordering), then decodes via
  `bytes.decode("utf-8")`, converting `UnicodeDecodeError` to
  `DecodeError`. Python's strict-mode UTF-8 decoder already rejects
  overlong encodings, lone continuation bytes, truncated sequences, and
  encoded surrogate halves -- exactly what the BRF spec and the C++/C
  runtimes' custom validators require, with no extra code needed.
* `pack_bytes(value, max_bytes)` / `unpack_bytes(data, max_bytes)` -- the
  raw content verbatim (no UTF-8 validation at all, per the BRF spec's
  "bytes" section), with the same `max_bytes` bound check as the string
  functions.

Since PR-122, four small functions cover fixed-width arrays:

* `pack_array_of_scalar(type_name, values, max_elements)` /
  `unpack_array_of_scalar(type_name, data, max_elements)` -- encode and
  decode the count varuint and tightly packed scalar elements, enforcing the
  element-count bound and exact payload consumption.
* `pack_array_of_enum(enum_cls, type_name, values, max_elements)` /
  `unpack_array_of_enum(enum_cls, type_name, data, max_elements)` -- the same
  framing for enum elements, delegating per-element membership validation to
  `pack_enum`/`unpack_enum`.

Since PR-123, four more helpers cover variable-width arrays:

* `pack_array_of_string(values, max_elements, max_bytes)` /
  `unpack_array_of_string(data, max_elements, max_bytes)` -- encode and
  decode count-prefixed, length-delimited UTF-8 elements while reusing
  `pack_string`/`unpack_string` for validation.
* `pack_array_of_bytes(values, max_elements, max_bytes)` /
  `unpack_array_of_bytes(data, max_elements, max_bytes)` -- the same framing
  for arbitrary byte elements while reusing `pack_bytes`/`unpack_bytes`.

Malformed count or element-length varuints, count/element bounds violations,
truncated element data, malformed UTF-8, and trailing bytes raise the existing
`DecodeError`, with array index and byte-offset context included in the
message where the Python exception model permits. Record arrays compose the
same generated child codecs and framing; nested arrays remain unsupported.

### Generated-name safety

Schema-derived Python identifiers are sanitized deterministically. ASCII
letters, digits after the first position, and underscores are preserved;
other characters become underscores, leading digits receive a leading
underscore, and keywords receive a trailing underscore. Generator-reserved
names (public codec methods, runtime imports, dataclass/type names, codec
locals, and the `_quarry_` helper prefix) are rejected or escaped where safe.
Dunder and
sunder conventions are rejected. Any collision after normalization is a
diagnostic naming the source identifiers, generated target, and owner; no
numeric suffix is chosen arbitrarily. The same mapping is used for namespace
segments, module stems, record/enum classes, enum members, and dataclass
fields, so `--list-outputs` and generated imports remain deterministic.

Every generated module begins with an import-time compatibility check:

```python
from quarry.runtime.python import QUARRY_GENERATED_CODE_API_VERSION_PYTHON
from quarry.runtime.python import binary_record as _brf

if QUARRY_GENERATED_CODE_API_VERSION_PYTHON != 1:
    raise ImportError(
        "Generated Quarry Python code is incompatible with the installed "
        "Quarry Python runtime. Regenerate the code using a compatible "
        "quarry-schema-compiler release."
    )
```

This mirrors the *philosophy* of the C/C++ generated-code epoch guards
(fail loudly and specifically on a stale runtime pairing) without copying
their *mechanism* -- Python has no preprocessor or `static_assert`, so the
check runs at import time and raises `ImportError` instead of failing to
compile. The expected value (`1`) is a `constexpr` embedded in
`compiler/backend_python/backend_python.cpp`
(`kGeneratedCodeApiVersionPython`), independent from
`QUARRY_GENERATED_CODE_API_VERSION` (C++) and
`QUARRY_GENERATED_CODE_API_VERSION_C` (C) -- the Python generator/runtime
contract can change on its own schedule.

---

## Known limitations

* **Not CMake-driven, unlike C/C++.** `kGeneratedCodeApiVersionPython` in
  `backend_python.cpp` and `QUARRY_GENERATED_CODE_API_VERSION_PYTHON` in
  `runtime/python/src/quarry/runtime/python/__init__.py` are two
  independently-maintained literals that must be bumped together by hand.
  The C/C++ epochs avoid this by `configure_file()`-generating both the
  backend's and the runtime's copy from one shared CMake scalar; Python's
  runtime package deliberately sits outside the CMake graph entirely (see
  `docs/distribution-model.md`'s "Python Runtime Packaging" section), so
  the same trick doesn't directly apply. A follow-up PR could introduce a
  small shared source of truth (e.g. a generated `_version.py` the
  packaging step copies in) if manual drift becomes a real problem.
* **Limited array support.** Arrays of fixed-width scalar,
  same-namespace non-negative-valued enum, bounded string, and bounded bytes
  elements and same-namespace records are supported. The current source
  schema frontend does not expose per-element `max_bytes` for array fields;
  PR-123 therefore consumes that already-defined constraint at the validated
  Schema IR/backend boundary without changing the frontend pipeline.
* **No cross-namespace enum or record references.** Only same-namespace
  references are supported; a field referencing a type declared elsewhere
  fails generation with a diagnostic.
* **`float32` round-trips through Python's only floating-point type.**
  Python has no native single-precision float; `pack_scalar("float32",
  value)` always narrows a Python `float` (always double-precision) to
  IEEE 754 binary32 via `struct.pack`, and `unpack_scalar` widens it back.
  A value not already exactly representable in binary32 loses precision
  on encode -- inherent to using Python's one float type for both widths,
  not a bug, and no different in effect from what any binary32 field
  does in any language. `float64` has no such narrowing.
* **No Python-keyword escaping.** A record or field named `class`,
  `import`, etc. would currently produce invalid Python (e.g. `class:
  Optional[bool] = None`). Reachable now that field rendering exists;
  not yet addressed.
* **Simple snake_case heuristic.** `HTTPResponse` becomes
  `h_t_t_p_response`, not `http_response` -- acronym runs are not
  special-cased. Acceptable for this PR's narrow scope; revisit if
  real schemas hit this.
* **Namespace name collision with the runtime package is unaddressed.**
  If a schema's own top-level namespace segment is literally `quarry` (a
  common convention in this repository's own fixtures), its generated
  `quarry/__init__.py` could collide with `quarry.runtime.python`'s own
  `quarry/__init__.py` if both were ever placed on the same
  `sys.path` with real (non-namespace-package) `__init__.py` files at the
  shared `quarry` segment. Not exercised by this PR's tests (which use a
  distinct `acme.*` test namespace precisely to sidestep this), and not a
  concern for the isolated verification use cases PR-118/PR-119/PR-120/PR-121
  target, but worth resolving (e.g. via PEP 420 namespace packages at the shared
  prefix, or reserving a different runtime package name) before real
  multi-namespace production use.
* **No re-exports in generated `__init__.py` files.** Every generated
  `__init__.py` (both ancestor-package markers and the runtime's own) is
  a near-empty file with only a one-line docstring; there is no
  `from .schema import Sample`-style convenience re-export yet, so callers
  must import the concrete module (`acme.telemetry.schema.Sample`), not
  the package (`acme.telemetry.Sample`).
* **`_quarry_encoded_size_<name>` always performs a full encode.** Correct by
  construction, but not the size-only computation the C/C++ backends do.
  Acceptable for now; revisit only if profiling shows it matters.

---

## Implementation roadmap

Sequencing sketch (subject to revision as each PR reveals concrete
problems), continuing PR-117 §11's sketch:

1. PR-118: establish the independent backend architecture and generated
   package shape. Done.
2. PR-119: scalar field support (bool, fixed-width integers, f32/f64)
   plus real runtime codec mechanics in `runtime/python/` (varuint,
   header/Field Directory assembly, `struct`-based scalar pack/unpack),
   verified byte-for-byte wire-compatible with C/C++. Done.
3. PR-120: enum support (`enum.IntEnum`, matching PR-117 §8's
   finding that `IntEnum`'s constructor already raises `ValueError` for
   undefined values with no extra validation code needed), same-namespace
   and non-negative-valued-only, verified byte-for-byte wire-compatible
   with C/C++. Done.
4. PR-121: string/bytes field support, using the BRF spec's
   existing variable-length encoding rules unchanged, with UTF-8
   validation delegated entirely to Python's own `str`/`bytes` codec
   methods, verified byte-for-byte wire-compatible with C/C++. Done.
5. PR-122: bounded arrays of fixed-width scalar and same-namespace enum
   elements, using the BRF count-prefix encoding and exact payload checks.
   Done.
6. PR-123: bounded arrays of strings and bytes, using count-plus-element-length
   varuint framing, UTF-8/arbitrary-byte validation, per-element bounds, and
   exact payload checks. Done.
7. PR-124: same-namespace nested record fields. Done.
8. PR-125: same-namespace arrays of records. Done.
9. PR-127: deterministic Python identifier escaping and collision diagnostics;
   stale skeleton metadata removed. Done.
10. Cross-namespace enum/record references, if a concrete need is
   demonstrated (currently out of scope for every backend, not just
   Python's).
11. Clean wheel/sdist and downstream installation validation (PR-128).

---

## Testing

`tests/backend_python/backend_python_test.cpp` covers backend
registration, output planning, package/`__init__.py` layout (including
sibling-namespace ancestor deduplication), zero-field dataclass
generation, exact-template verification, helper-function snake_case
naming, method-to-helper delegation, epoch-check preamble presence and
placement, the unsupported-field-type failure diagnostic (its
representative unsupported-type example has moved as each PR added
support for the previous one: `count`/`u32` -> `label`/`string` in
PR-119, `label`/`string` -> nested-record `item`/`record` in PR-122, mirroring the
same swap pattern the C backend's own test history established), all
eleven scalar types generating successfully, generated encode/decode
helper text referencing the runtime correctly, plan/generate agreement,
and generation determinism. Since PR-120, it also covers: enum class
generation and its ordering before any referencing record; enum wire
width selection for the smallest unsigned type covering the max declared
value; the cross-namespace-enum-field diagnostic; the
negative-enum-value diagnostic; and an enum-only namespace now emitting a
file (fixing the PR-118/PR-119-documented limitation). Since PR-121, it
also covers string/bytes dataclass field generation and generated
encode/decode helper text referencing `pack_string`/`unpack_string`/
`pack_bytes`/`unpack_bytes` with the correct `max_bytes` values.
Since PR-122, it also covers scalar and enum array annotations and generated
array helper calls. Since PR-124, nested-record annotations and helper
delegation are covered; since PR-125, record-array annotations and framing are
covered, with nested arrays remaining the representative unsupported type.
`tests/tools/schema_compiler_tool_test.cpp` adds `--language python`
coverage (list-outputs, determinism, `--file-extension` rejection,
generated content).

Since PR-119, three more test layers exist:

* `runtime/python/tests/test_binary_record.py` -- a stdlib `unittest`
  suite exercising `binary_record.py` in isolation (scalar round trips and
  boundary values for all eleven types, range-check rejection, varuint
  round trips and malformed-varuint rejection, whole-record encode/decode
  round trips, and every structural rejection case: truncated header,
  bad version, nonzero flags/reserved, payload-length mismatch, truncated
  directory, unsorted/duplicate directory entries, out-of-range field
  ranges, overlapping ranges, truncated field values, and trailing bytes.
  Since PR-120: `pack_enum`/`unpack_enum` round trips (by member and by a
  raw int matching a member), wire-identity with `pack_scalar` for the
  same width/value, rejection of a value not defined by the enum on both
  encode and decode, and a wider (`uint32`) width round trip. Since
  PR-121: string/bytes round trips (including empty and maximum-length
  values, embedded U+0000, and a lone-surrogate encode rejection),
  over-length rejection on both encode and decode, malformed-UTF-8
  rejection on decode, non-str/non-bytes value rejection on encode, and
  bytes fields never validating UTF-8 (round-tripping arbitrary binary
  content unchanged). Since PR-122, it also covers scalar arrays for all
  fixed-width element kinds, enum arrays, empty arrays, count bounds,
  malformed/truncated counts, exact payload consumption, and enum
  membership rejection.
  Since PR-123, it also covers string/bytes arrays, including empty elements,
  multibyte UTF-8, arbitrary binary data, per-element bounds, malformed
  element lengths, truncation, malformed UTF-8, and trailing bytes.
  `tests/backend_python/python_runtime_test.cpp` runs this suite via a
  real `python3 -m unittest` subprocess so `ctest` catches runtime
  regressions automatically.
* `tests/backend_python/python_execution_test.cpp` invokes the real
  `quarry-schema-compiler` binary to generate a package, then a real
  `python3` subprocess to: round-trip a zero-field record through real
  (not stubbed) encode/decode; round-trip a scalar record with
  representative values across all eleven types and assert the exact
  expected wire bytes; confirm an absent field decodes as `None`;
  confirm an out-of-range scalar value raises `EncodeError` at encode
  time; confirm truncated/trailing-byte/garbage input raises
  `DecodeError` at decode time; confirm an epoch mismatch still raises
  `ImportError` at import time; since PR-120, round-trip an enum field
  (including the raw-int-matching-a-member case and absence alongside an
  enum field), confirm an enum value the schema does not define raises
  `EncodeError` at encode time, and confirm a decoded byte the schema
  does not define raises `DecodeError` at decode time; and, since PR-121,
  round-trip string/bytes fields (empty-present, absent, maximum-length,
  and bytes-never-validates-UTF-8 cases), confirm an over-length
  string/bytes value raises `EncodeError` at encode time, and confirm
  malformed UTF-8 in a string field raises `DecodeError` at decode time; and,
  since PR-122, execute scalar/enum array round trips, empty-vs-absent array
  handling, array-bound rejection, and enum-membership rejection. Since
  PR-123, it generates the variable-width array module from direct validated
  Schema IR and executes it with a real Python interpreter, covering
  string/bytes arrays and their empty-vs-absent semantics.
  Since PR-124, it also executes nested dataclasses with present, absent, and
  empty child records, and verifies truncated and trailing embedded-record
  payloads raise `DecodeError`.
* `tests/interop/python_cpp_c_codec_interop_test.cpp` -- the genuinely new
  kind of test PR-119 added: generates one schema (covering all eleven
  supported scalar types, a same-namespace enum field since PR-120,
  bounded string/bytes fields since PR-121, and fixed-width scalar/enum
  arrays since PR-122) through the C++, C, and
  Python backends, compiles small C and C++ harnesses and writes a Python
  harness script, and verifies all three encoders produce byte-for-byte
  identical output for identical field values (the string field's value
  deliberately includes a non-ASCII character to exercise real UTF-8
  encoding, not just ASCII), all three decoders accept bytes produced by
  either of the other two languages, all three identically reject a
  truncated buffer and identically reject extra trailing bytes, all three
  identically reject a decoded enum byte the schema does not define
  (since PR-120), and all three identically reject malformed UTF-8 in the
  string field (since PR-121) -- the two corruption cases locate their
  target byte by searching for the string field's own known plaintext
  bytes rather than hand-computing a payload offset, so the test does not
  need updating if the schema's field list changes again.
  Since PR-123, a separate direct validated-Schema-IR case covers the
  variable-width string/bytes array shape through the C++ and Python
  backends, including byte-for-byte encoding, cross-decoding, and trailing
  payload rejection. The C backend still rejects these array element kinds,
  so three-way coverage remains a separate future C-backend increment.
  Since PR-124, the same interoperability target adds a direct validated-
  Schema-IR three-way C/C++/Python nested-record case, verifying identical
  bytes, all pairwise cross-decoding, and rejection of trailing bytes inside
  the embedded child record.
