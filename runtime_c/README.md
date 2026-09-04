# Runtime (C)

## BRF v2 C path

Generated C now includes `quarry/runtime_c/binary_record_v2.h` and emits
canonical BRF v2 records. The compiler-provided layout metadata owns fixed
slot and presence-bit locations; the v2 runtime owns the big-endian header,
8-byte variable descriptors, contiguous declaration-ordered variable tail,
and structural parsing. Generated record setters continue to use the
caller-owned logical C structs, so each encode rebuilds the tail rather than
mutating offsets in place. This keeps updates deterministic and bounded by
the caller's output buffer. C++ and Python generated paths remain explicit
BRF v1 users until their migration PRs.

This module owns generic C runtime support for Quarry binary records --
the C counterpart to `runtime/` (the C++ runtime). The two are independent
sibling implementations: neither depends on the other, and each is
installed under its own canonical path (`include/quarry/runtime/` for C++,
`include/quarry/runtime_c/` for C -- see "CMake Package" below).

**Status: scalar, enum, bounded string, bounded bytes, bounded array (of
scalar, enum, bounded string, bounded bytes, or record elements), nested
record fields, and compiler-resolved cross-namespace enum/record fields and
arrays (PR-108 through PR-139).** Imported source units remain separate
explicit generation roots and must be generated into the same output tree.
Recursive by-value records and nested arrays remain unsupported.
`include/quarry/runtime_c/binary_record.h` is not a speculative framework
for those future features: it exposes exactly the primitives these slices
need. PR-109 (enum fields) added **no new runtime code at all** -- enum
field support only required generated-code changes (see
`compiler/backend_c/README.md`'s "Enum fields" section); every primitive
this runtime exposed after PR-108 was already sufficient. PR-110 (string
fields) is the first increment since PR-108 to add genuinely new runtime
code: `quarry_c_is_valid_utf8` (a UTF-8 validator ported from the C++
runtime's `is_valid_utf8`, so both languages accept/reject exactly the same
byte sequences) and `quarry_c_copy_bounded` (a checked bounds-check-
and-copy primitive for materializing a decoded string's wire bytes into its
generated fixed-capacity buffer). This is also the first bump of
`QUARRY_GENERATED_CODE_API_VERSION_C` (1 -> 2) since the epoch was
introduced -- see `compiler/backend_c/README.md`'s "Generated-code API
version (C)" section for the full reasoning. PR-111 (bytes fields) added
**no new runtime code at all**, back to the PR-109 pattern:
`quarry_c_copy_bounded` is reused completely unchanged for bytes decode (a
bounds-checked byte copy has no UTF-8-specific behavior to begin with), so
the epoch stayed at 2. PR-112 (array fields) likewise added **no new
runtime code at all**: array encode/decode reuses
`quarry_c_write_varuint`/`quarry_c_read_varuint` (present since PR-108 for
the Field Directory itself) for the element count prefix, and the existing
per-width `quarry_c_write_uN`/`quarry_c_read_uN` functions for each
element -- the epoch stayed at 2. PR-113 (same-namespace nested record
fields) added **no new runtime code at all either, and calls no runtime
function directly**: a nested record field's encode/decode is pure
composition of the referenced record's own already-generated `_encode()`/
`_decode()`/`_encoded_size()` functions (themselves built entirely from
already-epoch-2 runtime primitives) -- see `compiler/backend_c/README.md`'s
"Nested record fields" section for why this composition alone is already
sufficient to satisfy every BRF "Nested Records" structural requirement,
with no new validation code anywhere. The epoch stayed at 2. **PR-114
(bounded arrays of same-namespace record elements) also added no new
runtime code**, and is the first field kind whose *encode* side needed no
runtime addition either, not just its decode side: array-of-record encode
composes the existing `<Type>_encoded_size()` (present since PR-108) with
the existing `quarry_c_write_varuint` and a repositioned call to the
existing `<Type>_encode()`, needing no scratch-buffer copy and no new
write-side primitive -- an initial investigation concluded a new
`quarry_c_write_bytes` function was required, but a follow-up
investigation disproved this (see `compiler/backend_c/README.md`'s "Record
array fields" section). The epoch stayed at 2.

Generated C code calls the header-only `quarry_runtime_c` target for
byte-level mechanics while generated code keeps schema-specific knowledge
such as `record_id`, `field_index`, field type, and enum value sets -- the
same split `runtime/README.md` documents for C++.

The generic C encoder's planning workspace and provider-free writer workspace are
separate caller-owned descriptors. Callers that encode nested records must provide
writer frames sized for the maximum active record depth; this capacity is checked
before destination bytes are modified. The writer performs no provider callbacks
and does not consume planning frames.

Current support:

* Binary Record Format v0.1 header emission and parsing
* Field Directory emission and parsing (sorted by `field_index`; duplicate
  and unsorted entries are rejected on decode)
* unsigned LEB128 `varuint` emission and parsing for directory
  offsets/lengths -- reused as-is (PR-112) for a bounded array field's
  element count prefix, and (PR-114) for a record-array field's
  per-element length prefix, composed with the element type's own existing
  `_encoded_size()`/`_encode()` rather than any new primitive
* big-endian scalar emission and parsing for `bool`, fixed-width signed and
  unsigned integers, and IEEE 754 `float`/`double` -- matching
  `docs/specifications/binary-record-format.md`'s mandatory big-endian byte
  order and two's-complement signed-integer representation exactly
* whole-record encode (`quarry_c_encode_record`) and structural decode
  (`quarry_c_parse_record`/`quarry_c_find_field`), the generic "assemble/
  validate header + Field Directory + payload" mechanics every generated
  record's encoder/decoder calls into
* (PR-110) UTF-8 validation (`quarry_c_is_valid_utf8`) and a checked
  bounds-check-and-copy primitive (`quarry_c_copy_bounded`) for bounded
  string fields -- (PR-111) `quarry_c_copy_bounded` reused unchanged for
  bounded bytes fields
* a generated-code API compatibility constant
  (`QUARRY_C_GENERATED_CODE_API_VERSION`) used by generated C headers to
  verify they are compiled against a compatible runtime header

Out of scope for this slice (all deferred, not rejected):

* diagnostic path support (`path`-equivalent locating a failure inside a
  nested record or array element) -- a flat byte offset alone is always
  sufficient to locate a failure deterministically, so this remains
  deferred until a concrete need for symbolic (as opposed to byte-offset)
  failure location is demonstrated
* diagnostic strings of any kind (see "Diagnostic Context" below -- the
  same closed decision as C++'s, not language-specific)
* zero-copy/view-based decode, caller-provided arenas for variable-length
  data (see `docs/design/c-backend.md` Section 2)

## Wire format

Byte-for-byte identical to what the C++ runtime produces and consumes --
BRF is a single, language-neutral wire format by design
(`docs/principles.md`: binary records are the canonical runtime
representation, independent of whichever language produced or consumes
them). This is verified directly:
`tests/interop/c_cpp_codec_interop_test.cpp` generates the same schema
through both backends and proves the C encoder's output is byte-for-byte
identical to the C++ encoder's output for the same field values, that the
C++ decoder accepts C-encoded bytes, and that the C decoder accepts
C++-encoded bytes.

## Bounded design (no heap, no unbounded stack)

`QUARRY_C_MAX_DIRECTORY_ENTRIES` (currently `64`) bounds how many Field
Directory entries a single `quarry_c_encode_record`/`quarry_c_parse_record`
call handles. The wire format itself allows up to 256 fields per record
(`fieldIndex` is one byte); this runtime deliberately supports fewer so
encode/decode use only small, fixed-size local storage instead of a full
256-entry worst case, matching `docs/principles.md`'s "bounded memory
usage" and "limited heap allocation" embedded-first principles. Exceeding
the bound is reported as `QUARRY_C_STATUS_UNSUPPORTED_FIELD_COUNT` -- a
runtime limitation, not a BRF wire-format defect, and therefore (unlike
every other decode failure) carries no byte offset. Revisit this bound if a
real schema needs more present fields in one record than it allows.

## Diagnostic context

`quarry_c_status_t` is a single, shared status enum covering both
structural/wire failures (e.g. `QUARRY_C_STATUS_TRUNCATED_HEADER`,
`QUARRY_C_STATUS_MALFORMED_VARUINT`) and schema-level failures generated
code itself detects (`QUARRY_C_STATUS_UNEXPECTED_RECORD_ID`,
`QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE` -- declared in PR-108 in anticipation
of enum field support and actually produced by generated code since PR-109,
which needed no runtime change to start using it: reserving schema-level
statuses in this one shared enum ahead of their first generated-code
producer means generated code never needs a second, parallel status type).
PR-110 added two more values to this same shared enum:
`QUARRY_C_STATUS_BOUNDS_EXCEEDED` (a string field's logical/wire length
exceeds its schema-declared `max_bytes` bound) and
`QUARRY_C_STATUS_INVALID_UTF8` (string content fails UTF-8 validation),
mirroring the C++ runtime's `DecodeError`/`EncodeError` variants of the
same names exactly.
Decode results additionally carry `byte_offset`
(`has_byte_offset`/`byte_offset`), populated for every decode failure except
`QUARRY_C_STATUS_UNSUPPORTED_FIELD_COUNT` (see above). Encode results never
carry a byte offset, for the same reason the C++ runtime's `EncodeResult`
never does: every encode error is a schema-value violation against a value
the caller already holds, not a wire-position problem.

**No diagnostic strings, and this is a closed decision, not a placeholder**
-- the same closed decision `runtime/README.md`'s "Diagnostic String
Boundary" section documents for C++ (PR-101), which was never
language-specific: every current status value is fully explained by the
enum itself plus, on decode, `byte_offset`; a caller who wants presentation
text for logging writes their own small mapping over this fixed status set
in their own downstream code (`compiler/backend_c/README.md`'s discussion of
the generated codec API rejects "separate diagnostic output objects" for
the same reason).

## CMake Package

`quarry_runtime_c` is a header-only `INTERFACE` target in the source tree,
requiring C99 (`c_std_99`). Installation exports it as `Quarry::runtime_c`
through the same `Quarry` CMake package the C++ runtime uses:

```cmake
find_package(Quarry CONFIG REQUIRED)
target_link_libraries(my_c_app PRIVATE Quarry::runtime_c)
```

Installed consumers should include:

```c
#include <quarry/runtime_c/binary_record.h>
```

This is the single canonical public include path -- no other install path
or include convention is provided, matching the discipline PR-104
established for the C++ runtime's own single canonical path. The package
also installs `quarry/runtime_c/version.h` (build-generated, carries
`QUARRY_C_GENERATED_CODE_API_VERSION`). There is no
`quarry_generate_c()` CMake helper yet (see
`docs/distribution-model.md`); downstream projects that want to generate C
from an installed package use the manual `add_custom_command()` pattern
documented in `tools/README.md`, exactly as
`tests/consumer/schema_compiler_package_test.cpp`'s
`CConsumerBuildsAndRunsAgainstInstalledPackage` test does. That test also
asserts the installed tree exposes exactly
`include/quarry/runtime_c/{binary_record.h,version.h}` with no duplicate or
generic unprefixed C runtime headers.
