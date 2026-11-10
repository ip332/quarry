cmake_minimum_required(VERSION 3.20)
project(quarry_cpp_c_storage_example LANGUAGES CXX)
find_package(Quarry CONFIG REQUIRED)
option(QUARRY_EXAMPLE_COMPACT_PRESENCE "Pack native presence bits" OFF)
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(schema "${CMAKE_CURRENT_SOURCE_DIR}/../../c/basic_encode_decode/schema.brd")
set(extra_options)
if(QUARRY_EXAMPLE_COMPACT_PRESENCE)
    list(APPEND extra_options --c-optimize-size)
endif()
add_custom_command(
    OUTPUT "${generated_dir}/demo/c.generated.hpp" "${generated_dir}/demo/c.generated.cpp"
    COMMAND "$<TARGET_FILE:Quarry::schema_compiler>" --language cpp --cpp-storage c
            ${extra_options} --output-directory "${generated_dir}" "${schema}"
    DEPENDS "${schema}" Quarry::schema_compiler
    VERBATIM)
add_executable(quarry_cpp_c_storage_example main.cpp "${generated_dir}/demo/c.generated.hpp")
target_include_directories(quarry_cpp_c_storage_example PRIVATE "${generated_dir}")
target_link_libraries(quarry_cpp_c_storage_example PRIVATE Quarry::runtime_c)
target_compile_features(quarry_cpp_c_storage_example PRIVATE cxx_std_20)
set_target_properties(quarry_cpp_c_storage_example PROPERTIES CXX_EXTENSIONS OFF)
