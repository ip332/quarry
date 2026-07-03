# Backend

Owns backend-facing scaffolding.

Responsibilities:

* consume Schema IR
* generate target-specific artifacts in future backends
* report backend-specific failures
* preserve schema semantics and layout meaning

Allowed dependencies:

* `compiler/schema_ir`
* `compiler/diagnostics`
* `compiler/support`

This layer must not parse source, resolve names, perform semantic analysis,
compute layout, or assign compiler-managed identifiers.
