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

find_path(BREADCRUMBS_LIBYAML_INCLUDE_DIR NAMES yaml.h)
find_library(BREADCRUMBS_LIBYAML_LIBRARY NAMES yaml libyaml)

if(NOT BREADCRUMBS_LIBYAML_INCLUDE_DIR OR NOT BREADCRUMBS_LIBYAML_LIBRARY)
    message(FATAL_ERROR "Could not find libyaml")
endif()

add_library(breadcrumbs_yaml::yaml UNKNOWN IMPORTED)
set_target_properties(breadcrumbs_yaml::yaml PROPERTIES
    IMPORTED_LOCATION "${BREADCRUMBS_LIBYAML_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${BREADCRUMBS_LIBYAML_INCLUDE_DIR}"
)

add_library(breadcrumbs_compiler_yaml
    yaml/yaml_document.cpp
    yaml/schema_decoder.cpp
    yaml/source_schema_lowering.cpp
    yaml/yaml_parser.cpp
)
set_source_files_properties(
    yaml/yaml_parser.cpp
    PROPERTIES SKIP_LINTING ON
)
target_include_directories(breadcrumbs_compiler_yaml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_yaml
    PUBLIC
        breadcrumbs_compiler_ast
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
        breadcrumbs_project_options
    PRIVATE
        breadcrumbs_yaml::yaml
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
        breadcrumbs_compiler_ast
        breadcrumbs_compiler_symbols
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
        breadcrumbs_project_options
)

find_package(Protobuf REQUIRED)
find_package(absl CONFIG REQUIRED)

set(BREADCRUMBS_ABSL_LINK_LIBS "")
foreach(BREADCRUMBS_ABSL_CANDIDATE
    absl::log_internal_check_op
    absl::log_internal_message
    absl::log_internal_nullguard
    absl::log_internal_format
    absl::log_internal_log_sink_set
    absl::log_internal_conditions
    absl::log_internal_globals
    absl::log_severity
    absl::raw_logging_internal
    absl::strings
    absl::str_format
    absl::base
    absl::container_memory
    absl::raw_hash_set
)
    if(TARGET ${BREADCRUMBS_ABSL_CANDIDATE})
        list(APPEND BREADCRUMBS_ABSL_LINK_LIBS ${BREADCRUMBS_ABSL_CANDIDATE})
    endif()
endforeach()

add_library(breadcrumbs_schema_ir_warning_tweaks INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(breadcrumbs_schema_ir_warning_tweaks INTERFACE
        -Wno-error=nullability-extension
        -Wno-error=gcc-compat
        -Wno-error=gnu-anonymous-struct
        -Wno-error=nested-anon-types
    )
endif()

set(BREADCRUMBS_SCHEMA_IR_PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/schema_ir)
set(BREADCRUMBS_SCHEMA_IR_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../proto/breadcrumbs/schema_ir.proto)

add_custom_command(
    OUTPUT
        ${BREADCRUMBS_SCHEMA_IR_PROTO_DIR}/breadcrumbs/schema_ir.pb.cc
        ${BREADCRUMBS_SCHEMA_IR_PROTO_DIR}/breadcrumbs/schema_ir.pb.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BREADCRUMBS_SCHEMA_IR_PROTO_DIR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/../proto
        --cpp_out=${BREADCRUMBS_SCHEMA_IR_PROTO_DIR}
        ${BREADCRUMBS_SCHEMA_IR_PROTO_FILE}
    DEPENDS ${BREADCRUMBS_SCHEMA_IR_PROTO_FILE}
    VERBATIM
)

add_library(breadcrumbs_schema_ir_proto
    ${BREADCRUMBS_SCHEMA_IR_PROTO_DIR}/breadcrumbs/schema_ir.pb.cc
)
target_include_directories(breadcrumbs_schema_ir_proto PUBLIC ${BREADCRUMBS_SCHEMA_IR_PROTO_DIR})
target_include_directories(breadcrumbs_schema_ir_proto SYSTEM PUBLIC ${Protobuf_INCLUDE_DIRS})
target_link_libraries(breadcrumbs_schema_ir_proto
    PUBLIC
        protobuf::libprotobuf
        breadcrumbs_project_options
        ${BREADCRUMBS_ABSL_LINK_LIBS}
    PRIVATE
        breadcrumbs_schema_ir_warning_tweaks
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
    schema_ir/validation.cpp
)
target_include_directories(breadcrumbs_compiler_schema_ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_schema_ir
    PUBLIC
        breadcrumbs_schema_ir_proto
        breadcrumbs_compiler_context
        breadcrumbs_compiler_layout
        breadcrumbs_compiler_semantic
        breadcrumbs_compiler_symbols
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_support
    PRIVATE
        breadcrumbs_schema_ir_warning_tweaks
)

add_library(breadcrumbs_compiler_frontend
    frontend/yaml_compiler.cpp
)
target_include_directories(breadcrumbs_compiler_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_frontend
    PUBLIC
        breadcrumbs_compiler_context
        breadcrumbs_compiler_schema_ir
        breadcrumbs_project_options
    PRIVATE
        breadcrumbs_compiler_layout
        breadcrumbs_compiler_semantic
        breadcrumbs_compiler_symbols
        breadcrumbs_compiler_yaml
        breadcrumbs_schema_ir_warning_tweaks
)

add_library(breadcrumbs_compiler_backend
    backend/backend.cpp
)
target_include_directories(breadcrumbs_compiler_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(breadcrumbs_compiler_backend
    PUBLIC
        breadcrumbs_compiler_schema_ir
        breadcrumbs_project_options
    PRIVATE
        breadcrumbs_schema_ir_warning_tweaks
)
