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
* compatibility AST projection
* `SchemaIrBuilder`
* `SchemaIrValidator`

The frontend returns validated Schema IR on success and no Schema IR on
failure. It does not invoke backend generation.

## Migration Boundary

The YAML path now builds the symbol table and semantic model directly from the
normalized source-schema model. The legacy declaration parser remains available
and is still used by existing declaration-syntax tests. The compatibility AST
projection is retained only immediately before `SchemaIrBuilder`.

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
