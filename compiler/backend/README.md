# Backend

Owns backend-facing scaffolding.

Responsibilities:

* consume Schema IR
* generate deterministic backend artifacts from validated IR
* report backend-specific failures
* preserve schema semantics and layout meaning

Current skeleton behavior:

* exposes `CodegenOptions`, `GeneratedFile`, and `CodegenResult`
* accepts Schema IR plus backend options only
* emits files only for namespaces that directly own records or enums
* derives file paths from the namespace FQN and configured output root
* writes deterministic placeholder declarations for records and enums
* keeps generation independent from AST, semantic analysis, and layout

Allowed dependencies:

* `compiler/schema_ir`
* `compiler/diagnostics`
* `compiler/support`

This layer must not parse source, resolve names, perform semantic analysis,
compute layout, or assign compiler-managed identifiers.
