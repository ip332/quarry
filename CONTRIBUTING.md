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

## Pre-push validation

Every pushed commit must pass the full `debug` configure, build and test cycle
on your machine first. `git push` opens its connection before running hooks,
and the full cycle takes longer than GitHub keeps an idle connection open, so
validation happens before the push and leaves a receipt that the pre-push hook
checks in under a second (issue #89).

Set up once per clone:

```sh
pre-commit install                     # pre-commit checks (formatting, whitespace, YAML)
scripts/quarry-validate install-hook   # pre-push receipt check
```

Then, for each push:

```sh
scripts/quarry-validate push -u origin my-branch   # validates what the push needs, then pushes
```

or in two steps:

```sh
scripts/quarry-validate validate        # HEAD; also accepts any revisions
git push -u origin my-branch
```

`scripts/quarry-validate status` shows whether a commit has a valid receipt and
why not. Validation runs in a separate worktree checked out at the exact
commit, so uncommitted and untracked files never affect it. Parallelism is set
with `--build-jobs`/`--test-jobs` or `QUARRY_VALIDATE_BUILD_JOBS` /
`QUARRY_VALIDATE_TEST_JOBS`. The full workflow, receipt rules and recovery
steps are in `docs/development-environment.md`.

**Do not use `git push --no-verify`** (or other ways of skipping hooks, such as
`core.hooksPath` overrides) unless a maintainer has explicitly authorized it
for that specific push. If validation cannot pass, fix the cause or ask; a
receipt is never needed for deleting a remote branch.

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
