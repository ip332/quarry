# Examples

Examples demonstrate supported public runtime and generated-code workflows.

Current examples:

* `cpp/basic_encode_decode`: minimal C++ runtime package consumer using
  `find_package(Quarry CONFIG REQUIRED)` and `Quarry::runtime`.
* `cpp/schema_compiler_cmake`: canonical downstream CMake pattern using
  `Quarry::schema_compiler` to generate a root and its imported namespace from
  one compilation root (`GENERATE`) and compile cross-namespace generated C++
  against `Quarry::runtime`.
* `c/basic_encode_decode`: strict-C99 generated C consumer using the installed
  compiler and `Quarry::runtime_c`.
* `python/basic_encode_decode`: generated Python dataclass consumer using an
  installed runtime wheel.
* `interop/cpp_python`: focused C++ encoder to Python decoder BRF workflow.
* `truck`: a Class 8 truck configuration root. Embedded C bindings for the
  engine ECU and cloud-side Python bindings are generated separately from the
  same root and agree on every record ID.
* `truck/multi_configuration`: rev A and rev B coexist in C++ owning, C99,
  C++/C storage, Python and Go using explicit prefixes and real round trips.
* `protobuf/descriptor_to_brd`: descriptor-set protobuf translation into
  bounded `.brd` units, followed by separate Quarry compiler invocations.

Each example README gives the install, generation, build, and execution
commands. Bindings that reference one another are always generated from the
same compilation root, so they share one record-ID mapping.

## Stage A verification

The [truck A/B example](truck/multi_configuration/README.md) demonstrates all five
variants in one application/process. The maintained [A5 matrix](../tests/interop/matrix/README.md)
exchanges actual BRF streams across all 25 producer/consumer pairs, including
nested cuts. These are complementary checks; independent round trips alone do
not establish interoperability. See the [CLI guide](../docs/stage-a-cli.md) and
[release migration](../docs/release-notes-v0.1.31-rc.1.md).
