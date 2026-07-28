# Quarry Specifications

## Purpose

This directory contains formal Markdown specifications for Quarry.

Specifications define implementation-level "how" details that follow the stable
architecture decisions in `docs/architecture/`.

Architecture documents define the system model. Specification documents define
precise behavior, formats, APIs, validation rules, compatibility rules, and
examples.

---

# Specification Format

Quarry specifications are written in Markdown.

Do not define JSON Schema, YAML meta-specifications, or external schema formats
to validate Quarry `.brd` schema files.

Validation of `.brd` files is the responsibility of the Quarry schema
compiler.

Markdown specifications SHALL use the shared structure defined in
`spec-authoring-guide.md`.

---

# Required Sections

Every specification SHALL include these sections:

* Status
* Version
* Purpose
* Scope
* Terminology
* Requirements
* Validation Rules
* Compatibility Rules
* Examples
* Open Questions

Specifications MAY add additional sections when needed, but the required
sections SHOULD remain in the template order.

---

# Requirement IDs

Normative requirements SHALL use stable requirement IDs.

Requirement IDs use this format:

```text
REQ-<SPEC>-NNN
```

Where:

* `<SPEC>` is a short uppercase specification identifier.
* `NNN` is a three-digit number.

Examples:

```text
REQ-SCHEMA-LANG-001
REQ-RECORD-BINARY-014
REQ-RUNTIME-BINDINGS-103
```

Requirement IDs SHALL NOT be reused for different requirements.

Removed requirements SHOULD remain reserved or be marked obsolete so references
from issues, tests, and implementation notes remain stable.

---

# Normative Language

Specifications use the following normative terms:

* SHALL: required behavior.
* SHOULD: recommended behavior with valid exceptions.
* MAY: optional behavior.
* MUST NOT: prohibited behavior.

Use these terms only for normative requirements.

Explanatory text should avoid normative language unless it is part of a numbered
requirement.

---

# Examples

Examples SHOULD be small and focused.

Examples SHOULD identify whether they are:

* valid examples
* invalid examples
* illustrative examples
* non-normative examples

Examples MUST NOT introduce behavior that is not covered by requirements.

When an example depends on another specification, reference that specification
instead of repeating its rules.

---

# Cross References

Specifications SHOULD link to related architecture documents and specifications.

Cross references SHOULD be short and specific.

Use cross references to avoid duplicating definitions. Keep the full explanation
in the document that owns the concept.

Examples:

* A schema language specification may reference `../architecture/schema-model.md`.
* A binary record format specification may reference `../architecture/data-model.md`.
* Runtime binding specifications may reference `../architecture/schema-compiler.md`.

---

# Reviewer Checklist

Before approving a specification, reviewers SHOULD verify:

* The specification follows `spec-authoring-guide.md`.
* All required sections are present.
* Normative statements use requirement IDs.
* Requirement IDs follow `REQ-<SPEC>-NNN`.
* Requirement IDs are stable and not reused.
* Normative language is used consistently.
* `.brd` validation remains assigned to the schema compiler.
* No JSON Schema or YAML meta-specification has been introduced.
* Examples do not create new requirements accidentally.
* Compatibility rules are explicit.
* Related architecture documents are linked.
* Duplicate explanations are replaced with cross references.
* Open questions are limited to unresolved specification details.

---

# Current Specifications

* `schema-language.md`
* `binary-record-format.md`
* `schema-compiler.md`
* `manifest-format.md`

---

# Planned Specifications

Planned specifications include:

* generated runtime bindings
* runtime library APIs
* transport protocol
* registration API
* provisioning API
* certificate lifecycle
* telemetry ingestion
* OTA metadata records

Record envelope layout is already covered by `binary-record-format.md` (see
"Current Specifications" above), not planned future work.
