# Tools

This directory contains tools built from the compiler libraries.

## Release artifact validation

The expected public release assets are declared in
`release/artifact-manifest.json`. Validate a separate release staging
directory with:

```sh
python3 tools/validate_release_artifacts.py \
  --root /path/to/release-staging \
  --version 0.1.7 \
  --tag v0.1.7-rc.1
```

See `docs/release-artifacts.md` for the staging contract. This command only
checks release contents; it does not upload or publish anything.

The schema compiler's downstream distribution decision is documented in
`docs/schema-compiler-tool-distribution.md`. The current supported boundary is
the installed `quarry-schema-compiler` executable and the
`Quarry::schema_compiler` imported executable target. Compiler libraries
remain private implementation details.

## quarry-schema-compiler

`quarry-schema-compiler` is the installed schema compiler command. It compiles
one compilation root (a YAML `.brd` file) with its complete import closure
through the production YAML frontend and writes generated files for the
selected namespaces and the selected C++, C, Python, or Go backend.

```text
usage: quarry-schema-compiler [options] INPUT

Options:
  -o, --output-directory PATH  Directory for generated files (default: generated)
      --root-file-stem NAME     Root namespace file stem (default: schema)
      --file-extension EXT      Generated file extension for --language cpp
                                (default: .generated.hpp)
      --language {cpp,c,python,go}
                                Target backend language (default: cpp)
      --cpp-storage {owning,c}  C++ owning containers or fixed C storage
      --c-optimize-size[=NS,...] Pack C presence flags for all or exact namespaces
      --go-import-prefix PATH   Go import path of the output directory, for
                                cross-namespace imports with --language go
      --generate NS[,NS...]     Emit only these namespaces (repeatable). Default: the
                                root's namespace, or every namespace of a
                                configuration root. IDs and QBS never change.
      --emit-qbs PATH           Emit the resolved schema as a deterministic QBS image
      --qbs-reflective          Include reflective record, field, and enum names in QBS
      --list-inputs             Print all resolved schema input paths without writing files
      --list-outputs            Print generated output paths without writing files
      --print-generated-code-api-version
                                Print the generated-code API compatibility version and exit
      --print-schema-id         Print the resolved configuration's 128-bit schema_id
      --name-prefix NAME        Nest generated source names and paths beneath NAME
      --describe                Describe the full configuration without writing files
      --version                 Show version information
  -h, --help                    Show this help text
```

`--language` selects which backend generates output: `cpp` (the default,
unchanged behavior), `c`, `python`, or `go`. Omitting `--language` and passing
`--language cpp` explicitly behave identically. An unrecognized value is a
usage error (exit code `2`) naming the invalid value and the accepted ones.
`--go-import-prefix` is accepted only with `--language go`.

### Compilation roots and `--generate`

The input file is the **compilation root**. The compiler always loads its
complete import closure, the *configuration*, and assigns every record ID from
it (see "recordId Structure and Reserved Ranges" in
`docs/specifications/binary-record-format.md`). Which namespaces are written is
a separate choice:

| Root | Without `--generate` | With `--generate` |
|---|---|---|
| Record schema (`namespace:`/`record:`) | its own namespace (unchanged) | exactly the listed namespaces |
| Configuration file (`configuration: <name>`) | every namespace of the closure | exactly the listed namespaces |

A configuration file names the configuration and imports the schemas it is made
of. It declares no namespace, record, version, fields or enums, gets no record
domain and no bindings of its own, and cannot itself be imported:

```yaml
configuration: class8_truck
imports:
  - vehicle.brd
  - engine.brd
```

`--generate` takes comma-separated namespace FQNs and may be repeated. A name
that no record of the closure declares fails with `BC8002`, listing the
namespaces that can be generated, and writes nothing. Output is planned per
namespace: every record of a selected namespace is generated together, whichever
file declares it.

Selected namespaces may reference records and enums of unselected namespaces.
Their generated files include or import those namespaces' bindings by their
planned paths. Generate those bindings from the **same root**, in the same run or
another one, into the same output directory. The compiler never generates them
implicitly. Separate runs selecting different namespaces of one root write
byte-identical files to one run selecting all of them, so the outputs compose.
Record IDs, Schema IR, the `--emit-qbs` image and its `schema_id` do not depend
on the selection.

```sh
# Embedded ECU: two namespaces of the truck configuration.
quarry-schema-compiler --language c --generate truck.engine,truck.common \
  --output-directory ecu examples/truck/schemas/configuration.brd
# Cloud: every namespace of the same configuration.
quarry-schema-compiler --language python --output-directory cloud \
  examples/truck/schemas/configuration.brd
```

Compiling an imported schema as its own root is still supported. It defines a
**different configuration** whose record IDs and `schema_id` generally differ,
so its bindings do not interoperate with bindings generated from the full root.
The compiler cannot tell a component from a root and doesn't warn about it.
Generated files record their configuration's `schema_id` and check it against
every generated file they include or import (at compile time in C, C++ and Go,
at import time in Python), so mixing bindings from two roots fails with
"... was generated from a different Quarry configuration than ...; regenerate
both from the same compilation root." A dependency generated by a compiler that
predates configuration identity fails with "... predates generated
configuration identity". See `docs/design/configuration-identity.md` §4.

To emit the resolved schema as a self-contained binary QBS image, use the
explicit QBS output mode:

```sh
quarry-schema-compiler --emit-qbs build/workload.qbs schema.brd
```

The image includes imported schema metadata and is produced by the canonical
QBS serializer without build paths or timestamps. It can be consumed by the
generic C runtime through `quarry_qbs_parse()`. Combining `--list-outputs`
with `--emit-qbs` prints the requested path without writing the image; normal
language generation is unchanged unless QBS output is explicitly requested.
`--file-extension` configures only the C++ backend's single generated file
extension; combining it with `--language c`, `--language python`, or
`--language go` is a usage error, since those backends' fixed extensions
(`.generated.h`/`.generated.c` for C; `.py` for Python; `.quarry.go` for Go)
are not configurable from the command line.

**`--language c` supports scalar, enum, bounded string/bytes, bounded arrays,
nested records, and compiler-resolved cross-namespace enum/record references
(including arrays).**
It emits a header per namespace owning records or enums, containing data types,
constant descriptors, and inline type-specific codec wrappers. Applications
include that header and link `Quarry::runtime_c`; no schema-specific `.c` file
needs compilation. An include-only `.c` compatibility file remains in the
output list for existing build scripts. The headers provide: generated C enum
declarations, real C structs for records whose fields are `bool`,
fixed-width signed/unsigned integers, `f32`/`f64`, enum references
declared locally or in an imported namespace (with only non-negative declared
values), bounded `string` fields (fixed-capacity,
NUL-terminated buffer storage sized from the schema's `max_bytes` bound),
bounded `bytes` fields (the same fixed-capacity strategy without the NUL
terminator, since arbitrary binary data has no such convenience), bounded
arrays of those scalar/enum/string/bytes/record element
kinds (fixed-capacity array of the element's own C type, sized from
`max_elements`, plus an explicit element count -- record elements compose
that element record's descriptor through the shared codec), or a record reference to another record
declared locally or in an imported namespace (embedded by value, no pointer or
heap allocation), and a real BRF encode/decode codec API (`_init`,
`_encoded_size`, `_encode`, `_decode`) for every record, backed by the
installed `Quarry::runtime_c` C runtime -- see
`compiler/backend_c/README.md`, `runtime_c/README.md`, and
`docs/design/c-backend.md` for the full scope, the generated codec API
design, and the roadmap. Cross-namespace C++ record and enum references
(including arrays) are supported when the referenced namespaces are generated
from the same compilation root into the same output directory. C, Python and Go
follow the same workflow; Python generated modules import dependencies through
deterministic full-module aliases.
Any record containing an unsupported field -- even mixed with otherwise-
supported fields -- fails generation with a diagnostic identifying the record
and field, rather than silently emitting a struct that drops that field.
Generated C code that contains records checks a C
generated-code API compatibility epoch
(`QUARRY_C_GENERATED_CODE_API_VERSION`, currently epoch `7`), independent
from the C++ epoch described below.

**Generated C header example.** The [C example schema](../examples/c/basic_encode_decode/schema.brd)
generates [`demo/c.generated.h`](../examples/c/basic_encode_decode/generated-header/demo/c.generated.h).
That checked-in snapshot is the complete, unmodified compiler output, including
BRF layout metadata, host member descriptors, and the inline wrappers. Generate
it locally with:

```sh
quarry-schema-compiler --language c --output-directory generated \
  examples/c/basic_encode_decode/schema.brd
```

The generated data type is:

```c
typedef struct {
    bool has_count;
    uint32_t count;
    char label[17];
    uint32_t label_length;
    uint8_t blob[4];
    uint32_t blob_length;
} demo_c_Sample_t;
```

String/bytes fields and arrays use their length/count as presence: zero means
absent, so they have no `has_` member. Scalars and nested records retain flags.
Empty elements inside a nonempty array are preserved.

The header also defines `demo_c_Sample_init()`, `demo_c_Sample_encoded_size()`,
`demo_c_Sample_encode()`, and `demo_c_Sample_decode()`. For example:

```c
demo_c_Sample_t sample;
demo_c_Sample_init(&sample);
sample.has_count = true;
sample.count = 42U;

demo_c_Sample_encoded_size_result_t measured = demo_c_Sample_encoded_size(&sample);
if (measured.status == QUARRY_C_STATUS_OK) {
    /* measured.size is the required output buffer capacity. */
}
```

Include `demo/c.generated.h` and link `Quarry::runtime_c`; the
[complete application](../examples/c/basic_encode_decode/main.c) shows encoding
and decoding with status checks.

**`--language python` supports scalar, enum, bounded string/bytes, nested
record, and supported array fields.** It emits one `.py` module per namespace
that directly owns one or more records, enums, or both, inside a true
nested Python package (one real directory plus `__init__.py` per
namespace segment, unlike C's flat symbol-prefix convention -- see
`docs/design/python-backend.md`). Each record renders as a `@dataclass`
(one `Optional[bool | int | float | str | bytes | <EnumClass>] = None`
attribute per declared field) whose `encode`/`decode`/`encoded_size`
methods delegate to private `_quarry_` codec helpers -- the public/internal split
PR-118A's investigation recommended, wired end to end. Those helpers
perform real BRF encode/decode for `bool` and every fixed-width
signed/unsigned integer and `f32`/`f64` field, for a local or imported,
non-negative-valued `enum` field (rendered as a real `enum.IntEnum`
subclass, always emitted before any record referencing it in the same
module), and for a bounded `string`/`bytes` field (UTF-8 validation for
`string` delegated entirely to Python's own `str.encode`/
`bytes.decode("utf-8")`, not hand-rolled), via a
`quarry.runtime.python.binary_record` runtime module (built on the
standard library's `struct` module), verified byte-for-byte
wire-compatible with the C and C++ backends
(`tests/interop/python_cpp_c_codec_interop_test.cpp`). Bounded arrays of
fixed-width scalar or non-negative enum elements and bounded arrays of string,
bytes, or record elements are supported; nested record fields and
compiler-resolved cross-namespace enum/record references are supported by
composing generated record helpers. Nested arrays and recursive records fail
generation with a diagnostic naming
the record and field, mirroring
the same "do not emit partial code" rule established for the C backend.
Schema-derived names are deterministically escaped for Python keywords and
illegal characters, with post-normalization collisions rejected. Every generated module imports
`QUARRY_GENERATED_CODE_API_VERSION_PYTHON`
from the `quarry.runtime.python` package (`runtime/python/`, pip-installed
independently of CMake) and raises `ImportError` at import time on a
mismatch, mirroring the philosophy of the C/C++ compile-time epoch guards
in an idiomatic Python (import-time, not compile-time) way. See
`docs/design/python-backend.md` for the full scope and roadmap.

**`--language go` supports every field kind the C and Python backends
support**: `bool`, fixed-width integers, `f32`/`f64`, non-negative enums,
bounded `string`/`bytes`, bounded arrays of those kinds and of records, and
nested records, including compiler-resolved cross-namespace references. It
emits one Go package per namespace that owns records or enums
(`<namespace segments>/<root-file-stem>.quarry.go`, package named after the
last segment), for services that decode device data in the cloud. Each record
is a Go struct with `MarshalBRF`, `AppendBRF`, `UnmarshalBRF` and
`...WithRecordID` methods; optional scalars, enums and nested records are
pointers (nil when absent), while strings, bytes and arrays are absent when
empty. Generated code imports only `github.com/ip332/quarry/runtime/go/brf`
(`runtime/go/`) and fails to compile against a runtime with a different Go
generated-code epoch (currently `1`). Generated files are gofmt-formatted.

Packages that refer to another namespace import it as
`<go-import-prefix>/<namespace segments>`, so pass the Go import path that
corresponds to `--output-directory`, and generate the referenced namespaces from
the same root into the same directory:

```sh
quarry-schema-compiler --language go --go-import-prefix example.com/svc/gen \
  --generate benchmark.workload,benchmark.workload.shared \
  --output-directory gen benchmarks/schemas/workload.brd
```

Go output is checked byte for byte against the C codec, including agreement on
which malformed inputs are rejected (`tests/interop/go_codec_interop_test.cpp`).
See `docs/design/go-backend.md` and `runtime/go/README.md`.

For downstream use, build and install the runtime package separately:

```sh
cd runtime/python
python -m pip install -e '.[test]'
python -m build --wheel --sdist
```

Install the wheel into the downstream environment, then place the generated
Python package on that project's import path. The runtime is pure Python,
requires Python 3.9+, is not yet published to PyPI, and generated modules
fail at import time if the runtime compatibility epoch differs.

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

`--print-schema-id INPUT` compiles the complete resolved configuration and
prints its QBS schema identity as exactly 32 lowercase hexadecimal digits and a
newline. It writes no files and writes no other stdout on success. A valid
`--generate` selection may also be supplied; selection does not affect the
identity. Invalid schemas or selections produce diagnostics on stderr and no
identity on stdout. The query reuses the same identity calculation used by QBS
serialization and backend code-generation inputs, without serializing a QBS
image unless `--emit-qbs` is requested.

The command-line parser treats `--help` first, `--version` second, and
`--print-generated-code-api-version` third.

`--list-inputs INPUT` compiles and validates the root and prints every schema in
its complete resolved import closure, including the root, configuration files,
transitive imports and unreferenced imports. Every schema is printed at its
canonical absolute path. The root and each import are also printed at the
absolute path they were named by, lexically normalized with symbolic links kept,
when it differs from the canonical path, so that a build system can notice a
link being retargeted. Paths are sorted lexicographically, with duplicates
removed and one newline-terminated path per line. Spaces are preserved. Paths containing a newline or carriage return
are rejected before any paths are printed. Invalid schemas or imports produce
diagnostics on stderr and no input inventory on stdout. The query writes no
files and does not prepare a BRF layout, compute identity, or serialize QBS.
A `--generate` selection may be supplied for validation but never narrows the
input closure. Other generation/output query options cannot be combined with it.

`quarry_generate_cpp()` uses this query at configuration time to track every
resolved schema in both custom-command `DEPENDS` and `CMAKE_CONFIGURE_DEPENDS`.
Editing a transitive or unused import therefore regenerates bindings; adding or
removing imports reconfigures the generated-output inventory. The selected host
compiler must support `--list-inputs`; older executables fail with a query
diagnostic instead of silently using incomplete dependencies. Manual generation
commands should likewise track the full closure, not just the root schema.

`--list-outputs` is a terminal generation mode. It still requires a valid input
schema and accepts generation options that affect output names:
`--output-directory`, `--root-file-stem`, `--generate`, and (for
`--language cpp`) `--file-extension`. It uses the same namespace output plan as
generation, so it lists exactly the files the same command writes. It compiles and validates the schema through backend
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

The output is line-oriented; scripts should avoid newline characters in
output-directory, root-stem, or extension arguments.

Exit codes:

* `0`: successful generation, output listing, help, or version
* `1`: input read failure, compiler diagnostics, backend failure, or output
  write failure
* `2`: command-line usage error

The command supports exactly one root input file. That root may load a
transitive relative-import graph, all of which is compiled; `--generate` and the
root kind decide which namespaces are emitted. Multiple roots per invocation,
stale-output deletion, response files, color diagnostics, and JSON diagnostics
remain out of scope.

Relative input and output paths are resolved relative to the process working
directory. Absolute input and output paths work from unrelated working
directories, including directories whose paths contain spaces. The tool does
not rebase relative paths against the input file's parent directory.

That input file contains one YAML document: either a configuration file or one
source schema unit. A schema unit has one dotted namespace path and one primary
record, with zero or more fields and zero or more enum declarations in that
namespace. Several files may declare records in the same namespace. Multiple
primary records per file and YAML document streams remain unsupported. Imports
are loaded into the compiler context and resolved before Schema IR lowering.

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
## Record discovery with `quarry-brf-inspect`

To discover the records available in a QBS image, use the installed generic
BRF inspection tool without a BRF input:

```sh
quarry-brf-inspect --qbs build/workload.qbs --list-records
```

This prints one record per line in validated QBS record-table order. Each line
starts with the exact numeric ID accepted by `--record-id`, followed by the
canonical record identity accepted by `--record-name`. Reflective QBS images
append the optional display name in parentheses; minimal QBS images still
provide the canonical identity. QBS has no runtime root-record concept, so all
records present in the image, including referenced records, are listed.

## Protobuf translator toolchain

The isolated `quarry-protobuf-translator` uses the protobuf descriptor-set
toolchain found by CMake. The compiler verifies that `protoc` and the selected
protobuf headers/library agree on their major and minor version during
configuration. Generated Schema IR protobuf sources are always produced in
the active build tree by that same `protoc`; they are not source-tree
artifacts.

Use separate build directories for native and Docker builds. The repository
provides `debug`/`coverage` presets for native builds and
`docker-debug`/`docker-coverage` presets for the Docker image. If a mismatch is
reported, remove or abandon the contaminated build directory and reconfigure
with the matching preset; do not reuse host-generated protobuf files inside a
container.


### Compact C presence storage

Use `quarry-schema-compiler --language c --c-optimize-size --output-directory generated schema.brd`
to pack scalar, enum, and nested-record presence flags into `uint8_t quarry_presence_bits[]`.
The default layout retains separate `bool has_<field>` members. Strings, bytes, and arrays
continue to derive presence from length/count in both layouts.

For the compact layout, use generated helpers instead of assigning a presence member:

```c
demo_c_Sample_t sample;
demo_c_Sample_init(&sample);
sample.count = 42;
demo_c_Sample_set_count_present(&sample, true);
if (demo_c_Sample_has_count(&sample)) { /* count is present */ }
```

Pass `false` to the setter to clear a bit. Encode, decode, and sizing APIs stay the same.
The bitmap uses one bit per scalar/enum/record field, rounded up to whole bytes; structure
alignment can affect the final size. This is a portable byte bitmap rather than native C
bitfield members, whose layout is implementation-defined. Wire bytes are unchanged.
Bindings exchanged as C structs must agree on each namespace's effective storage policy;
these native layouts are not interchangeable. See the separate-invocation example below.

### Per-namespace C storage optimization

`--c-optimize-size[=NS[,NS...]]` selects the native storage policy for C and
C++ with `--cpp-storage c`:

- Omitted: default storage in every namespace.
- Bare `--c-optimize-size`: compact storage in every namespace (existing behavior).
- `--c-optimize-size=fleet.common,fleet.shared`: compact storage only in those exact,
  case-sensitive canonical namespace FQNs; all other namespaces use default storage.

The list is nonempty: `--c-optimize-size=` is an error, as are empty entries,
whitespace anywhere (including around commas), malformed identifiers, duplicates,
unknown namespaces, and repeated occurrences of the option. Names use dot-separated
`[A-Za-z_][A-Za-z0-9_]*` segments. Selection is exact, never recursive: selecting
`fleet` does not select `fleet.common`. Existing container namespaces are accepted;
when they own no declarations there is no storage to optimize. The unnamed root
namespace has no nonempty FQN and can only be optimized with the bare flag.
Validation uses the complete resolved configuration, including namespaces excluded
by `--generate`, and also runs for `--list-outputs` and QBS-only output.

For separate invocations, declare the complete effective storage policy in **every
invocation** for namespaces whose generated definitions affect the selected output:

```sh
quarry-schema-compiler --language c --c-optimize-size=fleet.common \
  --generate fleet.common -o generated configuration.brd
quarry-schema-compiler --language c --c-optimize-size=fleet.common \
  --generate fleet.engine -o generated configuration.brd
```

Here `fleet.engine` uses default storage and embeds the compact `fleet.common`
records. Reverse invocation order gives the same files. Use `--language cpp
--cpp-storage c` for the same workflow with C++ wrappers. A dependency need not be
emitted for its policy to matter: the generator renders its definitions in memory
and chains its generated-code identity into its dependents. `--generate` changes
emission only. A dependency generated with an inconsistent policy is rejected at
compilation with the existing “contains different generated definitions” diagnostic.

Migration: replace differing bare flags across invocations with the same explicit
namespace list in each invocation, then regenerate and rebuild all affected consumers.
Identical effective policies retain identical generated definitions and identities;
the flag spelling and list order do not enter the identity. BRF, QBS, Schema IR,
record IDs, `schema_id`, runtime APIs and API epochs are unchanged. This option
adds no runtime storage, allocation, initialization or hashing. A different effective
policy can change native layouts and generated API, as the bare flag already does.

### C++ wrappers with fixed C storage

Use `--language cpp --cpp-storage c` to generate C++20 classes over the C structs
and the shared C runtime. Add `--c-optimize-size` for packed presence. Output is
`.generated.hpp` plus an include-only `.generated.cpp`; link `Quarry::runtime_c`.
The classes live in `quarry::cxx` and use the C record symbol as their name.
They provide bounded setters, const string/span views, native storage access,
and caller-buffer encode/in-place decode, without heap allocation or additional
object storage. The default owning C++ API is unchanged.

See [the runnable example](../examples/cpp/c_storage_encode_decode/README.md)
for API details, failure behavior, and build instructions.

Encoding validates fields as it writes them. On any encode error, discard or
clear the affected output span; `bytes_written` reports its extent even on
failure, including partial nested writes. It is not a valid record length on error. The codec does not roll back failed writes. `encoded_size()` remains
available for a separate validating size calculation.

### Schema description

Run `quarry-schema-compiler --describe examples/truck/schemas/configuration.brd`
from the repository root. The deterministic text includes the configuration name
(for configuration roots), root label, full schema identity and algorithm,
compiler and release metadata, format versions, API epochs, and assigned domain
and record mappings. Standalone record roots omit the configuration line.
Domains and records appear in assigned ID order, using four and eight lowercase
hexadecimal digits respectively. Only namespaces containing records have domains.
The IDs and canonical names come from the resolved Schema IR; identity comes from
the same configuration preparation as `--print-schema-id`.

`--describe` ignores `--generate` (including unknown namespace selections),
backend, storage, output-directory and naming options. Their basic argument syntax
is still checked. It describes the complete configuration and creates no files.
Combining it with `--list-inputs`, `--list-outputs`, `--print-schema-id`,
`--print-generated-code-api-version`, `--version`, `--emit-qbs` or
`--qbs-reflective` is a usage error (exit 2); `--help` remains terminal.
A missing root is exit 2; unreadable or invalid schemas are exit 1.

Relative root labels are lexically normalized with forward slashes; absolute
inputs display only their basename. Backslashes and control bytes in labels are
escaped. Output contains no timestamps or host-specific absolute paths and uses
locale-independent number formatting. Compiler metadata may differ between
compiler builds; it never contributes to schema identity.

### Deliberate source naming isolation

`--name-prefix rev_a` explicitly nests generated source names beneath `rev_a`.
The configuration name never supplies a prefix automatically. The prefix must
be an ASCII identifier starting with a letter, with no double underscores,
language keyword, reserved `std` / `quarry` name, identity constant, or `quarry_gen_` / `quarry_cfg_`
spelling. Empty, repeated or invalid options are usage errors (exit 2). A prefix
matching any canonical namespace's first segment is also exit 2, even when that
namespace is outside `--generate`. Comparisons preserve case. On case-insensitive
filesystems, choose prefixes differing beyond case when sharing an output tree.

The prefix is a source naming operation. Schema IR, canonical FQNs, hierarchical
record IDs, BRF/QBS bytes and `schema_id` remain unchanged. `--generate` and
`--c-optimize-size=NS` continue to take canonical, unprefixed namespace names.
`--describe`, `--print-schema-id`, `--list-inputs` and `--emit-qbs` do not include
prefixes in their data. Ordinary query/QBS conflicts keep their existing rules.
`--list-outputs` reports the prefixed output paths. Generation errors remain exit 1.

| Variant | Naming with `--name-prefix rev_a` |
|---|---|
| C++ owning | `rev_a::truck::engine`, with the existing `quarry_gen_<gid>` inline namespace |
| C | `revq0a_truck_engine_EngineStatus_t`, functions and descriptors with the same stem |
| C++ with C storage | The C names above and `quarry::cxx::revq0a_truck_engine_EngineStatus` |
| Python | `rev_a.truck.engine.schema`, absolute dependency imports and `rev_a/__init__.py` |
| Go | `<go-import-prefix>/rev_a/truck/engine`; the namespace's leaf package name is retained |

Every output path gains a literal `<prefix>/` directory. C/C++ include paths,
identity macro names and guards use the same prefix consistently. For C symbols
and identity metadata, encode the prefix as the first namespace segment using
[#117's encoding](../docs/design/configuration-identity.md#46-namespace-to-macro-encoding-117):
`q` becomes `qq`, `_` becomes `q0`, and `_` separates encoded segments. Prefixed
C enum constants retain this encoded namespace's case, followed by the existing
uppercase enum/member spelling. Unprefixed C symbols retain their legacy names;
#119's general unprefixed migration remains deferred.

The format-1 generated-code identity still hashes normalized emitted definitions
under canonical namespace keys. Prefixes can change those definitions and their
`gid`; the algorithm and format do not change. Prefixes do not make prebuilt
native objects ABI-interchangeable. See the
[truck multi-configuration example](../examples/truck/multi_configuration/README.md)
for working C, C++, C-storage, Python and Go consumers.

### Stage A executable documentation

The [CLI verification guide](../docs/stage-a-cli.md) runs literal commands for all
identity/root/selection/prefix/QBS/storage query modes and checks expected errors.
The [release notes](../docs/release-notes-v0.1.31-rc.1.md) describe regeneration and
rebuild requirements. These are implemented Stage A features; descriptor discovery,
partial images, compatibility classification and translation remain future work.
