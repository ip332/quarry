# Breadcrumbs Specification Authoring Guide

## Purpose

This document defines how technical specifications are written within the Breadcrumbs project.

Its purpose is to ensure that all specifications use consistent terminology, structure, and normative language.

This guide is intended for:

* project contributors
* reviewers
* AI-assisted authoring tools
* future maintainers

---

# Architecture vs Specification

Breadcrumbs distinguishes between architecture and specifications.

## Architecture

Architecture documents answer:

> **What is the system?**

Architecture documents define:

* principles
* major components
* responsibilities
* relationships
* long-term design decisions

Architecture intentionally avoids implementation details.

---

## Specifications

Specifications answer:

> **Exactly how does a component behave?**

Specifications define:

* syntax
* semantics
* algorithms
* binary formats
* compiler behavior
* runtime behavior
* compatibility rules

A specification should be precise enough that two independent developers can produce compatible implementations.

---

# Specification Structure

Every specification should contain the following sections.

## Required

* Purpose
* Scope
* Terminology
* Requirements
* Examples
* Related Documents

## Optional

Include these sections only when they add value:

* Validation
* Compatibility
* Security Considerations
* Performance Considerations
* Implementation Notes
* Open Questions

Avoid creating empty sections.

---

# Normative Language

Breadcrumbs follows the terminology defined by RFC 2119.

The following words have specific meanings:

* **SHALL** — mandatory behavior
* **SHOULD** — recommended behavior
* **MAY** — optional behavior
* **MUST NOT** — prohibited behavior

Only numbered requirements are normative.

All other text is explanatory.

---

# Requirement IDs

Every normative requirement shall have a unique identifier.

Format:

```text
REQ-<SPEC>-NNN
```

Examples:

```text
REQ-SL-001
REQ-SL-002

REQ-BRF-014

REQ-RAPI-101
```

Requirement identifiers are permanent.

Once published, an identifier shall never be reused.

---

# Writing Requirements

A requirement should express one behavior.

Good:

```text
REQ-SL-001

A schema SHALL define exactly one record.
```

Poor:

```text
Schemas shall define one record, use version numbers, and support imports.
```

Keep requirements:

* precise
* testable
* implementation independent

---

# Examples

Examples improve understanding but are **not normative**.

Examples should:

* illustrate requirements
* remain concise
* avoid introducing new behavior

Whenever useful, distinguish between:

* Valid Example
* Invalid Example
* Informative Example

---

# Terminology

Use project terminology consistently.

Preferred terms:

* deviceId
* record
* schema
* envelope
* payload
* schema compiler
* Breadcrumbs Agent
* Breadcrumbs Cloud

Avoid introducing synonyms for established terms.

---

# Cross References

Specifications should reference related documents rather than duplicate them.

For example:

* Schema Language → Schema Model
* Binary Record Format → Schema Language
* Runtime API Contract → Binary Record Format

Each concept should have a single authoritative definition.

---

# Separation of Concerns

A specification should define only its own responsibility.

For example:

The Schema Language specification defines:

* syntax
* semantics

It does not define:

* binary encoding
* runtime APIs
* transport protocols

Similarly:

The Binary Record Format defines encoding.

It does not redefine schema syntax.

---

# Documentation Principles

Specifications should describe:

* facts
* structure
* behavior
* constraints

Specifications should not describe:

* business logic
* application policies
* user interface behavior

Keep documents focused and cohesive.

---

# Review Checklist

Before publishing a specification, verify that:

* the document has a clear purpose
* requirements are uniquely identified
* only numbered requirements are normative
* terminology matches the project glossary
* examples are illustrative rather than normative
* duplicated concepts have been replaced with cross references
* the specification remains focused on a single responsibility

---

# Evolution

Specifications are expected to evolve.

Changes should preserve compatibility whenever practical.

Breaking changes should be documented explicitly and coordinated with the corresponding compatibility specification.

The goal is to evolve the Breadcrumbs platform while maintaining a stable and understandable specification set.
