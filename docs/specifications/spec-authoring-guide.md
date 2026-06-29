# <Specification Title>

## Status

Draft

## Version

0.1

## Purpose

Describe the purpose of this specification.

Explain what implementation-level behavior this specification defines and which
architecture document it refines.

## Scope

### In Scope

List the behaviors, formats, APIs, or rules covered by this specification.

### Out of Scope

List related topics that are intentionally defined elsewhere.

## Terminology

Define terms used by this specification.

Use shared Breadcrumbs terminology consistently:

* deviceId
* record
* schema
* envelope
* payload
* schema compiler
* Breadcrumbs Agent
* Breadcrumbs Cloud

## Requirements

Normative requirements SHALL use IDs in this format:

```text
REQ-<SPEC>-NNN
```

Example:

```text
REQ-SPEC-ID-001: The implementation SHALL define a stable behavior.
```

Use the following normative language:

* SHALL: required behavior.
* SHOULD: recommended behavior with valid exceptions.
* MAY: optional behavior.
* MUST NOT: prohibited behavior.

Requirements:

* REQ-<SPEC>-001: ...
* REQ-<SPEC>-002: ...
* REQ-<SPEC>-003: ...

## Validation Rules

Define how implementations validate inputs, outputs, files, messages, or
generated artifacts covered by this specification.

For `.brd` files, validation is performed by the Breadcrumbs schema compiler.
Do not introduce JSON Schema or YAML meta-specifications for `.brd` validation.

Validation rules:

* REQ-<SPEC>-100: ...
* REQ-<SPEC>-101: ...

## Compatibility Rules

Define compatibility requirements for versioning, evolution, migration, and
backward or forward compatibility.

Compatibility rules:

* REQ-<SPEC>-200: ...
* REQ-<SPEC>-201: ...

## Examples

Examples should be concise and focused.

Label examples as valid, invalid, illustrative, or non-normative.

Examples must not introduce behavior that is not defined by requirements.

### Valid Example

```text
...
```

### Invalid Example

```text
...
```

## Cross References

Link related architecture documents and specifications.

Use cross references instead of duplicating definitions.

Related architecture documents:

* `../architecture/<document>.md`

Related specifications:

* `<specification>.md`

## Open Questions

List unresolved questions that must be answered before the specification is
considered complete.

Open questions:

* ...

## Reviewer Checklist

Reviewers should verify:

* Required sections are present.
* Requirement IDs use `REQ-<SPEC>-NNN`.
* Requirement IDs are stable and not reused.
* Normative language is limited to requirements.
* Validation rules are explicit.
* Compatibility rules are explicit.
* `.brd` validation remains the responsibility of the schema compiler.
* No JSON Schema or YAML meta-specification has been introduced.
* Examples are labeled and do not create implicit requirements.
* Cross references avoid duplicated explanations.
* Open questions are unresolved and actionable.
