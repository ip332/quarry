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
tool workflow is direct CMake invocation of the imported executable target, not
a CMake helper.

Supported downstream contract:

* users choose whether to check generated files into source control or generate
  them in CI/build steps
* generated outputs are listed explicitly in `add_custom_command(OUTPUT ...)`
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

## CMake Generation Helper Decision

Breadcrumbs does not currently provide a CMake generation helper. The
supported SDK contract remains the explicit `add_custom_command()` pattern
shown in `examples/cpp/schema_compiler_cmake`.

The current manual workflow intentionally leaves these responsibilities with
the downstream project:

* declaring the schema input
* choosing the generated output directory
* listing every expected generated output
* invoking `Breadcrumbs::schema_compiler` in `add_custom_command()`
* declaring the schema and compiler target as dependencies
* adding the generated include directory
* attaching generated files to a consumer target
* linking generated-code consumers to `Breadcrumbs::runtime`
* coordinating multiple schema invocations
* deciding whether generated files are checked in or build-generated
* cleaning stale generated files

`--list-outputs` makes a future helper viable for **installed native builds**
because it lets CMake obtain the backend-owned output inventory during
configuration without duplicating filename logic. That viability is deliberately
limited. A first helper should not support source-tree `add_subdirectory()`
consumption, cross-compilation, multi-schema orchestration, stale-output
cleanup, target mutation, depfiles, manifests, or host-tool overrides.

### Configure-Time Compiler Resolution

CMake generator expressions such as
`$<TARGET_FILE:Breadcrumbs::schema_compiler>` are intended for generation and
build contexts, not for direct evaluation inside configure-time
`execute_process()`. A helper that runs the compiler during configuration
should resolve the installed imported executable target by reading its imported
location target property instead.

For the installed package, the helper should require
`Breadcrumbs::schema_compiler` and read an appropriate `IMPORTED_LOCATION`
property from that target. It must not hard-code `<prefix>/bin` or rely on
`PATH`. For the initial native contract, configuration-independent imported
locations are sufficient. Multi-config or configuration-specific imported
locations need a separate tested policy before they are supported.

Build-tree `add_subdirectory()` consumption is different. The
`Breadcrumbs::schema_compiler` alias may exist while configuring, but the
executable has not necessarily been built yet and therefore cannot be executed
at configure time. A configure-time query helper should initially be an
installed-package feature only. Source-tree consumers should continue using the
manual explicit-output pattern unless they provide a prebuilt compiler through
a future host-tool override policy.

### Proposed Future Helper Shape

A future helper may use this narrow custom-command-only shape:

```cmake
breadcrumbs_generate_cpp(
    SCHEMA <schema-file>
    OUTPUT_DIR <directory>
    OUT_FILES <variable>
    [ROOT_FILE_STEM <stem>]
    [FILE_EXTENSION <extension>]
)
```

Initial semantics:

* exactly one schema input
* installed native package use only
* compiler fixed to `Breadcrumbs::schema_compiler`
* helper resolves the imported executable location during configuration
* helper invokes `--list-outputs` during configuration
* helper invokes normal generation once at build time
* output directory is absolute
* relative `OUTPUT_DIR` arguments are resolved against
  `${CMAKE_CURRENT_BINARY_DIR}`
* schema path is absolute
* returned `OUT_FILES` contains the exact paths reported by `--list-outputs`
* outputs are declared in one `add_custom_command(OUTPUT ...)`
* dependencies include the schema and `Breadcrumbs::schema_compiler`
* no target is created or mutated
* no include directories are added automatically
* no automatic `Breadcrumbs::runtime` link dependency is added
* no stale generated files are deleted
* no imports, multi-schema orchestration, or cross-compilation support is
  claimed

### Reconfiguration Triggers

The helper must register the schema file as a configure dependency, for example
through `CMAKE_CONFIGURE_DEPENDS`, so schema edits can rerun configuration and
refresh the `OUTPUT` list. This is necessary because schema changes may alter
namespaces, emitted file count, output paths, or include relationships. Future
schema imports would require registering every imported schema as a configure
dependency; imports remain unsupported today.

The helper should also register the resolved compiler executable path as a
configure dependency when the path exists. That catches common in-place tool
replacement cases. It does not create a broad version-negotiation contract. If
the selected package prefix, helper module, or generation options change,
normal CMake configuration input tracking should rerun configuration.

The build-time `add_custom_command()` should also depend on
`Breadcrumbs::schema_compiler` so the generated files rebuild when the compiler
target changes. Configure-time re-query and build-time regeneration are
separate concerns; both are needed.

### Output Parsing and Path Rules

The helper should pass an absolute output directory to `--list-outputs` and to
the build-time generation command. This gives unambiguous `OUTPUT` paths,
avoids depending on CMake's configure-time working directory, and keeps output
paths under the binary tree by default.

The current `--list-outputs` format is one path per line. It is sufficient for
an initial helper only if the helper rejects path components that cannot be
represented safely as CMake list elements. The first helper should reject
schema paths, output directories, root stems, file extensions, and listed output
paths containing newlines or semicolons. Spaces are supported and must be
passed with normal CMake list quoting and `VERBATIM`.

The helper must treat malformed `--list-outputs` output as a configure-time
error. It should reject empty lines, duplicate output paths, outputs outside the
requested output directory, and an empty output inventory unless that case is
explicitly supported by a later design.

### Failure Contract

Configure-time failures should stop configuration with concise helper context
and the compiler's stderr preserved. Failure classes include:

* missing `Breadcrumbs::schema_compiler`
* missing imported executable location
* compiler executable not found or not executable
* missing schema file
* invalid schema or backend planning failure
* subprocess launch failure
* nonzero compiler exit status
* malformed or unsafe listed paths
* duplicate listed paths
* listed paths outside `OUTPUT_DIR`

The helper should not rewrite compiler diagnostics or try to recover by
guessing output names.

### Configure/Build Consistency

A configure-time query and a build-time generation are two compiler invocations.
If the schema or compiler changes between them, the build-time generation could
produce a different inventory than the configured `OUTPUT` list. The initial
helper should rely on configure dependencies to cause the next build to
reconfigure before generation. It should not implement a second filename
calculation path. A later robustness PR may add a build-time consistency check
that reruns `--list-outputs` and fails if the inventory differs from the
configured list.

### Output Ownership and Stale Files

The first helper should not delete stale files. Safe deletion requires an
ownership model that is broader than output discovery. The recommended initial
rule is that callers use a dedicated generated directory per helper invocation,
but the helper should document rather than enforce deletion. If a schema change
removes an output, the old file may remain until the user cleans the build tree.

Future cleanup options remain:

* never delete stale outputs and document clean-build expectations
* require a dedicated output directory and clean it before generation
* persist prior inventories and remove only files that used to be generated
* integrate outputs with CMake clean behavior only

No cleanup policy should be guessed in the first helper.

### Host Tools, Multi-Config, and Collisions

The first helper should be native-build-only and should use the compiler from
the selected installed Breadcrumbs package. It should not accept an arbitrary
compiler path or claim cross-compilation support. A host-tool override or
separate host-tools package should be decided separately.

Multi-config generators need a separate policy. Generated outputs should be
configuration-independent unless the helper explicitly supports
configuration-specific output directories. Generator expressions in
`OUTPUT_DIR` should be rejected by the initial helper because configure-time
`--list-outputs` cannot evaluate a per-configuration path.

Multiple helper invocations should maintain a configure-time registry of
claimed output paths within the current CMake configure run. Registering the
same schema twice may be allowed only if options and output paths are
identical; two different invocations that claim the same output path should be
an error.

Evaluated helper boundaries:

* **Manual integration only:** still supported and remains the lowest-level
  public contract.
* **Installed native custom-command helper:** viable as a future PR under the
  constraints above. This is the selected future direction, but it is not
  implemented here.
* **Source-tree/add_subdirectory helper:** deferred because configure-time
  output discovery requires an executable that may not exist yet.
* **Cross-compilation helper:** deferred until host-tool selection is designed.
* **Target-attaching helper:** rejected for now because it would mutate user
  targets by adding sources, include directories, and `Breadcrumbs::runtime`
  linkage before generated-code ownership conventions are mature.
* **Generated schema library abstraction:** rejected for now because it would
  introduce a higher-level target model before multi-schema and stale-output
  behavior are specified.

If a helper is introduced later, it should live in an explicit installed module
such as `BreadcrumbsGenerate.cmake` rather than placing substantial function
logic directly in `BreadcrumbsConfig.cmake`. Loading the main package should
remain small and should continue to expose only the runtime target and compiler
executable target by default.

## Generated Output Planning Contract

The compiler should have one authoritative generated-output planning model.
Filename and include-path decisions belong to the C++ backend, not to CMake
helpers, package config files, tests, or downstream projects.

Current architecture:

* backend options define the output directory, root file stem, and file
  extension
* backend namespace analysis calculates namespace-local file paths and include
  paths
* backend dependency analysis determines whether a namespace emits a file and
  how generated files include each other
* `Backend::generate(...)` returns a `CodegenResult` containing generated file
  paths and rendered file contents
* the schema compiler executable writes the returned files and performs
  tool-side safety checks for duplicate paths, output-root containment, and
  per-file atomic replacement

This separates backend generation from file writing. The backend now also has
an internal `GenerationPlan` stage before rendering:

```text
Schema IR
  -> backend output planner
  -> GenerationPlan
  -> renderer
  -> CodegenResult
  -> schema compiler file writer
```

The plan remains internal. It is sufficient for backend tests, future CLI query
modes, and future CMake integration to share the same output inventory without
reimplementing filename rules outside the backend.

Minimum useful plan data:

* generated language/backend, initially C++
* logical output role, initially generated header
* relative output path under the caller-selected output directory
* generated include path used by other generated files
* deterministic order

The plan does not include rendered file contents, file hashes, build-system
dependency graphs, stale-output cleanup policy, runtime package paths, or CMake
target information. Absolute path validation and atomic replacement remain
responsibilities of the schema compiler tool's file writer.

The `--list-outputs` CLI mode serializes this same internal plan as one path per
line after applying the selected output directory. It does not introduce a
second filename calculation path. This query mode can help users and future
CMake configure-time logic list outputs, but it is not a depfile, manifest,
helper API, or stale-output cleanup mechanism.

An output manifest remains a separate build-time artifact question. It may be
useful for diagnostics or audit trails, but because manifests are produced
during the build, they do not replace configure-time output planning for CMake.

Testing should move output inventory coverage toward the planning layer. Focused
planning tests should cover:

* root-namespace output using `root_file_stem`
* nested namespace directory layout
* custom file extensions
* multiple emitted namespaces and deterministic ordering
* generated include paths for cross-namespace references
* namespaces that do not emit files

The future CMake helper decision should depend on this plan and on
`--list-outputs`. A helper can avoid duplicating backend logic by obtaining
output paths from the compiler's authoritative planning model, but it would need
to execute the compiler during configuration to use those paths as
`add_custom_command(OUTPUT ...)` values. That raises reconfiguration questions
when schema contents, naming options, compiler versions, or backend naming rules
change. Those helper semantics remain deferred.

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
   cross-compilation.
3. Decide whether to add a build-time consistency check that reruns
   `--list-outputs` and fails if build-time generation would produce a
   different inventory than the configured `OUTPUT` set.
4. Decide whether host-tool overrides, multi-config-specific output
   directories, package components, or stale-output cleanup are justified.
