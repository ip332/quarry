# Schema Compiler CMake

This example is the recommended installed-native CMake integration pattern for
generating C++ with the installed Breadcrumbs schema compiler helper.

Build after installing Breadcrumbs to a prefix:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/install/prefix
cmake --build build
./build/breadcrumbs_schema_compiler_cmake
```

The helper discovers generated outputs during configuration and returns them to
the caller. The project still owns target creation, include directories, and
runtime linkage:

```cmake
find_package(Breadcrumbs CONFIG REQUIRED)

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

breadcrumbs_generate_cpp(
    SCHEMA schema.brd
    OUTPUT_DIR "${generated_dir}"
    OUT_FILES generated_files
)

add_executable(breadcrumbs_schema_compiler_cmake
    main.cpp
)
target_sources(breadcrumbs_schema_compiler_cmake PRIVATE ${generated_files})
target_include_directories(breadcrumbs_schema_compiler_cmake PRIVATE "${generated_dir}")
target_link_libraries(breadcrumbs_schema_compiler_cmake PRIVATE Breadcrumbs::runtime)
```

`breadcrumbs_generate_cpp()` supports installed package consumers. It handles
one schema input per invocation, returns absolute generated file paths, and
does not create or mutate targets. Native builds use
`Breadcrumbs::schema_compiler` by default. Cross-compiling builds must pass
`SCHEMA_COMPILER` with an absolute path to a compiler executable runnable on
the build host, not the target. Downstream projects still own the generated
include directory, target source attachment, runtime linkage, and stale-output
cleanup. The compiler does not currently emit depfiles or manifests.

The helper verifies the generated-output inventory at build time before normal
generation. If the current `--list-outputs` result differs from the configured
`OUT_FILES` list, the build fails before writing generated files and the project
must be reconfigured.

`find_package(Breadcrumbs CONFIG REQUIRED)` also provides
`Breadcrumbs_GENERATED_CODE_API_VERSION`, matching the runtime header's
`breadcrumbs::runtime::kGeneratedCodeApiVersion`.

The lower-level manual integration pattern remains supported when callers need
full control over the custom command:

```cmake
add_custom_command(
    OUTPUT "${generated_dir}/breadcrumbs/telemetry.generated.hpp"
    COMMAND
        "$<TARGET_FILE:Breadcrumbs::schema_compiler>"
        --output-directory "${generated_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
        Breadcrumbs::schema_compiler
    VERBATIM
)
```

Generated C++ is supported with `Breadcrumbs::runtime` from the same
Breadcrumbs release as the schema compiler that generated it. Generated headers
also check `breadcrumbs::runtime::kGeneratedCodeApiVersion` at compile time;
that guard covers generated-code/runtime API compatibility, not exact package
release equality or BRF wire compatibility.
