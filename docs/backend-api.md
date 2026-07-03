# Backend API

# Goals

Compiler backends transform Schema IR into output artifacts.

Backends do not perform parsing, semantic analysis, layout computation, or
identifier assignment.

The purpose of this document is to define what every backend may rely upon and
which responsibilities remain with the backend.

This document defines architectural responsibilities only. It does not describe
C++ classes, protobuf APIs, virtual interfaces, plugin mechanisms, or backend
implementation details.

---

# Inputs

The input to every backend is Schema IR.

Schema IR is the only compiler representation visible to backends.

Backends never consume:

* ASTs
* Semantic Models
* Layout Models
* compiler state

Backends may consume backend configuration, but backend configuration must not
change schema semantics or layout meaning.

---

# Schema IR Guarantees

Schema IR guarantees:

* all references are resolved
* all semantic validation has completed
* all layout computation has completed
* compiler-managed identifiers are assigned
* layout metadata is complete
* no unresolved names remain
* no import objects remain
* deterministic ordering is preserved
* source metadata is available only for tooling and diagnostics

Backends may assume these guarantees without repeating validation.

---

# Backend Responsibilities

A backend is responsible for:

* traversing Schema IR
* generating target-specific artifacts
* preserving semantic meaning
* preserving layout metadata where required
* reporting backend-specific failures

Examples of backend artifacts include:

* generated source code
* reflection metadata
* documentation
* schema manifests
* test artifacts

Backends decide how to represent Schema IR concepts in their target artifact,
provided they do not change schema semantics or layout meaning.

---

# Backend Constraints

Backends must not:

* perform semantic analysis
* resolve names
* compute layout
* assign `recordId` values
* assign `fieldIndex` values
* modify Schema IR
* depend on parser or AST structures

Backends are pure consumers of Schema IR.

---

# Determinism

Given identical Schema IR and identical backend configuration, a backend must
produce identical output.

Backends should avoid dependence on:

* hash map iteration order
* filesystem enumeration order
* system time
* random number generation

Deterministic output enables reproducible builds and regression testing.

---

# Diagnostics

Compiler diagnostics originate in compiler passes.

Backends may emit diagnostics only for backend-specific failures, such as:

* unsupported target language feature
* template generation failure
* output filesystem failure
* invalid backend configuration

Backends must not emit semantic diagnostics.

Backend diagnostics should use source metadata from Schema IR when that metadata
improves the user-facing message. Source metadata remains tooling and
diagnostic metadata; it does not affect generated semantics.

---

# Extensibility

New backends may be added without modifying the compiler pipeline.

Every backend consumes the same Schema IR contract.

Future backends should remain independent of parser, semantic analysis, and
layout implementation.

Adding a backend may require backend-specific configuration or output policy,
but it must not require changes to compiler passes or Schema IR semantics.

---

# Design Principles

Schema IR is the compiler/backend boundary.

Backends are pure transformations.

Compiler semantics are frozen before backend execution.

Backends are deterministic.

Backends are independent of compiler implementation.

Multiple backends may consume the same Schema IR.

Adding a backend does not require changes to compiler passes.

Backends preserve schema semantics and layout meaning.
