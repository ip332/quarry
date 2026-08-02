# Backend (C)

**Status: scalar, enum, bounded string, bounded bytes, bounded array (of
scalar, same-namespace-enum, bounded string, bounded bytes, or
same-namespace-record elements), and same-namespace nested record field
codec (PR-108/PR-109/PR-110/PR-111/PR-112/PR-113/PR-114/PR-131).
Arrays of string/bytes elements are now supported,
and cross-namespace references (plain enum/record fields, or as array
elements) remain unsupported.** See `docs/design/c-backend.md` for the
full proposed design this is an increment of, and `jira/backlog.md`'s
PR-107/PR-108/PR-109/PR-110/PR-111/PR-112/PR-113/PR-114 entries for what
was built when and why.

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
* generates one `typedef enum { ... } quarry_<namespace>_<EnumName>_t;`
  block per `EnumIR` (a real, named type -- an anonymous `enum { ... };`
  before PR-109, when nothing yet needed a type name to reference), with
  every enumerator fully uppercase and namespace-prefixed
  (`QUARRY_TELEMETRY_STATUS_OK`); rejects (fails generation with a
  diagnostic) any enum value outside the 32-bit signed integer range, since
  a plain C `enum`'s underlying type is implementation-defined and this
  backend does not commit to a wider, guaranteed-width representation
* **enum-typed record fields are supported when the referenced enum is
  declared in the same namespace as the record and every one of its
  declared values is non-negative** -- see "Supported field types" and
  "Enum fields" below. Cross-namespace enum field references and
  negative-valued enum fields still fail generation with a diagnostic.
* **bounded string fields are supported** -- see "Supported field types" and
  "String fields" below.
* **bounded bytes fields are supported** -- see "Supported field types" and
  "Bytes fields" below.
* **bounded arrays of scalar, same-namespace-enum, bounded string, or
  bounded bytes elements are
  supported** -- see "Supported field types" and "Array fields" below.
* **same-namespace nested record fields are supported** -- see "Supported
  field types" and "Nested record fields" below.
* **bounded arrays of same-namespace record elements are supported** --
  see "Supported field types" and "Record array fields" below. Cross-
  namespace record references (plain or array-element) remain unsupported.

## Supported field types (PR-108/PR-109/PR-110/PR-111/PR-112/PR-113/PR-114)

Record fields are supported when their Schema IR type is one of:

* a primitive scalar: `bool`, `i8`/`u8`/`i16`/`u16`/`i32`/`u32`/`i64`/`u64`,
  `f32`, `f64` -- exactly the scalar set Schema IR's `PrimitiveType`
  enumerates and the C++ backend already supports
* an enum reference, **if and only if** the target enum is declared in the
  same namespace as the referencing record, and every one of its declared
  values is non-negative (see "Enum fields" below)
* a `string` (see "String fields" below) -- every string field has a
  schema-validator-enforced positive `max_bytes` bound by the time
  backend_c sees it (`compiler/semantic/semantic.cpp`'s
  `validate_positive_u32`), so there is nothing for the C backend itself to
  re-validate about the bound's presence or positivity
* `bytes` (see "Bytes fields" below) -- the same schema-validator-enforced
  positive `max_bytes` guarantee applies identically
* an array (see "Array fields" and "Record array fields" below) whose
  element type is a scalar primitive, bounded string/bytes, a
  same-namespace non-negative-valued enum reference, or a same-namespace
  record reference -- exactly the
  plain-field element kinds this backend already supports, applied
  element-wise; every array field has a schema-validator-enforced positive
  `max_elements` bound by the time backend_c sees it (the same
  `validate_positive_u32` call used for `max_bytes`)
* a record reference (see "Nested record fields" below), **if and only if**
  the referenced record is declared in the same namespace as the embedding
  record

No new schema types were invented to support any of these.

**Any other field -- including a
cross-namespace nested record reference (plain or array-element), or a
cross-namespace/negative-valued enum reference (either as a plain field or
as an array element type) -- fails generation with a diagnostic naming the
record and field** (e.g. `backend_c: field
'quarry.telemetry.Sample.items' has a type the C backend does not support
yet`). **A record with a mix of supported and unsupported fields fails as
a whole** -- there is no partial generation
that silently drops the unsupported field. Deferred to later increments:
  cross-namespace nested record fields and cross-namespace/negative-valued
  enum fields (plain or array-element).

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
* every generated identifier is a valid, non-reserved C99 identifier. Safe
  schema names retain their existing spelling. C keywords, implementation-
  reserved spellings, and collisions with generated presence/length/count or
  scratch names are deterministically mapped with a `quarry_` prefix and,
  when necessary, a numeric suffix (`_2`, `_3`, ...). This mapping is local
  to the C backend; it does not change field indexes or wire names.

### Generated-name policy

The C backend keeps ordinary valid names unchanged, but never emits a C99
keyword or implementation-reserved identifier. A keyword or reserved source
name is prefixed with `quarry_`; a collision is resolved by trying
`_2`, `_3`, and so on in declaration order. Field allocation reserves the
base member, `has_<name>`, `<name>_length` or `<name>_count`, and all
field-specific local scratch names as one unit. This handles, for example,
`payload` plus `payload_length` without duplicate members. Uppercase enum
value normalization uses the same deterministic allocator, and generated
type names are checked against record, enum, and array-element types.

Namespace/type/function symbols use the existing namespace prefix and are
also normalized if a root-level name is reserved. Header-guard collisions
after path normalization are diagnosed. No Schema IR, BRF, or runtime
change is involved, and the C generated-code API epoch remains `2`.

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

### Enum fields

For a record with an enum field, e.g. `quarry.telemetry.Sample { status:
Status }` where `Status { OK = 0; WARNING = 1; ERROR = 2; }`:

```c
typedef struct {
    bool has_status;
    quarry_telemetry_Status_t status;
} quarry_telemetry_Sample_t;
```

**Representation decision.** `docs/design/c-backend.md` Section 4
investigated three options: (A) the generated enum typedef as the field's C
storage type; (B) a fixed-width integer storage type; (C) both, with an
explicit conversion boundary. **Selected (A)**, now implemented exactly as
proposed: the struct field's C type is the enum's own typedef
(`quarry_telemetry_Status_t`), matching the C++ backend's own choice
(`lower_field_type`'s `kEnumType` case sets `cpp_type = target->cpp_name`,
the enum's own class name) and giving the most idiomatic, readable,
type-hinted C API. This is safe for wire correctness because the WIRE width
is chosen independently, by the enum's max declared value (see below), and
is completely decoupled from whatever underlying representation a C
compiler happens to choose for a plain `enum` type (implementation-defined
per C99 6.7.2.2) -- every encode/decode call site explicitly casts to/from
a fixed-width unsigned type, never relying on the enum's own in-memory
width. (B) and (C) remain unselected: (B) loses the type-safety/readability
(A) gives essentially for free once wire correctness is already guaranteed
independently; (C) adds an explicit conversion-boundary layer with no
concrete forcing requirement at this scope.

**Wire encoding.** The wire width is the smallest unsigned width (1, 2, 4,
or 8 bytes) capable of representing the enum's largest declared value,
matching `compiler/backend/backend.cpp`'s `enum_width_for_max_value`
exactly -- the property that keeps enum field encoding byte-for-byte
identical to the C++ backend (verified by
`tests/interop/c_cpp_codec_interop_test.cpp`). The value is always written
as the matching unsigned type
(`quarry_c_write_u8`/`u16`/`u32`/`u64`), via an explicit cast
(`(uint8_t)record->status`) regardless of the enum's own (implementation-
defined) in-memory representation -- never `quarry_c_write_i8` et al., even
though the C `enum` keyword does not itself guarantee unsignedness. This
matches the C++ backend and the BRF spec's "Enum Encoding" section exactly
("the smallest fixed-width unsigned integer capable of representing the
largest enum value").

**Validation and unknown values.** Neither C's type system nor a plain
`enum` prevents a caller from constructing an out-of-range numeric value
(via an explicit cast, for instance) -- the same problem C++'s `enum class`
has, which is why the C++ backend's generated `encode_result` *and*
`decode_..._result` both explicitly check the numeric value against the
declared value set (see `tests/fixtures/backend/enum_reference.txt`'s
`enum_numeric == 1 || enum_numeric == 2` pattern). backend_c replicates
this exactly, on both sides:

* **Encode**: before writing, checks `record-><field> == V0 ||
  record-><field> == V1 || ...` over the enum's raw declared values; an
  out-of-range value returns `QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE` without
  writing anything. (`_encoded_size()` does not perform this check --
  encoded size depends only on the field's fixed wire width, never on
  whether the current value happens to be valid; the real `_encode()` call
  is where an invalid value is actually rejected.)
* **Decode**: reads the raw wire value into an unsigned temporary (not
  directly into the struct field), checks it against the same declared
  value set, and only then casts it into the enum-typed struct field
  (`result.value.status = (quarry_telemetry_Status_t)status_raw;`). An
  unrecognized numeric value returns `QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE`
  with `byte_offset` pointing at the field's payload -- values are never
  silently coerced or clamped.

`QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE` is not new in PR-109: it was already
declared in the shared runtime's `quarry_c_status_t` enum in PR-108,
explicitly reserved for this moment ("the shared runtime never produces
these itself... generated code reuses this one status type rather than
inventing a second, parallel enum") -- see "Generated-code API version (C)"
below for why this means no epoch bump was needed.

**Cross-namespace enum fields are not supported yet.** A field whose enum
is declared in a *different* namespace than the record fails generation
with a diagnostic naming the field and the enum's owning namespace.
backend_c has no cross-generated-file include-dependency mechanism (each
generated `.h` is self-contained today); building one to support this
would be exactly the "framework code for future features" this backend has
consistently avoided building ahead of a concrete need. The C++ backend
does support this (via its `TypeCatalog`'s cross-namespace `#include`
tracking) -- this is a real, deliberate narrowing relative to C++, not an
oversight.

### String fields

For a record with a string field, e.g. `quarry.telemetry.Sample { label:
string, max_bytes: 16 }`:

```c
typedef struct {
    bool has_label;
    char label[17];
    uint32_t label_length;
} quarry_telemetry_Sample_t;
```

**Representation decision.** `docs/design/c-backend.md` Section 2's
"Strings" investigated area proposed three shapes (labeled A/B/C in the
PR-110 task spec): (A) `char field[MAX_BYTES]` + `size_t field_length` +
`bool has_field`; (B) the same but `uint8_t` content; (C) a generated or
shared bounded-string struct type. **Selected a variant of (A)**, with two
deliberate refinements over the literal task-spec sketch:

* **Capacity is `max_bytes + 1`, not `max_bytes`.** The extra byte is
  reserved for a trailing NUL the generated decoder always writes
  (`result.value.<field>[field_view.length] = '\0';`), so decoded content
  with no embedded NUL can be handed directly to ordinary C string APIs
  (`strcmp`, `printf("%s", ...)`, etc.) without the caller having to
  special-case Quarry's own storage shape. This was `docs/design/
  c-backend.md`'s original recommendation for this field kind, now
  implemented rather than proposed.
* **The length member is `uint32_t`, not `size_t`.** `size_t`'s width is
  implementation-defined and can be as narrow as 16 bits on some embedded
  targets; `max_bytes` is already a `uint32_t` in Schema IR
  (`StringType.max_bytes`), so using the same type for the length member
  keeps the generated public struct layout portable and exactly matches the
  bound it is checked against, instead of introducing a second,
  platform-dependent width for what is conceptually the same quantity.

`char` (not `uint8_t`) is the content element type: this is (A), not (B) --
consistent with the C string-API interop goal above, and unlike enum
storage (Section 4 of the design doc), there is no wire/in-memory
independence concern here to motivate a different type, since string
content bytes are used as-is on the wire (no per-type transformation).
Option (C) -- a generated or shared bounded-string struct -- was rejected as
unnecessary indirection for this slice: two fields on the record struct
(the buffer and the length) accomplish everything a wrapper type would,
without a new public type per field or per schema.

**String semantics, matching the BRF spec and the C++ backend exactly:**

* Quarry strings are UTF-8 text, not opaque bytes: `docs/specifications/
  binary-record-format.md`'s "string" section states "String data bytes
  SHALL be valid UTF-8," and both backends enforce this at runtime (not
  merely documented and left unchecked) -- see "Encoding"/"Decoding" below.
* `max_bytes` is measured in encoded UTF-8 bytes and does **not** include
  any terminator; the wire payload itself never contains a NUL terminator
  (`docs/specifications/binary-record-format.md`: "No NUL terminator is
  encoded"). The generated buffer's `+1` capacity byte is a decode-side,
  in-memory-only convenience -- it is never part of `max_bytes`, the wire
  length, or `_encoded_size()`'s calculation.
* **Embedded `U+0000` is valid string data** (explicitly stated in the BRF
  spec), so `<field>_length` -- not `strlen(<field>)` -- is the
  authoritative content length. A string containing an embedded NUL still
  decodes correctly and still gets a trailing terminator written one byte
  past its *last* content byte; callers that need embedded-NUL-safe access
  must use `<field>_length`, not a `char*`-based C string function, exactly
  as they already must for any length-prefixed byte buffer.
* **Present-empty vs. absent are distinguished by `has_<field>`, never by
  content.** Both states leave `<field>` as an all-zero buffer and
  `<field>_length == 0` after `_init()` or a successful decode; the *only*
  distinguishing signal is `has_<field>`, matching every other optional
  field kind this backend generates (scalars, enums).
* **Constructing a value:** write up to `sizeof(record.<field>) - 1` (i.e.
  `max_bytes`) content bytes directly into `record.<field>`, set
  `record.<field>_length` to the exact byte count, and set
  `record.has_<field> = true`. No generated setter/helper function exists
  for this in PR-110 -- see "Generated codec API design" below for why
  direct struct manipulation was judged sufficient here, matching the
  scalar/enum precedent.
* **Inspecting a decoded value:** read `record.<field>_length` bytes
  starting at `record.<field>`; `record.<field>` is also guaranteed
  NUL-terminated at index `<field>_length` after a successful decode, so
  ordinary C string functions are safe to use directly whenever the caller
  already knows (from its own schema/application knowledge) that the field
  never carries an embedded NUL.
* **Input longer than capacity:** rejected, never truncated -- see
  "Encoding"/"Decoding" below. This backend never silently drops bytes to
  make an oversized value fit.

**Encoding.** `record.<field>_length` is checked against `max_bytes` first
(`QUARRY_C_STATUS_BOUNDS_EXCEEDED` if it exceeds the bound), then the
content bytes are validated as UTF-8 (`quarry_c_is_valid_utf8`,
`QUARRY_C_STATUS_INVALID_UTF8` if invalid) -- the same two-check order the
C++ backend's `render_string_field_encoding` uses (`value.size() >
max_bytes` then `append_string_utf8`), so both backends reject the same
invalid values for the same reason. No scratch buffer is used (unlike
scalar/enum fields): `record.<field>` already holds exactly the wire bytes
verbatim (raw UTF-8, no big-endian or other transformation needed), so the
generated `quarry_c_field_t` entry points `.bytes` directly at
`record.<field>` with `.length = record.<field>_length`. `_encoded_size()`
does not perform either check, matching the enum-membership precedent
exactly: encoded size is read from the field's current (possibly invalid)
length directly, and only the real `_encode()` call rejects an invalid
value.

**Decoding.** Chose **"decode into a temporary and commit only on
success"** (option B from the PR-110 task spec's "Investigate whether
decode should... (A) mutate the destination incrementally; (B) decode into
a temporary and commit only on success"), applied *per field*, matching the
existing enum decode precedent (PR-109: read into a raw local, validate,
only then write the struct field) rather than introducing a whole-record
temporary. Concretely: `quarry_c_copy_bounded` performs a bounds check
(wire length vs. `max_bytes`) and the copy in one runtime call
(`QUARRY_C_STATUS_BOUNDS_EXCEEDED` on failure, with no partial copy
performed), then the copied-from source bytes are validated as UTF-8
(`QUARRY_C_STATUS_INVALID_UTF8` on failure), and only after *both* checks
pass is the field committed: NUL-terminated, `<field>_length` set, and
(back in the shared per-field loop) `has_<field>` set to `true`. On any
failure the overall decode returns immediately with a non-OK `status` and a
`byte_offset` pointing at the field's payload, matching every other field
kind's failure contract; per `quarry_telemetry_Sample_decode_result_t`'s
existing documented contract, `result.value` is "only meaningful when
`status == QUARRY_C_STATUS_OK`," so content copied into the struct ahead of
a subsequent UTF-8-validation failure is never exposed as if it were a
successful decode. No wire byte is ever written into generated storage that
skips the bounds check that precedes it.

**No unsafe C string functions anywhere in the generated code or the
runtime addition:** no `strcpy`/`strcat`/`sprintf`, and no reliance on
`strlen` for wire data (`quarry_c_is_valid_utf8` and `quarry_c_copy_bounded`
both take an explicit length, never a NUL-terminated-string assumption).
`quarry_c_copy_bounded`'s single `memcpy` is always immediately preceded by
its own bounds check against `destination_capacity`, so it is a *checked*
copy, never a bare/unchecked one.

### Bytes fields

For a record with a bytes field, e.g. `quarry.telemetry.Sample { blob:
bytes, max_bytes: 16 }`:

```c
typedef struct {
    bool has_blob;
    uint8_t blob[16];
    uint32_t blob_length;
} quarry_telemetry_Sample_t;
```

**Representation decision: reuse the string layout *strategy*, with two
differences, both directly required by the BRF spec's "bytes" section**
("Bytes data may contain any byte sequence... No UTF-8 validation
applies"):

* **Capacity is exactly `max_bytes`, not `max_bytes + 1`.** There is no
  NUL-termination convenience to offer for arbitrary binary data -- a
  trailing NUL byte would be meaningless (and potentially misleading) for
  content that isn't text, matching `docs/design/c-backend.md` Section 2's
  original "Bytes" investigated area ("No NUL terminator concern").
* **The content element type is `uint8_t`, not `char`.** Bytes fields have
  no "hand to a C string API" use case to motivate `char`, and `uint8_t`
  reads as unambiguously "opaque binary data" rather than "text."

Everything else is identical to string's chosen shape: an explicit
`uint32_t <field>_length` byte-length member (never a compile-time
constant, since presence and content vary per record instance), and
`has_<field>` as the sole empty-vs-absent signal (both leave the buffer
all-zero and length 0; only the flag differs).

**No UTF-8 validation is performed anywhere for bytes fields** -- this is
the one behavioral difference from string's encode/decode path, not an
oversight: the BRF spec is explicit that "No UTF-8 validation applies" to
`bytes`. Encoding checks `record-><field>_length` against `max_bytes`
(`QUARRY_C_STATUS_BOUNDS_EXCEEDED` on violation) and, if that passes, adds
the field directly (`fields[field_count].bytes = record-><field>;` -- no
cast needed, since `record-><field>` is already `uint8_t*`, unlike
string's `char*` requiring `(const uint8_t*)`). Decoding calls
`quarry_c_copy_bounded` exactly as string decode does (bounds-check-and-
copy in one runtime call), but skips the UTF-8 validation step and the
NUL-terminator write entirely -- `<field>_length` is set immediately after
the copy succeeds. `_encoded_size()` performs no bounds check, matching the
string/enum precedent exactly (encoded size reflects the field's current
length regardless of validity; only the real `_encode()` call rejects an
invalid one).

**No runtime changes were needed for bytes.** `quarry_c_copy_bounded`
(added in PR-110 for string decode) is reused completely unchanged --
a bounds-checked byte copy has no UTF-8-specific behavior to begin with, so
there was nothing to add or generalize. See "Generated-code API version
(C)" below for why this means no epoch bump was needed either.

### Array fields

For a record with an array of a scalar element, e.g. `quarry.telemetry.
Sample { readings: float32[], max_elements: 4 }`:

```c
typedef struct {
    bool has_readings;
    float readings[4];
    uint32_t readings_count;
} quarry_telemetry_Sample_t;
```

And for an array of a same-namespace enum element, e.g. `statuses:
Status[], max_elements: 3`:

```c
typedef struct {
    bool has_statuses;
    quarry_telemetry_Status_t statuses[3];
    uint32_t statuses_count;
} quarry_telemetry_Sample_t;
```

**Representation decision (task spec's Option A, selected as-is):** a
fixed-capacity array of the element's own C type, sized directly from
`max_elements`, plus an explicit `uint32_t <field>_count` and the same
`has_<field>` presence flag every other field kind uses. The element type
is exactly what a *plain* field of that same scalar or enum type would use
(the generated struct's own scalar C type, or the enum's own typedef) --
there is no separate "array element" type distinct from the plain-field
type, and no generated per-element wrapper. `<field>_count` is never a
compile-time constant (presence and length vary per record instance), the
same reasoning already applied to string/bytes `<field>_length`.

**Supported element types:** `bool`, fixed-width signed/unsigned integers,
`f32`/`f64`, bounded strings, bounded bytes, and same-namespace
non-negative-valued enums. Scalar and enum elements reuse the existing
plain-field lowering and codecs. String/bytes elements use named,
fixed-capacity element structs and the existing varuint and bounded-copy
runtime primitives. Nested arrays remain unsupported and fail generation
with a diagnostic naming the field -- the diagnostic specifically says "array
whose element type..." rather than reusing the generic plain-field "has a
type the C backend does not support yet" message, since an unsupported
array element is a materially different situation (the array construct
itself is fine; only its element type isn't) worth naming precisely.
Nested arrays (`uint32[][]`) can never actually reach this backend's field
lowering at all: the normative YAML frontend already rejects nested
arrays outright (schema-language.md: "Nested arrays... SHALL be
rejected").

> **PR-114 update: same-namespace record elements are now supported
> too** (see "Record array fields" below), via the identical
> `lower_record_reference` extraction pattern `lower_enum_reference` set
> as precedent. This paragraph's original claim that "a record-reference
> field type requires cross-file imports this compiler does not resolve
> yet, so there is no way to construct a schema exercising [an
> array-of-record] combination through the production pipeline today" was
> correct only for a *distinctly-named* second record (still true, and
> still the reason `tests/interop/c_cpp_nested_record_interop_test.cpp`
> must build its array-of-record schema directly rather than through
> YAML) -- it did not account for a record referencing *itself* as an
> array element, which needs only one record declaration and is fully
> reachable through a real `.brd` file (see
> `SchemaCompilerToolTest.CBackendRejectsSelfReferentialArrayOfRecordsWith
> CycleDiagnostic` in `tests/tools/schema_compiler_tool_test.cpp`, which
> reaches the topological-sort cycle diagnostic through the genuine CLI,
> not a Schema-IR-direct test).

**Encoding.** Order matches BRF's "Array Encoding" section exactly: a
bounds check (`record-><field>_count` vs. schema `max_elements`,
`QUARRY_C_STATUS_BOUNDS_EXCEEDED` on violation) first, then the count
itself is written as an unsigned LEB128 varuint
(`quarry_c_write_varuint`), then every element is written in index order
using the *exact* per-element codec (and, for enum elements, the *exact*
membership check with an explicit cast) a plain scalar/enum field already
uses. Unlike string/bytes, an array field cannot skip the scratch buffer
and point directly at the record's own array memory: each element still
needs the same big-endian transformation a plain scalar/enum field's
value does, and the wire payload additionally needs the varuint count
prefix the struct doesn't store at all. The element loop carries an
unconditional `&& element_index < <max_elements>` safety bound,
independent of whether the call is validating (`_encode()`) or not
(`_encoded_size()`): harmless in the validated path (count is already
checked against `max_elements` before the loop starts), but load-bearing
in `_encoded_size()`'s unvalidated path, where it prevents an unbounded
loop over an arbitrary caller-supplied count that a corrupted/misused
struct might hold -- a real availability concern specific to arrays, since
(unlike a plain scalar/enum field's O(1) write) an array's write cost
scales with its declared count. `_encoded_size()` itself performs no
bounds/membership validation, matching the string/bytes/enum precedent
exactly.

**Decoding.** Also matches BRF's "Array Encoding" section exactly: the
varuint element count is read first (a malformed varuint, or a count
exceeding `max_elements`, is rejected before any element byte is read),
then the *exact* remaining-byte-count is checked against `count *
element_width` (rejecting truncated or over-long payloads before any
element is read), then each element is read in index order using the
exact same per-element codec (and, for enum elements, the same
raw-temporary-then-validate-then-cast pattern) a plain scalar/enum field's
decode already uses. All reads (the count varuint and every element)
share one `quarry_c_reader_t`, so each element read's own bounds check
(already present in every `quarry_c_read_uN` function) is sufficient on
its own -- no manual offset bookkeeping is needed, a small simplification
relative to the C++ backend's subspan-based approach (which manually
tracks and advances an offset). Never writes beyond
`result.value.<field>[max_elements - 1]`, since count is bounds-checked
against `max_elements` before the element loop ever runs.

**Scratch buffer sizing.** An array field's scratch buffer capacity is
`kMaxVaruintBytesForUint32` (a fixed constant, `5` -- the worst-case
LEB128-encoded byte length for any `uint32_t` value, `ceil(32/7)`) plus
`max_elements * element_width`. Using the worst-case constant here
(instead of computing the exact value-dependent encoded size of the
specific `max_elements` value at codegen time) was a deliberate choice to
avoid a second, host-side reimplementation of
`quarry_c_varuint_encoded_size`'s algorithm that would need to be kept in
sync with the runtime forever; the handful of bytes this can
over-allocate is negligible next to the array's own data.

**No runtime changes were needed for arrays.** BRF's "Array Encoding"
section requires only an unsigned LEB128 varuint element count followed
by tightly-packed, big-endian elements in index order for the element
types this PR supports -- `quarry_c_write_varuint`/`quarry_c_read_varuint`
and the existing per-width `quarry_c_write_uN`/`quarry_c_read_uN`
functions (all already present since PR-108, used internally for the
Field Directory itself but always public) are exactly sufficient. See
"Generated-code API version (C)" below for why this means no epoch bump
was needed either.

### Nested record fields

For a record embedding another same-namespace record by value, e.g.
`quarry.telemetry.Location { code: uint32 }` and `quarry.telemetry.Sample {
location: Location }`:

```c
typedef struct {
    bool has_location;
    quarry_telemetry_Location_t location;
} quarry_telemetry_Sample_t;
```

**Representation decision: generate nested C structs directly, embedded by
value -- no pointers, no heap allocation.** The task spec asked for exactly
this to be investigated and justified: ownership, presence tracking,
initialization, field layout, and deterministic ordering.

* **Ownership and layout.** The referenced record's struct is embedded
  directly as a same-sized-and-shaped member, exactly like a plain
  scalar/enum field's own C type would be -- there is no separate "nested
  reference" type distinct from the referenced record's own generated
  `_t`. This keeps the whole record one flat, contiguous, fixed-size value
  with no indirection anywhere in the public data model, matching this
  backend's existing "no opaque handle, no heap, no hidden state" rule for
  every other field kind (see "Generated public data model" above).
* **Presence tracking.** A `bool has_<field>` member, identical to every
  other optional field kind -- there is nothing nesting-specific about
  presence tracking; a nested record field is either there or it isn't,
  the same as a scalar, string, bytes, or array field.
* **Initialization.** No special-case code was needed: `_init()`'s existing
  `memset(record, 0, sizeof(*record))` already zero-initializes an embedded
  child completely, since the child is just more bytes inside the parent's
  own flat storage. This is a direct, "free" consequence of choosing
  by-value embedding over a pointer -- a pointer member would have left the
  referenced storage's existence and lifetime as a separate, undocumented
  question this design avoids by construction.
* **Deterministic field layout and ordering.** Struct members are declared
  in schema field order, exactly like every other field kind (see
  "Generated public data model" above) -- nesting introduces no new
  layout question at the *field* level. It does introduce one at the
  *record-declaration* level: embedding a record by value requires that
  record's struct to already be a **complete type** at the point of
  embedding (C, unlike C++, has no forward-declaration escape hatch for a
  by-value struct member), so **this is the first time backend_c has
  needed a real topological sort of same-namespace record declarations**
  (previously unnecessary: enums are dependency-free leaves, unconditionally
  rendered before any record in the same file). `order_records_topologically`
  (Kahn's algorithm, mirroring `compiler/backend/backend.cpp`'s
  `order_declarations_topologically` for the C++ backend) reorders each
  namespace's `PlannedRecord` list so every embedded record is fully
  declared -- and has its own `max_encoded_size` already resolved (see
  "Scratch buffer sizing" below) -- before the record that embeds it,
  **regardless of the order records are declared in Schema IR.** A record
  that (directly or transitively) embeds itself is rejected at generation
  time with a diagnostic naming the cycle -- Schema IR validation itself
  does not reject this (verified: no upstream pass does), so backend_c
  must, and does, detect it independently, exactly like the C++ backend
  already does for its own equivalent case.

**Same-namespace-only restriction, matching PR-109's enum precedent
exactly.** A field referencing a record declared in a *different*
namespace than the embedding record fails generation with a diagnostic
naming the field and the referenced record's owning namespace, for the
same reason cross-namespace enum fields are unsupported: backend_c has no
cross-generated-file include-dependency mechanism (each generated `.h` is
self-contained today), and building one purely to support this would be
speculative "framework code for future features" this backend has
consistently avoided. The C++ backend does support cross-namespace nested
records (via its `TypeCatalog`'s `#include` tracking) -- this is a real,
deliberate narrowing relative to C++, not an oversight.

**Encoding and decoding are pure composition of the referenced record's own
already-generated `_encode()`/`_decode()`/`_encoded_size()` -- no new
runtime code was needed at all.** Per `docs/specifications/
binary-record-format.md`'s "Nested Records" section, a nested record
field's wire payload is simply the complete, independently structured BRF
encoding of the referenced record (its own header, Field Directory, and
payload) -- exactly what that record's own generated codec functions
already produce and consume:

* **Encode** calls the child's real, validating `_encode()` into a
  scratch buffer sized from the child's own worst-case
  `max_encoded_size` (see below), and propagates any failure status
  directly. `_encoded_size()` instead calls the child's own
  `_encoded_size()` (no validation, matching the string/bytes/array
  precedent exactly: encoded size never depends on whether the current
  value happens to be valid).
* **Decode** calls the child's real `_decode()` directly on the isolated
  field-view byte span (`quarry_c_find_field`'s existing `field_view`,
  unchanged from every other field kind), and propagates any failure
  status directly. Because the child's own `_decode()` already calls
  `quarry_c_parse_record` on that byte span, **every BRF "Nested Records"
  structural requirement is already enforced for free**: header
  version/flags/reserved validation, exact payload-length validation, and
  -- critically -- the child's own record-id check
  (`QUARRY_C_STATUS_UNEXPECTED_RECORD_ID` if the embedded bytes claim to be
  a different record) all come from the exact same code path a top-level
  decode already uses. A wrong nested record id, a malformed nested
  payload, and a truncated nested payload are consequently all rejected
  with **no new validation code in the parent's decode at all** -- this
  was the checkpoint's key finding, and it is why this is the first
  backend_c field kind to require zero new runtime functions or
  `quarry_c_status_t` values (see "Generated-code API version (C)" below).
  On a child decode failure, the parent composes an absolute byte offset
  (`field_view.byte_offset + <child>_decode_result.byte_offset`), exactly
  mirroring the array decode path's identical
  `field_view.byte_offset + array_reader.offset` composition and the C++
  backend's `field->field_offset + *decoded.byte_offset`.

**Scratch buffer sizing.** A nested record field's scratch buffer capacity
is the referenced record's own `max_encoded_size`: `16` (BRF header) plus,
per field, `21` bytes of worst-case Field Directory entry overhead
(`1 + 10 + 10`: the field_index byte plus the worst-case LEB128 length of a
`uint64_t`-ranged offset and length, `kMaxVaruintBytesForUint64`, `10 =
ceil(64/7)`) plus that field's own worst-case payload contribution
(`max_bytes` for string, `max_bytes` for bytes, `array_scratch_capacity`
for arrays, a nested record's own `max_encoded_size` recursively, or
`width_bytes` for scalars/enums) -- assuming every field is present
simultaneously, the same safe-worst-case assumption arrays already use for
their own scratch buffer. This is computed by
`compute_record_max_encoded_size` once a record's fields are fully lowered,
and stored in a whole-schema `RecordCatalog` (mirroring the existing
`EnumCatalog`) so a field embedding that record can look its
already-resolved size up in constant time -- correct only because
`order_records_topologically` guarantees a record's own
`max_encoded_size` is resolved before any record that embeds it is
processed. (`kMaxVaruintBytesForUint64` is deliberately distinct from the
existing `kMaxVaruintBytesForUint32 = 5` used for array element *counts*,
which are genuinely `uint32_t`-bounded by `max_elements`; Field Directory
offsets and lengths are `size_t`/`uint64_t`-ranged in the runtime API, so
the wider bound is the correct one here, at negligible extra cost.)

### Record array fields

For a record with an array of a same-namespace record element, e.g.
`quarry.tree.Group { items: Item[], max_elements: 3 }` where `Item {
value: uint32 }`:

```c
typedef struct {
    bool has_items;
    quarry_tree_Item_t items[3];
    uint32_t items_count;
} quarry_tree_Group_t;
```

**Representation decision: reuse the existing array-of-scalar/enum shape
verbatim, with the referenced record's own generated `_t` type as the
element type.** No new struct-rendering code was needed at all --
`render_header`'s existing `is_array` branch already renders `<c_type>
<field>[<max_elements>]; uint32_t <field>_count;` generically over
whatever `c_type` is (already proven generic across scalar and enum
element types); lowering simply sets `c_type` to the referenced record's
`_t` name, mirroring exactly how array-of-enum already sets `c_type` to
the enum's own typedef. Ownership is by-value, fixed-capacity, no heap, no
pointers -- identical to every other field kind. `_init()`'s existing
`memset(record, 0, sizeof(*record))` continues to zero-initialize every
array slot's embedded child completely, for free, exactly as it already
does for a single embedded nested record.

**Same-namespace-only, matching the plain nested-record field restriction
exactly.** A record-typed array element from a *different* namespace fails
generation with a diagnostic naming the field and the referenced record's
owning namespace -- the same "no cross-generated-file include-dependency
mechanism" reason a plain cross-namespace nested-record field is
unsupported. `lower_record_reference` (extracted from what was previously
the plain `kRecord` branch's inline logic, mirroring how PR-112 extracted
`lower_enum_reference` for the identical plain-enum-field/array-of-enum
reuse) performs this same-namespace check once, shared by both a plain
record-typed field and a record-typed array element.

**Cycle detection generalizes to array-element dependencies, with zero
changes to the topological sort itself.** A record containing an array of
itself is a hard C-language impossibility -- exactly like a plain
self-referential record field, a fixed-size array member requires a
*complete* element type, and there is no valid declaration order for a
record embedding an array of itself, directly or transitively. Phase 1 of
`collect_namespace_files` now also pushes a same-namespace dependency when
a field is `is_array && array_element_is_record`, in addition to the
existing `is_record` case; `order_records_topologically`'s Kahn's-algorithm
cycle detection (introduced in PR-113) needed no changes at all to reject
this, since it already generalizes to any dependency edge.

**Encoding and decoding are pure composition, using PR-114's investigation
findings (`docs/design/c-backend.md` references the REPORT.md write-up
that resolved this) to avoid any new runtime code whatsoever** -- this
field kind was initially thought to need a new runtime primitive (a
raw-byte-append function for a `quarry_c_writer_t`), until a closer look
established that it does not:

* **Encode** learns each element's exact encoded length from the element
  type's own already-existing `<Type>_encoded_size()` (present since
  PR-108, originally added only so callers could pre-size an output
  buffer) *before* encoding anything, writes that length as the required
  wire length-prefix varuint via the existing `quarry_c_write_varuint`,
  and then calls the element type's own real, validating `<Type>_encode()`
  **directly into the array field's own writer, at its current tail
  position** (`writer.buffer + writer.length`, with capacity
  `writer.capacity - writer.length`) -- an entirely ordinary `_encode()`
  invocation, just with a repositioned destination instead of a fresh
  buffer, since nothing about `_encode()`'s contract requires a fresh,
  zero-offset destination. `writer.length` is then advanced by the real
  `bytes_written`, a direct write to an already-public field, mirroring
  how generated array-decode code already reads `array_reader.offset`
  directly. **No temporary per-element buffer and no raw-byte copy of any
  kind are used.** This is safe because `_encoded_size()`'s reported
  length can never disagree with a *successful* `_encode()`'s
  `bytes_written` -- the two are computed from the same shape-derived
  arithmetic, differing only in that `_encode()` additionally validates;
  wherever they could disagree (an invalid enum value, an over-length
  string, a corrupted count), `_encode()` fails outright and the whole
  array-field encode already propagates that failure before the
  (now-irrelevant) speculative length prefix is ever exposed as valid
  output -- the same safety argument `_encoded_size()`'s existence and its
  documented "no validation" behavior already rest on for every other
  field kind.
  `_encoded_size()` (the array field's own, non-validating counterpart)
  mirrors this without performing any real write at all: it calls each
  element's own `_encoded_size()` and advances the writer's `.length`
  field directly by that reported size, since nothing downstream in the
  `_encoded_size()` code path ever reads scratch-buffer *contents* --
  `quarry_c_record_encoded_size` sums only `.length` values -- matching the
  same optimization a plain nested-record field's own `_encoded_size()`
  already uses (calling the child's `_encoded_size()` instead of its
  `_encode()`).
* **Decode** reads a per-element length-prefix varuint via the existing
  `quarry_c_read_varuint` (reusing the same `array_reader` the element
  count was already read from), bounds-checks it against the reader's
  remaining bytes, then passes the isolated element byte span
  (`array_reader.buffer + array_reader.offset`, for `element_length`
  bytes -- a raw pointer into the existing buffer, no copy) directly to the
  element type's own real `<Type>_decode()`. Because that function already
  calls `quarry_c_parse_record` internally, **every BRF "Nested Records"
  structural requirement the spec's "Array Encoding" section demands for
  `array<record>` elements is already enforced for free**: header
  validation, exact embedded payload length, and -- critically -- the
  matching-record-id check, exactly mirroring the plain nested-record
  field's own zero-new-validation-code precedent. A per-element failure
  composes an absolute byte offset
  (`field_view.byte_offset + array_reader.offset + <element>_decode_result
  .byte_offset`), one level deeper than the plain nested-record field's
  identical composition pattern. `array_reader.offset` is advanced by the
  real, decoded element length afterward, a direct write to the same
  already-public field decode already reads elsewhere.
* **The post-count-read validation shape differs from fixed-width arrays,
  necessarily.** Scalar/enum array decode checks the *total* remaining
  byte count (`count * element_width`) up front, since element width is
  known at generation time. Record elements are variable-width, so that
  up-front check is impossible; instead, a running `array_reader.offset`
  is checked against `array_reader.length` **after** the per-element loop,
  rejecting trailing bytes -- exactly mirroring the C++ backend's own
  identical post-loop trailing-bytes check for its `array<record>`
  implementation.

**No new runtime function, and no generated-code API epoch bump.** Every
operation above is an existing runtime function, an existing generated
`_encode()`/`_decode()`/`_encoded_size()` call, or a direct read/write of
an already-public runtime struct field
(`quarry_c_writer_t.length`/`quarry_c_reader_t.offset`) -- the same kind
of direct field access generated decode code already performs today. See
"Generated-code API version (C)" below.

**Scratch buffer sizing.** A record-array field's scratch buffer capacity
extends the existing array formula with the element record's own worst
case, generalizing PR-113's `record_max_encoded_size` computation one
level: `kMaxVaruintBytesForUint32` (the existing element-count prefix
constant, unchanged) plus, per element, `kMaxVaruintBytesForUint64` (the
same worst-case length-prefix constant a nested record field's own Field
Directory overhead already uses) plus the element record's own
`max_encoded_size` (resolved, like a plain nested-record field's, only
after topological sorting -- deferred from the field's initial lowering
to `collect_namespace_files`'s Phase 3, alongside the existing
`record_max_encoded_size` resolution). **No change was needed to
`compute_record_max_encoded_size` itself** -- its existing `is_array`
branch already absorbs whatever this new formula produces unconditionally,
since `array_scratch_capacity` was always meant to mean "this field's
total worst-case wire contribution," independent of element kind.

**Composes with nested records at any depth, with no depth limit and no
new code.** An array element record may itself contain a further nested
record field (or another record array) -- already proven achievable in
principle by the C++ backend's own
`RecordArrayComposesWithNestedRecordFields` test, and verified for the C
backend by `BackendCTest.ArrayOfRecordComposesWithNestedRecordField`
(`tests/backend_c/backend_c_test.cpp`), which round-trips generated struct
and codec text for exactly this shape. The recursive
`max_encoded_size`/pure-composition model needed no special-casing for
this: an element record's own `max_encoded_size` already correctly
accounts for its own nested fields (including further record arrays),
computed by the same `compute_record_max_encoded_size` call every record
already gets.

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
array elements. Even with PR-112's arrays and PR-113's nested records now
implemented, `quarry_telemetry_Sample_decode_result_t` still has no `path`
member: a failure inside an array element or a nested record is reported
via the same flat `status`/`has_byte_offset`/`byte_offset` triple every
other field kind uses (a byte offset alone is always sufficient to locate
the failure, since BRF encoding is deterministic), not a structured path of
field/array-index steps. A `path` member remains deferred until a concrete
need for *symbolic* (as opposed to byte-offset) failure location is
demonstrated, not before.

**PR-110 (string fields) preserved this API exactly -- no new helper/setter
function was added for strings.** `record.<field>`/`record.<field>_length`/
`record.has_<field>` are documented for direct manipulation (see "String
fields" above); a `quarry_..._set_label()`-style helper was considered and
rejected for this PR, since direct struct assignment is already safe and
self-explanatory for a fixed-capacity buffer plus an explicit length, and no
concrete usability gap was demonstrated to justify one. No builder-style API
was introduced either, matching the C++ backend's own plain-struct-in-C
translation (Section 1 of `docs/design/c-backend.md`).

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
#if QUARRY_C_GENERATED_CODE_API_VERSION != 2U
#error "Generated Quarry C code is incompatible with the installed Quarry C runtime. ..."
#endif
```

C99 has no `static_assert`; `#if`/`#error` is the portable equivalent and
needs no compiler extension. Kept independent from the C++ epoch because the
two languages' generator/runtime contracts can (and likely will) change on
different schedules -- conflating them would force an artificial version
bump to one language whenever only the other actually changed.

**PR-109 (enum fields) did not bump this epoch, deliberately.** No runtime
function signature changed and no new runtime function was added: enum
field encode/decode calls the exact same `quarry_c_write_uN`/
`quarry_c_read_uN` functions scalar fields already used in PR-108, just
with an explicit width-appropriate cast at the call site (a generated-code
change, not a runtime one). `QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE` was
already part of the epoch-1 runtime contract (declared, if previously
unused, in PR-108's `quarry_c_status_t`). Generated code compiled against
the epoch-1 runtime before PR-109 continues to compile and behave
identically; PR-109's generated code depends on nothing the epoch-1 runtime
header didn't already provide. This is the "only bump the epoch if
compatibility would actually be broken" case working as intended.

**PR-110 (string fields) bumped this epoch, 1 -> 2, deliberately -- the
first bump since the epoch was introduced.** Unlike PR-109, this PR added
genuinely new public runtime surface: two new functions
(`quarry_c_is_valid_utf8`, `quarry_c_copy_bounded`) and two new
`quarry_c_status_t` values (`QUARRY_C_STATUS_BOUNDS_EXCEEDED`,
`QUARRY_C_STATUS_INVALID_UTF8`), all of which generated string-field code
now calls/references directly. Pairing new (post-PR-110) generated code
containing a string field with an old (pre-PR-110, epoch-1) runtime header
would fail to compile with a confusing "undefined identifier
`quarry_c_is_valid_utf8`" error rather than this epoch guard's clear,
actionable `#error` -- exactly the failure mode the epoch mechanism exists
to convert into an early, understandable diagnostic. Generated code for
schemas with **no** string field (scalars/enums only) still compiles
against either runtime version in principle, but the epoch is a single
per-schema-file compile-time constant, not a per-field one, so every
generated file compiled against the new compiler now requires epoch 2
regardless of which field kinds it actually uses -- consistent with how the
epoch has always been an all-or-nothing per-file compatibility gate, never
a finer-grained one.

**PR-111 (bytes fields) did not bump this epoch, staying at 2 -- back to
the "no bump needed" case, like PR-109.** Bytes field encode/decode calls
only `quarry_c_copy_bounded`, an existing epoch-2 runtime function already
present for string decode; no new runtime function was added and no new
`quarry_c_status_t` value was needed (bytes reuses
`QUARRY_C_STATUS_BOUNDS_EXCEEDED`, already part of the epoch-2 contract
since PR-110). Generated code compiled against the epoch-2 runtime before
PR-111 continues to compile and behave identically; PR-111's generated
code depends on nothing the epoch-2 runtime header didn't already provide.

**PR-112 (array fields) also did not bump this epoch, staying at 2.** Array
field encode/decode calls only `quarry_c_write_varuint`/
`quarry_c_read_varuint` and the existing per-width `quarry_c_write_uN`/
`quarry_c_read_uN` functions -- all already present in the epoch-2 runtime
contract since PR-108 (used internally for the Field Directory itself, but
always public, never gated behind an internal-linkage convention). No new
runtime function was added and no new `quarry_c_status_t` value was
needed (arrays reuse `QUARRY_C_STATUS_BOUNDS_EXCEEDED` and
`QUARRY_C_STATUS_INVALID_FIELD_LENGTH`/`QUARRY_C_STATUS_MALFORMED_VARUINT`,
all already part of the epoch-1/epoch-2 contract). Generated code compiled
against the epoch-2 runtime before PR-112 continues to compile and behave
identically; PR-112's generated code depends on nothing the epoch-2
runtime header didn't already provide.

**PR-113 (same-namespace nested record fields) also did not bump this
epoch, staying at 2 -- the strongest "no bump needed" case yet.** Nested
record encode/decode calls no runtime function directly at all: it is pure
composition of the referenced record's own already-generated `_encode()`/
`_decode()`/`_encoded_size()` functions, which themselves call only
functions and `quarry_c_status_t` values already part of the epoch-2
contract (see "Nested record fields" above for why this composition needs
no new validation code). `git diff --stat include/quarry/runtime_c/
binary_record.h CMakeLists.txt` confirms zero changes to either file for
this PR. Generated code compiled against the epoch-2 runtime before PR-113
continues to compile and behave identically; PR-113's generated code
depends on nothing the epoch-2 runtime header didn't already provide.

**PR-114 (bounded arrays of same-namespace record elements) also did not
bump this epoch, staying at 2 -- an even stronger "no bump needed" case
than PR-113's, since this is the first field kind whose *encode* path
(not just decode) needed no runtime addition either.** An investigation
initially concluded a new write-side runtime primitive
(`quarry_c_write_bytes`, an "append pre-encoded raw bytes" function) was
required, by analogy with the C++ backend's implementation -- a follow-up
investigation (recorded in this session's REPORT.md, and summarized in
"Record array fields" above) disproved this: C, unlike C++, already has a
size-only, non-allocating `<Type>_encoded_size()` for every generated
record (since PR-108), which lets generated code learn an array element's
encoded length *before* encoding it, write the length-prefix varuint via
the existing `quarry_c_write_varuint`, and then encode the element
directly into the array field's own writer at its current tail position
-- no temporary buffer, no byte copy, no new function. `git diff --stat
include/quarry/runtime_c/binary_record.h CMakeLists.txt` confirms zero
changes to either file for this PR. Generated code compiled against the
epoch-2 runtime before PR-114 continues to compile and behave identically;
PR-114's generated code depends on nothing the epoch-2 runtime header
didn't already provide.

## Runtime

`include/quarry/runtime_c/binary_record.h` (installed as part of
`Quarry::runtime_c`) is the shared, schema-neutral C runtime this backend
generates calls into: bounded writer/reader state, big-endian scalar
read/write (`quarry_c_write_u32`, etc., matching
`docs/specifications/binary-record-format.md`'s mandatory big-endian byte
order exactly), unsigned LEB128 varuint I/O (`quarry_c_write_varuint`/
`quarry_c_read_varuint` -- reused as-is by PR-112's array element count
prefix and PR-114's record-array per-element length prefix, on top of
their original Field Directory role), whole-record
assembly/parsing (`quarry_c_encode_record`/`quarry_c_parse_record`/
`quarry_c_find_field`), and (PR-110) bounded-string/bytes support
(`quarry_c_is_valid_utf8`, `quarry_c_copy_bounded` -- the latter reused
unchanged by PR-111's bytes fields) -- the same "generic byte mechanics in
the runtime, schema-specific decisions in generated code"
split `docs/principles.md`'s "Compile-Time Knowledge" principle already
states in language-neutral terms. See `runtime_c/CMakeLists.txt` and
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
