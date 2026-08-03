# Examples

Examples demonstrate supported public runtime and generated-code workflows.

Current examples:

* `cpp/basic_encode_decode`: minimal C++ runtime package consumer using
  `find_package(Quarry CONFIG REQUIRED)` and `Quarry::runtime`.
* `cpp/schema_compiler_cmake`: canonical downstream CMake pattern using
  `Quarry::schema_compiler` for separate dependency/root generation and
  compiling cross-namespace generated C++ against `Quarry::runtime`.
* `c/basic_encode_decode`: strict-C99 generated C consumer using the installed
  compiler and `Quarry::runtime_c`.
* `python/basic_encode_decode`: generated Python dataclass consumer using an
  installed runtime wheel.
* `interop/cpp_python`: focused C++ encoder to Python decoder BRF workflow.

Each example README gives the install, generation, build, and execution
commands. Imported dependency roots are always generated explicitly into the
same output directory; no example implies automatic dependency generation.
