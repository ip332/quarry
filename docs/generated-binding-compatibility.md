# Generated binding compatibility and migration

This is the consumer contract for C and C++ bindings after #111 G1–G4, intended
for v0.1.31-rc.1. It replaces the interim “do not mix generated bindings” rule
in #112. The [validation guide](generated-code-identity-validation.md) defines
what was actually measured; the [identity design](design/configuration-identity.md#161-computation)
defines format 1. Neither a package version nor a wire identity is a native ABI
certificate.

## Three independent compatibility mechanisms

| Mechanism | Identifies / checks | Does not establish |
| --- | --- | --- |
| Wire configuration identity, `schema_id` | The complete configuration's canonical wire-relevant input; generated dependency checks require the same algorithm and value | Equality of source declarations, native layouts, validation bodies or generator options; different IDs are not a wire-incompatibility proof |
| Generated-code identity, `gid` | One namespace and backend variant: normalized generated definitions, configuration identity and direct dependency identities, chained transitively with format 1 | Interchangeability of native objects, target/compiler ABI compatibility, or integrity of hand-edited files and precompiled binaries |
| Generated-code API epoch | The narrow generated-code/runtime API contract checked by generated headers | Wire compatibility, equal generated definitions, or binary compatibility of independently compiled native types |

`schema_id` remains the existing SHA-256/128 configuration identity. Field or
enum-member renames, unused enum changes and native presence-storage options can
leave it equal while generated definitions change. A different ID can reflect a
change elsewhere in the configuration even if a particular record's wire layout
is unchanged. Assess wire compatibility using the relevant records, record IDs
and evolution rules; identity equality/inequality is not that analysis.

`schema_id` is stored in QBS. It is not automatically carried by a BRF record;
an application serving multiple configurations must select the intended decoder
from its own protocol/registry context. A record ID alone is not a global
configuration selector. `gid` is generated metadata: it is transmitted in
neither BRF nor QBS, and is not a new discovery or authentication protocol.

The identity is computed during generation, not from compiled machine code.
Equivalent normalized definitions in the same variant/configuration, with the
same dependency identities, get the same `gid` regardless of output naming or
compiler version string. Different generated definitions re-identify that
namespace and its dependents. A new Quarry release does not automatically mean
a new `gid`, and the same `.brd` files do not guarantee one if the renderer changed.

Current runtime epochs are **3 for C++ owning** and **7 for C and C++/C storage**;
Python and Go remain at 1. G1–G4 changed none of them. Epochs and identity format
1 are independent of the [release/development version](versioning.md).

## Coexistence and automatic checks

| Situation | Contract |
| --- | --- |
| Identical generated definitions in multiple TUs | Supported with consistent compilation/ABI settings; identical C++ inline entities have one definition per identity |
| Identical copies included through different output paths in one TU | Identity-based guards deduplicate them |
| Different `gid`s in separate C++ TUs | Generated types and definitions are isolated by `quarry_gen_<gid>`; they can coexist, but are distinct types |
| Two incompatible definitions of one namespace/backend variant in one TU | Explicit compile-time conflict; changing include order is not a workaround |
| Generated dependency from another configuration | Configuration-identity check rejects it, even if some records happen to be wire-compatible |
| Generated dependency with another definition/policy or identity format | Generated-code identity presence, format and equality checks reject it after configuration checks |
| Dependency predating identity metadata | Actionable “predates generated configuration identity” or “predates generated-code identity” diagnostic; regenerate it |
| Independent sets with no include edge | No whole-program identity comparison; C++ linkage isolation still applies to generated entities, while the application owns API/decoder selection |

Checks run when the generated headers are compiled. They do not inspect archives,
previously built objects or all TUs in the final executable. Keep generated files
immutable: do not edit layouts, identity macros, guards or inline namespace
tokens. Regenerate instead. A wholly stale but internally consistent generated
set is not detected by identity checks; generation dependencies remain the build
system's responsibility.

C++ owning types retain public spellings such as `truck::engine::EngineStatus`.
The actual entity is inside `truck::engine::quarry_gen_<gid>`. C-storage wrappers
are inside `quarry::cxx::quarry_gen_<gid>`, and their native C-shaped types also
have identity-specific C++ linkage. Plain C headers compiled as C++20 wrap their
declarations in a global `inline namespace quarry_gen_<gid>` and retain their
convenient unqualified aliases. Templates over these generated C types therefore
use identity-specific C++ types too.

Plain C functions/descriptors keep internal linkage (`static inline` / `static
const`). C-storage descriptors and helpers referenced by external inline
wrappers are C++ `inline` entities. Its enum callbacks are the exception:
`extern "C" inline` functions with identity-bearing external names match the
runtime's C-language-linkage function pointers. Plain C enum callbacks remain
`static inline` with C language linkage in the C++ view. Do not change these
linkages or manually redeclare the callbacks.

Quarry does not rename application-defined inline functions or C ABI exports.
For example, an application `inline size_t record_size()` whose body uses a
generated type can still violate the ODR if that body differs between TUs.
Give such APIs application-specific namespaces/names or one consistent out-of-line
implementation. The generated namespace is not isolation for arbitrary user code.

## Consumer environments and native boundaries

| Consumer | Supported use | Responsibility / verification boundary |
| --- | --- | --- |
| Bare-metal / RTOS firmware, plain C | C99 generated headers, caller-owned storage/buffers and shared `Quarry::runtime_c`; distinct sets in separate TUs | Match the target ABI and runtime epoch, size stack/buffers for the schema and test on the target. Cortex-M4 cross-build/link/byte comparisons were measured; no RTOS, board, interrupt or hardware-execution certification is implied |
| Linux / other POSIX C applications | Same C contract; generated functions/objects do not merge across TUs | Linux and macOS toolchains were exercised. Other POSIX systems remain unverified. Same-TU C names can collide (#119) |
| C++ owning applications | C++20 generated APIs and `Quarry::runtime`; different generated sets can live in separate TUs | Keep native objects within their matching generated API and ABI. Owning containers retain their existing allocation behavior |
| C++/C-storage applications | C++20 fixed-capacity wrappers plus `Quarry::runtime_c`, with per-namespace policies | Include the generated C++ header; the C-shaped part of a C-storage header is C++ code, not a plain-C interface |
| C++ consumers of plain C headers | C++20 view of the C header, including identity-isolated template instantiations | Include the header instead of global forward declarations; preserve its established internal/C linkage |
| Mixed C/C++ applications | Identical effective definitions/policies on both sides of a C ABI object boundary; G4 tested C-produced objects consumed and encoded/decoded by C++ | C linkage does not encode the generated identity into user export names. The consumer must match headers, ABI, layout and runtime; different generated types are not an interchange format |
| Linux/cloud services with multiple configurations | Separate TUs/libraries can use their own generated sets and route BRF bytes to the matching decoder | Keep each TU's include roots unambiguous; isolate application APIs. Same-TU overlapping namespaces/definitions remain rejected, and #119 still applies |
| Static / prebuilt libraries | Matching generated headers, definitions, policies, runtime and compatible native toolchain ABI | Header checks do not authenticate or inspect a `.a`, `.so` or object file. Rebuild affected producers/consumers together; static linking does not create a stronger contract |

At native object boundaries, use the same effective generated definitions and
compatible compiler/target/standard-library ABI, packing/alignment settings and
runtime contract. Even equal `gid`s do not verify these compilation settings.
Never cast a native object into another generated variant because its name,
`schema_id` or `sizeof` happens to match. Native structs are not wire images.
Use BRF bytes with the correct configuration context, or a deliberately designed
opaque/application API, at boundaries between independently generated variants.

Different Quarry compiler releases producing identical normalized definitions
can retain the same `gid`; different definitions are isolated or rejected on a
dependency edge. Neither case promises compatibility with previously compiled
objects. Preserve the schema root/import closure, policy manifest, generator
version/commit, runtime epoch and toolchain/ABI information with prebuilt assets.

## Per-namespace storage policy

| CLI form | Effective storage |
| --- | --- |
| Option absent | Default storage in every namespace |
| Bare `--c-optimize-size` | Compact presence storage in every namespace |
| `--c-optimize-size=truck.common,truck.engine` | Compact storage only in those exact canonical FQNs; default elsewhere |

Matching is case-sensitive and nonrecursive. Selecting `truck` does not select
`truck.common`. Existing container namespaces are valid names but have no
storage of their own. The list must be nonempty: empty entries, whitespace
anywhere, malformed names, duplicates, unknown names and repeated option
occurrences are errors. Names are validated against the **complete resolved
configuration**, not only `--generate` outputs.

Unselected dependencies are rendered in memory using their effective policies
when computing identities. `--generate` controls emission only. Every invocation
must declare consistent complete effective policies for namespaces whose definitions
affect the selected outputs; the generator does not infer them from files already
on disk. A dependent generated under a different expectation rejects the actual
dependency at compile time (“contains different generated definitions”).

The unnamed root has no nonempty FQN list spelling. It can be compact with the
bare flag, which also compacts every other namespace. The YAML frontend cannot
declare an unnamed namespace; this restriction is exercised through the backend
API. `--c-optimize-size=` is not a spelling for that root.

### Working separate-invocation examples

Run these commands from the repository root with the current built or installed
`quarry-schema-compiler` on PATH. For a source build, the executable is
`build/debug/tools/quarry-schema-compiler`; see [the build workflow](development-environment.md).
Use a fresh build-owned output tree when migrating. These examples use the real
[truck configuration](../examples/truck/schemas/configuration.brd).

First record the tool/runtime query and wire configuration identity:

```sh
quarry-schema-compiler --version
quarry-schema-compiler --print-generated-code-api-version
quarry-schema-compiler --print-schema-id examples/truck/schemas/configuration.brd
```

The API query above reports the C++ owning epoch, not a package version or a
C-epoch query. C/C-storage headers check `QUARRY_C_GENERATED_CODE_API_VERSION`
from the matching installed runtime header.

Plain C: compact `truck.common`, default `truck.engine`, from the same root:

```sh
quarry-schema-compiler --language c --c-optimize-size=truck.common \
  --generate truck.common --output-directory build/binding-migration-demo/c \
  examples/truck/schemas/configuration.brd
quarry-schema-compiler --language c --c-optimize-size=truck.common \
  --generate truck.engine --output-directory build/binding-migration-demo/c \
  examples/truck/schemas/configuration.brd
```

C++/C storage uses the same policy, here emitting the dependent first:

```sh
quarry-schema-compiler --language cpp --cpp-storage c --c-optimize-size=truck.common \
  --generate truck.engine --output-directory build/binding-migration-demo/cpp-c \
  examples/truck/schemas/configuration.brd
quarry-schema-compiler --language cpp --cpp-storage c --c-optimize-size=truck.common \
  --generate truck.common --output-directory build/binding-migration-demo/cpp-c \
  examples/truck/schemas/configuration.brd
```

C++ owning uses its existing API and has no C-storage policy option:

```sh
quarry-schema-compiler --language cpp \
  --output-directory build/binding-migration-demo/cpp \
  examples/truck/schemas/configuration.brd
```

Within each variant, matching separate invocations produce the same selected
files as one invocation. Reverse invocation order is also valid. Compile C as
C99, and the C++ views as C++20. Link C/C-storage consumers to
`Quarry::runtime_c`, and owning consumers to `Quarry::runtime`. Generated `.c` /
`.cpp` compatibility files are include-only stubs; headers contain the generated
implementation. See the [C example](../examples/c/basic_encode_decode/README.md),
[C-storage example](../examples/cpp/c_storage_encode_decode/README.md) and
[owning CMake integration](../examples/cpp/schema_compiler_cmake/README.md).

The installed `quarry_generate_cpp()` helper is for C++ owning. This guide adds
no C-storage helper or `--name-prefix`, `--describe`, runtime identity reader or
other A4.3/A4.4/A4.5 feature. Do not treat proposed design examples as current CLI.

## Migrating existing bindings

Apply this procedure to **v0.1.30-rc.1, rc.2 and rc.3**, A4.2-era outputs and
other pre-#111 bindings. G4 actually built rc.3 and tested its C++ owning/plain-C
dependency rejection; it did not test rc.1/rc.2 interoperability or old C-storage
wrappers (unavailable in that release). This migration rule is conservative,
not a claim that all historical releases have identical features.

1. Retain the authoritative schemas, their complete intended root/import closure
   and old wire-format/record-ID evidence. Regenerating under a different root can
   change record domains and IDs. Compare the intended wire configuration; neither
   matching package labels nor an unchanged `schema_id` is a native ABI promise.
2. Select one current compiler and matching runtimes/toolchain ABI for each native
   boundary. Record `--version`, the existing epoch query and `--print-schema-id`
   as above. Obtain the C epoch from the runtime/header contract, not the C++ query.
3. Regenerate all referenced namespaces into a fresh generated tree from that root,
   with the full per-namespace policy repeated in every invocation. The commands
   above are a concrete example; merely renaming old macros does not migrate a file.
4. Replace obsolete includes/forward declarations/accessor names and remove stale
   generated include paths from the build. Do not name the private inline namespace
   or guard directly. Update any handwritten native adapter to the new declarations.
5. Rebuild every affected executable, static/shared library and consumer object
   together, using the matching generated headers. Discard old object/archive caches
   according to the build system. An old library may link through a user C ABI or
   type-erased boundary despite incompatible native layout; link success is not proof.
6. Validate BRF encode/decode behavior, decoder routing, runtime errors and target
   constraints. Identity checks do not replace wire evolution tests or real-device
   testing. Preserve the new generation/build provenance with distributed libraries.

| Historical surface | Current action |
| --- | --- |
| A4.2 `quarry_cfg_<schema_id>` C++ namespace | Regenerate to `quarry_gen_<gid>`; public aliases usually remain, but mangled names change. Rebuild binaries, templates and dependent libraries |
| A4.2 configuration-identity macros | Preserve the configuration checks, but regenerate; `schema_id` alone does not isolate all generated definitions |
| Pre-#117 C `<NS>_QUARRY_SCHEMA_ID_*` / path-derived C++ macro stems | Use regenerated `QUARRY_NS_<body>_SCHEMA_ID_*` (C/C storage) and `QUARRY_GENERATED_CPP_NS_<body>_SCHEMA_ID_*` (owning), with the case-preserving FQN encoding in design §4.6 |
| Generated-code macros / guards before #117/G2 | Use regenerated FQN/`gid` metadata and guards. The C / owning / C-storage guard families are `QUARRY_GENERATED_C_NS_`, `QUARRY_GENERATED_CPP_NS_`, `QUARRY_GENERATED_CPP_C_NS_`; guard names are not a consumer API |
| C accessor `<ns>_schema_id()` | Use `quarry_ns_<body>_schema_id()`, e.g. `truck_engine_schema_id()` becomes `quarry_ns_truck_engine_schema_id()`; `body` is encoded FQN, not the old C symbol prefix |
| C++ global forward declaration of a generated C struct | Remove it and include the generated header. In C++, `struct truck_engine_EngineStatus_t;` at global scope declares a different entity from the inline-namespace type |
| Handwritten owning/wrapper forward declaration | Include the generated header instead; do not guess its private identity namespace |
| Separate runs with differing bare storage flags | Replace them with consistent explicit per-namespace policies in all invocations, then regenerate and rebuild both sides |
| Prebuilt libraries or mixed configurations in one executable | Keep sets behind distinct application APIs/TUs, with unambiguous include roots. Rebuild any boundary exposing native generated types; exchange bytes/opaque handles where appropriate |

For `truck.engine`, the current encoded body is `truck_engine`; for
`dep_engine` it is `depq0engine`, distinct from `dep.engine`'s `dep_engine`.
#117 changed identity names/accessors, not the non-injective C record prefixes.
Format 1 and the C++/C runtime epochs did not change in that migration or G1–G4.

## Known limits and release recommendation

**#119: C symbol prefixes are not injective.** `dep.engine` and `dep_engine`,
`a.b` and `a_b`, or `a.b_c` and `a_b.c` can produce the same C prefix. Uppercased
enum constants can additionally collide for case-only differences. The planner
rejects collisions it sees within one configuration; it cannot inspect independent
configurations generated elsewhere.

For colliding names across configurations, pure C separate TUs keep generated
functions/objects internal and can coexist. One C TU gets redefinition errors.
In C++ separate TUs, identity-specific types/symbols provide isolation. In the
same C++ TU, Clang may accept inclusion until the ambiguous name is used; GCC can
reject the conflicting C-language-linkage declarations under strict warnings.
These measured cases are not silent merging, but they block legitimate same-TU
use. Conflicting definitions of the same namespace/variant have the explicit Quarry conflict
check independently of this limitation.

**Recommendation for v0.1.31-rc.1:** explicitly defer #119 with this restriction
and release notes if the release does not require colliding namespace sets in
one TU. It predates the identity work, has non-silent rejection in the measured
cases, and separate-TU isolation is validated. A late C-prefix change would be
a broad source-API migration requiring a separate naming decision and review.
If same-TU coexistence of those sets is a release requirement, #119 is a blocker
until that decision is approved. This is a recommendation, not a milestone
change or approval to implement it.

Future A4.5 naming isolation must encode a prefix as an injective leading segment
for C symbols as well as identity macros. Simply concatenating underscore-joined
text can still collide. `--name-prefix` is not available today and is not a
current workaround. #119 and its milestone remain unchanged by this guide.

## Evidence boundary

Use [G4's validation guide](generated-code-identity-validation.md) and its
[reviewed report / CI evidence](https://github.com/ip332/quarry/pull/122) for exact
commands, fixtures, mutations and coverage. G4 recorded AppleClang 17.0.0,
Clang 22.1.8, GCC 16.2.0, Linux GCC 13.3.0 and ARM GCC 13.2.1 cross-build evidence.
The profile matrix is not every combination of compiler, optimization and case.
GCC LTO is secondary; runtime assertions are primary. Callback checks retain
Clang's structural fallback where its internal function-linkage types are not
tracked. Older-release evidence is negative compatibility coverage, and generator
implementation drift used a real separately built renderer plus a negative control.

Cortex-M Default/MinSizeRel/Debug comparisons matched actual allocatable bytes,
relocations and defined symbols for equivalent policies, with zero mandatory
flash/RAM overhead; debug/non-allocatable metadata was reported separately.
Existing compact-layout differences are not identity overhead. No RTOS/hardware
execution or untested compiler/OS/ABI combination is certified. Epoch equality
and a clean sanitizer/LTO run do not extend these measured boundaries.
