cmake_minimum_required(VERSION 3.20)

project(quarry_schema_compiler_cmake LANGUAGES CXX)

find_package(Quarry CONFIG REQUIRED)

if(NOT DEFINED Quarry_GENERATED_CODE_API_VERSION)
    message(FATAL_ERROR "Quarry_GENERATED_CODE_API_VERSION is not defined")
endif()

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

quarry_generate_cpp(
    SCHEMA schema.brd
    OUTPUT_DIR "${generated_dir}"
    OUT_FILES generated_files
)

quarry_generate_cpp(
    SCHEMA shared.brd
    OUTPUT_DIR "${generated_dir}"
    OUT_FILES dependency_files
)

add_executable(quarry_schema_compiler_cmake
    main.cpp
)
target_sources(quarry_schema_compiler_cmake PRIVATE ${generated_files})
target_sources(quarry_schema_compiler_cmake PRIVATE ${dependency_files})
target_compile_definitions(quarry_schema_compiler_cmake
    PRIVATE
        QUARRY_PACKAGE_GENERATED_CODE_API_VERSION=${Quarry_GENERATED_CODE_API_VERSION}
)
target_include_directories(quarry_schema_compiler_cmake PRIVATE "${generated_dir}")
target_link_libraries(quarry_schema_compiler_cmake PRIVATE Quarry::runtime)
