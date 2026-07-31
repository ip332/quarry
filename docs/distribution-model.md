# Distribution Model

Quarry currently supports a small downstream SDK consisting of the
header-only runtime plus the installed schema compiler executable target. The
installed package surface is intentionally smaller than the source-tree build
graph.

## Supported Public SDK

The supported installed SDK surface is:

* `Quarry::runtime`
* `Quarry::schema_compiler`
* public runtime headers, including
  `<quarry/runtime/binary_record.hpp>` and
  `<quarry/runtime/version.hpp>`
* `QuarryConfig.cmake`
* `QuarryConfigVersion.cmake`
* `Quarry_GENERATED_CODE_API_VERSION` package metadata
* `quarry_generate_cpp()` from the installed package config

Downstream CMake projects consume the runtime with:

```cmake
find_package(Quarry CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Quarry::runtime)
```

They may invoke the installed compiler with:

```cmake
$<TARGET_FILE:Quarry::schema_compiler>
```

For installed native builds, they may ask the package helper to create the
custom command and return generated files:

```cmake
quarry_generate_cpp(
    SCHEMA schema.brd
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
    OUT_FILES generated_files
)
```

The runtime is header-only. Installed consumers should not need
repository-relative include paths.

The supported helper-based CMake generation pattern is documented and tested in
`examples/cpp/schema_compiler_cmake`. Quarry does not provide depfiles,
manifest files, target mutation, stale-output cleanup, source-tree
`add_subdirectory()` helper support, imported-target compiler overrides, or
host-tools package discovery. The helper verifies the generated-output
inventory at build time before normal generation and fails before writing files
if the configured inventory is stale. Native builds use
`Quarry::schema_compiler` by default; cross-compiling builds must pass
`SCHEMA_COMPILER <absolute-host-executable>` explicitly. Before output
discovery, the helper also compares the selected compiler's
generated-code API query against `Quarry_GENERATED_CODE_API_VERSION` and
fails configuration on mismatch.

## Artifact Classification

| Artifact | Current target or location | External role | Classification |
| --- | --- | --- | --- |
| Runtime library | `quarry_runtime`, exported as `Quarry::runtime` | Link through CMake and include public runtime headers | Supported public SDK |
| Runtime headers | `include/quarry/runtime/binary_record.hpp`, `include/quarry/runtime/version.hpp` | Compile generated or handwritten C++ that uses BRF runtime mechanics | Supported public SDK |
| C runtime library | `quarry_runtime_c`, exported as `Quarry::runtime_c` (PR-108) | Link through CMake and include public C runtime headers | Supported public SDK |
| C runtime headers | `include/quarry/runtime_c/binary_record.h`, `include/quarry/runtime_c/version.h` (PR-108) | Compile generated or handwritten C that uses BRF runtime mechanics for the scalar/same-namespace-enum/bounded-string/bounded-bytes/bounded-array (including same-namespace-record elements)/same-namespace-nested-record field subset `--language c` currently supports (PR-108/PR-109/PR-110/PR-111/PR-112/PR-113/PR-114) | Supported public SDK |
| CMake package files | `QuarryConfig.cmake`, `QuarryConfigVersion.cmake`, `QuarryTargets.cmake`, `QuarryGenerate.cmake` | Package discovery for the runtime(s), schema compiler target, generated-code API package metadata (C++ and, since PR-108, C), and installed-package generation helper (C++ only; no C equivalent yet, see "Evaluated Packaging Models") | Supported public SDK |
| Schema compiler executable | `quarry_schema_compiler`, exported as `Quarry::schema_compiler`, installed as `quarry-schema-compiler` | Direct CLI invocation or CMake command use through `$<TARGET_FILE:...>`; supports `--language cpp` (default), `--language c`, and, since PR-118, `--language python` | Installed tool target |
| Generated C++ code | Backend output under caller-selected paths | Owned by the downstream project that generated it | Downstream-owned build artifact |
| Generated C code | C backend output under caller-selected paths (PR-107/PR-108) | Owned by the downstream project that generated it | Downstream-owned build artifact |
| Generated Python code | Python backend output under caller-selected paths (PR-118 architecture; PR-119 scalar field support; PR-120 enum field support; PR-121 string/bytes field support; PR-122 fixed-width arrays; PR-123 string/bytes arrays) | Owned by the downstream project that generated it | Downstream-owned build artifact |
| Python runtime package | `runtime/python/` (`quarry-runtime-python` on PyPI, importable as `quarry.runtime.python`; PR-118 epoch constant, PR-119 `binary_record` scalar codec module, PR-120 enum helpers, PR-121 string/bytes helpers, PR-122 fixed-width array helpers, PR-123 variable-width string/bytes array helpers) | `pip install`-based, not CMake-installed or exported -- deliberately outside this document's CMake-centric distribution boundary, since Python has its own packaging ecosystem (see "Python Runtime Packaging" below) | Supported public SDK (distributed via pip, not CMake) |
| Compiler libraries | `quarry_compiler_*`, `quarry_schema_ir_proto` | Internal source-tree composition and tests | Implementation detail |
| Generated protobuf C++ | Build-tree `schema_ir.pb.*` | Compiler implementation dependency | Implementation detail |
| Fuzz targets and corpus | `fuzz/` | Parser hardening during development | Development-only |
| Tests and fixtures | `tests/` | Repository validation | Test-only |
| C++ example | `examples/cpp/basic_encode_decode` | Documentation for installed runtime consumption | Example, not SDK API |
| Future language SDK directory | `sdk/` | Reserved for later language-specific packaging | Deferred |
| Protocol directory | `protocol/` | Reserved for future transport/protocol integrations | Deferred |

Header visibility inside `compiler/` and source-tree CMake target visibility do
not imply an installed SDK contract.

## Evaluated Packaging Models

### Runtime SDK Only

Install only the header-only runtime and its CMake package metadata.

This is the current supported model. It matches the implemented install/export
rules, has a small dependency surface, and lets generated code depend on
`Quarry::runtime` without making compiler internals public.
Generated C++ headers use the runtime's generated-code API compatibility
constant to fail compilation with incompatible runtime headers. That check is
separate from package release version and BRF wire-format version.
The package exposes the target runtime's generated-code API value as
`Quarry_GENERATED_CODE_API_VERSION`. Host compiler/runtime API
compatibility for explicit `SCHEMA_COMPILER` overrides is enforced during CMake
configuration by comparing that package value against the compiler's
generated-code API query before any output discovery occurs.

Since PR-108, this model includes a second, independent runtime: the
header-only C runtime (`Quarry::runtime_c`), with its own generated-code API
compatibility constant (`QUARRY_C_GENERATED_CODE_API_VERSION`) and package
metadata (`Quarry_GENERATED_CODE_API_VERSION_C`), kept deliberately separate
from the C++ epoch so the two languages' generator/runtime contracts can
change independently. Unlike `quarry_generate_cpp()`, there is no
`quarry_generate_c()` CMake helper yet, and therefore no configure-time
host compiler/C-runtime compatibility check for C the way `SCHEMA_COMPILER`
overrides get for C++ -- deliberately deferred, not an oversight: even with
PR-114's scalar/enum/bounded-string/bounded-bytes/bounded-array (including
same-namespace-record elements)/same-namespace-nested-record field
support, there is still no C example or other real downstream C consumer
yet demonstrating a need for
the full helper contract (multi-schema handling, output-inventory
verification, cross-compiling override), so writing one now would remain
speculative. Downstream C consumers use the manual
`add_custom_command()` pattern (`tools/README.md`), exactly as
`tests/consumer/schema_compiler_package_test.cpp`'s
`CConsumerBuildsAndRunsAgainstInstalledPackage` test does.

### Python Runtime Packaging

Since PR-118, a third, independent runtime exists: `runtime/python/`, a plain
pip-installable package (`pyproject.toml` + `src/quarry/runtime/python/`)
exposing only `QUARRY_GENERATED_CODE_API_VERSION_PYTHON = 1`. Unlike the C++
and C runtimes, it is deliberately **not** part of the CMake configure/build
graph: Python has its own packaging ecosystem (pip/PyPI), and forcing it
through CMake's `install()`/`export()` machinery would buy nothing a
downstream Python consumer actually wants (nobody `find_package(Quarry)`s to
get a `.py` file). This also means the compiler-side expected epoch
(`kGeneratedCodeApiVersionPython` in `compiler/backend_python/backend_python.cpp`)
and the runtime package's own `QUARRY_GENERATED_CODE_API_VERSION_PYTHON`
literal are, for now, two independently-maintained copies of the same number
that must be kept in sync by hand -- unlike the C/C++ epochs, which
`configure_file()` derives from one shared CMake scalar. See
`docs/design/python-backend.md`'s "Known limitations" section; a follow-up PR
could wire a shared source of truth if manual drift ever becomes a real
problem.

### Runtime + Compiler SDK

Install the runtime and `quarry-schema-compiler`.

This is partially implemented. The standalone executable is installed to the
standard executable directory, verified from a clean prefix, and exposed through
the existing package as imported executable target `Quarry::schema_compiler`.
No package component, compiler libraries, compiler headers, generated protobuf
targets, or source-tree/compiler-SDK helper APIs are installed. The installed
native package does include the narrow `quarry_generate_cpp()` convenience
helper, which creates a custom command and returns generated files without
mutating downstream targets.
`docs/schema-compiler-tool-distribution.md` owns the detailed tool-distribution
contract.

### Full SDK

Install the runtime, compiler, generated helper headers, exported compiler
targets, and CMake helper functions.

This is rejected for the current repository state. It would expose compiler
libraries, generated protobufs, and backend APIs as public integration points
before their ABI, dependency, and multi-language contracts are stable.

### Source-First Project

Treat all artifacts as source-tree-only.

This no longer matches the repository after PR-065 because the runtime package
is intentionally installable and verified by an external consumer test.

## CMake Boundary

The current install/export boundary should remain limited to:

* `install(TARGETS quarry_runtime EXPORT QuarryTargets)`
* `install(TARGETS quarry_schema_compiler)` as a standalone executable
  in the `QuarryTargets` export set
* installing runtime public headers
* installing the `Quarry` config, version, and target files
* exporting the `Quarry::runtime` imported target
* exporting the `Quarry::schema_compiler` imported executable target

No compiler libraries, generated protobuf targets, tests, fuzzers, examples, or
schema compiler helper targets should be exported by the current package.

Source-tree compiler targets may keep `PUBLIC` dependencies when public
source-tree headers require them. That visibility is a build-graph requirement,
not a downstream packaging promise.

## Deferred Work

Future install/export expansion should be preceded by a separate distribution
decision. In particular:

* installing compiler libraries needs a public compiler SDK contract
* configure-time host compiler/runtime generated-code API validation needs a
  narrow compiler scalar query and helper comparison against the package's
  `Quarry_GENERATED_CODE_API_VERSION` value
* broader generated-code CMake helper support needs source-tree, multi-schema,
  imported-target compiler override, and cleanup policies beyond the current
  helper
* cross-compilation beyond the explicit
  `SCHEMA_COMPILER <absolute-host-executable>` override still needs a host-tool
  package or toolchain model before considering imported target overrides or a
  separate host-tools package
* language-specific examples should be introduced with their corresponding
  runtime or generator support
