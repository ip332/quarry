# Schema Compiler Tool Distribution

`quarry-schema-compiler` is installed as a standalone executable and
exposed through the `Quarry` CMake package as the imported executable
target `Quarry::schema_compiler`. This does not make compiler libraries,
compiler headers, or generated protobuf targets public. The package also
installs a narrow CMake generation helper with native default behavior and an
explicit host compiler override for cross-compiling builds.

## Current Consumers

| Consumer | Current status | Required stability before installation |
| --- | --- | --- |
| Repository developers | Supported from the build tree | Existing tests and docs are sufficient |
| External users invoking manually | Supported by direct installed executable path or `PATH` | Manual invocation remains caller-owned |
| Downstream CMake builds | Supported through `quarry_generate_cpp()` or direct `add_custom_command()` use of `Quarry::schema_compiler` | Cross builds require explicit `SCHEMA_COMPILER`; depfiles and stale-output cleanup remain deferred |
| Package maintainers | Runtime package plus imported compiler executable target | Component layout remains deferred |
| CI code-generation steps | Build-tree only | Stable CLI, deterministic outputs, and clear generated-file ownership |
| Future language SDK generators | Deferred | Generator selection and language-specific SDK contracts |

## Current CLI Contract

The build-tree command is:

```text
quarry-schema-compiler [options] INPUT

Options:
  -o, --output-directory PATH  Directory for generated files (default: generated)
      --root-file-stem NAME     Root namespace file stem (default: schema)
      --file-extension EXT      Generated file extension (default: .generated.hpp)
      --list-outputs            Print generated output paths without writing files
      --print-generated-code-api-version
                                Print the generated-code API compatibility version and exit
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
* supports `--list-outputs` as a read-only output-inventory query
* supports `--print-generated-code-api-version` as a terminal machine-readable
  compatibility query
* writes diagnostics and tool errors to stderr
* is quiet on successful compilation
* returns `0` for success, output listing, help, version, or generated-code API
  version query
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

Installed executable boundary:

* installed to `${CMAKE_INSTALL_BINDIR}` as `quarry-schema-compiler`
* may be invoked directly by absolute path or through the process `PATH`
* exposed as imported executable target `Quarry::schema_compiler`
* not exposed through package components
* no package variable points to the compiler executable
* `quarry_generate_cpp()` is provided for installed package builds, using
  `Quarry::schema_compiler` by default in native builds and
  `SCHEMA_COMPILER` when an explicit host tool is required

Implementation behavior that is not yet an installed-tool contract:

* no machine-readable diagnostics
* no depfile output
* no generated-output manifest
* no explicit backend or language selection
* no external type resolution or multi-file backend output planning; the
  compiler does load and validate a root's relative-import source graph
* no automatic cross-compilation host-tool discovery

`--version` prints `quarry-schema-compiler <version>` to stdout, writes
nothing to stderr, and exits with `0`. It is terminal like `--help`; when
combined with otherwise valid generation options or an input path, it reports
the version and does not generate files.

`--list-outputs` compiles and validates the input schema far enough to construct
the same backend `GenerationPlan` used by normal generation. It does not render
generated file contents, create output directories, inspect existing output
files, create temporary files, or invoke the file writer. On success, stdout
contains only one generated path per line in deterministic plan order and
stderr is empty. On compiler or planning failure, stdout is empty, diagnostics
are written to stderr, and the command exits with `1`.

Listed paths are the paths a normal generation invocation would write for the
same options: each backend-planned relative output path is joined to the
selected output directory. Relative output directories therefore produce
relative listed paths; absolute output directories produce absolute listed
paths. The tool does not canonicalize or rebase listed paths. `--help` and
`--version` take precedence over `--list-outputs`. The output is line-oriented
and does not define an escaping format; newline characters in output-directory,
root-stem, or extension arguments are outside the initial script contract.

## Dependency and Relocatability Findings

The installed executable links private Quarry compiler implementation
libraries into the tool binary. It does not require uninstalled Quarry
shared libraries at runtime in the tested configuration.

The executable may still depend dynamically on package-manager or system
libraries selected by the build:

* libyaml
* Protobuf runtime libraries
* absl libraries selected by the local Protobuf package

Those third-party libraries are not bundled or installed by Quarry in this
PR. Users and package managers are expected to provide compatible dynamic
dependencies through normal platform loader behavior.

The clean-prefix installed executable test installs Quarry beneath a
temporary prefix whose path contains spaces, invokes
`<prefix>/<bindir>/quarry-schema-compiler --version`, `--help`,
`--list-outputs`, and a representative schema compilation from an unrelated
working directory with absolute input and output paths. It verifies listed
paths, no-write behavior for listing, generated output, absence of stale
temporary files, and absence of source/build paths in generated content. It
does not rename or make the original source and build directories inaccessible.

The clean-prefix package-discovery test installs Quarry beneath a temporary
prefix whose path contains spaces, configures a separate downstream CMake
project with `find_package(Quarry CONFIG REQUIRED)`, verifies
`Quarry::runtime` and `Quarry::schema_compiler`, invokes the compiler
through `$<TARGET_FILE:Quarry::schema_compiler>` in an `add_custom_command`,
compiles the generated C++ against `Quarry::runtime`, and runs the
downstream executable. The installed target files are inspected to ensure
compiler implementation libraries and third-party link targets are not exported.

Compiler libraries, compiler headers, generated protobuf targets, and backend
internals remain uninstalled even though the executable itself is installed.

## Downstream Generated-Code Workflow

Generated C++ files are downstream-owned artifacts. The supported installed
native workflow is the narrow `quarry_generate_cpp()` helper, which
creates the custom command and returns generated files without mutating
downstream targets. Direct CMake invocation of the imported executable target
remains the supported lower-level workflow for callers that need explicit
control.

Supported downstream contract:

* users choose whether to check generated files into source control or generate
  them in CI/build steps
* helper-based builds receive generated outputs through `OUT_FILES`; manual
  builds list generated outputs explicitly in `add_custom_command(OUTPUT ...)`
* the generated output directory is added as an include directory by the caller
* generated files are attached to the caller's targets
* schema files are declared as custom-command dependencies
* generated files are compiled by the downstream project
* generated code includes the installed runtime and links `Quarry::runtime`
* CMake rebuilds generated files when the listed schema dependency is newer
  than the listed generated output
* stale-output cleanup remains caller-owned
* one compiler invocation handles one schema input
* generated C++ should be regenerated after schema changes and when upgrading
  Quarry releases

Broader build-system integration remains deferred because future helpers or
helper extensions must define:

* one-schema versus multi-schema behavior
* generated include directories and target mutation
* whether helper extensions link `Quarry::runtime`
* imported-target or package-wide host-tool override behavior
* stale-output cleanup
* automatic cross-compilation host-tool discovery

## CMake Generation Helper Contract

Quarry installs `QuarryGenerate.cmake` beside the package config and
auto-includes it from `QuarryConfig.cmake`. After:

```cmake
find_package(Quarry CONFIG REQUIRED)
```

installed package consumers can call:

```cmake
quarry_generate_cpp(
    SCHEMA <schema-file>
    OUTPUT_DIR <directory>
    OUT_FILES <variable>
    [ROOT_FILE_STEM <stem>]
    [FILE_EXTENSION <extension>]
    [SCHEMA_COMPILER <absolute-host-executable>]
)
```

The helper is deliberately narrow:

* installed package use only
* exactly one schema input per invocation
* native builds use `Quarry::schema_compiler` by default
* cross-compiling builds require `SCHEMA_COMPILER` with an absolute
  host-runnable compiler path
* configure-time `--list-outputs` for output discovery
* default build-time output-inventory verification before generation
* one build-time normal compiler invocation
* returned outputs are absolute paths in deterministic plan order
* no target creation or mutation
* no automatic include directories
* no automatic `Quarry::runtime` link dependency
* no stale-output deletion
* no depfiles or manifests
* no source-tree `add_subdirectory()` helper support
* no `PATH` search, environment-variable fallback, package-global compiler
  variable, or imported-target compiler override

The lower-level manual `add_custom_command()` workflow remains supported for
callers that want explicit control over the compiler invocation.

### Configure-Time Compiler Resolution

Without `SCHEMA_COMPILER`, the helper resolves `Quarry::schema_compiler`
during configuration by reading the imported executable target location. It
does not evaluate `$<TARGET_FILE:Quarry::schema_compiler>` in
`execute_process()`, does not search `PATH`, and does not hard-code
`<prefix>/bin`.

When `SCHEMA_COMPILER` is provided, the helper requires an absolute,
lexically-normalized path. It rejects generator expressions, semicolons,
newlines, carriage returns, missing paths, directories, and non-runnable
executables. Host-runnability is validated by executing:

```text
<compiler> --version
```

The helper does not parse the version for package-release equality; the later
`--list-outputs` query remains the functional schema/compiler validation.

The helper fails during configuration when:

* `Quarry::schema_compiler` is missing
* the target is not an imported executable target
* an unambiguous configure-time executable location cannot be resolved
* the executable path does not exist
* `CMAKE_CROSSCOMPILING` is true and `SCHEMA_COMPILER` is not provided
* `SCHEMA_COMPILER` cannot be launched successfully with `--version`

Build-tree `add_subdirectory()` consumption is intentionally unsupported by the
helper. A source-tree alias may exist while configuring, but the executable may
not have been built yet, so configure-time output discovery would be unreliable.

The first helper invocation records the selected compiler path in a private
global CMake property. Later invocations in the same build tree must select the
same lexically-normalized absolute path, whether it came from the native
imported target or from `SCHEMA_COMPILER`. Different symlink spellings count as
different compiler identities.

### Path Rules

Relative `SCHEMA` values are resolved against `${CMAKE_CURRENT_SOURCE_DIR}`.
Relative `OUTPUT_DIR` values are resolved against `${CMAKE_CURRENT_BINARY_DIR}`.
Both are lexically normalized to absolute paths. The schema file must exist at
configure time; the output directory does not need to exist.

The helper rejects generator expressions in `SCHEMA`, `OUTPUT_DIR`,
`ROOT_FILE_STEM`, and `FILE_EXTENSION`. It also rejects semicolons, newlines,
and carriage returns in schema paths, output directories, compiler paths,
option values, and reported output paths. Spaces are supported.

The helper passes the absolute output directory to both `--list-outputs` and
the build-time generation command. It requires every listed output to be
absolute, lexically inside `OUTPUT_DIR`, unique within the invocation, and
non-empty. Containment is checked path-component-wise, not with string-prefix
matching.

### Configure-Time Discovery

During configuration, the helper runs:

```text
quarry-schema-compiler \
  --list-outputs \
  --output-directory <absolute-output-dir> \
  [--root-file-stem <stem>] \
  [--file-extension <extension>] \
  <absolute-schema>
```

On success, stderr must be empty and stdout must contain one output path per
line. The helper parses those paths, preserves ordering, rejects malformed or
unsafe output, rejects an empty inventory, and returns the planned paths through
`OUT_FILES` in the caller scope.

On failure, configuration stops with helper context that includes the schema
path, compiler path, compiler exit code, and compiler stderr where available.
The helper does not reinterpret schema diagnostics or guess output names.

### Build-Time Generation

The helper creates one `add_custom_command()` whose `OUTPUT` list is exactly the
configure-time inventory. Before creating `OUTPUT_DIR` or running normal
generation, the command invokes the same resolved compiler executable with
`--list-outputs` and compares the current inventory against the configured
inventory. The comparison is an exact ordered comparison of normalized absolute
paths: same count, same path, same position. The helper does not sort paths.

If the inventories differ, the command fails before writing generated files and
prints the schema path, configured compiler path, configured outputs, current
outputs, and an instruction to reconfigure the CMake build. If the build-time
query itself fails, generation is not attempted and the compiler stderr is
reported.

When the inventory matches, the command creates `OUTPUT_DIR` with
`cmake -E make_directory` and then invokes the compiler with normal generation
arguments. Dependencies include the schema file, `Quarry::schema_compiler`,
the resolved compiler executable path, and `QuarryGenerate.cmake`, because
the installed module owns the build-time verification behavior.

The helper marks returned files as generated, but it does not create a target,
attach sources to a target, add include directories, or link the runtime.
Consumers must use the returned `OUT_FILES` explicitly.

### Reconfiguration Triggers

The helper appends the absolute schema path and resolved compiler executable to
`CMAKE_CONFIGURE_DEPENDS`. Schema edits can therefore rerun configuration and
refresh the `OUTPUT` list when the output inventory changes. Replacing the
compiler executable in place can also trigger reconfiguration on generators that
honor directory configure dependencies.

The build-time inventory check is defense in depth. It does not replace
`CMAKE_CONFIGURE_DEPENDS`, does not update CMake's declared `OUTPUT` list, and
does not rerun CMake automatically. A mismatch means the configured build graph
is stale; callers must rerun CMake configuration.

### Output Ownership and Stale Files

The helper never deletes generated files. If a schema change removes an output
from the plan, the old file may remain in the output directory but is no longer
returned by `OUT_FILES`. Callers should use a dedicated generated-output
directory per helper invocation and clean that directory or the build tree when
needed. The helper does not assume exclusive ownership of arbitrary
caller-provided directories.

On inventory mismatch, the helper does not create current-plan outputs, delete
configured-plan outputs, or run normal generation. Existing files are left as
they were before the failed command.

Duplicate output claims across helper invocations are always configuration
errors. A global configure-time registry records claimed output paths and the
schema that first claimed them.

### Multi-Config and Cross-Compilation Boundary

The helper supports configuration-independent generated outputs. Generator
expressions in helper arguments are rejected, so configuration-specific output
directories such as `$<CONFIG>` are unsupported.

The schema compiler is a host executable while `Quarry::runtime` is
consumed by target-side generated code. Cross-compiling builds must provide
`SCHEMA_COMPILER` with an absolute path to a compiler executable runnable on the
build host. The helper does not discover host tools, create a host-tools
package, or solve target package-manager integration.

## Host Tool and Cross-Compilation Policy

The installed package is still not a host/target split package:
`Quarry::runtime` and `Quarry::schema_compiler` are discovered from
the same package prefix. This is correct for native builds. Cross-compiling
consumers must explicitly provide a host-runnable schema compiler path through
`SCHEMA_COMPILER`; the target package's imported compiler target is ignored for
generation when that override is supplied.

Cross-compiling consumers need three distinct artifacts:

* a **host schema compiler** that runs on the build machine and emits
  target-independent C++ source
* a **target runtime** whose headers and header-only runtime contract are used
  by generated C++ compiled with the target toolchain
* **generated C++** owned by the downstream build and compiled for the target

A target-built `Quarry::schema_compiler` must not be assumed runnable on
the host. The helper therefore rejects `CMAKE_CROSSCOMPILING` unless
`SCHEMA_COMPILER` selects an explicit host compiler.

### Package-Manager Responsibility

Quarry should not try to discover host tools implicitly. Cross-build
systems already have separate concepts for host and target artifacts:

* CMake toolchain files can point a project at a target sysroot while separately
  providing native host tools.
* Conan distinguishes build and host profiles.
* vcpkg has host dependencies for tools needed during a target build.
* Yocto and Buildroot commonly model native tools separately from target
  packages.
* Manual SDKs often provide a host-tools directory plus a target sysroot.

Quarry should provide precise hooks and validation. Package managers or
toolchains remain responsible for installing or selecting the runnable host
compiler.

### Evaluated Host-Compiler Selection Options

* **Fixed imported target only:** retained for native builds, but insufficient
  for cross builds because `Quarry::schema_compiler` may come from a target
  package prefix and be non-runnable on the host.
* **Raw executable path override:** selected and implemented as the narrow
  cross-build extension. `SCHEMA_COMPILER <absolute-host-executable>` is
  resolved during configuration, registered as a configure/build dependency,
  reused for configure-time listing, build-time verification, and generation,
  and supplied by the consumer, package manager, or toolchain.
* **Executable target override:** deferred. It preserves CMake target identity
  but does not solve configure-time execution for non-imported targets and adds
  multi-config/imported-location ambiguity before there is a concrete need.
* **Separate host-tools package:** deferred. It is a good long-term packaging
  shape if package managers need first-class host and target package separation,
  but it is not required before a raw host executable can be tested.
* **Package or toolchain cache variable:** deferred. A global variable may be
  useful later, but the first override should be explicit and local to the
  helper call.
* **Keep cross-compilation unsupported forever:** rejected. The current helper
  architecture can support cross builds once a host-runnable compiler path is
  supplied and validated.

### Host Compiler Override Contract

The helper supports exactly one optional override:

```cmake
quarry_generate_cpp(
    SCHEMA schema.brd
    OUTPUT_DIR generated
    OUT_FILES generated_files
    SCHEMA_COMPILER /absolute/path/to/quarry-schema-compiler
)
```

Selection precedence is:

1. `SCHEMA_COMPILER`, when provided
2. `Quarry::schema_compiler`, only when not cross-compiling

`SCHEMA_COMPILER` must be an absolute path. It rejects generator
expressions, semicolons, newlines, carriage returns, directories, missing
paths, and non-runnable executables. The helper must not search `PATH`, read
environment variables, download tools, or silently fall back to another
compiler.

The selected executable path must be captured during configuration and reused
unchanged for:

* configure-time `--list-outputs`
* build-time inventory verification
* build-time normal generation

The path is added to `CMAKE_CONFIGURE_DEPENDS`, listed in the custom
command dependencies, and used in the build-time verification command. If the
executable is replaced, normal CMake reconfiguration should refresh the output
inventory when the generator honors configure dependencies; otherwise the
default build-time inventory verification remains defense in depth.

### Cross-Compiling Guard Evolution

The cross-compiling rule is:

* cross-compiling without `SCHEMA_COMPILER`: error
* cross-compiling with a valid absolute `SCHEMA_COMPILER`: allowed
* native build without `SCHEMA_COMPILER`: use `Quarry::schema_compiler`
* native build with `SCHEMA_COMPILER`: allowed only if the one-compiler policy
  below is satisfied

The helper can validate host-runnability only by attempting to execute the
selected tool. A target-architecture executable should fail during `--version`
or `--list-outputs` execution with a launch or compiler-query diagnostic.

### Compatibility Policy

`quarry-schema-compiler --version` reports the Quarry package release
only. It does not report generated-code API compatibility, schema-language
compatibility, BRF wire compatibility, or runtime ABI compatibility.

For a separately supplied host compiler, the initial policy should match the
native package policy:

* same-release host compiler and target `Quarry::runtime` are recommended
  while Quarry is pre-1.0
* exact package-release equality is not mechanically enforced by the helper
* generated C++ source compatibility is mechanically enforced later by the
  generated header `static_assert` against
  `quarry::runtime::kGeneratedCodeApiVersion`
* BRF wire compatibility remains independent from host-tool selection
* runtime ABI compatibility is not promised

A future machine-readable compiler capability query, such as a narrow
generated-code API version query, may be useful if configure-time compatibility
rejection becomes necessary. It is not a prerequisite for the first raw-path
override because the generated-code API guard already catches incompatible
runtime headers during target compilation.

### Generated-Code API Capability Query Decision

PR-083 evaluated whether the compiler should expose a machine-readable
generated-code API query for `quarry_generate_cpp()` to compare against
the selected target runtime package during CMake configuration.

The relevant compatibility property is the generated C++ source/runtime header
contract represented by:

```cpp
quarry::runtime::kGeneratedCodeApiVersion
```

This is an equality-based compatibility epoch today. Generated headers contain
a `static_assert` requiring the runtime header value to equal the value expected
by the compiler. A mismatch therefore fails deterministically during target
compilation before linking. This check does not represent package release
equality, runtime ABI compatibility, schema-language compatibility, or BRF
wire-format compatibility.

Current version and compatibility surfaces:

| Surface | Owner | Current value | Meaning | Exposure |
| --- | --- | --- | --- | --- |
| Quarry package version | Top-level CMake project | `0.1.0` | Release identity and CMake package version | `QuarryConfigVersion.cmake`, compiler `--version` |
| Compiler `--version` | `quarry-schema-compiler` | `quarry-schema-compiler 0.1.0` | Human/script release identity | CLI stdout |
| Generated-code API version | Top-level CMake scalar `QUARRY_GENERATED_CODE_API_VERSION` | `1` | Generated source/runtime header compatibility epoch | Configured runtime header, configured backend header, installed package metadata |
| Runtime generated-code API constant | Configured runtime header | `1` | Public C++ runtime representation of the generated-code API epoch | `quarry::runtime::kGeneratedCodeApiVersion` |
| Generated expected API version | Configured backend header | `1U` | Compiler-known value embedded in generated header `static_assert` | Generated C++ source |
| Package generated-code API metadata | `QuarryConfig.cmake` | `1` | Target runtime generated-code API epoch for CMake consumers | `Quarry_GENERATED_CODE_API_VERSION` |
| BRF wire-format version | Runtime parser/emitter | v0.1 record header version | Encoded-byte format compatibility | BRF bytes and parser errors |
| Schema record version | User schema and Schema IR | schema-provided positive integer | Application schema identity | YAML source, Schema IR, generated record IDs/versions |
| Schema IR version | Generated protobuf model | `schema_ir_version: 1` in fixtures | Internal compiler IR serialization version | Source-tree protobuf data |

The generated-code API version is single-sourced by the top-level
`QUARRY_GENERATED_CODE_API_VERSION` CMake scalar. CMake validates the value
as a non-negative `std::uint32_t`, configures the public runtime
`quarry/runtime/version.hpp`, configures the backend's private generated-code API
header, and writes the same value into installed package metadata as
`Quarry_GENERATED_CODE_API_VERSION`. The backend renderer no longer owns a
separate compatibility literal.

The compiler now exposes a narrow generated-code API scalar query and the
package side is available through `Quarry_GENERATED_CODE_API_VERSION`.
`quarry_generate_cpp()` compares the two values during CMake
configuration before output discovery.

Current CLI contract:

```text
quarry-schema-compiler --print-generated-code-api-version
```

Successful output should be exactly one base-10 unsigned integer followed by a
newline, for example:

```text
1
```

The query:

* require no input schema
* perform no generation
* write only the scalar and final newline to stdout
* write nothing to stderr on success
* exit `0` on success
* return usage errors with exit `2`
* keep `--help` terminal and highest precedence, with `--version` taking
  precedence over the generated-code API query when both are present
* remain separate from `--version`, which continues to report release identity
* reject unrelated generation arguments rather than treating them as part of a
  compatibility query

Package config contract:

```cmake
Quarry_GENERATED_CODE_API_VERSION
```

`quarry_generate_cpp()` uses this sequence for every selected compiler,
including the native default and explicit `SCHEMA_COMPILER` overrides:

1. select and validate the host compiler path
2. query the compiler generated-code API version
3. compare it for exact equality with
   `Quarry_GENERATED_CODE_API_VERSION`
4. fail CMake configuration on mismatch with a diagnostic naming the compiler,
   compiler API version, target runtime API version, and remediation
5. run configure-time `--list-outputs`

Exact equality is appropriate because the current generated header guard
already uses equality semantics. If Quarry later needs compatibility
ranges, that should be a separate generated-code API policy change rather than
an implicit reinterpretation of this scalar query.

The query catches a meaningful class of errors during CMake configuration
instead of later target compilation. It is especially useful for
cross-compiling builds where a package manager or toolchain supplies a host
compiler separately from the target runtime package. It does not remove the
generated-header `static_assert`; that assertion remains defense in depth and
covers users who generate code outside the CMake helper.

Implemented single-source design:

* one canonical generated-code API version definition is owned by the
  top-level `QUARRY_GENERATED_CODE_API_VERSION` CMake scalar
* `quarry/runtime/version.hpp` is configured from that scalar and installed at
  the existing public include path
* the backend uses a private configured header derived from the same scalar
  when rendering generated `static_assert` expectations
* `QuarryConfig.cmake` exposes the same value as
  `Quarry_GENERATED_CODE_API_VERSION`
* tests compare runtime headers, generated output, and installed package
  metadata without introducing another authoritative literal
* CMake does not parse C++ headers to recover the value

Future compiler query output should read the backend/compiler representation
configured from this same scalar.

Implementation test requirements for the query/comparison implementation:

* CLI query prints the exact scalar with final newline, empty stderr, and exit
  `0`
* `--help` precedence and usage errors are pinned
* installed and relocated packages preserve the value without source/build
  paths
* helper succeeds when compiler and runtime API versions match
* helper fails during configuration on mismatched API versions for native and
  explicit override compilers
* malformed query output, extra lines, nonnumeric output, and query failures are
  rejected
* generated-header `static_assert` remains present and still fails on mismatch
* tests verify all exposed values derive from the same canonical source

### One Compiler Per Build Tree

The cross-compilation contract enforces one Quarry schema
compiler executable per CMake build tree. The first `quarry_generate_cpp()`
call records the selected compiler path; later calls must use the same resolved
path, whether it came from the native imported target or from `SCHEMA_COMPILER`.

This avoids mixing generated code from multiple compiler releases in one target
build, keeps diagnostics straightforward, and aligns with the existing
same-release recommendation. Multiple compiler use can be reconsidered only
when a concrete multi-toolchain use case exists.

### Target Runtime Isolation

The helper must continue not to link `Quarry::runtime` or mutate consumer
targets. Host compiler selection must not import host runtime libraries into
target link interfaces. The downstream project remains responsible for linking
generated code to the target-side `Quarry::runtime` package it selected.

Executing an override compiler is a trusted build-configuration action. The
helper should validate path shape and launchability, but it cannot prove that
an arbitrary executable is semantically safe.

### Test Matrix

The `SCHEMA_COMPILER` implementation is tested for the portable parts of this
matrix:

* native installed package behavior without override remains unchanged
* relocated prefixes and paths containing spaces still work
* cross-compiling without override is rejected
* cross-compiling with a valid absolute host compiler path succeeds
* a missing, directory, or otherwise non-runnable compiler fails during launch
  or `--list-outputs`; target-architecture launch failures are expected to
  report through the same path where a platform can exercise them
* missing path, directory path, unsafe path, and generator-expression path are
  rejected
* one-compiler-per-build-tree rejects mixed compiler paths
* schema/compiler reconfiguration dependencies still work with the override
* build-time inventory verification uses the selected override
* generated code still compiles only when the target runtime exposes the
  expected `kGeneratedCodeApiVersion`
* manual `add_custom_command()` integration remains unchanged

## Installed Discovery Policy

The native CMake discovery model is an imported executable target in the
existing `Quarry` package:

```cmake
find_package(Quarry CONFIG REQUIRED)

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(generated_header "${generated_dir}/quarry/telemetry.generated.hpp")

add_custom_command(
    OUTPUT "${generated_header}"
    COMMAND
        "$<TARGET_FILE:Quarry::schema_compiler>"
        --output-directory "${generated_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
        Quarry::schema_compiler
    VERBATIM
)

add_executable(app
    main.cpp
    "${generated_header}"
)
target_include_directories(app PRIVATE "${generated_dir}")
target_link_libraries(app PRIVATE Quarry::runtime)
```

`Quarry::schema_compiler` should identify the executable from the same
installation prefix selected by `find_package(Quarry ...)`. It is a tool
target used in custom commands, not a link target, and it must not expose
compiler libraries, compiler headers, generated Schema IR protobuf targets, or
backend internals.

`find_package(Quarry CONFIG REQUIRED)` selects one installation prefix.
`Quarry::schema_compiler` resolves to the executable from that same prefix,
which avoids accidentally selecting a compiler from another installation
through `PATH`. Manual PATH invocation remains caller-owned.

Evaluated discovery options:

* **PATH-only discovery:** rejected as the primary CMake model. It is simple
  and may remain useful for manual invocation, but it cannot guarantee that the
  compiler comes from the same prefix as the selected runtime package when
  multiple Quarry versions are installed.
* **Imported executable target:** selected and implemented for native CMake
  discovery. It is relocatable, prefix-scoped, works naturally with
  multi-config generators via `$<TARGET_FILE:...>`, and keeps compiler
  internals private.
* **Package variable:** rejected as the primary interface because an imported
  executable target carries target semantics more robustly than a raw absolute
  path. A future override variable may still be useful for host-tool selection.
* **Runtime and compiler package components:** deferred. Components add failure
  and packaging semantics before there is a proven need to install runtime and
  compiler independently.
* **Separate compiler package:** deferred. It may become relevant for
  cross-compilation or host-tools packaging, but it fragments the current small
  SDK before those requirements are concrete.
* **CMake generation helper:** implemented for installed package consumers
  through `quarry_generate_cpp()`. The helper uses configure-time
  `--list-outputs`, returns generated files, accepts an explicit absolute
  `SCHEMA_COMPILER` host executable when needed, and intentionally avoids
  target mutation, stale cleanup, depfiles, manifests, source-tree helper
  support, package-global overrides, and imported-target overrides.
* **Expose compiler SDK:** rejected. Compiler libraries and generated protobufs
  are source-tree implementation details.

No CMake package components are needed for the current compiler discovery and
helper model.
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

* generated C++ should be compiled against the `Quarry::runtime` package
  from the same Quarry release as the compiler that generated it
* generated files contain a generated-code API compatibility guard that catches
  incompatible runtime headers at compile time
* compatible mismatched package releases are not intentionally rejected when the
  generated-code API version still matches
* newer or older runtime release compatibility is not promised as a tested
  workflow until mixed-release tests exist
* the CLI `--version` output is the Quarry package release version
* BRF v0.1 compatibility remains a wire-format concern, not a promise that any
  generated-code API version can use any runtime package version
* schema-language compatibility and compiler CLI compatibility are separate
  contracts from generated C++ source compatibility

Compiler execution and generated-code compilation are separate stages. Running
the compiler reads schemas and writes generated source; it does not need an
installed runtime package at execution time. Compiling the generated source is
where runtime headers are required and where `kGeneratedCodeApiVersion` is
checked.

`quarry-schema-compiler --version` reports the Quarry package release
only. It is not the generated-code API version, BRF wire-format version,
schema-language version, or a runtime ABI version. The installed
`--print-generated-code-api-version` query exposes only the generated-code API
epoch and remains separate from release-version output.

The generated-code API version should be incremented when generated C++ from a
new compiler depends on runtime header APIs or generated-code contracts that an
older runtime with the previous epoch cannot compile or use correctly. Examples
include generated code calling a new required runtime API, removing or changing
runtime APIs used by generated code, or changing generated builder/codec
contracts incompatibly with the runtime header surface. It does not necessarily
change for compiler diagnostics, parser improvements, backend refactors,
generated whitespace changes, bug fixes that preserve runtime API use, package
release changes, schema-language changes, or BRF wire-format changes unless
those changes also alter the generated C++/runtime header contract.

Multiple installed versions are expected to be controlled through the selected
CMake package prefix. `Quarry::schema_compiler` comes from that prefix,
avoiding accidental PATH selection of a compiler from a different installation.
Manual PATH invocation remains inherently caller-owned.

Cross-compilation is supported only through an explicit absolute
`SCHEMA_COMPILER` path supplied to `quarry_generate_cpp()`. The schema
compiler is a host executable, while `Quarry::runtime` is a target-side
header-only package. A separate host-tools package or imported target override
can be reconsidered if raw-path override usage proves insufficient. The native
imported target alone must not be described as cross-compilation support.

## Deferred Discovery Work

A future PR should define and test:

* whether a package component is useful if runtime and compiler packaging split
* whether an imported target override is useful after the raw executable
  override exists

## Future Implementation Sequence

1. Publish and test the minimal downstream `add_custom_command()` example that
   generates C++ and compiles it against `Quarry::runtime`. Completed by
   `examples/cpp/schema_compiler_cmake`.
2. Add an installed-native `quarry_generate_cpp()` helper that uses
   configure-time `--list-outputs`, returns `OUT_FILES`, and avoids target
   mutation, stale cleanup, depfiles, manifests, source-tree consumption, and
   cross-compilation in its first version. Completed by the installed
   `QuarryGenerate.cmake` module.
3. Add an installed-helper `SCHEMA_COMPILER <absolute-host-executable>` override
   with one-compiler-per-build-tree enforcement, preserving native default
   behavior and allowing cross-compilation only with the explicit override.
   Completed by PR-082.
4. Decide whether imported target overrides, host-tools packages,
   multi-config-specific output directories, package components, or
   stale-output cleanup are justified.
