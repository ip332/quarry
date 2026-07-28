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
  * `GenerationPlan` (built by the internal `build_render_generation_plan`
    stage) is the single, canonical model for generated filenames, output
    directories, and include paths — there is no second filename-computation
    path anywhere in the backend, CLI tool, or CMake helper
  * `Backend::plan()` (backing `--list-outputs`) and `Backend::generate()`
    (backing actual file writing) both call `build_render_generation_plan`
    and share the same `output_path_for_planned_file()` function to turn a
    planned relative path into the final output path, so the two modes
    cannot diverge
  * the CMake helper (`quarry_generate_cpp()`) never predicts or recomputes
    filenames itself; it takes the compiler's `--list-outputs` output as the
    literal, authoritative file list
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
* generates `encode_result(const Record&)` overloads that return
  `::quarry::runtime::EncodeResult<std::vector<std::byte>>`
  * encoding is deterministic and returns either an owning byte vector or a
    structured `EncodeError`
  * encode errors distinguish schema bounds, invalid UTF-8, unknown enum
    values, unsupported present field types, and runtime overflow
  * successful results contain a complete top-level Binary Record Format v0.1
    record
  * present supported fields are encoded through the runtime library
  * absent fields are omitted, including absent fields whose type is not yet
    encodable
  * present unsupported fields return `EncodeError::unsupported_field_type`
  * supported present field types are `bool`, fixed-width signed and unsigned
    integers, `f32`, `f64`, `string`, `bytes`, enum references whose declared
    values are all non-negative, arrays of supported fixed-width scalar or
    non-negative enum element types, arrays of `string`, `bytes`, or record
    references, and record references
  * string encoding validates UTF-8 and returns `EncodeError::invalid_utf8` for
    malformed string bytes
  * bytes encoding accepts arbitrary byte sequences
  * arrays of strings and bytes encode an element count followed by one
    length-delimited raw element payload per element; string elements validate
    UTF-8 and bytes elements accept arbitrary byte sequences
  * arrays of records encode an element count followed by one length-delimited
    complete embedded BRF record per element
  * record references encode by calling the referenced record's generated
    encoder and storing the complete embedded BRF record bytes as the parent
    field payload
  * present nested arrays return `EncodeError::unsupported_field_type`
  * unknown-field preservation remains out of scope
* generates `encode(const Record&)` compatibility wrappers that return
  `std::optional<std::vector<std::byte>>`
  * wrappers delegate to `encode_result` and intentionally discard error detail
* generates `decode_RecordName_result(std::span<const std::byte>)` overloads
  that return `::quarry::runtime::DecodeResult<RecordName>`
  * decoding structurally parses a complete top-level Binary Record Format v0.1
    record through the runtime library
  * structural parse/read failures preserve the runtime `DecodeError`
  * a mismatched `record_id` returns `DecodeError::unexpected_record_id`
  * unknown field indexes are ignored after structural validation
  * absent unsupported known fields are ignored
  * present unsupported known fields return
    `DecodeError::unsupported_field_type`
  * supported known field types match the encoder subset: `bool`, fixed-width
    signed and unsigned integers, `f32`, `f64`, `string`, `bytes`,
    non-negative enum references, arrays of supported fixed-width scalar or
    non-negative enum element types, arrays of `string`, `bytes`, or record
    references, and record references
  * string decoding validates UTF-8 and returns `DecodeError::invalid_utf8` for
    malformed string bytes
  * bytes decoding accepts arbitrary byte sequences
  * array decoding validates the encoded element count against `max_elements`
    before allocating, requires exact payload consumption, and preserves
    element order
  * arrays of strings and bytes decode one length-delimited raw element payload
    per element, enforce per-element `max_bytes`, validate UTF-8 for string
    elements, and preserve empty elements distinctly from empty arrays
  * arrays of records decode one length-delimited complete embedded BRF record
    per element, verify every element through the referenced record's generated
    decoder, and preserve empty records distinctly from empty arrays
  * record references decode by passing the bounded parent field span to the
    referenced record's generated decoder, which verifies the nested
    `record_id`, BRF version, flags, reserved fields, and payload length
  * present nested arrays return `DecodeError::unsupported_field_type`
  * decoded values are materialized through the generated builder, preserving
    presence and existing bounds validation
  * nested records and record-array elements propagate the child codec's root
    error code unchanged, and append a `PathElement` frame (the field's own
    `field_index`, plus an `array_index` when the frame is an array element) as
    the failure unwinds outward, so `path.front()` is the innermost failure
    site and `path.back()` is the outermost field
  * leaf field-level failures (bounds, UTF-8, unknown enum value, unsupported
    field type, structural read failures) attach a single-frame path directly
    at the point of failure
  * whole-field structural failures with no specific element attributable
    (array element-count/length parsing before any element loop, trailing-byte
    mismatches after the loop, builder rejection after all elements decoded)
    attach a field-level frame with no `array_index`
  * decode failures also carry an absolute-from-top-level-input
    `byte_offset`, translated across nested-record and array-element
    forwarding the same way `path` accumulates; encode results never carry
    one — see `runtime/README.md`'s "Byte-Offset Context" section
  * generated error results do not include diagnostic strings, and will
    not — a closed decision (PR-101), not a deferred one; see
    `runtime/README.md`'s "Diagnostic String Boundary" section
* generates `decode_RecordName(std::span<const std::byte>)` compatibility
  wrappers that return `std::optional<RecordName>`
  * wrappers delegate to `decode_RecordName_result` and intentionally discard
    error detail
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
* includes `quarry/runtime/binary_record.hpp` when a generated file contains
  records — the same canonical public include path documented for
  hand-written consumers (see `runtime/README.md`)
* emits a compile-time generated-code API compatibility assertion for generated
  record headers
  * the assertion checks
    `::quarry::runtime::kGeneratedCodeApiVersion`
  * the expected value is rendered from the backend's private configured
    generated-code API header, which is derived from the top-level
    `QUARRY_GENERATED_CODE_API_VERSION` CMake scalar
  * it guards only the generated C++ source/runtime header contract
  * it does not enforce package release equality, runtime ABI compatibility,
    schema-language compatibility, or BRF wire compatibility
* returns `success = false`, a non-empty `error_message`, and no generated
  files for backend failures
* keeps enum formatting, parsing, and reflection or accessors beyond the
  minimal const inspection API out of scope, as a closed decision (PR-101)
  rather than a deferred one; field-path and byte-offset codec diagnostics
  are implemented (see above; PR-091 through PR-094)
* backend code-generation tests consume validated Schema IR directly and do
  not exercise either source frontend

Implementation architecture (investigated in PR-100; not split — see
`jira/backlog.md` for the full analysis and a documented, deferred future
split proposal):

`backend.cpp` is a single translation unit implementing a strict,
one-directional pipeline of five phases, each communicating with the next
only through a well-defined data structure — no phase holds mutable state
that survives past its own call, and no cyclic dependency exists between
phases:

1. **Type Catalog** (`collect_named_types`, `lookup_named_type`) — one
   recursive pass over the whole Schema IR namespace tree, producing a flat
   `TypeCatalog` (id → C++ name, file path, include path) used to resolve
   any named type reference anywhere in the schema, not just the current
   namespace.
2. **Namespace/Declaration Analysis** (`analyze_namespace`,
   `lower_field_type`, `lower_runtime_field_encoding`,
   `order_declarations_topologically`) — consumes the Type Catalog and
   produces the `NamespacePlan` tree: per-field C++ type names, per-field
   runtime codec strategy (`RuntimeFieldEncoding`), required includes, and a
   topologically-sorted declaration order (cycle detection included).
3. **File Planning** (`collect_planned_files`, `validate_generation_plan`,
   `detect_file_cycles`, `to_public_generation_plan`,
   `build_render_generation_plan`) — consumes only the `NamespacePlan`
   tree's file/include metadata (never its declaration/field content) and
   produces the flat `RenderGenerationPlan` (internal) and public
   `GenerationPlan`. `Backend::plan()` stops here.
4. **Rendering** (`render_enum_definition`, `render_record_builder_definition`,
   `render_field_validation_function`/`_statements`, `render_record_definition`,
   `render_namespace_file`, and the encode/decode codec renderers documented
   below) — consumes one fully-resolved `NamespacePlan` node at a time and
   produces C++ text; has no dependency on the Type Catalog or the file
   plan.
5. **Output Assembly** (`render_generation_plan`, `output_path_for_planned_file`,
   `Backend::generate()`) — the thin loop gluing the file plan to the
   renderer and computing final on-disk paths. `backend.cpp` never touches
   the filesystem itself; writing `GeneratedFile::content` to disk is the
   caller's responsibility (`tools/schema_compiler`).

A cross-cutting family of small, pure text/name-formatting helpers
(`indent`, `namespace_parts`, `join_path`, `file_stem_for_namespace`,
`file_path_for_namespace`, `include_path_for_namespace`, `cpp_qualified_name`,
`cpp_namespace_prefix`, `namespace_comment`, `field_member_name`,
`append_namespace_open`/`_close`, `emits_records`) is shared across phases
1, 2, and 4 — these are not phase-exclusive.

Internal rendering helper architecture:

* one dispatcher per direction (`render_field_encoding`, `render_field_decoding`)
  routes to exactly one type-specific renderer per schema field kind (scalar,
  enum, string, bytes, record, array); each type-specific renderer holds real,
  substantial logic and is not a thin wrapper
* small lookup/utility helpers (runtime scalar append/read function names,
  enum append/read function names and width, namespace/path formatting,
  include requirement merging, the internal-to-public `GenerationPlan`
  conversion) each centralize one genuine mapping or transformation reused at
  real call sites — none of them merely forward their arguments unchanged
* `include_path_for_namespace` currently computes the same value as
  `file_stem_for_namespace` (the output-directory prefix that
  `file_path_for_namespace` adds is deliberately omitted for `#include` text),
  but keeps a distinct name because its one call site stores both values into
  different fields for different purposes — the name documents that contract
  even though the two happen to coincide today
* per-record encode and decode definitions structurally mirror each other but
  are not redundant: encoding and decoding are different operations and
  cannot share one implementation
* no dead or purely-forwarding helper exists in this file; investigated and
  confirmed as part of PR-097

Allowed dependencies:

* `compiler/schema_ir`
* `compiler/support`

This layer must not parse source, resolve names, perform semantic analysis,
compute layout, or assign compiler-managed identifiers.
