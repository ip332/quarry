# Python Backend

## Implementation Status

PR-118 implemented the first Python backend milestone: an **architecture
skeleton**, not serialization. `compiler/backend_python/` is a real,
independent CMake target that consumes Schema IR and produces real,
importable Python packages through the same `Backend`/`CodegenOptions`/
`plan()`/`generate()` shape and CLI (`--language python`) integration the C
and C++ backends already use. It supports **zero-field records only**; a
record declaring one or more fields fails generation with a diagnostic. No
BRF encoding/decoding, no scalar field support, no varuint, and no
interoperability are implemented -- every generated method body
unconditionally `raise`s `NotImplementedError`. This document describes the
architecture PR-118 established and the scope it deliberately left for
later PRs.

This document supersedes, for implementation purposes, the investigation
notes captured in this repository's PR-117 (native Python backend
architecture) and PR-118A (encode/decode API boundary) working reports;
where this document and those investigations agree, this document is
authoritative going forward.

---

## Purpose

Prove that Quarry's backend architecture generalizes to a third,
independent language backend without any compiler-pipeline or Schema IR
change, and without depending on either existing backend
(`compiler/backend/`, the C++ backend; `compiler/backend_c/`, the C
backend). `compiler/backend_python/` depends only on
`quarry_compiler_schema_ir` and `quarry_project_options`, exactly like
`backend_c` -- confirming `docs/backend-api.md`'s guarantee that "new
backends may be added without modifying the compiler pipeline" for a third,
structurally different language, not just a second one.

---

## Architecture

### Backend shape

`compiler/backend_python/backend_python.hpp` mirrors `backend_c.hpp`'s
shape, adapted for Python's simpler, single-file-per-namespace model
(closer to the C++ backend's single-`relative_output_path`
`PlannedGeneratedFile` than to C's header/source pair, since Python has
neither a header/source split nor an `#include` mechanism):

```cpp
struct CodegenOptions {
    std::string output_directory = "generated";
    std::string root_module_stem = "schema";
};

struct GeneratedFile { std::string path; std::string content; };
struct PlannedGeneratedFile { std::string relative_output_path; };
struct GenerationPlan { std::vector<PlannedGeneratedFile> files; };
struct PlanResult { bool success = true; std::string error_message; GenerationPlan plan; };
struct CodegenResult { bool success = true; std::string error_message; std::vector<GeneratedFile> files; };

class Backend {
public:
    PlanResult plan(const schema_ir::SchemaIrModel&, const CodegenOptions&) const;
    CodegenResult generate(const schema_ir::SchemaIrModel&, const CodegenOptions&) const;
};
```

`Backend::plan()` and `Backend::generate()` share one internal
`build_generation_plan()`, the same discipline `compiler/backend/backend.cpp`
and `compiler/backend_c/backend_c.cpp` already document, so the two modes
cannot diverge.

### Namespace mapping -- the one structurally different piece

C's file-planning model is deliberately *flat*: a namespace FQN's segments
are joined with `/` to form a single file's relative path (e.g.
`quarry.telemetry` -> `quarry/telemetry.generated.h`), where the last
segment doubles as the file name. This works because C has no real
directory-based package/import mechanism to satisfy -- `/` in the path is
purely cosmetic grouping.

Python's import system has no such luxury: a dotted namespace can only
become a real, importable Python package if every segment is backed by an
actual directory containing its own `__init__.py`. So the Python backend's
namespace mapping is genuinely different from both existing backends, not
an incidental implementation detail:

* Namespace FQN `acme.telemetry` -> directories `acme/__init__.py`,
  `acme/telemetry/__init__.py`, **plus** that namespace's own generated
  module *one level further in*: `acme/telemetry/schema.py` (using
  `CodegenOptions::root_module_stem`, default `"schema"`) -- an extra
  segment beyond the namespace path itself, so a namespace's own records
  never collide with a child namespace's own package directory.
* The root (synthetic, unnamed) namespace's module lives directly under
  the output directory with no wrapping package at all: `schema.py`,
  mirroring the root-namespace-has-no-directory-prefix precedent both
  existing backends already establish for their own root file stems.
* Sibling/cousin namespaces sharing a common ancestor (e.g.
  `acme.telemetry` and `acme.control`) legitimately contribute the *same*
  ancestor `__init__.py` path more than once; these are collected into a
  `std::set<std::string>` so expected overlap is silently deduplicated.
  Two different namespaces producing the same *module* path would instead
  be a genuine error (defensively checked, mirroring `backend_c`'s own
  duplicate-output-path guard), though this cannot currently happen since
  namespace FQNs are unique and the module path is a pure function of the
  FQN.
* Record class names use the bare record name (e.g. `Sample`), not
  namespace-prefixed like C's `quarry_telemetry_Sample_t` -- Python's own
  package/module structure already disambiguates.

### Scope: records only, this PR

Enums are not rendered at all in this skeleton. `namespace_emits_file()`
for Python is `ns.records_size() > 0`, ignoring any enums the namespace may
also declare -- an enum-only namespace emits nothing. This is a narrow,
intentional scope limitation (see "Known limitations" below), not an
oversight: the per-record template PR-118 specifies is the entire
generated-code surface this PR defines, and enums aren't mentioned in its
scope at all.

### Zero-field-only records

Mirroring PR-107's identical rule for the original C backend skeleton, any
record declaring one or more fields fails generation with a diagnostic
naming the record and its field count:

```
backend_python: record '<fqn>' declares <N> field(s); the Python backend
skeleton does not yet support any field types -- only zero-field records
can be generated (see docs/design/python-backend.md)
```

A struct/class that silently dropped unsupported fields would be partial,
misleading output; this PR follows the project's existing "do not emit
partial code" convention instead.

---

## Generated API

For each zero-field record, the backend emits exactly this template
(verbatim, per PR-118's specification -- a single blank line separates the
import from the class and separates each method within the class; two
blank lines, PEP8's top-level-definition convention, separate the class
from the first module-level helper and separate each helper from the
next):

```python
from dataclasses import dataclass

@dataclass
class Sample:

    def encode(self):
        return _encode_sample(self)

    @classmethod
    def decode(cls, data):
        return _decode_sample(data)

    def encoded_size(self):
        return _encoded_size_sample(self)


def _encode_sample(value):
    raise NotImplementedError


def _decode_sample(data):
    raise NotImplementedError


def _encoded_size_sample(value):
    raise NotImplementedError
```

The public API is the three dataclass methods; the leading-underscore
module-level functions are implementation details, per PR-118A's
methods-with-internal-free-function-delegation recommendation, now wired
end to end: each method's single line of implementation is a delegating
call to its corresponding helper (`return _encode_sample(self)`, etc.).
Only the helpers themselves `raise NotImplementedError` -- there is
deliberately one place, not two independent copies, where "not implemented
yet" is expressed. A later PR will replace each helper's body with real
codec logic; the methods above will not need to change at all when that
happens.

Helper function names use a PascalCase -> snake_case conversion (`Sample`
-> `sample`, `SensorReading` -> `sensor_reading`): an underscore is
inserted before each uppercase letter immediately following a lowercase
letter or digit, then the whole string is lowercased. This is a
deliberately simple heuristic -- see "Known limitations" for its
acronym-handling gap.

A namespace with multiple records repeats this block once per record,
separated by the same single blank line used within the template (a
judgment call for a case the literal single-record example doesn't show).

---

## Runtime boundary and compatibility epoch

`runtime/python/` is a small, independently pip-installable package
(`quarry-runtime-python` on PyPI; `pyproject.toml` + `src/quarry/runtime/
python/`), importable as `quarry.runtime.python`. This skeleton exposes
exactly one symbol:

```python
QUARRY_GENERATED_CODE_API_VERSION_PYTHON = 1
```

No serialization helpers exist yet -- unlike the C++ and C runtimes
(`quarry::runtime`, `quarry_runtime_c`), which already implement varuint,
header/Field-Directory, and scalar codec mechanics, the Python runtime's
real codec surface (varuint I/O, whole-record assembly/parsing, a
`struct`-based scalar pack/unpack helper -- see PR-117 §6/§7) is deferred
until a PR actually needs it.

Every generated module begins with an import-time compatibility check:

```python
from quarry.runtime.python import QUARRY_GENERATED_CODE_API_VERSION_PYTHON

if QUARRY_GENERATED_CODE_API_VERSION_PYTHON != 1:
    raise ImportError(
        "Generated Quarry Python code is incompatible with the installed "
        "Quarry Python runtime. Regenerate the code using a compatible "
        "quarry-schema-compiler release."
    )
```

This mirrors the *philosophy* of the C/C++ generated-code epoch guards
(fail loudly and specifically on a stale runtime pairing) without copying
their *mechanism* -- Python has no preprocessor or `static_assert`, so the
check runs at import time and raises `ImportError` instead of failing to
compile. The expected value (`1`) is a `constexpr` embedded in
`compiler/backend_python/backend_python.cpp`
(`kGeneratedCodeApiVersionPython`), independent from
`QUARRY_GENERATED_CODE_API_VERSION` (C++) and
`QUARRY_GENERATED_CODE_API_VERSION_C` (C) -- the Python generator/runtime
contract can change on its own schedule.

---

## Known limitations

* **Not CMake-driven, unlike C/C++.** `kGeneratedCodeApiVersionPython` in
  `backend_python.cpp` and `QUARRY_GENERATED_CODE_API_VERSION_PYTHON` in
  `runtime/python/src/quarry/runtime/python/__init__.py` are two
  independently-maintained literals that must be bumped together by hand.
  The C/C++ epochs avoid this by `configure_file()`-generating both the
  backend's and the runtime's copy from one shared CMake scalar; Python's
  runtime package deliberately sits outside the CMake graph entirely (see
  `docs/distribution-model.md`'s "Python Runtime Packaging" section), so
  the same trick doesn't directly apply. A follow-up PR could introduce a
  small shared source of truth (e.g. a generated `_version.py` the
  packaging step copies in) if manual drift becomes a real problem.
* **No enum support.** Enum-owning namespaces emit nothing in this
  skeleton, even if they also own records (only the records are rejected
  for having fields; the enum itself is silently not rendered). Deferred
  to a later PR alongside general field-type support.
* **No Python-keyword escaping.** A record or field named `class`,
  `import`, etc. would currently produce invalid Python. Not reachable
  yet since no field rendering exists, but must be addressed before field
  support lands.
  * **Simple snake_case heuristic.** `HTTPResponse` becomes
  `h_t_t_p_response`, not `http_response` -- acronym runs are not
  special-cased. Acceptable for this PR's narrow scope; revisit if
  real schemas hit this.
* **Namespace name collision with the runtime package is unaddressed.**
  If a schema's own top-level namespace segment is literally `quarry` (a
  common convention in this repository's own fixtures), its generated
  `quarry/__init__.py` could collide with `quarry.runtime.python`'s own
  `quarry/__init__.py` if both were ever placed on the same
  `sys.path` with real (non-namespace-package) `__init__.py` files at the
  shared `quarry` segment. Not exercised by this PR's tests (which use a
  distinct `acme.*` test namespace precisely to sidestep this), and not a
  concern for the isolated skeleton-verification use case PR-118 targets,
  but worth resolving (e.g. via PEP 420 namespace packages at the shared
  prefix, or reserving a different runtime package name) before real
  multi-namespace production use.
* **No re-exports in generated `__init__.py` files.** Every generated
  `__init__.py` (both ancestor-package markers and the runtime's own) is
  a near-empty file with only a one-line docstring; there is no
  `from .schema import Sample`-style convenience re-export yet, so callers
  must import the concrete module (`acme.telemetry.schema.Sample`), not
  the package (`acme.telemetry.Sample`).

---

## Implementation roadmap

Sequencing sketch (subject to revision as each PR reveals concrete
problems), continuing PR-117 §11's sketch:

1. PR-118 (this PR): architecture skeleton -- zero-field records only.
2. Scalar field support (bool, fixed-width integers, f32/f64) plus real
   runtime codec mechanics in `runtime/python/` (varuint, header/Field
   Directory assembly, `struct`-based scalar pack/unpack).
3. Enum support (`enum.IntEnum`, matching PR-117 §8's finding that
   `IntEnum`'s constructor already raises `ValueError` for undefined
   values with no extra validation code needed).
4. String/bytes fields.
5. Array fields.
6. Nested record fields.
7. Python-keyword escaping and generated `__init__.py` re-exports, once
   there is real content worth re-exporting.
8. Interoperability verification against the C/C++ backends' wire output.

---

## Testing

`tests/backend_python/backend_python_test.cpp` covers backend
registration, output planning, package/`__init__.py` layout (including
sibling-namespace ancestor deduplication), zero-field dataclass
generation, exact-template verification, helper-function snake_case
naming, epoch-check preamble presence and placement, the
record-with-fields failure diagnostic, enum-only-namespace exclusion,
plan/generate agreement, and generation determinism.
`tests/tools/schema_compiler_tool_test.cpp` adds `--language python`
coverage (list-outputs, determinism, `--file-extension` rejection,
generated content). `tests/backend_python/python_execution_test.cpp` is
the genuinely new kind of test this PR adds: it invokes the real
`quarry-schema-compiler` binary to generate a package, then a real
`python3` subprocess to import it, instantiate a zero-field record, verify
`encode()` raises `NotImplementedError`, and verify an epoch mismatch
raises `ImportError` at import time. No serialization tests exist, since
there is no serialization to test.
