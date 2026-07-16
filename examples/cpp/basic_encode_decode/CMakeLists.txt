cmake_minimum_required(VERSION 3.20)

project(breadcrumbs_basic_encode_decode LANGUAGES CXX)

find_package(Breadcrumbs CONFIG REQUIRED)

add_executable(breadcrumbs_basic_encode_decode main.cpp)
target_link_libraries(breadcrumbs_basic_encode_decode PRIVATE Breadcrumbs::runtime)
