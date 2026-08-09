# Standalone Cortex-M cross-compilation validation project.
#
# This is deliberately NOT add_subdirectory()'d from the repository's root
# CMakeLists.txt: the root project builds host tooling (protobuf/absl/yaml,
# gtest-based tests) that has no business being cross-compiled for a
# bare-metal target, and CMakePresets.json presets are bound to the
# CMakeLists.txt in the same directory, so a second, independent target
# needs its own project. See README.md for the full two-stage
# host-generate / target-cross-compile flow and how to run it locally; the
# same flow is what CI runs (.github/workflows/ci.yml, "Cortex-M Cross
# Build" job).
#
# Scope: this project proves Quarry's generated C code and strict-C99 C
# runtime cross-compile and link for a Cortex-M4 target. It does not run
# anything on real or emulated hardware, and it is not a board support
# package -- no HAL, no RTOS, no flashing.

cmake_minimum_required(VERSION 3.20)
project(quarry_cortex_m_validation LANGUAGES C)

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
        "cortex_m/ must be configured with "
        "-DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/toolchains/arm-cortex-m4.cmake "
        "(or another cross-compiling toolchain file). Configuring it as a "
        "native build makes no sense: it links a bare-metal ELF with no "
        "host operating system underneath it. See cortex_m/README.md.")
endif()

# Three explicit artifacts, matching docs/schema-compiler-tool-distribution.md's
# "Host Tool and Cross-Compilation Policy": a host-built schema compiler, a
# target runtime, and generated code. This project never builds or
# auto-discovers any of them itself -- the caller (a developer, or CI) must
# supply all three explicitly. This mirrors quarry_generate_cpp()'s
# fail-fast-if-CMAKE_CROSSCOMPILING-and-no-SCHEMA_COMPILER behavior
# (cmake/QuarryGenerate.cmake) for the (helper-less) C path.
if(NOT DEFINED QUARRY_HOST_SCHEMA_COMPILER)
    message(FATAL_ERROR
        "QUARRY_HOST_SCHEMA_COMPILER must be set to an absolute path to a "
        "host-built (native, host-runnable) quarry-schema-compiler "
        "executable. This project never builds or discovers one itself -- "
        "see cortex_m/README.md.")
endif()
if(NOT EXISTS "${QUARRY_HOST_SCHEMA_COMPILER}" OR IS_DIRECTORY "${QUARRY_HOST_SCHEMA_COMPILER}")
    message(FATAL_ERROR
        "QUARRY_HOST_SCHEMA_COMPILER (${QUARRY_HOST_SCHEMA_COMPILER}) does "
        "not name an existing file.")
endif()

if(NOT DEFINED QUARRY_RUNTIME_C_INCLUDE_DIR)
    message(FATAL_ERROR
        "QUARRY_RUNTIME_C_INCLUDE_DIR must point at the repository's "
        "include/ directory (containing quarry/runtime_c/binary_record.h). "
        "See cortex_m/README.md.")
endif()
if(NOT IS_DIRECTORY "${QUARRY_RUNTIME_C_INCLUDE_DIR}")
    message(FATAL_ERROR
        "QUARRY_RUNTIME_C_INCLUDE_DIR (${QUARRY_RUNTIME_C_INCLUDE_DIR}) is "
        "not a directory.")
endif()

if(NOT DEFINED QUARRY_RUNTIME_C_VERSION_HEADER_DIR)
    message(FATAL_ERROR
        "QUARRY_RUNTIME_C_VERSION_HEADER_DIR must point at a host build "
        "directory's generated/ directory (containing "
        "quarry/runtime_c/version.h, produced by configuring the root "
        "project). See cortex_m/README.md.")
endif()
if(NOT IS_DIRECTORY "${QUARRY_RUNTIME_C_VERSION_HEADER_DIR}")
    message(FATAL_ERROR
        "QUARRY_RUNTIME_C_VERSION_HEADER_DIR "
        "(${QUARRY_RUNTIME_C_VERSION_HEADER_DIR}) is not a directory.")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Reuse the existing representative benchmark schemas (records, a local
# enum, bounded string/bytes, a bounded scalar array, a cross-namespace
# nested record, and a cross-namespace array of records) instead of
# inventing a new one, and reference them in place rather than copying them
# so this project can never drift out of sync with the schemas it claims to
# validate against.
set(QUARRY_SCHEMAS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../benchmarks/schemas")
set(QUARRY_SHARED_SCHEMA "${QUARRY_SCHEMAS_DIR}/workload_shared.brd")
set(QUARRY_WORKLOAD_SCHEMA "${QUARRY_SCHEMAS_DIR}/workload.brd")

set(QUARRY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(QUARRY_GENERATED_SHARED_HEADER
    "${QUARRY_GENERATED_DIR}/benchmark/workload/shared.generated.h")
set(QUARRY_GENERATED_SHARED_SOURCE
    "${QUARRY_GENERATED_DIR}/benchmark/workload/shared.generated.c")
set(QUARRY_GENERATED_WORKLOAD_HEADER
    "${QUARRY_GENERATED_DIR}/benchmark/workload.generated.h")
set(QUARRY_GENERATED_WORKLOAD_SOURCE
    "${QUARRY_GENERATED_DIR}/benchmark/workload.generated.c")

add_custom_command(
    OUTPUT "${QUARRY_GENERATED_SHARED_HEADER}" "${QUARRY_GENERATED_SHARED_SOURCE}"
    COMMAND "${QUARRY_HOST_SCHEMA_COMPILER}" --language c
            --output-directory "${QUARRY_GENERATED_DIR}"
            "${QUARRY_SHARED_SCHEMA}"
    DEPENDS "${QUARRY_SHARED_SCHEMA}"
    COMMENT "Generating C code for benchmark.workload.shared (host quarry-schema-compiler)"
    VERBATIM
)
add_custom_command(
    OUTPUT "${QUARRY_GENERATED_WORKLOAD_HEADER}" "${QUARRY_GENERATED_WORKLOAD_SOURCE}"
    COMMAND "${QUARRY_HOST_SCHEMA_COMPILER}" --language c
            --output-directory "${QUARRY_GENERATED_DIR}"
            "${QUARRY_WORKLOAD_SCHEMA}"
    DEPENDS "${QUARRY_WORKLOAD_SCHEMA}"
            "${QUARRY_GENERATED_SHARED_HEADER}" "${QUARRY_GENERATED_SHARED_SOURCE}"
    COMMENT "Generating C code for benchmark.workload (host quarry-schema-compiler)"
    VERBATIM
)

add_executable(quarry_cortex_m_smoke
    startup_cortex_m4.c
    smoke/workload_smoke.c
    "${QUARRY_GENERATED_SHARED_SOURCE}"
    "${QUARRY_GENERATED_WORKLOAD_SOURCE}"
)
target_include_directories(quarry_cortex_m_smoke PRIVATE
    "${QUARRY_GENERATED_DIR}"
    "${QUARRY_RUNTIME_C_INCLUDE_DIR}"
    "${QUARRY_RUNTIME_C_VERSION_HEADER_DIR}"
)
target_compile_options(quarry_cortex_m_smoke PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -ffunction-sections -fdata-sections
)
target_link_options(quarry_cortex_m_smoke PRIVATE
    -nostartfiles
    -T "${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32f446_illustrative.ld"
    -Wl,--gc-sections
    -Wl,-Map=$<TARGET_FILE:quarry_cortex_m_smoke>.map
)
set_target_properties(quarry_cortex_m_smoke PROPERTIES
    SUFFIX ".elf"
)
