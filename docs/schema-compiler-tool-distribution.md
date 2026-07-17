# Schema Compiler Tool Distribution

`breadcrumbs-schema-compiler` is installed as a standalone executable and
exposed through the `Breadcrumbs` CMake package as the imported executable
target `Breadcrumbs::schema_compiler`. This does not make compiler libraries,
compiler headers, generated protobuf targets, or CMake generation helpers
public.

## Current Consumers

| Consumer | Current status | Required stability before installation |
| --- | --- | --- |
| Repository developers | Supported from the build tree | Existing tests and docs are sufficient |
| External users invoking manually | Supported by direct installed executable path or `PATH` | CMake package discovery remains deferred |
| Downstream CMake builds | Supported through direct `add_custom_command()` use of `Breadcrumbs::schema_compiler` | Higher-level helper, depfiles, and stale-output cleanup remain deferred |
| Package maintainers | Runtime package plus imported compiler executable target | Component layout remains deferred |
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
      --list-outputs            Print generated output paths without writing files
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
* writes diagnostics and tool errors to stderr
* is quiet on successful compilation
* returns `0` for success, output listing, help, or version
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

* installed to `${CMAKE_INSTALL_BINDIR}` as `breadcrumbs-schema-compiler`
* may be invoked directly by absolute path or through the process `PATH`
* exposed as imported executable target `Breadcrumbs::schema_compiler`
* not exposed through package components
* no package variable points to the compiler executable
* no CMake code-generation helper is provided

Implementation behavior that is not yet an installed-tool contract:

* no machine-readable diagnostics
* no depfile output
* no generated-output manifest
* no explicit backend or language selection
* no import resolution or multi-file source graph
* no CMake generation helper
* no cross-compilation host-tool separation

`--version` prints `breadcrumbs-schema-compiler <version>` to stdout, writes
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

The installed executable links private Breadcrumbs compiler implementation
libraries into the tool binary. It does not require uninstalled Breadcrumbs
shared libraries at runtime in the tested configuration.

The executable may still depend dynamically on package-manager or system
libraries selected by the build:

* libyaml
* Protobuf runtime libraries
* absl libraries selected by the local Protobuf package

Those third-party libraries are not bundled or installed by Breadcrumbs in this
PR. Users and package managers are expected to provide compatible dynamic
dependencies through normal platform loader behavior.

The clean-prefix installed executable test installs Breadcrumbs beneath a
temporary prefix whose path contains spaces, invokes
`<prefix>/<bindir>/breadcrumbs-schema-compiler --version`, `--help`,
`--list-outputs`, and a representative schema compilation from an unrelated
working directory with absolute input and output paths. It verifies listed
paths, no-write behavior for listing, generated output, absence of stale
temporary files, and absence of source/build paths in generated content. It
does not rename or make the original source and build directories inaccessible.

The clean-prefix package-discovery test installs Breadcrumbs beneath a temporary
prefix whose path contains spaces, configures a separate downstream CMake
project with `find_package(Breadcrumbs CONFIG REQUIRED)`, verifies
`Breadcrumbs::runtime` and `Breadcrumbs::schema_compiler`, invokes the compiler
through `$<TARGET_FILE:Breadcrumbs::schema_compiler>` in an `add_custom_command`,
compiles the generated C++ against `Breadcrumbs::runtime`, and runs the
downstream executable. The installed target files are inspected to ensure
compiler implementation libraries and third-party link targets are not exported.

Compiler libraries, compiler headers, generated protobuf targets, and backend
internals remain uninstalled even though the executable itself is installed.

## Downstream Generated-Code Workflow

Generated C++ files are downstream-owned artifacts. The supported installed
native workflow is the narrow `breadcrumbs_generate_cpp()` helper, which
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
* generated code includes the installed runtime and links `Breadcrumbs::runtime`
* CMake rebuilds generated files when the listed schema dependency is newer
  than the listed generated output
* stale-output cleanup remains caller-owned
* one compiler invocation handles one schema input
* generated C++ should be regenerated after schema changes and when upgrading
  Breadcrumbs releases

Broader build-system integration remains deferred because future helpers or
helper extensions must define:

* one-schema versus multi-schema behavior
* generated include directories and target mutation
* whether helper extensions link `Breadcrumbs::runtime`
* host-tool override behavior
* build-time inventory consistency checks
* stale-output cleanup
* cross-compilation host-tool discovery

## CMake Generation Helper Contract

Breadcrumbs installs `BreadcrumbsGenerate.cmake` beside the package config and
auto-includes it from `BreadcrumbsConfig.cmake`. After:

```cmake
find_package(Breadcrumbs CONFIG REQUIRED)
```

installed native consumers can call:

```cmake
breadcrumbs_generate_cpp(
    SCHEMA <schema-file>
    OUTPUT_DIR <directory>
    OUT_FILES <variable>
    [ROOT_FILE_STEM <stem>]
    [FILE_EXTENSION <extension>]
)
```

The helper is deliberately narrow:

* installed package use only
* native builds only
* exactly one schema input per invocation
* compiler fixed to `Breadcrumbs::schema_compiler`
* configure-time `--list-outputs` for output discovery
* one build-time normal compiler invocation
* returned outputs are absolute paths in deterministic plan order
* no target creation or mutation
* no automatic include directories
* no automatic `Breadcrumbs::runtime` link dependency
* no stale-output deletion
* no depfiles or manifests
* no source-tree `add_subdirectory()` helper support
* no cross-compilation support or host-tool override

The lower-level manual `add_custom_command()` workflow remains supported for
callers that want explicit control over the compiler invocation.

### Configure-Time Compiler Resolution

The helper resolves `Breadcrumbs::schema_compiler` during configuration by
reading the imported executable target location. It does not evaluate
`$<TARGET_FILE:Breadcrumbs::schema_compiler>` in `execute_process()`, does not
search `PATH`, and does not hard-code `<prefix>/bin`.

The helper fails during configuration when:

* `Breadcrumbs::schema_compiler` is missing
* the target is not an imported executable target
* an unambiguous configure-time executable location cannot be resolved
* the executable path does not exist
* `CMAKE_CROSSCOMPILING` is true

Build-tree `add_subdirectory()` consumption is intentionally unsupported by the
helper. A source-tree alias may exist while configuring, but the executable may
not have been built yet, so configure-time output discovery would be unreliable.

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
breadcrumbs-schema-compiler \
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
configure-time inventory. The command creates `OUTPUT_DIR` with
`cmake -E make_directory` and then invokes the same resolved compiler executable
with normal generation arguments. Dependencies include the schema file,
`Breadcrumbs::schema_compiler`, and the resolved compiler executable path.

The helper marks returned files as generated, but it does not create a target,
attach sources to a target, add include directories, or link the runtime.
Consumers must use the returned `OUT_FILES` explicitly.

### Reconfiguration Triggers

The helper appends the absolute schema path and resolved compiler executable to
`CMAKE_CONFIGURE_DEPENDS`. Schema edits can therefore rerun configuration and
refresh the `OUTPUT` list when the output inventory changes. Replacing the
compiler executable in place can also trigger reconfiguration on generators that
honor directory configure dependencies.

The helper does not add a separate build-time inventory consistency check. If a
schema or compiler changes between configure-time discovery and build-time
generation, the supported contract relies on the next reconfiguration. A future
PR may add a build-time check that reruns `--list-outputs` and fails when the
inventory differs from the configured `OUTPUT` set.

### Output Ownership and Stale Files

The helper never deletes generated files. If a schema change removes an output
from the plan, the old file may remain in the output directory but is no longer
returned by `OUT_FILES`. Callers should use a dedicated generated-output
directory per helper invocation and clean that directory or the build tree when
needed. The helper does not assume exclusive ownership of arbitrary
caller-provided directories.

Duplicate output claims across helper invocations are always configuration
errors. A global configure-time registry records claimed output paths and the
schema that first claimed them.

### Multi-Config and Cross-Compilation Boundary

The first helper contract supports native installed-package builds with a
configuration-independent imported compiler executable location. Generator
expressions in helper arguments are rejected, so configuration-specific output
directories such as `$<CONFIG>` are unsupported.

Cross-compilation remains unsupported. The schema compiler is a host executable
while `Breadcrumbs::runtime` is consumed by target-side generated code. A future
host-tool override or separate host-tools package must be designed before the
helper can claim cross-compilation support.

## Installed Discovery Policy

The native CMake discovery model is an imported executable target in the
existing `Breadcrumbs` package:

```cmake
find_package(Breadcrumbs CONFIG REQUIRED)

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(generated_header "${generated_dir}/breadcrumbs/telemetry.generated.hpp")

add_custom_command(
    OUTPUT "${generated_header}"
    COMMAND
        "$<TARGET_FILE:Breadcrumbs::schema_compiler>"
        --output-directory "${generated_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
        Breadcrumbs::schema_compiler
    VERBATIM
)

add_executable(app
    main.cpp
    "${generated_header}"
)
target_include_directories(app PRIVATE "${generated_dir}")
target_link_libraries(app PRIVATE Breadcrumbs::runtime)
```

`Breadcrumbs::schema_compiler` should identify the executable from the same
installation prefix selected by `find_package(Breadcrumbs ...)`. It is a tool
target used in custom commands, not a link target, and it must not expose
compiler libraries, compiler headers, generated Schema IR protobuf targets, or
backend internals.

`find_package(Breadcrumbs CONFIG REQUIRED)` selects one installation prefix.
`Breadcrumbs::schema_compiler` resolves to the executable from that same prefix,
which avoids accidentally selecting a compiler from another installation
through `PATH`. Manual PATH invocation remains caller-owned.

Evaluated discovery options:

* **PATH-only discovery:** rejected as the primary CMake model. It is simple
  and may remain useful for manual invocation, but it cannot guarantee that the
  compiler comes from the same prefix as the selected runtime package when
  multiple Breadcrumbs versions are installed.
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
* **CMake generation helper:** implemented only for installed native package
  consumers through `breadcrumbs_generate_cpp()`. The helper uses
  configure-time `--list-outputs`, returns generated files, and intentionally
  avoids target mutation, stale cleanup, depfiles, manifests, source-tree
  helper support, cross-compilation, and host-tool overrides.
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
CMake package prefix. `Breadcrumbs::schema_compiler` comes from that prefix,
avoiding accidental PATH selection of a compiler from a different installation.
Manual PATH invocation remains inherently caller-owned.

Cross-compilation remains deferred. The schema compiler is a host executable,
while `Breadcrumbs::runtime` is a target-side header-only package. A future
cross-compilation contract may require a host-tools package, a user-supplied
compiler executable override, or separate package discovery. The native-build
imported target policy must not be described as cross-compilation support.

## Deferred Discovery Work

A future PR should define and test:

* whether a host-tool override is needed before claiming cross-compilation
  support
* whether a package component is useful if runtime and compiler packaging split
* whether a build-time inventory consistency check is needed for a CMake
  generation helper

## Future Implementation Sequence

1. Publish and test the minimal downstream `add_custom_command()` example that
   generates C++ and compiles it against `Breadcrumbs::runtime`. Completed by
   `examples/cpp/schema_compiler_cmake`.
2. Add an installed-native `breadcrumbs_generate_cpp()` helper that uses
   configure-time `--list-outputs`, returns `OUT_FILES`, and avoids target
   mutation, stale cleanup, depfiles, manifests, source-tree consumption, and
   cross-compilation. Completed by the installed `BreadcrumbsGenerate.cmake`
   module.
3. Decide whether to add a build-time consistency check that reruns
   `--list-outputs` and fails if build-time generation would produce a
   different inventory than the configured `OUTPUT` set.
4. Decide whether host-tool overrides, multi-config-specific output
   directories, package components, or stale-output cleanup are justified.
