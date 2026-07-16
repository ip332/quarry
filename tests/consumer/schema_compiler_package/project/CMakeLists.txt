cmake_minimum_required(VERSION 3.20)

project(breadcrumbs_schema_compiler_package_consumer LANGUAGES CXX)

find_package(Breadcrumbs CONFIG REQUIRED)

if(NOT TARGET Breadcrumbs::runtime)
    message(FATAL_ERROR "Breadcrumbs::runtime target is missing")
endif()

if(NOT TARGET Breadcrumbs::schema_compiler)
    message(FATAL_ERROR "Breadcrumbs::schema_compiler target is missing")
endif()

get_target_property(BREADCRUMBS_SCHEMA_COMPILER_LOCATION
                    Breadcrumbs::schema_compiler IMPORTED_LOCATION)
get_target_property(BREADCRUMBS_SCHEMA_COMPILER_LOCATION_DEBUG
                    Breadcrumbs::schema_compiler IMPORTED_LOCATION_DEBUG)
set(BREADCRUMBS_SCHEMA_COMPILER_SELECTED_LOCATION "${BREADCRUMBS_SCHEMA_COMPILER_LOCATION}")
if(BREADCRUMBS_SCHEMA_COMPILER_LOCATION_DEBUG)
    set(BREADCRUMBS_SCHEMA_COMPILER_SELECTED_LOCATION
        "${BREADCRUMBS_SCHEMA_COMPILER_LOCATION_DEBUG}")
endif()
if(NOT BREADCRUMBS_SCHEMA_COMPILER_SELECTED_LOCATION MATCHES
   "^${EXPECTED_BREADCRUMBS_PREFIX}/")
    message(FATAL_ERROR
            "Breadcrumbs::schema_compiler does not resolve inside the selected prefix: "
            "${BREADCRUMBS_SCHEMA_COMPILER_SELECTED_LOCATION}")
endif()

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(generated_header "${generated_dir}/breadcrumbs/telemetry.generated.hpp")

add_custom_command(
    OUTPUT "${generated_header}"
    COMMAND
        "$<TARGET_FILE:Breadcrumbs::schema_compiler>"
        --output-directory "${generated_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.brd"
        Breadcrumbs::schema_compiler
    VERBATIM
)

add_executable(schema_compiler_package_consumer
    main.cpp
    "${generated_header}"
)
target_include_directories(schema_compiler_package_consumer PRIVATE "${generated_dir}")
target_link_libraries(schema_compiler_package_consumer PRIVATE Breadcrumbs::runtime)

