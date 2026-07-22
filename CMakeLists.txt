cmake_minimum_required(VERSION 3.20)

project(quarry VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QUARRY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(QUARRY_ENABLE_CLANG_TIDY "Run clang-tidy during CMake builds" OFF)
option(QUARRY_BUILD_FUZZERS "Build libFuzzer targets for BRF parser hardening" OFF)

# Compatibility epoch for generated C++ source and Quarry runtime headers.
# This is distinct from the package release version and BRF wire-format version.
set(QUARRY_GENERATED_CODE_API_VERSION 2)
if(NOT QUARRY_GENERATED_CODE_API_VERSION MATCHES "^[0-9]+$")
    message(FATAL_ERROR "QUARRY_GENERATED_CODE_API_VERSION must be a non-negative integer")
endif()
if(QUARRY_GENERATED_CODE_API_VERSION GREATER 4294967295)
    message(FATAL_ERROR "QUARRY_GENERATED_CODE_API_VERSION must fit in std::uint32_t")
endif()

set(QUARRY_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(
    runtime/version.hpp.in
    ${QUARRY_GENERATED_INCLUDE_DIR}/runtime/version.hpp
    @ONLY
)
configure_file(
    compiler/backend/generated_code_api_version.hpp.in
    ${QUARRY_GENERATED_INCLUDE_DIR}/compiler/backend/generated_code_api_version.hpp
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

if(QUARRY_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR "QUARRY_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
    endif()
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

enable_testing()

add_subdirectory(runtime)
if(QUARRY_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
add_subdirectory(compiler)
add_subdirectory(tools)
add_subdirectory(tests)

install(TARGETS quarry_runtime
    EXPORT QuarryTargets
)
install(FILES
    runtime/binary_record.hpp
    ${QUARRY_GENERATED_INCLUDE_DIR}/runtime/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/runtime
)
install(DIRECTORY include/quarry
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
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
