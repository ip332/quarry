# v0.1.31-rc.1 Stage A release readiness

This is an evidence map, **not publication approval**. Baseline:
`2a9aafdbc3edbe9eef8ea84efa02cad717b1c7b0` (A5 #127 merged; #103 closed).
[Post-merge CI](https://github.com/ip332/quarry/actions/runs/44964419771)
passed Build/test, coverage, clang-tidy, Cortex-M and benchmarks before A6 began.
Linux GCC: **72/72 CTests**, 244.12 s; coverage: **67/67**, 239.16 s.
The separate Clang 18 matrix passed in 9.714 s. These baseline measurements are
not claims about a later A6 revision. Exact A6 commit validation/CI is recorded in
its PR and commit-bound `quarry-validate` receipt, avoiding a self-referential SHA
in this file.

“Covered” means a maintained test exists and baseline CI passed it, not exhaustive
proof over all inputs/toolchains. “Partial” explicitly identifies a missing dedicated
scenario. “Future” rows are mapped exclusions, never counted as Stage A passes.
Paths below are source-tree evidence relative to the repository root; CTest names
identify executable test registrations (one CTest may contain many assertions).

## Implementation provenance

| Delivery | Implementation |
|---|---|
| Post-RC3 C runtime | #77 shared codec / #79 generated operations, C epoch 2 at RC3 to current 7; Go introduced by #80 |
| A1/A2/A3 | #81 guards; #84 hierarchical allocation; #86 roots/selection; #95 complete CMake dependencies |
| A4.1 | #91 identity preparation; #93 shared layout; #115 explicit backend identity |
| A4.2 | #106 configuration dependency checks; #116 native gid; #118 injective identity stems; #120 plain-C gid |
| G3/G4/G5 | #121 per-namespace C storage; #122 validation; #123 compatibility contract |
| A4.3/A4.4/A4.5 | #124 stored QBS identity; #125 describe/tag metadata; #126 explicit prefixes |
| A5 | #127 five-language byte-exchange matrix |
| A6 | This documentation/consistency-test PR, Closes #104; no compiler/runtime behavior changes |

## Design §13 coverage

Source: [record identity design §13](design/record-identity-and-schema-discovery.md#13-test-matrix).
Each semicolon-separated subrequirement is accounted for below.

| Requirement | Implementation/source | Maintained test / CI evidence | Status and boundary |
|---|---|---|---|
| Sorted namespace domains | #84 `compiler/layout/layout.cpp` | `layout_smoke_test`: DomainsAreSortedNamespacesAndLocalsAreSortedNames, OrderingIsBytewiseNotLocaleAware | Covered; bytewise canonical names |
| Reordered, transitive, diamond and duplicate imports | #84 frontend/layout | `yaml_compiler_test`: LoadsTransitiveDiamondOnceInDeterministicOrder; `hierarchical_record_id_test`: ImportOrderDoesNotAffectIdsOrSchemaId | Covered; no network/package resolution |
| Multi-file namespace | #86 output planning | `yaml_compiler_test`: AggregatesSourceUnitsOfOneNamespaceIntoOnePlannedOutput; `schema_compiler_tool_test`: MultiFileNamespaceIsGeneratedAsOneUnit | Covered; one primary record per file remains |
| Enum-only namespace gets no domain | #84 layout | `hierarchical_record_id_test` shared fixture includes unused enum namespace; root mapping assertions | Covered; enum metadata is not a record ID |
| Cycle rejection | frontend graph loader | `yaml_compiler_test`: RejectsImportCycleWithCycleDiagnostic | Covered |
| Domain/local limits and reserved values | #84 layout | `layout_smoke_test`: ApplicationDomainLimitIsEnforced, LocalRecordLimitIsEnforced, RootNamespaceIsTheFirstDomain | Covered allocator boundary; no system records assigned |
| Truck golden mapping | #84/#91 | `hierarchical_record_id_test`, `schema_description_test`, `interop_matrix`, `stage_a_documentation_test` | Covered all 8 IDs |
| Local name order independent of declarations/files | #84 layout | `layout_smoke_test`: AssignmentIsIndependentOfInputOrder; `schema_description_test` swaps declarations/files | Covered |
| Insertion before existing local shifts only its domain | #84 sorted allocator | source algorithm plus rev B MirrorFold in `name_prefix_test`/`public_examples_test` | Partial: append case maintained; no dedicated insertion-before-existing-name regression asserted here |
| Repeated generation byte identity | output planning/backends | `hierarchical_record_id_test`: RepeatedCompilationIsDeterministic; `interop_matrix` determinism | Covered for maintained fixtures/options |
| Import reorder preserves IDs/schema_id | #84/#91 | `hierarchical_record_id_test`, `schema_compiler_tool_test`, `interop_matrix` | Covered, also both QBS modes; source declaration byte order excluded (#90) |
| Selection preserves IDs/schema_id | #86/#91 | `schema_compiler_tool_test`: GenerationSelectionNeverChangesIdsOrQbs; `configuration_identity_test`; matrix | Covered, selected/full shared output bytes |
| Five-language encode/decode × all pairs | #127 `tests/interop/matrix/` | `interop_matrix`, Linux GCC and targeted Clang 18 job | Covered 25/25 cells, 295 streams, 1,475 positive consumer checks, no skips |
| Top-level, depth 1 and depth 2 | matrix shared model | all 8 truck records, Gear→Engine and Vehicle→Gear→Engine; record arrays extend to depth 4 | Covered populated/default models; companion fixture adds enums without altering truck root |
| Extracted nested records | C generic extraction + generated adapters | every actual child/array-element span compared with standalone bytes and decoded by all 5 consumers | Covered, own ID/type and bytes checked |
| Inspector with configuration image | existing inspector + #127 driver | C representative populated/default records in both modes; every producer's bytes identical | Covered representative values, not every possible record/value combination |
| Configuration mixing C/C++/C-storage/Python/Go | #106/#127 | `configuration_identity_test`, matrix dependency replacement with expected guard diagnostics | Covered all 5; import/build checks, no whole-program scan |
| RC3 IDs rejected | current generated decoders and inspector | matrix legacy controls for actual expected types and nested Timestamp; valid positive controls | Covered contextual mapping rejection, no universal numeric parser rule claimed |
| Identity excludes reflective names | #91 canonical QBS input | `qbs_model_test`: IdentityInputIgnoresReflectiveNames; native gid field/member rename checks | Covered; FQNs still identity inputs |
| Identity excludes comments/import paths | canonical model/preparation | `schema_compiler_tool_test`: PrintSchemaIdIgnoresSourcePathAndDiffersByResolvedConfiguration, ConfigurationMetadataDoesNotAffectSchemaIdentity | Partial: paths maintained; no dedicated comment-only mutation claimed |
| Identity excludes compiler version | QBS identity builder | `qbs_model_test`: ConfigurationIdentityIgnoresCompilerVersion | Covered metadata strings; exhaustive real cross-release build comparison unavailable |
| Identity changes with IDs/wire definitions | QBS canonical input | QBS model/serializer tests; `configuration_identity_test` rev B same-ID/renumbered fixtures; matrix identity mutation | Covered representative changes; not exhaustive all wire changes |
| Describe golden | #125 CLI | `schema_description_test`, `tests/fixtures/description/truck.txt` | Covered locale, normalization, errors, epochs and header identity |
| Namespace generation complete | #86 output planning | MultiFileNamespaceIsGeneratedAsOneUnit; SeparatelyGeneratedNamespacesReproduceFullGeneration | Covered all backend output inventories |
| CMake root/selection and install/package consumers | #86/#95/#126 helper | `schema_compiler_package_test`, `schema_compiler_install_test`, `runtime_package_consumer_test`, `public_examples_test`, `name_prefix_cmake_test` | Covered installed compiler/public headers; compiler libraries remain internal |
| Stage B descriptor round trip and old-reader skipping | Future parser/extension work | no maintained implementation | Future; not a Stage A pass |
| Stage B paging, size limits, partial image identity | Future discovery | no maintained implementation | Future; no discovery API advertised |
| Stage B classifier: truck A/B and reorder | Future classification | no maintained implementation | Future; equality is not compatibility analysis |
| Stage B translator round trips | Future translation | no maintained implementation | Future; historical readers/migrations remain application-owned |

## Configuration-identity §9 coverage

Source: [configuration identity §9](design/configuration-identity.md#9-test-requirements).

| Requirement | Implementation/source | Maintained test / evidence | Status and boundary |
|---|---|---|---|
| 1 Golden ID and invariance to compiler metadata, prefix, selection, name, QBS mode | #91/#125/#126 preparation | `configuration_identity_test`: SchemaIdIsPinnedAndIndependentOfSelectionNameAndQbsMode; QBS model compiler-version test; `name_prefix_test`; matrix | Covered named dimensions; actual cross-release comparison limited |
| 2 Every backend embeds full identity for every selection | #106 backends | EmbedsThePrintedSchemaIdForEverySelection; preparation tests; matrix full 128-bit comparisons | Covered five variants |
| 3 Wrong root dependencies fail; same-root separate invocations work | #106/#127 checks | RejectsDependenciesFromAnotherConfiguration; RejectsDependenciesThatPredateIdentity; separate-generation tests; matrix | Covered all 5; Go older metadata may report `undefined:` |
| 4 Wrong include order | #106 native checks | `configuration_identity_test`: RejectsIncludePathMixing, parameterized native variants | Covered generated dependency edges |
| 5 C++ owning/C/C-storage separate-TU configurations | #106 + #111 isolation | CppConfigurationsWithDifferentLayoutsLinkTogether; CConfigurationsWithDifferentLayoutsLinkTogether; CppCStorageConfigurationsWithDifferentLayoutsLinkTogether; G4 O0/O2/link-order runtime cases | Covered measured ABIs only; application inline functions still caller-owned |
| 5 Go import paths / Python prefixed same process | #106/#126 | GoConfigurationsCoexistUnderDistinctImportPaths; `name_prefix_test`; truck `public_examples_test` | Covered; no runtime schema negotiation |
| 6 Prefix isolation, same QBS/IDs, files/guards/modules | #126 | `name_prefix_test`, `name_prefix_cmake_test`, matrix prefixed-byte controls and A/B example | Covered; unprefixed C #119 exclusion persists |
| 7 Stored C/C++/Python identity, invalid args, no hashing/mandatory allocation | #124 `runtime_c/schema_id.c`, `compiler/qbs/parser.hpp`, Python generic | `generic_c_runtime_test`, `qbs_parser_test`, Python conformance, installed C99 consumer, cross-language identity test | Covered minimal/reflective and unchanged layouts; no universal fabricated-view validation |
| 8 Describe/print golden and QBS matching | #125 | `schema_description_test`, CLI and preparation tests, `stage_a_documentation_test` | Covered complete 128 bits, both modes |
| 9 Zero mandatory footprint / explicit accessor cost separately | #106/#124 CI Cortex-M | `.github/workflows/ci.yml`, `cortex_m/scripts/report_footprint.py`, smoke + identity targets, G4 embedded report | Covered cross-build/link evidence; base comparison runs on PR events, not baseline push; no hardware execution claim |
| 10 CMake NAME_PREFIX/SCHEMA_ID_VARIABLE, differing roots | #126 `cmake/QuarryGenerate.cmake` | `name_prefix_cmake_test`, truck A/B native example via `public_examples_test` | Covered configure-time identities and shared output directory |

Native gid adds the G4 matrix documented in
[generated-code identity validation](generated-code-identity-validation.md): C++ owning,
C++/C storage, C99, plain C headers consumed as C++20 and mixed TUs; actual runtime
behavior, both link orders, strict warnings, optional sanitizer and GCC LTO `-Wodr`
secondary evidence. It does not replace wire identity tests. Python/Go native gid
is N/A (not a skipped producer/consumer cell).

## Issue #104 scope and acceptance coverage

| Scope / acceptance criterion | Artifact and validation | Status / limitation |
|---|---|---|
| BRF allocation/ranges, QBS identity/accessors, generated constants/guards/dependencies/epochs | Updated BRF/QBS/compiler specs plus [identity specification](specifications/configuration-and-code-identity.md); source constants and maintained tests above | Documented; no algorithm/epoch change |
| Schema identity, layout, compiler passes/configuration preparation | Updated `design/schema-identity.md`, `layout-algorithm.md`, `compiler-passes.md`, `compiler-architecture.md` | Stale persistent state/import-free claims corrected to merged behavior |
| Manifest superseded | `specifications/manifest-format.md` status and cross references | Historical proposal retained, not available compiler mode |
| Versioning and A4.2 epoch rationale | `versioning.md`; version resolution tests; description/header checks | 3/7/1/1, BRF2/QBS1/IR1, exact tag separate from numeric version |
| Tools, compiler/runtime/backend/example and installed-package docs | Updated READMEs/distribution docs; [literal CLI guide](stage-a-cli.md) | Executable CLI/diagnostic checks plus installed consumer/example tests |
| Stage A implemented / Stage B future | Both design status lines, architecture boundary and specs | A1–A5 merged; A6 documentation delivered here, publication not authorized |
| Release notes: breaks, migration, compatibility, known issues | [Release notes](release-notes-v0.1.31-rc.1.md) | Requires regeneration/rebuild; does not promise RC3 BRF readability |
| Complete readiness map / open issue disposition | Every §13 area/subrequirement and §9 numbered item above; issue table below | Explicit partial/future rows retained, never counted as full coverage |
| Acceptance 1: scope updated and reviewed | This focused PR for independent review | Implementation complete; independent review pending |
| Acceptance 2: merged behavior checked | `stage_a_documentation_test`, existing `schema_description_test`, `consumer_documentation_test`, full native/Linux suites | Baseline CLI checked directly; A6 exact-revision results in PR/receipt |
| Acceptance 3: release notes and no unmapped requirements | This report and release notes | Covered, including explicit gaps |
| Acceptance 4: every doc link resolves | `stage_a_documentation_test` scans tracked Markdown paths and local heading fragments | Relative links checked; external services/network uptime not certified |

## Authoritative truck mapping

Root `examples/truck/schemas/configuration.brd`, identity
`9200ca494f010801b830d0857cf66dde`:

| Record | ID |
|---|---|
| truck.VehicleSnapshot | 0x00010001 |
| truck.brakes.AbsEvent | 0x00020001 |
| truck.brakes.BrakeStatus | 0x00020002 |
| truck.cms.CameraHealth | 0x00030001 |
| truck.common.Timestamp | 0x00040001 |
| truck.engine.EngineStatus | 0x00050001 |
| truck.engine.FaultCode | 0x00050002 |
| truck.transmission.GearState | 0x00060001 |

Revision B adds `truck.cms.MirrorFold` = `0x00030002`, retaining EngineStatus's ID
and bytes but changing full identity to `806e9a4e11d0df71c4e5b51045f85010`.
`public_examples_test` runs the existing installed-package/wheel A/B application
in all five variants. The A5 matrix adds prefixed/unprefixed byte comparisons.

## Outstanding issues and release decision

| Issue | Disposition | Supporting evidence / remaining gate |
|---|---|---|
| [#85](https://github.com/ip332/quarry/issues/85) | **Publication blocker** | Open milestone bug; maintainer's 2026-11-17 comment requires investigation/resolution before #105. Linux Clang18 full generic presence assertion remains unresolved. A5 targeted Clang18 matrix passes, does not close it. |
| [#88](https://github.com/ip332/quarry/issues/88) | **Unresolved publication gate** | Open milestone bug, no approved deferral. `import build` can resolve a local namespace directory. Current CI has real dependencies and passes, which does not fix detection. Resolve or obtain explicit disposition before #105; A6 does not change build behavior. |
| [#89](https://github.com/ip332/quarry/issues/89) | **Fixed** | Closed; #114 commit receipts, `quarry_validate_test`, documented hooked pushes. Validation completes before SSH connection; no bypass needed. |
| [#90](https://github.com/ip332/quarry/issues/90) | **Explicitly deferred / non-blocking** | Maintainer's 2026-11-17 backlog comment; declaration order may change source/gid after import reorder. IDs/schema_id/QBS remain invariant. |
| [#119](https://github.com/ip332/quarry/issues/119) | **Approved exclusion / deferred** | Maintainer's recorded RC disposition: same-TU distinct namespaces with colliding unprefixed C symbols unsupported. #126 implements prefixed-only injective names, not general renaming. |
| [#104](https://github.com/ip332/quarry/issues/104) | **Independent review pending** | This documentation PR; no automatic merge. |
| [#105](https://github.com/ip332/quarry/issues/105) | **Not started** | Final release validation/publication remains separate; no tag or release created here. |

No other issue disposition is changed by this report. The experimental
`explore/fqn-hash-record-ids` branch remains outside main; its rejected hash
allocation algorithm is not used. Native compiler/ABI permutations, actual
cross-release generator comparisons and board/RTOS execution are not exhaustive.
The candidate is ready for **A6 review**, not release publication.
