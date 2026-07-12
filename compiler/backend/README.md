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
* generates a corresponding `RecordNameBuilder` for every record IR object
  * builders start with every field absent
  * setters return `bool` and leave the builder unchanged when validation fails
  * `has_<field>()` distinguishes absence from a present default value
  * `build()` returns an immutable logical record value
  * generated record values expose only const inspection methods and do not
    allow public mutation after construction
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
* maps `string` to `std::string`
* maps `bytes` to `std::vector<std::byte>`
* maps arrays recursively to `std::vector<CppType<T>>`
* enforces schema-defined bounds in generated setters when they are present in
  validated Schema IR
  * string `max_bytes` is measured in encoded bytes
  * bytes `max_bytes` is measured in bytes
  * array `max_count` is measured in element count
  * invalid values are rejected atomically rather than truncated
* lowers named record and enum references to fully qualified C++ names
* emits namespace blocks matching the Schema IR namespace hierarchy
* includes standard headers only when required
  * `<cstddef>` for `std::byte`
  * `<cstdint>` for fixed-width integers and enum underlying types
  * `<optional>` for generated record presence tracking
  * `<string>` for `std::string`
  * `<vector>` for arrays and `bytes`
* returns `success = false`, a non-empty `error_message`, and no generated
  files for backend failures
* keeps enum formatting, parsing, reflection, codecs, accessors beyond the
  minimal const inspection API, and runtime serialization out of scope for
  this PR

Allowed dependencies:

* `compiler/schema_ir`
* `compiler/support`

This layer must not parse source, resolve names, perform semantic analysis,
compute layout, or assign compiler-managed identifiers.
