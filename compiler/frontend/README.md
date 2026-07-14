# Frontend

This module owns the production-facing orchestration layer for import-free
YAML `.brd` compilation.

## Responsibility

`YamlCompiler` runs the existing pipeline in order:

* `YamlParser`
* schema decoder
* source-schema normalization
* `NamespaceBuilder`
* `SemanticValidator`
* `LayoutComputer`
* `SchemaIrBuilder`
* `SchemaIrValidator`

The frontend returns validated Schema IR on success and no Schema IR on
failure. It does not invoke backend generation.

The Schema IR exact-output golden suite now runs through this production YAML
frontend against the YAML fixture tree under `tests/fixtures/schema_ir_yaml`.
The legacy declaration-syntax parser remains available for its own test
coverage.

## Migration Boundary

The YAML path now builds the symbol table and semantic model directly from the
normalized source-schema model and lowers directly into `SchemaIrBuilder`
without any compatibility AST hop. The legacy declaration parser remains
available only as compatibility/test infrastructure and is still used by
existing declaration-syntax tests. It is not a supported standalone compiler
frontend.

Non-empty YAML imports remain unsupported and continue to fail in the existing
source-schema normalization layer. Import resolution is not implemented here.

## Dependencies

Allowed direct dependencies:

* `compiler/context`
* `compiler/yaml`
* `compiler/symbols`
* `compiler/semantic`
* `compiler/layout`
* `compiler/schema_ir`

The frontend is an orchestration layer only. Lower compiler stages do not
depend on it.
