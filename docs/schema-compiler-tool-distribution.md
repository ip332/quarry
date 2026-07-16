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
* returns `0` for success or help
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

Implementation behavior that is not yet an installed-tool contract:

* no `--version`
* no machine-readable diagnostics
* no depfile output
* no generated-output manifest
* no explicit backend or language selection
* no import resolution or multi-file source graph
* no package-discovered compiler target
* no CMake generation helper
* no cross-compilation host-tool separation

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

## Package Discovery Options

Evaluated options:

* **Keep source-tree-only:** selected for now. This preserves the runtime-only
  SDK boundary and avoids promising unstabilized tool behavior.
* **Install standalone CLI only:** viable next step after relocatability,
  dependency, version, and CLI-contract work. It should not install compiler
  libraries or headers.
* **Install CLI and imported executable target:** useful after standalone CLI
  installation is stable. `Breadcrumbs::schema_compiler` would be used only in
  `add_custom_command`, not linked.
* **Install CLI with CMake generation helper:** deferred until output
  enumeration and host-tool semantics are precise.
* **Expose compiler SDK:** rejected. Compiler libraries and generated protobufs
  are source-tree implementation details.

No CMake package components are needed while only the runtime is installed.
Components can be considered when an installed compiler executable exists.

## Version Compatibility

The repository does not yet enforce broad cross-version compatibility among
the compiler, generated code, runtime, generated-code API, schema-language
version, and BRF version.

Initial safe rule for a future installed compiler:

* generated C++ should be compiled against the `Breadcrumbs::runtime` package
  from the same Breadcrumbs release
* newer or older runtime compatibility is not promised until generated files
  contain an explicit compatibility guard and tests cover mixed-version use
* the CLI should expose its version before becoming an installed supported tool
* BRF v0.1 compatibility remains a wire-format concern, not a promise that any
  generated-code API version can use any runtime package version

## Minimum Prerequisites Before Installation

Before installing `breadcrumbs-schema-compiler`, a future PR should define and
test:

* `--version` output
* stable help text and exit-code classes
* invocation from arbitrary working directories
* paths containing spaces
* deterministic output without timestamps or machine-specific paths
* installed executable relocatability on supported platforms
* dynamic dependency policy for libyaml, Protobuf, and absl
* exact same-release compiler/generated-code/runtime compatibility rule
* whether the executable is discoverable by `find_program` only or by an
  imported executable target

## Future Implementation Sequence

1. Stabilize and test CLI contract details, including `--version`, arbitrary
   working-directory invocation, and paths with spaces.
2. Install the standalone executable without compiler headers or libraries;
   verify relocatability from a temporary prefix.
3. Add optional CMake package discovery for the executable, likely as an
   imported executable target.
4. Define a narrow CMake generation helper only after output enumeration,
   dependency tracking, and cross-compilation host-tool behavior are specified.
