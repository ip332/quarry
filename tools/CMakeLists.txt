add_executable(breadcrumbs_schema_compiler
    schema_compiler/main.cpp
)
set_target_properties(breadcrumbs_schema_compiler PROPERTIES
    EXPORT_NAME schema_compiler
    OUTPUT_NAME breadcrumbs-schema-compiler
)
add_executable(Breadcrumbs::schema_compiler ALIAS breadcrumbs_schema_compiler)
target_link_libraries(breadcrumbs_schema_compiler
    PRIVATE
        breadcrumbs_compiler_backend
        breadcrumbs_compiler_context
        breadcrumbs_compiler_diagnostics
        breadcrumbs_compiler_frontend
        breadcrumbs_compiler_support
        breadcrumbs_project_options
        breadcrumbs_schema_ir_warning_tweaks
)
target_include_directories(breadcrumbs_schema_compiler
    PRIVATE
        ${BREADCRUMBS_GENERATED_INCLUDE_DIR}
)
target_compile_definitions(breadcrumbs_schema_compiler
    PRIVATE
        BREADCRUMBS_VERSION="${PROJECT_VERSION}"
)

install(TARGETS breadcrumbs_schema_compiler
    EXPORT BreadcrumbsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
