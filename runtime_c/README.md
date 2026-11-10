# Runtime (C)

## Shared typed BRF v2 codec

`typed_record.c` implements the descriptor-driven codec declared in
`quarry/runtime_c/typed_record.h`. Generated C headers contain the data types,
immutable descriptors, and inline `<Type>_encode()` / `<Type>_decode()` wrappers;
applications compile their own sources and link `Quarry::runtime_c`. The
include-only generated `.c` compatibility file is optional to compile.

This codec supports all currently generated C field kinds, uses the canonical
BRF v2 layout, performs no heap allocation, and validates encode values and
capacity as each field is written. Failed output must be discarded or cleared.
Host member offsets, enum representation, and array strides come from generated descriptors rather than serialized storage.
`_encoded_size()` returns a result with `status` and `size`; failures report the
specific status and set `size` to zero. Descriptors are trusted
compiler output; they are not a format for accepting untrusted host offsets.
Buffers must not overlap their source/destination structs, and encode source
values must remain stable for the complete call. Schema nesting bounds the
runtime's recursive call stack. The C generated-code API epoch is 7.

## BRF v2 generated bindings

Generated C includes `quarry/runtime_c/typed_record.h`, which builds on
`binary_record_v2.h` for structural parsing and scalar byte mechanics. The
compiler's canonical layout supplies field slots, presence bits, fixed-region
size, and nested-record storage classes. Generated descriptors additionally
map each logical field to its host member offset, element stride, length/count
member, enum adapters, and nested descriptor. Host structs are never treated
as wire images.

The typed codec supports bool, fixed-width integers, floats, non-negative
enums, bounded string/bytes fields, bounded arrays, nested records, and record
arrays, including imported types. Strings use explicit byte lengths and gain a
trailing NUL on decode. Presence flags remain for scalars, enums, and nested records. String/bytes
lengths and array counts determine presence directly: zero encodes as absent.
Legacy present-empty wire fields remain decodable. Empty elements of nonempty
arrays are retained. Enum values, bounds, UTF-8, array framing, and nested records
are validated by the shared codec. Recursive by-value records and nested
arrays remain unsupported by the schema/generator model.

Encoding validates each field immediately before writing it, in one traversal,
without a sizing pass or per-call plan cache. It fills record lengths and variable
field descriptors after their payloads are written. Failure returns an error and
`bytes_written` reports the output span touched, including fixed-region clearing
and partial nested writes. Bytes beyond that span remain untouched. The span
may contain partial or invalid bytes; it is not a valid record length on error. Callers
must discard or clear it before reuse. The runtime does not roll back or clear
failed output. Error precedence follows traversal and available capacity.
`encoded_size()` remains a separate validating sizing operation when needed.
Decode validates structure and copies field values in one traversal, reading
each presence bit and slot once. Structural errors (header, presence bitmap,
slot bounds, nonzero absent slots, descriptor chaining, record length) take
precedence over value errors (bool, enum, bounds, UTF-8, nested records): the
first value error is held while the rest of the structure is validated, so
statuses and byte offsets are the same as validating the whole record first.
Absent slots are checked a word at a time. Result values are zeroed initially
and may contain partially decoded fields on failure, including after a
structural error; applications must check status before using them.

Generated type descriptors also carry per-field decode operations
(`quarry_c_field_decode_op_t`): the wire presence byte/mask, slot size, a
resolved decode kind (direct 1/2/4/8-byte integer/float copy, bool, other
scalar, fixed or variable field), and the native presence operation, offsets
and clear size. They are immutable schema metadata emitted by the compiler, so
decode does not reclassify fields from the layout and member descriptors on
every record. A descriptor with `decode_ops == NULL` (for example, a hand-built
one) decodes from the layout and member descriptors alone with identical
results.

The common typed-runtime boundary uses fixed-width kind/status members so
application enum-storage options do not change descriptor/result layouts.
Generated enum load/store adapters use the application's enum type; generated
public results retain `quarry_c_status_t`.

## Generic QBS view/provider API

The independent QBS-driven C API remains available for applications that do
not use generated structs. It indexes caller-owned QBS bytes into typed
workspace arrays, validates BRF records into zero-copy views, and encodes
logical values supplied by providers. Its parser, validation, encoder, writer,
and traversal workspaces are caller-owned and bounded; there is no hidden
heap allocation. View and provider lifetimes follow their source buffers and
workspaces. See [Generic C QBS/BRF runtime](../runtime/README-generic-c-runtime.md)
for supported wire-format contracts, limits, and examples. These APIs are
independent of the generated typed BRF v2 bindings.

## Reflective QBS names

The generic QBS API exposes optional reflective names without exposing the
binary string-section representation. `quarry_qbs_get_string()` resolves a
string-table index, while `quarry_qbs_record_name()`,
`quarry_qbs_field_name()`, and `quarry_qbs_enum_name()` resolve descriptor
names through their existing `name_index` metadata. Each result is the public
`quarry_string_view_t` (`data` plus `size`), points into the caller-owned QBS
input buffer, is not NUL-terminated, and remains valid only while that buffer
is unchanged and alive.

Minimal QBS images do not contain reflective strings. Name lookup then returns
`QUARRY_GENERIC_FIELD_ABSENT`; the image remains valid and all structural
metadata remains usable. An invalid string index returns
`QUARRY_GENERIC_FIELD_NOT_FOUND`, while an invalid argument or malformed
string-table state returns the corresponding generic status.

QBS currently stores enum type names and numeric enum values, but not symbolic
names for individual enum values. These APIs therefore do not provide enum
value names; consumers should render enum values numerically.

## Streaming BRF inspection

`quarry_brf_print()` formats a validated BRF record by consuming the existing
`quarry_brf_traverse()` event stream. It does not parse BRF bytes independently
and is intended for diagnostics, logging, debugging, and development tools;
its output is not a serialization format. Output is sent incrementally to a
caller-provided writer callback, with caller-owned printer and traversal
workspaces. Reflective QBS names are used when available; minimal QBS falls
back to labels such as `record[1]` and `field[0]`. Strings are escaped and
bytes are rendered as hexadecimal. Enum values are rendered numerically.

## Diagnostics

Generated wrappers retain the common `quarry_c_status_t` status values for
structural errors and schema violations such as unknown enums, bounds, and
invalid UTF-8. Encode results carry `bytes_written` on success and failure: the extent of the
output span touched, never exceeding capacity. Zero means nothing was written.
A nonzero failure count includes partial nested writes and is only suitable for
discarding or clearing that span, not transmitting a record.
Decode results carry `has_byte_offset` and `byte_offset`, composing failures
across nested fields and record-array elements. The runtime does not allocate
diagnostic strings; applications may map statuses to presentation text.

## CMake package

The source-tree `quarry_runtime_c` target is a C static library, exported as
`Quarry::runtime_c` by the installed Quarry CMake package:

```cmake
find_package(Quarry CONFIG REQUIRED)
target_link_libraries(my_c_app PRIVATE Quarry::runtime_c)
```

Applications include their generated `.generated.h` files and compile their
own sources. The compiler still emits an include-only `.generated.c` file to
preserve existing output plans and custom commands; compiling it is optional.
See [the C example](../examples/c/basic_encode_decode/README.md) for an
installed-package application using only its generated header.

Public runtime headers use the canonical `quarry/runtime_c/` include path.
`version.h` supplies the generated-code API epoch. `binary_record.h` retains
legacy byte primitives and `binary_record_v2.h` provides BRF v2 structural
helpers; neither alone replaces linking the typed codec for current generated
bindings. The C and C++ runtimes are independent sibling implementations.

### Shared typed codec work per call

The typed encoder validates and writes in a single recursive traversal. Presence
is checked once per field using a generated bool/bitmap/count operation; enum membership, bounds and UTF-8 are checked as
values are written. Fixed-width scalars write directly from the record loop,
bypassing array/element dispatch while retaining scalar and enum checks.
There is no encode-plan workspace or preliminary measurement.
The wire header/fixed region is zeroed for canonical absent slots and padding;
variable payload bytes are overwritten without a preliminary clear. Lengths and
offsets are backfilled. Prefixed array elements reserve one byte, then shift their
encoded payload if a longer canonical varuint prefix is needed. This accepts
exact-size output buffers without measuring elements first. `encoded_size()`
still performs a separate complete validation and size calculation.

The typed decoder validates the wire layout before reading values, then accesses
fields directly by their layout ordinal rather than searching their schema field
indexes. Native decode storage is still fully zero-initialized, including absent
values and unused capacity. This preserves the existing decode behavior.
