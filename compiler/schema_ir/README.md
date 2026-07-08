# Schema IR

Owns lowering from syntax-oriented compiler inputs into the protobuf Schema IR
defined by `proto/breadcrumbs/schema_ir.proto`.

Responsibilities:

* lower parsed AST into protobuf Schema IR
* preserve the parsed namespace hierarchy exactly as represented by the AST and
  `SymbolModel`
* represent resolved record and enum references using compiler-assigned IR
  identifiers
* preserve compiler-only source metadata where available
* avoid semantic validation, import resolution, and backend policy

Validation responsibilities:

* validate lowered Schema IR before later layout or backend stages consume it
* reject malformed namespace, record, enum, field, and value structure
* reject duplicate names in the same IR namespace or container
* reject duplicate IR identifiers
* reject missing or wrong-kind record and enum references
* validate that field types are present and structurally well-formed

`SchemaIrValidator` consumes the protobuf `SchemaIR` object and emits
diagnostics for IR integrity issues. It does not mutate the IR.

Public lowering surface:

* `SchemaIrModel` aliases the protobuf `breadcrumbs::schema_ir::SchemaIR`
* `SchemaIrBuilder::build(const ast::Ast&, const semantic::SemanticModel&,
  const layout::LayoutModel&, const symbols::SymbolModel&,
  context::CompilerContext&, diagnostics::DiagnosticCollection&)`
* `SchemaIrValidator::validate(const SchemaIrModel&, context::CompilerContext&,
  diagnostics::DiagnosticCollection&)`

Allowed dependencies:

* `compiler/ast`
* `compiler/context`
* `compiler/diagnostics`
* `compiler/layout`
* `compiler/semantic`
* `compiler/support`
* `compiler/symbols`
* protobuf-generated Schema IR types

Namespace handling:

* the protobuf model requires a single `root_namespace`
* `root_namespace` is treated as a synthetic container
* top-level AST namespaces are lowered as children of that synthetic root
* multi-component namespace declarations are lowered mechanically, one path
  component at a time, using the canonical `SymbolModel` scope tree

Type lowering:

* builtin scalar names are mapped to `PrimitiveType`
* `string` and `bytes` use the dedicated protobuf message kinds
* named record and enum references are lowered through resolved symbols to
  compiler IR identifiers
* fixed-size array syntax is lowered recursively as an `ArrayType`
* `field_index` remains a later layout concern and is not assigned here

Source metadata:

* source file paths and spans are copied from `SourceManager`
* source locations remain compiler-only metadata

Deferred work:

* deterministic Schema IR identifier derivation is not specified yet
* imports remain out of scope for this pass
* layout-driven field index assignment remains a later compiler stage
* backend generation remains out of scope
