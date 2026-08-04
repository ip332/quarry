cmake_minimum_required(VERSION 3.20)

set(QUARRY_BENCHMARK_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(QUARRY_BENCHMARK_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/schemas/proof.brd)
set(QUARRY_BENCHMARK_CPP_HEADER
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/proof.generated.hpp)
set(QUARRY_BENCHMARK_C_HEADER
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/proof.generated.h)
set(QUARRY_BENCHMARK_C_SOURCE
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/proof.generated.c)
set(QUARRY_BENCHMARK_PYTHON_MODULE
    ${QUARRY_BENCHMARK_GENERATED_DIR}/benchmark/proof/schema.py)

add_custom_target(quarry_benchmark_generated
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
                ${QUARRY_BENCHMARK_C_SOURCE}
                ${QUARRY_BENCHMARK_PYTHON_MODULE}
    DEPENDS quarry_schema_compiler ${QUARRY_BENCHMARK_SCHEMA}
    VERBATIM
)

add_executable(quarry_benchmark_cpp cpp/proof_benchmark.cpp)
add_dependencies(quarry_benchmark_cpp quarry_benchmark_generated)
target_include_directories(quarry_benchmark_cpp PRIVATE ${QUARRY_BENCHMARK_GENERATED_DIR})
target_link_libraries(quarry_benchmark_cpp PRIVATE quarry_runtime quarry_project_options)

add_executable(quarry_benchmark_c c/proof_benchmark.c ${QUARRY_BENCHMARK_C_SOURCE})
add_dependencies(quarry_benchmark_c quarry_benchmark_generated)
target_include_directories(quarry_benchmark_c PRIVATE ${QUARRY_BENCHMARK_GENERATED_DIR})
target_link_libraries(quarry_benchmark_c PRIVATE quarry_runtime_c quarry_project_options)
set_target_properties(quarry_benchmark_c PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON
                      C_EXTENSIONS OFF)

add_custom_target(quarry_benchmark_python DEPENDS quarry_benchmark_generated)
