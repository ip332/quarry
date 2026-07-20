if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "QUARRY_BUILD_FUZZERS requires a Clang compiler with libFuzzer")
endif()

set(QUARRY_FUZZ_SANITIZERS -fsanitize=fuzzer,address,undefined)

function(add_quarry_fuzzer name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE quarry_runtime quarry_project_options)
    target_compile_options(${name} PRIVATE ${QUARRY_FUZZ_SANITIZERS} -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE ${QUARRY_FUZZ_SANITIZERS})
endfunction()

add_quarry_fuzzer(brf_parse_fuzzer brf_parse_fuzzer.cpp)
add_quarry_fuzzer(brf_generated_decode_fuzzer brf_generated_decode_fuzzer.cpp)
