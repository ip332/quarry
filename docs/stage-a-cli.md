# Stage A CLI verification guide

Run from the repository root with the current built or installed
`quarry-schema-compiler` on PATH (`build/debug/tools` for a source build).
These literal commands are executed by `stage_a_documentation_test`; temporary
outputs are used during testing. Full flag syntax is in [Tools](../tools/README.md).

## Identity and resolved inputs

```sh
quarry-schema-compiler --version
quarry-schema-compiler --help
quarry-schema-compiler --print-generated-code-api-version
quarry-schema-compiler --print-schema-id examples/truck/schemas/configuration.brd
quarry-schema-compiler --describe examples/truck/schemas/configuration.brd
quarry-schema-compiler --list-inputs examples/truck/schemas/configuration.brd
```

The API query prints `3`, the C++ owning epoch. The schema query prints
`9200ca494f010801b830d0857cf66dde` plus newline. `--describe` reports 6 domains,
8 records, formats `brf 2, qbs 1, schema-ir 1` and epochs
`cpp 3, c 7, python 1, go 1`. All eight IDs are tabulated in the
[readiness report](release-readiness-v0.1.31-rc.1.md#authoritative-truck-mapping).
`--list-inputs` lists all nine resolved schema paths (the root plus eight units),
without writing outputs. Compiler version/SHA/tag lines depend on the build;
the baseline reports `quarry-schema-compiler 0.1.291 (2a9aafd, untagged)`.
This is not a claim that the proposed release tag exists.

## QBS modes

```sh
quarry-schema-compiler --emit-qbs build/stage-a-docs/minimal.qbs examples/truck/schemas/configuration.brd
quarry-schema-compiler --emit-qbs build/stage-a-docs/reflective.qbs --qbs-reflective examples/truck/schemas/configuration.brd
```

Both headers contain algorithm 1 at byte 8 and the same full identity at bytes
12–27. Minimal and reflective images differ in optional names; repeat generation
of each mode is byte-identical. Selection or naming prefixes do not change
these wire bytes. Emitting QBS is explicit and does not generate bindings.

## Selection, prefixing and storage

```sh
quarry-schema-compiler --language c --name-prefix rev_a --generate truck.common,truck.engine --c-optimize-size=truck.common --output-directory build/stage-a-docs/c --list-outputs examples/truck/schemas/configuration.brd
quarry-schema-compiler --language c --name-prefix rev_a --generate truck.common,truck.engine --c-optimize-size=truck.common --output-directory build/stage-a-docs/c examples/truck/schemas/configuration.brd
quarry-schema-compiler --language cpp --cpp-storage c --c-optimize-size --generate truck.common,truck.engine --output-directory build/stage-a-docs/cpp-c examples/truck/schemas/configuration.brd
```

Listing writes nothing; generation writes exactly the listed files. Prefixes
nest paths beneath literal `rev_a/` and encode C symbols as
`revq0a_truck_engine_EngineStatus_t`. `--generate` and storage lists take canonical
unprefixed namespace FQNs. Bare `--c-optimize-size` compacts every namespace;
`=truck.common` compacts only that exact namespace, not its descendants. Declare
the same effective policy in separate invocations, including policies for
unselected dependencies; mismatches are compile errors. Output naming can change
source definitions/gid, never wire schema_id. Unprefixed #119 collisions remain
outside this RC's same-TU support contract.

## Diagnostics and exits

| Case | Exit | Diagnostic / behavior |
|---|---|---|
| Successful generation/query | 0 | stdout for queries; ordinary generation is quiet |
| Unknown `--generate` namespace | 1 | `BC8002`, lists available namespaces, no files written |
| Unreadable/invalid schema or backend/write failure | 1 | error on stderr |
| `--qbs-reflective` without `--emit-qbs` | 2 | `--qbs-reflective requires --emit-qbs` |
| `--describe` with `--print-schema-id` | 2 | query conflict |
| Invalid/repeated prefix, malformed option syntax | 2 | usage error |

`--describe` ignores emission selections (even unknown FQNs), storage/backend
choices and output naming after basic argument parsing. It cannot combine with
other query modes, QBS emission or version output. Unknown per-namespace storage
names fail generation with exit 1; malformed lists fail usage with exit 2.
No discovery, compatibility negotiation, descriptor emission or automatic
historical-record translation is provided.

The existing [consumer guide](generated-binding-compatibility.md#working-separate-invocation-examples)
executes separate generation plus strict C99/C++20 compilation. The
[truck A/B runner](../examples/truck/multi_configuration/README.md) and installed
example tests exercise all five variants; this guide does not duplicate them.
