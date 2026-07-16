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
currently own their own `add_custom_command()` wiring, expected generated
output lists, generated include directories, target source attachment, and
stale-output cleanup. The canonical pattern is shown in
`examples/cpp/schema_compiler_cmake`.

Minimal installed-package examples are available in `examples/cpp/`.

The supported downstream distribution model is defined in
`docs/distribution-model.md`. The installed SDK currently consists of the
header-only runtime package plus the `Breadcrumbs::schema_compiler` executable
target. Compiler libraries, generated protobufs, tests, and fuzzers remain
source-tree artifacts. No generated-code helper function is provided yet.
