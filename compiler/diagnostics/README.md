# Diagnostics

Owns diagnostic IDs, severity, diagnostic objects, collection, and formatting
placeholders.

Responsibilities:

* represent diagnostics emitted by compiler passes
* collect diagnostics deterministically
* model severity and diagnostic identity
* provide formatting placeholders

Allowed dependencies:

* `compiler/support`

Diagnostics infrastructure must not depend on compiler passes.
