cmake_minimum_required(VERSION 3.20)

project(breadcrumbs_runtime_package_consumer LANGUAGES CXX)

find_package(Breadcrumbs CONFIG REQUIRED)

add_executable(runtime_package_consumer main.cpp)
target_link_libraries(runtime_package_consumer PRIVATE Breadcrumbs::runtime)
