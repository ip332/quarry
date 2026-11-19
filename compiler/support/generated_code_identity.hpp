#pragma once

#include "compiler/support/configuration_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Generated-code identity (#111): a 128-bit identity of one namespace's
// generated definitions in one backend variant.
//
// It is separate from the configuration identity (schema_id, the wire
// identity) and from the generated-code API epochs (the generator/runtime
// contract). Two bindings of a namespace share a generated-code identity
// exactly when their normalized generated definitions, their variant, their
// configuration and the identities of the generated namespaces they depend on
// are all equal. Generated C++ uses it to give different definitions
// different mangled names and to reject mixed dependencies at compile time.
//
// Compiler-only: generated bindings and runtimes never depend on this header,
// and nothing here is computed at runtime.
namespace quarry::compiler::support {

// Version of the canonical encoding below. A change to the encoding, the
// placeholder grammar or the normalization rules is a new format.
inline constexpr std::uint8_t kGeneratedCodeIdentityFormat = 1U;
inline constexpr std::size_t kGeneratedCodeIdentitySize = 16U;
// Domain separation: the encoding starts with these bytes, including the NUL.
inline constexpr std::string_view kGeneratedCodeIdentityDomain{"QUARRY-GENERATED-CODE-ID\0", 25};

enum class GeneratedCodeVariant : std::uint8_t {
    C = 1,           // plain C (not yet identified; #111 G2)
    CppOwning = 2,   // --language cpp
    CppCStorage = 3, // --language cpp --cpp-storage c
};

enum class GeneratedFileRole : std::uint8_t {
    Header = 1,
    Source = 2,
};

struct GeneratedCodeIdentity {
    std::uint8_t format = kGeneratedCodeIdentityFormat;
    std::array<std::uint8_t, kGeneratedCodeIdentitySize> bytes{};

    friend bool operator==(const GeneratedCodeIdentity&, const GeneratedCodeIdentity&) = default;
};

// One generated file of the namespace, as normalized text: the backend's
// rendering with structured placeholders (below) in place of every
// self-referential and output-naming token.
struct GeneratedCodeIdentityFile {
    GeneratedFileRole role = GeneratedFileRole::Header;
    std::string_view normalized_content;
};

// A generated namespace this namespace's generated files include.
struct GeneratedCodeIdentityDependency {
    std::string_view namespace_fqn;
    GeneratedCodeVariant variant = GeneratedCodeVariant::CppOwning;
    GeneratedCodeIdentity identity;
};

struct GeneratedCodeIdentityInput {
    GeneratedCodeVariant variant = GeneratedCodeVariant::CppOwning;
    ConfigurationIdentity configuration;
    std::string_view namespace_fqn;
    std::vector<GeneratedCodeIdentityFile> files;
    std::vector<GeneratedCodeIdentityDependency> dependencies;
};

// Canonical encoding, format 1. All integers are big-endian; lp(x) is a u32
// byte length followed by the bytes of x.
//
//   "QUARRY-GENERATED-CODE-ID" 0x00
//   u8  format (1)
//   u8  variant
//   u8  configuration identity algorithm
//   16  schema_id
//   lp  namespace FQN (UTF-8, "" for the root namespace)
//   u32 file count, then per file in ascending role order:  u8 role, lp normalized content
//   u32 dependency count, then per dependency in ascending FQN byte order:
//       lp FQN, u8 variant, u8 identity format, 16 identity bytes
//
// Returns nullopt (with `error`) for a malformed input: duplicate file roles,
// duplicate dependencies, a dependency on itself, or content that is not valid
// placeholder text.
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_generated_code_identity_input(const GeneratedCodeIdentityInput& input, std::string& error);

// SHA-256 of the canonical encoding, truncated to its first 16 bytes.
[[nodiscard]] std::optional<GeneratedCodeIdentity>
compute_generated_code_identity(const GeneratedCodeIdentityInput& input, std::string& error);

// 32 lowercase hex digits, the spelling used in quarry_gen_<hex>.
[[nodiscard]] std::string generated_code_identity_hex(const GeneratedCodeIdentity& identity);
[[nodiscard]] std::string generated_code_identity_hex_upper(const GeneratedCodeIdentity& identity);
// Big-endian 32-bit word `index` (0..3), for generated identity macros.
[[nodiscard]] std::uint32_t generated_code_identity_word(const GeneratedCodeIdentity& identity,
                                                         std::size_t index);

// --------------------------------------------------------------------------
// Structured renderer placeholders
//
// Renderers never write their own generated-code identity (it is not known
// until their output is hashed) or any token derived from output naming
// (--output-directory, --file-extension, --root-file-stem) directly. They write
// one of these typed placeholders instead. A placeholder is framed by the
// ASCII record and unit separators, which never occur in generated text:
//
//   0x1E 'S' form 0x1F       this namespace's generated-code identity, where
//                            form is 'h' (32 lowercase hex digits), 'H' (32
//                            uppercase), '0'..'3' (word i as 0x........UL) or
//                            'b' (the 16 bytes as 0x..U, separated by ", ")
//   0x1E 'P' fqn 0x1F        the generated include path of namespace fqn
//   0x1E 'M' fqn 0x1F        the include-path-derived macro stem of namespace fqn
//
// The placeholder text is the normalized content that is hashed; the final
// output substitutes every placeholder. Nothing else is rewritten.
namespace placeholder {

inline constexpr char kBegin = '\x1E';
inline constexpr char kEnd = '\x1F';

[[nodiscard]] std::string self_identity(char form);
[[nodiscard]] std::string include_path(std::string_view namespace_fqn);
[[nodiscard]] std::string macro_stem(std::string_view namespace_fqn);

} // namespace placeholder

struct PlaceholderValues {
    GeneratedCodeIdentity self;
    // Namespace FQN -> value, for 'P' and 'M' placeholders.
    std::map<std::string, std::string, std::less<>> include_paths;
    std::map<std::string, std::string, std::less<>> macro_stems;
};

// Whether `text` uses the placeholder framing only in well-formed placeholders.
[[nodiscard]] bool validate_placeholder_text(std::string_view text, std::string& error);

// `text` with every placeholder replaced; nullopt (with `error`) if a
// placeholder is malformed or names a namespace without a value.
[[nodiscard]] std::optional<std::string>
substitute_placeholders(std::string_view text, const PlaceholderValues& values, std::string& error);

} // namespace quarry::compiler::support
