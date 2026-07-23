# Runtime

This module owns generic runtime support for Quarry binary records.

Generated C++ schema code calls the header-only `quarry_runtime` target for
byte-level mechanics while generated code keeps schema-specific knowledge such
as `record_id`, `field_index`, field type, and enum value sets.

Current support:

* top-level Binary Record Format v0.1 header emission
* top-level Binary Record Format v0.1 header parsing
* Field Directory emission sorted by `fieldIndex`
* structural Field Directory parsing and field lookup
* unsigned LEB128 `varuint` emission for directory offsets and lengths
* unsigned LEB128 `varuint` parsing for directory offsets and lengths
* big-endian scalar byte emission
* big-endian scalar byte parsing
* raw byte-sequence emission and parsing for generated `bytes` fields
* UTF-8 validation plus raw byte emission and parsing for generated `string`
  fields
* fixed-width array payload emission and parsing for generated arrays of
  supported scalar and enum leaf types
* length-delimited array payload emission and parsing for generated arrays of
  `string`, `bytes`, and record-reference element types
* complete embedded BRF record payloads for generated nested record fields
* owning `std::vector<std::byte>` encode results
* compact runtime parse/read errors used by generated decoders
* structured encode and decode result helpers for generated diagnostic codec
  APIs, including nested field-path, array-index, and byte-offset diagnostic
  context for decode failures
* a generated-code API compatibility constant used by generated C++ headers to
  verify that they are being compiled with a compatible runtime header

Generated decoders currently materialize generated record values from
caller-owned byte spans. Unknown field indexes are ignored after structural
validation. Present known fields whose types are not yet supported by the
generated decoder cause generated decoding to fail.

`kGeneratedCodeApiVersion` is a narrow source-compatibility marker for generated
C++ code. Generated headers compile-time assert that the runtime exposes the
expected value. This guard does not represent the Quarry package release
version, C++ ABI stability, schema-language compatibility, or BRF wire-format
compatibility.

The generated-code API version is owned by the top-level
`QUARRY_GENERATED_CODE_API_VERSION` CMake scalar. CMake validates that it
is a non-negative `std::uint32_t` value, configures the public runtime
`version.hpp` header from it, configures the compiler backend's private
generated-code API header from the same value, and writes it into installed
package metadata as `Quarry_GENERATED_CODE_API_VERSION`. The installed
schema compiler prints the backend-side value with
`--print-generated-code-api-version`.

Generated code exposes diagnostic codec APIs backed by
`EncodeResult<T>`/`DecodeResult<T>`. The runtime result type carries either an
owning value or a compact error enum. `EncodeError` distinguishes schema-bound
failures, invalid UTF-8, unknown enum values, unsupported present field types,
and runtime overflow. `DecodeError` includes structural parse/read failures as
well as generated schema failures such as unexpected record IDs, bound
violations, invalid UTF-8, unknown enum values, and unsupported present field
types. Generated compatibility wrappers may still collapse those errors to
`std::nullopt`.

Generated nested-record and record-array codecs propagate the child codec's
root error code unchanged, and extend a structured path and a translated
absolute byte offset as the failure unwinds outward. Error results do not yet
carry diagnostic strings.

### Codec Diagnostic Context

`CodecResult<T, E>` (aliased as `EncodeResult<T>`/`DecodeResult<T>`) carries a
`std::vector<PathElement> path` member alongside `value` and `error`, so a
caller can determine exactly where in a nested record or array a codec
failure occurred, not only which error kind occurred.

* `PathElement` holds a `field_index` and an optional `array_index` (present
  only when that frame represents an array element).
* `path` is empty on success, and on any failure that originates directly in
  the top-level record with no nesting (e.g. structural header/record-ID
  failures detected before any field is examined).
* Each nesting level appends (`push_back`) its own frame as a failure
  unwinds outward through generated codecs: `path.front()` is always the
  innermost/deepest failure site, and `path.back()` is the outermost field
  that led to it. Callers who want root-to-leaf display order reverse the
  vector themselves; the runtime does not reverse it for them.
* Leaf-detecting generated code — the field-level `render_*_field_encoding`/
  `render_*_field_decoding` call sites that already know their own
  `field_index` — attaches the first, single-element path directly at the
  point of failure. Wrapping code for nested records and array elements
  (which already receives a child `CodecResult` carrying its own `path`)
  appends its own frame and forwards the error code unchanged.
* Whole-field structural failures with no attributable element (array
  element-count/length parsing before any element loop, trailing-byte
  mismatches after the loop, builder rejection after all elements decoded)
  attach a field-level frame with no `array_index`.
* This is a breaking change to `CodecResult`'s shape, reflected in
  `kGeneratedCodeApiVersion`.

### Byte-Offset Context

`CodecResult<T, E>` carries a `std::optional<std::uint64_t> byte_offset`
member alongside `value`, `error`, and `path`, populated for decode failures
only.

* **Decode only.** Every `EncodeError` origin is a schema-value violation
  against an already-fully-known value, never malformed wire bytes — there is
  no meaningful byte position to report, and approximating one (e.g. "where
  this field would have landed in the output") would require computing the
  final field layout before validation runs, inverting the single-pass,
  fail-fast encode architecture, and would be actively wrong whenever field
  declaration order and `field_index` order diverge. `EncodeResult.byte_offset`
  stays permanently `std::nullopt`; the member exists so `CodecResult<T, E>`
  remains one shared type across encode and decode rather than bifurcating the
  result type.
* **Absolute, not relative.** `byte_offset` is measured from the start of the
  byte span given to the top-level generated decoder — the same buffer the
  caller already has — not from the start of whatever nested/embedded record
  span happened to contain the failure. Each nesting level translates its
  child's reported offset by adding the absolute position (already known at
  that level, by induction) where the child's span begins, mirroring how
  `path` frames already accumulate outward through nested forwarding.
* **`uint64_t`, not `size_t`.** Matches the width already used internally for
  Field Directory `fieldOffset`/`fieldLength`, and avoids a truncation risk on
  32-bit embedded targets that a platform-width `size_t` would carry.
* **Deterministic byte per error category** (no "near the error" wording):
  * Whole-record failures detected before any field is examined identify the
    start of that record (offset `0`): truncated header, unsupported version,
    unexpected record ID. `unsupported_flags` identifies the flags byte
    (offset `1`).
  * `invalid_header`'s two independent reserved-byte checks were split into
    two separate return sites so each reports its own byte precisely:
    `reserved0` at offset `3`, `reserved1` at offset `8` (previously combined
    into one check with no way to distinguish them).
    `invalid_payload_length`'s two independent checks were left combined,
    since both are about the same header field being inconsistent with the
    input: both report offset `12`, the start of the `payloadLength` field.
  * Field Directory failures (malformed directory, malformed varuint,
    duplicate/unsorted entries) identify the start of the specific directory
    entry being parsed when the failure occurred. Invalid/overlapping field
    range failures identify the claimed field-payload position instead (the
    position the directory entry asserts, even though that assertion is
    itself invalid).
  * Field-level failures on an otherwise structurally valid field (invalid
    field length, invalid bool, invalid UTF-8, unknown enum value,
    unsupported field type, decode-time bounds rejection) identify the start
    of that field's payload, via the new `FieldView::field_offset` member.
  * Array element failures identify the start of the specific element (or its
    length prefix, for length-varuint failures) within the array field's
    payload. Trailing unconsumed array bytes identify the first unconsumed
    byte — more precise than the array's field-level `path` frame alone.
  * Every varuint read site that contributes to a reported offset captures
    its cursor position *before* calling `read_varuint`, not after:
    `read_varuint` mutates its `offset` output parameter during the read,
    including partial advancement before a terminal failure.
* **Runtime support added.** `FieldView` gained a `field_offset` member so
  field-level generated decoders can report a position without re-deriving
  it. `ParseRecordResult` gained an `offset` member so `parse_record`'s
  structural failures carry one through to the generated
  `decode_..._result` wrapper. `read_varuint` itself needed no signature
  change — its callers already held the pre-call cursor.
* This is a breaking change to `CodecResult`'s shape, reflected in
  `kGeneratedCodeApiVersion` (bumped again for this change).

Generated string codecs validate UTF-8 on both encode and decode. Embedded
U+0000 is valid, and no Unicode normalization is performed. Generated bytes
codecs accept arbitrary byte sequences. Present empty strings and bytes are
encoded as zero-length Field Directory entries, which remain distinct from
absent fields.

Generated array codecs encode a present array as a Field Directory entry whose
payload starts with an unsigned LEB128 element count. Fixed-width element arrays
then use tightly packed element bytes. Arrays of `string` and `bytes` then use
one unsigned LEB128 element length followed by raw element data for each element;
there is no offset table, terminator, or array-level internal byte length.
Present empty arrays encode the canonical zero count byte and remain distinct
from arrays containing empty elements. Decoders validate the schema
`max_elements` bound before allocating materialized vectors, validate
per-element `max_bytes` before copying variable-length elements, and require
exact array payload consumption.

Generated `array<string>` codecs validate UTF-8 for every element on encode and
decode. Generated `array<bytes>` codecs accept arbitrary byte sequences.
Generated `array<record>` codecs store each element as one length-delimited
complete embedded BRF record and reuse the referenced record's generated
encoder and decoder for each element.

Generated nested record fields are encoded as complete embedded Binary Record
Format v0.1 records. The parent field length bounds the full embedded record
byte sequence, including the nested 16-byte header. Generated decoders reuse the
nested record's generated decoder, so nested `record_id`, version, flags,
reserved fields, payload length, and exact input consumption are validated by
the same runtime parser used for top-level records.

Out of scope:

* dynamic Schema IR or manifest interpretation
* nested arrays
* unknown-field preservation
* byte-offset context for encode errors (see "Byte-Offset Context" above for
  why); diagnostic strings for codec errors
* generated read/view APIs
* zero-copy or caller-provided output buffers

Runtime code must not depend on compiler libraries, YAML, Schema IR protobufs,
source-schema models, symbols, semantic validation, layout, or backend code.

## CMake Package

`quarry_runtime` is a header-only `INTERFACE` target in the source tree.
Installation exports it as `Quarry::runtime` through the `Quarry`
CMake package:

```cmake
find_package(Quarry CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Quarry::runtime)
```

Installed consumers should include:

```cpp
#include <quarry/runtime/binary_record.hpp>
```

The package installs `QuarryConfig.cmake`,
`QuarryConfigVersion.cmake`, public runtime headers including
`<quarry/runtime/version.hpp>`, and the exported runtime target. The
packaging verification test under `tests/consumer/runtime_package` installs the
runtime to a temporary prefix, configures a separate CMake project with
`find_package(Quarry CONFIG REQUIRED)`, links `Quarry::runtime`, and
runs a small encode/decode smoke executable.

This is the complete supported installed SDK surface today. Compiler targets,
schema compiler tools, generated protobufs, fuzzers, tests, and examples are not
installed or exported by the runtime package; see
`docs/distribution-model.md`.

## Fuzzing

BRF parser fuzz targets are available behind the opt-in
`QUARRY_BUILD_FUZZERS` CMake option. The normal debug build does not build
or run fuzzers.

The `debug-fuzz` preset builds Clang/libFuzzer targets with AddressSanitizer and
UndefinedBehaviorSanitizer enabled:

* `brf_parse_fuzzer` feeds arbitrary bytes to `parse_record` and checks generic
  parser invariants for successful parses.
* `brf_generated_decode_fuzzer` feeds arbitrary bytes to a representative
  generated-style decoder covering scalars, enums, strings, bytes, arrays,
  nested records, and arrays of records.

The reviewable seed corpus lives under `fuzz/corpus/brf` as hexadecimal byte
files. `fuzz/run_seed_corpus.py` converts those seeds to temporary raw inputs
and runs a selected fuzz executable against them. Fuzzing is a hardening tool,
not a proof of complete parser correctness; deterministic malformed-input unit
tests remain the regression mechanism for discovered defects.
