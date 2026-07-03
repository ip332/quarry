cmake_minimum_required(VERSION 3.20)

project(breadcrumbs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BREADCRUMBS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(BREADCRUMBS_ENABLE_CLANG_TIDY "Run clang-tidy during CMake builds" OFF)

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

add_subdirectory(compiler)
add_subdirectory(tests)
