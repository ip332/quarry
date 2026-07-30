add_library(quarry_compiler_support
    support/file_system.cpp
    support/source_location.cpp
    support/source_manager.cpp
)
target_include_directories(quarry_compiler_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_support PUBLIC quarry_project_options)

add_library(quarry_compiler_diagnostics
    diagnostics/diagnostic.cpp
)
target_include_directories(quarry_compiler_diagnostics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_diagnostics
    PUBLIC
        quarry_compiler_support
        quarry_project_options
)

add_library(quarry_compiler_context
    context/compiler_context.cpp
)
target_include_directories(quarry_compiler_context PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_context
    PUBLIC
        quarry_compiler_diagnostics
        quarry_compiler_support
        quarry_project_options
)

add_library(quarry_compiler_ast
    ast/ast.cpp
)
target_include_directories(quarry_compiler_ast PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_ast
    PUBLIC
        quarry_compiler_support
        quarry_project_options
)

add_library(quarry_compiler_parser
    parser/lexer.cpp
    parser/parser.cpp
)
target_include_directories(quarry_compiler_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_parser
    PUBLIC
        quarry_compiler_ast
        quarry_compiler_diagnostics
        quarry_compiler_support
        quarry_project_options
)

find_path(QUARRY_LIBYAML_INCLUDE_DIR NAMES yaml.h)
find_library(QUARRY_LIBYAML_LIBRARY NAMES yaml libyaml)

if(NOT QUARRY_LIBYAML_INCLUDE_DIR OR NOT QUARRY_LIBYAML_LIBRARY)
    message(FATAL_ERROR "Could not find libyaml")
endif()

add_library(quarry_yaml::yaml UNKNOWN IMPORTED)
set_target_properties(quarry_yaml::yaml PROPERTIES
    IMPORTED_LOCATION "${QUARRY_LIBYAML_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${QUARRY_LIBYAML_INCLUDE_DIR}"
)

add_library(quarry_compiler_source_schema
    source_schema/source_schema.cpp
)
target_include_directories(quarry_compiler_source_schema PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_source_schema
    PUBLIC
        quarry_compiler_diagnostics
        quarry_compiler_support
        quarry_project_options
)

add_library(quarry_compiler_yaml
    yaml/yaml_document.cpp
    yaml/schema_decoder.cpp
    yaml/yaml_parser.cpp
)
set_source_files_properties(
    yaml/yaml_parser.cpp
    PROPERTIES SKIP_LINTING ON
)
target_include_directories(quarry_compiler_yaml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_yaml
    PUBLIC
        quarry_compiler_diagnostics
        quarry_compiler_source_schema
        quarry_compiler_support
        quarry_project_options
    PRIVATE
        quarry_yaml::yaml
)

add_library(quarry_compiler_symbols
    symbols/symbols.cpp
)
target_include_directories(quarry_compiler_symbols PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_symbols
    PUBLIC
        quarry_compiler_diagnostics
        quarry_compiler_source_schema
        quarry_compiler_support
        quarry_project_options
)

add_library(quarry_compiler_semantic
    semantic/semantic.cpp
)
target_include_directories(quarry_compiler_semantic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_semantic
    PUBLIC
        quarry_compiler_symbols
        quarry_compiler_diagnostics
        quarry_compiler_source_schema
        quarry_compiler_support
        quarry_project_options
)

find_package(Protobuf REQUIRED)
find_package(absl CONFIG REQUIRED)

set(QUARRY_ABSL_LINK_LIBS "")
foreach(QUARRY_ABSL_CANDIDATE
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
    if(TARGET ${QUARRY_ABSL_CANDIDATE})
        list(APPEND QUARRY_ABSL_LINK_LIBS ${QUARRY_ABSL_CANDIDATE})
    endif()
endforeach()

add_library(quarry_schema_ir_warning_tweaks INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(quarry_schema_ir_warning_tweaks INTERFACE
        -Wno-error=nullability-extension
        -Wno-error=gcc-compat
        -Wno-error=gnu-anonymous-struct
        -Wno-error=nested-anon-types
    )
endif()

set(QUARRY_SCHEMA_IR_PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/schema_ir)
set(QUARRY_SCHEMA_IR_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../proto/quarry/schema_ir.proto)

add_custom_command(
    OUTPUT
        ${QUARRY_SCHEMA_IR_PROTO_DIR}/quarry/schema_ir.pb.cc
        ${QUARRY_SCHEMA_IR_PROTO_DIR}/quarry/schema_ir.pb.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${QUARRY_SCHEMA_IR_PROTO_DIR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/../proto
        --cpp_out=${QUARRY_SCHEMA_IR_PROTO_DIR}
        ${QUARRY_SCHEMA_IR_PROTO_FILE}
    DEPENDS ${QUARRY_SCHEMA_IR_PROTO_FILE}
    VERBATIM
)

add_library(quarry_schema_ir_proto
    ${QUARRY_SCHEMA_IR_PROTO_DIR}/quarry/schema_ir.pb.cc
)
target_include_directories(quarry_schema_ir_proto PUBLIC ${QUARRY_SCHEMA_IR_PROTO_DIR})
target_include_directories(quarry_schema_ir_proto SYSTEM PUBLIC ${Protobuf_INCLUDE_DIRS})
target_link_libraries(quarry_schema_ir_proto
    PUBLIC
        protobuf::libprotobuf
        quarry_project_options
        ${QUARRY_ABSL_LINK_LIBS}
    PRIVATE
        quarry_schema_ir_warning_tweaks
)

add_library(quarry_compiler_layout
    layout/layout.cpp
)
target_include_directories(quarry_compiler_layout PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_layout
    PUBLIC
        quarry_compiler_context
        quarry_compiler_semantic
        quarry_compiler_diagnostics
        quarry_compiler_support
        quarry_project_options
)

add_library(quarry_compiler_schema_ir
    schema_ir/schema_ir.cpp
    schema_ir/validation.cpp
)
target_include_directories(quarry_compiler_schema_ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_schema_ir
    PUBLIC
        quarry_compiler_source_schema
        quarry_schema_ir_proto
        quarry_compiler_context
        quarry_compiler_layout
        quarry_compiler_semantic
        quarry_compiler_diagnostics
        quarry_compiler_support
    PRIVATE
        quarry_schema_ir_warning_tweaks
)

add_library(quarry_compiler_frontend
    frontend/yaml_compiler.cpp
)
target_include_directories(quarry_compiler_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quarry_compiler_frontend
    PUBLIC
        quarry_compiler_context
        quarry_compiler_schema_ir
        quarry_project_options
    PRIVATE
        quarry_compiler_layout
        quarry_compiler_semantic
        quarry_compiler_symbols
        quarry_compiler_source_schema
        quarry_compiler_yaml
        quarry_schema_ir_warning_tweaks
)

add_library(quarry_compiler_backend
    backend/backend.cpp
)
target_include_directories(quarry_compiler_backend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${QUARRY_GENERATED_INCLUDE_DIR}
)
target_link_libraries(quarry_compiler_backend
    PUBLIC
        quarry_compiler_schema_ir
        quarry_project_options
    PRIVATE
        quarry_schema_ir_warning_tweaks
)

# Independent sibling of quarry_compiler_backend (the C++ backend): the C
# backend depends only on Schema IR and support, never on the C++ backend
# implementation. See compiler/backend_c/README.md and
# docs/design/c-backend.md.
add_library(quarry_compiler_backend_c
    backend_c/backend_c.cpp
)
target_include_directories(quarry_compiler_backend_c
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${QUARRY_GENERATED_INCLUDE_DIR}
)
target_link_libraries(quarry_compiler_backend_c
    PUBLIC
        quarry_compiler_schema_ir
        quarry_project_options
    PRIVATE
        quarry_schema_ir_warning_tweaks
)

# Independent sibling of quarry_compiler_backend and quarry_compiler_backend_c:
# the Python backend depends only on Schema IR and support, never on the C++
# or C backend implementations. See docs/design/python-backend.md.
add_library(quarry_compiler_backend_python
    backend_python/backend_python.cpp
)
target_include_directories(quarry_compiler_backend_python
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(quarry_compiler_backend_python
    PUBLIC
        quarry_compiler_schema_ir
        quarry_project_options
    PRIVATE
        quarry_schema_ir_warning_tweaks
)
