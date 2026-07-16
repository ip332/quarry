# Distribution Model

Breadcrumbs currently supports a runtime-SDK-only downstream distribution
model. The installed package surface is intentionally smaller than the
source-tree build graph.

## Supported Public SDK

The supported installed SDK surface is:

* `Breadcrumbs::runtime`
* public runtime headers, including
  `<breadcrumbs/runtime/binary_record.hpp>` and
  `<breadcrumbs/runtime/version.hpp>`
* `BreadcrumbsConfig.cmake`
* `BreadcrumbsConfigVersion.cmake`

Downstream CMake projects consume the runtime with:

```cmake
find_package(Breadcrumbs CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Breadcrumbs::runtime)
```

The runtime is header-only. Installed consumers should not need
repository-relative include paths.

## Artifact Classification

| Artifact | Current target or location | External role | Classification |
| --- | --- | --- | --- |
| Runtime library | `breadcrumbs_runtime`, exported as `Breadcrumbs::runtime` | Link through CMake and include public runtime headers | Supported public SDK |
| Runtime headers | `runtime/binary_record.hpp`, `include/breadcrumbs/runtime/binary_record.hpp` | Compile generated or handwritten C++ that uses BRF runtime mechanics | Supported public SDK |
| CMake package files | `BreadcrumbsConfig.cmake`, `BreadcrumbsConfigVersion.cmake`, `BreadcrumbsTargets.cmake` | Package discovery for the runtime target | Supported public SDK |
| Schema compiler executable | `breadcrumbs_schema_compiler`, installed as `breadcrumbs-schema-compiler` | Direct CLI invocation by path or `PATH`; not package-discovered | Installed standalone tool |
| Generated C++ code | Backend output under caller-selected paths | Owned by the downstream project that generated it | Downstream-owned build artifact |
| Compiler libraries | `breadcrumbs_compiler_*`, `breadcrumbs_schema_ir_proto` | Internal source-tree composition and tests | Implementation detail |
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
`Breadcrumbs::runtime` without making compiler internals public.
Generated C++ headers use the runtime's generated-code API compatibility
constant to fail compilation with incompatible runtime headers. That check is
separate from package release version and BRF wire-format version.

### Runtime + Compiler SDK

Install the runtime and `breadcrumbs-schema-compiler`.

This is partially implemented. The standalone executable is installed to the
standard executable directory and verified from a clean prefix. It remains a
direct CLI tool only: no package-discovered compiler target, package component,
compiler libraries, compiler headers, generated protobuf targets, or CMake
generation helpers are installed.
`docs/schema-compiler-tool-distribution.md` owns the detailed tool-distribution
contract. The selected future native CMake discovery model is an imported
executable target named `Breadcrumbs::schema_compiler` in the existing package,
but that target remains deferred.

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

* `install(TARGETS breadcrumbs_runtime EXPORT BreadcrumbsTargets)`
* `install(TARGETS breadcrumbs_schema_compiler)` as a standalone executable
  without adding it to the `BreadcrumbsTargets` export set
* installing runtime public headers
* installing the `Breadcrumbs` config, version, and target files
* exporting the `Breadcrumbs::runtime` imported target

No compiler libraries, generated protobuf targets, tests, fuzzers, examples, or
schema compiler CMake targets should be exported by the current package.

Source-tree compiler targets may keep `PUBLIC` dependencies when public
source-tree headers require them. That visibility is a build-graph requirement,
not a downstream packaging promise.

## Deferred Work

Future install/export expansion should be preceded by a separate distribution
decision. In particular:

* exposing `Breadcrumbs::schema_compiler` should happen only after the
  executable itself is installable
* installing compiler libraries needs a public compiler SDK contract
* generated-code CMake helper functions need a stable compiler invocation
  model
* language-specific examples should be introduced with their corresponding
  runtime or generator support
