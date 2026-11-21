# Configuration Identity in Generated Code (A4)

Status: **approved; A4.1–A4.4 and #111 G1–G5 implemented**. A4.5 (`--name-prefix`) remains a proposal,
not supported CLI/runtime features. A4 builds on hierarchical record IDs (#84)
and configuration roots / `--generate` (#86); §15 records its historical review
and §16 specifies the implemented generated-code identity.

## 1. Summary

Every generated binding embeds the complete configuration's existing QBS
`schema_id`. Dependencies check it at compilation (C, C++, Go) or import time
(Python). C and C++ additionally use a per-namespace, per-variant generated-code
identity (`gid`), because equal wire identities can conceal different source
names, storage policies and validation bodies. C++ linkage and C/C++ guards use
`gid`, not `schema_id`. Neither identity is a native ABI guarantee.

The [consumer compatibility and migration guide](../generated-binding-compatibility.md)
is the current consumer contract and three-mechanism compatibility matrix.
[Validation coverage](../generated-code-identity-validation.md) records measured
G4 evidence and unverified combinations. These documents supersede interim
blanket prohibitions on mixing generated bindings.

No BRF, QBS, Schema IR, record ID, identity algorithm or runtime API epoch changes
are introduced by #111. The proposals below are retained as design history and
future work, rather than instructions to use unavailable features.

## 2. Historical findings at the A4 investigation

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

These are current C/C++ contracts after #111 G1–G4. The original A4.2 linkage
used `quarry_cfg_<schema_id>`; that spelling is historical. See §16 for the
precise generated-code identity encoding and checks, and the
[consumer guide](../generated-binding-compatibility.md) for migration.

### 4.1 C (`--language c`)

Each namespace with generated declarations carries
`QUARRY_NS_<body>_SCHEMA_ID_{ALGORITHM,W0..W3}` and the optional generated
accessor `quarry_ns_<body>_schema_id()`. `<body>` is the injective encoding in
§4.6, not the C symbol prefix. #117 replaced `<NS>_QUARRY_SCHEMA_ID_*` and
`<ns>_schema_id`; regenerate dependencies using the old names.

The guard and `QUARRY_GENERATED_C_NS_<body>_CODE_ID_{FORMAT,W0..W3}` use the
full generated-code identity. Configuration checks precede generated-code
presence, format and equality checks for every generated dependency. Conflicting
definitions of the same namespace/variant in one TU produce an explicit error;
identical copies deduplicate even through different output paths. C99 functions
and descriptors retain internal linkage. When consumed as C++20, declarations
are in a global `inline namespace quarry_gen_<gid>`, so generated native types
and templates over them have identity-specific C++ names. Enum callbacks retain
C language linkage. See §16.5.

### 4.2 C++ owning (`--language cpp`)

Public source names such as `truck::engine::EngineStatus` remain convenient,
but the actual declarations are inside
`truck::engine::quarry_gen_<gid>`. Identity metadata uses
`QUARRY_GENERATED_CPP_NS_<body>_SCHEMA_ID_{ALGORITHM,W0..W3}` and
`QUARRY_GENERATED_CPP_NS_<body>_CODE_ID_{FORMAT,W0..W3}`. Constants expose
`kQuarrySchemaId{,Algorithm}` and `kQuarryGeneratedCodeId{,Format}`.

Guards are keyed by namespace and `gid`; dependencies check both identities.
Different generated definitions can coexist in separate TUs; incompatible
versions of one namespace in the same TU are rejected. No `--name-prefix`
workaround exists today. User-defined inline functions and prebuilt native
interfaces remain the consumer's responsibility, even when their signatures
hide use of generated types.

### 4.3 C++/C storage (`--cpp-storage c`)

Both the native C-shaped declarations (global scope) and wrappers
(`quarry::cxx`) are inside `quarry_gen_<gid>` inline namespaces. Public names
remain `::truck_engine_EngineStatus_t` and
`quarry::cxx::truck_engine_EngineStatus`. Isolating the native declarations also
isolates templates instantiated over them, such as `std::span`.

Descriptors and helpers are C++ inline entities, one per generated identity.
Enum callbacks are the exception: `extern "C" inline` names carry the full
`gid`, matching runtime C-language-linkage function pointers without merging
different definitions. Metadata uses `QUARRY_NS_<body>_SCHEMA_ID_*` and
`QUARRY_GENERATED_CPP_C_NS_<body>_CODE_ID_*`; guards and dependency checks use
the generated-code identity. See §16.3 for the linkage details.

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

### 4.6 Namespace-to-macro encoding (#117)

One utility, `compiler/support/namespace_macro.hpp`, spells a namespace for every identity macro,
generated-code identity header guard and C configuration-identity accessor. It is deterministic,
injective over the namespace names the frontend accepts, and case-preserving. Output paths, file
extensions, root file stems and `--generate` do not enter it. `decode_namespace_macro` is the
inverse, and the round-trip tests reject strings outside the image.

A namespace FQN is zero or more identifier segments separated by `.`. The empty FQN is the root
namespace. A segment matches `[A-Za-z_][A-Za-z0-9_]*`.

The encoded body is the single character `0` for the root. Otherwise each segment is encoded and
the pieces are joined by one `_`. Inside a segment, `q` becomes `qq`, `_` becomes `q0`, and every
other character is copied. So `dep.engine` is `dep_engine`, `dep_engine` is `depq0engine`, `Ab`
stays `Ab`, `a.b_c` is `a_bq0c` and `a_b.c` is `aq0b_c`. A segment that ends in `_` encodes that
character as `q0`, so the `_` that joins segments is never adjacent to another `_`.

The segment encoding is a prefix code: a decoder that sees `q` consumes the next character
(`q` → `q`, `0` → `_`) and otherwise copies one character. An encoded segment contains no `_`,
so splitting the body on `_` recovers the segments. Every encoded segment starts with a letter.
The root body `0` starts with a digit, so it is outside that image. The decoder then checks that
each segment is an identifier and that re-encoding the result equals the input.

The composed macro is a prefix that ends in one `_`, the body, and a suffix that starts with one
`_`. The prefixes are `QUARRY_GENERATED_CPP_NS_`, `QUARRY_GENERATED_CPP_C_NS_`, `QUARRY_NS_` and
`quarry_ns_`. None of those pieces contains `__`, the body neither starts nor ends with `_`, and
the whole identifier matches `[A-Za-z][A-Za-z0-9_]*`. It is a C and C++ macro name and not a
reserved identifier.

A future `--name-prefix` (#102) is a leading segment of this encoding, not text concatenated onto
it. `encode_namespace_macro_segments` takes the prefix identifier first. A prefix `rev_a` on
`truck.engine` encodes as `revq0a_truck_engine`. A prefix `rev_` on `a_b` encodes as
`revq0_aq0b`. Concatenating the prefix onto an already encoded body would not be injective.
The flag is not implemented.

The canonical generated-code identity format stays 1. Renderers still write the `M` placeholder;
only the substituted stem is this encoding. The `CODE_ID` prefix and the identity guard are
written literally, so their spelling is inside the hashed text and generated-code identity values
change. Python and Go output is byte-identical. C++ epoch 3 and C epoch 7 are unchanged, and
there is no new runtime storage, initialization, allocation or hashing.

Consumers must regenerate bindings and rebuild affected C and C++ consumers after this macro
naming migration. A dependency generated before #117 does not define the new macros, so the
configuration-identity check reports that it predates generated configuration identity.

## 5. Deliberate multiple configurations

### 5.1 Proposed A4.5: `--name-prefix <identifier>` (not implemented)

The following describes the proposed option, which the current CLI rejects.
In that proposal `--name-prefix` is optional and off by default. Bindings are generated as if every namespace were
nested under `<identifier>`, which changes names and paths only:

| Backend | Without prefix | With `--name-prefix rev_a` |
|---|---|---|
| C types, functions | `truck_engine_EngineStatus_t` | `rev_a_truck_engine_EngineStatus_t` |
| C macros | `QUARRY_NS_truck_engine_SCHEMA_ID_W0` | `QUARRY_NS_revq0a_truck_engine_SCHEMA_ID_W0` |
| C/C++ files, includes | `truck/engine.generated.h` | `rev_a/truck/engine.generated.h` |
| Header guards | `QUARRY_GENERATED_C_TRUCK_ENGINE_…` | `QUARRY_GENERATED_C_REV_A_TRUCK_ENGINE_…` |
| C++ namespace | `truck::engine` (+ inline cfg namespace) | `rev_a::truck::engine` (+ inline cfg namespace) |
| C++/C-storage wrappers | `quarry::cxx::truck_engine_EngineStatus` | `quarry::cxx::rev_a_truck_engine_EngineStatus` |
| Python modules | `truck.engine.schema` | `rev_a.truck.engine.schema` (with `rev_a/__init__.py`) |
| Go packages | `<prefix>/truck/engine`, package `engine` | `<prefix>/rev_a/truck/engine`, package `engine` |

- **Unaffected:** record IDs, Schema IR, QBS (FQNs stay `truck.engine.EngineStatus`) and `schema_id`.
- **Byte-identity baseline (#102).** Output without `--name-prefix` is the output after #117.
  It is not the A4.2 spelling: #117 renamed the C and C++ identity macros. Python and Go output
  without the flag stays byte-identical to their current output. The prefix, when implemented,
  uses §4.6 with the prefix as the leading segment.
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

### 5.2 Proposed A4.5 truck cloud application with two vehicle configurations

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

A4.3 (#100) exposes the existing stored QBS configuration identity. Accessors
perform no hashing or allocation (apart from Python returning its small `bytes`
value), and never expose per-namespace generated-code `gid` as wire metadata.
C/Python parsers retain their existing structural checks; the C++ parser retains
its existing canonical hash verification. An accessor adds no verification.


### 6.1 C (`Quarry::runtime_c`)

```c
/* quarry/runtime_c/schema_id.h: header-only value/comparison, <stdint.h>/<stdbool.h> */
#define QUARRY_SCHEMA_ID_ALGORITHM_SHA256_128 1U
#define QUARRY_SCHEMA_ID_SIZE 16U
typedef struct {
    uint8_t algorithm;
    uint8_t bytes[QUARRY_SCHEMA_ID_SIZE];
} quarry_schema_id_t;

static inline bool quarry_schema_id_equals(const quarry_schema_id_t* id, uint8_t algorithm,
                                           const uint8_t bytes[QUARRY_SCHEMA_ID_SIZE]);

/* generic_brf.h: copies the identity of an already parsed QBS image. */
quarry_generic_status_t quarry_qbs_schema_id(const quarry_qbs_view_t* qbs, quarry_schema_id_t* out);
```

- **Use:** a device compares its compiled-in identity (`quarry_ns_truck_engine_schema_id()`, the macros) with a
  QBS image it stores or receives.
- **The runtime doesn't verify the hash.** Verifying needs SHA-256, which stays optional (Stage B).
  The compiler-side QBS parser does recompute it.
- **Invalid arguments:** a null view/output, null backing buffer or fewer than
  40 backing bytes returns `QUARRY_GENERIC_INVALID_ARGUMENT`; unsupported identity
  algorithm/length metadata returns `QUARRY_GENERIC_MALFORMED_QBS`. Non-null output
  is cleared on failure. Call only after successful parsing, with unchanged live
  backing bytes; these bounded checks do not validate a fabricated view.
- **Packaging:** the C SDK installs `schema_id.h`; applications using the new
  external accessor must link the updated `Quarry::runtime_c`. Existing public
  C structures, parser behavior and old entry points are unchanged.
- **Generated C doesn't depend on this header,** so it doesn't need a new runtime. That keeps the
  C epoch at 7.

### 6.2 Existing C++ and Python views (additive)

- **C++:** `quarry::compiler::qbs::ValidatedQbsView::schema_id()` returns the existing
  compiler `support::ConfigurationIdentity` value (`algorithm`, `schema_id`, a
  16-byte `std::array`). It reads the already validated algorithm byte and cached
  header identity; no new parser or standalone schema class is introduced.
  `header().schema_id` and all existing view layouts remain unchanged. A default
  view without an image returns algorithm 0 and zero bytes. This compiler-owned
  parser remains internal tooling, not an installed C++ runtime SDK dependency.
- **Python:** `QbsSchema.schema_id` (`bytes`, exactly 16) and
  `QbsSchema.schema_id_algorithm` (`int`) are read-only properties over the existing
  retained image from `quarry.load_qbs()` / `QbsSchema.from_bytes()`. Hand-built
  metadata with no complete QBS header raises `QbsError` when queried. Construction
  and parser errors retain their existing behavior; no identity cache is added.
- **Go:** has no QBS runtime, so only the generated constants exist.

## 7. Implemented `--describe` and `--print-schema-id`

`--describe` prints human-readable, deterministic, line-oriented text. It ignores `--generate`,
writes nothing, and doesn't affect any output.

```text
$ quarry-schema-compiler --describe examples/truck/schemas/configuration.brd
configuration: class8_truck
root: examples/truck/schemas/configuration.brd
schema_id: 9200ca494f010801b830d0857cf66dde (algorithm 1: sha256-128)
compiler: quarry-schema-compiler 0.1.288 (2507998, release tag v0.1.31-rc.1)
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
- **The compiler version line** includes separate release metadata. `QuarryVersion.cmake` derives a
  release tag from `git describe --tags --exact-match --match 'v*'` (or the packaged archive tag), giving
  `QUARRY_RELEASE_TAG`. Untagged builds print `untagged`.
- **Version metadata never enters the identity input.** A4.1 leaves generated files unchanged; future bindings expose informational version metadata without mandatory runtime storage.

Mappings use assigned ID order as approved in #101. Numeric Git versions keep
the existing commit counter; release tags are separate informational metadata.
See [CLI behavior](../../tools/README.md#schema-description) and
[version resolution](../versioning.md#informational-release-metadata).

## 8. Measured overhead

These are historical A4 measurements, before generated-code identity. Current
measured evidence is in the [G4 validation guide](../generated-code-identity-validation.md).


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

This is the original A4 test plan, including future A4.4–A4.5 features. A4.3
now has parser, C99, installed-package and three-language identity tests; the
remaining proposed tests are not a statement that future features exist. Use the [G4 validation
guide](../generated-code-identity-validation.md) for current measured coverage.


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

Generate from the complete configuration root with consistent effective storage
policy for every namespace, including unselected dependencies. Selection and
output naming do not change either identity. A bare compact flag and an explicit
list covering all namespaces are equivalent; matching flag spelling is not the
requirement. Dependencies with incompatible configuration identity or generated
policy/definitions fail compilation. No dependency edge means no automatic
whole-program identity comparison.

The [consumer guide](../generated-binding-compatibility.md#per-namespace-storage-policy)
contains executable separate-invocation examples. The
[CLI contract](../../tools/README.md) and
[CMake integration](../schema-compiler-tool-distribution.md) define supported
options. `NAME_PREFIX`, `SCHEMA_ID_VARIABLE` and `--name-prefix` are A4.5
proposals, not current helper/CLI options. Build systems must regenerate the
complete affected closure; a wholly stale but internally consistent set can
compile successfully. Identity checks do not replace dependency tracking.

## 11. Current limitations

The [consumer guide](../generated-binding-compatibility.md#known-limits-and-release-recommendation)
is authoritative for coexistence, native ABI responsibilities, the non-injective
C symbol prefix (#119), and its release-deferral recommendation. Checks inspect
included generated headers, not prebuilt archives or all translation units.
Different `gid`s isolate generated C++ entities, not user-defined exports or
inline functions. Identical `gid`s do not certify packing, compiler ABI, target
or standard-library compatibility. Include headers rather than forward-declaring
generated types. Python imports check configuration identity; Go reports generic
constant-overflow compile errors for mismatches. Neither gains C/C++ `gid`
isolation in #111.

## 12. Compatibility and migration

BRF v2 (generated bindings), QBS v1, Schema IR v1, record IDs, `schema_id`
algorithm 1 and generated-code identity format 1 are unchanged. Runtime API
epochs remain C++ owning 3, C / C++-C storage 7, Python 1 and Go 1. Epochs
check generated/runtime APIs, not native ABI or wire compatibility.

Consumers must regenerate bindings and rebuild affected binaries together,
including static/prebuilt libraries exposing generated types. Equal `schema_id`
is insufficient to reuse old native objects. The
[migration procedure](../generated-binding-compatibility.md#migrating-existing-bindings)
covers v0.1.30-rc.1–rc.3, A4.2, #117 macro/accessor names, `quarry_cfg_*` to
`quarry_gen_*`, guards, C headers in C++, forward declarations and mixed-storage
dependencies. Different releases with identical normalized definitions can
retain `gid`; renderer changes can change it. No general cross-release native
binary compatibility is promised.

## 13. Implementation plan

| PR | Scope | Depends on |
|---|---|---|
| A4.1 | Build one `ConfigurationIdentity` from the complete resolved configuration, share it with QBS serialization and all backends, and add `--print-schema-id`. No QBS model validation or serialization for ordinary code generation and no generated-code change. | – |
| A4.2 | After #82 is fixed and the generated-code API epoch policy is reviewed: generated identity constants, dependency checks and full-128-bit `schema_id` guards in C, C++, C++/C storage, Python and Go. Add the full-128-bit C++ inline namespace. Update goldens and run the mixing/ODR and Cortex-M checks. **Implemented (#99).** | A4.1, #82 |
| A4.3 | **Implemented (#100).** Runtime accessors: C `schema_id.h` and `quarry_qbs_schema_id()`, the C++ reader and Python generic `schema_id`. | A4.1 (tests only) |
| A4.4 | `--describe` and the release-tag version field (`QuarryVersion.cmake`, `--version`, `--describe`). | A4.1 |
| A4.5 | After #87 is fixed: `--name-prefix` in all backends; CMake `NAME_PREFIX` and `SCHEMA_ID_VARIABLE`; the two-configuration truck cloud example (Go and Python, rev A and rev B); docs. | A4.2, #87 |

A4.3 and A4.4 are independent of A4.2. The A5 cross-language matrix follows A4.5.

## 14. Approved design constraints

1. Originally A4.2 used all 128 bits of `schema_id` for C++ inline namespaces
   and guards. #111 supersedes that choice with the full 128-bit `gid` (§16).
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
   `<ns>_schema_id` (through the existing file-scope identifier registry; #117 renamed these to
   `QUARRY_NS_<body>_SCHEMA_ID_*` and `quarry_ns_<body>_schema_id`, still in that registry); Go `QuarrySchemaID*`
   (package-level names). Python keeps its established policy of renaming a schema name equal to
   a reserved module name by appending `_`, and rejects any resulting duplicate.
6. **C++ with C storage.** The wrapper namespace must not conceal ODR problems in the underlying C
   types, so the C part is configuration-specific too (§4.3). Two configurations with different
   record IDs and layouts are linked into one executable from separate translation units and tested.

## 16. Generated-code identity in C and C++ bindings (#111 G1–G4)

`schema_id` identifies the wire configuration; it does not identify generated definitions. Field
or enum-member renames, unused enums, `--c-optimize-size` and generator changes all keep
`schema_id` while changing generated C++, and A4.2 isolated linkage by `schema_id` only (see the
[#111 investigation](https://github.com/ip332/quarry/issues/111)). G1 adds a third, independent
identity for the C++ owning and C++/C-storage variants, and G2 extends it to plain C (§16.5):

| Identity | Identifies | Used for |
| --- | --- | --- |
| `schema_id` (§3) | the wire configuration | QBS, configuration-mismatch checks |
| generated-code identity | one namespace's generated definitions in one variant | C++ inline namespace, header guard, generated-definition checks |
| generated-code API epoch | the generator/runtime contract | `kGeneratedCodeApiVersion` / `QUARRY_C_GENERATED_CODE_API_VERSION` checks |

BRF, QBS, Schema IR, record IDs, the `schema_id` algorithm and every epoch are unchanged.
G3 adds per-namespace storage selection (§16.4); G4 establishes the
[reproducible validation baseline](../generated-code-identity-validation.md).
Python and Go are unchanged.

### 16.1 Computation

Each backend renders a namespace's generated files in **normalized form**: every
self-referential or output-naming token is a typed placeholder written by the renderer
(`compiler/support/generated_code_identity.hpp`), never found by rewriting text afterwards.

| Placeholder | Stands for |
| --- | --- |
| `␞S<form>␟` | this namespace's own generated-code identity (`h`/`H` 32 hex digits, `0`..`3` a word literal, `b` a byte list) |
| `␞P<fqn>␟` | the generated include path of namespace `<fqn>` (include lines and paths quoted in diagnostics) |
| `␞M<fqn>␟` | the §4.6 namespace-encoded macro stem of `<fqn>` (substituted after the identity is hashed; the grammar is unchanged) |

(␞ and ␟ are the ASCII record and unit separators, which never occur in generated text.)

The identity is SHA-256, truncated to its first 16 bytes, of the canonical encoding
(format 1; big-endian; `lp(x)` = u32 length + bytes):

```text
"QUARRY-GENERATED-CODE-ID" 0x00
u8 format = 1 | u8 variant (1 C, 2 C++ owning, 3 C++/C storage) | u8 schema_id algorithm | 16 schema_id
lp namespace FQN
u32 file count, per file in role order (1 header, 2 source):        u8 role, lp normalized content
u32 dependency count, per direct generated dependency in FQN order: lp FQN, u8 variant, u8 format, 16 identity
```

Pinned vectors in `tests/support/generated_code_identity_test.cpp` were computed with an
independent implementation of this specification; changing any of them is a new format.

Identities are computed in dependency-topological order over **every** namespace of the
configuration, not only the `--generate` selection, so an unselected dependency gets exactly the
identity it has when it is generated itself. The selected headers are checked to render exactly
the normalized text their identity was computed from (A3); then every placeholder is substituted.

Consequently the identity does **not** depend on `--output-directory`, `--file-extension`,
`--root-file-stem`, `--generate`, import order, the configuration name, source paths or the
compiler build; and it **does** change with any change of generated definitions: names, unused
enums, storage options, generator implementation, epochs, `schema_id` and, through the dependency
list and the dependency checks, any change of a generated dependency. Identical output from two
compiler releases has one identity.

### 16.2 Generated C++ (owning and C storage)

```cpp
#ifndef QUARRY_GENERATED_CPP_NS_truck_engine_<GID>_      // C storage: QUARRY_GENERATED_CPP_C_NS_...
#define QUARRY_GENERATED_CPP_NS_truck_engine_<GID>_
#if defined(QUARRY_GENERATED_CPP_NS_truck_engine_CODE_ID_FORMAT) || \
    defined(QUARRY_GENERATED_CPP_NS_truck_engine_SCHEMA_ID_ALGORITHM)
#error "truck/engine.generated.hpp: this translation unit already includes a different generated definition of namespace truck.engine ..."
#endif
... schema_id macros (§4.2), same stem as the CODE_ID macros ...
#define QUARRY_GENERATED_CPP_NS_truck_engine_CODE_ID_FORMAT 1U
#define QUARRY_GENERATED_CPP_NS_truck_engine_CODE_ID_W0 0x....UL   // ... W1, W2, W3
... includes; A4.2 configuration checks; then for every generated dependency:
#if !defined(..._CODE_ID_FORMAT) || !defined(..._CODE_ID_W0..W3)   -> "predates generated-code identity"
#elif ..._CODE_ID_FORMAT != 1U                                       -> "uses a different generated-code identity format"
#elif ..._CODE_ID_W0..W3 != <expected>                               -> "contains different generated definitions than ..."
namespace truck::engine { inline namespace quarry_gen_<gid> {
inline constexpr std::uint8_t kQuarryGeneratedCodeIdFormat = 1U;
inline constexpr std::array<std::uint8_t, 16> kQuarryGeneratedCodeId{...};
... } }
```

- **Linkage isolation:** `quarry_gen_<gid>` replaces `quarry_cfg_<schema_id>` (same length, so
  mangled names do not grow). It covers `schema_id`, so cross-configuration isolation is kept, and
  also isolates differences in normalized generated definitions within one configuration. C storage uses it for the C part (global
  inline namespace) and the `quarry::cxx` wrappers.
- **Identity macros** are named from the §4.6 encoding of the namespace FQN
  (`QUARRY_GENERATED_CPP_NS_<body>_CODE_ID_*`, root `QUARRY_GENERATED_CPP_NS_0_CODE_ID_*`;
  C storage uses `QUARRY_GENERATED_CPP_C_NS_<body>_`), never from the output path, so naming
  options cannot affect them. The same body is the configuration-identity stem.
- **Coexistence and rejection:** different definitions in separate TUs without an include edge coexist in one
  program, isolated (distinct entities; not interchangeable). Across an include edge they are
  rejected at compile time. Two different definitions of one namespace in one translation unit
  are rejected explicitly by the check after the guard; identical definitions reached through
  two paths share a guard and are included once. A4.2 configuration checks run first and keep
  their messages.
- **Reserved names:** C++ rejects records, enums and namespace segments named
  `kQuarryGeneratedCodeId`, `kQuarryGeneratedCodeIdFormat` or `quarry_gen_*`; C storage reserves
  its `..._CODE_ID_*` macros and the `quarry_gen_` prefix in the file-scope identifier registry.
- **Footprint:** identity computation and checks add no mandatory runtime storage,
  initialization or hashing. ODR-using metadata constants or calling optional
  accessors can materialize bytes; see the measured G4 footprint evidence. #117 changes the plain C configuration-identity macro and
  accessor spellings (§4.1). The C runtime, the C epoch and Python and Go output stay
  byte-identical. Cortex-M call sites of the accessor use the new name.

### 16.3 Linkage audit

| Variant | Entities | Linkage after G1 |
| --- | --- | --- |
| C++ owning | classes, enums, `inline` functions (layouts are function-local statics of `inline` functions), static member functions | external, one entity per identity |
| C++/C storage, C part | struct/enum types, descriptor tables, `*_type` descriptors, codec and presence helpers, identity accessor | **was** `static` (internal) inside `extern "C"`, referenced from the external-linkage inline `quarry::cxx` wrappers, an ODR violation even with identical headers; **now** `inline` C++ entities in `quarry_gen_<gid>` without an enclosing `extern "C"` (C-storage headers are C++ only), one entity per identity |
| C++/C storage, enum callbacks | `<record>_field_<i>_enum_load` / `_enum_store` | `extern "C" inline` with the external name `quarry_gen_<gid>_<record>_field_<i>_enum_{load,store}`: C language linkage, external linkage, one entity per identity (below) |
| C++/C storage, wrappers | classes and inline members in `quarry::cxx::quarry_gen_<gid>` | external, one entity per identity |
| Plain C, compiled as C | `static` / `static inline` (internal); struct and enum types | unchanged |
| Plain C, compiled as C++ (G2) | the same declarations, inside `inline namespace quarry_gen_<gid> { extern "C" { ... } }` | functions and objects keep internal linkage and C-language-linkage function types; struct and enum types (and every template instantiated over them) are mangled with the identity, one entity per identity (§16.5) |

**Enum callbacks and language linkage.** The C runtime declares the `enum_load`/`enum_store`
members of `quarry_c_element_descriptor_t` inside `extern "C"`, so in C++ they are pointers to
functions with C language linkage: a linkage-specification applies to every function declarator
it contains, and only class member function types and member names are exempt ([dcl.link]).
Function types with different language linkages are distinct types, so a C++-linkage function
is not a valid initializer for those members. Calling a function through a pointer whose type has
a different language linkage is undefined behavior; SEI CERT C++ rule EXP56-CPP ("Do not call a
function with a mismatched language linkage") forbids it.

GCC, Clang and MSVC give both function types the same calling convention and don't diagnose the
mismatch, so their acceptance is no evidence of conformance. EDG-based front ends, behind several
embedded C++ toolchains, can be configured to keep the types distinct, and Oracle C++ treats
mismatches as an anachronism. Before G1 the callbacks were `static inline` inside `extern "C"`,
and the types matched. The first G1 revision made them C++-linkage `inline` functions, and the
types diverged (found in the PR #116 review). The C-storage backend therefore emits:

```cpp
inline namespace quarry_gen_<gid> {
extern "C" inline uint64_t quarry_gen_<gid>_inv_core_Status_field_1_enum_load(const void* p) { ... }
extern "C" inline void quarry_gen_<gid>_inv_core_Status_field_1_enum_store(void* p, uint64_t n) { ... }
... { ..., quarry_gen_<gid>_inv_core_Status_field_1_enum_load,
          quarry_gen_<gid>_inv_core_Status_field_1_enum_store } ...
}
```

- **Matching types.** The callbacks have C language linkage, so their types are exactly the
  runtime members' types. The runtime API, the C API epoch and the wire format are unchanged.
- **Isolation.** A C-linkage name is not mangled, and [dcl.link] makes same-named C-linkage
  functions declared in different namespaces one function. The enclosing `quarry_gen_<gid>`
  namespace therefore doesn't isolate them, and the name itself carries the full 128-bit
  identity. Different generated definitions get different external symbols, and identical
  definitions share one. The renderer writes the identity as the `S(h)` self-identity
  placeholder, so the name is hashed as written and substituted like every other self reference.
- **ODR.** The callbacks are `inline` with external linkage: one entity, defined with the same
  tokens in every translation unit that includes the same identity. That matches the external-
  linkage `inline` descriptors and wrappers that reference them. Two alternatives don't work:
  `static` with `extern "C"` would restore the original internal-linkage reference from
  external-linkage inline entities, and C-linkage names without the identity would merge
  different definitions.
- **Reserved names.** Schema identifiers can't start with `quarry_gen_` in C-storage output
  (§16.2), so no other generated name can collide with the callback names. The backend also
  validates each callback name and checks that it is unique per namespace. The `quarry_gen_`
  prefix is reserved for external C symbols too: user code must not define C-linkage symbols
  with it.
- **Cost.** The code is the same, with no runtime storage, initialization, allocation or hashing.
  The unmangled name is shorter than the mangled C++ name it replaces. Plain C output is
  byte-identical: plain C keeps `static inline` callbacks inside its `extern "C"` block, where the
  types already match.
- **Regression tests** (`generated_code_identity_integration_test`) use only rules that mainstream
  compilers enforce:
  - a structural check of every callback definition and descriptor reference;
  - a compile probe: redeclaring a callback with `extern "C++"` in its own namespace must be
    rejected, because one entity can't have two language linkages;
  - a link and run probe: `extern "C"` redeclarations in an unrelated namespace must denote the
    descriptor's callbacks, which holds only for C linkage;
  - `nm` checks that each object defines exactly its own unmangled, identity-bearing callbacks;
  - in-tree mutation tests that apply each violation to real generated output and require the
    checks to reject it.

### 16.4 Per-namespace storage policies (G3)

`--c-optimize-size[=NS[,NS...]]` selects the native storage policy for C and
C++ with `--cpp-storage c`:

- Omitted: default storage in every namespace.
- Bare `--c-optimize-size`: compact storage in every namespace (existing behavior).
- `--c-optimize-size=fleet.common,fleet.shared`: compact storage only in those exact,
  case-sensitive canonical namespace FQNs; all other namespaces use default storage.

The list is nonempty: `--c-optimize-size=` is an error, as are empty entries,
whitespace anywhere (including around commas), malformed identifiers, duplicates,
unknown namespaces, and repeated occurrences of the option. Names use dot-separated
`[A-Za-z_][A-Za-z0-9_]*` segments. Selection is exact, never recursive: selecting
`fleet` does not select `fleet.common`. Existing container namespaces are accepted;
when they own no declarations there is no storage to optimize. The unnamed root
namespace has no nonempty FQN and can only be optimized with the bare flag.
Validation uses the complete resolved configuration, including namespaces excluded
by `--generate`, and also runs for `--list-outputs` and QBS-only output.

For separate invocations, declare the complete effective storage policy in **every
invocation** for namespaces whose generated definitions affect the selected output:

```sh
quarry-schema-compiler --language c --c-optimize-size=fleet.common \
  --generate fleet.common -o generated configuration.brd
quarry-schema-compiler --language c --c-optimize-size=fleet.common \
  --generate fleet.engine -o generated configuration.brd
```

Here `fleet.engine` uses default storage and embeds the compact `fleet.common`
records. Reverse invocation order gives the same files. Use `--language cpp
--cpp-storage c` for the same workflow with C++ wrappers. A dependency need not be
emitted for its policy to matter: the generator renders its definitions in memory
and chains its generated-code identity into its dependents. `--generate` changes
emission only. A dependency generated with an inconsistent policy is rejected at
compilation with the existing “contains different generated definitions” diagnostic.

Migration: replace differing bare flags across invocations with the same explicit
namespace list in each invocation, then regenerate and rebuild all affected consumers.
Identical effective policies retain identical generated definitions and identities;
the flag spelling and list order do not enter the identity. BRF, QBS, Schema IR,
record IDs, `schema_id`, runtime APIs and API epochs are unchanged. This option
adds no runtime storage, allocation, initialization or hashing. A different effective
policy can change native layouts and generated API, as the bare flag already does.

The C backend resolves the policy once on each planned namespace, before allocating
field identifiers. That same policy drives normalized rendering for every namespace,
including unselected dependencies, followed by the existing dependency-topological
identity computation and substitution. No option text or new field is added to the
canonical identity encoding (format 1): the effective policy enters through the
normalized generated definitions. A dependency-only policy change propagates through
the existing identity checks and dependency identity chain.

Remaining limitations:

- **No ABI promise across releases:** bindings from different generator releases are isolated,
  not interchangeable.
- **User code** that hides generated types behind its own inline definitions (no generated type in
  the signature) is outside what the generated identity can isolate.
- **Non-injective C symbol prefixes** (#119): `dep.engine` and `dep_engine` both name their C
  symbols `dep_engine_...`. Within one configuration the backend rejects a collision; across
  configurations the same record name collides in one translation unit (a compile error in C,
  an ambiguity or GCC "conflicting C language linkage" error in C++). It is never silent, and
  separate translation units stay isolated. #111 case 7 (plain C headers in C++) is resolved by
  G2.

### 16.5 Plain C bindings (#111 G2)

Plain C headers now carry the generated-code identity like C++/C storage, as variant `C` (1) of the
§16.1 encoding. The header and the compatibility `.c` file are hashed in placeholder form,
dependencies first, with the plain C identities of the generated headers they include.

```c
#ifndef QUARRY_GENERATED_C_NS_truck_engine_<GID>_
#define QUARRY_GENERATED_C_NS_truck_engine_<GID>_
#if defined(QUARRY_GENERATED_C_NS_truck_engine_CODE_ID_FORMAT) || \
    defined(QUARRY_NS_truck_engine_SCHEMA_ID_ALGORITHM)
#error "truck/engine.generated.h: this translation unit already includes a different generated definition of namespace truck.engine ..."
#endif
... configuration identity macros (§4.1) ...
#define QUARRY_GENERATED_C_NS_truck_engine_CODE_ID_FORMAT 1U
#define QUARRY_GENERATED_C_NS_truck_engine_CODE_ID_W0 0x....UL   // ... W1, W2, W3
... includes; configuration checks; generated-code identity checks for every dependency ...
#ifdef __cplusplus
inline namespace quarry_gen_<gid> {
extern "C" {
#endif
... declarations, unchanged ...
#ifdef __cplusplus
} /* extern "C" */
} /* inline namespace quarry_gen_<gid> */
#endif
#endif
```

- **Guard and metadata:** the guard is keyed by the namespace encoding (§4.6) and the full
  identity, no longer by the output path and `schema_id`, so output naming cannot change it,
  identical definitions reached through two paths are included once, and different definitions
  never share it. `QUARRY_GENERATED_C_NS_<body>_CODE_ID_*` are preprocessor-only and usable in
  `#if`; the dependency checks use them in C and C++ alike, after the configuration checks.
- **Same translation unit:** a second, different definition of a namespace is rejected by the
  check after the guard. The configuration macros it also tests are shared with C++/C storage, so
  a plain C and a C-storage header of one namespace in one C++ translation unit are rejected too.
- **C++ consumers:** the declarations are in the identity's inline namespace, so
  `::truck_engine_EngineStatus_t` keeps working while the C++ entities it defines get mangled
  names specific to these definitions. That isolates every template instantiated over them
  (#111 case 7: `std::vector<inv_core_Status_t>` from default and `--c-optimize-size` headers in
  two translation units corrupted elements before G2). Approved decision 3: a global-scope
  forward declaration such as `struct truck_engine_EngineStatus_t;` written by user C++ code
  now declares a different entity.
- **Linkage audit:** every plain C function and object is `static` or `static inline`, so it has
  internal linkage in C and in C++, and nothing in the header has external linkage. No
  external-linkage inline definition exists to reference a translation-unit-local entity.
  `extern "C"` stays inside the namespace: it gives every function type C language linkage,
  which keeps the enum `enum_load`/`enum_store` callbacks matching the runtime's C-linkage
  function pointers ([dcl.link], SEI CERT C++ EXP56-CPP, as in §16.3). It cannot merge names
  across namespaces, because [dcl.link] gives C language linkage only to names with external
  linkage. User C functions declared `extern "C"` that take generated types keep unmangled
  names, so C and C++ translation units still link against each other.
- **Reserved names:** schema identifiers starting with `quarry_gen_` are rejected for plain C
  too, and the `..._CODE_ID_*` macros are reserved in the identifier registry.
- **Embedded footprint:** nothing is added outside the preprocessor and `#ifdef __cplusplus`.
  Compiled as C, the allocatable sections, relocations and defined symbols of the Cortex-M
  objects and ELFs are byte-identical to the pre-G2 build; the debug line tables change because
  the header has more lines. CI compares them for every pull request
  (`cortex_m/scripts/compare_machine_code.py`).
- **Tests:** `generated_code_identity_integration_test` (`PlainC*`) covers determinism, definition
  changes, propagation, metadata, the three dependency diagnostics (including a real pre-G2
  header in `tests/fixtures/configuration_identity/pre_generated_code_identity/c/`), same
  translation unit conflicts, case 7 and wire-equivalent definitions in two C++ and two C
  translation units (both link orders, `-O0`, `-O2`, ASan+UBSan), `nm` symbol isolation, callback
  linkage, and mutation tests that remove the namespace or the dependency checks.
