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
        quarry_compiler_context
        quarry_compiler_diagnostics
        quarry_compiler_frontend
        quarry_compiler_support
        quarry_project_options
        quarry_schema_ir_warning_tweaks
)
target_include_directories(quarry_schema_compiler
    PRIVATE
        ${QUARRY_GENERATED_INCLUDE_DIR}
)
target_compile_definitions(quarry_schema_compiler
    PRIVATE
        QUARRY_VERSION="${PROJECT_VERSION}"
)

install(TARGETS quarry_schema_compiler
    EXPORT QuarryTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
