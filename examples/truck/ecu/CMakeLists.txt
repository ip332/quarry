cmake_minimum_required(VERSION 3.20)

project(quarry_truck_engine_ecu LANGUAGES C)

find_package(Quarry CONFIG REQUIRED)

# The engine ECU firmware needs only its own namespace and the shared
# timestamp. Both are generated from the truck's configuration root, so their
# record IDs are the configuration's, the same IDs the cloud decodes.
set(schemas "${CMAKE_CURRENT_SOURCE_DIR}/../schemas")
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(generated_files
    "${generated_dir}/truck/engine.generated.h"
    "${generated_dir}/truck/engine.generated.c"
    "${generated_dir}/truck/common.generated.h"
    "${generated_dir}/truck/common.generated.c")
file(GLOB schema_files CONFIGURE_DEPENDS "${schemas}/*.brd")

add_custom_command(
    OUTPUT ${generated_files}
    COMMAND "$<TARGET_FILE:Quarry::schema_compiler>"
            --language c
            --generate truck.engine,truck.common
            --output-directory "${generated_dir}"
            "${schemas}/configuration.brd"
    DEPENDS ${schema_files} Quarry::schema_compiler
    VERBATIM
)

add_executable(quarry_truck_engine_ecu main.c ${generated_files})
target_include_directories(quarry_truck_engine_ecu PRIVATE "${generated_dir}")
target_link_libraries(quarry_truck_engine_ecu PRIVATE Quarry::runtime_c)
set_target_properties(quarry_truck_engine_ecu PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
