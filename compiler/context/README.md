# Context

Owns the compiler context aggregation layer.

`CompilerContext` is shared compiler infrastructure. It owns:

* `support::SourceManager`
* `support::FileSystem`
* `diagnostics::DiagnosticEngine`
* source-unit records and import edges discovered for one compilation
  invocation

This layer sits above support and diagnostics. Support source-location and
filesystem primitives remain independent of diagnostics. Diagnostics may depend
on support source-location types. Context aggregates both for compiler passes.

`CompilerContext` remains infrastructure rather than semantic state. Its
source-unit records contain canonical paths, declared source identity,
namespace metadata, source locations, and import edges; they do not contain
symbols, semantic models, layout state, backend dependencies, or compatibility
policy.

Allowed dependencies:

* C++ standard library
* `compiler/support`
* `compiler/diagnostics`

Context does not parse YAML or resolve imports. The frontend populates its
source-unit graph using the context's existing source manager and filesystem.
Context must not depend on parser, AST, YAML, symbols, semantic validation,
layout computation, Schema IR construction, or backends.
