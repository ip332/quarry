# Tools

This directory contains source-tree tools built from the compiler libraries.
They are not installed or exported as a package.

The schema compiler's downstream distribution decision is documented in
`docs/schema-compiler-tool-distribution.md`. The current supported boundary is
build-tree execution only; installing the executable or exposing a CMake
imported executable target remains deferred.

## breadcrumbs-schema-compiler

`breadcrumbs-schema-compiler` is the first end-to-end schema compiler command.
It compiles one YAML `.brd` source file through the production YAML frontend,
generates C++ backend files, and writes those generated files to disk.

```text
breadcrumbs-schema-compiler [options] INPUT

Options:
  -o, --output-directory PATH  Directory for generated files (default: generated)
      --root-file-stem NAME     Root namespace file stem (default: schema)
      --file-extension EXT      Generated file extension (default: .generated.hpp)
      --version                 Show version information
  -h, --help                    Show help
```

The command prints compiler diagnostics and tool errors to stderr. Successful
compilation is quiet and returns exit code `0`.

`--version` is a terminal informational option. It prints:

```text
breadcrumbs-schema-compiler <version>
```

to stdout, writes nothing to stderr, and exits with code `0`. When combined
with otherwise valid generation options or an input path, it still reports the
version and does not generate files.

Exit codes:

* `0`: success or help
* `1`: input read failure, compiler diagnostics, backend failure, or output
  write failure
* `2`: command-line usage error

The command supports exactly one input file. Import resolution, multiple input
files, stale-output deletion, response files, configuration files, installed
package integration, color diagnostics, and JSON diagnostics are intentionally
out of scope.

Relative input and output paths are resolved relative to the process working
directory. Absolute input and output paths work from unrelated working
directories, including directories whose paths contain spaces. The tool does
not rebase relative paths against the input file's parent directory.

That input file contains one YAML document and one source schema unit. The
current schema unit has one dotted namespace path and one primary record, with
zero or more fields and zero or more enum declarations in that namespace.
Multiple primary records, multiple namespace roots, YAML document streams, and
imports remain unsupported. With the current YAML contract, successful
compilation normally produces one generated namespace file; richer multi-file
backend output is exercised through lower-level Schema IR inputs.

Generated files are written from backend-provided in-memory `GeneratedFile`
values. Compiler and backend failures do not write output files. Output writes
create parent directories as needed and write each file through a temporary
sibling path before replacing the target file.

Generated-output consistency is file-scoped, not invocation-scoped. Before any
file is written, the tool checks all backend-provided generated paths for
lexical containment under the selected output directory and duplicate
normalized paths. After that preflight, files are committed one at a time in
backend order. If a later file write or rename fails, earlier files from the
same invocation may already have been replaced.

The tool preserves unrelated files in the output directory and does not delete
stale generated files. It does not provide a manifest, a rollback transaction,
concurrent writer coordination, or symlink sandboxing.

Generated C++ code is supported with `Breadcrumbs::runtime` from the same
Breadcrumbs release as the schema compiler that generated it. Generated headers
also contain a narrow compile-time generated-code API guard against incompatible
runtime headers. That guard does not enforce exact release equality or BRF wire
compatibility. Users should regenerate generated code when upgrading
Breadcrumbs.

The compiler executable is installed to the package executable directory, such
as `<prefix>/bin/breadcrumbs-schema-compiler` on platforms using the default
GNU install layout. Installed use is direct invocation by absolute path or
`PATH`, or through the imported executable target
`Breadcrumbs::schema_compiler` from `find_package(Breadcrumbs CONFIG REQUIRED)`.
There is no package component for compiler tools, and no CMake generation
helper is provided.

In CMake, invoke the imported executable target through a generator expression:

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

Downstream projects currently own the generated-output list, include directory,
target source attachment, dependency declaration, and stale-output cleanup.
`examples/cpp/schema_compiler_cmake` is the canonical tested example for this
manual integration pattern. A CMake helper is intentionally deferred until
generated-output enumeration, stale-output ownership, and host-tool selection
are specified.

The installed executable links private Breadcrumbs compiler libraries into the
tool binary. It may still depend on system or package-manager-provided dynamic
libraries such as libyaml, Protobuf, and absl according to the platform and
build configuration. Those third-party libraries are not bundled by
Breadcrumbs.
