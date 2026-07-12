# Backend

Owns backend-facing scaffolding.

Responsibilities:

* consume validated Schema IR
* generate deterministic backend artifacts from validated IR
* report backend-specific failures
* preserve schema semantics and layout meaning

Current C++ generation behavior:

* exposes `CodegenOptions`, `GeneratedFile`, and `CodegenResult`
* accepts Schema IR plus backend options only
* emits files only for namespaces that directly own records or enums
* derives output file paths from the namespace FQN and configured output root
* uses include paths relative to the generated include root
  * for example, `generated/alpha/one.generated.hpp` is included as
    `"alpha/one.generated.hpp"`
* generates concrete `enum class` declarations for enum IR objects
  * uses `std::int64_t` as the fixed underlying type so enum values are never
    narrowed or truncated
  * preserves explicit numeric values exactly as stored in Schema IR
  * does not auto-number omitted values or synthesize reflection helpers
* generates concrete `struct` declarations for record IR objects
* preserves declaration order unless record dependencies force a deterministic
  reordering
* fails clearly when record dependencies form a cycle instead of emitting
  invalid C++
* maps supported primitive field types to C++ scalar types
  * `bool` -> `bool`
  * `i8`, `i16`, `i32`, `i64` -> `std::int*_t`
  * `u8`, `u16`, `u32`, `u64` -> `std::uint*_t`
  * `f32` -> `float`
  * `f64` -> `double`
* lowers named record and enum references to fully qualified C++ names
* emits namespace blocks matching the Schema IR namespace hierarchy
* fails clearly when it encounters a valid-but-unsupported field kind such as
  `string`, `bytes`, or `array`
* returns `success = false`, a non-empty `error_message`, and no generated
  files for backend failures
* keeps enum formatting, parsing, reflection, and helper APIs out of scope
  for this PR

Allowed dependencies:

* `compiler/schema_ir`
* `compiler/support`

This layer must not parse source, resolve names, perform semantic analysis,
compute layout, or assign compiler-managed identifiers.
