# Python Backend

The Python backend consumes validated Schema IR and emits importable dataclass
modules. Its generated public representation is unchanged across PR-117
through PR-140: scalar, enum, string, bytes, nested record, and supported
array fields are
`Optional[...]` values where `None` means absent and an empty list means a
present empty array.

Generated modules now emit and consume BRF v2 through the canonical compiler
Layout IR. Fixed slots, presence bits, variable descriptors, and tail ordering
are emitted as layout metadata; Python does not recalculate wire offsets.

Supported array element types are fixed-width scalars, non-negative enums,
bounded strings, and bounded bytes. String and bytes
arrays use the existing BRF count-plus-length-delimited-element encoding and
carry the element `max_bytes` constraint from Schema IR into the runtime
helper call. Nested record fields and record arrays, including references to
declarations in imported namespaces, compose generated child helpers and the
existing variable-width array framing. Nested arrays and recursive by-value
records remain unsupported.

The backend does not modify Schema IR or the compiler pipeline and does not
depend on the C or C++ backend. See [the design document](../../docs/design/python-backend.md)
for the complete lowering, runtime, naming, testing, and packaging contract.

## Local distribution validation

From `runtime/python/`, install the test tooling and build standard artifacts:

```sh
python -m pip install -e '.[test]'
python -m build --wheel --sdist
```

The wheel or a wheel rebuilt from the sdist can be installed into a clean
virtual environment. Generated modules import `quarry.runtime.python` from
that installation only; they do not require the Quarry source tree on
`PYTHONPATH`. The package is not published to PyPI yet.

## Shared layout

Both `Backend::plan()` and `Backend::generate()` also have an overload taking the
caller's `layout::LayoutModel`, computed once from the same Schema IR.
`quarry-schema-compiler` uses those overloads so one compilation computes its BRF layout
once (see `docs/compiler-architecture.md`, "Configuration Preparation"); the overloads
without a layout compute it themselves.

## Explicit naming prefix

`CodegenOptions::name_prefix` (CLI `--name-prefix`) is optional and empty by
default. It projects generated source names and output paths without modifying
Schema IR, canonical FQNs, record IDs or configuration identity. Selection and
storage-policy options still use canonical namespace names. Unprefixed output
is pinned byte for byte in `tests/fixtures/name_prefix/unprefixed_sha256.json`.
See the [source naming contract](../../tools/README.md#deliberate-source-naming-isolation)
and [multi-configuration example](../../examples/truck/multi_configuration/README.md).

## Stage A contract and release migration

Generate dependent namespaces from one authoritative configuration root. Selection
changes emission only; `--name-prefix` isolates deliberately coexisting sets without
changing wire identity. The [normative identity contract](../../docs/specifications/configuration-and-code-identity.md)
and [release notes](../../docs/release-notes-v0.1.31-rc.1.md) distinguish wire
schema_id, native gid and runtime epochs. Regenerate old bindings and rebuild
consumers; equal epochs do not certify native ABI compatibility. Python has
configuration import checks; the native per-namespace gid applies only to C/C++.
