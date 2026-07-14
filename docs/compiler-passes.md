# Compiler Passes

## Goals

This document defines the Breadcrumbs schema compiler pipeline from parsed
source files to Schema IR.

The compiler is described as a sequence of pure transformations. Each pass
consumes an input representation, validates or enriches it, and produces a new
output representation. Earlier representations remain available for
diagnostics, debugging, and tooling.

Every pass operates on its declared input representation together with a
Compiler Context. The Compiler Context provides shared compiler infrastructure,
such as the source manager, diagnostics, file system abstraction, compilation
options, compatibility settings, and persistent compiler state such as
`recordId` allocation.

Compiler Context is infrastructure, not semantic state. Semantic information
flows through the pass input and output representations.

Each pass specifies:

* purpose
* input representation
* output representation
* consumes, produces, and required-by dependencies
* assumptions on input
* invariants established on output
* diagnostics emitted
* why the pass exists separately

This document does not define implementation classes, C++ APIs, storage formats,
or backend APIs.

---

## YAML Frontend Migration Path

During the migration to the normative YAML `.brd` format, YAML source files
flow through:

```text
YAML source
    |
    v
YamlParser
    |
    v
YamlDocument
    |
    v
schema decoder
    |
    v
source-schema normalization
    |
    v
Symbol Model / Semantic Model
```

The existing legacy declaration parser remains available for transitional test
coverage and produces the AST-shaped input consumed by legacy parser-facing
pass overloads. The YAML frontend does not replace the legacy parser; it feeds
a normalized source-schema model into the same downstream semantic and layout
pipeline through an explicit normalization boundary.

Scalar- and enum-shaped YAML schemas can currently traverse the complete
legacy pipeline. Bounded-variable arrays are preserved through YAML decoding,
source-schema normalization, semantic validation, and Schema IR, but
downstream layout and runtime policy still do not interpret the full
bounded-array contract yet.

The `compiler/frontend` orchestration layer now exposes a production-facing
import-free YAML compilation path through validated Schema IR. That layer now
builds symbols, semantic state, and Schema IR directly from the normalized
source-schema model. AST remains in use only for the independent legacy
declaration-syntax pipeline. The legacy declaration parser remains available
during the migration.

---

## 1. Parser

### Purpose

The legacy parser converts declaration-syntax schema source text into a
syntax-oriented AST.

This pass understands source syntax only. It does not resolve names, validate
types, build namespaces, compute layout, or assign compiler-managed identifiers.

### Input Representation

* source text

### Output Representation

* AST

### Consumes / Produces / Required by

Consumes:

* source text

Produces:

* AST

Required by:

* Import Resolver

### Assumptions on Input

The input is a source text unit selected for compilation or import resolution.
The parser does not assume that the source is syntactically valid.

### Responsibilities

* perform lexical analysis
* parse tokens into AST nodes
* attach source locations to AST nodes
* ignore comments for semantic purposes
* preserve source structure needed by later diagnostics
* avoid semantic validation

### Invariants Established on Output

* every emitted AST is syntactically valid
* every AST node has source location metadata where practical
* syntax-level import declarations remain represented in the AST
* comments do not affect compiler semantics
* no semantic names or type references are resolved

### Diagnostics Emitted

* syntax errors
* unexpected tokens
* malformed literals
* unterminated strings or blocks
* invalid token sequences

### Why This Pass Exists Separately

Parsing is source-syntax work. Keeping it separate prevents syntax recovery,
token handling, and source-location logic from leaking into semantic analysis
or layout computation.

---

## 2. Import Resolver

### Purpose

The import resolver currently acts as a lightweight AST grouping boundary for
the legacy declaration-syntax pipeline.

Imports are source-level compiler directives only. They remain AST-owned until
the repository defines a dedicated import-graph boundary.

### Input Representation

* ASTs

### Output Representation

* Compilation Unit

### Consumes / Produces / Required by

Consumes:

* AST

Produces:

* Compilation Unit

Required by:

* Namespace Builder

### Assumptions on Input

All input ASTs are syntactically valid. Import declarations are represented as
source-level AST nodes.

### Responsibilities

* preserve the input AST documents in a deterministic compilation unit
* provide the current boundary for future import-graph work

### Invariants Established on Output

* input AST document order is preserved
* input AST identity is preserved by pointer
* import declarations remain AST syntax

### Diagnostics Emitted

* none in the current implementation

### Why This Pass Exists Separately

This pass exists to preserve the legacy declaration-syntax pipeline boundary
for future multi-file import work. It keeps AST grouping separate from
namespace construction and semantic validation even though it does not yet
resolve an import graph.

---

## 3. Namespace Builder

### Purpose

The namespace builder converts the Compilation Unit or the normalized
source-schema document into the Symbol Model.

This pass establishes logical declaration identity using fully qualified names.

### Input Representation

* Compilation Unit
* normalized source-schema document

### Output Representation

* Symbol Model

### Consumes / Produces / Required by

Consumes:

* Compilation Unit

Produces:

* Symbol Model

Required by:

* Semantic Validator

### Assumptions on Input

All ASTs are syntactically valid and all imports required for the compilation
have been resolved. For the normalized source-schema path, the schema has
already had unsupported YAML imports rejected and normalized identifiers
validated.

### Responsibilities

* construct the namespace hierarchy
* register every top-level declaration
* assign fully qualified names
* detect duplicate definitions
* detect namespace conflicts
* attach source-origin metadata to registered declarations

### Invariants Established on Output

* every declaration has a unique logical identity
* every registered declaration has a fully qualified name
* namespace nesting is represented canonically
* duplicate symbols have been rejected
* source-level imports are no longer needed by downstream passes

### Diagnostics Emitted

* duplicate symbol
* duplicate namespace definition, when illegal by policy
* conflicting declaration
* invalid namespace declaration
* declaration outside an allowed namespace context

### Why This Pass Exists Separately

Name registration must happen before type checking. Separating namespace
construction from semantic validation allows the compiler to report duplicate
and conflicting declarations before resolving references between declarations.

---

## 4. Semantic Validator

### Purpose

The semantic validator checks that the schema is meaningful according to the
Breadcrumbs schema language and converts either the legacy AST path or the
normalized source-schema model into the Semantic Model.

### Input Representation

* Symbol Model
* normalized source-schema document

### Output Representation

* Semantic Model

### Consumes / Produces / Required by

Consumes:

* Symbol Model

Produces:

* Semantic Model

Required by:

* Layout Computation

### Assumptions on Input

All declarations have fully qualified names, duplicate top-level symbols have
been rejected, and the namespace hierarchy is canonical.

### Responsibilities

* resolve all type references
* resolve record references
* resolve enum references
* validate field declarations
* validate bounded arrays defined by the schema-language specification
* validate string and bytes bounds
* construct resolved field-type values for valid fields
* validate typed constraints and annotations supported by the language
* reject unsupported language constructs
* reject user-authored compiler-managed identifiers
* detect illegal recursion
* enforce schema language rules

Unsupported constructs include inheritance, schema-defined defaults, and
user-visible field or record IDs unless a future specification explicitly adds
them.

### Invariants Established on Output

* the schema is semantically correct
* every reference is resolved
* every field has a resolved field type
* no unresolved names remain
* unsupported constructs have been rejected
* the model is independent of source import directives
* source-origin metadata is preserved for diagnostics and tooling

### Diagnostics Emitted

* undefined type
* invalid reference
* unsupported inheritance
* unsupported default value
* unsupported user-authored ID
* recursive definition
* illegal recursion
* invalid array declaration
* invalid string or bytes bounds
* invalid annotation
* type mismatch
* language rule violation

### Why This Pass Exists Separately

Semantic validation establishes meaning independent of source syntax. Layout
computation and Schema IR generation should never need to recover from
unresolved names, invalid field types, or unsupported language constructs.

---

## 5. Layout Computation

### Purpose

Layout computation derives the Layout Model from the Semantic Model.

This pass describes how records are represented as sparse binary records. It
does not emit runtime bytes and does not perform backend generation.

### Input Representation

* Semantic Model

### Output Representation

* Layout Model

### Consumes / Produces / Required by

Consumes:

* Semantic Model

Produces:

* Layout Model

Required by:

* Schema IR Builder

### Assumptions on Input

The Semantic Model is valid. All references are resolved, all field types are
known, and unsupported language constructs have already been rejected.

### Responsibilities

* compute binary layout information
* assign or preserve `recordId` metadata as required by compiler state
* assign or preserve `fieldIndex` metadata for fields
* compute sparse record directory metadata
* compute field value encoding metadata
* compute value metadata required for binary layout and bounded collections
* compute reference encoding metadata
* enforce field count limits
* enforce layout size limits
* produce deterministic ordering for the Layout Model

### Invariants Established on Output

* every record has complete binary layout information
* every record has a compiler-managed `recordId`
* every field has a compiler-managed `fieldIndex`
* `fieldIndex` values fit within the binary record format limits
* sparse record directory metadata is complete
* field value encodings are known
* Layout Model is deterministic for the same validated input and compiler
  state

### Diagnostics Emitted

* layout overflow
* impossible layout
* unsupported construct in layout
* too many fields for one record
* invalid compiler-managed identifier state
* incompatible layout change, when compatibility analysis is enabled

### Why This Pass Exists Separately

Binary layout is derived from semantic meaning but is not itself semantic
meaning. Keeping layout computation separate prevents field encoding,
`recordId`, `fieldIndex`, sparse directory details, and size limits from
polluting semantic validation.

---

## 6. Schema IR Builder

### Purpose

The Schema IR builder produces the final protobuf Schema IR consumed by
compiler backends.

Schema IR is emitted only after successful semantic validation and layout
computation.

### Input Representation

* Semantic Model
* Layout Model

### Output Representation

* `schema_ir.proto` representation

### Consumes / Produces / Required by

Consumes:

* Semantic Model
* Layout Model

Produces:

* Schema IR

Required by:

* Backends

### Assumptions on Input

The Semantic Model is valid and the Layout Model is complete. Earlier
diagnostics that prevent compilation have already stopped the pipeline.

### Responsibilities

* produce the final protobuf Schema IR
* populate every required Schema IR field
* preserve source locations where appropriate
* lower field types from resolved Semantic Model values
* include compiler-only metadata needed by backends and tools
* represent resolved references using compiler-assigned object handles
* emit deterministic output where required by tooling
* avoid re-running semantic analysis

### Invariants Established on Output

* Schema IR is backend-ready
* Schema IR contains no import objects
* all references are resolved
* semantic IR objects have graph-local handles
* value objects do not have handles
* source metadata is compiler-only
* runtime and binary artifacts can be produced by backends without semantic
  analysis

### Diagnostics Emitted

* internal consistency failure
* missing required Schema IR field
* invalid unresolved reference in compiler state
* invalid Layout Model in compiler state

### Why This Pass Exists Separately

Schema IR is the backend-facing contract. Building it in a final pass keeps the
backend model stable while allowing earlier compiler passes to use internal
representations optimized for parsing, validation, and layout computation.

---

# Compiler Data Flow

```text
Source Files
    |
    v
AST
    |
    v
Compilation Unit
    |
    v
Symbol Model
    |
    v
Semantic Model
    |
    v
Layout Model
    |
    v
Schema IR
    |
    v
Backends
```

---

# Design Principles

Each pass has a single responsibility.

Passes are deterministic.

Passes are side-effect free where practical.

Each pass produces a new representation rather than mutating the previous one.

Each pass establishes stronger invariants than the previous pass. Downstream
passes may rely on these invariants without repeating earlier validation.

Later passes never recover from earlier semantic failures.

Diagnostics are emitted as early as possible.

Schema IR is produced only after successful validation and layout computation.

Backends never perform semantic analysis.

Imports are resolved before namespace construction and do not survive into
semantic, layout, Schema IR, or binary representations.

Compiler-managed identifiers are assigned and preserved by compiler passes, not
by schema authors.

Source metadata is preserved for diagnostics and tooling, but runtime and
binary artifacts contain only runtime metadata.

---

# Compilation Failure Policy

Passes may emit multiple diagnostics before terminating.

Fatal diagnostics prevent downstream passes from executing.

Later passes may assume all invariants established by earlier passes.

A pass never attempts to recover from violations of earlier-pass invariants.

Internal compiler errors indicate compiler defects rather than user errors.

Schema IR is produced only if all compilation passes complete successfully.

Backends are never invoked after fatal compilation failures.
