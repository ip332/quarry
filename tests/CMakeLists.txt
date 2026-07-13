find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
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

function(add_breadcrumbs_gtest name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library} GTest::gtest_main breadcrumbs_project_options)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Parser layer test target.
add_breadcrumbs_gtest(parser_smoke_test parser/parser_smoke_test.cpp breadcrumbs_compiler_parser)
add_breadcrumbs_gtest(lexer_test parser/lexer_test.cpp breadcrumbs_compiler_parser)
add_breadcrumbs_gtest(ast_test ast/ast_test.cpp breadcrumbs_compiler_ast)
add_breadcrumbs_gtest(imports_smoke_test imports/imports_smoke_test.cpp breadcrumbs_compiler_imports)
add_breadcrumbs_gtest(symbols_smoke_test symbols/symbols_smoke_test.cpp breadcrumbs_compiler_symbols)
add_breadcrumbs_gtest(semantic_smoke_test semantic/semantic_smoke_test.cpp breadcrumbs_compiler_semantic)
target_link_libraries(semantic_smoke_test PRIVATE breadcrumbs_compiler_parser)
add_breadcrumbs_gtest(yaml_parser_test yaml/yaml_parser_test.cpp breadcrumbs_compiler_yaml)
add_breadcrumbs_gtest(yaml_schema_decoder_test yaml/schema_decoder_test.cpp breadcrumbs_compiler_yaml)
add_breadcrumbs_gtest(layout_smoke_test layout/layout_smoke_test.cpp breadcrumbs_compiler_layout)
target_link_libraries(layout_smoke_test PRIVATE breadcrumbs_compiler_parser)
add_breadcrumbs_gtest(schema_ir_smoke_test schema_ir/schema_ir_smoke_test.cpp breadcrumbs_compiler_schema_ir)
target_link_libraries(schema_ir_smoke_test PRIVATE breadcrumbs_compiler_parser)
target_link_libraries(schema_ir_smoke_test PRIVATE breadcrumbs_schema_ir_warning_tweaks)
add_breadcrumbs_gtest(schema_ir_golden_test schema_ir/schema_ir_golden_test.cpp
                      breadcrumbs_compiler_schema_ir)
target_link_libraries(schema_ir_golden_test PRIVATE breadcrumbs_compiler_parser)
target_link_libraries(schema_ir_golden_test PRIVATE breadcrumbs_schema_ir_warning_tweaks)
add_breadcrumbs_gtest(schema_ir_validation_test schema_ir/schema_ir_validation_test.cpp
                      breadcrumbs_compiler_schema_ir)
target_link_libraries(schema_ir_validation_test PRIVATE breadcrumbs_compiler_parser)
target_link_libraries(schema_ir_validation_test PRIVATE breadcrumbs_schema_ir_warning_tweaks)
add_breadcrumbs_gtest(backend_smoke_test backend/backend_smoke_test.cpp breadcrumbs_compiler_backend)
target_link_libraries(backend_smoke_test PRIVATE breadcrumbs_schema_ir_warning_tweaks)
add_breadcrumbs_gtest(backend_codegen_test backend/backend_codegen_test.cpp
                      breadcrumbs_compiler_backend)
target_compile_definitions(backend_codegen_test PRIVATE
    BREADCRUMBS_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}")
target_link_libraries(backend_codegen_test PRIVATE breadcrumbs_compiler_parser)
target_link_libraries(backend_codegen_test PRIVATE breadcrumbs_compiler_layout)
target_link_libraries(backend_codegen_test PRIVATE breadcrumbs_compiler_schema_ir)
target_link_libraries(backend_codegen_test PRIVATE breadcrumbs_schema_ir_warning_tweaks)
add_breadcrumbs_gtest(diagnostics_smoke_test diagnostics/diagnostics_smoke_test.cpp breadcrumbs_compiler_context)
add_breadcrumbs_gtest(support_test support/support_test.cpp breadcrumbs_compiler_support)
