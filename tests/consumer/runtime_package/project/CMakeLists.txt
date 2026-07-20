cmake_minimum_required(VERSION 3.20)

project(quarry_runtime_package_consumer LANGUAGES CXX)

find_package(Quarry CONFIG REQUIRED)

if(NOT DEFINED Quarry_GENERATED_CODE_API_VERSION)
    message(FATAL_ERROR "Quarry_GENERATED_CODE_API_VERSION is not defined")
endif()
if(NOT Quarry_GENERATED_CODE_API_VERSION MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Quarry_GENERATED_CODE_API_VERSION is not numeric")
endif()

add_executable(runtime_package_consumer main.cpp)
target_compile_definitions(runtime_package_consumer
    PRIVATE
        QUARRY_PACKAGE_GENERATED_CODE_API_VERSION=${Quarry_GENERATED_CODE_API_VERSION}
)
target_link_libraries(runtime_package_consumer PRIVATE Quarry::runtime)
