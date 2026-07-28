# C Backend

## Purpose

This document proposes an architecture and public API for a C language
generator ("the C backend"): the second production-quality Schema Compiler
backend, after the C++ reference implementation.

This is a design proposal, not a specification and not implementation
documentation. It records the rationale for a set of recommended decisions so
that future implementation PRs have a foundation to build against instead of
re-deriving these choices ad hoc, field by field. Precise generated-output
formats belong to a future specification once an initial C backend exists to
specify; this document intentionally stays one level above that.

`docs/architecture/language-generators.md` already anticipates this backend
in outline (symbol-prefix namespacing, "one schema produces equivalent APIs in
every language") and `docs/backend-api.md` already states the constraint this
proposal must satisfy ("new backends may be added without modifying the
compiler pipeline... adding a backend does not require changes to compiler
passes"). This document is the concrete design that instantiates both.

---

## Summary

* **Public API shape:** generated plain C structs (fixed-capacity, no heap
  allocation) plus generated free functions — not opaque handles, not a
  builder *type* (C cannot express one), but setter functions preserved for
  validated writes.
* **Memory ownership:** every variable-length field already carries a
  compiler-enforced, schema-declared upper bound (`max_bytes`/`max_elements`).
  Generated structs use fixed-capacity inline storage sized from that bound.
  No heap allocation in generated code or in the C runtime.
* **Runtime split:** unchanged in spirit from the C++ backend — generic BRF
  byte mechanics live in a shared, header-only C runtime; schema-specific
  knowledge lives in generated code. The diagnostic path/offset containers
  also live in the shared runtime, sized generically rather than per-record.
* **Error model:** full parity with `CodecResult` — error enum, path,
  byte-offset — realized as one non-generic result struct per record (C has
  no templates), embedding a shared, fixed-capacity path type.
* **Naming:** namespace FQN becomes a `quarry_<namespace>_` symbol prefix,
  exactly as `language-generators.md` already specifies.
* **File layout:** paired `.h`/`.c` per namespace file, not header-only —
  the one deliberate divergence from the C++ backend's approach, justified
  below.
* **Build integration:** the existing installed-package model (imported
  CMake targets, a generation helper, a generated-code API compatibility
  epoch) extends directly; it needs a second epoch scalar and a second
  generation helper/target, not a redesign.
* **Backend architecture:** the existing `compiler/backend/` implementation
  does not generalize to C in place — it is C++-specific starting at its
  second phase — so the C backend should be a new, independent sibling
  library, not a parameterization of the existing one.
* **Wire format:** no changes. BRF is already a plain byte-level format with
  no C++-specific assumption anywhere in it.

---

## 1. Public C API

### Alternatives considered

**Struct-based (fixed layout, public fields plus generated accessor/setter
functions).** A plain C `struct` per record, with presence tracked by
explicit `bool has_<field>` members alongside each optional field, is the
idiomatic representation for schema-derived data in C — the same role
`nanopb` and `protobuf-c` structs play for protobuf messages. Selected; see
below for why fields stay technically public but access stays function-based.

**Opaque handles (forward-declared incomplete struct, all access through
accessor functions, allocation hidden behind `create`/`destroy`).** Rejected.
Opaque handles almost always imply heap allocation or an internal arena
hidden from the caller, which cuts directly against `docs/principles.md`'s
"limited heap allocation" and "bounded memory usage" for embedded/RTOS/
bare-metal targets — exactly the class of target a C backend exists to
serve that the C++ backend does not. Opaque handles also prevent callers
from placing records in static memory, stack memory, or memory-mapped
regions, all common embedded patterns this design should not foreclose.

**Builder API (a distinct builder type, mirroring the C++
`RecordNameBuilder` class, transitioning to an immutable value type via
`build()`).** Rejected as a literal port. C has no access control, so a
builder *type* cannot enforce anything a plain struct doesn't already allow;
the builder/value split existed in C++ specifically to get compiler-enforced
immutability and encapsulated validation, neither of which C's type system
can express. Carrying the split anyway would just be two struct types with
an unenforced convention between them.

**Free functions over caller-defined structs (the schema compiler emits only
codec functions; the application hand-writes the data structures).**
Rejected. This would make hand-written structs the source of truth for
record shape, contradicting `docs/principles.md`'s "derived views... do not
define the canonical model" and this project's schema-driven premise. The
schema compiler must still generate the struct definitions.

### Recommendation

Generate one plain C struct per record and per array-of-scalars/enums field
shape, with:

* one member per field; presence for optional-by-nature fields tracked by an
  adjacent `bool has_<field>` member (the direct translation of C++'s
  `std::optional<T>` presence tracking, since C has no `std::optional`);
* fixed-capacity inline storage for `string`, `bytes`, and `array` fields,
  sized from the field's schema-declared bound (see Section 2);
* one generated `bool quarry_<ns>_<Record>_set_<field>(quarry_<ns>_<Record>_t*
  record, <field-type> value)` per field, performing the same
  atomic-validated-write the C++ builder's setter performs today (reject
  invalid values wholesale, leave the struct's previous state untouched on
  rejection) — this is the load-bearing reason setters are kept as the
  documented way to populate a record instead of "just assign the struct
  fields directly," even though C cannot prevent the latter;
* one generated `bool quarry_<ns>_<Record>_has_<field>(const
  quarry_<ns>_<Record>_t* record)` per field;
* one generated `void quarry_<ns>_<Record>_init(quarry_<ns>_<Record>_t*
  record)` that zero-initializes a record to the all-fields-absent state
  (the C equivalent of default-constructing a C++ builder), replacing the
  C++ backend's separate builder-then-`build()` step, since a C struct has no
  meaningful distinction between "being built" and "built" beyond the
  documented convention of calling `_init()` first.

Sketch, for the same `quarry.telemetry.Sample { count: uint32 }` schema used
by `examples/cpp/schema_compiler_cmake`:

```c
typedef struct {
    bool has_count;
    uint32_t count;
} quarry_telemetry_Sample_t;

void quarry_telemetry_Sample_init(quarry_telemetry_Sample_t* record);
bool quarry_telemetry_Sample_has_count(const quarry_telemetry_Sample_t* record);
bool quarry_telemetry_Sample_set_count(quarry_telemetry_Sample_t* record, uint32_t value);
```

Records and arrays of records nest by value (Section 2), so a nested-record
field is simply another generated struct embedded inline — no pointers, no
allocation, no lifetime to manage beyond the parent's own.

---

## 2. Memory Ownership

### What the schema already guarantees

`docs/specifications/schema-language.md` requires `max_bytes` on every
`string`/`bytes` field and exactly one `max_elements` on every array field,
and both SHALL be positive. This is not a convention the C backend would need
to introduce — it is an existing, mandatory, validated schema constraint
(`compiler/semantic`, carried unchanged into `schema_ir.proto`'s
`StringType`/`BytesType`/`ArrayType`). Consequently, **the worst-case
encoded and decoded size of any record is already computable at
schema-compile time**, recursively through nested records and arrays of
records, because the backend already rejects reference cycles
(`compiler/backend/README.md`: "fails clearly when record dependencies form
a cycle"). This is the property a no-heap C design needs, and Quarry already
has it for reasons unrelated to any C backend.

### Investigated areas

* **Strings.** Fixed-capacity `char` array member sized `max_bytes + 1`
  (room for a trailing NUL, so decoded strings can be handed to C string
  APIs directly) plus a `uint32_t <field>_len` byte-length member (UTF-8
  byte length, matching how `max_bytes` is already specified — not
  necessarily equal to `strlen` if the string contains embedded `U+0000`,
  which the runtime documents as valid).
* **Bytes.** Fixed-capacity `uint8_t` array member sized `max_bytes` plus a
  `uint32_t <field>_len` member. No NUL terminator concern (arbitrary bytes).
* **Arrays.** Fixed-capacity array of the element type, `[max_elements]`,
  plus a `uint32_t <field>_count` member. Arrays of `string`/`bytes` are
  arrays of the fixed-capacity string/bytes shape above. Arrays of records
  are arrays of the nested record struct, by value.
* **Nested records.** Embedded inline, by value, as a normal (possibly
  large) struct member — not a pointer, not heap-allocated. A `has_<field>`
  bool still tracks presence for optional nested-record fields, matching
  scalar-field presence tracking.
* **Zero-copy decode.** Investigated and rejected as the default, for now.
  The C++ runtime's own wire-parsing layer (`parse_record`/`FieldView`) is
  already zero-copy — `FieldView::bytes` is a non-owning span into the
  caller's input buffer — but `runtime/README.md` lists "generated read/view
  APIs" and "zero-copy or caller-provided output buffers" as explicit,
  current out-of-scope items for the *materialized* generated record types,
  for both languages, not as a C++-specific gap. A C zero-copy decode API
  (returning views into the input buffer instead of copying into a
  fixed-capacity struct) is a legitimate, larger follow-on design — arguably
  more valuable for C than for C++, given embedded targets' memory pressure
  — but it is a separate decision with its own lifetime and aliasing
  questions, and should not be adopted implicitly as a side effect of
  picking C as a target language. The fixed-capacity, copying model
  recommended here keeps the C backend's decoded values as ordinary,
  independently-owned data (safe to store, pass around, and outlive the
  input buffer) matching the C++ backend's existing behavior, and is the
  smaller, lower-risk starting point.

### Recommendation

Fixed-capacity inline storage, sized from schema bounds, embedded directly in
generated structs. No `malloc`/`free` anywhere in generated code or in the C
runtime for the initial backend. This is a strictly stronger embedded-first
property than the C++ backend has today (which uses `std::string`/
`std::vector`, i.e. heap allocation, for every materialized variable-length
field) — appropriate, since C is specifically the backend embedded/RTOS/
bare-metal consumers are expected to reach for.

The one accepted cost: a record with a generously-bounded field (e.g.
`max_bytes: 4096` for a field that usually holds a few bytes) always reserves
the bound's full worst-case storage inline, whether or not a given instance
uses it. This is the standard fixed-capacity trade-off (also made by
`nanopb`, in the same embedded-C serialization space) and is judged
acceptable for the initial backend; a future caller-provided-arena mode
remains available as a non-default option if a concrete need for tighter
packing emerges.

---

## 3. Runtime Architecture

`docs/principles.md`'s "Compile-Time Knowledge" principle already states the
intended split in language-neutral terms: "generated codecs should keep
schema-specific decisions in generated code and delegate only
representation-neutral byte mechanics and structural record parsing to
runtime libraries." The C backend keeps exactly this split; only the
realization changes.

**Shared C runtime (new, header-only, e.g.
`include/quarry/runtime_c/binary_record.h`), owning:**

* BRF header emission/parsing (byte-for-byte identical wire behavior to the
  C++ runtime — this is the same format, see Section 8);
* Field Directory emission/parsing;
* LEB128 varuint emission/parsing;
* big-endian scalar emission/parsing;
* the shared, fixed-capacity codec-path type (`quarry_codec_path_t`, Section
  4) and the two error enums (`quarry_decode_error_t`/
  `quarry_encode_error_t`) — these are schema-neutral, exactly like
  `PathElement`/`DecodeError`/`EncodeError` are today;
* a generated-code API compatibility constant, mirroring
  `kGeneratedCodeApiVersion` (Section 6).

**Generated code, owning (per record/enum, unchanged responsibilities from
the C++ backend, just realized as C rather than C++):**

* the struct/enum type definitions themselves;
* per-field runtime codec dispatch (which runtime append/read function a
  given field uses);
* the per-record encode/decode result structs that embed the shared error
  enum/path/offset by value (Section 4);
* schema-declared bounds enforcement (`max_bytes`/`max_elements`) in
  generated setters and re-validated again in generated decoders, matching
  the C++ backend's existing "generated codecs recheck bounds so external
  bytes cannot bypass builder validation" behavior.

This keeps the runtime just as small, in proportion, as the C++ runtime is
today — it does not grow to contain anything schema-aware, and generated
code does not grow to reimplement byte-level mechanics.

---

## 4. Error Model

C has no templates, so `CodecResult<T, E>` cannot be ported as a single
generic type the way it exists in C++. The recommended translation preserves
every piece of information `CodecResult` carries today, split across a
shared generic part and a generated per-record part — mirroring the
generated/runtime split in Section 3.

**Shared (runtime-owned, not generated):**

```c
typedef enum {
    QUARRY_DECODE_ERROR_NONE = 0,
    QUARRY_DECODE_ERROR_TRUNCATED_HEADER,
    /* ... one variant per quarry::runtime::DecodeError value ... */
} quarry_decode_error_t;

typedef enum {
    QUARRY_ENCODE_ERROR_NONE = 0,
    /* ... one variant per quarry::runtime::EncodeError value ... */
} quarry_encode_error_t;

typedef struct {
    uint8_t field_index;
    bool has_array_index;
    uint32_t array_index;
} quarry_path_element_t;

#define QUARRY_MAX_CODEC_PATH_DEPTH 8

typedef struct {
    quarry_path_element_t elements[QUARRY_MAX_CODEC_PATH_DEPTH];
    uint8_t count;
} quarry_codec_path_t;
```

`QUARRY_MAX_CODEC_PATH_DEPTH` is a fixed, generous bound on record-nesting
depth (the backend already rejects reference cycles, so nesting depth is
always finite; a small fixed constant is simpler than computing an exact
per-schema maximum and matches the embedded-first preference for static
bounds over dynamic ones). `path[0]` is still the innermost failure site and
`path[count - 1]` the outermost, matching the existing accumulation order
documented in `runtime/README.md`.

**Generated, per record (not generic — C cannot express
`DecodeResult<Sample>` as an instantiation, so the compiler generates the
concrete struct directly, the same way it already generates a concrete
`Sample` struct instead of relying on a template):**

```c
typedef struct {
    bool ok;
    quarry_telemetry_Sample_t value; /* only meaningful when ok is true */
    quarry_decode_error_t error;
    quarry_codec_path_t path;
    bool has_byte_offset;
    uint64_t byte_offset;
} quarry_telemetry_Sample_decode_result_t;

quarry_telemetry_Sample_decode_result_t
quarry_telemetry_Sample_decode_result(const uint8_t* input, size_t input_len);
```

This is full feature parity with `CodecResult`: error enum, path, and
byte-offset all remain available, with the same semantics already
documented and closed for C++ — including that `path` is empty for
structural failures detected before any field is examined, and that
`byte_offset` remains decode-only for the same reason `EncodeResult`'s
`byte_offset` is permanently absent in C++ (every `EncodeError` is a
schema-value violation against a value the caller already holds; see
`runtime/README.md`'s "Byte-Offset Context").

**Diagnostic strings.** PR-101's closed decision — no message strings in
the error type, ever, applications format their own presentation text from
the structured fields — was justified on grounds that apply identically
regardless of target language (no demonstrated demand, avoids an implicit
localization commitment, avoids a new allocating failure mode on the
failure-reporting path itself). It carries over unchanged to C, and applies
with even more force in C: adding string formatting to a "no heap allocation"
C runtime would either force an allocation the rest of this design
specifically avoids, or force a fixed-size message buffer no one asked for.

---

## 5. Generated Code Organization

**Naming.** `docs/architecture/language-generators.md` already specifies
this: a schema namespace FQN becomes a C symbol prefix
(`quarry.telemetry` -> `quarry_telemetry_`). This proposal follows that
exactly: `quarry_<namespace_with_underscores>_<RecordName>_t` for struct
types, `quarry_<namespace_with_underscores>_<RecordName>_<verb>` for
functions. C enums have no scoping at all (unlike C++'s `enum class`), so
enum value names need the full prefix too:
`QUARRY_<NAMESPACE>_<ENUMNAME>_<VALUE>`.

**File layout: `.h`/`.c` pairs, not header-only.** This is the one
deliberate divergence from the C++ backend, which emits a single header-only
file per namespace (all functions defined inline in the header). Two reasons
favor a split for C specifically:

* idiomatic C separates declaration from definition, and embedded C
  toolchains (older compilers, some vendor SDKs) have historically had
  weaker or non-conforming `static inline` support than a modern C++
  compiler's inline/COMDAT handling, which the header-only C++ approach
  currently leans on;
* a `.c` file the build already compiles once, rather than an all-inline
  header re-parsed and re-instantiated for every translation unit that
  includes it, scales better across the larger number of translation units
  typical in embedded firmware projects.

Layout mirrors the C++ generator's per-namespace-file convention:
`generated/<namespace-path>.generated.h` (types, enums, function
declarations) and `generated/<namespace-path>.generated.c` (function
definitions), for every namespace that directly owns records or enums —
the same "emit files only for namespaces that directly own records or
enums" rule the C++ backend already follows. The shared runtime itself stays
header-only (like the C++ runtime): it is small, does not want per-consumer
duplicate-symbol linking concerns, and every generated `.c` file already
`#include`s it, exactly as generated C++ headers include
`<quarry/runtime/binary_record.hpp>` today.

**Installation layout.** A sibling directory to the existing canonical C++
path (`include/quarry/runtime/`, established by PR-104): propose
`include/quarry/runtime_c/binary_record.h` (and a build-generated
`include/quarry/runtime_c/version.h` mirroring `quarry/runtime/version.h`'s
role) — never colliding with the C++ headers, both discoverable under the
same `quarry/` install root.

---

## 6. Build Integration

The current installed-package model (imported CMake targets, a generation
helper, a generated-code API compatibility epoch — see
`docs/distribution-model.md`) extends to C directly; it does not need a
redesign, only a second instance of the same three pieces:

* **Runtime target.** `Quarry::runtime_c`, header-only `INTERFACE`, exported
  the same way `Quarry::runtime` is today.
* **Compiler entry point.** Rather than a second installed executable, add a
  `--language {cpp,c}` flag to the existing `quarry-schema-compiler` binary
  (default `cpp`, preserving today's behavior with no flag), dispatching to
  whichever backend implementation is selected. `Backend::plan()`/
  `generate()`'s already-defined `GeneratedLanguage` enum in
  `compiler/backend/backend.hpp` (currently only ever set to `Cpp`) is the
  natural hook this flag would exercise going forward. One binary
  understanding the whole schema and dispatching to a target-language
  backend matches how the tool is already documented ("compiles exactly one
  `.brd` file... through `YamlCompiler` and the backend") and avoids
  doubling the installed-executable and packaging surface for what is
  conceptually one tool.
* **Generation helper.** A new `quarry_generate_c()` CMake function,
  parallel to `quarry_generate_cpp()`, differing only in which
  `--language` value it passes and which file extensions it expects back;
  the underlying custom-command/output-verification machinery
  (`QuarryGenerate.cmake`) is already language-agnostic in its current form
  (it treats `OUT_FILES` as an opaque list of paths the compiler reports),
  so this is additive, not a rewrite.
* **Generated-code API epoch.** A second scalar,
  `QUARRY_GENERATED_CODE_API_VERSION_C`, independent of the existing
  `QUARRY_GENERATED_CODE_API_VERSION` (C++). Keeping them separate lets the
  C and C++ generator/runtime contracts change on independent schedules —
  conflating them would force an artificial bump to one language's
  generated code every time only the other language's contract changed.
  Exposed the same way: a compile-time constant in the generated-code
  header, a compile-time assertion in generated code, and package metadata
  (`Quarry_GENERATED_CODE_API_VERSION_C`) alongside the existing one.
* **Examples and consumer tests.** `docs/distribution-model.md` already
  states the intended sequencing: "language-specific examples should be
  introduced with their corresponding runtime or generator support." A C
  example (`examples/c/basic_encode_decode`, mirroring the C++ one) and a
  packaging consumer test (mirroring `tests/consumer/runtime_package_test.cpp`
  and `tests/consumer/schema_compiler_package_test.cpp`) should land with
  the first working C backend milestone, not before it exists and not as an
  afterthought once it's "done."

---

## 7. Backend Architecture

**Does the existing implementation cleanly support another language?** No,
not by parameterizing it. `compiler/backend/backend.cpp`'s five-phase
pipeline (Type Catalog -> Namespace/Declaration Analysis -> File Planning ->
Rendering -> Output Assembly, documented in `compiler/backend/README.md`)
is only phase-1 language-neutral. Every phase from Namespace/Declaration
Analysis onward is C++-specific by construction, not merely by current
default:

* `FieldPlan`/`RuntimeFieldEncoding`/`TypeCatalog`'s `NamedTypeInfo` all
  carry a member literally named `cpp_type`;
* `lower_field_type` maps Schema IR primitive types directly to C++ type
  spellings (`"std::string"`, `"std::vector<std::byte>"`,
  `"std::vector<" + element + ">"`for arrays);
* the entire Rendering phase (roughly half the file) emits C++ text
  directly — `class`/`struct`/`namespace` syntax, `std::optional<T>`
  presence tracking, builder methods — with no intermediate language-neutral
  representation between "resolved field plan" and "C++ source text."

`backend.hpp`'s `GeneratedLanguage` enum (currently only ever
`GeneratedLanguage::Cpp`) and the defensive `if (file.file.language !=
GeneratedLanguage::Cpp)` check in `validate_generation_plan` are best read as
forward-looking scaffolding, not an active dispatch mechanism — there is no
branch anywhere that produces a different `GeneratedLanguage` value or
reacts differently to one.

**Recommendation.** Do not add a language parameter to the existing
`quarry::compiler::backend::Backend` class or thread a language switch
through `backend.cpp`'s internals. Instead, add a new, independent sibling
library — `compiler/backend_c/`, its own `add_library(quarry_compiler_backend_c
...)` CMake target, following the exact pattern every other `compiler/`
stage already uses (one directory, one target, declared "Allowed
dependencies"). It depends only on `compiler/schema_ir` and
`compiler/support`, exactly like `compiler/backend` does today, and has zero
dependency on `compiler/backend` itself. This matches
`docs/backend-api.md`'s existing design principle ("backends are independent
of compiler implementation... multiple backends may consume the same Schema
IR") to the letter, and keeps the C++ backend's implementation untouched —
this proposal requires no change to `compiler/backend/backend.cpp`.

The five-phase *methodology* (type catalog, then per-language lowering, then
file planning, then rendering, then output assembly) is a reusable
*pattern*, not reusable *code*: `compiler/backend_c/` would re-implement its
own phase 2 (Schema IR type -> C type/storage-shape lowering) and phase 4
(C text rendering) from scratch, sharing only the Schema IR input and the
public shape of `CodegenOptions`/`GeneratedFile`/`CodegenResult` (which are
already language-neutral in `backend.hpp`, aside from `CodegenOptions`'s
C++-flavored default `file_extension`, which a C backend would simply
default differently, or set explicitly through `CodegenOptions`).

---

## 8. Wire Compatibility

No BRF (Binary Record Format) change is needed or recommended for a C
backend. The format is already fully language-neutral by design: a 16-byte
fixed header, a Field Directory of fixed-width-plus-LEB128-varuint entries,
and big-endian fixed-width scalar/opaque-byte payloads — nothing in the
format's definition (`docs/specifications/binary-record-format.md`) refers
to C++ types, layout, or ABI. A conforming C decoder reading bytes produced
by the C++ encoder (and vice versa) is not a new capability this backend
needs to add; it is a direct consequence of BRF already being
`docs/principles.md`'s "canonical runtime representation," independent of
whichever language produced or consumes it.

No change was found that would even be "beneficial but deferred." If one is
found during actual implementation, it should not be made before 1.0 unless
it is absolutely necessary: this project has zero git tags today (see
PR-104's precedent reasoning), so wire changes are still low-cost to make
correctly now, but "low-cost" is not the same as "needed" — introducing a
wire-format change motivated only by a hypothetical C-implementation
convenience, without a demonstrated concrete blocker, would be exactly the
kind of speculative change `docs/principles.md`'s "predictable failure
behavior" and this project's closed-decision discipline (PR-101, PR-104)
argue against.

---

## 9. Scope and Implementation Roadmap

### Smallest useful C backend

A "skeleton" milestone: a `compiler/backend_c` target that consumes Schema
IR and emits a `.h`/`.c` pair per namespace containing only empty struct
shells and `_init()` functions for records with no supported field types
yet (mirroring how `single_record.txt`'s C++ fixture handles a zero-field
record today) — no encode/decode yet. This proves the CMake target,
`--language c` dispatch, and file-planning/naming plumbing work end to end
before any codec logic exists, the same low-risk-first ordering this
project has used for every backend change to date (PR-097 through PR-100).

### Recommended order, and why

1. **Skeleton** (above).
2. **Scalars** (`bool`, fixed-width integers, `f32`/`f64`) — the simplest
   possible encode/decode pair (fixed-width, no bounds, no allocation
   question), proves the runtime-codec-dispatch mechanism end to end.
3. **Enums** — structurally identical to a bounded scalar (fixed underlying
   width, range-checked on decode), and the C representation question (a
   plain C `enum` with fully prefixed value names, since C enums don't
   scope) is independent of anything in steps 4+.
4. **Strings** — first fixed-capacity-with-explicit-length field kind and
   first UTF-8 validation path; proves the "bound comes from schema, storage
   is inline" pattern this whole design leans on.
5. **Bytes** — a strict subset of strings' concerns (no UTF-8 validation);
   sequenced right after strings because it reuses the same
   fixed-capacity-plus-length shape with one less concern to prove.
6. **Arrays** (of the scalar/enum/string/bytes kinds above) — proves the
   `[max_elements]` fixed-capacity-array-plus-count shape once each element
   kind it can contain already exists.
7. **Nested records** — proves by-value embedding and the recursive
   worst-case-size property (Section 2), and first exercises the codec
   path/offset propagation across a nesting boundary (Section 4).
8. **Arrays of records** — composes steps 6 and 7; sequenced last among the
   data-shape milestones because it has no independent concern of its own,
   only the composition of two already-proven ones.
9. **Diagnostics** — `.error`/`.path`/`.byte_offset` on realistic
   malformed/truncated input, exercised across at least one nested case from
   step 7/8, mirroring `examples/cpp/schema_compiler_cmake`'s PR-105
   demonstration; sequenced after every data shape exists so failure paths
   across nesting can actually be exercised.
10. **Package tests** — a consumer test mirroring
    `tests/consumer/schema_compiler_package_test.cpp`/
    `runtime_package_test.cpp`, verifying the installed C runtime and
    `--language c` output actually configure/build/run from a fresh
    install, per `docs/distribution-model.md`'s own sequencing guidance.
11. **Example** — `examples/c/basic_encode_decode` (and, once diagnostics
    exist, an error-handling demonstration mirroring PR-105), landing
    alongside working generator/runtime support rather than ahead of it.

This order matches, feature-for-feature, the sequence the C++ backend
itself was actually built in (per `jira/backlog.md`'s PR history): prove the
narrowest possible slice (skeleton, scalars) before compounding
complexity (arrays of records), and land diagnostics and packaging/examples
only once there is a real, multi-feature generator to diagnose and package.

---

## Non-Goals

This proposal does not cover:

* an exact generated-`.h`/`.c` text specification (byte-for-byte output,
  comment conventions, exact macro names) — that is future specification
  work once an initial implementation exists to specify, the same
  relationship `docs/specifications/binary-record-format.md` has to the
  runtime that already implements it;
* any language beyond C (Rust, Go, Java, C#, Python remain in
  `docs/architecture/language-generators.md`'s outline only);
* zero-copy/view-based decode APIs (Section 2) — noted as a legitimate
  future direction, not adopted here;
* changes to Schema IR, the semantic validator, or any upstream compiler
  pass — none are needed, and `docs/backend-api.md` treats "does not
  require compiler pass changes" as a hard constraint on any new backend;
* changes to the existing C++ backend, runtime, or generated-code API epoch
  — this proposal adds a sibling, it does not modify the reference
  implementation.

## Future Work

* the exact generated-output specification, once an initial `backend_c`
  implementation exists;
* a caller-provided-arena or zero-copy decode mode, if a concrete need
  emerges (Section 2);
* revisiting whether C and C++ should ever share a single generated-code API
  epoch scalar, if in practice they always change together (Section 6) —
  starting separate and merging later is reversible; starting merged and
  splitting later is not.
