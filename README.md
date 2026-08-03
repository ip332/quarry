# Quarry

Quarry is an open-source, schema-driven platform for secure edge-to-cloud
systems, with asset tracking as the first reference application.

The platform is intended to support many domains without requiring changes to
its core architecture.

## Goals

* Device identity and provisioning
* Secure communication (TLS-first)
* Event-driven telemetry
* Serialized-first data model
* Remote command execution
* OTA software updates
* Fleet management and diagnostics

## Design Principles

* TLS-first
* Serialized-first
* schema-driven
* Event-driven
* OTA-capable
* Diagnostics by design

## Status

Project planning and architecture phase.

> This project was formerly called Breadcrumbs. It was renamed to Quarry to
> reflect the current focus on schema-driven binary records rather than the
> original asset-tracking framing; see `jira/backlog.md` (PR-088) for details.

## Development Environment

Docker is the recommended and authoritative environment for CI-equivalent
builds, including `debug-clang-tidy`. Native (non-Docker) `debug` builds are
also supported; native `debug-clang-tidy` is best-effort. See
`docs/development-environment.md` for the full support policy and
troubleshooting.

```sh
docker compose build
docker compose run --rm dev bash
```

## Runtime Package

The C++ Quarry runtime is header-only and installable as a CMake package.
After installing the project, downstream CMake projects can consume it with:

```cmake
find_package(Quarry CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Quarry::runtime)
```

The public runtime include path is:

```cpp
#include <quarry/runtime/binary_record.hpp>
```

The package also defines `Quarry_GENERATED_CODE_API_VERSION`, which is
derived from the same canonical value as
`quarry::runtime::kGeneratedCodeApiVersion` and the generated C++
compatibility assertions.

The schema compiler also exposes a machine-readable compatibility query:

```text
quarry-schema-compiler --print-generated-code-api-version
```

`quarry_generate_cpp()` compares that query against
`Quarry_GENERATED_CODE_API_VERSION` during CMake configuration before it
discovers generated outputs.

The installed package also exposes the schema compiler as an executable CMake
target:

```cmake
$<TARGET_FILE:Quarry::schema_compiler>
```

Generated code should link against `Quarry::runtime`. Downstream projects
can use the installed-native helper:

```cmake
quarry_generate_cpp(
    SCHEMA schema.brd
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
    OUT_FILES generated_files
)
```

The helper returns generated files but does not create or mutate targets.
Downstream projects still own generated include directories, target source
attachment, runtime linkage, and stale-output cleanup. The canonical pattern is
shown in
`examples/cpp/schema_compiler_cmake`.

For C++ schemas that reference imported records or enums, generate each
imported source unit as an explicit root into the same output directory before
generating the root schema. The root header then includes the dependency
headers and uses fully qualified C++ types deterministically. C and Python
cross-namespace dependency generation are also supported; generate each
imported source unit as an explicit root into the same output tree.

At build time, the helper reruns `--list-outputs` before generation and fails
before writing files if the current inventory no longer matches the inventory
captured during CMake configuration. Reconfigure the build after schema or
compiler changes that affect generated output paths.

Native builds use `Quarry::schema_compiler` by default. Cross-compiling
builds must provide `SCHEMA_COMPILER` with an absolute path to a host-runnable
compiler executable; the helper does not search `PATH`, read environment
fallbacks, or import host artifacts into target link interfaces.

The schema compiler also supports `--list-outputs` to print the generated paths
for a schema and generation options without writing files. This query is backed
by the backend's internal generation plan, but it is not a CMake helper,
manifest, depfile, or stale-output cleanup mechanism.

Minimal installed-package examples are available in `examples/cpp/`.
`examples/cpp/schema_compiler_cmake` also demonstrates structured
decode-failure handling (`CodecResult`'s `.error`/`.path`/`.byte_offset`)
against a truncated and a corrupted payload; see `runtime/README.md` for the
full diagnostic contract.

C is the second supported backend language (`--language c`), currently
covering records whose fields are `bool`, fixed-width signed/unsigned
integers, `f32`/`f64`, bounded `string`/`bytes` fields, non-negative enum
references, bounded arrays of those scalar/enum/string/bytes/record element
kinds, or a record reference (embedded by value, no pointer or heap
allocation). Compiler-resolved cross-namespace enum/record references use
imported generated headers and remain separate explicit generation roots,
with real BRF encode/decode backed by the installed `Quarry::runtime_c` C
runtime and verified byte-for-byte wire-compatible with the C++ backend,
including identical unknown-enum-value rejection (plain field or array
element), identical string/bytes/array-field bounds-violation rejection
(and, for string, identical UTF-8-validation rejection -- bytes and array
fields never validate UTF-8, matching the BRF spec), and identical
rejection of a malformed or wrong-record-id nested or record-array-element
payload. String fields use fixed-capacity, NUL-terminated buffer storage
sized from the schema's `max_bytes` bound; bytes fields use the same
fixed-capacity strategy without the NUL terminator (arbitrary binary data
has no such convenience); array fields use a fixed-capacity array of the
element's own C type plus an explicit element count (record elements
compose that element record's own encode/decode functions directly, with
no new runtime code); nested record fields embed the referenced record's
own generated struct directly by value -- no heap allocation anywhere. See
`compiler/backend_c/README.md`, `runtime_c/README.md`, and
`docs/design/c-backend.md` for the exact
supported subset, the generated API, and the roadmap for the rest.

Python is the third backend language (`--language python`). PR-118
established the architecture (independent backend, true nested Python
packages -- a real directory plus `__init__.py` per namespace segment,
`@dataclass` records, methods delegating to internal helper functions).
PR-119 made it functional for scalar fields: `bool` and every fixed-width
signed/unsigned integer and `f32`/`f64` field, encoded/decoded via a new
`quarry.runtime.python.binary_record` runtime module built entirely on the
standard library's `struct` module. PR-120 added enum fields: a
same-namespace, non-negative-valued enum renders as a real `enum.IntEnum`
subclass, with two small `binary_record.py` additions
(`pack_enum`/`unpack_enum`) validating membership and delegating to the
existing scalar pack/unpack for the enum's wire width. PR-121 added
bounded `string`/`bytes` fields, using the BRF spec's existing
variable-length encoding rules unchanged and deliberately reusing Python's
own `str.encode`/`bytes.decode("utf-8")` for UTF-8 validation rather than
hand-rolling a validator. All are verified byte-for-byte wire-compatible
with the C and C++ backends for the same field values. Bounded arrays of
fixed-width scalar and non-negative-valued enum elements, plus
bounded arrays of string and bytes elements, are supported using the BRF
count-prefix encoding. Nested record fields and arrays of records, including
compiler-resolved cross-namespace enum/record references, are supported by
composing the existing generated record helpers and BRF array framing.
Imported schemas must be generated as separate explicit roots into the same
output tree. Nested arrays and recursive by-value record graphs remain
unsupported and fail generation with a diagnostic naming the record and field.
Generated modules check a
Python generated-code API compatibility epoch
(`QUARRY_GENERATED_CODE_API_VERSION_PYTHON`) against the small pip-installed
`runtime/python/` package at import time. See `docs/design/python-backend.md`
for the full architecture, scope, and roadmap.

The Python runtime is distributed as a standard pure-Python package from
`runtime/python/`. Build local wheel and sdist artifacts with
`python -m build --wheel --sdist`, install a wheel into a virtual environment,
and make generated output importable alongside it. The runtime is not yet
published to PyPI; generated modules check the installed runtime epoch at
import time and raise `ImportError` on mismatch.

The supported downstream distribution model is defined in
`docs/distribution-model.md`. The installed SDK currently consists of the
header-only runtime package plus the `Quarry::schema_compiler` executable
target. Compiler libraries, generated protobufs, tests, and fuzzers remain
source-tree artifacts.
