cmake_minimum_required(VERSION 3.20)

project(quarry VERSION 0.1.0 LANGUAGES CXX C)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(QUARRY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(QUARRY_ENABLE_CLANG_TIDY "Run clang-tidy during CMake builds" OFF)
option(QUARRY_BUILD_FUZZERS "Build libFuzzer targets for BRF parser hardening" OFF)
option(QUARRY_ENABLE_COVERAGE "Enable native coverage instrumentation" OFF)
option(QUARRY_BUILD_BENCHMARKS "Build the standalone benchmark proof harnesses" OFF)

# Compatibility epoch for generated C++ source and Quarry runtime headers.
# This is distinct from the package release version and BRF wire-format version.
set(QUARRY_GENERATED_CODE_API_VERSION 3)
if(NOT QUARRY_GENERATED_CODE_API_VERSION MATCHES "^[0-9]+$")
    message(FATAL_ERROR "QUARRY_GENERATED_CODE_API_VERSION must be a non-negative integer")
endif()
if(QUARRY_GENERATED_CODE_API_VERSION GREATER 4294967295)
    message(FATAL_ERROR "QUARRY_GENERATED_CODE_API_VERSION must fit in std::uint32_t")
endif()

# Compatibility epoch for generated C source and the Quarry C runtime header.
# Independent from QUARRY_GENERATED_CODE_API_VERSION (C++): the C and C++
# generator/runtime contracts may change on independent schedules (PR-108;
# see docs/design/c-backend.md). Bumped 1 -> 2 in PR-110: bounded string
# field support added new public runtime symbols (quarry_c_is_valid_utf8,
# quarry_c_copy_bounded, QUARRY_C_STATUS_BOUNDS_EXCEEDED,
# QUARRY_C_STATUS_INVALID_UTF8) that generated string-field code now calls;
# pairing new generated code with a pre-PR-110 runtime header would fail to
# compile with a confusing "undefined identifier" error instead of this
# epoch guard's clear, actionable #error.
set(QUARRY_GENERATED_CODE_API_VERSION_C 2)
if(NOT QUARRY_GENERATED_CODE_API_VERSION_C MATCHES "^[0-9]+$")
    message(FATAL_ERROR "QUARRY_GENERATED_CODE_API_VERSION_C must be a non-negative integer")
endif()
if(QUARRY_GENERATED_CODE_API_VERSION_C GREATER 4294967295)
    message(FATAL_ERROR "QUARRY_GENERATED_CODE_API_VERSION_C must fit in uint32_t")
endif()

set(QUARRY_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(
    include/quarry/runtime/version.hpp.in
    ${QUARRY_GENERATED_INCLUDE_DIR}/quarry/runtime/version.hpp
    @ONLY
)
configure_file(
    include/quarry/runtime_c/version.h.in
    ${QUARRY_GENERATED_INCLUDE_DIR}/quarry/runtime_c/version.h
    @ONLY
)
configure_file(
    compiler/backend/generated_code_api_version.hpp.in
    ${QUARRY_GENERATED_INCLUDE_DIR}/compiler/backend/generated_code_api_version.hpp
    @ONLY
)
configure_file(
    compiler/backend_c/generated_code_api_version_c.hpp.in
    ${QUARRY_GENERATED_INCLUDE_DIR}/compiler/backend_c/generated_code_api_version_c.hpp
    @ONLY
)

add_library(quarry_project_options INTERFACE)
target_compile_features(quarry_project_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(quarry_project_options INTERFACE /W4)
    if(QUARRY_WARNINGS_AS_ERRORS)
        target_compile_options(quarry_project_options INTERFACE /WX)
    endif()
else()
    target_compile_options(quarry_project_options INTERFACE -Wall -Wextra -Wpedantic)
    if(QUARRY_WARNINGS_AS_ERRORS)
        target_compile_options(quarry_project_options INTERFACE -Werror)
    endif()
endif()

if(QUARRY_ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang" OR
       NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "QUARRY_ENABLE_COVERAGE requires GCC or Clang-compatible compilers")
    endif()
    target_compile_options(quarry_project_options INTERFACE --coverage -O0)
    target_link_options(quarry_project_options INTERFACE --coverage)
endif()

if(QUARRY_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR "QUARRY_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
    endif()
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

enable_testing()

add_subdirectory(runtime)
add_subdirectory(runtime_c)
if(QUARRY_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
add_subdirectory(compiler)
add_subdirectory(tools)
if(QUARRY_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
add_subdirectory(tests)

install(TARGETS quarry_runtime
    EXPORT QuarryTargets
)
install(TARGETS quarry_runtime_c
    EXPORT QuarryTargets
)
install(FILES
    ${QUARRY_GENERATED_INCLUDE_DIR}/quarry/runtime/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quarry/runtime
)
install(FILES
    ${QUARRY_GENERATED_INCLUDE_DIR}/quarry/runtime_c/version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quarry/runtime_c
)
install(DIRECTORY include/quarry
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "*.in" EXCLUDE
)
install(EXPORT QuarryTargets
    FILE QuarryTargets.cmake
    NAMESPACE Quarry::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Quarry
)

configure_package_config_file(
    cmake/QuarryConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/QuarryConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Quarry
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/QuarryConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/QuarryConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/QuarryConfigVersion.cmake
    cmake/QuarryGenerate.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Quarry
)
