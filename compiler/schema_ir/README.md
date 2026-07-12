# Schema IR

Owns lowering from syntax-oriented compiler inputs into the protobuf Schema IR
defined by `proto/breadcrumbs/schema_ir.proto`.

Responsibilities:

* lower resolved Semantic Model field types into protobuf Schema IR field
  types
* preserve the parsed namespace hierarchy exactly as represented by the AST and
  `SymbolTable`
* represent resolved record and enum references using compiler-assigned IR
  identifiers
* preserve compiler-only source metadata where available
* avoid semantic validation, import resolution, and backend policy

Validation responsibilities:

* validate lowered Schema IR before later layout or backend stages consume it
* reject malformed namespace, record, enum, field, and value structure
* reject duplicate names in the same IR namespace or container
* reject duplicate IR identifiers
* reject missing or duplicate compiler-assigned record IDs
* reject missing or wrong-kind record and enum references
* validate that field types are present and structurally well-formed

`SchemaIrValidator` consumes the protobuf `SchemaIR` object and emits
diagnostics for IR integrity issues. It does not mutate the IR.

Public lowering surface:

* `SchemaIrModel` aliases the protobuf `breadcrumbs::schema_ir::SchemaIR`
* `SchemaIrBuilder::build(const ast::Ast&, const semantic::SemanticModel&,
  const layout::LayoutModel&, const symbols::SymbolTable&, context::CompilerContext&,
  diagnostics::DiagnosticCollection&)`
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
  component at a time, using the canonical `SymbolTable` scope tree

Type lowering:

* resolved `SemanticType` values are lowered into protobuf `FieldType`
* semantic analysis owns builtin alias normalization and named-type resolution
* `string` and `bytes` use the dedicated protobuf message kinds
* named record and enum references use the canonical target FQNs stored in the
  Semantic Model and are translated to compiler IR identifiers here
* semantic arrays are lowered recursively as `ArrayType`
* the current builder still copies the parsed array count into the protobuf
  `ArrayType` because the Semantic Model carries recursive array shape but not
  bounds yet
* `field_index` is copied from the Layout Model and is not invented here
* record IDs are computed in the Layout Model and are copied into `RecordIR`
* `record_id` and `ir_id` remain separate compiler-owned identifiers

Source metadata:

* source file paths and spans are copied from `SourceManager`
* source locations remain compiler-only metadata

Golden tests:

* fixture-backed golden tests live under `tests/fixtures/schema_ir`
* each fixture pairs a `.brd` source file with a `.textproto` expectation
* goldens compare a normalized text-format rendering of the lowered IR
* compiler-only source metadata is stripped from the comparison so the golden
  files stay stable and reviewable
* schema IR smoke tests still cover source metadata behavior separately

Deferred work:

* deterministic Schema IR identifier derivation is not specified yet
* imports remain out of scope for this pass
* preserved manifest state and compatibility-aware layout reuse remain deferred
* backend generation remains out of scope
