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
  APIs
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
root error code directly. Error results do not yet carry field paths, array
indexes, byte offsets, or diagnostic strings.

### Codec Diagnostic Context (Decided, Not Yet Implemented)

A future increment will add structured nested diagnostic context to
`EncodeResult<T>`/`DecodeResult<T>` so a caller can determine exactly where in
a nested record or array a codec failure occurred, not only which error kind
occurred. The design is decided; no code in this revision implements it yet.

* `CodecResult<T, E>` gains a `std::vector<PathElement> path` member, where
  `PathElement` holds a `field_index` and an optional `array_index` (present
  only when that frame represents an array element).
* `path` is empty on success, and on any failure that originates directly in
  the top-level record with no nesting.
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
* Byte-offset context remains a separate, still-undecided follow-up; it is
  not part of this decision.
* This changes the shape of a type generated code depends on and requires
  bumping `kGeneratedCodeApiVersion`.

The next coherent PR should implement this exact shape end to end (runtime
type, generated codec propagation at every nesting site, and the API version
bump) rather than choosing a different design at implementation time.

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
* byte-offset context for codec errors (field-path and array-index context
  are decided but not yet implemented; see "Codec Diagnostic Context" above)
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
