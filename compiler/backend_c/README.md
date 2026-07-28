# Backend (C)

**Status: scalar codec vertical slice (PR-108). Scalar primitive fields only —
no enum fields, strings, bytes, arrays, or nested records yet.** See
`docs/design/c-backend.md` for the full proposed design this is an increment
of, and `jira/backlog.md`'s PR-107/PR-108 entries for what was built when and
why.

Owns the C language generator: Schema IR -> generated `.h`/`.c`, plus real
BRF encode/decode for the currently-supported field subset.

Current C generation behavior:

* exposes `CodegenOptions`, `GeneratedFile`, `PlannedGeneratedFile`,
  `GenerationPlan`, `PlanResult`, and `CodegenResult` -- the same public
  shape convention `compiler/backend/backend.hpp` uses, independently
  defined in `quarry::compiler::backend_c` rather than shared with it
* emits one `.h`/`.c` pair for every namespace that directly owns records or
  enums, the same "emit files only for namespaces that directly own records
  or enums" rule the C++ backend follows -- independently re-derived, not
  shared code
* `PlannedGeneratedFile` carries both `relative_header_path` and
  `relative_source_path` together, so the planner always knows both paths up
  front; nothing later infers one from the other. `Backend::plan()` and
  `Backend::generate()` share one internal `build_generation_plan()`
  function, so `--list-outputs` and actual generation cannot diverge
* derives a C symbol prefix from the namespace FQN by replacing `.` with `_`
  and appending a trailing `_` (e.g. `quarry.telemetry` ->
  `quarry_telemetry_`), matching
  `docs/architecture/language-generators.md`'s existing C namespace-mapping
  specification
* generates one C `enum { ... }` block per `EnumIR`, with every enumerator
  fully uppercase and namespace-prefixed (`QUARRY_TELEMETRY_STATUS_OK`);
  rejects (fails generation with a diagnostic) any enum value outside the
  32-bit signed integer range, since a plain C `enum`'s underlying type is
  implementation-defined and this backend does not commit to a wider,
  guaranteed-width representation
* **enum declarations render; enum-typed record fields do not.** A record
  field whose type references an enum still fails generation with the same
  "unsupported field type" diagnostic as string/bytes/array/record-reference
  fields -- see "Supported field types" below

## Supported field types (PR-108)

Record fields are supported if and only if their Schema IR type is one of
the primitive scalars: `bool`, `i8`/`u8`/`i16`/`u16`/`i32`/`u32`/`i64`/`u64`,
`f32`, `f64` -- exactly the scalar set Schema IR's `PrimitiveType` enumerates
and the C++ backend already supports; no new schema types were invented to
support this.

**Any other field type fails generation with a diagnostic naming the record
and field** (e.g. `backend_c: field 'quarry.telemetry.Sample.label' has a
type the C backend does not support yet`). **A record with a mix of
supported and unsupported fields fails as a whole** -- there is no partial
generation that silently drops the unsupported field. Deferred to later
increments: enum-typed fields, `string`, `bytes`, arrays, nested-record
fields, arrays of records.

## Generated public data model

For a record with fields, e.g. `quarry.telemetry.Sample { count: uint32;
ratio: float32 }`:

```c
typedef struct {
    bool has_count;
    uint32_t count;
    bool has_ratio;
    float ratio;
} quarry_telemetry_Sample_t;
```

* struct members are declared directly (no opaque handle, no heap, no
  private/hidden state) in schema declaration order -- not reordered for
  memory-layout optimization, since predictability relative to the schema
  outranks a memory saving this project has no demonstrated need for yet
  (the same "no speculative work without demonstrated need" bar this
  project has applied consistently, e.g. PR-101). Compiler-inserted padding
  between members is expected and is explicitly sanctioned by
  `docs/specifications/binary-record-format.md`'s "binary layout is
  independent of host CPU alignment rules and programming language
  structure layout" -- the in-memory struct layout is not the wire layout
  and was never meant to match it byte-for-byte.
* presence is tracked by an adjacent `bool has_<field>` member per field,
  the direct C translation of the C++ backend's `std::optional<T>` presence
  tracking (C has no `std::optional`)
* fixed-width C types come from `<stdint.h>` (`int8_t` .. `uint64_t`); `bool`
  comes from `<stdbool.h>` and is used consistently for both the scalar
  `bool` field type and every `has_<field>` presence flag
* a record with **zero** fields (still a valid, encodable record) renders an
  empty-shell struct with a single `uint8_t reserved;` member, needed only
  because ISO C forbids an empty struct body; it is not a schema field, uses
  no leading underscore (avoiding any identifier the C standard reserves for
  the implementation), and disappears once the record has real fields
* every public identifier is schema-derived and prefixed
  (`quarry_<namespace>_<Record>...`); nothing is emitted under a reserved
  name

Every record additionally gets:

```c
void quarry_telemetry_Sample_init(quarry_telemetry_Sample_t* record);
size_t quarry_telemetry_Sample_encoded_size(const quarry_telemetry_Sample_t* record);

typedef struct { quarry_c_status_t status; size_t bytes_written; }
    quarry_telemetry_Sample_encode_result_t;
quarry_telemetry_Sample_encode_result_t quarry_telemetry_Sample_encode(
    const quarry_telemetry_Sample_t* record, uint8_t* output, size_t output_capacity);

typedef struct {
    quarry_c_status_t status;
    quarry_telemetry_Sample_t value;
    bool has_byte_offset;
    size_t byte_offset;
} quarry_telemetry_Sample_decode_result_t;
quarry_telemetry_Sample_decode_result_t quarry_telemetry_Sample_decode(
    const uint8_t* input, size_t input_length);
```

### Generated codec API design

Considered three shapes for the generated encode/decode signatures:

**A. Status return plus output parameters** (e.g.
`quarry_c_status_t quarry_telemetry_Sample_encode(const T* record, uint8_t*
output, size_t output_capacity, size_t* out_bytes_written)`). Rejected as
the *primary* API: works, but scatters what is conceptually one result
(status + byte count) across a return value and an out-parameter, and every
caller needs to remember the out-parameter is only meaningful on success.

**B. A result struct containing status and byte counts.** **Selected.**
`{status, bytes_written}` for encode and `{status, value, has_byte_offset,
byte_offset}` for decode keeps one call, one result, self-documenting field
names, and is directly analogous in spirit to the C++ backend's
`CodecResult` (a value/status bundle) while using plain C aggregate
initialization/assignment instead of `std::optional` or templates C doesn't
have.

**C. Separate status and diagnostic output objects** (a status enum plus a
distinct "diagnostics" struct populated only on failure). Rejected as
unnecessary indirection for this slice's diagnostic surface (status +
optional byte offset) -- two objects to keep in sync for less information
than option B carries in one.

The API supports: `_encoded_size()` for pre-sizing a caller-owned buffer
(field presence varies the exact size, so this cannot be a compile-time
constant); insufficient-capacity detection
(`QUARRY_C_STATUS_INSUFFICIENT_CAPACITY`); malformed/truncated input
detection (the full structural status set from
`include/quarry/runtime_c/binary_record.h`); `bytes_written` on encode
success; and `byte_offset` on decode failures where one is meaningful (see
below).

**This is not full parity with C++'s `CodecResult` yet, and that is
deliberate, not an oversight.** `CodecResult` also carries a `path`
(`std::vector<PathElement>`) locating a failure inside nested records or
array elements. This slice has no nesting and no arrays -- there is nothing
for a path to describe yet, so `quarry_telemetry_Sample_decode_result_t` has
no `path` member at all rather than an always-empty placeholder one. Path
support is expected to arrive together with nested-record/array field
support, not before it is needed.

`byte_offset` (`has_byte_offset`/`byte_offset` on the decode result) is
populated for every decode failure **except**
`QUARRY_C_STATUS_UNSUPPORTED_FIELD_COUNT`, which is this runtime's own
bounded-entry-count limitation (see
`include/quarry/runtime_c/binary_record.h`), not a wire-format defect with a
meaningful byte position. `QUARRY_C_STATUS_UNEXPECTED_RECORD_ID` (a
schema-level check the generated decoder itself performs, not the shared
runtime) reports offset `0`, matching the C++ backend's identical
`unexpected_record_id` behavior. Encode results never carry a byte offset,
for the same reason the C++ runtime's `EncodeResult` never does: every
encode error is a schema-value violation against a value the caller already
holds, not a wire-position problem (see `runtime/README.md`'s "Byte-Offset
Context").

## Generated-code API version (C)

This PR introduces the first C generated code that depends on a public C
runtime contract (`include/quarry/runtime_c/binary_record.h`), so a
generated-code API compatibility epoch is warranted now, unlike PR-107's
architectural skeleton (which called no runtime function and had nothing to
guard). `QUARRY_GENERATED_CODE_API_VERSION_C` is a new, independent CMake
scalar (currently `1`) -- **not** a reuse or bump of the existing C++
`QUARRY_GENERATED_CODE_API_VERSION` -- single-sourced into both the public
`quarry/runtime_c/version.h` (`#define QUARRY_C_GENERATED_CODE_API_VERSION`)
and this backend's private
`generated_code_api_version_c.hpp` (`kGeneratedCodeApiVersionC`), the same
two-sided pattern the C++ backend already uses for its own epoch. Generated
C headers that contain records emit a compile-time check:

```c
#if QUARRY_C_GENERATED_CODE_API_VERSION != 1U
#error "Generated Quarry C code is incompatible with the installed Quarry C runtime. ..."
#endif
```

C99 has no `static_assert`; `#if`/`#error` is the portable equivalent and
needs no compiler extension. Kept independent from the C++ epoch because the
two languages' generator/runtime contracts can (and likely will) change on
different schedules -- conflating them would force an artificial version
bump to one language whenever only the other actually changed.

## Runtime

`include/quarry/runtime_c/binary_record.h` (installed as part of
`Quarry::runtime_c`) is the shared, schema-neutral C runtime this backend
generates calls into: bounded writer/reader state, big-endian scalar
read/write (`quarry_c_write_u32`, etc., matching
`docs/specifications/binary-record-format.md`'s mandatory big-endian byte
order exactly), unsigned LEB128 varuint I/O, and whole-record
assembly/parsing (`quarry_c_encode_record`/`quarry_c_parse_record`/
`quarry_c_find_field`) -- the same "generic byte mechanics in the runtime,
schema-specific decisions in generated code" split
`docs/principles.md`'s "Compile-Time Knowledge" principle already states in
language-neutral terms. See `runtime_c/CMakeLists.txt` and
`include/quarry/runtime_c/binary_record.h`'s own header comment for the
runtime's scope and constraints (C99, no heap, caller-owned buffers, no
global state, no platform-endian assumptions).

Allowed dependencies:

* `compiler/schema_ir`
* `compiler/support`

This layer must not parse source, resolve names, perform semantic analysis,
compute layout, or assign compiler-managed identifiers -- the same
constraint `compiler/backend/README.md` states for the C++ backend. It must
also not depend on `compiler/backend` (the C++ backend): the two are
independent sibling implementations that happen to consume the same Schema
IR, not one backend built on top of the other.
