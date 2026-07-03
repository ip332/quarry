# ADR 0001: Schema IR References

## Status

Proposed

## Context

Schema IR is the compiler's stable, backend-facing representation of one fully
compiled schema.

Schema IR contains resolved references only. References appear in places such
as:

* `FieldType` to `RecordIR`
* `FieldType` to `EnumIR`

The project needs a reference representation that works well for protobuf,
compiler backends, tooling, debugging, documentation generation, and future
compiler evolution.

This ADR compares options. It does not modify the current protobuf schema.

---

## Option 1: Compiler-Assigned Object Handles

Example:

```proto
message RecordRef {
  uint64 record_handle = 1;
}
```

Each referenced semantic IR object has a compiler-assigned object handle.
References store that handle.

Object handles are graph-local references. They are not schema identity, are not
part of the schema language, and must not be confused with `recordId`.

### Evaluation

Simplicity: Object handles are simple for backends to compare, store, and
resolve.

Implementation complexity: The compiler must allocate handles and maintain a
handle to object table while constructing Schema IR.

Protobuf friendliness: Numeric scalar fields are natural in protobuf, compact,
and efficient.

Serialization: Object handles serialize cleanly. A serialized Schema IR
document can be loaded and reference-resolved by rebuilding a handle table.

Deterministic compilation: Handles can be assigned deterministically within one
compiled Schema IR graph if serialized debug/tooling output needs stable output.

Incremental compilation: Handles are graph-local. Incremental compilers may
rebuild or remap handles internally without changing schema identity.

Tooling: Tools can resolve handles quickly, but raw handle values are less
readable than names. Tooling may need to display both handle and fully qualified
name.

Debugging: Object handles are concise but not self-explanatory. Debug output
should include resolved names alongside handles.

Documentation generation: Documentation generators can resolve handles to
objects and render names. They should not expose object handles as user-facing
schema identity.

Compiler performance: Object handles are efficient. Resolution can be O(1) with
a handle map.

Future evolution: Object handles are flexible if kept compiler-only. They can
survive refactors of namespace representation and can reference future object
kinds.

Compatibility with multiple compiler backends: Object handles provide a uniform
reference mechanism for all backends, independent of backend language.

### Recommended Handle Semantics

Schema IR uses compiler-assigned object handles with the following semantics:

* Handles are opaque graph-local identifiers.
* Handle numeric values have no semantic meaning.
* Handles exist only to represent references inside one Schema IR graph.
* Equality comparison is valid.
* Ordering comparisons are meaningless.
* Backends must not derive ordering or identity from handle values.
* Handles are not schema identity and must not be used as user-facing
  identifiers.
* Handles are compiler implementation details.
* Handles may be serialized inside debug/tooling Schema IR artifacts when those
  artifacts need reference resolution.
* Handles are compiler-only and must not be emitted into runtime binary
  artifacts.
* Backends resolve handles by building lookup tables from semantic IR objects
  before generation.

Schema IR uses a single graph-wide handle namespace. Every semantic IR object
receives a unique handle. Handles are unique across the entire Schema IR graph,
not per object type.

This keeps the reference model simple and allows future semantic object types
to be added without changing the reference mechanism.

Object handles are assigned only to semantic IR objects. Semantic IR objects
include:

* `SchemaIR`, if represented as a semantic object
* `NamespaceIR`
* `RecordIR`
* `EnumIR`
* `EnumValueIR`
* `FieldIR`

Value objects do not receive handles. Examples include:

* `FieldType`
* `ArrayType`
* `StringType`
* `BytesType`

Value objects are owned by semantic IR objects and are never referenced
independently. They exist only as part of another semantic IR object, have no
independent lifetime, and are never the target of references. Therefore, they do
not require handles.

---

## Option 2: Fully Qualified Names

Example:

```proto
message RecordRef {
  string fq_name = 1;
}
```

References store the fully qualified name of the target object.

### Evaluation

Simplicity: FQNs are easy for humans to read and debug.

Implementation complexity: The compiler must still maintain symbol tables and
resolve names, but references remain text after Schema IR construction.

Protobuf friendliness: Strings are straightforward in protobuf but larger and
slower to compare than object handles.

Serialization: FQNs serialize well and are self-describing.

Deterministic compilation: FQNs are deterministic if namespace and naming rules
are deterministic.

Incremental compilation: FQNs can help incremental compilers correlate objects
between runs, but rename handling still requires explicit schema semantics.

Tooling: FQNs are friendly for debug tools, documentation, and inspection.

Debugging: FQNs are the easiest option to inspect manually.

Documentation generation: Documentation generators can use FQNs directly.

Compiler performance: String lookup and comparison cost more than object-handle
lookup, though this may be acceptable for small schemas.

Future evolution: FQNs become awkward around explicit renames. They also conflate
reference identity with display identity unless handled carefully.

Compatibility with multiple compiler backends: FQNs are portable, but every
backend may need name lookup or string matching unless the compiler provides
resolved lookup tables.

### Risks

FQNs can make Schema IR feel less fully resolved because backends may need to
resolve strings again.

FQNs also make rename handling more sensitive. A record rename can preserve
`recordId`, but an FQN reference changes text.

---

## Option 3: Object Graph Indices

References point to objects by position within the serialized Schema IR graph.

For example, a record reference might point to the Nth record in a flattened
record table or to a path of child indexes through the namespace tree.

### Evaluation

Simplicity: Object graph indices are simple only when the graph layout is fixed.
They become difficult once the graph evolves.

Implementation complexity: The compiler and every backend must agree on the
exact traversal or indexing scheme.

Protobuf friendliness: Indices are easy to encode as integers, but protobuf does
not provide object identity or pointer semantics.

Serialization: Indices serialize compactly, but they are fragile if object order
changes.

Deterministic compilation: Deterministic output requires strict ordering rules
for every repeated field and graph traversal.

Incremental compilation: Indices are poor for incremental compilation because
inserting one object can shift many references.

Tooling: Indices are hard for tools to inspect without reconstructing the exact
graph traversal.

Debugging: Indices are not self-explanatory and are more difficult to debug than
object handles or FQNs.

Documentation generation: Documentation generators must resolve indices before
rendering names.

Compiler performance: Indices can be efficient after tables are built, but
maintaining stable indices adds complexity.

Future evolution: Indices are brittle as Schema IR grows new object kinds or
ownership shapes.

Compatibility with multiple compiler backends: Indices create a hidden contract
around graph ordering that every backend must implement exactly.

### Risks

Object graph indices couple references to serialization layout. That is a poor
fit for an IR intended to evolve independently from its serialized debug/tooling
form.

---

## Recommendation

Use compiler-assigned object handles for Schema IR references.

Object handles best match the goal that Schema IR contains resolved references
without forcing backends to re-resolve text names. They are protobuf-friendly,
compact, efficient, and easy for multiple compiler backends to consume.

FQNs should remain available on owned objects for diagnostics, generated
documentation, and debug display, but references should not rely on FQNs as the
primary mechanism.

These three concepts intentionally serve different purposes:

* `recordId` is the stable schema/runtime identifier.
* Object handle is the compiler-internal graph reference.
* Fully qualified name is the human-readable logical identifier.

Object graph indices should be avoided because they couple references to
serialized graph ordering and make evolution harder.

---

## Decision Deferred

The project should defer only architectural questions that affect serialized
Schema IR or backend behavior.

Open items:

* whether serialized debug/tooling Schema IR requires deterministic handle
  values

Until those questions are resolved, Schema IR should treat object handles as
compiler-only graph-local references.
