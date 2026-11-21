# Configuration preparation shared by the schema compiler CLI and its tests.
# Internal: not installed or exported.
add_library(quarry_schema_compiler_preparation STATIC
    schema_compiler/configuration_preparation.cpp
)
target_link_libraries(quarry_schema_compiler_preparation
    PUBLIC
        quarry_compiler_diagnostics
        quarry_compiler_layout
        quarry_compiler_output_planning
        quarry_compiler_schema_ir
        quarry_compiler_support
        quarry_project_options
    PRIVATE
        quarry_compiler_qbs
        quarry_schema_ir_warning_tweaks
)

add_executable(quarry_schema_compiler
    schema_compiler/main.cpp
)
set_target_properties(quarry_schema_compiler PROPERTIES
    EXPORT_NAME schema_compiler
    OUTPUT_NAME quarry-schema-compiler
)
add_executable(Quarry::schema_compiler ALIAS quarry_schema_compiler)
target_link_libraries(quarry_schema_compiler
    PRIVATE
        quarry_compiler_backend
        quarry_compiler_backend_c
        quarry_compiler_backend_go
        quarry_compiler_backend_python
        quarry_compiler_context
        quarry_compiler_diagnostics
        quarry_compiler_frontend
        quarry_compiler_qbs
        quarry_compiler_layout
        quarry_compiler_support
        quarry_project_options
        quarry_schema_compiler_preparation
        quarry_schema_ir_warning_tweaks
)
# Python's runtime epoch is maintained outside CMake. Read its public literal
# at configure time so --describe needs neither Python at runtime nor a second
# hard-coded epoch. Watch the source when an incremental build updates it.
set(_python_epoch_source "${CMAKE_SOURCE_DIR}/runtime/python/src/quarry/runtime/python/__init__.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_python_epoch_source}")
file(STRINGS "${_python_epoch_source}" _python_epoch_line
    REGEX "^QUARRY_GENERATED_CODE_API_VERSION_PYTHON = [0-9]+$")
list(LENGTH _python_epoch_line _python_epoch_count)
if(NOT _python_epoch_count EQUAL 1)
    message(FATAL_ERROR "Cannot read the Python runtime's generated-code API epoch")
endif()
string(REGEX REPLACE "^QUARRY_GENERATED_CODE_API_VERSION_PYTHON = ([0-9]+)$" "\\1"
    _python_epoch "${_python_epoch_line}")
target_compile_definitions(quarry_schema_compiler PRIVATE
    QUARRY_SCHEMA_COMPILER_PYTHON_API_EPOCH=${_python_epoch})

target_include_directories(quarry_schema_compiler
    PRIVATE
        ${QUARRY_GENERATED_INCLUDE_DIR}
)

install(TARGETS quarry_schema_compiler
    EXPORT QuarryTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_subdirectory(schema-translators/protobuf)

add_executable(quarry_brf_inspect brf_inspect/main.cpp)
set_target_properties(quarry_brf_inspect PROPERTIES OUTPUT_NAME quarry-brf-inspect)
target_link_libraries(quarry_brf_inspect PRIVATE quarry_runtime_c)
install(TARGETS quarry_brf_inspect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
