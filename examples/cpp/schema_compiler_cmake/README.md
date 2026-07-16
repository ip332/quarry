# Schema Compiler CMake

This example is the supported downstream CMake integration pattern for
generating C++ with the installed Breadcrumbs schema compiler.

Build after installing Breadcrumbs to a prefix:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/install/prefix
cmake --build build
./build/breadcrumbs_schema_compiler_cmake
```

The project intentionally uses only standard CMake primitives:

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

add_executable(breadcrumbs_schema_compiler_cmake
    main.cpp
    "${generated_header}"
)
target_include_directories(breadcrumbs_schema_compiler_cmake PRIVATE "${generated_dir}")
target_link_libraries(breadcrumbs_schema_compiler_cmake PRIVATE Breadcrumbs::runtime)
```

Downstream projects currently own the generated output list, generated include
directory, target source attachment, schema dependency declaration, and stale
output cleanup. The compiler handles one schema input per invocation and does
not currently emit depfiles or manifests.

Generated C++ is supported with `Breadcrumbs::runtime` from the same
Breadcrumbs release as the schema compiler that generated it. Generated headers
also check `breadcrumbs::runtime::kGeneratedCodeApiVersion` at compile time;
that guard covers generated-code/runtime API compatibility, not exact package
release equality or BRF wire compatibility.
