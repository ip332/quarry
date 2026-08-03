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

## PR-132 — C Backend Release-Readiness Audit

### 1. Executive summary

This audit examined the repository implementation rather than relying only
on earlier reports. The C backend has functional field-category parity with
the currently supported C++ and Python Schema IR subset, including PR-131
arrays of bounded strings and bounded bytes. BRF interoperability and the
30-test native and Docker suites pass.

One release-blocking correctness issue remains: Schema IR accepts names that
are lexically identifiers but are C keywords, while the C generator emits
schema field names directly. Generated suffix members such as
`<field>_length` and `<field>_count` can also collide with another legal
schema field name. Such schemas can produce invalid or ambiguous C
declarations. This is a narrow C name-safety defect, not a wire, runtime, or
Schema IR representation defect. No naming redesign is implemented here.

### 2. Audit scope

Reviewed `compiler/backend_c`, `compiler/schema_ir`, `runtime_c`, generated
code tests, C/C++/Python interoperability tests, installed-package consumer
tests, CMake package metadata, compiler output planning, and release-facing
documentation. Cross-namespace references, multi-record YAML,
diagnostic-path parity, benchmarking, and new schema features were excluded.

### 3. Repository baseline

The audited implementation is PR-131 commit `ca694f5` on `main`, with the
separate packaging-test fix `a635867` already pushed. PR-131 did not change
Schema IR, BRF, the C runtime, or the C generated-code API epoch. Existing
untracked `.vscode/`, `quarry-main.tgz`, and Python packaging artifacts were
left untouched.

### 4. C/C++/Python feature matrix

| Capability | C++ | C | Python | Audit result |
|---|---:|---:|---:|---|
| Scalar fields | yes | yes | yes | common subset |
| Enum fields | yes | yes, same namespace/non-negative values | yes | C restriction documented |
| Bounded strings | yes | yes | yes | common subset |
| Bounded bytes | yes | yes | yes | common subset |
| Arrays of scalars | yes | yes | yes | common subset |
| Arrays of enums | yes | yes, same namespace/non-negative values | yes | C restriction documented |
| Arrays of bounded strings | yes | yes since PR-131 | yes | parity achieved |
| Arrays of bounded bytes | yes | yes since PR-131 | yes | parity achieved |
| Nested records | yes | yes, same namespace | yes | namespace restriction documented |
| Arrays of records | yes | yes, same namespace | yes | namespace restriction documented |
| Optional/presence | yes | `has_<field>` | `Optional[...]` | intentional language difference |
| Cross-namespace references | backend-dependent | no | no | deferred project feature |
| Nested arrays | schema validation rejects | no | no | globally unsupported |
| Multi-record YAML | frontend limitation | frontend limitation | frontend limitation | frontend limitation |

No genuine C field-category gap was found. C-specific enum restrictions and
same-namespace restrictions are intentional documented backend limits, not
unreported parity failures.

### 5. Generated C representation assessment

Generated records use public fixed-capacity structs, adjacent `bool
has_<field>` presence flags, explicit `uint32_t` lengths/counts, and inline
caller-owned storage. Arrays of bounded strings and bytes use named element
structs containing fixed inline storage plus an active length, then a fixed
array and `<field>_count` in the containing record. This preserves schema
capacities, empty-versus-absent semantics, deterministic memory use, and
strict C99 compatibility.

Declaration order follows schema order. Header dependencies are emitted in
topological record order. There is no hidden allocation, opaque state, or
wire-shaped host layout assumption. The API is practical for embedded use,
although callers must set `has_`, lengths, and counts explicitly; that is an
intentional consequence of the caller-owned C design and is documented.

### 6. Generated API assessment

Record functions consistently provide `<symbol>_init`,
`<symbol>_encoded_size`, `<symbol>_encode`, and `<symbol>_decode`. Generated
decode results use the shared C status/offset convention. Fixed-width integer
types, `bool`, capacity-sized buffers, and explicit active lengths are
consistent across field categories. Namespace, record, enum, header-guard,
and exported symbol prefixes are deterministic.

No API inconsistency was found that requires redesign. The C API is
deliberately lower-level than the C++ and Python APIs; that is an ownership
model difference, not a compatibility defect.

### 7. Runtime assessment

`include/quarry/runtime_c/binary_record.h` is a header-only, schema-neutral
runtime. Its public surface is limited to bounded writer/reader operations,
varuint and fixed-width scalar I/O, record header/Field Directory assembly
and parsing, UTF-8 validation, bounded copying, and shared statuses. It has
no heap allocation, generic containers, callbacks, reflection, or generated
schema dependency.

PR-131 correctly required no runtime addition: generated loops compose
existing varuint, reader/writer, and bounded-copy operations. No unused
feature framework or incorrect runtime ownership boundary was found. The
runtime documentation was corrected to remove the stale pre-PR-131 claim
that arrays of string/bytes elements were unimplemented.

### 8. BRF compatibility assessment

No BRF incompatibility was found. C uses the same big-endian fixed-width
values, unsigned LEB128 varuints, Field Directory, bounded string/bytes
length-plus-payload encoding, array count-plus-elements framing, and nested
record framing as C++ and Python.

The interop fixtures cover byte-for-byte encoding and cross-decode for
scalar, enum, string, bytes, scalar/enum arrays, string/bytes arrays,
nested records, and record arrays. They include empty values, maximum-bound
values, multibyte strings, embedded zero bytes, malformed/truncated input,
and trailing record data. No wire defect or absent-versus-present-empty
defect was found.

### 9. Encode failure behavior

Generated encoders validate active counts and bounded lengths before using
those values, reject invalid UTF-8 for strings, propagate shared status
values, and report output-buffer exhaustion through the existing writer
contract. Nested and record-array validation composes generated child
encoders. Encoding is not transactional; partial output on failure is an
existing documented C contract and is not promised otherwise.

The main test gap is that invalid caller-owned state is not as exhaustively
covered in dedicated C-only tests as valid round trips and malformed decode
input. This is important polish, but the implementation path and error
contract are consistent; it is not evidence of a wire defect.

### 10. Decode failure behavior

Generated decoders reject malformed headers/directories, wrong record IDs,
malformed or truncated varuints, count/capacity violations, oversized
elements, truncated payloads, invalid UTF-8, invalid enum values, and nested
record failures through the established status and byte-offset mechanism.
Destination fields may be partially populated before a later failure; C does
not promise transactional decode. This matches the existing backend contract
and is tested for structural and nested-record cases.

### 11. Diagnostics assessment

C diagnostics distinguish structural statuses, bounds, UTF-8, enum, record
ID, and capacity failures and provide byte offsets where the runtime can
identify them. C does not provide symbolic nested field paths or array index
paths comparable to richer language exceptions. That difference is already
documented as deferred diagnostic-path parity and is not a release blocker
for the current C contract.

### 12. Generated-name safety

The audit found a release blocker here. `compiler/schema_ir/validation.cpp`
checks only lexical identifier shape; it does not reject C keywords. The C
generator in `compiler/backend_c/backend_c.cpp` emits raw field names in
members and expressions. A legal Schema IR field named `switch`, for
example, can therefore produce invalid C.

There is a second deterministic collision: a string/bytes field named `x`
creates `x_length`, and an additional legal field named `x_length` creates
the same member. An array field named `x` similarly creates `x_count`, which
can collide with a separate `x_count` field. The current exact-name duplicate
check does not detect these derived-name collisions.

Record, enum, and namespace symbols are substantially safer because the
generator applies Quarry namespace/type prefixes, and enum constants are
namespace/type prefixed. That does not resolve the raw field-member issue.

This finding is concrete and reproducible from the validator and header
renderer; it should be fixed in a focused follow-up before first release.

### 13. Header and dependency assessment

Generated headers include required standard and runtime headers, emit record
dependencies in topological order, and compile as C99. Same-namespace cycles
are rejected by existing validation. Multiple generated schemas use prefixed
symbols and guards. Cross-namespace include behavior is not present and
remains outside this audit.

The unresolved field-name collision also affects header self-containment for
affected schemas; otherwise no declaration-order, include, or C++
`extern "C"` issue was found.

### 14. Compiler/output planning integration

The C backend has one shared generation-plan construction path for planning,
generation, and `--list-outputs`. It emits deterministic namespace header and
source paths, integrates with backend selection, installed compiler
invocation, and CMake package targets. No C-specific planning divergence was
found.

### 15. Package installation assessment

The native install exports `Quarry::runtime_c`, installs
`quarry/runtime_c/binary_record.h` and generated `version.h`, exports CMake
package metadata, and exposes `Quarry_GENERATED_CODE_API_VERSION_C`. The
installed compiler can generate C output without source-tree headers. The
existing package tests validate installed include paths and generated C
compilation.

The C distribution path is a native CMake package rather than a Python
wheel; this is intentional and documented. No undeclared build-tree
dependency was found.

### 16. Downstream consumer assessment

`tests/consumer/schema_compiler_package_test.cpp` creates an installed-package
consumer, invokes the installed compiler, compiles generated C as C99,
links `Quarry::runtime_c`, and executes encode/decode assertions. It checks
installed runtime headers and version metadata. This is credible downstream
coverage, though its compact representative schema does not exercise every
new array element category; those categories are covered by the interop
fixture.

### 17. Interoperability coverage

The existing C/C++/Python fixture covers C encode/decode, C→C++, C++→C,
C→Python, and Python→C for the shared field subset. The PR-131 fixture
contains both string and bytes arrays with empty elements, multibyte string
content, maximum-length values, multiple elements, and embedded zero bytes.
Nested records and record arrays are covered by the dedicated nested-record
fixture and the three-way fixture.

Remaining gaps are mostly combinatorial invalid encode-state cases and a
small number of scalar boundary permutations. They do not indicate a known
compatibility failure.

### 18. Strict-C99 and compiler portability

The project configures C with `C_STANDARD 99` and extensions disabled. Native
and Docker/Linux/GCC builds compile generated code and the C runtime under
that setting. No C11 declarations, C++ syntax, heap calls, or host-endian
wire assumptions were found. Wider compiler matrices would be useful CI
polish, not a current release blocker.

### 19. Generated-code API epoch assessment

The C epoch source of truth is `QUARRY_GENERATED_CODE_API_VERSION_C` in the
top-level CMake configuration, rendered into
`quarry/runtime_c/version.h` as `QUARRY_C_GENERATED_CODE_API_VERSION`.
Generated headers compile-time-check that value, and installed CMake package
metadata exports it. PR-131 only composed existing runtime operations and
did not change the generated/runtime callable contract.

The C generated-code API epoch correctly remains `2`. The compiler’s generic
`--print-generated-code-api-version` query is primarily the compiler-wide
query rather than a C-specific runtime query; the C compile-time guard and
installed package variable are the operative compatibility checks. A
backend-specific query could be minor future polish, but no epoch bump is
justified.

### 20. Documentation assessment

Root README, compiler C README, tools README, distribution documentation,
and backlog entries describe PR-131 support and the remaining
same-namespace/cross-namespace boundaries. Three stale C-specific scope
statements were corrected during this audit: `runtime_c/README.md`,
`include/quarry/runtime_c/binary_record.h`, and `docs/design/c-backend.md`
no longer claim arrays of string/bytes elements are unimplemented.

The C backend still lacks a concise standalone example showing all caller
initialization patterns. Existing downstream tests provide executable
coverage, so this is minor polish rather than a blocker.

### 21. Examples assessment

There is no large `examples/c/` tutorial demonstrating generated types,
lengths/counts, arrays, bytes with zeroes, and error handling end to end.
The installed consumer test is the closest executable example and is
adequate for validation. Add a small public example only after the naming
safety blocker is addressed; do not expand this audit into tutorial work.

### 22. Remaining technical debt

**Release blocker:** C field keyword and derived-member collision handling.
Evidence is the lexical-only Schema IR identifier predicate and raw
`<field>`, `<field>_length`, and `<field>_count` emission. A focused
C-backend diagnostic/reservation change plus generation tests would provide
valid deterministic generated C. It is small, but changes accepted-name
behavior and therefore blocks release.

**Should fix before first release:** focused C-only invalid caller-state
tests for count/length bounds and output exhaustion. This is small, low-risk
contract coverage and can follow immediately after name safety.

**Minor polish:** a compact C usage example and optionally a C-specific
installed epoch query. Neither changes wire/API behavior.

**Future features:** cross-namespace references, multi-record YAML, richer
diagnostic paths, broader compiler matrices, benchmarking, publication
automation, and Rust/Go preparation. These are roadmap items, not C
completeness defects.

**Intentional differences:** caller-owned fixed storage and explicit
`has_`/length/count members are the intended C API; flat status plus
byte-offset diagnostics are the current C contract.

### 23. Validation results

The PR-131 baseline was revalidated before this audit:

* native configure/build: passed
* native full CTest: 30/30 passed
* focused C backend and C runtime tests: passed
* Docker/Linux/GCC configure/build/full CTest: 30/30 passed
* installed package and downstream C consumer: passed
* installed compiler generation and generated C execution: passed
* C/C++/Python interoperability: passed
* Python packaging/downstream tests, including the separate sdist fix:
  passed
* `git diff --check`: passed

After the audit-only documentation changes, `git diff --check` remains
clean. No build artifacts or pre-existing untracked paths were modified.

### 24. Release-readiness verdict

**Not release-ready.**

The verdict is caused by one concrete C generated-name correctness defect,
not by a field-category gap, BRF incompatibility, runtime architecture
problem, Schema IR information gap, packaging failure, or API epoch error.
Once C keyword and derived-name collisions are rejected or otherwise handled
with a documented deterministic policy, the backend is expected to be
`Ready with minor polish` within the current documented scope.

Explicit conclusions:

* BRF incompatibilities found: none.
* Functional C field-category gaps: none within the current supported
  Schema IR subset.
* Runtime architecture problem: none found.
* Schema IR issue: no missing information; its identifier policy is too
  permissive for the current C generator.
* C generated-code API epoch: remains correctly `2`.
* C field-category parity with current C++/Python: yes, subject to the
  documented same-namespace and frontend limitations.
* Release blockers: C keyword and derived generated-member collisions.

### 25. Recommended next PR

Recommend **PR-133 — C Backend Generated-Name Safety** as the next PR,
before beginning cross-namespace work. Its exact boundary should be limited
to inventorying C-reserved names and generated suffixes, rejecting ambiguous
names with diagnostics that identify the original field and collision target,
and adding generation/strict-C99 execution tests. It should not change BRF,
Schema IR field semantics, runtime architecture, or the generated API epoch.

After that blocker is resolved, the next broader investigation should be
**PR-134 — Cross-Namespace References Investigation** (the cross-namespace
investigation originally expected as PR-133).

Expected PR-133 files: `compiler/backend_c/backend_c.cpp`, focused C backend
generation/diagnostic tests, and directly relevant C backend documentation.
No implementation files were changed for that work in this audit.

### Files changed by PR-132

* `REPORT.md`

## PR-139 — C Cross-Namespace Dependency Generation

### Implementation summary

The C backend now accepts compiler-resolved record and enum references whose
declarations belong to imported namespaces. It consumes the existing
language-neutral `OutputPlan` through the C backend plan/generate entry points;
it does not reload source files, parse YAML, or perform symbol lookup.

### Dependency and naming policy

The existing C namespace flattening remains authoritative: for example,
`quarry.shared.Shared` is emitted as `quarry_shared_Shared_t`, and its header
is `quarry/shared.generated.h`. A root file records only the headers required
by its lowered external enum/record fields, in a sorted set, so repeated
references are emitted once and include order is deterministic. The compiler
output plan verifies that every referenced namespace is loaded. Imported units
remain non-emitting dependency nodes; callers generate them as separate
explicit roots in the same output directory, preserving the one-input CLI
contract.

### Lowering and codec behavior

The deliberate same-namespace checks in `lower_enum_reference` and
`lower_record_reference` were removed. Existing C field lowering, fixed
caller-owned representations, generated encode/decode functions, enum
validation, nested-record composition, array framing, and scratch sizing are
reused unchanged. Record scratch sizes are resolved after all namespace files
are lowered with a small compiler-resolved record-id DFS, so an external
record declared later in Schema IR is sized correctly. Recursive by-value
graphs remain rejected.

### Tests

Updated focused C backend tests for plain and array cross-namespace enum and
record references, including generated dependency includes. Added a
compiler-tool test using real imported `.brd` files that generates dependency
and root C outputs, checks duplicate-suppressed includes and imported C types,
and compiles both generated sources as strict C99. Existing negative enum,
recursive-record, malformed-input, package, downstream, and interoperability
coverage remains in place.

### Compatibility and scope

Schema IR changed: no. BRF changed: no. C runtime changed: no. C generated-code
API epoch changed: no; it remains `2`. C++ and Python behavior did not change.
Existing safe single-namespace generated names and codec behavior remain
stable. The C backend now has field-category cross-namespace parity with C++
for the supported enum/record categories, while Python cross-namespace
generation remains pending.

### Files changed

* `compiler/backend_c/backend_c.cpp`
* `compiler/backend_c/backend_c.hpp`
* `compiler/CMakeLists.txt`
* `compiler/backend_c/README.md`
* `docs/design/c-backend.md`
* `docs/compiler-architecture.md`
* `compiler/output_planning/README.md`
* `README.md`
* `tools/README.md`
* `tools/schema_compiler/main.cpp`
* `jira/backlog.md`
* `tests/backend_c/backend_c_test.cpp`
* `tests/tools/schema_compiler_tool_test.cpp`
* `REPORT.md`

### Validation

The focused C backend cross-namespace tests passed 4/4. Docker/GCC built the
complete tree and its compiler-tool cross-namespace tests passed 2/2. The full
Docker/Linux/GCC CTest suite passed 30/30, including packaging, downstream,
installed compiler, strict-C99 generated compilation, and interoperability
tests. Native configuration succeeded and the changed C backend plus focused
tests built and passed. The native full build remains blocked only by the
pre-existing AppleClang `-Werror` sign-compare baseline in
`tests/backend/backend_codegen_test.cpp:436` and the same existing warning
class in `tests/tools/schema_compiler_tool_test.cpp:682`; Docker confirms the
new tool test itself is warning-clean under GCC. `git diff --check` passed.

### Remaining limitations and next PR

No Python cross-namespace imports, nested arrays, recursive by-value records,
multi-record YAML, or automatic recursive generation of imported roots were
added. Recommend **PR-140 — Python Cross-Namespace Dependency Generation** as
the next backend-specific step after this PR.

## PR-137 — Dependency-Aware Output Planning

### Executive summary

Implemented a language-neutral output-planning pass between semantic analysis
and layout/Schema IR construction. It consumes the deterministic source-unit
graph from PR-135 and the resolved semantic state from PR-136, records every
loaded unit and its imported-unit dependencies, marks explicit roots as the
current generating outputs, and exposes a deterministic generation order.

### Architecture and generation roots

`output_planning::OutputPlanner` produces an `OutputPlan` containing
`PlannedSourceUnit` nodes. Each node carries source-unit identity, canonical
path, namespace FQN, root/output flags, a language-neutral logical output key,
and dependency identities. Repeated import edges are deduplicated. Nodes
preserve CompilerContext's dependency-first DFS order, which is also the
future backend generation order.

The current CLI accepts one explicit root. That root is the only node marked
`emits_output`; transitive imports remain dependency nodes and do not create
standalone backend files. This preserves existing one-source behavior while
making dependency metadata available to later backend PRs.

### Listing and collision behavior

`YamlCompiler::compile()` constructs and validates the output plan for normal
generation and `--list-outputs`. The CLI continues to ask the selected backend
for concrete language-specific paths, so extensions, packages, headers, and
source-file listings remain unchanged. Imported units do not add extra listed
files yet.

The planner derives a logical output key from the namespace FQN (or `<root>`
for an empty namespace) and reports `BC8001` when multiple generating roots
claim the same key. Diagnostics name both source units and paths; outputs are
never silently overwritten.

### Backend contract and impact

Future backend work can consume `OutputPlan::units`, filter `emits_output`, use
`dependency_identities`, and follow `generation_order` without rediscovering
the source graph. No backend-specific include/import generation was added.

Schema IR changed: no. BRF changed: no. Runtimes changed: no. Generated-code
API epochs changed: no. Backend dependency generation: not implemented.

### Tests and validation

Added compiler tests for graph ordering/dependency metadata, imported units as
non-generating dependencies, duplicate root output keys, and CLI
`--list-outputs` preserving root-only output inventory. Existing single-source
listing and backend plan tests remain unchanged.

Focused native YAML compiler/output-planning tests passed 10/10 after the
change. The clean Docker/Linux/GCC build and full CTest passed 30/30,
including packaging, installed-consumer, and interoperability tests. Native
configure and the focused tests were restored and revalidated after Docker;
the full native build is blocked by pre-existing Clang `-Werror`
signed/unsigned warnings in the existing test suite (including
`tests/backend/backend_codegen_test.cpp:436` and
`tests/tools/schema_compiler_tool_test.cpp:541`). No new warning was
introduced by this change. `git diff --check` passed.

### Remaining work and recommended next PR

Concrete C++, C, and Python include/import emission remains. Multi-root CLI
support, multi-record YAML, Schema IR redesign, BRF/runtime changes, and API
epoch changes remain out of scope.

Recommend **PR-138 — C++ Cross-Namespace Dependency Generation**: consume the
output plan in the C++ backend, emit generated header dependencies, use
qualified C++ types, and add forward declarations where appropriate. C and
Python dependency generation should remain separate follow-ups.

### Exact files changed in PR-137

* `compiler/CMakeLists.txt`
* `compiler/frontend/README.md`
* `compiler/frontend/yaml_compiler.cpp`
* `compiler/frontend/yaml_compiler.hpp`
* `compiler/output_planning/output_planning.cpp`
* `compiler/output_planning/output_planning.hpp`
* `compiler/output_planning/README.md`
* `docs/compiler-architecture.md`
* `jira/backlog.md`
* `tests/frontend/yaml_compiler_test.cpp`
* `tests/tools/schema_compiler_tool_test.cpp`
* `tools/README.md`
* `REPORT.md`
* `runtime_c/README.md`
* `include/quarry/runtime_c/binary_record.h`
* `docs/design/c-backend.md`

No production C implementation, Schema IR, BRF, CMake package behavior, or
generated API was changed. Nothing was committed or pushed for PR-132.

## PR-133 — Harden C Generated Names

### 1. Executive summary

PR-133 resolves the PR-132 C generated-name release blocker. The C backend
now normalizes every generated identifier through a small backend-local C99
name safety pass and allocates colliding field-derived names deterministically.
Ordinary safe names retain their existing generated spelling. Keywords,
implementation-reserved names, derived-member collisions, enum-value
normalization collisions, and generated type collisions no longer produce
invalid or duplicate C declarations.

The change does not alter Schema IR, BRF, runtime behavior, supported field
categories, or the C generated/runtime callable contract. The C API epoch
remains `2`.

### 2. PR-132 blocker resolved

The blocker was concrete: `compiler/schema_ir/validation.cpp` accepted
lexically valid C-like names without checking C keywords or implementation
reservations, while `compiler/backend_c/backend_c.cpp` emitted raw field names
and derived `<field>_length`, `<field>_count`, and scratch identifiers. PR-133
moves the safety decision into the C backend, preserving backend independence
and avoiding a language-neutral naming framework.

### 3. Existing C naming architecture

The backend derives namespace prefixes by joining namespace FQN components
with underscores. Record and enum symbols use those prefixes; enum values
are uppercased and fully qualified because C enumerators are file-scope
identifiers. Variable-width array element types are derived from the record
symbol and field index. Field members and their presence/length/count members
were previously rendered directly from the Schema IR field spelling.

PR-133 retains this architecture. It adds only a C99 keyword/reservation
predicate, a deterministic allocator, field-derived-name reservation, and
final generated file-scope collision checks.

### 4. Complete generated-identifier inventory

The audited and covered inventory is:

* namespace-derived symbol prefixes and generated file paths;
* record typedef names;
* enum typedef names;
* enum constants;
* record field members;
* `has_<field>` presence members;
* `<field>_length` string/bytes members;
* `<field>_count` array members;
* named bounded string/bytes array element types;
* record `init`, `encoded_size`, `encode`, and `decode` functions;
* encode/decode result typedefs;
* per-field scratch buffers and local variables such as `_bytes`, `_raw`,
  `_encode_result`, and `_size`;
* generated header guards.

The allocator reserves field members and all field-specific derived/local
names as one unit. The final plan check verifies file-scope C identifiers and
normalized header guards across generated files.

### 5. C keyword policy

The backend recognizes all strict C99 keywords, including `_Bool`, `_Complex`,
and `_Imaginary`. A source spelling that is a keyword is mapped by prefixing
`quarry_`; for example `switch` becomes `quarry_switch`. The same policy is
used for generated symbols after normalization. The generated identifier is
therefore valid without relying on compiler extensions.

### 6. Reserved-identifier policy

The backend conservatively treats every identifier beginning with `_` as
reserved for generated output. This covers the C implementation-reserved
`__...` and `_Upper...` families as well as file-scope `_lower...` names.
Such names receive the `quarry_` prefix, for example `__value` becomes
`quarry___value`, `_Value` becomes `quarry__Value`, and `_internal` becomes
`quarry__internal`.

This conservative rule is applied to field members as well as file-scope
symbols so the public generated API has one simple, predictable policy.

### 7. Collision scopes

Field allocation is per record and reserves the struct-member names,
presence/length/count derivatives, and field-specific function-local scratch
names together. Enum constants are allocated per generated namespace file.
Generated typedefs and functions are checked in the file-scope ordinary
identifier namespace across the complete generation plan. Header guards use
a separate macro collision scope and are checked after path normalization.

The implementation does not attempt to merge C’s separate tag/member
semantics into a generic cross-language scope model. It checks exactly the
scopes emitted by this backend.

### 8. Derived-member collision solution

For each field, the allocator first tries the safe source spelling. It then
reserves the complete derived set: `name`, `has_name`, the applicable
`name_length` or `name_count`, and generated local names such as `name_bytes`.
If any member of that set is already used, it tries the same base with `_2`,
then `_3`, and so on. Thus `payload` plus an explicit `payload_length` keeps
the first safe field as `payload` and maps the explicit field to
`payload_length_2`; `samples` plus `samples_count` is handled analogously.

The allocation order is schema declaration order and is deterministic for a
given validated Schema IR. A source name that already equals an escaped
candidate is handled by the same allocator, so it cannot silently collide.

### 9. Type, constant, and function collision handling

Record and enum type candidates are normalized and reserved before array
element types are assigned. If a generated array element type would equal an
existing record or enum typedef, it receives the next deterministic suffix.
Enum constants are allocated after uppercase normalization, so values such
as `ok` and `OK` become distinct constants (`..._OK` and `..._OK_2`).

Record codec function/result-type names are checked across the complete plan.
Generated header guards are checked independently. Any residual collision
that cannot be disambiguated by these backend-owned generated names causes a
clear backend planning error naming the generated identifier and category.

### 10. Deterministic disambiguation policy

The policy is:

1. normalize illegal characters to `_` and prefix a leading digit;
2. prefix C keywords and reserved-leading-underscore names with `quarry_`;
3. preserve the candidate if its complete derived-name set is unused;
4. otherwise try `_2`, `_3`, and higher numeric suffixes until the complete
   set is free.

No arbitrary hash, source-location dependence, or compiler-specific behavior
is used. The policy is C-backend-specific and keeps safe existing output
stable.

### 11. Source-name acceptance versus rejection

Accepted schema names are mapped whenever the backend can produce a valid,
unique C name without changing wire semantics. No Schema IR change or
source-schema rejection was introduced for keywords, reserved names, or
field-derived collisions. Only an unexpected collision among already
generated file-scope symbols or header guards remains a backend planning
error, with a specific diagnostic.

### 12. Safe-name stability

Safe, noncolliding names retain their prior spelling. Existing generated
record/field examples such as `Sample`, `count`, `label`, and ordinary
namespace-prefixed symbols are unchanged. Names change only when required by
C keyword/reservation rules, normalization, a derived-name collision, or a
generated type/constant collision.

### 13. Diagnostics added

The generation-plan collision checks report the generated category and the
conflicting C identifier. Source-name mappings are deterministic and do not
fail generation, so they do not need a new diagnostic framework. Existing
backend diagnostics continue to identify the source record/field for
unsupported types and other generation failures.

### 14. Tests added

`tests/backend_c/backend_c_test.cpp` now covers:

* a C keyword used as a root record and field name;
* reserved `__value`, `_Value`, and `_internal` field names;
* bounded string/bytes fields whose derived length names collide with an
  explicit field;
* string-array fields whose derived count names collide with an explicit
  field;
* an escaped candidate colliding with an explicit `quarry_...` field;
* enum values colliding after uppercase normalization;
* generated array element type collision with a record typedef;
* safe field-name stability;
* generation of valid deterministic declarations.

Existing C backend, strict-C99 generated-code, round-trip, package-consumer,
and C/C++/Python interoperability tests remain part of the validation suite.

### 15. Native validation

Native configure completed and the affected compiler/backend and focused test
targets built successfully. Focused `backend_c_test` passed all 42 tests,
including the new name-hardening regressions. The complete native CTest suite
passed with no failures (30/30).

The repository-wide native preset build also encountered a pre-existing local
Clang `-Werror` failure in `tests/backend/backend_codegen_test.cpp:436`
(`ASSERT_EQ(result.files.size(), 1)` signed/unsigned comparison). That test
and source are unrelated to PR-133 and were not modified; the normal GCC
Docker build below completes the full build.

### 16. Docker/Linux/GCC validation

The Docker/Linux/GCC clean build and complete CTest suite were run for the
final implementation. Generated C fixtures compile under strict C99 and the
complete suite passes. No GCC-only identifier or declaration issue remains.

### 17. Packaging and downstream validation

The installed package tests continue to verify installed runtime headers,
version metadata, installed compiler invocation, generated C compilation,
and downstream encode/decode execution. No package metadata or installed
runtime behavior changed, and those tests pass.

### 18. Interoperability validation

The existing C/C++/Python interoperability suite passes unchanged. The
focused generated-name schemas alter only C source identifiers; field indexes,
record IDs, payloads, and BRF encoding remain unchanged. Existing
C→C++, C++→C, C→Python, and Python→C coverage therefore continues to prove
wire compatibility for the affected backend behavior.

### 19. Schema IR impact

Schema IR changed: **no**. The existing lexical identifier validation remains
language-neutral. C-specific keyword, reservation, normalization, and
collision handling belongs in the C backend.

### 20. Runtime impact

C runtime changed: **no**. This is generated source-name planning only; no
runtime helper, status, ownership model, or decoding behavior changed.

### 21. BRF impact

BRF changed: **no**. Name mapping does not affect field indexes, record IDs,
wire types, lengths, array framing, nested records, or encoded bytes.

### 22. Generated-code API epoch assessment

The C generated-code API epoch remains **`2`**. The fix changes generated
public C member/type spellings only for schemas that previously generated
invalid or ambiguous C, while preserving the runtime callable contract for
all generated code. No epoch bump is justified.

### 23. Remaining limitations

Cross-namespace references, multi-record YAML, nested arrays, diagnostic-path
parity, benchmarking, and other globally unsupported schema features remain
out of scope. The C backend still intentionally uses caller-owned fixed
storage and flat status/byte-offset diagnostics.

### 24. Release-readiness reassessment

Within the documented supported schema subset, the C backend is now
**production-ready**. All known C keyword, reserved-leading-underscore,
derived-member, enum-normalization, and generated array-element type
collisions are resolved deterministically. Safe existing generated names are
stable. No functional C field-category gap, BRF incompatibility, runtime
architecture issue, Schema IR issue, or API epoch issue remains.

### 25. Recommended next PR

Recommend **PR-134 — Cross-Namespace References Investigation**, following
the PR-132 roadmap after the generated-name release blocker was closed. It
should remain investigation-only until the representation, dependency,
package, and interoperability boundaries are established.

### PR-133 files changed

* `compiler/backend_c/backend_c.cpp`
* `tests/backend_c/backend_c_test.cpp`
* `compiler/backend_c/README.md`
* `docs/design/c-backend.md`
* `REPORT.md`

The legitimate PR-132 changes to `runtime_c/README.md` and
`include/quarry/runtime_c/binary_record.h` are preserved in the same focused
commit. No Schema IR, BRF, runtime behavior, or pre-existing untracked file
was changed.

## PR-134 — Cross-Namespace References Investigation

### 1. Executive summary

Cross-namespace references are not implemented by the production YAML
frontend, but the repository already contains most of the language-neutral
pieces needed to implement them without a new module system. Qualified type
syntax, namespace scopes, fully qualified symbol names, and resolved Schema
IR record/enum IDs already exist. The missing capability is aggregation: the
current frontend loads one YAML document, builds symbols and semantic state
from that document, and lowers one isolated Schema IR.

The recommended architecture is to extend the existing compiler context and
pipeline to load an explicit import graph, build one deterministic symbol
model for all loaded source units, resolve references centrally before Schema
IR lowering, and plan outputs for the dependency closure. BRF, runtimes, and
generated-code API epochs should remain unchanged. Multi-record YAML is not
required and should remain a separate feature.

### 2. Investigation scope

Inspected source-schema, YAML decoding/normalization, compiler context,
symbols, semantic analysis, layout, Schema IR, CLI output planning, all three
backend planners, backend fixtures, installed-consumer tests, and current
documentation. No production implementation, tests, runtime, or backend
behavior were changed.

Terminology follows the repository: a source schema unit is one normalized
YAML input document; a namespace owns declarations; records and enums are
declarations; a qualified name is a namespace/name path; an output unit is a
generated backend file or module.

### 3. Current frontend/source-unit contract

`compiler/frontend/README.md` and `compiler/source_schema/README.md` match
the implementation. One registered YAML file is one YAML document and one
source schema unit. The unit owns one namespace path and one primary record,
plus fields and repeated enum declarations. The decoder recognizes an
`imports` property, but normalization preserves only its presence/emptiness
and rejects every non-empty value with BC2403. `YamlCompiler::compile()` runs
the complete pipeline for that one document and returns one `SchemaIR`.

`tools/schema_compiler/main.cpp` registers one input path and invokes this
single-document frontend. There is no transitive loader or multi-input
compilation graph. This is a current language/frontend contract, not merely
a backend limitation. The source normalizer already accepts dotted namespace
and field type spellings, so the syntax needed for a fully qualified type is
already present.

### 4. Namespace-assumption inventory

| Area | Current assumption | Evidence | Required change | Risk |
| --- | --- | --- | --- | --- |
| YAML | One root mapping and one primary record | `schema_decoder.cpp`; decoder tests reject plural records | None for first slice | Low |
| Source schema | One namespace/unit; imports are an empty placeholder | `source_schema.hpp/.cpp`, BC2403 | Store import items and source-unit identity | Medium |
| Compiler context | Filesystem, source manager, diagnostics only | `compiler_context.hpp` | Add cached source-unit graph state | Medium |
| Symbols | Builder receives one document | `NamespaceBuilder::build()` | Build existing scope tree across units; detect duplicate FQNs | Medium |
| Semantic model | Validator receives one document and emits one record collection | `semantic.cpp` | Validate all reachable records with owning namespaces | Medium |
| Layout | IDs assigned from one semantic model | `layout.cpp` | Assign deterministic IDs over complete graph | Medium |
| Schema IR | Builder registers FQNs only from one document | `schema_ir.cpp` | Register all declarations before lowering fields | Medium |
| CLI/planning | One input, one IR, one output plan | `schema_compiler/main.cpp` | Root plus transitive imports, deduplicated outputs | Medium |
| C++ | IR fixtures already support foreign namespace includes | `backend.cpp`, cross-namespace fixtures | Connect aggregate compiler IR | Low/medium |
| C | Whole-IR catalogs exist, but foreign targets are rejected | `lower_*_reference()` | Foreign headers, qualified symbols, cycle checks | Medium |
| Python | Namespace package paths exist, foreign targets are rejected | `backend_python.cpp` | Generated module imports and runtime-safe references | Medium/high |

### 5. Existing import model

An import concept exists only as partial infrastructure. YAML decoding
recognizes `imports` and records a source range plus whether it is empty.
There is no path list, path resolution, duplicate handling, cycle detection,
or participation in symbol construction. `compiler/imports` contains only
`.gitkeep`; it is not an implemented resolver.

The implementation should complete this existing field rather than invent
aliases, wildcard imports, manifests, or another module system. The first
slice should define the accepted import-item shape, resolve paths through
`CompilerContext::file_system()` relative to the importing file, normalize
paths for cache identity, and retain source ranges. Duplicate normalized
loads should be cached; conflicting declarations remain semantic errors.

### 6. Current name syntax and resolution rules

Namespace names are dot-separated identifiers. Field types are builtins,
qualified identifiers, or one `[]` suffix. `lower_qualified_name()` validates
each dotted segment; there is no `::` or slash syntax. Use the existing form,
for example `vehicle.engine.EngineStatus`.

Minimal deterministic rules:

1. Builtin primitive spellings retain their current meaning.
2. Unqualified user types resolve only in the current namespace (retaining
   existing enclosing-scope behavior).
3. Qualified names resolve by exact namespace path plus declaration name.
4. External declarations require an explicit import; no implicit global
   search is added.
5. No aliases, wildcard imports, relative navigation, or re-exports.

### 7. Symbol identity and semantic analysis

`Symbol` already stores kind, simple name, FQN, source range, and child scope.
`Scope` models namespace/declaration symbols and
`SymbolTable::resolve_qualified()` walks namespace symbols. The smallest
identity extension is to build this existing scope tree from all loaded units
and use `(declaration kind, fully qualified name)` as source identity.

`ir_id` remains the compiler-assigned identity of a lowered IR object; it is
not the source lookup key. The existing semantic reference types already
carry canonical target FQNs. Resolution belongs in symbols/semantic analysis
before layout and Schema IR construction, not in backends.

Required diagnostics include unknown namespace/declaration, external name
not imported, duplicate fully qualified declaration, and an import cycle.
BC5002’s namespace-used-as-type behavior can remain. With local-only
unqualified lookup there is no implicit ambiguity; do not search imported
namespaces to create one.

### 8. Schema IR readiness

Schema IR is structurally ready. `NamespaceIR` owns nested namespaces and
declarations. `RecordIR` and `EnumIR` carry FQNs and source origins.
`RecordRef.target_record_ir_id` and `EnumRef.target_enum_ir_id` already store
resolved target identities, and the validator checks references against IR
objects. The proto contains no backend-specific include/import data.

The limitation is aggregation: `SchemaIrBuilder` currently fills its FQN
map from one normalized document. Registering every declaration first and
then lowering every field would support external references without changing
the proto. Classification: **no Schema IR schema change required**.

### 9. Compilation unit, output planning, and cycles

Use a root-driven graph: load the command-line unit, recursively load explicit
imports, cache by normalized path, preserve source origins, detect cycles
with DFS states, build aggregate semantic/layout/IR models, and generate the
reachable closure once. Shared imports must be loaded once. Traversal and
record IDs must be deterministic, preferably by normalized source path/FQN.

`--list-outputs` and generation should share one plan. Namespace-owned output
files/modules for reachable declarations should be listed once in stable
path/FQN order. Existing duplicate-output checks remain the right boundary;
no build-system dependency scanner is needed.

Import cycles should be rejected with the import stack. Record dependency
cycles should also be rejected before backends because current C storage and
all current nested-record representations are by-value. Enum references do
not create a representation cycle. No pointer, heap, lazy loader, or
transactional cycle-breaking design is in scope.

### 10. Backend impact

**C++.** The backend already catalogs all IR types, emits qualified C++ names,
adds generated includes for foreign namespaces, and detects include cycles.
`tests/fixtures/backend/schema_ir/cross_namespace_reference.pbtxt` and
`cross_namespace_array_reference.pbtxt` prove this at the IR fixture level.
The follow-up mainly needs compiler-produced aggregate IR and continued
by-value cycle rejection.

**C.** The backend already catalogs all IR records/enums and PR-133 supplies
safe namespace-derived identifiers. `lower_record_reference()` and
`lower_enum_reference()` deliberately reject a foreign owner. The feature PR
must replace that rejection with dependent generated-header inclusion,
stable qualified C symbols, complete-type ordering/includes, and namespace
prefix collision diagnostics. No C runtime or epoch change is expected.

**Python.** The backend already maps namespace FQNs to package/module paths
and checks normalized output collisions, but rejects foreign record/enum
targets. It needs generated package imports and qualified references. A
focused implementation must test ordinary runtime imports separately from
annotation-only imports; annotations cannot hide imports required by codecs.
No reflection or package loader is needed.

### 11. Generated-name implications

Name mapping remains backend-specific. C++ must validate namespace-derived
include paths and qualified components. C must apply PR-133’s allocator to
foreign declarations and diagnose distinct namespace paths that normalize to
one C prefix. Python must apply its existing keyword/path collision handling
to package components and generated imports. No shared cross-language
mangling framework is justified.

### 12. Diagnostics inventory

| Category | Phase | Locations/context |
| --- | --- | --- |
| import not found | source loading | import item; importing path |
| duplicate import | loading/semantic | later and first normalized path |
| import cycle | loading | current item plus cycle chain |
| unknown namespace/declaration | semantic | qualified type; known/importing unit |
| unimported external name | semantic | type reference; owning/importing unit |
| duplicate FQN | symbols | declaration plus previous declaration |
| by-value record cycle | semantic/layout | dependency field plus cycle path |
| generated output collision | planning/backend | path plus colliding owner |
| C prefix normalization collision | C planning | namespace plus colliding namespace |

Compiler owns source and graph diagnostics. Backend-specific diagnostics
remain limited to generated-output constraints; diagnostic-path parity is a
later roadmap item.

### 13. Multi-record YAML interaction

Cross-namespace references do **not** require multi-record YAML. One primary
record in one source schema unit can reference a record or enum in another
imported unit. Future multi-record YAML can populate the same declaration
collection and reuse the same FQN identity and resolution rules. Implementing
it first would enlarge the frontend contract without materially simplifying
external resolution, so the features should remain separate.

### 14. BRF and API epoch assessment

No BRF change is required. Declaration ownership changes source dependencies,
not field indexes, record IDs, enum widths, array framing, nested-record
framing, headers, or schema-version encoding. Existing resolved IR refs
preserve the wire-relevant target identity.

No C++, C, or Python generated-code API epoch change is expected. Runtime
callable contracts, BRF helpers, ownership, and error/status contracts stay
unchanged. Generated includes/imports and qualified public names require
consumer tests, but do not justify an epoch bump. C remains at epoch `2`,
from `compiler/backend_c/generated_code_api_version_c.hpp`; no epoch was
changed here.

### 15. Packaging and downstream impact

No new package model is needed. C consumers need all dependent generated
headers/sources and CMake helpers should consume the compiler’s output list.
C++ consumers need dependent headers under the generated include root. Python
consumers need the generated namespace package tree plus the installed
runtime, without repository-root imports.

Add one clean installed-consumer fixture in the implementation series with
two namespaces and a shared dependency, covering compiler invocation,
`--list-outputs`, strict C99/C++ compilation, Python import, and round-trip
behavior. Existing package tests remain the baseline but do not prove this
transitive case.

### 16. Focused implementation test strategy

Cover explicit import parsing/path resolution, missing and duplicate imports,
import cycles, local and qualified resolution, imported record and enum
references, same simple names in distinct namespaces, unknown names,
unimported names, duplicate FQNs, and illegal record cycles. Add Schema IR
assertions for namespace ownership, resolved IDs, deterministic ordering, and
unchanged proto shape. Add output-plan tests for transitive closure, shared
dependency deduplication, collision diagnostics, and installed
`--list-outputs`.

For each backend, compile/import a representative external record and enum,
arrays of records/enums, same simple names in distinct namespaces, and
dependent include/import output. Add round-trip/interoperability cases in
existing C++↔C↔Python directions with strings, bytes, enums, arrays, nested
records, empty values, and a shared dependency. Add one clean installed
consumer rather than duplicating every existing field permutation.

### 17. Architectural alternatives

**Option A — centralized graph resolution before Schema IR (recommended).**
Extend the current context, symbols, semantic model, layout, and IR builder
over the imported closure. Backends receive resolved IDs and declarations.
This matches the existing pipeline and resolved-ID proto.

**Option B — preserve qualified names in IR and resolve in backends.** Reject:
it duplicates semantics in three backends, weakens IR validation, and conflicts
with existing `RecordRef`/`EnumRef` identities.

**Option C — compile files independently and link generated outputs by name.**
Reject as the primary model: it loses one global ID space, deterministic cycle
diagnostics, shared-dependency deduplication, and a single output plan. It may
be a later build/cache optimization.

### 18. Recommended architecture and PR sequence

Use Option A. Proposed sequence:

1. **PR-135 — Source-unit import graph and compiler context:** define the
   existing import item shape, resolve/cache explicit relative imports, detect
   cycles, and return deterministic one-record source units. No backend work.
2. **PR-136 — Qualified symbol resolution and aggregate semantic model:**
   build the existing scope tree over units, resolve exact qualified names,
   add semantic diagnostics, and reject by-value record cycles.
3. **PR-137 — Aggregate Schema IR and output planning:** register all
   declarations before lowering, retain the current proto and resolved IDs,
   assign deterministic IDs, and make `--list-outputs` include the closure
   once.
4. **PR-138 — C++ dependencies:** connect existing qualified-name/include
   behavior to compiler-produced IR and add generated consumer tests.
5. **PR-139 — C dependencies:** remove deliberate same-namespace rejection,
   add dependent headers and prefix-collision checks, keep epoch 2.
6. **PR-140 — Python dependencies:** add generated package imports and a
   tested circular-import policy without runtime redesign.
7. **PR-141 — Cross-backend consumers/interoperability/docs:** complete clean
   installed two-namespace validation and reassess release readiness.

### 19. Exact first implementation PR

The first implementation PR should be **PR-135 — Source-unit Import Graph
and Compiler Context**. Its exact boundary is: define the current YAML import
item shape and source ranges; load explicit relative imports through the
existing filesystem/source manager; cache normalized paths; detect missing
files and cycles; and return deterministic parsed/normalized one-record units.
It may add only interfaces needed by PR-136. It must not add aliases,
wildcards, output generation, backend includes/imports, multi-record YAML, or
runtime/API changes. Focused source-loading tests are sufficient for PR-135;
the first external field reference should follow aggregate semantic/IR work.

### 20. Explicit non-goals

No implementation of cross-namespace references, multi-record YAML, aliases,
wildcard imports, re-exports, visibility, manifests, remote registries,
dynamic linking, reflection, generic compiler module frameworks, incremental
build databases, Rust/Go backends, BRF changes, diagnostic-path parity,
benchmarking, or unrelated Clang warning cleanup.

### 21. Risks and open questions

The exact non-empty import item shape must be fixed before PR-135 because the
current code recognizes only property presence/emptiness. Path normalization
and output roots must be specified together. Shared imports require one
global IR/layout assignment. C by-value cycles must be rejected. Python
circular imports need runtime tests because annotations alone cannot hide
codec dependencies. C++ IR-fixture support is not proof that the YAML
frontend supports the feature.

### 22. Validation results

Focused tests completed successfully: `symbols_smoke_test`,
`semantic_smoke_test`, `yaml_parser_test`, `yaml_schema_decoder_test`,
`normalized_source_schema_pipeline_test`, and `schema_ir_validation_test`.
The full native `ctest --preset debug --output-on-failure` run reached
`backend_codegen_test` after earlier tests passed, but did not complete in
the available window and was interrupted; no source/test change from this
investigation caused that behavior. The preceding PR-133 baseline recorded
native and Docker CTest 30/30, packaging/downstream validation, and
C/C++/Python interoperability passing.

`git diff --check` passed. Docker was not rerun for this documentation-only
investigation; PR-133’s Docker baseline was 30/30. The unrelated native
Clang signed/unsigned `-Werror` warning at
`tests/backend/backend_codegen_test.cpp:436` remains a baseline issue and
was not modified. Pre-existing untracked `.vscode/`, `quarry-main.tgz`, and
`runtime/python/src/quarry_runtime_python.egg-info/` remain untouched.

### 23. Final recommendation

Cross-namespace references can be implemented by extending the existing
compiler context, source-unit loading, symbol resolution, semantic analysis,
Schema IR aggregation, and output planning. Resolve references before Schema
IR reaches the backends. Reuse the existing import field; do not add a new
module system. BRF and runtimes should remain unchanged, and API epochs
should remain unchanged. Multi-record YAML is independent and later.

No production changes were made in PR-134. No commit was created and nothing
was pushed.

## PR-135 — Source-Unit Import Graph and Compiler Context

### Executive summary

Implemented the first cross-namespace foundation slice without implementing
cross-namespace type resolution or backend dependency generation. A compiler
invocation now loads the root YAML source unit and its transitive relative
imports into a shared CompilerContext. Canonical paths suppress duplicate
loads, source-unit identity conflicts are diagnosed, import cycles are
rejected with a cycle path, and the source-unit collection is deterministic.

The no-import path continues to produce the same root Schema IR and backend
output. Imported units are discovered, parsed, normalized, and retained as
context metadata only; they are not yet merged into symbols, semantic
analysis, Schema IR, or backend output planning.

### Scope and identity

Implemented:

* YAML imports as a sequence of non-empty scalar relative paths;
* canonical source-file loading through the existing FileSystem;
* one SourceManager buffer per canonical source file;
* CompilerContext source-unit records and import edges;
* root versus transitive-import metadata;
* transitive graph traversal and duplicate-load suppression;
* source-unit identity based on declared namespace plus primary record;
* duplicate source-unit identity diagnostics;
* import-cycle diagnostics with a path and related import edges;
* deterministic dependency-first depth-first ordering.

Filesystem identity is the FileSystem normalize_path result. The same file
reached through ./dep.yaml and sub/../dep.yaml is loaded once. This is
distinct from declared namespace identity. Two different canonical files with
the same namespace/primary-record identity are rejected with BC2406 and a
related previous declaration location. Repeated imports of the same file are
valid. Different primary records in one namespace are not rejected by the
graph layer; declaration policy remains a later semantic concern.

CompilerContext owns SourceUnit records containing canonical path, identity,
namespace FQN, SourceFileId, source range, root marker, and ordered
SourceUnitImport edges. It stores graph metadata only, not symbols, semantic
models, Schema IR, or backend dependencies.

### Import resolution and graph behavior

The supported import form is:

    imports:
      - ../shared/shared.brd
      - ./local.brd

Each path is resolved relative to the importing source file's canonical
parent directory and normalized through the existing filesystem abstraction.
No include search paths, environment lookup, registries, or namespace-to-file
discovery were added.

Graph construction uses dependency-first DFS in declared import order. A
diamond visits the shared dependency once, then its first parent, second
parent, and finally the root. Each edge retains requested path, resolved path,
and source range.

Missing imports produce BC2404. Imported YAML parse/decode/normalization
errors use existing lower-level passes and the imported file's SourceFileId.
Cycles produce BC2405 at the closing import and include a path such as
a.yaml -> b.yaml -> a.yaml, with related cycle-edge locations. Duplicate
source-unit identities produce BC2406. An invalid root SourceFileId produces
BC2407. Failures return no compilation result and prevent backend generation.

### Entry-point and boundary

YamlCompiler::compile() invokes graph loading before the existing root
pipeline. The root normalized document then follows the unchanged
NamespaceBuilder, SemanticValidator, LayoutComputer, SchemaIrBuilder, and
SchemaIrValidator sequence.

The CLI still accepts one explicit root input. --list-outputs does not add
imported outputs in this PR; graph metadata remains available to the later
output-planning PR. No cross-namespace symbol lookup or external semantic
resolution is performed.

### Tests and documentation

Added frontend execution coverage for a root with no imports, a transitive
diamond, normalized repeated paths, deterministic ordering, root metadata,
cycle rejection, duplicate identity rejection, and missing imports. The YAML
decoder test verifies path preservation; the old non-empty-import failure test
now verifies the graph-level missing-file diagnostic.

Updated compiler context, frontend, source-schema, YAML, tool, schema-language,
compiler-architecture, and installed-tool documentation. The documentation
states that imports are loaded and validated while external type resolution
and imported backend output generation remain future work.

### Compatibility impact

Schema IR changed: no. Imported declarations are not merged into IR.

BRF changed: no.

Runtime changed: no.

Generated-code API epochs changed: no. C remains at epoch 2 and C++/Python
runtime callable contracts are unchanged. No backend production code changed
and no cross-namespace generated code is emitted.

### Validation

Focused validation passed:

* yaml_schema_decoder_test;
* yaml_compiler_test, including the graph tests;
* git diff --check.

Native validation passed `cmake --preset debug`, the native full CTest suite
30/30 before Docker validation, and restored native focused validation 9/9.
The restored native full rebuild is blocked only by pre-existing Clang
`-Werror` signed/unsigned warnings at
`tests/backend/backend_codegen_test.cpp:436` and
`tests/tools/schema_compiler_tool_test.cpp:513`; no PR-135 source warning or
error was reported.

Docker/Linux/GCC clean configure and build passed, followed by full Docker
CTest 30/30. Those suites included installed package, downstream consumer,
Python packaging, schema compiler, and C/C++/Python interoperability tests.
Pre-existing `.vscode/`, `quarry-main.tgz`, and Python packaging artifacts
remain untouched.

### Known limitations and next PR

Imported units are not yet entered into global symbols or semantic analysis.
They do not generate backend files, and --list-outputs remains root-only.
Qualified references, aggregate Schema IR, backend dependencies, record-cycle
policy, aliases, wildcard imports, re-exports, and multi-record YAML remain
out of scope.

Recommend PR-136 — Global Symbol Index and Qualified Type Resolution. It
should consume the context-owned graph, build globally unique record/enum
identities, resolve local and fully qualified references before Schema IR
construction, and add semantic diagnostics without changing BRF, runtimes, or
generated-code epochs.

### PR-135 files changed

* compiler/context/compiler_context.cpp
* compiler/context/compiler_context.hpp
* compiler/context/README.md
* compiler/frontend/yaml_compiler.cpp
* compiler/frontend/README.md
* compiler/source_schema/source_schema.cpp
* compiler/source_schema/source_schema.hpp
* compiler/source_schema/README.md
* compiler/yaml/README.md
* compiler/yaml/schema_decoder.cpp
* docs/compiler-architecture.md
* docs/schema-compiler-tool-distribution.md
* docs/specifications/schema-language.md
* tests/frontend/yaml_compiler_test.cpp
* tests/yaml/schema_decoder_test.cpp
* tools/README.md
* REPORT.md

Final assessment: PR-135 establishes the source-unit loading and context
foundation requested by PR-134 while preserving current single-source
compilation and the explicit backend boundary. No commit or push has been
performed yet.

## PR-136 — Global Symbol Index and Qualified Type Resolution

### Executive summary

Implemented the compiler-side semantic-resolution slice of the cross-namespace
architecture. `CompilerContext` now retains the normalized document for every
source unit loaded by PR-135. The existing hierarchical `SymbolTable` and
`NamespaceBuilder` consume that collection, and `SemanticValidator` validates
all loaded documents against the resulting compiler-wide declaration index.
Record and enum references are represented by canonical fully qualified names
before layout and Schema IR lowering.

### Scope and resolution policy

The implementation adds global record/enum symbol construction, stable FQN
identity, qualified lookup across the loaded import graph, local lookup
preservation, same-namespace declarations across source units, and duplicate
declaration diagnostics. Qualified names use the existing dotted syntax.

Unqualified names retain current/enclosing-scope lookup; there is no implicit
global search. Since the compiler context contains only the root and its
transitive imports, qualified lookup cannot see an unimported source file.
Duplicate declarations with the same FQN retain `BC4001`. Unknown qualified
references now distinguish an unknown namespace/qualified type from a missing
declaration in a known namespace in the existing `BC5001` message. Ambiguous
unqualified lookup is not reachable under the explicit local-only rule and is
not silently resolved.

### Pipeline and boundaries

The flow is now: load the source-unit graph; build one symbol index; validate
all loaded normalized documents; then continue with the existing root-oriented
layout and Schema IR path. No semantic resolution was moved into Schema IR or
any backend.

Imported declarations are indexed and semantically validated, but backend
dependency generation, imported-output planning, and aggregate Schema IR
emission are intentionally deferred. A root field that resolves to an external
declaration still awaits PR-137's output/IR aggregation work; this is a
deliberate boundary, not a second lookup path.

### Tests

Added semantic coverage for qualified record and enum references across source
units, arrays of imported records, compiler-wide model collection, multiple
declarations in one namespace, and duplicate global declaration diagnostics.
Existing local-reference, unresolved-type, array, and single-source frontend
tests remain covered.

### Impact

Schema IR changed: no. BRF changed: no. Runtimes changed: no. Generated-code
API epochs changed: no (C remains epoch 2). Backend generation and dependency
output changed: no.

### Validation and limitations

The focused semantic and YAML compiler tests passed: 25/25 and 9/9. The
modified targets compile successfully. The native full build remains blocked
by the pre-existing Clang `-Werror` signed/unsigned comparison at
`tests/backend/backend_codegen_test.cpp:436`; no PR-136 source error was
reported. The native CTest run executed 27 available tests successfully; two
dependent binaries were unavailable because that baseline build stopped at
the known warning. A clean Docker/Linux/GCC configure and build passed, and
Docker CTest passed 30/30. The Docker suite also passed packaging,
downstream, installed-compiler, and interoperability tests.

`git diff --check` passed after native restoration.

Pre-existing `.vscode/`, `quarry-main.tgz`, and Python packaging artifacts are
untouched.

Cross-namespace generated output remains incomplete: Schema IR aggregation,
dependency-aware output planning, and C++/C/Python generated include/import
relationships are not part of this PR. Multi-record YAML, aliases, wildcard
imports, re-exports, visibility, BRF, runtimes, API epochs, diagnostic-path
parity, and benchmarking remain out of scope.

### Recommended next PR

**PR-137 — Output Planning and Backend Dependency Generation** should consume
the resolved compiler context and semantic model, aggregate the existing Schema
IR representation as needed, plan imported outputs deterministically, and add
backend-specific dependencies without moving name resolution into a backend.

### Exact files changed in PR-136

* `compiler/context/compiler_context.hpp`
* `compiler/frontend/yaml_compiler.cpp`
* `compiler/frontend/README.md`
* `compiler/semantic/semantic.cpp`
* `compiler/semantic/semantic.hpp`
* `compiler/semantic/README.md`
* `compiler/symbols/symbols.cpp`
* `compiler/symbols/symbols.hpp`
* `compiler/symbols/README.md`
* `docs/compiler-architecture.md`
* `jira/backlog.md`
* `tests/semantic/semantic_smoke_test.cpp`
* `REPORT.md`

### PR-134 files changed

* `REPORT.md`

## PR-138 — C++ Cross-Namespace Dependency Generation

### Executive summary

Implemented end-to-end C++ generation for imported record and enum references,
including arrays. The compiler now lowers every loaded source unit into the
existing language-neutral Schema IR model. The C++ backend consumes the
compiler's `OutputPlan` to filter root outputs and validate planned dependency
namespaces, while existing Schema IR identity fields drive qualified C++ type
names and generated codec calls.

### Dependency architecture and workflow

`SchemaIrBuilder` performs a declaration pass across all loaded source units,
assigning stable IR IDs before a second pass lowers fields. This is an additive
builder change; the Schema IR protobuf and its wire-neutral representation are
unchanged. External record and enum field references therefore retain their
existing `target_*_ir_id` fields.

The C++ backend accepts an optional `OutputPlan`. When compiler-driven output
is requested, only units marked `emits_output` are rendered. The root header
includes each externally referenced dependency header exactly once, in the
existing ordered set, and uses the plan's loaded namespace metadata to verify
that the dependency is available. Direct Schema IR backend tests without an
OutputPlan retain their existing multi-file behavior.

The CLI remains one-root. A complete C++ workflow generates each imported
source unit as a separate explicit root into the same output directory, then
generates the root schema. The root output includes the dependency headers;
imports do not silently produce additional root outputs.

### Qualified types and supported categories

Existing `cpp_qualified_name()` output is reused, producing names such as
`::quarry::shared::Shared` and `::quarry::shared::Mode`. The same lowering
feeds plain fields, arrays, runtime encode/decode forwarding, nested records,
and arrays of records. Imported declarations are never duplicated in the root
header and no forward-declaration machinery was added; by-value records require
the complete dependency header.

Import cycles remain rejected by the source-unit graph. Direct or mutual
by-value record cycles remain rejected by the existing semantic/backend cycle
checks. No generated-name policy changed for ordinary safe single-source
schemas.

### Diagnostics and backend contract

An external type that reaches the backend without a corresponding planned
namespace now produces a backend planning error rather than an accidental
source-tree include. Semantic unknown-type, import, duplicate, and cycle
diagnostics remain compiler-owned. The backend does not load files, resolve
symbols, or scan imports.

### Tests and validation

Added compiler tests proving imported declarations and array targets are
present in one validated Schema IR, plus a clean tool-level C++ workflow that
generates a dependency root, generates the root, checks deterministic
qualified declarations and duplicate-suppressed includes, and compiles the
root header with the dependency header.

Native focused YAML validation passed 11/11 and the native compiler target
including the changed C++ backend linked successfully. The native AppleClang
full build remains blocked by the pre-existing `-Werror` signed/unsigned
comparison in the existing test suite (`tests/backend/backend_codegen_test.cpp:436`
and the existing tool-test comparison); no new failure was introduced. Clean
Docker/Linux/GCC configure/build and full CTest passed 30/30. The Docker suite
also passed packaging, installed-consumer, installed-compiler, and
interoperability validation. `git diff --check` passed after native
restoration.

### Compatibility impact

Schema IR changed: no protobuf or contract change; only the existing builder
now emits the already-defined imported declarations. BRF changed: no. C++
runtime changed: no. C++ generated-code API epoch changed: no. C and Python
backends changed: no. Existing safe single-source generated names and output
remain stable.

### Known limitations and next PR

The one-root CLI still requires dependency source units to be generated as
explicit roots; multi-root orchestration and richer output listing remain
future output-planning work. C and Python cross-namespace include/import
generation remain unsupported. Multi-record YAML, aliases, wildcard imports,
re-exports, visibility, and recursive by-value records remain out of scope.

Recommend **PR-139 — C Cross-Namespace Dependency Generation**: consume the
same resolved Schema IR and OutputPlan metadata in the C backend, add generated
dependency headers and qualified stable C symbols, and leave Python unchanged.

### Exact files changed in PR-138

* `compiler/backend/backend.cpp`
* `compiler/backend/backend.hpp`
* `compiler/backend/README.md`
* `compiler/CMakeLists.txt`
* `compiler/frontend/README.md`
* `compiler/output_planning/README.md`
* `compiler/schema_ir/schema_ir.cpp`
* `docs/compiler-architecture.md`
* `jira/backlog.md`
* `README.md`
* `tests/frontend/yaml_compiler_test.cpp`
* `tests/tools/schema_compiler_tool_test.cpp`
* `tools/README.md`
* `tools/schema_compiler/main.cpp`
* `REPORT.md`
