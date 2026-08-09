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

Status: High-priority post-release validation

Investigate validation with `arm-none-eabi-gcc` for an appropriate Cortex-M
target/profile. Compile representative generated strict-C99 code and the C
runtime, report flash/code size and static RAM/data/BSS, and document stack
considerations where measurable. Confirm that generated and runtime code does
not accidentally depend on hosted Linux facilities, and establish CI
regression tracking for embedded footprint. Cross-compilation evidence must be
reported separately from hardware execution or emulation; a successful
cross-build alone is not evidence of runtime behavior on the target.

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
