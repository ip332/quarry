# Backend (C)

## Header bindings and shared codec

C generation uses one shared BRF v2 codec by default; there is no separate
mode. Generated `.h` files contain enums, bounded caller-owned record structs,
constant host-layout descriptors, and `static inline` type-specific wrappers.
Applications include the generated header and link `Quarry::runtime_c`.
No schema-specific `.c` file needs to be compiled. An include-only `.c` file
is still emitted and listed by `--list-outputs` so existing build scripts
continue to work.

The public `_init`, `_encode`, and `_decode` signatures are preserved,
including the decode result's value and byte-offset fields. `_encoded_size`
returns a `<Type>_encoded_size_result_t` with `status` and `size`. For example:

```c
telemetry_Sample_t sample;
telemetry_Sample_init(&sample);
sample.has_count = true;
sample.count = 42;
telemetry_Sample_encoded_size_result_t measured = telemetry_Sample_encoded_size(&sample);
if (measured.status != QUARRY_C_STATUS_OK) {
    /* Handle the specific validation error before allocating an output buffer. */
    return;
}
/* measured.size is the required output capacity. */
uint8_t output[128];
telemetry_Sample_encode_result_t encoded =
    telemetry_Sample_encode(&sample, output, sizeof(output));
if (encoded.status == QUARRY_C_STATUS_OK) {
    telemetry_Sample_decode_result_t decoded =
        telemetry_Sample_decode(output, encoded.bytes_written);
    /* Check decoded.status before using decoded.value. */
}
```

`quarry/runtime_c/typed_record.h` defines the descriptor contract and generic
entry points. Descriptors use `offsetof`/`sizeof` for host layout and the
compiler's canonical layout for wire placement. Enum load/store adapters
preserve the generated enum type even when the C compiler uses short enums.
Imported record descriptors retain the containing schema's resolved record ID.
Descriptors and wrappers have internal linkage and can be included in multiple
translation units. Their addresses are not stable type identities across TUs.

The shared runtime validates and measures values before modifying the encode
destination, then writes directly into it. Scalars, enums, strings, bytes,
bounded arrays, nested records, and record arrays use the same codec. There
is no heap allocation or worst-case generated scratch buffer. Input/output
buffers and source/destination structs must not overlap, and encoding source
values must remain unchanged during the call. Traversal uses a call stack
bounded by the acyclic schema's record nesting depth.

`_encoded_size()` reports the specific validation status, including invalid
UTF-8, enum values, bounds, and invalid arguments. Its `size` is zero on failure
and is the encoded byte count only when `status == QUARRY_C_STATUS_OK`.
The shared sizing and encoding paths use the same validation rules. Decode initializes its result value to zero and may retain
partially decoded fields on failure; callers must check status. Wire bytes
remain BRF v2. The generated C/runtime API epoch is now **4**; regenerate
headers and link the matching runtime when upgrading.

## Supported data model

Fields support bool, fixed-width signed/unsigned integers, float32/float64,
non-negative enums, bounded strings/bytes, bounded arrays of these types,
nested records, and record arrays. Cross-namespace enums and records use
includes for their generated headers. Imported source units must be generated
as separate explicit roots into the same output tree. Recursive by-value
records and arrays of arrays are rejected.

Each record struct stores fields in declaration order, with an adjacent
`bool has_<field>` flag. String fields use `char[max_bytes + 1]` plus
`uint32_t <field>_length`; bytes use `uint8_t[max_bytes]` plus length. Arrays
use fixed-capacity element storage plus `uint32_t <field>_count`. String/bytes
array elements have named structs containing `value` and `length`. Nested
records are embedded by value. Empty records have a non-schema `reserved`
member to keep them valid C99. Present-empty values remain distinct from
absent fields. Decoded strings receive a trailing NUL, but explicit length
remains authoritative for embedded NULs.

Enums retain namespace-prefixed C typedefs and uppercase enumerators. Enum
fields use the smallest supported unsigned wire width and reject values outside
the declared set. Plain C enum declarations reject values outside signed 32-bit
range; enum fields additionally reject negative declared values.

C keywords, reserved identifiers, and collisions with generated members are
mapped deterministically to safe names. Record declarations are sorted by
by-value dependencies; dependency cycles and file/symbol collisions produce
diagnostics rather than partial output.

## Planning and generation

`Backend::plan()` and `Backend::generate()` share the same internal planning
path. `CodegenOptions` controls the output directory, root stem, and header /
compatibility-source extensions. Each namespace directly owning records or
enums gets a `.generated.h` header and `.generated.c` compatibility file.
Namespaces map to paths such as `quarry/telemetry.generated.h` and symbol
prefixes such as `quarry_telemetry_`. The synthetic root uses `root_file_stem`.

The backend consumes Schema IR and canonical BRF v2 layout metadata. It does
not recompute wire offsets, bitmap positions, fixed/variable storage classes,
or record IDs in the runtime. Descriptor host offsets are C constant
expressions evaluated by the application's compiler.

The public result structs retain the shared `quarry_c_status_t` status values.
Decode errors additionally report `has_byte_offset` / `byte_offset`, composing
nested errors with the containing field or array element's position. The
internal `_encode_with_record_id` / `_decode_with_record_id` helpers remain
available for generated-code interoperability.

## Dependencies and verification

Allowed compiler dependencies are Schema IR, layout, output planning, support,
and diagnostics; the C backend does not depend on the C++ backend. The shared
runtime is independently implemented in C99.

Coverage includes planner/naming tests, generated C99 compilation, short-enum
host representation, scalar boundary values, malformed inputs, capacity
failures, installed-package consumers, and byte-for-byte C/C++/Python
interoperability for nested and variable-width values. The public C example
builds with only its own application translation unit plus the generated
header and installed runtime.
