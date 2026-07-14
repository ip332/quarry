# Schema IR

Owns lowering from normalized source schema into the protobuf Schema IR
defined by `proto/breadcrumbs/schema_ir.proto`.

Responsibilities:

* lower resolved Semantic Model field types into protobuf Schema IR field
  types
* copy Semantic Model record metadata such as schema version and logical
  record type when available
* preserve the parsed namespace hierarchy exactly as represented by the
  normalized source schema
* represent resolved record and enum references using compiler-assigned IR
  identifiers
* copy validated string/bytes `max_bytes` and array `max_elements` values from
  the Semantic Model into protobuf Schema IR
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
* `SchemaIrBuilder::build(const source_schema::NormalizedSourceSchemaDocument&,
  const semantic::SemanticModel&, const layout::LayoutModel&,
  context::CompilerContext&, diagnostics::DiagnosticCollection&)`
* `SchemaIrValidator::validate(const SchemaIrModel&, context::CompilerContext&,
  diagnostics::DiagnosticCollection&)`

Allowed dependencies:

* `compiler/context`
* `compiler/diagnostics`
* `compiler/layout`
* `compiler/semantic`
* `compiler/support`
* `compiler/source_schema`
* protobuf-generated Schema IR types

Implementation status:

* the normative `.brd` source contract is defined in
  `docs/specifications/schema-language.md`
* the production-facing YAML frontend now orchestrates the import-free YAML
  pipeline through validated Schema IR directly from the normalized
  source-schema model
* direct normalized-source-schema tests under
  `tests/yaml/normalized_source_schema_pipeline_test.cpp` provide the primary
  Schema IR builder unit coverage
* `tests/schema_ir/schema_ir_validation_test.cpp` constructs representation-
  neutral protobuf Schema IR directly for validator coverage
* multiple sibling top-level namespaces remain valid Schema IR and are covered
  through direct protobuf/backend fixtures
* parser/AST compatibility is not a Schema IR responsibility

Namespace handling:

* the protobuf model requires a single `root_namespace`
* `root_namespace` is treated as a synthetic container
* top-level YAML source-schema namespaces are lowered as children of that
  synthetic root
* multi-component namespace declarations are lowered mechanically, one path
  component at a time

Type lowering:

* resolved `SemanticType` values are lowered into protobuf `FieldType`
* semantic analysis owns builtin alias normalization and named-type resolution
* `string` and `bytes` use the dedicated protobuf message kinds and carry exact
  validated `max_bytes`
* named record and enum references use the canonical target FQNs stored in the
  Semantic Model and are translated to compiler IR identifiers here
* semantic arrays are lowered recursively as bounded-variable `ArrayType`
  values and carry exact validated `max_elements`
* `field_index` is copied from the Layout Model and is not invented here
* record IDs are computed in the Layout Model and are copied into `RecordIR`
* `record_id` and `ir_id` remain separate compiler-owned identifiers

Source metadata:

* source file paths and spans are copied from `SourceManager`
* source locations remain compiler-only metadata

Golden tests:

* fixture-backed golden tests live under `tests/fixtures/schema_ir_yaml` for
  the production YAML path
* the production golden suite compiles YAML fixtures through
  `frontend::YamlCompiler` and compares a normalized text-format rendering of
  the resulting Schema IR
* compiler-only source metadata is stripped from the comparison so the golden
  files stay stable and reviewable

Deferred work:

* deterministic Schema IR identifier derivation is not specified yet
* imports remain out of scope for this pass
* preserved manifest state and compatibility-aware layout reuse remain deferred
* backend generation remains out of scope
