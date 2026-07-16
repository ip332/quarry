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
currently own their own `add_custom_command()` wiring and expected generated
output lists.

A minimal installed-package example is available in
`examples/cpp/basic_encode_decode`.

The supported downstream distribution model is defined in
`docs/distribution-model.md`. The installed SDK currently consists of the
header-only runtime package plus the `Breadcrumbs::schema_compiler` executable
target. Compiler libraries, generated protobufs, tests, and fuzzers remain
source-tree artifacts. No generated-code helper function is provided yet.
