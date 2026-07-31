# Tools

This directory contains tools built from the compiler libraries.

The schema compiler's downstream distribution decision is documented in
`docs/schema-compiler-tool-distribution.md`. The current supported boundary is
the installed `quarry-schema-compiler` executable and the
`Quarry::schema_compiler` imported executable target. Compiler libraries
remain private implementation details.

## quarry-schema-compiler

`quarry-schema-compiler` is the first end-to-end schema compiler command.
It compiles one YAML `.brd` source file through the production YAML frontend,
generates C++ backend files, and writes those generated files to disk.

```text
quarry-schema-compiler [options] INPUT

Options:
  -o, --output-directory PATH  Directory for generated files (default: generated)
      --root-file-stem NAME     Root namespace file stem (default: schema)
      --file-extension EXT      Generated file extension for --language cpp
                                (default: .generated.hpp)
      --language {cpp,c,python} Target backend language (default: cpp)
      --list-outputs            Print generated output paths without writing files
      --print-generated-code-api-version
                                Print the generated-code API compatibility version and exit
      --version                 Show version information
  -h, --help                    Show help
```

`--language` selects which backend generates output: `cpp` (the default,
unchanged behavior), `c`, or `python`. Omitting `--language` and passing
`--language cpp` explicitly behave identically. An unrecognized value is a
usage error (exit code `2`) naming the invalid value and the three accepted
ones.
`--file-extension` configures only the C++ backend's single generated file
extension; combining it with `--language c` or `--language python` is a
usage error, since neither backend's fixed extension
(`.generated.h`/`.generated.c` for C; `.py` for Python) is yet independently
configurable from the command line.

**`--language c` supports scalar, same-namespace-enum, bounded
string/bytes, bounded array (of scalar, same-namespace-enum, or
same-namespace-record elements), and same-namespace nested record records
(PR-108/PR-109/PR-110/PR-111/PR-112/PR-113/PR-114).** It emits one
`.h`/`.c` pair per namespace that owns records or enums: generated C enum
declarations, real C structs for records whose fields are `bool`,
fixed-width signed/unsigned integers, `f32`/`f64`, enum references
declared in the *same namespace* as the referencing record (with only
non-negative declared values), bounded `string` fields (fixed-capacity,
NUL-terminated buffer storage sized from the schema's `max_bytes` bound),
bounded `bytes` fields (the same fixed-capacity strategy without the NUL
terminator, since arbitrary binary data has no such convenience), bounded
arrays of those scalar/same-namespace-enum/same-namespace-record element
kinds (fixed-capacity array of the element's own C type, sized from
`max_elements`, plus an explicit element count -- record elements compose
that element record's own generated encode/decode functions directly,
with no new runtime code needed), or a record reference to another record
declared in the *same namespace* (embedded by value, no pointer or heap
allocation), and a real BRF encode/decode codec API (`_init`,
`_encoded_size`, `_encode`, `_decode`) for every record, backed by the
installed `Quarry::runtime_c` C runtime -- see
`compiler/backend_c/README.md`, `runtime_c/README.md`, and
`docs/design/c-backend.md` for the full scope, the generated codec API
design, and the roadmap. **Arrays of string/bytes elements, and
cross-namespace references (plain enum/record fields, or as array
elements), remain unsupported.**
A record containing any such field -- even mixed with otherwise-supported
fields -- fails generation with a diagnostic
identifying the record and field, rather than silently emitting a struct
that drops that field. Generated C code that contains records checks a C
generated-code API compatibility epoch
(`QUARRY_C_GENERATED_CODE_API_VERSION`, currently epoch `2`), independent
from the C++ epoch described below.

**`--language python` supports scalar fields (PR-119), enum fields
(PR-120), string/bytes fields (PR-121), and bounded arrays of fixed-width
scalar or same-namespace enum elements (PR-122), on top of the architecture
skeleton PR-118 established.** It emits one `.py` module per namespace
that directly owns one or more records, enums, or both, inside a true
nested Python package (one real directory plus `__init__.py` per
namespace segment, unlike C's flat symbol-prefix convention -- see
`docs/design/python-backend.md`). Each record renders as a `@dataclass`
(one `Optional[bool | int | float | str | bytes | <EnumClass>] = None`
attribute per declared field) whose `encode`/`decode`/`encoded_size`
methods delegate to three module-level `_encode_<name>`/`_decode_<name>`/
`_encoded_size_<name>` helper functions -- the public/internal split
PR-118A's investigation recommended, wired end to end. Those helpers
perform real BRF encode/decode for `bool` and every fixed-width
signed/unsigned integer and `f32`/`f64` field, for a same-namespace,
non-negative-valued `enum` field (rendered as a real `enum.IntEnum`
subclass, always emitted before any record referencing it in the same
module), and for a bounded `string`/`bytes` field (UTF-8 validation for
`string` delegated entirely to Python's own `str.encode`/
`bytes.decode("utf-8")`, not hand-rolled), via a
`quarry.runtime.python.binary_record` runtime module (built on the
standard library's `struct` module), verified byte-for-byte
wire-compatible with the C and C++ backends
(`tests/interop/python_cpp_c_codec_interop_test.cpp`). Bounded arrays of
fixed-width scalar or same-namespace enum elements are supported; arrays of
string, bytes, records, or arrays, standalone nested-record fields, and
cross-namespace enum references fail generation with a diagnostic naming
the record and field, mirroring
the same "do not emit partial code" rule PR-107 established for the
original C skeleton. Every generated module imports
`QUARRY_GENERATED_CODE_API_VERSION_PYTHON`
from the `quarry.runtime.python` package (`runtime/python/`, pip-installed
independently of CMake) and raises `ImportError` at import time on a
mismatch, mirroring the philosophy of the C/C++ compile-time epoch guards
in an idiomatic Python (import-time, not compile-time) way. See
`docs/design/python-backend.md` for the full scope and roadmap.

The command prints compiler diagnostics and tool errors to stderr. Successful
compilation is quiet and returns exit code `0`.

`--version` is a terminal informational option. It prints:

```text
quarry-schema-compiler <version>
```

to stdout, writes nothing to stderr, and exits with code `0`. When combined
with otherwise valid generation options or an input path, it still reports the
version and does not generate files.

`--print-generated-code-api-version` is the machine-readable compatibility
query. It prints the compiler/backend generated-code API epoch as one base-10
integer followed by a newline, writes nothing to stderr on success, requires no
input schema, performs no generation, and is terminal like `--help` and
`--version`. `quarry_generate_cpp()` uses this query during CMake
configuration before it discovers outputs.

The command-line parser treats `--help` first, `--version` second, and
`--print-generated-code-api-version` third.

`--list-outputs` is a terminal generation mode. It still requires a valid input
schema and accepts generation options that affect output names:
`--output-directory`, `--root-file-stem`, and (for `--language cpp`)
`--file-extension`. It compiles and validates the schema through backend
output planning, prints one planned generated path per line to stdout, writes
diagnostics to stderr on failure, and does not render generated content or
write files. For `--language c`, each planned namespace file contributes two
lines -- the header path, then the source path -- since the C backend's
planner always knows both paths together (see `compiler/backend_c/README.md`).
For `--language python`, output includes one line per ancestor package
`__init__.py` (deduplicated across sibling namespaces sharing a common
ancestor) followed by one line per namespace's own generated module.

Listed paths use the same path-base semantics as normal generation: the backend
planned relative output path is joined to the selected output directory. If the
output directory is relative, listed paths are relative to the process working
directory. If the output directory is absolute, listed paths are absolute. The
tool does not canonicalize or rebase them. Output order is deterministic and
matches backend `GenerationPlan` order.

The output is line-oriented and does not define an escaping format; scripts
should avoid newline characters in output-directory, root-stem, or extension
arguments.

Exit codes:

* `0`: successful generation, output listing, help, or version
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

Generated C++ code is supported with `Quarry::runtime` from the same
Quarry release as the schema compiler that generated it. Generated headers
also contain a narrow compile-time generated-code API guard against incompatible
runtime headers. That guard does not enforce exact release equality or BRF wire
compatibility. Users should regenerate generated code when upgrading
Quarry.

For installed-package helper builds, the generated-code API query is compared
against `Quarry_GENERATED_CODE_API_VERSION` during CMake configuration
before output discovery.

The compiler executable is installed to the package executable directory, such
as `<prefix>/bin/quarry-schema-compiler` on platforms using the default
GNU install layout. Installed use is direct invocation by absolute path or
`PATH`, or through the imported executable target
`Quarry::schema_compiler` from `find_package(Quarry CONFIG REQUIRED)`.
There is no package component for compiler tools.

For installed native builds, use the package helper:

```cmake
find_package(Quarry CONFIG REQUIRED)

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

quarry_generate_cpp(
    SCHEMA schema.brd
    OUTPUT_DIR "${generated_dir}"
    OUT_FILES generated_files
)

add_executable(app
    main.cpp
)
target_sources(app PRIVATE ${generated_files})
target_include_directories(app PRIVATE "${generated_dir}")
target_link_libraries(app PRIVATE Quarry::runtime)
```

For cross-compiling builds, the target Quarry package still provides
`Quarry::runtime`, but the helper must be given a compiler executable that
runs on the build host:

```cmake
find_package(Quarry CONFIG REQUIRED)

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

quarry_generate_cpp(
    SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/telemetry.brd"
    OUTPUT_DIR "${generated_dir}"
    OUT_FILES generated_files
    SCHEMA_COMPILER "/opt/host-tools/bin/quarry-schema-compiler"
)

add_library(telemetry)
target_sources(telemetry PRIVATE ${generated_files})
target_include_directories(telemetry PRIVATE "${generated_dir}")
target_link_libraries(telemetry PRIVATE Quarry::runtime)
```

The path above is only an example layout; package managers or toolchains are
responsible for provisioning the host compiler and passing its absolute path.

The helper is installed with the package and auto-loaded by
`find_package(Quarry CONFIG REQUIRED)`. It supports installed package
consumers, handles one schema per invocation, returns absolute generated paths
through `OUT_FILES`, and does not create or mutate targets. Downstream projects
still own include directories, target source attachment, runtime linkage, and
stale-output cleanup.

The helper verifies generated-output inventory by default at build time. Before
normal generation it reruns `--list-outputs` with the same compiler, schema,
output directory, root file stem, and extension captured at configuration time.
If the ordered output list differs, the command fails before creating the
output directory or writing generated files and instructs the user to rerun
CMake configuration. This check is defense in depth; schema and compiler paths
remain registered with `CMAKE_CONFIGURE_DEPENDS`.

The lower-level manual `add_custom_command()` pattern remains supported for
callers that want explicit control over the compiler invocation.

`SCHEMA_COMPILER` is trusted build configuration and is executed during CMake
configuration. The helper validates that it is absolute, exists, is not a
directory, and successfully runs `--version`; it does not search `PATH`, read
environment-variable fallbacks, download tools, or enforce exact package
release equality. Generated headers still enforce
`quarry::runtime::kGeneratedCodeApiVersion` when the generated C++ is
compiled against the target runtime.

The installed executable links private Quarry compiler libraries into the
tool binary. It may still depend on system or package-manager-provided dynamic
libraries such as libyaml, Protobuf, and absl according to the platform and
build configuration. Those third-party libraries are not bundled by
Quarry.
