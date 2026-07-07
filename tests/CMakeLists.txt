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

function(add_breadcrumbs_smoke_test name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library} breadcrumbs_project_options)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

function(add_breadcrumbs_gtest name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library} GTest::gtest_main breadcrumbs_project_options)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_breadcrumbs_smoke_test(parser_smoke_test parser/parser_smoke_test.cpp breadcrumbs_compiler_parser)
add_breadcrumbs_smoke_test(imports_smoke_test imports/imports_smoke_test.cpp breadcrumbs_compiler_imports)
add_breadcrumbs_smoke_test(symbols_smoke_test symbols/symbols_smoke_test.cpp breadcrumbs_compiler_symbols)
add_breadcrumbs_smoke_test(semantic_smoke_test semantic/semantic_smoke_test.cpp breadcrumbs_compiler_semantic)
add_breadcrumbs_smoke_test(layout_smoke_test layout/layout_smoke_test.cpp breadcrumbs_compiler_layout)
add_breadcrumbs_smoke_test(schema_ir_smoke_test schema_ir/schema_ir_smoke_test.cpp breadcrumbs_compiler_schema_ir)
add_breadcrumbs_smoke_test(backend_smoke_test backend/backend_smoke_test.cpp breadcrumbs_compiler_backend)
add_breadcrumbs_gtest(diagnostics_smoke_test diagnostics/diagnostics_smoke_test.cpp breadcrumbs_compiler_context)
add_breadcrumbs_gtest(support_test support/support_test.cpp breadcrumbs_compiler_support)
