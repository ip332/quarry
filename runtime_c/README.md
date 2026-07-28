# Runtime (C)

This module owns generic C runtime support for Quarry binary records --
the C counterpart to `runtime/` (the C++ runtime). The two are independent
sibling implementations: neither depends on the other, and each is
installed under its own canonical path (`include/quarry/runtime/` for C++,
`include/quarry/runtime_c/` for C -- see "CMake Package" below).

**Status: scalar codec vertical slice (PR-108).** This runtime supports
generated code for scalar-only records. No string/bytes/array/nested-record
support exists yet -- see `compiler/backend_c/README.md` and
`docs/design/c-backend.md` for the current implemented subset and the
roadmap for later increments. `include/quarry/runtime_c/binary_record.h` is
not a speculative framework for those future features: it exposes exactly
the primitives this slice needs.

Generated C code calls the header-only `quarry_runtime_c` target for
byte-level mechanics while generated code keeps schema-specific knowledge
such as `record_id`, `field_index`, field type, and (once implemented) enum
value sets -- the same split `runtime/README.md` documents for C++.

Current support:

* Binary Record Format v0.1 header emission and parsing
* Field Directory emission and parsing (sorted by `field_index`; duplicate
  and unsorted entries are rejected on decode)
* unsigned LEB128 `varuint` emission and parsing for directory
  offsets/lengths
* big-endian scalar emission and parsing for `bool`, fixed-width signed and
  unsigned integers, and IEEE 754 `float`/`double` -- matching
  `docs/specifications/binary-record-format.md`'s mandatory big-endian byte
  order and two's-complement signed-integer representation exactly
* whole-record encode (`quarry_c_encode_record`) and structural decode
  (`quarry_c_parse_record`/`quarry_c_find_field`), the generic "assemble/
  validate header + Field Directory + payload" mechanics every generated
  record's encoder/decoder calls into
* a generated-code API compatibility constant
  (`QUARRY_C_GENERATED_CODE_API_VERSION`) used by generated C headers to
  verify they are compiled against a compatible runtime header

Out of scope for this slice (all deferred, not rejected):

* enum-typed fields, `string`, `bytes`, arrays, nested records, arrays of
  records
* diagnostic path support (`path`-equivalent locating a failure inside a
  nested record or array element) -- there is no nesting yet for a path to
  describe
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
`QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE` -- the latter not yet produced by
anything, since enum fields aren't implemented yet, but reserved in the
shared enum so generated code will not need a second, parallel status type
once they are). Decode results additionally carry `byte_offset`
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
