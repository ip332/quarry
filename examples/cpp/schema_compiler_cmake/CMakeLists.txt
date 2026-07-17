cmake_minimum_required(VERSION 3.20)

project(breadcrumbs_schema_compiler_cmake LANGUAGES CXX)

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
