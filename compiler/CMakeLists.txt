add_library(breadcrumbs_compiler_support
    support/file_system.cpp
    support/source_location.cpp
    support/source_manager.cpp
)
target_include_directories(breadcrumbs_compiler_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_support PUBLIC breadcrumbs_project_options)

add_library(breadcrumbs_compiler_diagnostics
    diagnostics/diagnostic.cpp
)
target_include_directories(breadcrumbs_compiler_diagnostics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_diagnostics
    PUBLIC
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)

add_library(breadcrumbs_compiler_context
    context/compiler_context.cpp
)
target_include_directories(breadcrumbs_compiler_context PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_context
    PUBLIC
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)

add_library(breadcrumbs_compiler_ast
    ast/ast.cpp
)
target_include_directories(breadcrumbs_compiler_ast PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_ast
    PUBLIC
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)

add_library(breadcrumbs_compiler_parser
    parser/lexer.cpp
    parser/parser.cpp
)
target_include_directories(breadcrumbs_compiler_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_parser
    PUBLIC
        breadcrumbs_compiler_ast
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)

add_library(breadcrumbs_compiler_imports
    imports/imports.cpp
)
target_include_directories(breadcrumbs_compiler_imports PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_imports
    PUBLIC
        breadcrumbs_compiler_ast
        breadcrumbs_compiler_context
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)

add_library(breadcrumbs_compiler_symbols
    symbols/symbols.cpp
)
target_include_directories(breadcrumbs_compiler_symbols PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_symbols
    PUBLIC
        breadcrumbs_compiler_ast
        breadcrumbs_compiler_context
        breadcrumbs_compiler_imports
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)

add_library(breadcrumbs_compiler_semantic
    semantic/semantic.cpp
)
target_include_directories(breadcrumbs_compiler_semantic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_semantic
    PUBLIC
        breadcrumbs_compiler_context
        breadcrumbs_compiler_symbols
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)

add_library(breadcrumbs_compiler_layout
    layout/layout.cpp
)
target_include_directories(breadcrumbs_compiler_layout PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_layout
    PUBLIC
        breadcrumbs_compiler_context
        breadcrumbs_compiler_semantic
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)

add_library(breadcrumbs_compiler_schema_ir
    schema_ir/schema_ir.cpp
)
target_include_directories(breadcrumbs_compiler_schema_ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_schema_ir
    PUBLIC
        breadcrumbs_compiler_context
        breadcrumbs_compiler_semantic
        breadcrumbs_compiler_layout
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)

add_library(breadcrumbs_compiler_backend
    backend/backend.cpp
)
target_include_directories(breadcrumbs_compiler_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_backend
    PUBLIC
        breadcrumbs_compiler_context
        breadcrumbs_compiler_schema_ir
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)
