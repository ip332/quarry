# Diagnostics

# Goals

Compiler diagnostics help schema authors identify and fix problems in
Quarry schema files.

The diagnostics architecture should:

* produce deterministic, high-quality error messages
* identify source locations precisely
* support command-line compilation
* support CI systems
* support IDE and editor integrations
* remain independent of compiler implementation details

Diagnostics are part of the compiler user experience. They should explain what
went wrong, where it happened, and what the author can do next when a useful
fix is known.

---

# Diagnostic Model

A diagnostic contains:

* stable diagnostic ID
* severity
* primary message
* primary source location
* optional source range
* optional related locations
* optional notes
* optional suggested fix
* compiler pass that emitted it

Diagnostics are immutable once emitted.

The primary message describes the problem. The primary source location points to
the most relevant source position. Related locations, notes, and suggested fixes
provide additional context without changing the core diagnostic.

The compiler pass name identifies where the diagnostic originated. It is
compiler metadata for users and tools; it does not affect schema semantics.

---

# Severity

## Error

An error reports invalid user input that prevents successful compilation.

Errors are fatal for downstream compiler passes. A pass may emit multiple
independent errors before terminating, but later passes must not rely on invalid
representations.

## Warning

A warning reports a valid construct that is suspicious, discouraged, or likely
to cause future problems.

Warnings do not prevent compilation by default. Compilation policy may allow
warnings to be promoted to errors.

## Note

A note provides additional context for another diagnostic.

Notes do not affect compilation. They are attached to errors, warnings, or
internal compiler errors to improve explanation.

## Internal Compiler Error

An internal compiler error reports a compiler defect rather than a schema author
mistake.

Internal compiler errors prevent successful compilation. They should include
enough context to support debugging the compiler without presenting the issue as
a schema language error.

---

# Source Locations

Source locations identify where a diagnostic applies.

A source location includes:

* file
* line
* column
* optional byte offset

A diagnostic may also include a source range. A source range identifies a span
of source text rather than a single point.

Source locations and ranges should be preserved throughout compiler passes.
Later passes may emit diagnostics using source metadata attached to AST nodes,
semantic objects, layout metadata, or Schema IR objects.

When a diagnostic is not tied to a specific source span, it should still include
the most useful available context, such as the source file, declaration, or
compiler pass.

---

# Related Locations

Some diagnostics are only clear when multiple source locations are shown
together.

Examples include:

* duplicate symbol
* previous definition
* conflicting declaration
* incompatible declaration
* invalid reference and referenced declaration

Related locations give supporting context. They should not replace the primary
source location. The primary location remains the location most directly
responsible for the diagnostic.

For a duplicate symbol diagnostic, the primary location may point to the later
definition and a related location may point to the previous definition.

---

# Diagnostic IDs

Every diagnostic should have a stable diagnostic ID.

Quarry diagnostic IDs use a stable project-specific scheme such as:

```text
BC1001
```

The exact numeric ranges may be defined by future diagnostic catalogs. IDs
should remain stable across compiler versions whenever practical.

Stable IDs allow:

* documentation lookup
* CI filtering
* editor integration
* warning policy configuration
* regression testing

Diagnostic messages may improve over time, but the ID should continue to refer
to the same class of problem whenever practical.

---

# Diagnostic Emission

Diagnostics should be emitted as early as possible.

A pass may emit multiple independent diagnostics before terminating. This helps
schema authors fix several unrelated problems in one edit cycle.

Later passes never recover from earlier fatal errors. If a pass cannot
establish its output invariants, downstream passes must not execute.

Diagnostics should avoid cascading failures. Once the compiler knows that a
representation is invalid, it should avoid emitting speculative diagnostics
that are likely caused by the original failure.

When possible, diagnostics should distinguish between user errors and compiler
defects. User errors are reported as errors or warnings. Compiler defects are
reported as internal compiler errors.

---

# Diagnostic Lifecycle

Compiler passes create diagnostics.

Diagnostics are collected by the Compiler Context.

Diagnostics are formatted only after compilation completes or compilation
fails.

Compiler passes never inspect or modify diagnostics emitted by earlier passes.

---

# Diagnostic Ordering

Diagnostics should be emitted in deterministic order.

Unless otherwise specified, diagnostic ordering should be based on:

* source file
* source location
* compiler pass
* diagnostic ID

Deterministic ordering improves reproducibility, regression testing, CI systems,
and IDE behavior.

---

# Output Formats

The diagnostics model is independent of output format.

Conceptual output formats include:

* human-readable console output
* structured machine-readable output, such as JSON
* IDE and editor integration

Human-readable output should be concise and actionable.

Structured output should preserve diagnostic IDs, severity, messages, source
locations, ranges, related locations, notes, suggested fixes, and compiler pass
metadata.

IDE integrations should be able to map diagnostics to source ranges and display
related locations when available.

This document does not define serialization details.

---

# Design Principles

Diagnostics are deterministic.

Diagnostic locations are precise.

Messages are actionable.

Cascading diagnostics are minimized.

Diagnostic IDs are stable whenever practical.

Diagnostics remain independent of compiler implementation details.

Diagnostics are emitted as early as possible.

Related locations are used when they materially improve usability.

Suggested fixes are provided only when the compiler can identify a safe and
specific correction.
