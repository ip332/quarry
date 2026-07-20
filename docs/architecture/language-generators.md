# Language Generators

## Purpose

The Schema Compiler is language-independent. Its responsibility is to parse
schemas, validate them, compute binary layouts, preserve record identities, and
produce a language-neutral intermediate representation (IR).

The IR is an internal compiler model that captures the complete semantics of the
schema. It may exist only in memory or may be persisted as an implementation
detail.

Language generators consume this IR and produce idiomatic bindings for
individual programming languages.

---

## Compiler Pipeline

The language generation pipeline is:

```text
.brd files
        |
        v
Schema Compiler
        |
        v
Intermediate Representation (IR)
        |
        +-- C Generator
        +-- C++ Generator
        +-- Rust Generator
        +-- Go Generator
        +-- Java Generator
        +-- C# Generator
        +-- Python Generator
```

All generators consume exactly the same IR to ensure identical semantics across
languages.

Each language generator produces only language-specific artifacts. Generated
source code is a derived representation of the schema and is never considered
the authoritative definition of the schema itself.

---

## Namespace Mapping

Namespaces are logical schema concepts.

Each language generator maps the logical schema namespace into the idiomatic
namespace mechanism provided by the target language.

| Language | Namespace Representation |
|----------|--------------------------|
| C | Symbol prefix |
| C++ | namespace |
| Rust | Module hierarchy |
| Go | Package |
| Java | Package |
| C# | Namespace |
| Python | Package / module hierarchy |

C has no native namespace mechanism, so generators normalize the namespace into
a symbol prefix.

Example schema namespace:

```text
quarry.telemetry
```

Example generated C symbol prefix:

```text
quarry_telemetry_
```

---

## Responsibilities

A language generator is responsible for:

* producing idiomatic code for the target language
* preserving schema semantics
* using record identities assigned by the Schema Compiler
* mapping namespaces appropriately
* avoiding symbol collisions
* generating documentation comments when available
* faithfully implementing the binary layout computed by the Schema Compiler

---

## Non-Responsibilities

Language generators do not:

* parse schema files
* resolve imports
* compute layouts
* assign record IDs
* perform compatibility analysis

Those responsibilities belong to the Schema Compiler.

---

## Design Principles

One schema produces equivalent APIs in every language.

Generated code must feel natural for each target language.

Binary compatibility is determined by the Schema Compiler, not by language
generators.

Language generators translate logical schema concepts into language-specific
constructs without changing their meaning.
