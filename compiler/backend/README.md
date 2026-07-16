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
* generates `encode(const Record&)` overloads that return
  `std::optional<std::vector<std::byte>>`
  * encoding is deterministic and returns `std::nullopt` on failure
  * successful results contain a complete top-level Binary Record Format v0.1
    record
  * present supported fields are encoded through the runtime library
  * absent fields are omitted, including absent fields whose type is not yet
    encodable
  * present unsupported fields return `std::nullopt`
  * supported present field types are `bool`, fixed-width signed and unsigned
    integers, `f32`, `f64`, `string`, `bytes`, enum references whose declared
    values are all non-negative, arrays of supported fixed-width scalar or
    non-negative enum element types, arrays of `string` or `bytes`, and record
    references
  * string encoding validates UTF-8 and returns `std::nullopt` for malformed
    string bytes
  * bytes encoding accepts arbitrary byte sequences
  * arrays of strings and bytes encode an element count followed by one
    length-delimited raw element payload per element; string elements validate
    UTF-8 and bytes elements accept arbitrary byte sequences
  * record references encode by calling the referenced record's generated
    encoder and storing the complete embedded BRF record bytes as the parent
    field payload
  * present arrays of records and nested arrays return `std::nullopt`
  * unknown-field preservation remains out of scope
* generates `decode_RecordName(std::span<const std::byte>)` overloads that
  return `std::optional<RecordName>`
  * decoding structurally parses a complete top-level Binary Record Format v0.1
    record through the runtime library
  * a mismatched `record_id` returns `std::nullopt`
  * unknown field indexes are ignored after structural validation
  * absent unsupported known fields are ignored
  * present unsupported known fields return `std::nullopt`
  * supported known field types match the encoder subset: `bool`, fixed-width
    signed and unsigned integers, `f32`, `f64`, `string`, `bytes`,
    non-negative enum references, arrays of supported fixed-width scalar or
    non-negative enum element types, arrays of `string` or `bytes`, and record
    references
  * string decoding validates UTF-8 and returns `std::nullopt` for malformed
    string bytes
  * bytes decoding accepts arbitrary byte sequences
  * array decoding validates the encoded element count against `max_elements`
    before allocating, requires exact payload consumption, and preserves
    element order
  * arrays of strings and bytes decode one length-delimited raw element payload
    per element, enforce per-element `max_bytes`, validate UTF-8 for string
    elements, and preserve empty elements distinctly from empty arrays
  * record references decode by passing the bounded parent field span to the
    referenced record's generated decoder, which verifies the nested
    `record_id`, BRF version, flags, reserved fields, and payload length
  * present arrays of records and nested arrays return `std::nullopt`
  * decoded values are materialized through the generated builder, preserving
    presence and existing bounds validation
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
  * generated codecs recheck array bounds so external bytes cannot bypass
    builder validation
  * generated codecs recheck per-element `max_bytes` for string and bytes array
    elements before copying decoded element data
  * invalid values are rejected atomically rather than truncated
  * generated codecs recheck string and bytes bounds so decoded external bytes
    cannot bypass builder validation
* lowers named record and enum references to fully qualified C++ names
* emits namespace blocks matching the Schema IR namespace hierarchy
* includes standard headers only when required
  * `<cstddef>` for `std::byte`
  * `<cstdint>` for fixed-width integers and enum underlying types
  * `<optional>` for generated record presence tracking
  * `<span>` for generated decode inputs
  * `<utility>` for generated encoder moves
  * `<string>` for `std::string`
  * `<vector>` for arrays, `bytes`, and generated encoder results
* includes `runtime/binary_record.hpp` when a generated file contains records
* returns `success = false`, a non-empty `error_message`, and no generated
  files for backend failures
* keeps enum formatting, parsing, reflection, accessors beyond the minimal
  const inspection API, and richer decode diagnostics out of scope for this PR
* backend code-generation tests consume validated Schema IR directly and do
  not exercise either source frontend

Allowed dependencies:

* `compiler/schema_ir`
* `compiler/support`

This layer must not parse source, resolve names, perform semantic analysis,
compute layout, or assign compiler-managed identifiers.
