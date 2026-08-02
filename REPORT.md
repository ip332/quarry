# PR-126: Python Backend Release-Readiness Audit

## 1. Executive summary

This was an investigation-only audit of the Python backend at commit
`b429c30 Add Python record arrays`. No production code, tests, or tracked
documentation were changed.

The codec implementation is coherent and appropriately small for its first
supported field-shape release. The supported BRF encodings, absent-versus-
present semantics, nested-record composition, record-array framing, and
Python/C++/C interoperability paths were reviewed. No wire-compatibility
defect was found.

The release surface is not yet ready for a polished first public release.
Two concrete issues block that verdict:

1. Schema IR accepts ordinary identifiers that are Python keywords, and the
   backend does not reject or escape them. It also permits names that collide
   with generated methods or generated helper names. Such schemas can produce
   syntax errors or silently broken dataclass behavior.
2. The distributable runtime metadata and package docstring still say
   “skeleton -- no serialization helpers yet”, although the runtime is fully
   implemented. Several release-facing documents also stop their feature
   history at PR-123 or still claim record arrays are unsupported.

These are release-contract defects, not BRF encoding defects. The backend is
ready for controlled use with the currently documented safe-name and package
installation constraints.

## 2. Release-readiness verdict

**Not ready for a first public Python backend release; ready for internal use
with minor scope restrictions.**

The implementation itself is close to release quality. Before publication,
the generator must either enforce a documented Python identifier policy or
implement deterministic escaping and collision handling, and the runtime
package metadata/documentation must accurately describe the shipped codec.

## 3. Confirmed strengths

### Backend architecture

- `compiler/backend_python/backend_python.cpp` consumes Schema IR directly and
  has no link-time or source dependency on either the C or C++ backend.
- The implementation has a clear catalog/lowering/planning/rendering/output
  flow. `Backend::plan()` and `Backend::generate()` share
  `build_generation_plan()`, so output listing and generation do not maintain
  separate filename logic.
- Same-namespace record dependencies are collected once and ordered by
  `order_records_topologically()`. The ready set uses source order as a
  deterministic tie-breaker; `std::set` also makes ancestor `__init__.py`
  output ordering deterministic.
- The `PlannedField` flags are somewhat verbose, but they match the backend's
  intentionally simple per-kind lowering model. No dead helper or obsolete
  `NotImplementedError` production path was found. The skeleton-era wording is
  stale documentation, not a live skeleton implementation.
- Generated record codecs delegate record validation to generated child
  helpers and delegate BRF primitives to the schema-neutral runtime. No
  generic reflection, callback codec, or cross-language abstraction is needed.
- Namespace package planning correctly emits ancestor `__init__.py` files and
  keeps a namespace's module below its namespace directory.

### Generated API

- All supported fields use the consistent `Optional[...] = None` shape.
- `None` omits a Field Directory entry; `[]` produces a present array with a
  zero count; an empty nested record is a present embedded record.
- `encode()`, `decode()`, and `encoded_size()` consistently delegate to
  private module helpers. Dataclass equality and repr behavior are naturally
  provided by `@dataclass`.
- Enums are generated as `enum.IntEnum`; list annotations preserve scalar,
  enum, string, bytes, and same-namespace record element types.
- Same-module forward references are solved by deterministic topological
  record ordering rather than string annotations or runtime reflection.
- The import-time Python generated-code epoch check is explicit and fails with
  `ImportError` before use of an incompatible generated module.

### Correctness and safety

- `pack_scalar`/`unpack_scalar` use explicit big-endian `struct` formats and
  preserve fixed-width signed, unsigned, bool, float32, and float64 behavior.
- String encoding uses `str.encode("utf-8")`; string bounds apply to encoded
  bytes. Bytes are preserved verbatim and do not undergo UTF-8 validation.
- Array helpers enforce `max_elements`, and variable-width helpers enforce
  per-element `max_bytes`, malformed varuint rejection, truncation rejection,
  and exact trailing-byte consumption.
- `parse_record()` validates header version, flags, reserved bytes, exact
  payload length, directory ordering/duplicates, field ranges, and overlap.
- Nested records and record arrays isolate complete child BRF payloads before
  invoking the child decoder, so child record IDs and child structural rules
  are checked by the existing child helper.
- `encoded_size()` is defined as `len(_encode_<record>(value))`, so agreement
  with actual encoding is guaranteed by construction.
- Python's arbitrary-size integers avoid the C/C++ integer-overflow hazards
  in offset arithmetic; the runtime still enforces the BRF's 64-bit varuint
  input bound.

### Wire parity

- Existing three-way interop coverage exercises byte identity and pairwise
  decoding for the common supported schema shapes. The PR-125 extension also
  exercises a same-namespace record array through C, C++, and Python.
- Python/C++ coverage is the applicable boundary for Python string/bytes
  arrays because the C backend intentionally does not support those element
  types. This is a documented capability difference, not an accidental
  acceptance/rejection mismatch.
- No difference was found in BRF header, Field Directory, scalar, enum,
  bounded string/bytes, fixed-width array, nested-record, or record-array
  framing for the overlapping field kinds.

## 4. Critical findings

### C1 — Accepted Schema IR names can generate invalid or broken Python

Evidence:

- `compiler/schema_ir/validation.cpp:is_valid_identifier()` checks ASCII
  identifier shape but does not reject Python keywords.
- `compiler/backend_python/backend_python.cpp:129` only applies a simple
  PascalCase-to-snake-case transform; no Python keyword or generated-symbol
  policy exists.
- `render_record_block()` emits field names directly into annotations,
  constructor keywords, and class bodies.

Consequences include:

- A field or record named `class`/`import` produces invalid Python syntax.
- A field named `encode`, `decode`, or `encoded_size` is first emitted as a
  dataclass field and then overwritten by the generated method. The default
  value and/or instance method behavior becomes incorrect.
- Distinct legal record names such as `FooBar` and `Foo_Bar` can map to the
  same private helper name (`foo_bar`), causing helper collision and wrong
  dispatch.
- Python keyword namespace segments make normal `from package.keyword.schema`
  imports invalid.

Expected benefit of fixing: every validated Schema IR shape either produces a
valid, unambiguous module or receives a deterministic backend diagnostic.
Estimated scope: medium. Risk: medium, because changing names affects the
generated public API; a reject-with-diagnostic policy is lower risk than
escaping in the first release. Release impact: **blocks public release**.

The existing design document mentions Python-keyword escaping as a known
limitation, but it does not cover method-name and snake-case helper
collisions. Those must be made explicit if deliberately deferred.

## 5. Important findings

### I1 — Runtime package metadata still describes a removed skeleton

Evidence:

- `runtime/python/pyproject.toml` describes the package as
  “skeleton -- no serialization helpers yet”.
- `runtime/python/src/quarry/runtime/python/__init__.py` repeats the same
  skeleton wording.
- `compiler/backend_python/README.md` opens with “through PR-123”, while the
  implementation includes PR-124 and PR-125.
- `docs/design/python-backend.md` still says the runtime boundary covers
  record arrays as unsupported in its runtime/limitations text.
- `docs/distribution-model.md` feature rows stop at PR-123.

Expected benefit of fixing: package metadata, generated-code users, and
release reviewers see an accurate supported surface. Scope: small. Risk:
low. Release impact: **blocks a credible public release**, though it cannot
change wire behavior.

### I2 — Nested diagnostic offsets are textual and not fully composed

The Python exception model is intentionally exception-based rather than the
C/C++ structured `CodecResult` model. Array helpers include element indexes
and local byte offsets. However, a record-array error wraps the child error
without translating a child `parse_record()` offset to the absolute offset in
the enclosing record, and Python has no structured path/offset fields.

This is an intentional language/runtime difference, not a wire defect. Scope:
medium if structured diagnostics are desired. Risk: medium because it would
expand the public Python error API. Release impact: defer; document the
difference and address only with demonstrated downstream demand.

### I3 — Mis-typed nested record values can leak non-codec exceptions

Scalar, enum, string, and bytes helpers convert ordinary value violations to
`EncodeError`. A caller who puts an arbitrary object or `None` inside a plain
nested-record field or record array can instead trigger `AttributeError` when
the generated child helper accesses a field. Dataclass annotations are not
runtime enforcement in Python.

Expected benefit of adding explicit type checks: consistent public exceptions
for misuse. Scope: small to medium in generated code or a small schema-neutral
record-value check. Risk: low implementation risk but adds generated/runtime
behavior and must define subclass/protocol policy. Release impact: important
polish; defer if the first release explicitly treats annotations as caller
contracts.

## 6. Minor findings

- Generated methods and fields have no return/type annotations beyond field
  annotations. This is usable but limits static checking; it belongs in a
  typing-improvements PR, not a codec redesign.
- `decode(cls, data)` is a classmethod for the public API but returns the
  concrete generated class rather than using `cls`. This is harmless for the
  generated classes and should either be documented as non-subclassable or
  revisited if subclassing is ever supported.
- `EncodeError` and `DecodeError` directly subclass `Exception` with no common
  `CodecError` base. Direct catching is coherent and minimal today; adding a
  hierarchy is optional polish, not a release blocker.
- `_encoded_size_<name>()` performs a full encode. It causes a second full
  encoding when callers ask for size after encoding, but it is linear rather
  than pathological and guarantees correctness.
- Runtime public functions assume their documented argument types. Direct
  misuse of schema-neutral functions such as `encode_record()` can expose a
  native `TypeError`; generated normal paths use valid types.
- `root_module_stem` is accepted as a free-form path component by the backend
  API. The CLI's output writer prevents paths outside the output root, but a
  stem containing separators can still produce surprising nested output.
  This is an API validation polish item, not a current generated-schema issue.

## 7. Simplification opportunities

No simplification is recommended for the current release candidate.

- Splitting the 1,132-line backend translation unit would introduce shared
  internal interfaces without an observed correctness, build-time, or
  ownership problem. The existing phase boundaries are readable.
- A generic recursive codec or callback-based runtime would reduce generated
  text at the cost of exactly the reflection/abstraction surface the approved
  architecture excludes.
- The repeated per-kind `PlannedField` flags are explicit, easy to audit, and
  keep the runtime schema-neutral. Replacing them with a generic tagged codec
  would be a redesign, not simplification.
- Record-array framing in generated code is intentionally local: it reuses
  `append_varuint`, `read_varuint`, and child helpers without adding a
  callback-based runtime API. Keep this boundary.

## 8. Test gaps

Existing coverage is strong: native and Docker CTest each passed 29/29 in the
PR-125 validation cycle; the Python runtime suite has 84 tests; generated
execution uses a real interpreter; and the interop target covers common
three-way byte identity and record arrays.

High-value gaps:

- No generation or execution test covers Python keywords, method-name fields,
  or helper-name collisions (`FooBar`/`Foo_Bar`). This is the most important
  missing test because it exposes C1.
- No clean virtual-environment wheel/sdist install test verifies that the
  installed runtime and generated package coexist and import together.
- No CI matrix explicitly tests the declared Python floor (`>=3.9`) or a
  supported set of Python versions; CI discovers whichever `python3` is
  installed.
- Tool tests cover Python output listing/determinism and generation, but
  installed-package/downstream tests are primarily native C/C++ package tests.
- Malformed-input coverage verifies broad rejection, but Python does not have
  structured-path/absolute-offset assertions comparable to the C++ diagnostic
  model. This is a parity/documentation gap, not evidence of accepted bad
  bytes.
- Namespace/package tests use safe `acme.*` names and intentionally avoid the
  `quarry` runtime-package collision.

Do not expand the suite for all of these in PR-126. Add only the identifier
contract and packaging/downstream tests in the next release-hardening PR.

## 9. Documentation gaps

Concrete stale or incomplete evidence:

- `runtime/python/pyproject.toml` and
  `runtime/python/src/quarry/runtime/python/__init__.py` still say skeleton.
- `compiler/backend_python/README.md` and parts of
  `docs/design/python-backend.md` stop feature status at PR-123 or describe
  record arrays as unsupported.
- `docs/distribution-model.md` Python feature rows stop at PR-123 and do not
  describe the current record-array capability or a release artifact flow.
- Runtime documentation does not give a complete clean-environment
  `pip install` plus generated-package import example.
- The known Python-keyword limitation is documented, but method/helper symbol
  collisions and the exact safe-name policy are not.
- The Python/C++-only boundary for string/bytes arrays is documented in older
  material, but the current full supported-shape matrix should explicitly
  include record arrays and distinguish C's missing variable-width arrays.

These are documentation-only observations for this audit; no documentation
was edited.

## 10. Packaging findings

The packaging model is structurally credible:

- `runtime/python/pyproject.toml` uses a standard setuptools `src` layout,
  package name `quarry-runtime-python`, version `0.1.0`, and
  `requires-python = ">=3.9"`.
- The import path `quarry.runtime.python` matches the generated import and the
  package directory layout.
- The runtime is intentionally pip-distributed rather than CMake-installed,
  which is appropriate for Python and is already described in
  `docs/distribution-model.md`.

Release evidence is incomplete:

- A direct `pip wheel --no-deps --no-build-isolation runtime/python` audit in
  this environment could not import `setuptools.build_meta`; this indicates
  the local non-isolated environment lacks the declared build backend. It is
  not proof that an isolated build fails, because `pyproject.toml` declares
  `setuptools>=68`, but a clean isolated wheel/sdist build was not verified.
- No repository test installs the runtime wheel/sdist into a clean virtual
  environment, invokes the installed schema compiler, and imports generated
  code against the installed runtime.
- The `quarry` top-level package collision with generated namespaces is a real
  coexistence hazard and is already acknowledged by the design document.
  Isolated output roots avoid it; a shared application `sys.path` does not.
- The runtime package has no explicit public export list or package version
  attribute. Neither is required for the current generated import path, but
  both would improve release discoverability.

Verdict for packaging: credible model, insufficient release verification, and
stale metadata that must be corrected before publication.

## 11. Performance posture

Performance is appropriate for a first release and no optimization PR is
justified by the audit.

- Scalar and fixed-width array encoding is linear.
- Variable-width arrays use a `bytearray` and extend operations, avoiding
  repeated immutable-byte concatenation.
- Record arrays encode each child once into a temporary bytes object so its
  length can be written, then extend the array buffer. This is an unavoidable
  straightforward consequence of the length-delimited format without adding
  a size-only or streaming abstraction.
- `encoded_size()` intentionally re-encodes the record; this is an obvious
  extra pass but not pathological without profiling evidence.
- Decode paths parse the enclosing record once and isolate each child span
  once. No repeated parsing of the same record was found.
- Schema recursion is constrained by backend dependency-cycle rejection; no
  unbounded recursive schema path is introduced by this backend.

## 12. Prioritized remaining roadmap

1. **Release contract hardening**: safe Python identifiers, generated-symbol
   collision policy, accurate runtime metadata/docs, clean wheel/sdist and
   downstream import validation, and a declared Python-version CI matrix.
2. **Cross-namespace Python references**: package imports and deterministic
   dependency handling for enum and record references, including array
   elements.
3. **Diagnostic parity**: structured Python error context or a documented
   stable exception-context model for field paths and absolute nested offsets.
4. **Downstream examples and publication**: installed compiler/runtime example,
   wheel/sdist publishing, and package coexistence guidance.
5. **Typing improvements**: return annotations, generated `bytes` input types,
   and an explicit policy for subclassing generated dataclasses.
6. **Performance benchmarking**: measure encode/decode and size-query costs
   before considering a size-only encoder or streaming buffers.

## 13. Recommended next PR

Recommend exactly one next PR:

**PR-127 — Python release contract and packaging hardening.**

It should establish a safe identifier/collision policy (prefer deterministic
backend rejection for keywords, method names, and helper-name collisions in
the first release), correct stale package metadata and status documentation,
and add a clean wheel/sdist plus installed generated-consumer validation. It
should also make the supported Python version matrix explicit in CI.

Cross-namespace support, diagnostic redesign, and performance work should not
be included in PR-127.

## 14. Exact PR boundary

In scope for PR-127:

- backend validation for Python keywords and generated symbol collisions;
- focused generation/execution tests for those rejection cases;
- truthful runtime description/version/public-surface metadata;
- corrected Python feature/status matrices through PR-125;
- wheel and sdist build/install/import validation in a clean environment;
- one downstream generated-package consumer test;
- explicit supported Python-version CI coverage.

Out of scope:

- Schema IR changes;
- compiler-pipeline changes;
- BRF wire-format changes;
- cross-namespace references;
- generic serialization frameworks;
- structured diagnostic redesign;
- performance optimization;
- generated public API redesign beyond the minimum safe-name policy.

## 15. Expected files to modify

Likely PR-127 files, subject to the repository's final CI/package design:

- `compiler/backend_python/backend_python.cpp`
- `compiler/backend_python/backend_python.hpp` only if an explicit naming
  policy helper/API is needed;
- `runtime/python/pyproject.toml`
- `runtime/python/src/quarry/runtime/python/__init__.py`
- `compiler/backend_python/README.md`
- `docs/design/python-backend.md`
- `docs/distribution-model.md`
- `tools/README.md` and/or top-level `README.md`
- focused backend Python and tool/package consumer tests;
- the Python CI workflow and/or a new packaging test script.

Those are expected files for the recommended future PR, not changes made by
this audit.

## 16. Validation plan

For PR-127:

1. Generate safe-name schemas and confirm byte-for-byte output is unchanged.
2. Feed keyword, method-collision, helper-collision, and keyword-namespace
   schemas through the real compiler and verify deterministic diagnostics.
3. Build a wheel and sdist in an isolated clean environment using the
   declared build requirements.
4. Install only the built runtime artifact into clean virtual environments for
   each supported Python version.
5. Generate a package with the installed compiler, import it with the
   installed runtime, and execute round-trip/error cases.
6. Run native configure/build/full CTest, Python runtime tests, and the full
   interop target.
7. Run Docker/Linux/GCC configure/build/full CTest.
8. Run `git diff --check` and verify no Schema IR or BRF bytes changed.

For this audit, the existing PR-125 validation evidence was reviewed; no
validation command modified repository files and no implementation was
performed.

## 17. Risks and rejected alternatives

- **Risk: escaping changes the public API.** Rejected as the default first
  release fix. Deterministic rejection is simpler, safer, and keeps generated
  names unsurprising; escaping can be a later, explicitly versioned API.
- **Risk: adding a generic runtime codec hides field-specific behavior.**
  Rejected. Existing generated loops plus schema-neutral varuint/record
  helpers are already minimal and auditable.
- **Risk: splitting `backend_python.cpp` creates artificial interfaces.**
  Deferred. The current single translation unit has clear internal phases and
  no demonstrated maintenance or build-time pain.
- **Risk: making Python errors match C++ `CodecResult` exactly expands the
  API.** Deferred. Exceptions are the intentional Python boundary; only
  demonstrated demand should justify structured diagnostic objects.
- **Risk: optimizing `encoded_size()` or record-array scratch buffers early.**
  Rejected. The current behavior is linear and wire-correct; benchmark first.
- **Risk: changing the Python runtime package name to avoid `quarry` prefix
  collision.** Deferred. It would be a breaking import-path change; first
  release documentation and isolated package layout should establish the
  current contract, with a separate migration decision if shared `sys.path`
  use becomes a real requirement.

**Wire-compatibility conclusion:** no real Python/C++/C wire defect was found
for the supported overlapping field kinds. The audit's release blockers are
identifier safety and package/release contract accuracy, not encoding bytes.

## PR-127 pre-implementation name policy

The generated identifier inventory is:

- schema-derived namespace segments become package directories;
- the configured root module stem becomes the generated `.py` filename;
- record and enum names become module-level class names;
- enum values become class-body member names;
- record fields become dataclass attributes and constructor keywords;
- public generated methods are `encode`, `decode`, and `encoded_size`;
- private record helpers are `_quarry_`-prefixed generator-owned
  encode/decode/size functions;
- generated modules import `dataclass`, `IntEnum`, `Optional`, `_brf`, and the
  Python epoch constant;
- generated codec bodies use fixed function-local names; schema fields that
  would shadow decoder locals are reserved and escaped;
- generated `__init__.py` files currently contain only a docstring and expose
  no schema-derived symbols.

The user-schema-derived names are namespace segments, module stem, record
names, enum names, enum values, and field names. The imported symbols, public
methods, private helpers, temporary names, and package marker contents are
generator-owned.

The Python-backend-specific policy to implement is:

1. Normalize each name deterministically: replace characters outside the
   ASCII Python identifier set with `_`, prefix a leading digit with `_`, and
   use `_` for an empty result. Then append `_` to Python keywords, so
   `class` becomes `class_` and `import` becomes `import_`.
2. Preserve an already-safe name unchanged unless it is generator-reserved in
   its context. Context-reserved field/class/import names receive a trailing
   `_`; public method names are reserved for fields, and imported runtime/
   decorator/type names are reserved for module-level classes and fields.
3. Reject Python dunder/sunder-style reserved names rather than guessing at
   their runtime meaning.
4. Detect collisions after normalization and escaping within every relevant
   namespace: package paths, module paths, module-level classes/helpers,
   enum members, and fields. Never apply an arbitrary numeric suffix. The
   diagnostic names both original identifiers, their generated spelling, and
   the owning namespace/record.
5. Use a dedicated `_quarry_` prefix for private generated helpers, avoiding
   Python's double-underscore class name mangling, while checking generated
   symbols against user-derived module names. Function-local codec names are
   scope-isolated and schema fields that would shadow them are escaped. No
   new public generated symbols are exposed.

This policy preserves field indexes and all wire names; only Python source
identifiers change. It does not add cross-namespace imports. The Python epoch
will remain unchanged because this PR changes generated source naming and
diagnostics, not the callable runtime protocol or BRF bytes.

## PR-127 implementation report

### Executive summary and verdict

PR-127 hardens Python generated identifiers and removes stale skeleton release
metadata. The Python backend is ready with minor polish within its documented
scope. First public release remains deferred until PR-128 completes clean
wheel/sdist and downstream-install validation.

### Findings addressed

- Added one deterministic Python-specific identifier mapping for namespaces,
  module stems, records, enums, enum members, fields, and private helpers.
- Keywords receive a trailing underscore; illegal ASCII characters become
  underscores; leading digits receive a leading underscore.
- Dunder/sunder conventions and the `_quarry_` generator prefix are rejected.
- Post-normalization collisions are rejected without numeric suffixes, with
  diagnostics naming source identifiers, generated target, and owner.
- Reserved field names include `encode`, `decode`, `encoded_size`, runtime
  imports/types, and generated decoder locals. Enum `name`/`value` collisions
  are escaped.
- Package/module paths use the same mapping and duplicate normalized paths are
  diagnosed with both source namespaces.
- The public API remains `value.encode()`, `Record.decode(data)`, and
  `value.encoded_size()`; the Python epoch remains 1.

### Tests and interoperability

Added generation tests for keywords, reserved methods, post-sanitization
collisions, reserved dunder names, escaped namespace/module paths, and
deterministic planning. Added a real Python execution test covering escaped
namespace, record, enum, member, and field names. Extended three-way C/C++/
Python interop with a safely escaped Python `Optional` field and byte identity.
Existing runtime, generated execution, malformed-input, package, and full
interop coverage remains passing.

### Documentation and metadata

Corrected the Python backend README, design document, distribution model,
tools README, runtime package docstring, and runtime project description.
Updated the Python roadmap through PR-127 and recorded PR-128's release
blocker. No stale skeleton claim remains in release-facing Python materials.

### Exact tracked files changed

`compiler/backend_python/backend_python.cpp`,
`compiler/backend_python/README.md`, `docs/design/python-backend.md`,
`docs/distribution-model.md`, `runtime/python/pyproject.toml`,
`runtime/python/src/quarry/runtime/python/__init__.py`,
`tests/backend_python/backend_python_test.cpp`,
`tests/backend_python/python_execution_test.cpp`,
`tests/interop/python_cpp_c_codec_interop_test.cpp`,
`tests/tools/schema_compiler_tool_test.cpp`, and `tools/README.md`.

### Validation

- Native configure/build: passed.
- Native full CTest: passed after Docker validation and native restore.
- Python runtime and generated execution tests with Python 3: passed.
- C/C++/Python interoperability: passed.
- `git diff --check`: passed.
- Docker/Linux/GCC clean build and full CTest: 29/29 passed.

Schema IR, the compiler pipeline, BRF wire format, and generated public API
were not changed. Deviations from scope: none. Wheel/sdist publication and
downstream installation validation remain intentionally deferred to PR-128.

## PR-128 implementation report

### Executive summary and release verdict

PR-128 completed the PR-126 packaging blocker. The Python runtime builds as a
pure-Python wheel and source distribution, installs in clean virtual
environments without the repository on `PYTHONPATH`, and supports generated
code from an installed Quarry compiler. The backend is ready for a first
supported release within its documented scope; PyPI publication remains a
release-process step, not an implementation blocker.

### Package metadata and artifacts

The package is `quarry-runtime-python`, version `0.1.0`, importable as
`quarry.runtime.python`, requires Python `>=3.9`, has no runtime dependencies,
and uses setuptools package discovery from `src/`. `python -m build` was used
through the standard frontend (with `--no-isolation` in the repository test
because the validation environments are network-isolated). Both artifacts
were built and inspected:

- `quarry_runtime_python-0.1.0-py3-none-any.whl`
- `quarry_runtime_python-0.1.0.tar.gz` (the sdist spelling may use the
  distribution's hyphenated name with older setuptools)

The wheel contains the intended `quarry/` runtime package and metadata only;
the sdist contains the source package, package metadata, README, and tests
needed to rebuild it, with no repository archive or `.git` content.

### Clean downstream validation

`runtime/python/tests/test_packaging.py` creates temporary virtual
environments, strips `PYTHONPATH` and `PYTHONHOME`, installs the wheel with no
network or dependencies, checks importable runtime helpers, exceptions,
metadata, and epoch, and repeats installation from a wheel built from the
sdist. It installs the locally built compiler into a clean prefix, generates
a representative YAML-expressible package, and runs a downstream consumer
covering scalar, enum, bounded string, bounded bytes, and fixed-width array
fields through import, construction, encode, decode, and `encoded_size()`.
Nested records and record arrays remain covered by the existing direct
Schema-IR/interoperability tests because the current one-record YAML
frontend cannot express distinct nested record types.

The same test creates a controlled runtime copy reporting epoch `999` and
confirms generated-module import fails before codec use. The diagnostic names
both epochs and recommends regenerating code or installing a compatible
runtime.

### Implementation and documentation changes

The epoch diagnostic in `compiler/backend_python/backend_python.cpp` now
reports expected and installed values. `runtime/python/pyproject.toml` adds
standard build metadata and test tooling; `tests/CMakeLists.txt` runs the
packaging test when its build prerequisites are available. Docker includes
the isolated packaging prerequisites. Installation and usage instructions
were added or corrected in the top-level README, backend README, runtime
README, design/distribution documents, tools README, and backlog.

Exact files changed:

`Dockerfile`, `README.md`, `compiler/backend_python/README.md`,
`compiler/backend_python/backend_python.cpp`, `docs/design/python-backend.md`,
`docs/distribution-model.md`, `jira/backlog.md`,
`runtime/python/README.md`, `runtime/python/pyproject.toml`,
`runtime/python/tests/test_packaging.py`, `tests/CMakeLists.txt`, and
`tools/README.md`.

### Validation

- Native configure/build and full CTest: 30/30 passed after Docker restore.
- Native clean wheel/sdist and downstream/epoch test: passed with Python 3.14.
- Docker/Linux/GCC clean build and full CTest: 30/30 passed with Python 3.12.
- `git diff --check`: passed.
- Existing Python runtime, generated execution, and C/C++/Python
  interoperability tests: passed.

The generated-code API epoch remains `1`: the callable runtime contract is
unchanged. BRF, Schema IR, compiler pipeline, and generated public API were
unchanged. The project/compiler version, runtime distribution version, and
generated-code epoch remain separate values (`0.1.0`, `0.1.0`, and `1`).

Remaining non-blocking roadmap: clean publication rehearsal and downstream
consumer matrix, PyPI publication, broader Python-version CI, and performance
benchmarking. Deviations from scope: none.

## PR-129 — Cross-Backend Release Readiness Audit

### 1. Executive summary

The C++, C, and Python backends are independently implemented, consume Schema
IR directly, and produce the same BRF representation for their documented
common subset. Native and Docker validation are already green, the C++ and C
CMake packages have downstream consumer tests, and the Python runtime has
wheel/sdist plus clean-environment downstream validation.

No real wire-compatibility defect was found. The remaining differences are
intentional backend boundaries: C is strict C99 with fixed-capacity storage,
Python has no cross-namespace generated imports, the C backend lacks arrays of
string/bytes, and the YAML source frontend describes one primary record only.

### 2. Release verdict

**Ready with minor polish** for a first supported Quarry release within the
scope below.

The verdict assumes “first release” means a versioned source/binary release
whose documented artifacts are consumable. Python PyPI publication and
release automation are not yet complete, but are distribution-process work,
not a correctness blocker. If “public release” specifically requires an
immediately published PyPI package, that publication step remains a release
gate outside the implementation completed so far.

### 3. Supported release scope

The v0.1 release scope is:

- BRF v0.1 records, field directories, varuints, scalar values, enums,
  bounded strings, bounded bytes, bounded arrays, same-namespace nested
  records, and same-namespace record arrays.
- C++ generated code and header-only `Quarry::runtime`, including
  cross-namespace enum/record references supported by its generated include
  planner.
- C99 generated `.h`/`.c` code and `Quarry::runtime_c`, with no heap
  allocation, caller-owned output, fixed-capacity strings/bytes/arrays, and
  same-namespace enum/record references only.
- Python dataclass modules and `quarry-runtime-python`, with standard-library
  runtime code, `Optional[...]` presence semantics, deterministic name
  escaping, same-namespace enum/record references, and wheel/sdist
  installation.
- The YAML frontend's one-document, one-primary-record contract. Additional
  records and richer schema composition are available through validated
  Schema IR tests, not the current YAML authoring surface.

Nested arrays, C arrays of string/bytes, Python cross-namespace references,
C cross-namespace references, recursive by-value schemas, and Python package
publication automation are deferred. They are not silently treated as
portable across all three backends.

### 4. Confirmed strengths

- `compiler/backend/backend.cpp`, `compiler/backend_c/backend_c.cpp`, and
  `compiler/backend_python/backend_python.cpp` are independent lowering and
  rendering implementations; Python does not depend on either native
  backend.
- `tests/interop/python_cpp_c_codec_interop_test.cpp` verifies byte identity
  and pairwise decoding for the common scalar/enum/string/bytes/fixed-array
  shape, plus top-level truncation, trailing bytes, unknown enum values, and
  malformed UTF-8.
- The same interop test covers three-way same-namespace nested-record
  encoding/decoding. `tests/interop/c_cpp_nested_record_interop_test.cpp`
  additionally exercises C/C++ record arrays, wrong embedded IDs,
  truncation, and trailing bytes.
- `tests/interop/python_cpp_c_codec_interop_test.cpp` has a Python/C++
  variable-width string/bytes-array test because the C backend intentionally
  does not support those element types.
- Runtime unit tests cover structural Field Directory validation, bounds,
  varuint failures, UTF-8, presence, nested records, and record arrays in
  each runtime's native model.
- Generated-code epochs are independent and explicit: C++ epoch `3` in
  `QUARRY_GENERATED_CODE_API_VERSION`, C epoch `2` in
  `QUARRY_GENERATED_CODE_API_VERSION_C`, and Python epoch `1` in
  `QUARRY_GENERATED_CODE_API_VERSION_PYTHON`.
- CMake package consumer tests cover installed C++/C runtime/compiler use;
  `runtime/python/tests/test_packaging.py` covers wheel, sdist, clean venvs,
  installed compiler generation, downstream execution, and epoch mismatch.
- Docker CI installs the required toolchain and runs the same full CTest
  suite. The final PR-128 validation completed 30/30 tests in native and
  Docker environments.

### 5. Critical findings

None. No confirmed BRF byte-level incompatibility, presence-semantics defect,
record framing defect, wrong-record-ID acceptance, or epoch-pairing failure
was found in the reviewed implementation and tests.

### 6. Important findings

#### Intentional feature asymmetry limits the portable subset

Evidence: `compiler/backend/backend.cpp` handles cross-namespace named types
and string/bytes arrays; `compiler/backend_c/backend_c.cpp` explicitly rejects
cross-namespace references and string/bytes array elements; the Python backend
has the same-namespace restriction in `compiler/backend_python/backend_python.cpp`.
The C and Python READMEs state these restrictions, and the C/Python interop
test explicitly records why string/bytes arrays are tested only C++/Python.

Impact: schemas using these shapes cannot be selected as portable across all
three generated languages. This is an intentional capability boundary, not a
wire defect.

Scope/risk: expanding it requires backend-specific generation, import or
symbol planning, storage decisions, and new interop tests. It does not require
a BRF change. It does not block release if the common subset is the contract.

#### Malformed-input parity is broad but not exhaustive

Evidence: common three-way tests cover top-level truncation/trailing bytes,
unknown enum values, malformed UTF-8, and nested-record framing. Runtime
tests cover more individual structural cases. Variable-width string/bytes
array malformed payloads are exercised through Python and C++ only because C
does not generate those fields. There is no single all-three corpus covering
every Field Directory ordering, overlap, malformed-varuint, enum-width, and
nested-array rejection permutation.

Impact: future validation-order or diagnostic differences could escape the
cross-language suite even though the existing implementations independently
reject the tested malformed inputs.

Scope/risk: a focused shared malformed corpus would be moderate test work and
could expose intentional status/diagnostic differences. It is valuable but
does not block release because each runtime has direct structural tests and
the BRF contract is explicit.

#### Epoch diagnostics are not equally detailed

Evidence: Python now reports expected and installed epochs at import time.
C++ emits a compile-time `static_assert` message and C emits a preprocessor
`#error`, but neither message prints both numeric values. CMake's installed
`QuarryGenerate.cmake` helper performs an explicit compiler/runtime version
comparison for the C++ package path; no equivalent installed helper exists for
C or Python generation.

Impact: failures are early and actionable, but numeric diagnosis and
automation are less uniform across backends.

Scope/risk: small diagnostic/test/documentation polish; no wire or API change
needed. Non-blocking.

### 7. Minor findings

- `docs/design/python-backend.md` retains historical milestone prose that
  reads awkwardly as current status (for example, the PR-118 “zero-field only”
  sentence) and includes an epoch-check example with the older generic error
  text, while generated Python now reports both epoch values. Non-blocking
  documentation drift.
- `compiler/backend_c/README.md` correctly says record arrays are supported,
  but its later deferred-work paragraph still lists “arrays of records” among
  deferred items. This is a concrete matrix inconsistency, not an
  implementation defect; non-blocking.
- The Python roadmap says cross-namespace references are “out of scope for
  every backend,” although the C++ backend and its
  `cross_namespace_reference` tests support them. This should be corrected in
  a documentation pass; non-blocking.
- Python's compiler epoch literal and runtime epoch literal are intentionally
  separate copies. The packaging test proves mismatch behavior, but a future
  edit can still update one without the other. The existing explicit policy
  and import-time guard make this a maintainability risk, not a release
  blocker.
- Generated Python package directories do not re-export records from
  `__init__.py`; consumers import the concrete module. This is documented and
  deterministic, but examples should consistently show the concrete import.

### 8. Cross-backend feature matrix

| Field/schema capability | C++ | C | Python | Release status |
|---|---|---|---|---|
| bool and fixed-width integer scalars | Yes | Yes | Yes | Common |
| f32/f64 | Yes | Yes | Yes | Common |
| enum field, same namespace, non-negative values | Yes | Yes, with C range restrictions | Yes | Common subset |
| cross-namespace enum field | Yes | No | No | C++ only |
| bounded string field | Yes | Yes | Yes | Common |
| bounded bytes field | Yes | Yes | Yes | Common |
| arrays of scalar elements | Yes | Yes | Yes | Common |
| arrays of enum elements | Yes | Yes, same namespace/non-negative | Yes, same namespace/non-negative | Common subset |
| arrays of string/bytes elements | Yes | No | Yes | C++/Python |
| same-namespace nested record | Yes | Yes | Yes | Common |
| cross-namespace nested record | Yes | No | No | C++ only |
| same-namespace record arrays | Yes | Yes | Yes | Common |
| cross-namespace record arrays | Yes | No | No | C++ only |
| nested arrays | No, frontend rejects | No, frontend rejects | No, frontend rejects | Deferred globally |
| YAML multiple primary records | No | No | No | YAML limitation |
| Schema IR multi-record tests | Yes | Yes where backend supports shape | Yes where backend supports shape | Test/integration surface |

The common portable subset is therefore all scalar types, bounded scalar
string/bytes, bounded scalar/enum arrays, same-namespace non-negative enums,
same-namespace nested records, and same-namespace record arrays, excluding
string/bytes array elements and cross-namespace references.

### 9. Wire-compatibility assessment

BRF framing is centralized by specification, not by a shared runtime. The
reviewed implementations agree on big-endian scalar values, unsigned
varuints, Field Directory ordering, bounded variable-length payloads, enum
width selection for non-negative values, record headers, nested complete
records, record-array length prefixes, absent fields, and present-empty
arrays.

The strongest evidence is the active interop suite: three-way byte-for-byte
encoding and cross-decoding for the common scalar/enum/string/bytes/fixed-array
schema; three-way nested-record encoding and decoding; C/C++ record-array
framing; and Python/C++ variable-width arrays. Runtime tests independently
reject malformed headers, directories, overlaps, bounds, varuints, UTF-8,
wrong record IDs, truncation, and trailing data.

No real compatibility defect was identified. The principal coverage limits
are (a) C cannot participate in string/bytes-array tests, (b) cross-namespace
shapes are C++-only, and (c) enum-width edge cases are not all represented in
one three-way fixture. These are coverage/scope limits and should not be
described as universal backend parity.

### 10. Distribution assessment

#### C++

`cmake --install` exports the runtime target, headers, package config, schema
compiler, and CMake generation helper. Consumer tests compile and run against
the installed package and check compiler/runtime generated-code API metadata.
This is a credible first-release path.

#### C

The installed package exports `Quarry::runtime_c`, C headers/version metadata,
and the schema compiler. C consumer tests compile strict C99 generated code
against the installed package. There is no C-specific CMake generation helper;
direct installed CLI generation is the documented path. This is credible but
less ergonomic than C++.

#### Python

`runtime/python/pyproject.toml` defines a pure-Python `quarry-runtime-python`
wheel/sdist with Python `>=3.9` and no runtime dependencies. PR-128's clean
venv test validates artifact contents, wheel installation, sdist rebuild,
installed compiler generation, downstream imports, encode/decode/size, and
epoch mismatch. The package is not yet published to PyPI, so publication and
release automation remain operational work.

#### Docker and toolchain

The Docker development image builds the compiler and runs the full CTest suite,
including Python packaging validation. The image's Ubuntu/GCC environment is
credible CI coverage, but it does not constitute a complete platform matrix.

### 11. Documentation assessment

The principal support claims in `README.md`, backend/runtime READMEs,
`docs/distribution-model.md`, the BRF specification, and `tools/README.md`
are substantively aligned with the implementation. The known restrictions
and C/Python variable-array split are documented.

The concrete cleanup items are the historical Python epoch example, the C
README's contradictory deferred record-array sentence, and the Python roadmap
claim that cross-namespace references are out of scope for every backend.
These should be corrected before a polished release announcement, but none
changes generated code or BRF behavior.

### 12. Compatibility/versioning assessment

The package release version (`0.1.0`), C++ epoch (`3`), C epoch (`2`), Python
runtime distribution version (`0.1.0`), and Python generated-code epoch (`1`)
are intentionally independent. No backend's epoch is a BRF wire version or a
full package semantic-version guarantee.

Generated C++ code fails at compile time through `static_assert`; generated C
headers fail at preprocessing through `#error`; generated Python modules fail
at import through `ImportError`. The checks occur before normal codec use.
The versioning model is adequate for the first release, with the diagnostic
uniformity and duplicated Python literal risks above deferred as polish.

### 13. Remaining roadmap

Priority order:

1. C cross-namespace enum/record references, including record-array elements.
2. A deliberate Python cross-namespace import/package model.
3. Multi-record YAML or an explicit multi-file/import authoring contract.
4. Shared malformed-input parity fixtures and diagnostic-path parity where
   consumers demonstrate need.
5. C arrays of string/bytes, requiring fixed-capacity representation and
   caller-owned storage decisions.
6. Downstream examples for C and Python, then Python-version CI coverage.
7. Release publication automation, wheel/sdist publishing, and provenance.
8. Performance benchmarking before considering any codec optimization.
9. Rust/Go preparation only after the language-neutral release contract is
   stable.

These are roadmap items, not hidden release blockers. The first item has the
largest effect on the cross-backend feature gap while preserving the existing
BRF and Schema IR contracts.

### 14. Recommended next PR

**PR-130 — C Cross-Namespace References**

This is the single recommended next PR because C++ already supports the
feature, while C is the largest remaining backend portability gap that can be
addressed without changing BRF or Schema IR. Python cross-namespace imports
and YAML multi-record authoring should remain separate decisions.

### 15. Exact boundary

PR-130 should implement only same-schema/direct-Schema-IR cross-namespace
enum and record references in the C backend, including record-array element
references. It should add the required generated include/declaration planning,
preserve fixed-capacity/no-heap C storage, add direct Schema-IR generation and
C/C++ interop coverage, and update only the C support matrix needed to state
the new boundary. It must not change BRF bytes, Schema IR, the YAML frontend,
Python imports, nested arrays, or C runtime ownership semantics.

### 16. Expected files

Expected implementation/test boundary:

- `compiler/backend_c/backend_c.cpp` and, only if required, its private
  header;
- `tests/backend_c/backend_c_test.cpp`;
- `tests/interop/c_cpp_nested_record_interop_test.cpp` and/or a focused
  cross-namespace interop fixture;
- C backend design/README support statements;
- `jira/backlog.md`.

No Python backend, Schema IR, compiler pipeline, BRF specification, or
generated public API redesign should be included.

### 17. Validation plan

For PR-130, run generation from direct Schema IR for cross-namespace enum,
plain record, record-array, forward-reference, sibling-namespace, and
multiple-namespace cases. Compile generated C as strict C99 with warnings as
errors, run C and C++ byte identity/cross-decoding, and reject wrong IDs,
truncation, malformed directories, and trailing bytes.

Also run `cmake --preset debug`, native build and full CTest, Docker/Linux/GCC
build and full CTest, installed C package consumer tests, `git diff --check`,
and a native rebuild after Docker validation. Confirm C/Python restrictions
and all three generated-code epochs remain unchanged.

### 18. Risks and rejected alternatives

- Rejected a shared multi-language codec abstraction: the existing independent
  runtimes are deliberately small and have already demonstrated wire parity.
- Rejected treating C++-only cross-namespace support as common parity: doing
  so would make the release matrix inaccurate.
- Rejected making C or Python silently accept unsupported cross-namespace
  fields: partial or ambiguous generated output would be more dangerous than
  explicit diagnostics.
- Rejected changing BRF or Schema IR to solve backend feature differences:
  the current wire contract and IR already represent the needed references.
- Rejected making PyPI publication a code-correctness blocker for this audit;
  it remains an explicit operational release step.
- Rejected broad performance work: current runtimes show no demonstrated
  pathological behavior in the reviewed encode/decode paths, and C's
  fixed-capacity trade-offs are part of its documented design.

### Audit conclusion

Within the documented v0.1 scope, Quarry is ready with minor polish. The
remaining differences are explicit, test-backed, and safe to defer. No
production changes, tests, documentation files, commits, or pushes were made
for PR-129; only this local `REPORT.md` audit was updated.
## PR-130 Investigation — C Arrays of Bounded Strings and Bytes

### 1. Executive summary

The remaining C backend gap is implementable without changing Schema IR, the
compiler pipeline, BRF, or the generated-code epoch. Schema IR already
represents array max_elements and nested StringType or BytesType max_bytes.
The C backend already has bounded-copy, UTF-8, varuint, Field Directory, and
fixed-capacity mechanisms.

The smallest correct implementation is one PR covering array<string> and
array<bytes>. They share the element representation, count/length framing,
encode/decode loops, bounds checks, and tests. The only semantic difference is
UTF-8 validation and the string element's optional NUL terminator.

Recommendation: implement both together as PR-131 — C String and Bytes Arrays.
No production support was added by this investigation.

### 2. Current architecture findings

The C implementation is in compiler/backend_c/backend_c.cpp. FieldEncoding
already carries scalar/enum, string, bytes, array, and record metadata. The
relevant existing paths are:

- plain strings: lower_field_encoding, declaration rendering,
  render_string_field_build, and render_string_field_decode;
- plain bytes: render_bytes_field_build and render_bytes_field_decode;
- scalar/enum arrays: render_array_field_build and render_array_field_decode;
- record arrays: render_record_array_element_build and
  render_record_array_element_decode;
- field assembly: render_build_fields_loop, scratch declarations, and
  quarry_c_encode_record;
- decode state: generated decoders zero the result value before parsing, so a
  failed result is not meaningful except for status and diagnostic offset.

The current array path checks count before indexing generated storage and bounds
the size-only loop by the schema count. Its fixed-width scratch sizing and
fixed-width element loop are the parts that need a variable-width branch.

### 3. Schema IR readiness

Schema IR is ready; no change is required.

Evidence:

- ArrayType has FieldType element_type and uint32 max_elements.
- StringType has uint32 max_bytes.
- BytesType has uint32 max_bytes.
- FieldType.array.element_type can already be nested FieldType.string or
  bytes.

compiler/semantic/semantic.cpp recursively resolves the element type, clears
outer bounds while resolving the element, validates the element max_bytes, and
stores both limits in Schema IR. It separately rejects nested arrays. The
Schema IR validator validates array and element bounds. No required language-
neutral information is missing.

### 4. Proposed generated C representation

Generate one named C99 element type per variable-width array field, using the
owning record symbol and field index rather than the user field name:

    typedef struct {
        char value[9];       /* max_bytes + 1 */
        uint32_t length;     /* authoritative UTF-8 byte length */
    } quarry_telemetry_Sample_array_2_string_element_t;

    typedef struct {
        bool has_labels;
        quarry_telemetry_Sample_array_2_string_element_t labels[4];
        uint32_t labels_count;
    } quarry_telemetry_Sample_t;

For bytes:

    typedef struct {
        uint8_t value[8];    /* exactly max_bytes; no terminator */
        uint32_t length;     /* authoritative raw-byte length */
    } quarry_telemetry_Sample_array_3_bytes_element_t;

The exact suffix may use the existing C symbol convention, but it must be
deterministic and field-index-derived. The parent field keeps existing
has_<field>, storage, and field_count conventions.

This is preferable to an anonymous inline element struct because anonymous
structs are not portable strict C99. It is preferable to parallel value and
length arrays because each element's storage and active length remain together
and callers can inspect record->labels[i].value and .length. A pointer,
arena, view, or generic runtime container would violate the current C model.

The representation provides fixed caller-owned storage, no heap allocation,
compile-time max_elements and max_bytes capacities, an active uint32_t count,
a uint32_t length per occupied element, present-empty arrays, and empty
elements.

### 5. BRF compatibility analysis

The existing BRF specification defines:

    array payload = count varuint
                    repeated element-length varuint + element bytes

For array<string>, element bytes are raw UTF-8 with no terminator and the
element bound is measured in encoded UTF-8 bytes. For array<bytes>, element
bytes are arbitrary raw bytes and the bound is measured directly in bytes.
There is no array-level length, offset table, padding, or terminator.

Canonical cases are absent array with no Field Directory entry, present empty
array payload 00, one empty element payload 01 00, maximum count, and maximum
element lengths encoded in order.

The C++ variable-array renderer and Python helpers already implement this
format. C should write the count varuint, then each element length varuint and
exactly its active bytes. No BRF change is needed.

Malformed input must reject malformed count or element-length varuints, count
over max_elements, element length over max_bytes, payload truncation,
malformed UTF-8 strings, and trailing bytes after the declared elements.

### 6. Encode and decode behavior

Validated C encode should:

1. Omit the field when has_<field> is false.
2. Check field_count <= max_elements before writing field bytes.
3. Check each element length <= max_bytes before reading storage.
4. Validate UTF-8 for string elements only.
5. Write count, element lengths, and exactly each element's active bytes into
   the existing field scratch writer, then pass one completed field to record
   assembly.

This follows current validation-before-assembly behavior and uses
BOUNDS_EXCEEDED, INVALID_UTF8, and INSUFFICIENT_CAPACITY. No new status is
justified.

The existing size-only path should remain non-validating but must bound its
loop by max_elements. Its field payload size is:

    varuint_size(count) + sum(varuint_size(length[i]) + length[i])

Decode should zero the result as today, read and bound the count, read and
bound each element length, check remaining bytes, copy with
quarry_c_copy_bounded, validate string UTF-8, set the element length, advance
exactly by that length, require full field consumption, and commit the array
count/presence only after success.

On failure, the result remains the existing zeroed/partial result with
status != OK. No transactional decode model is needed. Use the existing
field_view.byte_offset plus array_reader.offset convention. Element UTF-8
failure can report the element start, matching current C string granularity.

### 7. Runtime impact

No runtime change is required. Reuse quarry_c_read_varuint,
quarry_c_write_varuint, quarry_c_reader_t, quarry_c_writer_t,
quarry_c_copy_bounded, quarry_c_is_valid_utf8, existing statuses and offsets,
and existing record assembly/parsing.

The loops belong in generated code because capacities and destination members
are schema-specific. A callback/type-descriptor collection helper would add
unnecessary runtime surface. The C generated-code epoch remains 2 because no
runtime symbol or callable contract changes.

### 8. Generated-name analysis

Use names such as:

    quarry_telemetry_Sample_array_2_string_element_t
    quarry_telemetry_Sample_array_3_bytes_element_t

The record symbol plus field index plus element kind avoids collisions from
field spellings, C keywords, or similar fields. Element members value and
length have their own struct-member scope. The parent retains existing field,
has_<field>, and field_count names.

Generated locals may use the current field-derived style with names such as
field_array_writer, field_element_length_raw, and field_element_index. The
new helper type should not use a raw user field spelling. Do not broaden C
identifier sanitization or add a cross-backend naming framework.

### 9. Testing plan

Backend generation tests in tests/backend_c/backend_c_test.cpp should cover
one string array, one bytes array, multiple arrays with distinct capacities,
boundary capacities, nested records containing these arrays, generated
declarations and loops, scratch sizing, UTF-8/copy calls, deterministic
plan/generate output, and unchanged nested-array/cross-namespace failures.

Real strict-C99 execution tests should cover absent and present-empty arrays,
one element, maximum count, empty elements, maximum-length elements, mixed
lengths, binary zero bytes, count overflow, element-length overflow, malformed
count and length varuints, truncation, malformed UTF-8, trailing bytes,
encode status/offset, and the existing partial-result contract. Use execution
tests, not only source-text assertions.

Extend the existing interop fixture with direct Schema IR if necessary for
element bounds. Verify C encode to C++ decode, C++ encode to C decode, C
encode to Python decode, Python encode to C decode, and the existing C++/
Python directions. Use empty values, multibyte UTF-8 at the limit, maximum
binary values, embedded zero bytes, and mixed lengths. Test shared malformed
inputs where all three backends support the shape.

Run installed C package consumer tests, native and Docker full CTest, strict
C99 compilation with -std=c99 -Wall -Wextra -Wpedantic -Werror, and verify
the C epoch remains 2.

### 10. Documentation impact

The implementation PR should update only affected support statements in
compiler/backend_c/README.md, runtime_c/README.md, docs/design/c-backend.md,
README.md, tools/README.md, docs/distribution-model.md, and jira/backlog.md.

The BRF specification needs no change. Keep cross-namespace, nested-array,
and YAML claims unchanged. The current runtime header contains stale
historical scope wording saying arrays are unsupported; it should be corrected
when the implementation lands, without adding schema-specific runtime logic.

### 11. Recommended PR decomposition

Implement both categories together. They share the named element struct,
count/length varuint loop, capacity checks, bounded copy, scratch sizing,
decode commit ordering, trailing-byte check, and interop fixture. Splitting
them would duplicate almost all machinery and leave the C representation
half-finished. The only conditional branch is string UTF-8/NUL behavior versus
raw bytes/no terminator.

### 12. Exact proposed implementation scope

PR-131 should:

1. Add small C-backend-specific variable-array element metadata.
2. Generate deterministic named C99 element structs and the parent
   has_/storage/_count layout.
3. Extend declaration, size, scratch sizing, encode, decode, and validation
   rendering for both categories.
4. Reuse current runtime functions and keep epoch 2.
5. Add generation, strict-C99 execution, malformed-input, and C/C++/Python
   interop coverage.
6. Update only affected support matrices and backlog.

### 13. Explicit non-goals

No Schema IR or compiler pipeline changes; no BRF or runtime wire-format
changes; no heap, arenas, zero-copy views, or generic containers; no
cross-namespace references; no nested arrays; no multi-record YAML; no Python
or C++ behavior changes beyond interop verification; no unrelated C name
hardening; no C epoch bump unless concrete evidence disproves the no-runtime
change approach; and no unrelated cleanup.

### 14. Risks and open questions

- max_elements multiplied by max_bytes can make generated structs and scratch
  buffers large. This is deterministic and consistent with the C contract,
  but large schema bounds may need an existing-style planning diagnostic.
- Named element structs are visible in generated headers. That is the simplest
  portable C99 representation; hiding them would require a non-standard
  anonymous struct or opaque pointer model.
- C permits direct struct mutation, so encode-time validation remains
  essential. Do not add setters or transactional decode.
- The UTF-8 helper returns only boolean validity. Report element start offsets
  unless a concrete requirement justifies a runtime API change.
- Existing C field identifiers are emitted directly. The field-index-derived
  helper type avoids a new collision class, but broader C name safety is a
  separate audit.

### 15. Final recommendation

Schema IR changes are not required. Use one deterministic named,
fixed-capacity element struct per variable-width array field, stored inline in
a fixed-capacity parent array with a count and per-element lengths. Runtime
changes are not required. Implement strings and bytes together in PR-131,
keep the C generated-code epoch at 2, and keep cross-namespace references and
multi-record YAML out of scope.

Expected PR-131 files are the C backend implementation and tests, the focused
C/C++/Python interop fixture, affected C/runtime/support documentation, and
the backlog. This investigation changed only local REPORT.md.

### 16. Validation and working-tree status

Baseline validation found no failures:

- Native focused CTest: 5/5 passed: backend_c_test, C runtime, C/C++
  interop, C/C++ nested/record-array interop, and three-way Python interop.
- Docker/Linux/GCC focused CTest: the same 5/5 passed.
- git diff --check passed for tracked changes; no tracked production, test,
  or documentation files were modified.
- REPORT.md remains ignored; no commit or push was performed for PR-130.

The working tree also contains untracked .vscode/ and quarry-main.tgz plus
generated packaging artifacts under runtime/python/. They were not touched or
cleaned because this task is investigation-only and excludes unrelated
cleanup.

## PR-131 — C String and Bytes Arrays

### Executive summary

Implemented bounded arrays of bounded strings and bytes in the independent C
backend. The implementation follows PR-130: strict C99, fixed caller-owned
storage, no heap allocation, no generic container framework, and composition
of existing C runtime primitives.

### Scope implemented

The C backend now supports arrays whose element type is bounded `string` or
bounded `bytes`, in addition to its existing scalar, enum, nested-record, and
record-array categories. Nested arrays and cross-namespace references remain
unsupported.

### Generated C representation

Each variable-width array field receives a named element type derived from its
own record symbol and Schema IR field index:

```c
typedef struct {
    char value[9];       /* max_bytes + 1 */
    uint32_t length;
} acme_telemetry_Sample_array_0_string_element_t;

typedef struct {
    uint8_t value[4];    /* max_bytes */
    uint32_t length;
} acme_telemetry_Sample_array_1_bytes_element_t;
```

The containing record retains `has_<field>`, fixed `[max_elements]` element
storage, and `<field>_count`. Strings reserve a trailing NUL; the explicit
length remains authoritative. Bytes have no terminator and preserve arbitrary
binary data, including zero bytes. Names use the field index, avoiding
collisions between similarly named fields and avoiding schema-name-derived
element helper collisions.

### Encode and decode implementation

Encoding writes the existing BRF variable-width array payload: count varuint,
then element-length varuint and exact element bytes. Generated loops validate
the count and each element length. String elements use the existing C UTF-8
validator; bytes elements perform no UTF-8 validation. `encoded_size()` uses
the same bounded generated loops without encode-time validation, matching the
existing C convention.

Decoding reads and bounds-checks the count, then each element length, checks
remaining payload bytes, copies into the fixed-capacity element storage using
`quarry_c_copy_bounded`, validates UTF-8 for strings, and advances by exactly
the declared length. A post-loop exact-consumption check rejects trailing
bytes. Existing C status and byte-offset behavior is preserved.

### Runtime and Schema IR impact

No C runtime helper was added. Existing varuint, bounded-copy, and UTF-8
helpers are sufficient. Schema IR was not changed; existing
`ArrayType.max_elements` and nested `StringType.max_bytes`/`BytesType.max_bytes`
provide all required information.

### BRF compatibility

The BRF wire format was not changed. C, C++, and Python now produce identical
bytes for representative arrays containing empty arrays/elements, multibyte
UTF-8, maximum-length values, mixed lengths, and embedded zero bytes.
Malformed count, oversized element length, truncation, and trailing payload
cases are rejected by all three harnesses.

### Tests and interoperability

Updated backend generation tests to assert named string/bytes element typedefs
and array storage. Extended the existing `python_cpp_c_codec_interop_test`
instead of adding duplicate infrastructure. It now generates and compiles a
strict-C99 harness, checks C/C++/Python byte identity and all relevant decode
directions, and tests malformed count and element-length rejection plus
trailing data. The existing C backend and nested/record-array tests remain
green.

### Documentation and exact files changed

Changed:

- `compiler/backend_c/backend_c.cpp`
- `compiler/backend_c/README.md`
- `docs/design/c-backend.md`
- `docs/distribution-model.md`
- `README.md`
- `tools/README.md`
- `jira/backlog.md`
- `tests/backend_c/backend_c_test.cpp`
- `tests/CMakeLists.txt`
- `tests/interop/python_cpp_c_codec_interop_test.cpp`
- `REPORT.md`

The separate packaging-test compatibility fix was previously pushed as
`a635867` and is not part of the PR-131 commit.

### Validation

Focused native validation passed:

- `cmake --preset debug`
- `cmake --build --preset debug --parallel`
- `backend_c_test`
- `python_cpp_c_codec_interop_test`
- strict C99 compilation of generated C and harness code

The packaging CI regression was separately fixed and pushed as commit
`a635867`; the native packaging test passes. Docker full-suite validation for
the current PR-131 changes remains to be run after the native validation
cycle, followed by native build restoration.

### API and compatibility confirmation

- Schema IR changed: no.
- C runtime changed: no.
- BRF changed: no.
- C generated-code API epoch changed: no; it remains `2`.
- C/C++/Python field-category parity: yes for the currently implemented
  schema categories, subject to the existing same-namespace and YAML
  limitations.

### Known limitations and deviations

None beyond the approved scope: cross-namespace references, nested arrays,
and multi-record YAML remain deferred. No public C API redesign, runtime
abstraction, heap allocation, or unrelated backend change was introduced.
