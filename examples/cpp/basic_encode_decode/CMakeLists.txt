cmake_minimum_required(VERSION 3.20)

project(quarry_basic_encode_decode LANGUAGES CXX)

find_package(Quarry CONFIG REQUIRED)

add_executable(quarry_basic_encode_decode main.cpp)
target_link_libraries(quarry_basic_encode_decode PRIVATE Quarry::runtime)
