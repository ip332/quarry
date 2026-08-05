# Quarry Roadmap

Quarry is in maintenance mode around the v0.1 release line.

This roadmap records likely future directions. Items are not commitments and
may change based on user feedback, compatibility constraints, and investigation.

## Status of this roadmap

This document communicates current direction rather than a commitment or
release plan. Items describe investigations or possible future work. Every
significant change begins with an investigation, and implementation depends on
compatibility analysis, validation, and project priorities.

## Current focus

- Stabilize the public release candidate.
- Improve first-time user experience.
- Fix reported defects.
- Improve release quality and artifact hygiene.
- Collect community feedback.
- Avoid speculative feature development.

## Near-term priorities

- Release engineering improvements.
- Documentation and first-time-user usability.
- Clearer diagnostics.
- Packaging and installed-consumer workflows.
- Community feedback and maintenance.

## Future investigations

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
