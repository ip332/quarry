# Contributing to Quarry

Quarry is a deterministic schema compiler and binary serialization framework
with C++, strict-C99 C, and Python backends. Contributions should preserve its
language-neutral wire contract and embedded-friendly behavior.

## Development principles

Investigate the existing architecture and tests before implementing a change.
Prefer small, focused, reviewable pull requests and preserve backward
compatibility. Discuss large features or architectural changes in an issue
before starting work.

Schema IR, BRF, runtime APIs, and generated-code API epochs are
compatibility-sensitive. Do not change them without prior discussion and
explicit compatibility analysis.

Follow the existing C++, C99, Python, CMake, and Markdown conventions. Keep
generated files and unrelated working-tree changes out of commits.

## Validation

Run the checks relevant to the change. At minimum, run focused tests and:

```sh
git diff --check
```

Use the documented native or Docker presets for broader validation. Changes to
generated code should include generation and consumer/interoperability checks
where applicable.

## Pull requests

Commit focused work with a concise subject. The pull request should explain
the behavior changed, design decisions, compatibility impact, and validation
performed. Keep commits reviewable; do not mix formatting or unrelated cleanup
with feature work.

Before requesting review, confirm:

- tests were added or updated for behavioral changes;
- relevant documentation was updated;
- backward compatibility and API epochs were considered;
- no unnecessary public API or wire-format changes were introduced;
- validation completed successfully;
- `git diff --check` passed.
