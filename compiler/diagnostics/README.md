# Diagnostics

Owns diagnostic IDs, severity, diagnostic objects, collection, deterministic
ordering, and basic human-readable formatting.

## Diagnostic Model

A diagnostic contains:

* stable diagnostic ID
* severity
* primary message
* optional primary source location
* optional source range
* optional related locations
* optional notes
* optional suggested fix
* compiler pass name

Diagnostics use `compiler/support` source primitives. They do not store line or
column numbers. Formatters derive line and column values from `SourceManager`.

Diagnostics are immutable after construction. Callers build diagnostics through
`Diagnostic::Builder` and emit the resulting value into `DiagnosticEngine`.

## Severity

Severity values are:

* `Error`
* `Warning`
* `Note`
* `InternalCompilerError`

`to_string(Severity)` returns stable, deterministic lowercase strings for
formatting and tests.

## Diagnostic IDs

`DiagnosticId` is a compact wrapper for stable project IDs such as `BC1001`.
The current implementation accepts the `BC` prefix followed by four digits.

Invalid IDs are represented explicitly by the default value or
`DiagnosticId::invalid()`. The implementation does not define a diagnostic
catalog yet.

## Related Locations

`RelatedLocation` attaches supporting source context to a diagnostic. A related
location can point at either a `SourceLocation` or a `SourceRange` and always
has a message.

Related locations are used for context such as previous definitions, referenced
declarations, or conflicting source spans. They do not replace the diagnostic's
primary location.

## Diagnostic Engine

`DiagnosticEngine` is the collection point for compiler diagnostics. It:

* collects diagnostics
* preserves insertion order for direct access
* exposes diagnostics read-only
* provides deterministic sorted views
* counts errors, warnings, and internal compiler errors
* reports whether fatal diagnostics are present
* can be cleared for tests or repeated runs

Compiler passes should emit diagnostics and then treat emitted diagnostics as
immutable.

## Ordering

The sorted view orders diagnostics deterministically by:

* resolved source path
* byte offset
* compiler pass name
* diagnostic ID

Diagnostics with resolvable source locations sort before diagnostics without
locations. Insertion order is preserved as the final tie-breaker.

## Formatting

`DiagnosticFormatter` provides basic human-readable output. Formatting includes:

* file, one-based line, and one-based column when `SourceManager` can resolve
  the location
* severity
* diagnostic ID
* primary message
* compiler pass name when present
* related locations
* notes
* suggested fix text

Unknown or unresolved locations are formatted as `<unknown>`.

Structured output is intentionally not implemented in this PR.

## Dependency Restrictions

Allowed dependencies:

* C++ standard library
* `compiler/support`

Diagnostics infrastructure must not depend on parser, AST, imports, symbols,
semantic validation, layout computation, Schema IR construction, or backends.
