# Frontend

This module owns the production-facing orchestration layer for YAML `.brd`
compilation and source-unit graph discovery.

## Responsibility

`YamlCompiler` runs the existing pipeline in order:

* `YamlParser`
* schema decoder
* source-schema normalization
* compiler-wide `NamespaceBuilder`
* compiler-wide `SemanticValidator`
* language-neutral `OutputPlanner`
* `LayoutComputer`
* `SchemaIrBuilder`
* `SchemaIrValidator`

The frontend returns validated Schema IR on success and no Schema IR on
failure. It stops after the first stage that reports errors or fails to
produce its expected model. It does not invoke backend generation.

Each invocation has one registered YAML root containing one YAML document and
one source schema unit. That unit has one namespace path and one primary
record; fields and enum declarations may be repeated within that unit. The
frontend now loads a root's transitive relative imports into the shared
compiler context, while semantic resolution and backend generation still use
only the root document in this PR. Multiple records, multiple namespace roots,
and YAML document streams remain outside the contract.

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

`YamlCompiler` owns stage sequencing only. YAML parsing, schema decoding, and
source-schema normalization remain separately testable lower-level compiler
APIs.

Non-empty YAML imports are decoded, normalized, and loaded transitively by the
source-unit graph loader. Missing files, duplicate source-unit identities, and
import cycles fail before semantic analysis. The loaded normalized documents
now feed one compiler-wide symbol index and semantic pass; qualified record
and enum references resolve to canonical FQNs within that graph. The output
planner then records the root artifact and its imported-unit dependencies in a
deterministic generation graph. Backend dependency generation and concrete
import/include emission remain future work.

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
