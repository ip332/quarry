# v0.1.31-rc.1 release notes

Release candidate documentation; publication requires explicit maintainer approval. Relative to
v0.1.30-rc.3, Stage A adds configuration-wide identity and interoperability.
Publication remains gated by the [readiness report](release-readiness-v0.1.31-rc.1.md).

## Changes

- Hierarchical record IDs (#84): `(domain << 16) | local`, with domains sorted
  by canonical namespace and locals sorted by record name. Domain 0 and
  `0xff00`–`0xffff`, local 0 and `0xffff` are reserved; no system records are
  assigned. Import traversal no longer determines numeric identity.
- Authoritative configuration roots and `--generate` (#86): root plus complete
  transitive imports determine IDs and identity. Multiple files of one namespace
  emit one complete namespace output. Record roots still work, but compiling a
  component independently defines a different configuration.
- Configuration identity (#91, #93, #106, #115): all five variants embed the
  same full QBS schema_id and check generated dependency edges. Native C/C++
  checks happen at compilation, Python at import, Go through constants.
- Native generated-definition identity (#116, #118, #120–#123): format-1 gid,
  injective identity stems, identity guards, dependency checks and C++ inline
  namespaces isolate definitions differing in names, unused enums, validation
  bodies or host storage even when wire schema_id is equal. This is not a
  prebuilt-library ABI certificate or whole-program scan.
- C storage selection (#121): compact presence flags globally or for exact
  canonical namespaces with `--c-optimize-size[=NS,...]`. Mixed policies are
  supported with consistent expectations across generation invocations.
- QBS identity accessors (#124): C `quarry_qbs_schema_id()`, internal C++
  `ValidatedQbsView::schema_id()` and Python read-only schema identity properties.
  Accessors read stored bytes without new hashing or mandatory initialization.
  Public view layouts stay unchanged; reflective C string validation now checks
  actual payload strings, and fuzz build include paths/QBS parser entry are corrected.
- `--describe` and exact release metadata (#125): deterministic configuration
  summary, IDs, formats and epochs; exact matching HEAD release tag or `untagged`
  reported separately from numeric Git-derived development version.
- Explicit `--name-prefix` isolation (#126): two truck configurations coexist
  in C, C++ owning, C++/C storage, Python and Go. Prefixed C encoding preserves
  case and escapes `q`/`_`; unprefixed generated source remains unchanged.
- Cross-language validation (#127): actual BRF streams exchanged across all 25
  producer/consumer pairs, generated and generic decoding, inspector checks,
  nested cuts, negative controls, mutation detection and determinism. The baseline
  passed 295 streams / 1,475 consumer checks without matrix skips, including
  Linux GCC and a targeted Linux Clang 18 matrix. The separate generic
  conformance test defect was subsequently fixed by #131 (see below).
- Post-RC3 C generation moved to header bindings backed by the shared BRF v2
  codec (#77), with no schema-specific C object required. C/C-storage runtime
  descriptors, presence operations and generated decode operations evolved through
  epoch 7 (#79); these are source/runtime-contract changes, not a wire-format bump.
- Earlier post-RC3 maintenance includes Go bindings/runtime (#80), generated C
  decode operations (#79), C++ include guards (#81), inline header definitions
  (#97), strict warning fixes (#98), complete CMake import dependencies (#95),
  Git-environment isolation (#96) and receipt-based pre-push validation (#114).
- Release benchmark reporting derives format and epoch metadata from the same
  compiler's `--describe` query, identifying current Quarry bindings as BRF v2
  and C epoch 7, C++ epoch 3 and Python epoch 1. A maintained regression checks those labels
  against the CLI description fixture and rejects encoded-size disagreement
  within one wire format; this corrects reporting, not codec behavior.

## Compatibility and migration

Release artifacts use numeric package version **0.1.31** and exact release tag
**v0.1.31-rc.1**. The ordinary Git checkout retains its independently derived
development version; its commit counter need not equal the release revision.
See [versioning](versioning.md) for the archive fallback and exact-tag rules.

BRF remains version **2**, QBS **1**, Schema IR **1**, wire identity algorithm **1**
and native gid format **1**. Owning C++ epoch remains **3** and Python **1**.
C/C-storage is now **7**, up from C epoch **2** at the exact v0.1.30-rc.3 tag;
Go is newly available with epoch **1**. Upgrade generated C/C-storage and their
linked runtime together: older epoch-2 bindings are not current runtime-contract
consumers. A4.2/gid changes themselves do not alter the runtime call contract,
so no epoch bump accompanies their generated source/linkage changes. Equal epochs
are not native ABI compatibility guarantees.

The hierarchical allocation **breaks RC3 record mappings** despite retaining BRF
v2: old flat IDs below `0x10000` do not match the current configuration's types.
Format-version equality alone never establishes that historical records decode.
There is no automatic discovery, translation or persistent manifest/rename state.

1. Choose one authoritative root and preserve its full import closure. Record its
   `--print-schema-id`, `--describe`, generator commit and runtime/toolchain.
2. Regenerate **all** dependent bindings from that root, even when generated in
   separate invocations. Use consistent storage options and prefix policy;
   discard stale build-owned outputs because generation does not delete them.
3. Update generated includes/names/guards as required. Include generated C++
   headers instead of forward-declaring types globally: actual types now live
   in identity-scoped inline namespaces. Header guards/metadata names changed.
   Prefixed C APIs use encoded stems such as `revq0a_truck_engine_`.
4. Rebuild affected native binaries/libraries and their consumers together.
   Previously generated bindings generally require regeneration; equal schema_id,
   gid, epochs or sizes do not authorize native casts or stale object reuse.
5. Keep configuration context with stored/transmitted BRF data and choose its
   decoder explicitly. Retain historical readers or plan an application-owned
   migration for historical data; do not feed RC3 data to new bindings blindly.
6. Run the installed-package/example tests and the five-language matrix. Use
   `scripts/quarry-validate validate HEAD` and `scripts/quarry-validate push` with
   hooks enabled before delivery.

## Known limitations and release gates

- [#85](https://github.com/ip332/quarry/issues/85): fixed by merged #131.
  The conformance test used a wire record ID as a table index and dereferenced
  empty optional metadata. The corrected lookup retains the absence assertion;
  Linux Clang 18 Debug and ASan/UBSan with checked optional access passed.
- [#88](https://github.com/ip332/quarry/issues/88): fixed by merged #132.
  Dependency detection checks the executable `build.__main__` capability,
  preserving the selected interpreter and user-site installations. The regression
  validates missing/installed dependencies and registration from both source-root
  and out-of-source directories; packaging and public examples execute in CI.
- [#89](https://github.com/ip332/quarry/issues/89): fixed by validated-commit
  receipts and short pre-push checks (#114); no hook bypass is required.
- [#90](https://github.com/ip332/quarry/issues/90): explicitly deferred,
  non-blocking declaration-order source churn after import reordering. Wire IDs,
  schema_id and QBS remain stable; source-byte/gid invariance is not promised.
- [#119](https://github.com/ip332/quarry/issues/119): approved release exclusion:
  same-TU coexistence of distinct namespaces whose unprefixed C symbols collide
  is unsupported, including dot/underscore and uppercase-enum case collisions.
  Prefixed-only injective naming is implemented; general C renaming is deferred.
- [#129](https://github.com/ip332/quarry/issues/129) and
  [#130](https://github.com/ip332/quarry/issues/130): open, non-blocking regression
  follow-ups for this RC by maintainer disposition. Dedicated insertion-before-local
  and comment-only identity-mutation coverage is still missing. Existing sorted
  allocation and canonical identity tests provide indirect protection, not those
  exact regressions; see the readiness report's risk assessment.
- Dependency checks cover generated edges, not arbitrary prebuilt objects or
  application inline functions. Go's older-metadata diagnostic can be a generic
  `undefined:` error; Python/Go have no native gid mechanism; Go has no QBS runtime.
- Stage B descriptor/discovery/partial-image/classification/translation remains
  future work. Schema IR and compiler/QBS C++ libraries remain internal; Python
  and Go runtimes are separate packages. No hardware-execution certification is
  implied by Cortex-M cross-builds.

See the [normative identity contract](specifications/configuration-and-code-identity.md),
[CLI guide](stage-a-cli.md) and [consumer migration contract](generated-binding-compatibility.md).
