# Distribution Model

Breadcrumbs currently supports a runtime-SDK-only downstream distribution
model. The installed package surface is intentionally smaller than the
source-tree build graph.

## Supported Public SDK

The supported installed SDK surface is:

* `Breadcrumbs::runtime`
* public runtime headers, including
  `<breadcrumbs/runtime/binary_record.hpp>`
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
| Schema compiler executable | `breadcrumbs_schema_compiler` | Source-tree tool; may be executed from the build tree | Source-tree tool, not installed SDK |
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

### Runtime + Compiler SDK

Install the runtime and `breadcrumbs-schema-compiler`.

This is deferred. The schema compiler command is useful, but its distribution
contract is not yet stable: CLI packaging, dependency discovery for libyaml,
Protobuf and absl, generated-output layout, and source-loading/import behavior
all need explicit support before the executable becomes an installed SDK tool.

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
* installing runtime public headers
* installing the `Breadcrumbs` config, version, and target files
* exporting the `Breadcrumbs::runtime` imported target

No compiler libraries, generated protobuf targets, tools, tests, fuzzers, or
examples should be exported by the current package.

Source-tree compiler targets may keep `PUBLIC` dependencies when public
source-tree headers require them. That visibility is a build-graph requirement,
not a downstream packaging promise.

## Deferred Work

Future install/export expansion should be preceded by a separate distribution
decision. In particular:

* installing `breadcrumbs-schema-compiler` needs a stable CLI and dependency
  packaging story
* installing compiler libraries needs a public compiler SDK contract
* generated-code CMake helper functions need a stable compiler invocation
  model
* language-specific examples should be introduced with their corresponding
  runtime or generator support
