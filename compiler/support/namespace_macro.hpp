#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

// Namespace-to-macro encoding (#117).
//
// One deterministic spelling of a namespace name for every identity macro,
// generated-code identity guard and C configuration-identity accessor. It is
// injective over the namespace names the frontend accepts, preserves case, and
// never contains a reserved identifier spelling. Output paths, file extensions,
// root file stems and --generate do not enter the encoding.
//
// A future --name-prefix (A4.5, #102) is a leading segment of this encoding,
// not text concatenated onto it. Call encode_namespace_macro_segments with the
// prefix segment first. This header does not implement the flag.
//
// Encoding
// --------
// A namespace FQN is zero or more segments separated by '.'. A segment is an
// identifier: [A-Za-z_][A-Za-z0-9_]*. The empty FQN is the root namespace.
//
// The encoded body is:
//   * root (no segments): the single character 0
//   * otherwise: each segment encoded as below, joined by one '_'
//
// Segment encoding copies every character except:
//   * 'q' becomes "qq"
//   * '_' becomes "q0"
//
// So dep.engine encodes as dep_engine, dep_engine as depq0engine, Ab as Ab,
// a.b_c as a_bq0c and a_b.c as aq0b_c. A segment that ends in '_' encodes that
// '_' as q0, so the '_' that joins segments is never adjacent to another '_'.
//
// Injectivity
// -----------
// The segment encoding is a prefix code: a decoder that sees 'q' consumes the
// following character (q -> 'q', 0 -> '_') and otherwise copies one character.
// Concatenating a prefix code is injective, and an encoded segment contains no
// '_'. Splitting the body on '_' therefore recovers the segments uniquely.
//
// Every encoded segment starts with a letter (a segment starts with a letter,
// or with '_' which encodes as q0, or with 'q' which encodes as qq). The root
// body "0" starts with a digit, so it is not an encoded segment and not a join
// of them. decode_namespace_macro is the inverse: it returns the FQN, or ""
// for the root, or nullopt for a string outside the image.
//
// Reserved spellings
// ------------------
// The body is "0" or matches [A-Za-z][A-Za-z0-9]*(_[A-Za-z][A-Za-z0-9]*)*.
// Identity macros place that body between a prefix that ends in a single '_'
// and a suffix that starts with a single '_', and neither side contains '__'.
// The concatenation then matches [A-Za-z][A-Za-z0-9_]* and contains no '__',
// so it is a C and C++ macro name and not a reserved identifier (no double
// underscore anywhere, and it does not begin with an underscore).
//
// The canonical generated-code identity format is unchanged. Renderers still
// write the M placeholder; only the substituted stem is this encoding.
namespace quarry::compiler::support {

// Prefixes of the identity macro families. The encoded body is appended, then
// the family's suffix (_SCHEMA_ID_*, _CODE_ID_*, _<guard hex>_, _schema_id).
inline constexpr std::string_view kCppOwningNamespaceMacroPrefix = "QUARRY_GENERATED_CPP_NS_";
inline constexpr std::string_view kCppCStorageNamespaceMacroPrefix = "QUARRY_GENERATED_CPP_C_NS_";
inline constexpr std::string_view kCNamespaceMacroPrefix = "QUARRY_NS_";
inline constexpr std::string_view kCNamespaceAccessorPrefix = "quarry_ns_";

// Body of `namespace_fqn` ("" is the root). nullopt when a segment is not an
// identifier or a separator is empty, leading or trailing.
[[nodiscard]] std::optional<std::string> encode_namespace_macro(std::string_view namespace_fqn);

// Body of `segments` in order. An empty span is the root body "0". A future
// --name-prefix passes its identifier as the first segment, ahead of the
// namespace segments. nullopt when any segment is not an identifier.
[[nodiscard]] std::optional<std::string>
encode_namespace_macro_segments(std::span<const std::string_view> segments);

// Inverse of encode_namespace_macro. "" is the root. nullopt when `encoded`
// is not an encoding of a namespace name.
[[nodiscard]] std::optional<std::string> decode_namespace_macro(std::string_view encoded);

// True when `identifier` matches [A-Za-z][A-Za-z0-9_]* and contains no "__".
// Such a name does not begin with '_' and is not a reserved C or C++ identifier.
[[nodiscard]] bool is_unreserved_macro_identifier(std::string_view identifier);

} // namespace quarry::compiler::support
