# Backend (C)

**Status: architectural skeleton only (PR-107). This is not a serialization
backend yet.** See `docs/design/c-backend.md` for the full proposed design
this skeleton is the first increment of, and `jira/backlog.md`'s PR-107 entry
for what was deliberately deferred and why.

Owns the C language generator: Schema IR -> generated `.h`/`.c`. Exists to
prove the compiler architecture cleanly supports an independent language
backend, not to implement Quarry's C serialization.

Current C generation behavior:

* exposes `CodegenOptions`, `GeneratedFile`, `PlannedGeneratedFile`,
  `GenerationPlan`, `PlanResult`, and `CodegenResult` -- the same public
  shape convention `compiler/backend/backend.hpp` uses, independently
  defined in `quarry::compiler::backend_c` rather than shared with it
* accepts Schema IR plus backend options only
* emits one `.h`/`.c` pair for every namespace that directly owns records or
  enums, the same "emit files only for namespaces that directly own records
  or enums" rule the C++ backend follows -- independently re-derived, not
  shared code
* `PlannedGeneratedFile` carries both `relative_header_path` and
  `relative_source_path` together, so the planner always knows both paths
  up front; nothing later infers one from the other
* `Backend::plan()` and `Backend::generate()` both call the same internal
  `build_generation_plan()` function, so `--list-outputs` and actual
  generation cannot diverge -- the same discipline
  `compiler/backend/backend.cpp` documents for C++, independently
  implemented here
* derives a C symbol prefix from the namespace FQN by replacing `.` with `_`
  and appending a trailing `_` (e.g. `quarry.telemetry` ->
  `quarry_telemetry_`), matching
  `docs/architecture/language-generators.md`'s existing C namespace-mapping
  specification
* derives file paths from the namespace FQN the same way the C++ backend
  does (root namespace -> configured root file stem; child namespaces ->
  slash-joined namespace path), with `.generated.h`/`.generated.c`
  extensions in place of `.generated.hpp`
* generates one C `enum { ... }` block per `EnumIR`, with every enumerator
  name prefixed by the owning namespace's symbol prefix plus the enum's own
  name (C enumerators are not scoped, unlike C++'s `enum class`, so the full
  prefix is required to avoid collisions)
* rejects (fails generation with a diagnostic identifying the enum and
  value) any enum value outside the 32-bit signed integer range, since a
  plain C `enum`'s underlying type is implementation-defined and this
  skeleton does not yet commit to a wider, guaranteed-width representation;
  see `docs/design/c-backend.md`'s note on this as a currently-accepted,
  revisitable simplification
* generates one C `struct` per `RecordIR` that declares **zero** fields,
  plus a `<prefix><Record>_init()` function that zero-initializes it
* **fails generation with a clear diagnostic identifying the record and its
  field count for any record that declares one or more fields.** No field
  type is supported yet at any stage of the pipeline; emitting a struct that
  silently dropped declared fields would be partial, misleading output, not
  a smaller feature set -- see "Unsupported features" in the PR-107 task
  description and `docs/design/c-backend.md`
* every generated struct's sole member is a `uint8_t _reserved;` placeholder,
  needed only because ISO C forbids an empty struct body; it is not a schema
  field and is expected to disappear once real field support lands
* wraps every generated header's declarations in `#ifdef __cplusplus extern
  "C" { ... }` guards, so generated C can be compiled and linked from C++
  translation units without name-mangling mismatches
* uses traditional `#ifndef`/`#define`/`#endif` include guards (not
  `#pragma once`), matching the embedded/portable-toolchain audience
  `docs/design/c-backend.md` targets
* declares **no** encode/decode API of any kind. BRF encoding/decoding is
  explicitly out of scope for this PR; nothing in this backend calls, links
  against, or requires any C runtime, and no C runtime is introduced by this
  PR -- see "Runtime" below

Deliberately deferred to later PRs (see `docs/design/c-backend.md`'s Section
9 roadmap):

* any field type (scalars, enums-as-fields, strings, bytes, arrays, nested
  records, arrays of records)
* `encode`/`decode` APIs of any kind
* a generated-code API compatibility epoch for C (there is nothing yet for
  such a guard to protect -- no runtime, no codec logic -- so adding one now
  would be exactly the kind of placeholder API this PR was told to avoid;
  one should be introduced alongside the first PR that adds a C runtime)
* CLI-configurable header/source extensions (`CodegenOptions.header_extension`/
  `.source_extension` exist and are directly testable through this library's
  own API, but `tools/schema_compiler`'s CLI does not yet expose flags to
  override them for `--language c`)
* an installed C runtime target, a `quarry_generate_c()` CMake helper, and a
  packaged C example

## Runtime

This PR introduces no C runtime. The generated skeleton needs none: it
declares no codec function, calls no runtime primitive, and has nothing to
version-guard yet. `docs/design/c-backend.md` and `docs/principles.md`'s
"Compile-Time Knowledge" principle still govern the eventual split (generic
byte mechanics and generic diagnostic containers in a shared runtime;
schema-specific structs/dispatch in generated code) -- this PR simply has
not reached the point where any runtime code is required to make the
generated output compile.

Allowed dependencies:

* `compiler/schema_ir`
* `compiler/support`

This layer must not parse source, resolve names, perform semantic analysis,
compute layout, or assign compiler-managed identifiers -- the same
constraint `compiler/backend/README.md` states for the C++ backend. It must
also not depend on `compiler/backend` (the C++ backend): the two are
independent sibling implementations that happen to consume the same Schema
IR, not one backend built on top of the other.
