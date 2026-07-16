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

A minimal installed-package example is available in
`examples/cpp/basic_encode_decode`.
