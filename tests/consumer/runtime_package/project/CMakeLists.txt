cmake_minimum_required(VERSION 3.20)

project(breadcrumbs_runtime_package_consumer LANGUAGES CXX)

find_package(Breadcrumbs CONFIG REQUIRED)

if(NOT DEFINED Breadcrumbs_GENERATED_CODE_API_VERSION)
    message(FATAL_ERROR "Breadcrumbs_GENERATED_CODE_API_VERSION is not defined")
endif()
if(NOT Breadcrumbs_GENERATED_CODE_API_VERSION MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Breadcrumbs_GENERATED_CODE_API_VERSION is not numeric")
endif()

add_executable(runtime_package_consumer main.cpp)
target_compile_definitions(runtime_package_consumer
    PRIVATE
        BREADCRUMBS_PACKAGE_GENERATED_CODE_API_VERSION=${Breadcrumbs_GENERATED_CODE_API_VERSION}
)
target_link_libraries(runtime_package_consumer PRIVATE Breadcrumbs::runtime)
