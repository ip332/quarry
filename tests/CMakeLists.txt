find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    set(INSTALL_GTEST OFF CACHE BOOL "Disable googletest installation" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    foreach(gtest_target gtest gtest_main gmock gmock_main)
        if(TARGET ${gtest_target})
            set_target_properties(${gtest_target} PROPERTIES CXX_CLANG_TIDY "")
        endif()
    endforeach()
endif()

function(add_quarry_gtest name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library} GTest::gtest_main quarry_project_options)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Parser/AST compatibility tests intentionally stop at parser-owned behavior.
# Symbols and semantic smoke coverage live in separate targets below.
add_quarry_gtest(parser_smoke_test parser/parser_smoke_test.cpp quarry_compiler_parser)
add_quarry_gtest(lexer_test parser/lexer_test.cpp quarry_compiler_parser)
add_quarry_gtest(ast_test ast/ast_test.cpp quarry_compiler_ast)
add_quarry_gtest(symbols_smoke_test symbols/symbols_smoke_test.cpp quarry_compiler_symbols)
add_quarry_gtest(semantic_smoke_test semantic/semantic_smoke_test.cpp quarry_compiler_semantic)
add_quarry_gtest(yaml_parser_test yaml/yaml_parser_test.cpp quarry_compiler_yaml)
add_quarry_gtest(yaml_schema_decoder_test yaml/schema_decoder_test.cpp quarry_compiler_yaml)
add_quarry_gtest(normalized_source_schema_pipeline_test
                      yaml/normalized_source_schema_pipeline_test.cpp
                      quarry_compiler_yaml)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_compiler_symbols)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_compiler_semantic)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_compiler_layout)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_compiler_schema_ir)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_compiler_context)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(yaml_compiler_test frontend/yaml_compiler_test.cpp
                      quarry_compiler_frontend)
target_link_libraries(yaml_compiler_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(layout_smoke_test layout/layout_smoke_test.cpp quarry_compiler_layout)
add_quarry_gtest(schema_ir_golden_test schema_ir/schema_ir_golden_test.cpp
                      quarry_compiler_frontend)
target_link_libraries(schema_ir_golden_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(schema_ir_validation_test schema_ir/schema_ir_validation_test.cpp
                      quarry_compiler_schema_ir)
target_link_libraries(schema_ir_validation_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(backend_smoke_test backend/backend_smoke_test.cpp quarry_compiler_backend)
target_link_libraries(backend_smoke_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(backend_codegen_test backend/backend_codegen_test.cpp
                      quarry_compiler_backend)
target_compile_definitions(backend_codegen_test PRIVATE
    QUARRY_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    QUARRY_TEST_GENERATED_INCLUDE_DIR="${QUARRY_GENERATED_INCLUDE_DIR}"
    QUARRY_TEST_GENERATED_CODE_API_VERSION=${QUARRY_GENERATED_CODE_API_VERSION})
target_link_libraries(backend_codegen_test PRIVATE quarry_compiler_schema_ir)
target_link_libraries(backend_codegen_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(runtime_binary_record_test runtime/binary_record_test.cpp
                      quarry_runtime)
target_compile_definitions(runtime_binary_record_test PRIVATE
    QUARRY_TEST_GENERATED_CODE_API_VERSION=${QUARRY_GENERATED_CODE_API_VERSION})
add_quarry_gtest(runtime_package_consumer_test consumer/runtime_package_test.cpp
                      quarry_compiler_support)
target_compile_definitions(runtime_package_consumer_test PRIVATE
    QUARRY_TEST_CMAKE_COMMAND="${CMAKE_COMMAND}"
    QUARRY_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    QUARRY_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    QUARRY_RUNTIME_PACKAGE_CONSUMER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/consumer/runtime_package/project")
add_quarry_gtest(schema_compiler_install_test consumer/schema_compiler_install_test.cpp
                      quarry_compiler_support)
target_compile_definitions(schema_compiler_install_test PRIVATE
    QUARRY_TEST_CMAKE_COMMAND="${CMAKE_COMMAND}"
    QUARRY_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    QUARRY_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    QUARRY_TEST_INSTALL_BINDIR="${CMAKE_INSTALL_BINDIR}")
add_quarry_gtest(schema_compiler_package_test consumer/schema_compiler_package_test.cpp
                      quarry_compiler_support)
target_compile_definitions(schema_compiler_package_test PRIVATE
    QUARRY_TEST_CMAKE_COMMAND="${CMAKE_COMMAND}"
    QUARRY_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    QUARRY_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    QUARRY_SCHEMA_COMPILER_PACKAGE_CONSUMER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/examples/cpp/schema_compiler_cmake")
add_quarry_gtest(schema_compiler_tool_test tools/schema_compiler_tool_test.cpp
                      quarry_compiler_support)
add_dependencies(schema_compiler_tool_test quarry_schema_compiler)
target_compile_definitions(schema_compiler_tool_test PRIVATE
    QUARRY_SCHEMA_COMPILER_TOOL="$<TARGET_FILE:quarry_schema_compiler>"
    QUARRY_TEST_GENERATED_CODE_API_VERSION=${QUARRY_GENERATED_CODE_API_VERSION})
add_quarry_gtest(diagnostics_smoke_test diagnostics/diagnostics_smoke_test.cpp quarry_compiler_context)
add_quarry_gtest(support_test support/support_test.cpp quarry_compiler_support)
