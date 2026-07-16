# Schema Compiler Tool Distribution

`breadcrumbs-schema-compiler` remains a source-tree tool. It should not be
installed or exposed through the `Breadcrumbs` CMake package until the command,
dependency, and downstream generation contracts below are stabilized.

## Current Consumers

| Consumer | Current status | Required stability before installation |
| --- | --- | --- |
| Repository developers | Supported from the build tree | Existing tests and docs are sufficient |
| External users invoking manually | Not yet a supported installed workflow | Relocatable executable, version reporting, and documented dependency behavior |
| Downstream CMake builds | Not yet supported | Host-tool discovery, declared outputs, dependency tracking, and generated include semantics |
| Package maintainers | Not yet supported | Install rules, runtime dependency policy, and package/component layout |
| CI code-generation steps | Build-tree only | Stable CLI, deterministic outputs, and clear generated-file ownership |
| Future language SDK generators | Deferred | Generator selection and language-specific SDK contracts |

## Current CLI Contract

The build-tree command is:

```text
breadcrumbs-schema-compiler [options] INPUT

Options:
  -o, --output-directory PATH  Directory for generated files (default: generated)
      --root-file-stem NAME     Root namespace file stem (default: schema)
      --file-extension EXT      Generated file extension (default: .generated.hpp)
      --version                 Show version information
  -h, --help                    Show help
```

Current behavior:

* accepts exactly one YAML `.brd` input file
* supports one YAML document and the current single-schema-unit frontend
  boundary
* writes generated C++ files into one output directory
* defaults to `generated` for the output directory
* defaults to `schema` for the root file stem
* defaults to `.generated.hpp` for generated file extensions
* writes diagnostics and tool errors to stderr
* is quiet on successful compilation
* returns `0` for success, help, or version
* returns `1` for input read failure, compiler diagnostics, backend failure, or
  output write failure
* returns `2` for command-line usage errors
* preserves unrelated files in the output directory
* replaces each generated file through a temporary sibling path before renaming
  it into place
* preflights backend-generated paths for duplicate normalized paths and lexical
  containment under the selected output directory
* does not delete stale generated files
* does not provide an invocation-wide rollback transaction
* resolves relative input and output paths against the process working
  directory
* supports absolute input and output paths from unrelated working directories
* supports input, output, and working-directory paths containing spaces when
  arguments are passed directly to the process
* supports `--version` as a terminal informational option

Implementation behavior that is not yet an installed-tool contract:

* no machine-readable diagnostics
* no depfile output
* no generated-output manifest
* no explicit backend or language selection
* no import resolution or multi-file source graph
* no package-discovered compiler target
* no CMake generation helper
* no cross-compilation host-tool separation

`--version` prints `breadcrumbs-schema-compiler <version>` to stdout, writes
nothing to stderr, and exits with `0`. It is terminal like `--help`; when
combined with otherwise valid generation options or an input path, it reports
the version and does not generate files.

## Dependency and Relocatability Findings

Installing only the executable is conceptually possible because downstream
users do not need compiler headers or libraries to execute a CLI. It is not yet
a supported package boundary because the current tool links compiler
implementation libraries that depend on:

* libyaml
* Protobuf runtime libraries
* absl libraries selected by the local Protobuf package
* build-generated Schema IR protobuf C++

The source tree currently proves that the executable builds and runs from the
build directory. It does not prove that an installed executable is relocatable
across platforms, that dynamic library lookup works after installation, or that
package managers can discover and bundle the same dependency set.

Compiler libraries, compiler headers, generated protobuf targets, and backend
internals should remain uninstalled even if a future PR installs the executable.

## Downstream Generated-Code Workflow

Generated C++ files are downstream-owned artifacts. The first supported
installed-tool workflow should be direct CLI invocation, not a CMake helper.

Recommended initial downstream contract:

* users choose whether to check generated files into source control or generate
  them in CI/build steps
* generated files are compiled by the downstream project
* generated code includes the installed runtime and links `Breadcrumbs::runtime`
* stale-output cleanup remains caller-owned
* one compiler invocation handles one schema input

Build-system integration remains deferred because a helper must define:

* expected output enumeration
* one-schema versus multi-schema behavior
* dependency tracking and regeneration triggers
* generated include directories
* whether generated files are added to targets automatically
* whether the helper links `Breadcrumbs::runtime`
* filename collision behavior
* multi-config generator behavior
* cross-compilation host-tool discovery

## Future Installed Discovery Policy

The selected future native CMake discovery model is an imported executable
target in the existing `Breadcrumbs` package:

```cmake
find_package(Breadcrumbs CONFIG REQUIRED)

add_custom_command(
    COMMAND $<TARGET_FILE:Breadcrumbs::schema_compiler> ...)
```

`Breadcrumbs::schema_compiler` should identify the executable from the same
installation prefix selected by `find_package(Breadcrumbs ...)`. It is a tool
target used in custom commands, not a link target, and it must not expose
compiler libraries, compiler headers, generated Schema IR protobuf targets, or
backend internals.

The imported target should be added only after a prior PR installs the
standalone executable and verifies relocatability and runtime dependency
lookup from a clean prefix. Until then the compiler remains source-tree-only.

Evaluated discovery options:

* **PATH-only discovery:** rejected as the primary CMake model. It is simple
  and may remain useful for manual invocation, but it cannot guarantee that the
  compiler comes from the same prefix as the selected runtime package when
  multiple Breadcrumbs versions are installed.
* **Imported executable target:** selected for the first native-build CMake
  discovery contract after executable relocatability is proven. It is
  relocatable, prefix-scoped, works naturally with multi-config generators via
  `$<TARGET_FILE:...>`, and keeps compiler internals private.
* **Package variable:** rejected as the primary interface because an imported
  executable target carries target semantics more robustly than a raw absolute
  path. A future override variable may still be useful for host-tool selection.
* **Runtime and compiler package components:** deferred. Components add failure
  and packaging semantics before there is a proven need to install runtime and
  compiler independently.
* **Separate compiler package:** deferred. It may become relevant for
  cross-compilation or host-tools packaging, but it fragments the current small
  SDK before those requirements are concrete.
* **CMake generation helper:** deferred until output enumeration, depfiles,
  stale-output behavior, and host-tool overrides are specified.
* **Expose compiler SDK:** rejected. Compiler libraries and generated protobufs
  are source-tree implementation details.

No CMake package components are needed for the next compiler discovery step.
Components can be reconsidered only if runtime and compiler packaging become
independently installable products.

## Version Compatibility

The repository does not yet enforce broad cross-version compatibility among
the compiler, generated code, runtime, generated-code API, schema-language
version, and BRF version.

The selected pre-1.0 release policy is: same-release compiler/runtime usage is
recommended, while generated-source compatibility is mechanically enforced only
by `kGeneratedCodeApiVersion`.

Compatibility boundaries:

* generated C++ should be compiled against the `Breadcrumbs::runtime` package
  from the same Breadcrumbs release as the compiler that generated it
* generated files contain a generated-code API compatibility guard that catches
  incompatible runtime headers at compile time
* compatible mismatched package releases are not intentionally rejected when the
  generated-code API version still matches
* newer or older runtime release compatibility is not promised as a tested
  workflow until mixed-release tests exist
* the CLI `--version` output is the Breadcrumbs package release version
* BRF v0.1 compatibility remains a wire-format concern, not a promise that any
  generated-code API version can use any runtime package version
* schema-language compatibility and compiler CLI compatibility are separate
  contracts from generated C++ source compatibility

Compiler execution and generated-code compilation are separate stages. Running
the compiler reads schemas and writes generated source; it does not need an
installed runtime package at execution time. Compiling the generated source is
where runtime headers are required and where `kGeneratedCodeApiVersion` is
checked.

`breadcrumbs-schema-compiler --version` reports the Breadcrumbs package release
only. It is not the generated-code API version, BRF wire-format version,
schema-language version, or a runtime ABI version. No additional CLI option for
the generated-code API version is planned until a downstream build has a
demonstrated need to query it before generation.

Multiple installed versions are expected to be controlled through the selected
CMake package prefix. A future `Breadcrumbs::schema_compiler` target should
come from that prefix, avoiding accidental PATH selection of a compiler from a
different installation. Manual PATH invocation remains inherently caller-owned.

Cross-compilation remains deferred. The schema compiler is a host executable,
while `Breadcrumbs::runtime` is a target-side header-only package. A future
cross-compilation contract may require a host-tools package, a user-supplied
compiler executable override, or separate package discovery. The native-build
imported target policy must not be described as cross-compilation support.

## Minimum Prerequisites Before Installation

Before installing `breadcrumbs-schema-compiler`, a future PR should define and
test:

* deterministic output without timestamps or machine-specific paths
* installed executable relocatability on supported platforms
* dynamic dependency policy for libyaml, Protobuf, and absl
* clean-prefix execution of the installed executable
* package-prefix-scoped imported executable discovery as
  `Breadcrumbs::schema_compiler`
* whether a host-tool override is needed before claiming cross-compilation
  support

## Future Implementation Sequence

1. Install the standalone executable without compiler headers or libraries;
   verify relocatability from a temporary prefix.
2. Add `Breadcrumbs::schema_compiler` as an imported executable target in the
   existing package; verify that it resolves from the selected package prefix.
3. Add a minimal downstream `add_custom_command()` example that generates C++
   and compiles it against `Breadcrumbs::runtime`.
4. Decide whether a generation helper is justified only after output enumeration,
   dependency tracking, and cross-compilation host-tool behavior are specified.
