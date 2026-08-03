cmake_minimum_required(VERSION 3.20)

project(quarry_basic_encode_decode_c LANGUAGES C)

find_package(Quarry CONFIG REQUIRED)

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(generated_header "${generated_dir}/demo/c.generated.h")
set(generated_source "${generated_dir}/demo/c.generated.c")

add_custom_command(
    OUTPUT "${generated_header}" "${generated_source}"
    COMMAND "$<TARGET_FILE:Quarry::schema_compiler>"
            --language c
            --output-directory "${generated_dir}"
            "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd" Quarry::schema_compiler
    VERBATIM
)

add_executable(quarry_basic_encode_decode_c main.c "${generated_header}" "${generated_source}")
target_include_directories(quarry_basic_encode_decode_c PRIVATE "${generated_dir}")
target_link_libraries(quarry_basic_encode_decode_c PRIVATE Quarry::runtime_c)
set_target_properties(quarry_basic_encode_decode_c PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
