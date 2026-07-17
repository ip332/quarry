# Breadcrumbs

Breadcrumbs is an open-source, schema-driven platform for secure edge-to-cloud
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

## Runtime Package

The C++ Breadcrumbs runtime is header-only and installable as a CMake package.
After installing the project, downstream CMake projects can consume it with:

```cmake
find_package(Breadcrumbs CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Breadcrumbs::runtime)
```

The public runtime include path is:

```cpp
#include <breadcrumbs/runtime/binary_record.hpp>
```

The installed package also exposes the schema compiler as an executable CMake
target:

```cmake
$<TARGET_FILE:Breadcrumbs::schema_compiler>
```

Generated code should link against `Breadcrumbs::runtime`. Downstream projects
can use the installed-native helper:

```cmake
breadcrumbs_generate_cpp(
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

The schema compiler also supports `--list-outputs` to print the generated paths
for a schema and generation options without writing files. This query is backed
by the backend's internal generation plan, but it is not a CMake helper,
manifest, depfile, or stale-output cleanup mechanism.

Minimal installed-package examples are available in `examples/cpp/`.

The supported downstream distribution model is defined in
`docs/distribution-model.md`. The installed SDK currently consists of the
header-only runtime package plus the `Breadcrumbs::schema_compiler` executable
target. Compiler libraries, generated protobufs, tests, and fuzzers remain
source-tree artifacts.
