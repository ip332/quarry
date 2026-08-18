# Quarry Roadmap

Quarry is in maintenance mode around the v0.1 release line.

This roadmap records likely future directions. Items are not commitments and
may change based on user feedback, compatibility constraints, and investigation.

## Status of this roadmap

This document communicates current direction rather than a commitment or
release plan. Items describe investigations or possible future work. Every
significant change begins with an investigation, and implementation depends on
compatibility analysis, validation, and project priorities.

## Release discipline

The `v0.1.30` release candidate and final-release work are separate from the
post-release roadmap. The external feedback recorded below was received during
the `v0.1.30-rc.1` evaluation, but is not a blocker for that release. It does
not authorize changes to API epochs, Schema IR, BRF, compiler or runtime
behavior, packaging behavior, or release artifacts.

## Near-term post-release priorities

- Release engineering and artifact hygiene.
- Documentation and first-time-user usability.
- Clearer diagnostics and reported-defect fixes.
- Packaging and installed-consumer workflows.
- Community feedback and maintenance.
- Publish reproducible benchmark results.
- Validate the embedded story against a real Cortex-M toolchain.

### Public benchmark results

Status: Near-term post-release improvement

Publish representative results from the existing benchmark harness, including
encoded payload sizes, encode/decode performance, generated-code size,
runtime/binary size where meaningful, and allocation behavior where measurable.
Include C, C++, and Python results where applicable, alongside the existing
Protobuf comparisons. The publication must retain the harness methodology,
environment, compiler and runtime versions, workloads, provenance, and
limitations, and must explain API ownership/allocation asymmetries rather than
hide or optimize around unfavorable results. Results should be reproducible
from documented commands; generated result files are not part of this roadmap
change.

### Cortex-M validation

Status: Cross-compilation validated in CI; hardware execution and footprint
regression tracking remain future work

CI now cross-compiles Quarry's generated strict-C99 code and the C runtime
for a generic Cortex-M4 target (`arm-none-eabi-gcc`, illustrative
STM32F446-class profile, `-mcpu=cortex-m4 -mthumb -mfloat-abi=soft`, no FPU
flags) via a standalone `cortex_m/` CMake project and a reusable toolchain
file (`cmake/toolchains/arm-cortex-m4.cmake`), exercised against the
existing representative `benchmark.workload`/`workload_shared` schemas
(records, an enum, bounded string/bytes, a bounded array, a cross-namespace
nested record, and a cross-namespace array of records). The generated C
headers and the C runtime were confirmed to require only
`<stdbool.h> <stddef.h> <stdint.h> <string.h>` -- no heap, exceptions, RTTI,
or other hosted Linux/POSIX dependency -- so no runtime or generated-code
changes were needed to make this build pass. The "Cortex-M Cross Build" CI
job reports flash/RAM footprint (`.text`/`.rodata`/`.data`/`.bss`) for the
smoke build on every run; see `cortex_m/README.md`.

Not yet done, and explicitly out of scope for this initial validation:

- execution on physical Cortex-M hardware or an emulator (QEMU) -- a
  successful cross-build is evidence of build/portability, not of runtime
  behavior on target;
- footprint regression tracking against a baseline (today's job reports
  footprint; it does not gate on it);
- additional Cortex-M profiles (M0/M0+/M3/M7, hard-float);
- RTOS/Zephyr, HAL, or board-support-package integration.

## Pre-1.0 investigations

### Quarry name and package-distribution decision

Status: Pre-1.0 investigation

Investigate the Quarry name and namespace before broader external adoption.
The investigation should cover PyPI availability and collisions, crates.io,
npm, GitHub/project discoverability, domain-name considerations, CLI executable
naming, CMake package and target naming, C/C++ namespace and prefix choices,
Python import/package naming, and future Rust and Go package implications. It
should also estimate migration cost after adoption and identify
backward-compatibility options if a rename is necessary.

The output must be an explicit decision to keep Quarry, keep the project name
with ecosystem-specific package names, or rename before broader adoption/1.0.
This is an investigation, not a predetermined rename.

A project rename before 1.0 remains explicitly on the table if cross-ecosystem
name collisions (PyPI, crates.io, npm, CMake `find_package`/package
registries, embedded package indices, GitHub/project discoverability) are
found to materially harm discoverability or distribution. This project has
already executed a full rename once (Breadcrumbs to Quarry; see
`jira/backlog.md`, PR-088), which is precedent that a further rename, if
warranted, is operationally tractable rather than merely theoretical.

### Positioning and adoption/distribution

Status: Pre-1.0 investigation

Review the README and documentation so Quarry does not imply that schema
compilation is necessary for every small embedded firmware project. Evaluate
positioning around systems where coordination cost matters: multiple teams or
languages, firmware plus host/cloud/tooling components, long-lived schemas,
compatibility requirements, generated APIs, and cross-component
interoperability. The intended problem domain should be clarified against the
project goals before any suggested wording becomes marketing copy.

Treat adoption and distribution as strategic concerns, including straightforward
package installation, embedded ecosystem and build-system integrations,
reference projects and examples, documentation discoverability, and downstream
integration tests. Do not assume vendor partnerships.

### Project status and business model

Quarry remains an independent Apache-2.0 open-source project. No commercial
repositioning, dual licensing, paid editions, hosted services, or monetization
plan is implied by this roadmap. Any future business-model question would be a
separate strategic consideration.

## Longer-term backend and feature investigations

### Compact compiled-schema access layer

Status: Future investigation

Investigate compiling a schema into a compact binary representation consumed by
a small, highly tested set of operations: `put`, `get`, `count_size`, and
`print`. The investigation should define the representation, operation
semantics, validation and error behavior, generated-code/runtime/tooling
boundaries, and cross-language consistency, while assessing code size,
allocation, lookup cost, and suitability for constrained targets. Keep this
distinct from BRF unless a later compatibility analysis explicitly determines
otherwise; this roadmap item does not authorize changes to BRF, Schema IR, or
generated APIs.

### Human-readable runtime data representation

Status: Future investigation

Investigate a human-readable representation of schema-defined runtime data,
separate from the YAML schema language: YAML schema describes types and schema
definitions, while this capability would represent instances/runtime values and
support typed Quarry object → text → typed Quarry object conversion. Primary
use cases include debugging and diagnostics, configuration, logging, CLI
inspection/editing, human-readable test vectors and golden files, and manually
constructing data for tests and tools.

The investigation should compare YAML, JSON, and a Quarry-specific language;
define round-trip, deterministic/canonical-output, validation, error-reporting,
and schema-evolution semantics; and cover records, arrays, enums, strings,
integers, floating-point values, booleans, nested types, presence information,
unknown/unsupported values, and the optional role of comments. It should
explicitly compare the valuable properties of Protocol Buffers TextFormat
without assuming its syntax or API, and determine the appropriate division
between generated APIs, runtime libraries, and tooling. It must also assess
behavioral consistency across C++, C, and Python, embedded code-size and
allocation costs, exclusion from constrained targets, and the fact that this
is an additional human-facing representation that must not replace or modify
BRF.

### Rust backend

Status: Future enhancement

Investigate:

- generated API design;
- ownership and borrowing model;
- `no_std` feasibility;
- allocation-free or bounded-memory operation;
- BRF runtime architecture;
- package/crate structure;
- cross-namespace imports;
- interoperability with existing C, C++, and Python backends.

Implementation requires a separate investigation covering generated API design,
runtime architecture, package structure, interoperability, memory model, and
compatibility impact. It is not scheduled work.

### Go backend

Status: Future enhancement

Investigate:

- generated API conventions;
- package and module layout;
- value versus pointer semantics;
- bounded-memory expectations;
- BRF runtime design;
- cross-namespace imports;
- interoperability with existing backends.

Implementation requires a separate investigation covering generated API design,
runtime architecture, package structure, interoperability, memory model, and
compatibility impact. It is not scheduled work.

## Other possible future investigations

- Additional language backends.
- Improved embedded package-manager integration.
- Expanded Protocol Buffers migration support.
- Performance and memory optimizations justified by benchmarks.
- Additional diagnostics requested by users.

## Completed milestones

- Production C++ backend.
- Production strict-C99 C backend.
- Production Python backend.
- Deterministic BRF serialization and cross-language interoperability.
- Protocol Buffers descriptor-set translation.
- Nanopb options compatibility.
- Native and Docker CI validation.
- Deterministic benchmark infrastructure and reports.
- Public documentation and examples.
- Release engineering and semantic versioning.
- Contribution and security documentation.

## Explicitly deferred

- Recursive records.
- Required/optional field semantics.
- Reflection.
- Dynamic runtime schemas.
- Plugin systems.
- Features without demonstrated user demand.

## Compatibility policy

Every roadmap item must be investigated before implementation.

Changes affecting any of the following require explicit compatibility analysis:

- Schema IR;
- BRF wire format;
- generated APIs;
- runtime APIs;
- descriptor translation;
- semantic versioning;
- API epochs.

## Development process

1. Record the direction in this roadmap.
2. Open an investigation issue.
3. Produce an investigation report.
4. Discuss the design.
5. Implement through one or more narrowly scoped pull requests.
6. Update this roadmap when the status changes.
