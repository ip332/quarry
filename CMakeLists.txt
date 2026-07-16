cmake_minimum_required(VERSION 3.20)

project(breadcrumbs VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BREADCRUMBS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(BREADCRUMBS_ENABLE_CLANG_TIDY "Run clang-tidy during CMake builds" OFF)
option(BREADCRUMBS_BUILD_FUZZERS "Build libFuzzer targets for BRF parser hardening" OFF)

add_library(breadcrumbs_project_options INTERFACE)
target_compile_features(breadcrumbs_project_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(breadcrumbs_project_options INTERFACE /W4)
    if(BREADCRUMBS_WARNINGS_AS_ERRORS)
        target_compile_options(breadcrumbs_project_options INTERFACE /WX)
    endif()
else()
    target_compile_options(breadcrumbs_project_options INTERFACE -Wall -Wextra -Wpedantic)
    if(BREADCRUMBS_WARNINGS_AS_ERRORS)
        target_compile_options(breadcrumbs_project_options INTERFACE -Werror)
    endif()
endif()

if(BREADCRUMBS_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR "BREADCRUMBS_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
    endif()
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

enable_testing()

add_subdirectory(runtime)
if(BREADCRUMBS_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
add_subdirectory(compiler)
add_subdirectory(tools)
add_subdirectory(tests)

install(TARGETS breadcrumbs_runtime
    EXPORT BreadcrumbsTargets
)
install(FILES runtime/binary_record.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/runtime
)
install(DIRECTORY include/breadcrumbs
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT BreadcrumbsTargets
    FILE BreadcrumbsTargets.cmake
    NAMESPACE Breadcrumbs::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Breadcrumbs
)

configure_package_config_file(
    cmake/BreadcrumbsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/BreadcrumbsConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Breadcrumbs
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/BreadcrumbsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/BreadcrumbsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/BreadcrumbsConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Breadcrumbs
)
