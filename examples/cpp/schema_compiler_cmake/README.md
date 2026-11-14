# Schema Compiler CMake

This example is the recommended installed-native CMake integration pattern for
generating C++ with the installed Quarry schema compiler helper. It also shows
root-based generation of a cross-namespace record: one compilation root, two
generated namespaces, one record-ID mapping.

Build after installing Quarry to a prefix:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/install/prefix
cmake --build build
./build/quarry_schema_compiler_cmake
```

Expected output:

```text
decoded count: 42
```

The helper is invoked once, on the compilation root `schema.brd`. The compiler
always resolves the root's complete import closure (here `shared.brd`), which
determines every record ID; `GENERATE` selects the namespaces to emit. The
project still owns target creation, include directories, and runtime linkage:

```cmake
find_package(Quarry CONFIG REQUIRED)

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

quarry_generate_cpp(
    SCHEMA schema.brd
    GENERATE "quarry.telemetry,quarry.shared"
    OUTPUT_DIR "${generated_dir}"
    OUT_FILES generated_files
)

add_executable(quarry_schema_compiler_cmake
    main.cpp
)
target_sources(quarry_schema_compiler_cmake PRIVATE ${generated_files})
target_include_directories(quarry_schema_compiler_cmake PRIVATE "${generated_dir}")
target_link_libraries(quarry_schema_compiler_cmake PRIVATE Quarry::runtime)
```

`quarry_generate_cpp()` supports installed package consumers. It handles one
compilation root per invocation, returns absolute generated file paths, and
does not create or mutate targets. Without `GENERATE`, a record root emits only
its own namespace and a configuration root (`configuration: <name>`) emits
every namespace. Separate invocations may emit different namespaces of the same
root; generating an imported schema as its own root defines a different
configuration with different record IDs. Native builds use
`Quarry::schema_compiler` by default. Cross-compiling builds must pass
`SCHEMA_COMPILER` with an absolute path to a compiler executable runnable on
the build host, not the target. Downstream projects still own the generated
include directory, target source attachment, runtime linkage, and stale-output
cleanup. The compiler does not currently emit depfiles or manifests.

The helper verifies the generated-output inventory at build time before normal
generation. If the current `--list-outputs` result differs from the configured
`OUT_FILES` list, the build fails before writing generated files and the project
must be reconfigured.

`find_package(Quarry CONFIG REQUIRED)` also provides
`Quarry_GENERATED_CODE_API_VERSION`, matching the runtime header's
`quarry::runtime::kGeneratedCodeApiVersion`.

The lower-level manual integration pattern remains supported when callers need
full control over the custom command:

```cmake
add_custom_command(
    OUTPUT "${generated_dir}/quarry/telemetry.generated.hpp"
    COMMAND
        "$<TARGET_FILE:Quarry::schema_compiler>"
        --output-directory "${generated_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
        Quarry::schema_compiler
    VERBATIM
)
```

Generated C++ is supported with `Quarry::runtime` from the same
Quarry release as the schema compiler that generated it. Generated headers
also check `quarry::runtime::kGeneratedCodeApiVersion` at compile time;
that guard covers generated-code/runtime API compatibility, not exact package
release equality or BRF wire compatibility.

The example intentionally keeps the consumer small: it checks the decoded
scalar and imported nested record after the round trip. Structured decode
diagnostics are covered by the runtime documentation and automated tests.
