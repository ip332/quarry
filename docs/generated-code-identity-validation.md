# Generated-code identity validation (#111 G4)

This is the validation baseline for v0.1.31-rc.1. G3's baseline is
`59ebeda4cbbadbd79c0d33498c917cd85eba694d` (PR #121, reviewed head
`927327b74ce695b643448fee44eaef331fc9faec`). Format 1 and production generators
are unchanged by G4. This document describes tests and their limits; G5 and
#112 still own the final consumer contract and release migration guidance.

## Mandatory checks

```sh
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug -L identity-fast --output-on-failure
ctest --preset debug --output-on-failure
```

`generated_code_identity_fast_test` reuses the existing integration executable
and helpers. It checks configuration metadata, a three-namespace dependency
chain, configuration-name/import-order equivalence, unselected dependencies,
runtime enum-member renames, different wire layouts, identical definitions,
and actual C99/C++20 object interchange across separate generator invocations.
Both **object-file link orders** are exercised with the same input translation
units. Every program checks runtime values and encode/decode status; the helper
also requires a successful exit and the expected results from both TUs.

The original `generated_code_identity_integration_test` still runs every G1–G3
case with its original `-O0`, `-O2` and supported ASan/UBSan coverage. No old test
is moved to an opt-in profile or removed. Its CTest filter excludes only the new
G4 cases, which have their own CTest entry. Direct execution without a filter
runs both sets. G4's second-generator case explicitly skips unless supplied a
second generator; it is not claimed as mandatory coverage.

Other maintained evidence remains required:

| Property | Maintained tests |
| --- | --- |
| Canonical encoding, independent pinned vectors, dependency ordering and field sensitivity | `generated_code_identity_test` |
| Missing configuration identity; explicit all-zero identity is valid | `backend_identity_requirement_test` |
| Output directories, stems, extensions, subsets, same-TU conflicts, older/missing/incorrect identity metadata | identity integration tests, `configuration_identity_test`, `backend_c_test` |
| Wire identity, QBS, Schema IR and generated-language output preservation | existing goldens, QBS tests and pinned truck output trees |
| Callback language linkage, identity-bearing names, external inline entities and template-symbol isolation | existing callback structural/compiler probes, mutation checks and `nm` checks |
| Exact, case-sensitive, nonrecursive storage policies; complete configuration validation; invocation/include orders | G3 integration/CLI tests, plus G4 case-sensitive selection and unnamed-root tests |

The unnamed root is a backend-API case: the YAML frontend cannot declare it.
Its empty FQN is not a list entry. The existing root bitmap test uses the bare
policy; `UnnamedRootCannotBeSelectedWithAnEmptyNamespaceListEntry` rejects an
explicit empty entry. CLI empty lists remain errors. Selecting a container
namespace never recursively selects its children.

## Extended matrix

No extra compiler installation or generic mutation framework is needed. The
small driver reuses the maintained executable, records compiler versions,
flags, exact test filters, timings, skipped tests and XML/log evidence. C and
C++ compile/run probes must pass before a row is run. An unsupported sanitizer
or LTO probe is recorded as unsupported, never as passed coverage. A failed
regression stops the run for investigation.

```sh
python3 scripts/quarry-identity-validate matrix \
  --test-binary build/debug/tests/generated_code_identity_integration_test \
  --toolchain appleclang /usr/bin/clang++ /usr/bin/clang \
  --toolchain clang22 /usr/local/opt/llvm/bin/clang++ /usr/local/opt/llvm/bin/clang \
  --toolchain gcc16 /usr/local/bin/g++-16 /usr/local/bin/gcc-16 \
  --output /tmp/quarry-identity-matrix-new
```

Use paths available on the host; those paths are examples, not portability
claims. Output directories must be new so prior evidence is not overwritten.
`--profiles core,opt,size,sanitize,lto` is the default. A smaller selection can
be supplied explicitly. This is a pairwise/profile matrix, not the Cartesian
product of every compiler, scenario, flag and backend:

| Profile | Flags | Cases |
| --- | --- | --- |
| core | `-O0` | G4 cases and existing enum callback/linkage tests |
| opt | `-O2` | G4 runtime cases, original C-storage corruption case, plain C/C++ template-isolation cases |
| size | `-Os` | G4 runtime cases and mixed C/C++ interchange |
| sanitize | `-O0 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined` | mixed C/C++ interchange, original storage/template cases and G3 mixed dependencies |
| lto | `-O2 -flto -Wodr` | GCC only: G4 runtime cases and mixed-language interchange |

Strict warnings remain enabled. `-Wodr` is passed only to C++ compilation and
linking; C compilation retains LTO without a C++-only warning option. Runtime
assertions remain primary. Linux CI runs the small GCC LTO profile after the
full suite and uploads its JSON/XML/log evidence. The other extended profiles
are opt-in; they do not multiply the ordinary CI test matrix.

`QUARRY_IDENTITY_CXX`, `QUARRY_IDENTITY_CC` and `QUARRY_IDENTITY_FLAGS` select
child compilers/flags, not the toolchain that built the GoogleTest executable.
The normal CTest environment does not set these overrides. The already-built
C runtime archive has a C ABI and is linked into the child programs. This is
not a claim that the runtime archive itself was rebuilt with every sanitizer
or with LTO. The sanitizer probes run, rather than merely compile.

## Generator implementation drift and build interoperability

The scratch #111 harness hand-edited `validate_rpm` after generation, retaining
its original identity. G4 instead changes the renderer in an isolated worktree
and **builds a real second generator**, so normalized rendering computes the
identity of the changed validation body:

```sh
git worktree add --detach /tmp/quarry-identity-drift 59ebeda4cbbadbd79c0d33498c917cd85eba694d
python3 scripts/quarry-identity-validate drift-build \
  --worktree /tmp/quarry-identity-drift \
  --test-binary build/debug/tests/generated_code_identity_integration_test \
  --output /tmp/quarry-identity-drift-results-new
```

Only `rpm` validation changes (`1234` accepted by A, rejected by B). The same
schema retains its wire identity, the generated-code identities differ, and
both translation units must exhibit their own expected validation behavior in
both link orders. A deliberate negative control then assigns B's declarations
A's inline namespace: the contradictory validators cannot both behave correctly
at `-O0`. This verifies the original failure mechanism without trusting optimizer
or sanitizer detection. The source is restored in `finally`; retained mutant
binaries and logs are experiment artifacts, not production generators.

For two separately built generators expected to produce identical definitions,
pass `--generator-b /path/to/other/quarry-schema-compiler` to `matrix --profiles
core`. The test requires byte-identical headers and matching behavior. This
checks only the supplied builds. It does **not** certify arbitrary released
generators, ABI compatibility, or interoperability with headers predating the
identity mechanism. Report actual versions/commits; do not label different
builds of the same renderer as comprehensive cross-release coverage.

An actual older-release negative check is available with
`--generator-b ... --older --profiles core`. It generates a standalone dependency
schema rather than a modern configuration document, then requires the current
dependent to report missing older identity metadata. Unsupported old C-storage
CLI modes are printed explicitly. This is rejection coverage, not a claim that
types or runtime APIs interoperate across those releases.

## Focused safeguard mutations

```sh
git worktree add --detach /tmp/quarry-identity-mutants 59ebeda4cbbadbd79c0d33498c917cd85eba694d
python3 scripts/quarry-identity-validate mutations \
  --worktree /tmp/quarry-identity-mutants \
  --test-binary build/debug/tests/generated_code_identity_integration_test \
  --output /tmp/quarry-identity-mutations-new
```

The driver requires a clean disposable linked worktree, verifies exact source
anchors, builds each mutant, records the targeted assertion failure, and restores
the source even on failure. A failed build or unexplained crash is not counted
as detection. `--only name[,name...]` selects a reproducible subset.

| Mutation | Detector / interpretation |
| --- | --- |
| configuration-check | G4 removes configuration macros while preserving compatible generated-code identity; missing configuration metadata must be diagnosed |
| code-id-check | existing plain-C incompatible dependency diagnostic test |
| namespace-isolation | plain-C metadata/namespace consistency, then runtime/template isolation checks |
| conflict-diagnostic | existing same-TU test requires the explicit Quarry diagnostic; compiler redefinition errors are an independent fallback |
| callback-linkage | existing C-storage structural and compiler linkage checks |
| unselected-policy | G3 byte equality across one/separate invocations |
| dependency-encoding | independent format-1 vector 3 pins dependency format bytes |
| dependency-list | independent vector 2 pins canonical dependency input |

Dependency identities also occur in normalized dependency-check text. Removing
only the canonical dependency list need not stop integration-level propagation;
the pinned independent vectors are what detect that encoding corruption. This
redundancy is intentional and must not be misreported as a propagation defect.
The namespace-removal probe alone accepted the constant-namespace source mutant
because it derived its expected identity from the namespace being tested. The
independent metadata-versus-namespace check detects that mutant. The driver
therefore targets both metadata consistency and runtime isolation, rather than
claiming the first probe detects every namespace corruption.

Existing generated-output mutations (callback names/linkage/descriptors, omitted
identity checks and C++ namespace isolation) remain in the mandatory suite.

## Original defects and limitations

The preserved investigation directories were inspected, not copied wholesale:
`quarry-investigations/111-odr-harness` and `quarry-investigations/116-g1-review`.
Their minimal storage, validator, template and dependency mechanisms are covered
by maintained tests. Missing-identity generation remains rejected before any
output; all-zero identity is tested as a valid supplied value, not a sentinel.

| Area | Classification |
| --- | --- |
| #119 non-injective C symbol prefixes | Known source-naming limitation. Certain distinct namespaces cannot coexist in one TU; separate-TU identity isolation does not rename their C API. No fix in G4. |
| User inline code hiding generated types in its signature | Outside generated-type symbol isolation; user code still owns its ODR obligations. |
| Regeneration | Source names may remain convenient aliases, but generated mangled names change with definitions/dependencies. No ABI promise across generator releases; rebuild affected consumers. |
| Same TU | Identical definitions deduplicate; incompatible definitions of one namespace are explicitly rejected. Legitimate different namespace sets may still encounter #119. |
| Separate TUs | Tested runtime coexistence and symbol isolation do not imply differently generated types are interchangeable. C/C++ object interchange is tested only with identical effective definitions/policies. |
| Toolchains | Only recorded compiler/profile rows are evidence. GCC's linkage-type enforcement and Clang's structural-only fallback are retained. Other OSes, linkers, ABIs and compiler releases remain unverified. |

No architecture, runtime API/epoch, BRF/QBS, Schema IR, record-ID, `schema_id` or
format-1 change is authorized here. Python/Go pinned output remains required.
G5/#112 must turn the validated behavior and limitations into the final consumer
contract and release migration notes; G4 does not close either issue or #111.

## Embedded evidence

G2/G3's Cortex-M comparison steps remain unchanged. CI compares base/candidate
and equivalent global/explicit compact policies in Default, MinSizeRel and Debug:
actual allocatable bytes, relocations and defined symbols, with debug and other
non-allocatable metadata reported separately. No runtime policy object, hashing,
allocation or initialization is added. Different compact layouts can change
native sizes and codec code; those deltas are not identity overhead. Measurements
are compiler/flags/workload-specific and do not establish untested-board behavior.
