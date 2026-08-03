cmake_minimum_required(VERSION 3.20)

project(quarry_cpp_python_interop LANGUAGES CXX)

find_package(Quarry CONFIG REQUIRED)

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
quarry_generate_cpp(
    SCHEMA schema.brd
    OUTPUT_DIR "${generated_dir}"
    OUT_FILES generated_files
)

add_executable(quarry_cpp_python_encode main.cpp ${generated_files})
target_compile_definitions(quarry_cpp_python_encode
    PRIVATE QUARRY_PACKAGE_GENERATED_CODE_API_VERSION=${Quarry_GENERATED_CODE_API_VERSION})
target_include_directories(quarry_cpp_python_encode PRIVATE "${generated_dir}")
target_link_libraries(quarry_cpp_python_encode PRIVATE Quarry::runtime)
