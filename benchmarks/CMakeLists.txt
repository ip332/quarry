cmake_minimum_required(VERSION 3.20)

set(QUARRY_BENCHMARK_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(QUARRY_BENCHMARK_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/schemas/workload.brd)
set(QUARRY_BENCHMARK_DEPENDENCY_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/schemas/workload_shared.brd)
set(QUARRY_BENCHMARK_CPP_HEADER
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/workload.generated.hpp)
set(QUARRY_BENCHMARK_C_HEADER
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/workload.generated.h)
set(QUARRY_BENCHMARK_C_SOURCE
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/workload.generated.c)
set(QUARRY_BENCHMARK_PYTHON_MODULE
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/workload/schema.py)
set(QUARRY_BENCHMARK_DEPENDENCY_CPP_HEADER
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/workload/shared.generated.hpp)
set(QUARRY_BENCHMARK_DEPENDENCY_C_HEADER
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/workload/shared.generated.h)
set(QUARRY_BENCHMARK_DEPENDENCY_C_SOURCE
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/workload/shared.generated.c)
set(QUARRY_BENCHMARK_DEPENDENCY_PYTHON_MODULE
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/workload/shared/schema.py)

add_custom_target(quarry_benchmark_generated
    COMMAND $<TARGET_FILE:quarry_schema_compiler>
            --language cpp --output-directory ${QUARRY_BENCHMARK_GENERATED_DIR}
            ${QUARRY_BENCHMARK_DEPENDENCY_SCHEMA}
    COMMAND $<TARGET_FILE:quarry_schema_compiler>
            --language c --output-directory ${QUARRY_BENCHMARK_GENERATED_DIR}
            ${QUARRY_BENCHMARK_DEPENDENCY_SCHEMA}
    COMMAND $<TARGET_FILE:quarry_schema_compiler>
            --language python --output-directory ${QUARRY_BENCHMARK_GENERATED_DIR}
            ${QUARRY_BENCHMARK_DEPENDENCY_SCHEMA}
    COMMAND $<TARGET_FILE:quarry_schema_compiler>
            --language cpp --output-directory ${QUARRY_BENCHMARK_GENERATED_DIR}
            ${QUARRY_BENCHMARK_SCHEMA}
    COMMAND $<TARGET_FILE:quarry_schema_compiler>
            --language c --output-directory ${QUARRY_BENCHMARK_GENERATED_DIR}
            ${QUARRY_BENCHMARK_SCHEMA}
    COMMAND $<TARGET_FILE:quarry_schema_compiler>
            --language python --output-directory ${QUARRY_BENCHMARK_GENERATED_DIR}
            ${QUARRY_BENCHMARK_SCHEMA}
    BYPRODUCTS ${QUARRY_BENCHMARK_CPP_HEADER} ${QUARRY_BENCHMARK_C_HEADER}
                ${QUARRY_BENCHMARK_C_SOURCE} ${QUARRY_BENCHMARK_PYTHON_MODULE}
                ${QUARRY_BENCHMARK_DEPENDENCY_CPP_HEADER}
                ${QUARRY_BENCHMARK_DEPENDENCY_C_HEADER}
                ${QUARRY_BENCHMARK_DEPENDENCY_C_SOURCE}
                ${QUARRY_BENCHMARK_DEPENDENCY_PYTHON_MODULE}
    DEPENDS quarry_schema_compiler ${QUARRY_BENCHMARK_SCHEMA}
            ${QUARRY_BENCHMARK_DEPENDENCY_SCHEMA}
    VERBATIM
)

add_executable(quarry_benchmark_cpp cpp/proof_benchmark.cpp)
add_dependencies(quarry_benchmark_cpp quarry_benchmark_generated)
target_include_directories(quarry_benchmark_cpp PRIVATE ${QUARRY_BENCHMARK_GENERATED_DIR})
target_link_libraries(quarry_benchmark_cpp PRIVATE quarry_runtime quarry_project_options)

add_executable(quarry_benchmark_c c/proof_benchmark.c ${QUARRY_BENCHMARK_C_SOURCE}
               ${QUARRY_BENCHMARK_DEPENDENCY_C_SOURCE})
add_dependencies(quarry_benchmark_c quarry_benchmark_generated)
target_include_directories(quarry_benchmark_c PRIVATE ${QUARRY_BENCHMARK_GENERATED_DIR})
target_link_libraries(quarry_benchmark_c PRIVATE quarry_runtime_c quarry_project_options)
set_target_properties(quarry_benchmark_c PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON
                      C_EXTENSIONS OFF)

add_custom_target(quarry_benchmark_python DEPENDS quarry_benchmark_generated)

find_package(Protobuf REQUIRED)
set(QUARRY_BENCHMARK_PROTOBUF_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf_generated)
set(QUARRY_BENCHMARK_PROTOBUF_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/schemas/workload.proto)
set(QUARRY_BENCHMARK_PROTOBUF_CPP_HEADER
    ${QUARRY_BENCHMARK_PROTOBUF_GENERATED_DIR}/workload.pb.h)
set(QUARRY_BENCHMARK_PROTOBUF_CPP_SOURCE
    ${QUARRY_BENCHMARK_PROTOBUF_GENERATED_DIR}/workload.pb.cc)
set(QUARRY_BENCHMARK_PROTOBUF_PYTHON_MODULE
    ${QUARRY_BENCHMARK_PROTOBUF_GENERATED_DIR}/workload_pb2.py)

add_custom_command(
    OUTPUT ${QUARRY_BENCHMARK_PROTOBUF_CPP_HEADER} ${QUARRY_BENCHMARK_PROTOBUF_CPP_SOURCE}
           ${QUARRY_BENCHMARK_PROTOBUF_PYTHON_MODULE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${QUARRY_BENCHMARK_PROTOBUF_GENERATED_DIR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/schemas
            --cpp_out=${QUARRY_BENCHMARK_PROTOBUF_GENERATED_DIR}
            --python_out=${QUARRY_BENCHMARK_PROTOBUF_GENERATED_DIR}
            ${QUARRY_BENCHMARK_PROTOBUF_SCHEMA}
    DEPENDS ${QUARRY_BENCHMARK_PROTOBUF_SCHEMA} ${Protobuf_PROTOC_EXECUTABLE}
    VERBATIM
)
add_custom_target(quarry_benchmark_protobuf_generated
    DEPENDS ${QUARRY_BENCHMARK_PROTOBUF_CPP_HEADER}
            ${QUARRY_BENCHMARK_PROTOBUF_CPP_SOURCE}
            ${QUARRY_BENCHMARK_PROTOBUF_PYTHON_MODULE})

add_executable(quarry_benchmark_protobuf_cpp protobuf/proof_benchmark.cpp
               ${QUARRY_BENCHMARK_PROTOBUF_CPP_SOURCE})
add_dependencies(quarry_benchmark_protobuf_cpp quarry_benchmark_protobuf_generated)
target_include_directories(quarry_benchmark_protobuf_cpp PRIVATE
    ${QUARRY_BENCHMARK_PROTOBUF_GENERATED_DIR} ${Protobuf_INCLUDE_DIRS})
target_link_libraries(quarry_benchmark_protobuf_cpp PRIVATE protobuf::libprotobuf quarry_project_options)

add_executable(quarry_benchmark_protobuf_cpp_arena protobuf/proof_benchmark.cpp
               ${QUARRY_BENCHMARK_PROTOBUF_CPP_SOURCE})
add_dependencies(quarry_benchmark_protobuf_cpp_arena quarry_benchmark_protobuf_generated)
target_compile_definitions(quarry_benchmark_protobuf_cpp_arena PRIVATE QUARRY_PROTOBUF_ARENA_DEFAULT=1)
target_include_directories(quarry_benchmark_protobuf_cpp_arena PRIVATE
    ${QUARRY_BENCHMARK_PROTOBUF_GENERATED_DIR} ${Protobuf_INCLUDE_DIRS})
target_link_libraries(quarry_benchmark_protobuf_cpp_arena PRIVATE protobuf::libprotobuf quarry_project_options)

add_custom_target(quarry_benchmark_protobuf_python
    DEPENDS quarry_benchmark_protobuf_generated)
