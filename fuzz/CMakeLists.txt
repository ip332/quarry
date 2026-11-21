if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "QUARRY_BUILD_FUZZERS requires a Clang compiler with libFuzzer")
endif()

set(QUARRY_FUZZ_SANITIZERS -fsanitize=fuzzer,address,undefined)

function(add_quarry_fuzzer name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE quarry_runtime quarry_project_options)
    target_compile_options(${name} PRIVATE ${QUARRY_FUZZ_SANITIZERS} -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE ${QUARRY_FUZZ_SANITIZERS})
endfunction()

add_quarry_fuzzer(brf_parse_fuzzer brf_parse_fuzzer.cpp)
add_quarry_fuzzer(brf_generated_decode_fuzzer brf_generated_decode_fuzzer.cpp)

add_quarry_fuzzer(qbs_parse_fuzzer qbs_parse_fuzzer.cpp)
# Compile the existing parser/accessor sources with fuzzer coverage + ASan/UBSan.
# The normal compiler/runtime libraries keep their ordinary build policy; their
# corresponding archive members are not pulled when already defined here.
target_sources(qbs_parse_fuzzer PRIVATE
    ../compiler/qbs/parser.cpp ../runtime_c/generic_brf.c ../runtime_c/schema_id.c)
target_link_libraries(qbs_parse_fuzzer PRIVATE quarry_compiler_qbs quarry_runtime_c)
target_compile_options(qbs_parse_fuzzer PRIVATE -fno-sanitize-recover=undefined)
