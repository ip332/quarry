# Compiler Source-Tree API Boundary

This directory contains compiler libraries used by the Quarry source tree.
They are not installed or exported as a CMake package, and the repository does
not currently promise an external compiler SDK or ABI-stable public API.

Header visibility in this directory means source-tree visibility. It does not
by itself make a type or function an external integration contract.

## Supported Source-Tree Entry Points

The highest-level source compiler entry point is
`frontend::YamlCompiler::compile`. Callers provide a `SourceFileId` already
registered in a `context::CompilerContext`; the compiler returns validated
Schema IR on success and no Schema IR after fatal diagnostics.

Backend generation starts from validated Schema IR. The supported backend entry
point is `backend::Backend::generate`, which consumes `schema_ir::SchemaIrModel`
and `backend::CodegenOptions` and returns generated files or a backend error.

The compiler does not currently provide:

* a file-loading source compiler facade
* an import resolver
* a declaration-syntax production frontend
* a combined source-to-backend facade
* install or export rules for compiler libraries

## Supported Low-Level Building Blocks

Lower compiler libraries expose source-tree APIs so passes can be tested and
composed independently:

* `yaml::YamlParser` and `yaml::decode_schema`
* `source_schema::normalize_source_schema`
* `symbols::NamespaceBuilder`
* `semantic::SemanticValidator`
* `layout::LayoutComputer`
* `schema_ir::SchemaIrBuilder`
* `schema_ir::SchemaIrValidator`
* diagnostics and support infrastructure

These APIs are legitimate internal compiler building blocks. They are not
competing high-level source compiler entry points, and they should not be used
to bypass `frontend::YamlCompiler` when the desired operation is normal YAML
source compilation.

## Cross-Layer Models

Several headers define concrete models that intentionally cross target
boundaries:

* `yaml::YamlDocument`
* `source_schema::SourceSchemaDocument`
* `source_schema::NormalizedSourceSchemaDocument`
* `symbols::SymbolTable`
* `semantic::SemanticModel`
* `layout::LayoutModel`
* `schema_ir::SchemaIrModel`
* `diagnostics::DiagnosticEngine`
* `support::SourceManager`, `support::SourceLocation`, and `support::SourceRange`

These models are compiler-internal source-tree contracts. They are concrete and
mutable because the current compiler stages and tests inspect them directly.
They may represent intermediate or invalid states before the owning pass has
validated them.

## CMake Visibility

Many target dependencies are `PUBLIC` because public source-tree headers expose
concrete types from other compiler targets. This visibility supports current
source-tree composition and tests; it is not an external packaging statement.

Do not tighten a dependency from `PUBLIC` to `PRIVATE` unless every public
header signature and every direct test or production consumer has been checked.

## External API Status

The `quarry-schema-compiler` executable is installed and exported as the
`Quarry::schema_compiler` imported executable target, but compiler
libraries remain source-tree implementation details. There are currently no
`install(...)`, `export(...)`, `BUILD_INTERFACE`, or `INSTALL_INTERFACE` rules
for compiler libraries. External compiler-library API shape, package layout,
and stability guarantees are deferred until a real embedding consumer or
packaging requirement exists.
