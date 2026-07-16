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
`<prefix>/<bindir>/breadcrumbs-schema-compiler --version`, `--help`, and a
representative schema compilation from an unrelated working directory with
absolute input and output paths. It verifies generated output, absence of stale
temporary files, and absence of source/build paths in generated content. It does
not rename or make the original source and build directories inaccessible.

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

Some of these responsibilities are repetitive and could eventually be
encapsulated, but the current compiler CLI does not expose enough build-system
metadata for a reliable public helper. A single compiler invocation can produce
one or more generated headers. The output paths depend on backend-owned logic:
the output directory, namespace partitioning, root file stem, and file
extension. CMake requires the `OUTPUT` set at configure time, so a helper would
either need to duplicate backend filename logic in CMake or require callers to
provide the same output list they already write today. Duplicating the backend
logic would create drift risk and make future backend output changes part of a
CMake API compatibility problem.

A generated-output manifest would be useful for diagnostics and validation, but
it would be produced at build time and therefore would not by itself solve
configure-time `add_custom_command(OUTPUT ...)` enumeration. A depfile is also
not needed for the current one-input compiler contract because the schema file
is already the complete source dependency. Depfiles become more valuable only
after imports or multi-file schema graphs exist.

Stale-output cleanup remains caller-owned. A future helper must define a safe
ownership rule before deleting anything. The likely minimum rule is that each
helper invocation owns a dedicated generated directory that is not shared with
unrelated files, but that rule is not yet enforced by the compiler or package
API.

Multi-schema orchestration is also deferred. The compiler processes one schema
input per invocation. A helper that accepts multiple schemas would need to
define one invocation per schema, collision handling for equal stems or
namespace output paths, unique output directories or root stems, and target
aggregation semantics. Those behaviors should not be guessed in CMake.

Host-tool selection remains native-build-only. The installed
`Breadcrumbs::schema_compiler` target is a host executable selected from the
same package prefix as `Breadcrumbs::runtime`, which is sufficient for the
validated native workflow. A helper should not claim cross-compilation support
until a host-tool override or separate host-tools package policy exists.

Evaluated helper boundaries:

* **Manual integration only:** selected. It is explicit, tested, and accurately
  exposes the current one-schema/no-depfile/no-manifest compiler contract.
* **Custom-command helper:** deferred. It would be the smallest eventual helper,
  but only after generated-output enumeration and host-tool selection are
  stable. A plausible future shape is:

  ```cmake
  breadcrumbs_generate_cpp(
      SCHEMA <schema-file>
      OUTPUT_DIR <directory>
      OUT_FILES <variable>
      [ROOT_FILE_STEM <stem>]
      [FILE_EXTENSION <extension>]
  )
  ```

  Such a helper should create one custom command, return generated files, avoid
  target mutation, avoid stale cleanup, and remain native-build-only unless a
  host-tool policy is defined.
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
CMake package prefix. A future `Breadcrumbs::schema_compiler` target should
come from that prefix, avoiding accidental PATH selection of a compiler from a
different installation. Manual PATH invocation remains inherently caller-owned.

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
* whether a CMake generation helper is justified

## Future Implementation Sequence

1. Publish and test the minimal downstream `add_custom_command()` example that
   generates C++ and compiles it against `Breadcrumbs::runtime`. Completed by
   `examples/cpp/schema_compiler_cmake`.
2. Decide whether a generation helper is justified only after output enumeration,
   dependency tracking, and cross-compilation host-tool behavior are specified.
