# Two truck revisions in one application

This example uses the authoritative revision A root at
`../schemas/configuration.brd` and revision B at `rev_b/configuration.brd`.
Revision B imports the same records and adds `truck.cms.MirrorFold` with a
`folded: bool` field. No configuration name implicitly supplies a prefix.

| Configuration | Complete schema_id | EngineStatus ID | MirrorFold ID |
|---|---|---|---|
| Revision A | `9200ca494f010801b830d0857cf66dde` | `0x00050001` | absent |
| Revision B | `806e9a4e11d0df71c4e5b51045f85010` | `0x00050001` | `0x00030002` |

Install Quarry and its Python runtime wheel first. From this directory, run:

```sh
python3 run.py --prefix /path/to/installed/quarry \
  --python /path/to/runtime-venv/bin/python \
  --go-runtime ../../../runtime/go --go-module example.com/fleet
```

`--go-runtime` points to the Go runtime module's source tree; it is not an
installed CMake target. `--go-module` is the caller-selected example application
module path, passed through `--go-import-prefix`. Go 1.22+ is required. The runner
uses a temporary build directory and local Go caches; it publishes nothing.
Use `--work /tmp/truck-multi` to retain generated files and logs for inspection.
Source-tree developers may replace the installed wheel with
`--python-runtime ../../../runtime/python/src`. `--skip-go` explicitly limits
execution to the native and Python examples when Go is unavailable.

The runner builds and executes:

- C++ owning revision A and B in one translation unit, using
  `rev_a::truck::engine` and `rev_b::truck::engine`.
- Plain C99 revision A and B in one translation unit. The approved prefixed-only
  C naming rule gives stems `revq0a_truck_engine_` and `revq0b_truck_engine_`.
- C++/C-storage wrappers in one translation unit, using the same encoded stems
  beneath `quarry::cxx`.
- Both absolute Python package trees in one process, plus unprefixed revision A
  as a wire-byte control.
- Both Go import trees in one binary, plus the unprefixed wire-byte control.

Every variant encodes and decodes EngineStatus and revision B's MirrorFold,
checks the actual record IDs, and verifies revision A/B EngineStatus bytes agree.
The shared timestamp exercises a generated cross-namespace dependency. The native
CMake project demonstrates `NAME_PREFIX`, `SCHEMA_ID_VARIABLE`, multiple helper
calls sharing an output directory, and explicit C/C-storage custom commands.
Python imports and Go imports stay absolute. Existing dependency identity checks
remain enabled. Selection of a vehicle revision remains out of band; there is
no runtime negotiation, new wire metadata or native ABI interchangeability claim.

Run only the native examples with:

```sh
cmake -S . -B /tmp/truck-native -DCMAKE_PREFIX_PATH=/path/to/installed/quarry
cmake --build /tmp/truck-native --parallel
/tmp/truck-native/truck_multi_cpp
/tmp/truck-native/truck_multi_c
/tmp/truck-native/truck_multi_c_storage
```

The public-example validation runs this workflow through an installed package
and Python wheel. Go execution is explicitly skipped only on developer machines
without Go; CI and the A4.5 validation run all five variants. Unprefixed source
names remain unchanged, and #119's general C naming migration remains deferred.
