# Configuration Identity in Generated Code (A4)

Status: **approved; A4.1 and A4.2 implemented**. This document refines §4, §5 and stage A4 of
`docs/design/record-identity-and-schema-discovery.md`, on top of A2 (hierarchical record IDs, #84)
and A3 (configuration roots and `--generate`, #86). A4.2 (#99) follows the review decisions in
§15. A4.3 and later remain unimplemented.

## 1. Summary

Every binding generated from one compilation root embeds the configuration's existing 128-bit QBS
`schema_id`. Generated files that reference each other check at build time that they come from the
same configuration: in C, C++ and Go when compiling, and in Python when importing. So bindings mixed by
accident fail to build instead of silently disagreeing on record IDs.

Deliberate multi-configuration use stays possible. In C++, an inline namespace keyed by the full
128-bit `schema_id` makes every generated symbol configuration-specific, so two configurations can't
be merged by the linker. C++ and C header guards also include the full 128-bit value. Go already
separates configurations by import path. An optional `--name-prefix` gives the other languages
source-level isolation.

The C runtime gets a read-only `schema_id` accessor for QBS images. It needs no SHA-256 and does no
allocation. `--describe` prints the configuration's identity and its version metadata.

The design uses no new wire format and preserves generated-code runtime independence. Future generated
bindings expose compiler and format versions as compile-time constants or equivalent module metadata,
without mandatory runtime storage. Review the generated-code API epoch policy before A4.2's generated
ABI changes; do not assume that an epoch bump is unnecessary.

## 2. Findings from the current implementation

| Area | Current state | Consequence for A4 |
|---|---|---|
| `schema_id` | Bytes 12–27 of the QBS header: SHA-256 truncated to 128 bits over a canonical identity input (domain tag, BRF version, each record's FQN, `record_id` and layout, and the enums records use). Algorithm byte 8 = `1`, length field = 16. | Reused unchanged. The input contains no compiler version, configuration name, path, field names or QBS mode. Minimal and reflective images of the truck configuration share `9200ca494f010801b830d0857cf66dde`. |
| Where it's computed | A4.1 computes it once from the complete resolved configuration's canonical identity input. The serializer writes the precomputed value and does not hash again. Since #92 the CLI computes the BRF layout once and shares it with identity computation, QBS construction and the backends; `--list-outputs` hashes nothing. | The CLI passes the same identity to each backend's code-generation options. Ordinary code generation does not build or serialize a QBS image. See `docs/compiler-architecture.md`, "Configuration Preparation". |
| C runtime | `quarry_qbs_parse()` validates algorithm `1` and length `16`. `quarry_qbs_view_t` keeps `bytes` but exposes no `schema_id`. | An accessor can copy 16 bytes from an already validated view. |
| Generated C | All functions are `static inline` and tables `static const`. The compatibility `.c` file has **no external symbols**. Future guard: `QUARRY_GENERATED_C_TRUCK_ENGINE_9200CA494F010801B830D0857CF66DDE_GENERATED_H_`. | Two configurations in different C translation units can't collide at link time. Within one translation unit, the full identity prevents the second header from being silently skipped by a shared guard. |
| Generated C++ | Functions such as `truck::engine::brf_v2_layout_EngineStatus()` have external linkage and hold record IDs in function-local statics. | Mixing configurations in different translation units can violate the ODR: inline definitions may be silently coalesced, while non-inline encode/decode functions cause multiple-definition errors (#82). |
| C++/C storage | `quarry::cxx::truck_engine_EngineStatus` has inline member functions embedding record IDs. | Same ODR hazard as C++. |
| Python | Absolute imports (`import truck.common.schema`); the top-level package is the first namespace segment. | Two configurations can't be imported into one process without a package prefix. |
| Go | Packages live at `<go-import-prefix>/<namespace path>`. The runtime epoch is checked at compile time with `const _ = uint(x - y)` written both ways. | Configurations are already isolated by import path, and the same idiom can check `schema_id`. |
| Includes | Dependency includes are relative to the output root (`"truck/common.generated.h"`). | With two configurations' output roots on the include path, a header can resolve a dependency from the wrong configuration. This is the main accidental-mix scenario. |
| Version | Git builds report `0.1.<commits since git_version>` (`0.1.273`). Releases are tagged `v0.1.30-rc.N`. Only packaged archive tags parse `-rc.N`. | `--describe` needs a release-tag or prerelease field; Git builds don't carry one today. |
| Design doc §5 | Previously said `schema_id` changes after an import reorder. | Corrected: sorted hierarchical IDs make import order irrelevant to IDs and `schema_id`. |

## 3. Identity model

```text
ConfigurationIdentity
  algorithm   = 1                     (SHA-256 truncated to 128 bits, QBS header byte 8)
  schema_id   = 16 bytes              (QBS header bytes 12..27)
  name        = "class8_truck"        (configuration name, optional; never hashed)
```

- **Computed once per compiler invocation** from the complete resolved configuration. A4.1 builds
  the canonical QBS identity input from Schema IR and Layout IR, then computes `ConfigurationIdentity`
  without constructing or serializing a QBS image for ordinary code generation. Explicit QBS output
  uses the same canonical identity input. The result is independent of `--generate` (A3 invariant)
  and of `--name-prefix`.
- **Passed to every backend** as `CodegenOptions::configuration_identity`, together with the
  authoritative layout it was computed from. Backends never recompute either. The option is a
  `std::optional`; every backend rejects generation without it (#113), so no generated file
  carries a default identity.
- **Only the algorithm and the 16 bytes enter generated identity checks.** The configuration name,
  root path and compiler version are never hashed. Future generated bindings expose compiler and
  format versions as compile-time constants or equivalent module metadata, without mandatory runtime
  storage; those metadata do not affect `schema_id`.
- **Compiler version never affects `schema_id`.** That's true by construction, and pinned by a golden
  test (§9).

## 4. Generated code

The examples below are for `truck.engine` in the truck configuration
(`9200ca494f010801b830d0857cf66dde`), which depends on `truck.common`. None of this generated code
references a new runtime symbol.

As implemented in A4.2 (#99), every generated file of a namespace that declares a record or an
enum carries the identity, including enum-only files. Every dependency check first tests that all
identity components of the dependency exist (§15, decision 2); the examples below show the C form.

### 4.1 C (`--language c`)

```c
#ifndef QUARRY_GENERATED_C_TRUCK_ENGINE_9200CA494F010801B830D0857CF66DDE_GENERATED_H_
#define QUARRY_GENERATED_C_TRUCK_ENGINE_9200CA494F010801B830D0857CF66DDE_GENERATED_H_
#include "truck/common.generated.h"
...
/* Configuration identity: QBS schema_id of the compilation root (algorithm 1 =
 * SHA-256 truncated to 128 bits). Compile-time constants only. */
#define TRUCK_ENGINE_QUARRY_SCHEMA_ID_ALGORITHM 1U
#define TRUCK_ENGINE_QUARRY_SCHEMA_ID_W0 0x9200ca49UL
#define TRUCK_ENGINE_QUARRY_SCHEMA_ID_W1 0x4f010801UL
#define TRUCK_ENGINE_QUARRY_SCHEMA_ID_W2 0xb830d085UL
#define TRUCK_ENGINE_QUARRY_SCHEMA_ID_W3 0x7cf66ddeUL

/* One check per generated dependency. */
#if !defined(TRUCK_COMMON_QUARRY_SCHEMA_ID_ALGORITHM) || \
    !defined(TRUCK_COMMON_QUARRY_SCHEMA_ID_W0) || ... || !defined(TRUCK_COMMON_QUARRY_SCHEMA_ID_W3)
#error "truck/common.generated.h predates generated configuration identity: it was generated by an older quarry-schema-compiler. Regenerate it with this compiler from the same compilation root as truck/engine.generated.h."
#elif TRUCK_COMMON_QUARRY_SCHEMA_ID_ALGORITHM != 1U || TRUCK_COMMON_QUARRY_SCHEMA_ID_W0 != 0x9200ca49UL || \
    TRUCK_COMMON_QUARRY_SCHEMA_ID_W1 != 0x4f010801UL || TRUCK_COMMON_QUARRY_SCHEMA_ID_W2 != 0xb830d085UL || \
    TRUCK_COMMON_QUARRY_SCHEMA_ID_W3 != 0x7cf66ddeUL
#error "truck/common.generated.h was generated from a different Quarry configuration than truck/engine.generated.h; regenerate both from the same compilation root."
#endif

/* Optional runtime access; emitted into object code only when called. */
static inline const uint8_t* truck_engine_schema_id(void) {
    static const uint8_t id[16] = {0x92, 0x00, 0xca, 0x49, 0x4f, 0x01, 0x08, 0x01,
                                   0xb8, 0x30, 0xd0, 0x85, 0x7c, 0xf6, 0x6d, 0xde};
    return id;
}
```

- **Guard:** all 128 bits of `schema_id` are in the guard, so distinct configurations' headers for
  one namespace in one translation unit both get included and then fail with redefinitions, instead
  of the second being skipped silently.
- **Macros:** they use the namespace's existing symbol prefix, so configurations with different
  `--name-prefix` values never share macro names. The root (unnamed) namespace, which has no
  symbol prefix, uses `QUARRY_ROOT_QUARRY_SCHEMA_ID_*` and `quarry_root_schema_id()`.

### 4.2 C++ (owning, `--language cpp`)

```cpp
// Identity metadata of this header, tested by the headers that include it.
#define QUARRY_GENERATED_CPP_TRUCK_ENGINE_SCHEMA_ID_ALGORITHM 1U
#define QUARRY_GENERATED_CPP_TRUCK_ENGINE_SCHEMA_ID_W0 0x9200ca49UL  // ... W1, W2, W3

#if !defined(QUARRY_GENERATED_CPP_TRUCK_COMMON_SCHEMA_ID_ALGORITHM) || ... // every component
#error "truck/common.generated.hpp predates generated configuration identity: ..."
#endif
static_assert(::truck::common::kQuarrySchemaIdAlgorithm == 1U &&
                  ::truck::common::kQuarrySchemaId == std::array<std::uint8_t, 16>{0x92U, ..., 0xdeU},
              "truck/common.generated.hpp was generated from a different Quarry configuration than "
              "truck/engine.generated.hpp; regenerate both from the same compilation root.");

namespace truck {
namespace engine {
// The inline namespace makes every generated symbol configuration-specific, so two
// configurations linked into one program can't be merged by the linker.
inline namespace quarry_cfg_9200ca494f010801b830d0857cf66dde {
inline constexpr std::uint8_t kQuarrySchemaIdAlgorithm = 1U;
inline constexpr std::array<std::uint8_t, 16> kQuarrySchemaId{0x92U, 0x00U, ..., 0xdeU};
... // records, layouts, codecs as today
} // inline namespace quarry_cfg_9200ca494f010801b830d0857cf66dde
} // namespace engine
} // namespace truck
```

- **Source code is unchanged:** users still write `truck::engine::EngineStatus`.
- **Mangled names change:** `truck::engine::quarry_cfg_9200ca494f010801b830d0857cf66dde::brf_v2_layout_EngineStatus()`,
  so cross-translation-unit ODR merging between configurations is impossible.
- **Two configurations in one translation unit** make `truck::engine::EngineStatus` ambiguous, which
  is a compile error. Such code uses `--name-prefix`.
- **Older dependencies:** C++ can't test whether a constant exists, so each header also defines
  `QUARRY_GENERATED_CPP_<PATH STEM>_SCHEMA_ID_{ALGORITHM,W0..W3}` macros, and dependents test all
  five before the `static_assert`.

### 4.3 C++/C storage (`--cpp-storage c`)

- The C part has the identity of §4.1.
- The `quarry::cxx` wrapper classes go in the same kind of inline namespace:
  `quarry::cxx::quarry_cfg_9200ca494f010801b830d0857cf66dde::truck_engine_EngineStatus`. That removes the ODR hazard of
  their inline member functions.
- **The C declarations go in a global inline namespace too** (§15, decision 6). In C++ the C
  storage types (`truck_engine_FaultCode_t`) have external linkage, and the wrappers instantiate
  templates over them, such as `std::span<const truck_engine_FaultCode_t>`. Those instantiations
  are keyed by the C type's name, not the wrapper's, so a wrapper namespace alone would let the
  linker merge two configurations' `span::size_bytes()` or `operator[]` even though the layouts
  differ. A4.2 therefore emits the whole C part of a C-storage header inside
  `inline namespace quarry_cfg_<schema_id> { ... }` at global scope. Source code still writes
  `::truck_engine_FaultCode_t`; the mangled names become
  `quarry_cfg_9200ca49...::truck_engine_FaultCode_t`. C-storage headers are C++ only, so the plain
  C backend is unchanged.

### 4.4 Python

```python
QUARRY_SCHEMA_ID_ALGORITHM = 1
QUARRY_SCHEMA_ID = bytes.fromhex("9200ca494f010801b830d0857cf66dde")

import truck.common.schema as _quarry_dep_truck_common_schema
if (getattr(_quarry_dep_truck_common_schema, "QUARRY_SCHEMA_ID_ALGORITHM", None) is None
        or getattr(_quarry_dep_truck_common_schema, "QUARRY_SCHEMA_ID", None) is None):
    raise ImportError("truck.common.schema predates generated configuration identity: ...")
if (_quarry_dep_truck_common_schema.QUARRY_SCHEMA_ID_ALGORITHM != QUARRY_SCHEMA_ID_ALGORITHM
        or _quarry_dep_truck_common_schema.QUARRY_SCHEMA_ID != QUARRY_SCHEMA_ID):
    raise ImportError(
        "truck.common.schema was generated from a different Quarry configuration than "
        "truck.engine.schema; regenerate both from the same compilation root.")
```

The check runs once per dependency at import time, before any record is used.

### 4.5 Go

```go
// Configuration identity: QBS schema_id (algorithm 1, truncated SHA-256).
const QuarrySchemaIDAlgorithm uint8 = 1
const QuarrySchemaIDHi uint64 = 0x9200ca494f010801
const QuarrySchemaIDLo uint64 = 0xb830d0857cf66dde

// QuarrySchemaID returns the configuration identity bytes.
func QuarrySchemaID() [16]byte { return [16]byte{0x92, 0x00, 0xca, 0x49, /* ... */ 0xde} }

// quarrydep0 (example.com/gen/truck/common) must be generated from the same configuration.
// A mismatch fails to compile with "constant ... overflows" on one of these lines;
// "undefined: quarrydep0.QuarrySchemaID..." means the dependency predates identity.
const _ = quarrydep0.QuarrySchemaIDAlgorithm - QuarrySchemaIDAlgorithm
const _ = QuarrySchemaIDAlgorithm - quarrydep0.QuarrySchemaIDAlgorithm
const _ = quarrydep0.QuarrySchemaIDHi - QuarrySchemaIDHi
const _ = QuarrySchemaIDHi - quarrydep0.QuarrySchemaIDHi
const _ = quarrydep0.QuarrySchemaIDLo - QuarrySchemaIDLo
const _ = QuarrySchemaIDLo - quarrydep0.QuarrySchemaIDLo
```

## 5. Deliberate multiple configurations

### 5.1 `--name-prefix <identifier>`

`--name-prefix` is optional and off by default. Bindings are generated as if every namespace were
nested under `<identifier>`, which changes names and paths only:

| Backend | Without prefix | With `--name-prefix rev_a` |
|---|---|---|
| C types, functions | `truck_engine_EngineStatus_t` | `rev_a_truck_engine_EngineStatus_t` |
| C macros | `TRUCK_ENGINE_QUARRY_SCHEMA_ID_W0` | `REV_A_TRUCK_ENGINE_QUARRY_SCHEMA_ID_W0` |
| C/C++ files, includes | `truck/engine.generated.h` | `rev_a/truck/engine.generated.h` |
| Header guards | `QUARRY_GENERATED_C_TRUCK_ENGINE_…` | `QUARRY_GENERATED_C_REV_A_TRUCK_ENGINE_…` |
| C++ namespace | `truck::engine` (+ inline cfg namespace) | `rev_a::truck::engine` (+ inline cfg namespace) |
| C++/C-storage wrappers | `quarry::cxx::truck_engine_EngineStatus` | `quarry::cxx::rev_a_truck_engine_EngineStatus` |
| Python modules | `truck.engine.schema` | `rev_a.truck.engine.schema` (with `rev_a/__init__.py`) |
| Go packages | `<prefix>/truck/engine`, package `engine` | `<prefix>/rev_a/truck/engine`, package `engine` |

- **Unaffected:** record IDs, Schema IR, QBS (FQNs stay `truck.engine.EngineStatus`) and `schema_id`.
- **Validation:** the prefix must be a valid identifier and must not equal a namespace's first
  segment.
- **Mismatched prefixes:** when dependencies are generated with different prefixes, the dependency
  include or import path doesn't exist, so the build fails visibly.

**When a prefix is needed:**

| Language | Two configurations in one program, separate translation units/files | In one translation unit or module |
|---|---|---|
| C | not needed (no external symbols) | needed |
| C++, C++/C storage | not needed (inline namespace) | needed |
| Python | needed (one module namespace per process) | needed |
| Go | not needed (distinct `--go-import-prefix`) | not needed |

Alternatives considered:
1. **Prefix derived from the configuration name.** Implicit, and renaming the configuration would
   rename every symbol. I'd allow `--name-prefix` to take any value, and leave the configuration name
   as documentation.
2. **Relative imports in Python.** These would make the package tree relocatable without an option,
   but would change the backend's deliberate absolute-import design. I don't recommend it for A4.

### 5.2 Truck cloud application with two vehicle configurations

The fleet runs revision A (`class8_truck`, `9200ca494f010801b830d0857cf66dde`) and revision B, which
adds `truck.cms.MirrorFold` (`class8_truck_rev_b`, `acf1443058daea59db96ff373869a4be`). In both,
`EngineStatus` is `0x00050001`; `MirrorFold` is `0x00030002` and exists only in B.

```sh
# Go service: separate import paths, no prefix needed.
quarry-schema-compiler --language go --go-import-prefix example.com/fleet/gen/rev_a \
  -o gen/rev_a rev_a/configuration.brd
quarry-schema-compiler --language go --go-import-prefix example.com/fleet/gen/rev_b \
  -o gen/rev_b rev_b/configuration.brd
# Python analytics: one package tree, prefixed.
quarry-schema-compiler --language python --name-prefix rev_a -o py rev_a/configuration.brd
quarry-schema-compiler --language python --name-prefix rev_b -o py rev_b/configuration.brd
```

```go
import (
	enginea "example.com/fleet/gen/rev_a/truck/engine"
	engineb "example.com/fleet/gen/rev_b/truck/engine"
)

// The sender's schema_id is known out of band in Stage A, for example from the vehicle registry.
// Stage B discovery supplies it in a handshake.
func decodeEngine(vehicleSchemaID [16]byte, data []byte) (any, error) {
	switch vehicleSchemaID {
	case enginea.QuarrySchemaID():
		var m enginea.EngineStatus
		return &m, m.UnmarshalBRF(data)
	case engineb.QuarrySchemaID():
		var m engineb.EngineStatus
		return &m, m.UnmarshalBRF(data)
	}
	return nil, fmt.Errorf("unknown vehicle configuration %x", vehicleSchemaID)
}
```

```python
from rev_a.truck.engine.schema import EngineStatus as EngineStatusA, QUARRY_SCHEMA_ID as REV_A
from rev_b.truck.engine.schema import EngineStatus as EngineStatusB, QUARRY_SCHEMA_ID as REV_B
DECODERS = {REV_A: EngineStatusA.decode, REV_B: EngineStatusB.decode}
```

- **No global restriction:** choosing a configuration is an ordinary runtime decision.
- **Scope of the checks:** they apply only between generated files that reference each other, never
  across the application's own use of two configurations.

## 6. Runtime accessors

### 6.1 C (`Quarry::runtime_c`)

```c
/* quarry/runtime_c/schema_id.h (new, header-only, no dependencies beyond <stdint.h>) */
#define QUARRY_SCHEMA_ID_ALGORITHM_SHA256_128 1U
#define QUARRY_SCHEMA_ID_SIZE 16U
typedef struct {
    uint8_t algorithm;
    uint8_t bytes[QUARRY_SCHEMA_ID_SIZE];
} quarry_schema_id_t;

static inline bool quarry_schema_id_equals(const quarry_schema_id_t* id, uint8_t algorithm,
                                           const uint8_t bytes[QUARRY_SCHEMA_ID_SIZE]);

/* generic_brf.h: copies the identity of an already parsed QBS image. No hashing, no allocation. */
quarry_generic_status_t quarry_qbs_schema_id(const quarry_qbs_view_t* qbs, quarry_schema_id_t* out);
```

- **Use:** a device compares its compiled-in identity (`truck_engine_schema_id()`, the macros) with a
  QBS image it stores or receives.
- **The runtime doesn't verify the hash.** Verifying needs SHA-256, which stays optional (Stage B).
  The compiler-side QBS parser does recompute it.
- **Generated C doesn't depend on this header,** so it doesn't need a new runtime. That keeps the
  C epoch at 7.

### 6.2 Other runtimes (additive)

- **C++:** `quarry::runtime::QbsSchema::schema_id()` returns `{algorithm, std::array<std::uint8_t, 16>}`.
- **Python:** `QbsSchema.schema_id` (`bytes`) and `QbsSchema.schema_id_algorithm`.
- **Go:** has no QBS runtime, so only the generated constants exist.

## 7. `--describe` and `--print-schema-id`

`--describe` prints human-readable, deterministic, line-oriented text. It ignores `--generate`,
writes nothing, and doesn't affect any output.

```text
$ quarry-schema-compiler --describe examples/truck/schemas/configuration.brd
configuration: class8_truck
root: examples/truck/schemas/configuration.brd
schema_id: 9200ca494f010801b830d0857cf66dde (algorithm 1: sha256-128)
compiler: quarry-schema-compiler 0.1.31-rc.1 (2507998, release tag v0.1.31-rc.1)
formats: brf 2, qbs 1, schema-ir 1
generated-code epochs: cpp 3, c 7, python 1, go 1
domains: 6
  0x0001 truck
  0x0002 truck.brakes
  0x0003 truck.cms
  0x0004 truck.common
  0x0005 truck.engine
  0x0006 truck.transmission
records: 8
  0x00010001 truck.VehicleSnapshot
  0x00020001 truck.brakes.AbsEvent
  0x00020002 truck.brakes.BrakeStatus
  0x00030001 truck.cms.CameraHealth
  0x00040001 truck.common.Timestamp
  0x00050001 truck.engine.EngineStatus
  0x00050002 truck.engine.FaultCode
  0x00060001 truck.transmission.GearState
```

- **`--print-schema-id`** prints only the 32 hex digits, for scripts and CMake, alongside the existing
  `--print-generated-code-api-version`.
- **Record roots** omit the `configuration:` line.
- **The compiler version line** needs a new version field. `QuarryVersion.cmake` would derive a
  release tag from `git describe --exact-match --match 'v*'` (or the packaged archive tag), giving
  `QUARRY_RELEASE_TAG`. Untagged builds print `untagged`.
- **Version metadata never enters the identity input.** A4.1 leaves generated files unchanged; future bindings expose informational version metadata without mandatory runtime storage.

## 8. Measured overhead

Measured with hand-edited prototypes of the truck headers (§4 code), not with an implementation:

| Configuration | Without identity | With identity, unused | Identity used |
|---|---|---|---|
| C, Cortex-M4 (`clang --target=thumbv7em-none-eabi`), `-Os`, encode `EngineStatus` | text 98 B, rodata 1068 B | **identical** (`.text` byte-identical) | +12 B text, +17 B rodata (accessor called) |
| C, Cortex-M4, `-O0` | text 208 B, rodata 1068 B | **identical** | n/a |
| C++ owning, host `-O2`, decode `EngineStatus` | `__text` 26964 B, `__const` 112 B | **identical**; 29 symbols gain `quarry_cfg_9200ca494f010801b830d0857cf66dde` | `kQuarrySchemaId` is not emitted unless odr-used |
| Go, stripped binary | 1,620,352 B | **identical** | identical (accessor inlined) |
| Python | – | one 16-byte `bytes` object per module, one comparison per dependency at import | – |

- **Mismatch detection** was verified in the prototypes: C `#error` with the intended message, and a
  Go compile error. C++ `static_assert` follows the same pattern as the existing epoch check.
- **Mangled names get longer** in C++. That affects symbol tables of unstripped binaries only, not
  code or data.
- **C object code doesn't depend on `schema_id`** unless the accessor is used, so firmware images stay
  byte-identical across configuration changes that don't touch the firmware's records.

I didn't measure with `arm-none-eabi-gcc` because Docker was unavailable. The CI Cortex-M job should
confirm the numbers with GCC as part of A4.2.

## 9. Test requirements

1. **Golden identity:** the truck configuration's `schema_id` is pinned
   (`9200ca494f010801b830d0857cf66dde`). A test fails if the identity input changes, including when
   compiler-version metadata, `--name-prefix`, `--generate`, the configuration name or QBS mode
   differ.
2. **Embedding:** every backend embeds exactly the `--emit-qbs` value, whatever the selection.
3. **Accidental mixing,** for each of C, C++, C++/C storage, Python and Go: generate `truck.engine` from
   one root and `truck.common` from another (for example rev A and rev B), and expect a build or
   import failure with the documented message. Generating both from the same root in separate
   invocations must build.
4. **Include-path mixing:** with two configurations' output roots on the include path in the wrong
   order, C and C++ must fail to compile.
5. **Multiple configurations:**
   - C++: a program links rev A and rev B in separate translation units and decodes each
     configuration's records correctly. This is the ODR regression test, and it would fail today.
   - C: the same with two translation units.
   - Go: two import paths in one binary.
   - Python: rev A and rev B imported with `--name-prefix` in one process.
6. **Prefix isolation:** prefixed and unprefixed bindings of one configuration produce identical QBS
   and IDs, and have no symbol, guard, file or module collisions.
7. **Runtime accessors:** `quarry_qbs_schema_id()` returns the header bytes for minimal and reflective
   images, rejects a NULL view or output, and allocates nothing. The C++ and Python accessors match.
8. **`--describe` and `--print-schema-id`:** golden output for the truck configuration, unchanged by
   `--generate`. `--print-schema-id` matches `--emit-qbs`.
9. **Code size** (as amended by §15, decision 4): zero mandatory runtime footprint. The CI Cortex-M
   job compares linked `.text`, `.rodata`, `.data` and `.bss` of the smoke ELF with the pull
   request's base commit, under the identical GCC image, toolchain file and flags, and reports the
   cost of explicitly calling the generated accessor separately (`quarry_cortex_m_smoke_identity`).
10. **CMake:** the `quarry_generate_cpp()` `NAME_PREFIX` and `SCHEMA_ID_VARIABLE` keywords work, and two
    helper calls on different roots can be asserted different at configure time.

## 10. Separate invocations and CMake

- **Same root, separate invocations:** A3 guarantees selection-independent output, so every invocation
  embeds the same `schema_id` and the checks pass. Tested (§9.3).
- **Different roots:** files that reference each other fail to build.
- **Not detected:** files that don't reference each other (for example cfg-A `truck.engine` and cfg-B
  `truck.brakes` in one ECU). Without a dependency edge there's nothing to compare. Applications can
  assert it explicitly: in C, compare the two namespaces' `_QUARRY_SCHEMA_ID_W*` macros; at configure
  time, use `SCHEMA_ID_VARIABLE`.
- **`quarry_generate_cpp()`** gains:
  - `NAME_PREFIX <identifier>`, passed as `--name-prefix`;
  - `SCHEMA_ID_VARIABLE <var>`, which runs `--print-schema-id` at configure time so projects can assert
    that several helper calls share, or deliberately differ in, a configuration.
- **The output-claim registry** already rejects two invocations writing the same path. Two unprefixed
  configurations therefore can't share an output directory in one CMake project.
- **#87, the release blocker and A4.5 prerequisite:** without it, editing an imported schema leaves stale bindings with the
  old `schema_id`. A4 then turns some stale mixes into compile errors, but #87 still has to be fixed,
  since a fully stale set builds cleanly with outdated IDs. `SCHEMA_ID_VARIABLE` and #87 both change
  the helper's configure step, so they should land together or in sequence.
- **Installed packages:** the runtime C package gains `schema_id.h` (additive). Python and Go runtimes
  are unchanged. Package metadata gets no new variables beyond the helper keywords.

## 11. Limitations

- **Checks need a dependency edge.** They compare only files that include or import each other (§10).
- **Python checks at import time,** not build time.
- **Go's compile error is generic** (`constant -1 overflows uint64`). The explanation is in a comment
  on the failing line.
- **The C runtime doesn't verify `schema_id`** (no SHA-256). It reports the value stored in the QBS
  image.
- **`schema_id` covers the whole configuration.** Rev A and rev B bindings are rejected as mixed even
  for `EngineStatus`, which is wire-identical in both. Relaxing this per record needs fingerprints
  (Stage B §7).
- **Every configuration change renames all C++ symbols** (inline namespace) and changes every
  generated file of the configuration. That causes rebuilds and changes symbol names in crash reports
  and debuggers. C object code is unaffected.
- **C++ forward declarations break.** User code that forward-declares generated types
  (`namespace truck::engine { class EngineStatus; }`) declares a different entity, so it must include
  the generated header instead.
- **Reserved names** in generated code: `kQuarrySchemaId*` (C++), `QUARRY_SCHEMA_ID*` (Python),
  `QuarrySchemaID*` (Go), `<NS>_QUARRY_SCHEMA_ID_*` and `<ns>_schema_id` (C).

## 12. Compatibility

- **Formats:** BRF v2, QBS v1 and the `schema_id` algorithm are unchanged.
- **A4.1:** BRF, QBS, Schema IR and generated-code epochs are unchanged. Generated code still has no
  runtime dependency.
- **Generated-code API epochs** (§15, decision 1): A4.2 keeps C++ `3`, C `7`, Python `1` and Go
  `1`. The generated code references no new runtime symbol or header and calls the runtimes exactly
  as before, so no generator/runtime contract changed. A future change that needs one requires an
  explicit epoch decision.
- **Generated C++ ABI change (A4.2), separate from the epochs:** every generated C++ symbol (owning
  bindings, `quarry::cxx` wrappers and, in C-storage headers, the C storage types and the templates
  instantiated over them) now has a configuration-specific mangled name. Consumers must regenerate
  their bindings and rebuild every binary that depends on generated types; prebuilt libraries that
  expose generated types are not link-compatible with bindings from another configuration.
- **Version metadata:** future generated bindings expose compiler and format versions as
  compile-time constants or equivalent module metadata, without mandatory runtime storage.
- **Generated output changes** (identity constants, checks, guards, the C++ inline namespace), so all
  backend goldens are updated.
- **Source compatibility:** unchanged for code that includes generated headers and uses public names.
  Two breaks: C++ forward declarations of generated types (§11), and code that names a header guard
  macro.
- **C++ ABI** of generated types becomes configuration-specific. That's arguably correct for shared
  libraries exposing generated types.

## 13. Implementation plan

| PR | Scope | Depends on |
|---|---|---|
| A4.1 | Build one `ConfigurationIdentity` from the complete resolved configuration, share it with QBS serialization and all backends, and add `--print-schema-id`. No QBS model validation or serialization for ordinary code generation and no generated-code change. | – |
| A4.2 | After #82 is fixed and the generated-code API epoch policy is reviewed: generated identity constants, dependency checks and full-128-bit `schema_id` guards in C, C++, C++/C storage, Python and Go. Add the full-128-bit C++ inline namespace. Update goldens and run the mixing/ODR and Cortex-M checks. **Implemented (#99).** | A4.1, #82 |
| A4.3 | Runtime accessors: C `schema_id.h` and `quarry_qbs_schema_id()`, the C++ reader and Python generic `schema_id`. | A4.1 (tests only) |
| A4.4 | `--describe` and the release-tag version field (`QuarryVersion.cmake`, `--version`, `--describe`). | A4.1 |
| A4.5 | After #87 is fixed: `--name-prefix` in all backends; CMake `NAME_PREFIX` and `SCHEMA_ID_VARIABLE`; the two-configuration truck cloud example (Go and Python, rev A and rev B); docs. | A4.2, #87 |

A4.3 and A4.4 are independent of A4.2. The A5 cross-language matrix follows A4.5.

## 14. Approved design constraints

1. C++ inline namespaces and C/C++ header guards use all 128 bits of `schema_id`.
2. Generated bindings remain runtime-independent; identity checks introduce no mandatory runtime
   storage.
3. Before A4.2 changes generated ABI, review the generated-code API epoch policy and decide whether
   an epoch bump is required.
4. Future generated bindings expose compiler and format versions as compile-time constants or
   equivalent module metadata, without mandatory runtime storage.
5. `schema_id` remains independent of compiler version, configuration name, source path, and QBS
   mode.
6. Application-level checks for namespaces without a dependency edge stay documented patterns
   (C macros, `SCHEMA_ID_VARIABLE`), rather than a new mandatory mechanism.
7. Complete #82 before A4.2 and #87 before A4.5.
8. The release-tag version field is delivered in A4.4, or separately before the release.

## 15. A4.2 review decisions (2026-11-17, #99)

1. **Epochs.** Preserve C++ `3`, C `7`, Python `1` and Go `1`, provided the implementation does
   not change the corresponding generator/runtime contract. Document the generated C++ ABI change
   separately (§12): consumers must regenerate bindings and rebuild dependent binaries. If an
   implementation requires a runtime contract change, stop and request an epoch decision.
2. **Older dependencies.** Detect them with explicit identity-metadata checks, not header-guard
   names, and check every required identity component (C: the algorithm and `W0`–`W3` macros; C++:
   the per-header identity macros; Python: both module constants). The Go limitation (§11; a
   generic `undefined:` error explained by a comment) is accepted.
3. **Fixtures.** Revision B fixtures live under `tests/fixtures/configuration_identity/`: one with a
   different `schema_id` and unchanged numeric record IDs (`rev_b_same_ids`), one with a different
   `schema_id`, renumbered records and changed layouts (`rev_b_renumbered`). The public
   multi-configuration example remains A4.5 work.
4. **Footprint.** The byte-identical object-code requirement is replaced by a zero mandatory runtime
   footprint: linked `.text`, `.rodata`, `.data` and `.bss` are compared under identical GCC build
   settings; no mandatory identity-related storage, startup code, allocation or hashing. Symbol
   tables, mangled names, relocations and debug metadata may differ. The cost of explicitly
   accessing identity is measured separately. Python's constant and import-time comparison are
   approved exceptions.
5. **Collisions.** Newly introduced generated identifiers are checked against schema-derived names
   and conflicts are rejected with a diagnostic: C++ `kQuarrySchemaId`, `kQuarrySchemaIdAlgorithm`
   and `quarry_cfg_*` (record, enum and namespace names); C `<NS>_QUARRY_SCHEMA_ID_*` and
   `<ns>_schema_id` (through the existing file-scope identifier registry); Go `QuarrySchemaID*`
   (package-level names). Python keeps its established policy of renaming a schema name equal to
   a reserved module name by appending `_`, and rejects any resulting duplicate.
6. **C++ with C storage.** The wrapper namespace must not conceal ODR problems in the underlying C
   types, so the C part is configuration-specific too (§4.3). Two configurations with different
   record IDs and layouts are linked into one executable from separate translation units and tested.
