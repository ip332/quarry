# Examples

Examples demonstrate supported public runtime and generated-code workflows.

Current examples:

* `cpp/basic_encode_decode`: minimal C++ runtime package consumer using
  `find_package(Quarry CONFIG REQUIRED)` and `Quarry::runtime`.
* `cpp/schema_compiler_cmake`: canonical downstream CMake pattern using
  `Quarry::schema_compiler` in `add_custom_command()` and compiling the
  generated C++ against `Quarry::runtime`.

Language-specific examples should be added only with corresponding language
runtime or generator support.
