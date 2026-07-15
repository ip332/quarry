add_executable(breadcrumbs_schema_compiler
    schema_compiler/main.cpp
)
set_target_properties(breadcrumbs_schema_compiler PROPERTIES
    OUTPUT_NAME breadcrumbs-schema-compiler
)
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
