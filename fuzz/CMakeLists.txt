if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "BREADCRUMBS_BUILD_FUZZERS requires a Clang compiler with libFuzzer")
endif()

set(BREADCRUMBS_FUZZ_SANITIZERS -fsanitize=fuzzer,address,undefined)

function(add_breadcrumbs_fuzzer name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE breadcrumbs_runtime breadcrumbs_project_options)
    target_compile_options(${name} PRIVATE ${BREADCRUMBS_FUZZ_SANITIZERS} -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE ${BREADCRUMBS_FUZZ_SANITIZERS})
endfunction()

add_breadcrumbs_fuzzer(brf_parse_fuzzer brf_parse_fuzzer.cpp)
add_breadcrumbs_fuzzer(brf_generated_decode_fuzzer brf_generated_decode_fuzzer.cpp)
