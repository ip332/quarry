# Context

Owns the compiler context aggregation layer.

`CompilerContext` is shared compiler infrastructure. It owns:

* `support::SourceManager`
* `support::FileSystem`
* `diagnostics::DiagnosticEngine`

This layer sits above support and diagnostics. Support source-location and
filesystem primitives remain independent of diagnostics. Diagnostics may depend
on support source-location types. Context aggregates both for compiler passes.

`CompilerContext` must remain infrastructure rather than semantic state. Do not
add semantic models, layout state, identifier allocation state, or compatibility
policy here until the relevant compiler layer requires them and the ownership
boundary is explicit.

Allowed dependencies:

* C++ standard library
* `compiler/support`
* `compiler/diagnostics`

Context must not depend on parser, AST, imports, symbols, semantic validation,
layout computation, Schema IR construction, or backends.
