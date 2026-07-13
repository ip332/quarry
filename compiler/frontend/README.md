# Frontend

This module owns the production-facing orchestration layer for import-free
YAML `.brd` compilation.

## Responsibility

`YamlCompiler` runs the existing pipeline in order:

* `YamlParser`
* schema decoder
* source-schema lowering
* `NamespaceBuilder`
* `SemanticValidator`
* `LayoutComputer`
* `SchemaIrBuilder`
* `SchemaIrValidator`

The frontend returns validated Schema IR on success and no Schema IR on
failure. It does not invoke backend generation.

## Migration Boundary

The current YAML path still lowers into the transitional AST boundary before
the existing compiler passes consume it. The legacy declaration parser remains
available and is still used by existing declaration-syntax tests. The
SourceSchemaDocument-to-SemanticModel migration has not happened yet.

Non-empty YAML imports remain unsupported and continue to fail in the existing
source-schema lowering pass. Import resolution is not implemented here.

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
