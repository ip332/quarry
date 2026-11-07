#include "compiler/backend_c/backend_c.hpp"
#include "compiler/backend_c/generated_code_api_version_c.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quarry::compiler::backend_c {

namespace {

using ::quarry::schema_ir::EnumIR;
using ::quarry::schema_ir::EnumValueIR;
using ::quarry::schema_ir::FieldIR;
using ::quarry::schema_ir::FieldType;
using ::quarry::schema_ir::NamespaceIR;
using ::quarry::schema_ir::RecordIR;
using ::quarry::compiler::layout::FieldStorage;
using ::quarry::compiler::layout::LayoutModel;
using ::quarry::compiler::layout::RecordLayout;

// --- Namespace/symbol naming -------------------------------------------------
//
// Independently derived for the C backend; intentionally not shared with
// compiler/backend/backend.cpp (see compiler/backend_c/README.md). The naming
// convention itself (namespace FQN -> underscore-joined symbol prefix) matches
// docs/architecture/language-generators.md's existing C namespace-mapping
// specification and docs/design/c-backend.md Section 5.

[[nodiscard]] std::vector<std::string> namespace_parts(std::string_view fqn) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : fqn) {
        if (ch == '.') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

[[nodiscard]] std::string join_with(const std::vector<std::string>& parts, char separator) {
    std::string joined;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            joined.push_back(separator);
        }
        joined.append(parts[index]);
    }
    return joined;
}

// Namespace FQN -> C symbol prefix, e.g. "quarry.telemetry" -> "quarry_telemetry_".
// The root (synthetic, unnamed) namespace contributes no prefix segment.
[[nodiscard]] std::string symbol_prefix_for_namespace(std::string_view fqn) {
    const std::vector<std::string> parts = namespace_parts(fqn);
    if (parts.empty()) {
        return "";
    }
    return join_with(parts, '_') + "_";
}

// Namespace FQN -> file stem without extension, e.g. "quarry.telemetry" ->
// "quarry/telemetry". The root namespace uses the configured root file stem.
[[nodiscard]] std::string file_stem_for_namespace(const CodegenOptions& options,
                                                  std::string_view fqn) {
    const std::vector<std::string> parts = namespace_parts(fqn);
    if (parts.empty()) {
        return options.root_file_stem;
    }
    return join_with(parts, '/');
}

[[nodiscard]] std::string uppercase_alnum_or_underscore(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
            result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        } else {
            result.push_back('_');
        }
    }
    return result;
}

// C99 keywords are kept here rather than in Schema IR validation because
// this is a C-backend naming concern. Other backends may legitimately map
// the same source spelling differently.
[[nodiscard]] bool is_c_keyword(std::string_view identifier) {
    static constexpr std::string_view keywords[] = {
        "auto",     "break",    "case",      "char",      "const",     "continue",
        "default",  "do",       "double",    "else",      "enum",      "extern",
        "float",    "for",      "goto",      "if",        "inline",    "int",
        "long",     "register", "restrict",  "return",    "short",     "signed",
        "sizeof",   "static",   "struct",    "switch",    "typedef",   "union",
        "unsigned", "void",     "volatile",  "while",     "_Bool",     "_Complex",
        "_Imaginary"};
    return std::find(std::begin(keywords), std::end(keywords), identifier) !=
           std::end(keywords);
}

[[nodiscard]] bool is_c_identifier(std::string_view identifier) {
    if (identifier.empty()) {
        return false;
    }
    const auto is_start = [](char character) {
        return std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_';
    };
    const auto is_continue = [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
    };
    if (!is_start(identifier.front())) {
        return false;
    }
    return std::all_of(identifier.begin() + 1, identifier.end(), is_continue);
}

[[nodiscard]] bool is_c_reserved_identifier(std::string_view identifier) {
    // The backend uses the conservative file-scope rule for every emitted
    // name, including struct members: no generated identifier begins with
    // an underscore. This covers the C implementation-reserved `__...`,
    // `_Upper...`, and file-scope `_lower...` families uniformly.
    return !identifier.empty() && identifier.front() == '_';
}

[[nodiscard]] std::string safe_c_identifier(std::string_view candidate) {
    std::string normalized;
    normalized.reserve(candidate.size());
    for (const char character : candidate) {
        normalized.push_back(
            (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_')
                ? character
                : '_');
    }
    if (normalized.empty()) {
        return "quarry_generated";
    }
    if (std::isdigit(static_cast<unsigned char>(normalized.front())) != 0) {
        normalized = "quarry_" + normalized;
    }
    if (is_c_keyword(normalized) || is_c_reserved_identifier(normalized) ||
        !is_c_identifier(normalized)) {
        normalized = "quarry_" + normalized;
    }
    return normalized;
}

class CIdentifierAllocator {
  public:
    [[nodiscard]] std::string allocate(std::string_view candidate) {
        const std::string base = safe_c_identifier(candidate);
        if (used_.insert(base).second) {
            return base;
        }
        for (std::uint32_t suffix = 2U;; ++suffix) {
            const std::string disambiguated = base + "_" + std::to_string(suffix);
            if (used_.insert(disambiguated).second) {
                return disambiguated;
            }
        }
    }

    [[nodiscard]] bool reserve(std::string_view identifier) {
        return used_.insert(std::string(identifier)).second;
    }

    [[nodiscard]] bool contains(std::string_view identifier) const {
        return used_.find(std::string(identifier)) != used_.end();
    }

  private:
    std::set<std::string> used_;
};

struct FieldEncoding;
[[nodiscard]] bool is_field_scratch_required(const FieldEncoding& encoding);

[[nodiscard]] std::string header_guard_macro(std::string_view relative_header_path) {
    return "QUARRY_GENERATED_C_" + uppercase_alnum_or_underscore(relative_header_path) + "_";
}

// --- Field type lowering --------------------------------------------------
//
// Lower Schema IR into bounded host storage and shared-codec descriptors.
// Logical C layout is independent of canonical wire layout. Record references
// are resolved and ordered before rendering because nested values are embedded
// by value. Preserve the established generated member-name allocation even
// though the shared codec no longer needs per-field scratch variables.

struct FieldEncoding {
    std::string c_type;       // e.g. "int32_t", "float", "bool", or an enum's own typedef name
                              // -- for an array field, describes the *element* type
    std::string runtime_verb; // e.g. "i32" -> quarry_c_write_i32 / quarry_c_read_i32
                              // -- for an array field, the *element* verb
    std::uint8_t width_bytes = 0U; // for an array field, the *element* width
    bool is_enum = false;          // for an array field, whether the *element* type is an enum
    std::vector<std::int64_t> enum_valid_values; // only meaningful when is_enum
    bool is_string = false;
    std::uint64_t string_max_bytes = 0U; // only meaningful when is_string; widened to
                                         // std::uint64_t so max_bytes + 1 cannot overflow
                                         // while computing the generated buffer's capacity,
                                         // even for a (pathological) max_bytes == UINT32_MAX
    bool is_bytes = false;
    std::uint32_t bytes_max_bytes = 0U; // only meaningful when is_bytes; capacity is exactly
                                        // this value (no "+1" -- see "Bytes fields" above), so
                                        // unlike string_max_bytes this never needs widening
    bool is_array = false;
    std::uint32_t array_max_elements = 0U; // only meaningful when is_array
    std::uint64_t array_scratch_capacity = 0U; // only meaningful when is_array; widened to
                                               // std::uint64_t for the same overflow-avoidance
                                               // reason as string_max_bytes (max_elements *
                                               // width_bytes, plus the worst-case varuint count
                                               // prefix, could overflow uint32_t)
    bool array_element_is_record = false; // only meaningful when is_array; true when the
                                          // array's element type is a same-namespace record
                                          // reference rather than a scalar/enum. Reuses (does
                                          // not duplicate) record_target_ir_id/
                                          // record_symbol_name/record_max_encoded_size below --
                                          // a field is never simultaneously is_record and
                                          // is_array, so field reuse is unambiguous, mirroring
                                          // how is_enum already double-duties for both plain-enum
                                          // and array-of-enum fields.
    bool array_element_is_fixed_record = false;
    std::uint32_t array_element_fixed_record_size = 0U;
    bool is_record = false;
    std::uint64_t record_target_ir_id = 0U; // only meaningful when is_record; the referenced
                                            // record's Schema IR id, used to look up its
                                            // (by-then-already-computed) max_encoded_size once
                                            // same-namespace records are processed in
                                            // dependency order (see collect_namespace_files)
    std::uint32_t record_target_id = 0U;
    std::string record_symbol_name; // only meaningful when is_record, e.g.
                                    // "quarry_telemetry_Child" (no _t/_encode/_decode suffix --
                                    // c_type is set to "<record_symbol_name>_t" directly, and
                                    // rendering appends "_encode"/"_encoded_size"/"_decode" as
                                    // needed, matching how a plain field never stores redundant
                                    // derived strings either)
    std::uint64_t record_max_encoded_size = 0U; // only meaningful when is_record; the
                                                // referenced record's own worst-case total
                                                // encoded byte count (header + Field Directory
                                                // + payload, every field assumed present),
                                                // used to size this field's encode scratch
                                                // buffer; resolved after topological sorting,
                                                // not at initial lowering time (see
                                                // collect_namespace_files)
};

[[nodiscard]] bool is_field_scratch_required(const FieldEncoding& encoding) {
    return encoding.is_array || encoding.is_record ||
           (!encoding.is_string && !encoding.is_bytes);
}

// A field name participates in more than the struct-member namespace. Its
// generated base is also used for presence/length/count members and for
// function-local scratch variables. Reserve all of those names together so
// two fields cannot collide after lowering.
[[nodiscard]] std::string allocate_field_name(CIdentifierAllocator& allocator,
                                               std::string_view source_name,
                                               const FieldEncoding& encoding) {
    const std::string base = safe_c_identifier(source_name);
    for (std::uint32_t suffix = 0U;; ++suffix) {
        const std::string candidate = suffix == 0U
                                          ? base
                                          : base + "_" + std::to_string(suffix + 1U);
        std::vector<std::string> generated_names;
        generated_names.push_back(candidate);
        generated_names.push_back("has_" + candidate);
        if (encoding.is_array) {
            generated_names.push_back(candidate + "_count");
        } else if (encoding.is_string || encoding.is_bytes) {
            generated_names.push_back(candidate + "_length");
        }
        if (is_field_scratch_required(encoding)) {
            generated_names.push_back(candidate + "_bytes");
        }
        if (encoding.is_enum) {
            generated_names.push_back(candidate + "_raw");
        }
        if (encoding.is_record) {
            generated_names.push_back(candidate + "_encode_result");
            generated_names.push_back(candidate + "_size");
        }

        if (std::all_of(generated_names.begin(), generated_names.end(),
                        [&allocator](const std::string& name) {
                            return !allocator.contains(name);
                        })) {
            for (const std::string& name : generated_names) {
                (void)allocator.reserve(name);
            }
            return candidate;
        }
    }
}

// Builds a non-enum FieldEncoding. A plain function (not a designated
// aggregate initializer at each call site) so every field of the struct is
// always explicitly assigned exactly once, regardless of how many members
// FieldEncoding has -- GCC's -Wmissing-field-initializers (part of
// -Wextra) flags a designated initializer that omits any member, even one
// with a default member initializer, unlike Clang; this was caught by the
// Docker/CI-equivalent validation's GCC toolchain, not reproduced locally
// under Clang.
[[nodiscard]] FieldEncoding make_scalar_encoding(std::string c_type, std::string runtime_verb,
                                                 std::uint8_t width_bytes) {
    FieldEncoding encoding;
    encoding.c_type = std::move(c_type);
    encoding.runtime_verb = std::move(runtime_verb);
    encoding.width_bytes = width_bytes;
    encoding.is_enum = false;
    return encoding;
}

// Returns std::nullopt for any field type this backend does not support yet.
[[nodiscard]] std::optional<FieldEncoding> lower_scalar_field_type(const FieldType& type) {
    if (type.kind_case() != FieldType::kPrimitive) {
        return std::nullopt;
    }
    using ::quarry::schema_ir::PrimitiveType;
    switch (type.primitive()) {
    case PrimitiveType::PRIMITIVE_TYPE_BOOL:
        return make_scalar_encoding("bool", "bool", 1U);
    case PrimitiveType::PRIMITIVE_TYPE_I8:
        return make_scalar_encoding("int8_t", "i8", 1U);
    case PrimitiveType::PRIMITIVE_TYPE_U8:
        return make_scalar_encoding("uint8_t", "u8", 1U);
    case PrimitiveType::PRIMITIVE_TYPE_I16:
        return make_scalar_encoding("int16_t", "i16", 2U);
    case PrimitiveType::PRIMITIVE_TYPE_U16:
        return make_scalar_encoding("uint16_t", "u16", 2U);
    case PrimitiveType::PRIMITIVE_TYPE_I32:
        return make_scalar_encoding("int32_t", "i32", 4U);
    case PrimitiveType::PRIMITIVE_TYPE_U32:
        return make_scalar_encoding("uint32_t", "u32", 4U);
    case PrimitiveType::PRIMITIVE_TYPE_I64:
        return make_scalar_encoding("int64_t", "i64", 8U);
    case PrimitiveType::PRIMITIVE_TYPE_U64:
        return make_scalar_encoding("uint64_t", "u64", 8U);
    case PrimitiveType::PRIMITIVE_TYPE_F32:
        return make_scalar_encoding("float", "f32", 4U);
    case PrimitiveType::PRIMITIVE_TYPE_F64:
        return make_scalar_encoding("double", "f64", 8U);
    case PrimitiveType::PRIMITIVE_TYPE_UNSPECIFIED:
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::string unsigned_runtime_verb_for_width(std::uint8_t width_bytes) {
    switch (width_bytes) {
    case 1U:
        return "u8";
    case 2U:
        return "u16";
    case 4U:
        return "u32";
    default:
        return "u64";
    }
}

// Smallest unsigned width capable of representing max_value, matching
// compiler/backend/backend.cpp's enum_width_for_max_value exactly -- this
// is the property that keeps enum field wire encoding byte-for-byte
// identical between the C and C++ backends.
[[nodiscard]] std::uint8_t enum_width_for_max_value(std::uint64_t max_value) {
    if (max_value <= std::numeric_limits<std::uint8_t>::max()) {
        return 1U;
    }
    if (max_value <= std::numeric_limits<std::uint16_t>::max()) {
        return 2U;
    }
    if (max_value <= std::numeric_limits<std::uint32_t>::max()) {
        return 4U;
    }
    return 8U;
}

// --- Enum catalog -----------------------------------------------------
//
// A flat, whole-schema map from enum ir_id to the information a field
// reference needs, built once before any namespace file is planned. This
// is the smallest addition that lets a record field resolve an EnumRef by
// id (Schema IR's own addressing scheme) without recomputing each
// referenced enum's declaration data at every field site, and without
// needing a broader Type-Catalog-style refactor -- namespace/file planning
// itself is unchanged.

struct EnumCatalogEntry {
    std::string type_name;    // e.g. "quarry_telemetry_Status_t"
    std::string value_prefix; // e.g. "QUARRY_TELEMETRY_STATUS"
    std::string owning_namespace_fqn;
    bool all_non_negative = false;
    std::uint8_t width_bytes = 0U; // meaningful only when all_non_negative
    std::vector<std::int64_t> values;
};

using EnumCatalog = std::unordered_map<std::uint64_t, EnumCatalogEntry>;

[[nodiscard]] bool collect_enum_catalog(const NamespaceIR& ns, EnumCatalog& catalog,
                                        std::string& error_message) {
    const std::string symbol_prefix = symbol_prefix_for_namespace(ns.fqn());
    for (const EnumIR& enum_ir : ns.enums()) {
        EnumCatalogEntry entry;
        entry.type_name = safe_c_identifier(symbol_prefix + enum_ir.name()) + "_t";
        entry.value_prefix = uppercase_alnum_or_underscore(symbol_prefix + enum_ir.name());
        entry.owning_namespace_fqn = ns.fqn();
        entry.all_non_negative = true;

        std::uint64_t max_value = 0U;
        for (const EnumValueIR& value_ir : enum_ir.values()) {
            if (value_ir.value() > std::numeric_limits<std::int32_t>::max() ||
                value_ir.value() < std::numeric_limits<std::int32_t>::min()) {
                std::ostringstream stream;
                stream << "backend_c: enum value out of supported range for '" << enum_ir.fqn()
                       << "." << value_ir.name() << "' (" << value_ir.value()
                       << "): the C backend does not yet support enum values outside the "
                          "32-bit signed integer range";
                error_message = stream.str();
                return false;
            }
            entry.values.push_back(value_ir.value());
            if (value_ir.value() < 0) {
                entry.all_non_negative = false;
            } else {
                max_value = std::max(max_value, static_cast<std::uint64_t>(value_ir.value()));
            }
        }
        if (entry.all_non_negative) {
            entry.width_bytes = enum_width_for_max_value(max_value);
        }
        catalog.emplace(enum_ir.ir_id(), std::move(entry));
    }

    for (const NamespaceIR& child : ns.namespaces()) {
        if (!collect_enum_catalog(child, catalog, error_message)) {
            return false;
        }
    }
    return true;
}

// --- Planning -----------------------------------------------------------

struct PlannedEnumValue {
    std::string name;
    std::string c_name;
    std::int64_t value = 0;
};

struct PlannedEnum {
    std::string type_name;
    std::string symbol_name;
    std::vector<PlannedEnumValue> values;
};

struct PlannedField {
    std::string name;        // generated C member/local base name
    std::string source_name; // original Schema IR field name, for diagnostics
    std::uint32_t field_index = 0U;
    std::uint32_t presence_bit_index = 0U;
    std::uint32_t byte_offset = 0U;
    std::uint32_t slot_size = 0U;
    FieldStorage storage = FieldStorage::Fixed;
    FieldEncoding encoding;
};

struct PlannedRecord {
    std::uint64_t ir_id = 0U;
    std::string symbol_name;
    std::uint32_t record_id = 0U;
    std::uint32_t presence_bitmap_size = 0U;
    std::uint32_t fixed_region_size = 0U;
    std::vector<PlannedField> fields;
    // Transient: same-namespace record ir_ids this record's fields embed by
    // value, used only by order_records_topologically below (not consulted
    // by any rendering function). May contain duplicate ir_ids if a record
    // embeds the same other record more than once -- harmless, standard
    // Kahn's-algorithm topological sort handles duplicate dependency edges
    // correctly.
    std::vector<std::uint64_t> same_namespace_dependencies;
};

struct PlannedNamespaceFile {
    std::string namespace_fqn;
    bool emits_output = true;
    std::string relative_header_path;
    std::string relative_source_path;
    std::string generated_include_path;
    std::set<std::string> generated_includes;
    std::vector<PlannedEnum> enums;
    std::vector<PlannedRecord> records;
};

struct OutputSelection {
    std::set<std::string> planned_namespaces;
    std::set<std::string> emitting_namespaces;
};

[[nodiscard]] std::string generated_include_path_for_namespace(const CodegenOptions& options,
                                                               std::string_view namespace_fqn) {
    return file_stem_for_namespace(options, namespace_fqn) + options.header_extension;
}

[[nodiscard]] bool namespace_emits_file(const NamespaceIR& ns) {
    return ns.records_size() > 0 || ns.enums_size() > 0;
}

// Worst-case LEB128-encoded byte length for any uint32_t value: ceil(32/7) =
// 5. Used (rather than computing the exact value-dependent encoded size of
// a specific max_elements) to size an array field's scratch buffer, so this
// backend does not need a second, host-side reimplementation of
// quarry_c_varuint_encoded_size's algorithm to keep in sync with the
// runtime -- the few extra bytes this can over-allocate relative to the
// exact size are negligible next to the array's own data.
constexpr std::uint32_t kMaxVaruintBytesForUint32 = 5U;

// Worst-case LEB128-encoded byte length for any uint64_t value: ceil(64/7) =
// 10. Used (rather than kMaxVaruintBytesForUint32) to estimate a record's
// own worst-case Field Directory overhead in compute_record_max_encoded_size
// below: a field's wire *offset*/*length* are size_t/uint64_t quantities in
// the runtime's own API (quarry_c_record_encoded_size's out_size parameter,
// quarry_c_write_varuint's value parameter), not uint32_t-capped like an
// array's element *count* (which is genuinely bounded by a uint32_t
// max_elements) -- so estimating a nested record's own scratch-buffer
// capacity with the narrower 32-bit bound would theoretically
// under-provision for a pathologically large nested record. Using the wider
// bound here costs only a few extra conservatively-unused bytes per field
// and removes that theoretical gap entirely.
constexpr std::uint64_t kMaxVaruintBytesForUint64 = 10U;

// Resolves an enum-typed reference (either a plain enum field, or an array
// field's enum element type) by ir_id, given the whole-schema enum catalog
// and the FQN of the namespace the reference occurs in. `context_description`
// is used only in diagnostic text (e.g. "field 'X.Y'" or "field 'X.Y' array
// element type"). Returns std::nullopt with `error_message` set for a
// cross-namespace or negative-valued enum; returns std::nullopt with
// `error_message` left untouched if `target_enum_ir_id` is not in the
// catalog at all (callers fall through to their own generic diagnostic in
// that case, matching this function's only caller's pre-refactor behavior).
[[nodiscard]] std::optional<FieldEncoding>
lower_enum_reference(std::uint64_t target_enum_ir_id, std::string_view current_namespace_fqn,
                     const EnumCatalog& catalog, std::string_view context_description,
                     std::string& error_message) {
    (void)current_namespace_fqn;
    (void)context_description;
    const auto catalog_it = catalog.find(target_enum_ir_id);
    if (catalog_it == catalog.end()) {
        return std::nullopt;
    }
    const EnumCatalogEntry& entry = catalog_it->second;
    if (!entry.all_non_negative) {
        std::ostringstream stream;
        stream << "backend_c: " << context_description
               << " references an enum with a negative declared value; enum fields "
                  "are only supported when every declared value is non-negative, "
                  "matching the C++ backend and the BRF spec's Enum Encoding rule";
        error_message = stream.str();
        return std::nullopt;
    }
    FieldEncoding encoding;
    encoding.c_type = entry.type_name;
    encoding.width_bytes = entry.width_bytes;
    encoding.runtime_verb = unsigned_runtime_verb_for_width(entry.width_bytes);
    encoding.is_enum = true;
    encoding.enum_valid_values = entry.values;
    return encoding;
}

// --- Record catalog -----------------------------------------------------
//
// A flat, whole-schema map from record ir_id to the identity information a
// nested-record field reference needs (symbol_name, owning namespace),
// built once before any namespace file is planned -- mirroring
// collect_enum_catalog exactly, and for the same reason: a record field can
// reference another record by ir_id (Schema IR's own addressing scheme)
// regardless of declaration order within the schema, so every record's
// identity must be known up front. Unlike EnumCatalogEntry, `max_encoded_size`
// is deliberately left at 0 here and filled in *later*, once that record's
// own fields have been fully resolved in dependency order (see
// collect_namespace_files) -- a record's total encoded size depends on its
// fields, which is exactly the information not yet available at the point
// every record's catalog entry is first created.

struct RecordCatalogEntry {
    std::string symbol_name;    // e.g. "quarry_telemetry_Child" (no _t/_encode/_decode suffix)
    std::string owning_namespace_fqn;
    std::uint64_t max_encoded_size = 0U; // resolved later; see collect_namespace_files
    std::uint32_t record_id = 0U;
};

using RecordCatalog = std::unordered_map<std::uint64_t, RecordCatalogEntry>;

void collect_record_catalog(const NamespaceIR& ns, RecordCatalog& catalog) {
    const std::string symbol_prefix = symbol_prefix_for_namespace(ns.fqn());
    for (const RecordIR& record_ir : ns.records()) {
        RecordCatalogEntry entry;
        entry.symbol_name = safe_c_identifier(symbol_prefix + record_ir.name());
        entry.owning_namespace_fqn = ns.fqn();
        entry.record_id = record_ir.record_id();
        catalog.emplace(record_ir.ir_id(), std::move(entry));
    }
    for (const NamespaceIR& child : ns.namespaces()) {
        collect_record_catalog(child, catalog);
    }
}

// Resolves a record-typed reference (either a plain nested-record field, or
// an array field's record element type) by ir_id, given the whole-schema
// record catalog and the FQN of the namespace the reference occurs in.
// Mirrors lower_enum_reference's shape exactly, and for the same reason
// (PR-112 extracted lower_enum_reference so plain enum fields and
// array-of-enum elements share one ownership lookup; this does the same for
// records so plain nested-record fields (PR-113) and array-of-record elements
// (PR-114) share one resolution path).
// `context_description` is used only in diagnostic text (e.g. "field
// 'X.Y'" or "field 'X.Y' array element type"). Returns std::nullopt with
// `error_message` set for a cross-namespace reference; returns
// std::nullopt with `error_message` left untouched if `target_record_ir_id`
// is not in the catalog at all (callers fall through to their own generic
// diagnostic in that case). Deliberately does not set is_record or
// array_element_is_record itself -- callers set whichever flag applies,
// since the same reference-resolution logic serves both a plain
// record-typed field and a record-typed array element.
[[nodiscard]] std::optional<FieldEncoding>
lower_record_reference(std::uint64_t target_record_ir_id, std::string_view current_namespace_fqn,
                       const RecordCatalog& record_catalog, std::string_view context_description,
                       std::string& error_message) {
    (void)current_namespace_fqn;
    (void)context_description;
    (void)error_message;
    const auto catalog_it = record_catalog.find(target_record_ir_id);
    if (catalog_it == record_catalog.end()) {
        return std::nullopt;
    }
    const RecordCatalogEntry& entry = catalog_it->second;
    FieldEncoding encoding;
    encoding.record_target_ir_id = target_record_ir_id;
    encoding.record_symbol_name = entry.symbol_name;
    encoding.record_target_id = entry.record_id;
    encoding.c_type = entry.symbol_name + "_t";
    // encoding.record_max_encoded_size is resolved later, once the
    // referenced record's own fields have been processed in dependency
    // order -- see collect_namespace_files.
    return encoding;
}

// Computes a record's own worst-case total encoded byte count (16-byte
// header + Field Directory + payload), assuming every field is present
// simultaneously (the true worst case -- a real encoding with fewer present
// fields only produces fewer bytes, never more) and using
// kMaxVaruintBytesForUint64 for each Field Directory entry's offset/length
// varuint overhead. Used only to size a *parent* record's encode scratch
// buffer for a nested-record field; by the time this is called (in
// dependency order, after order_records_topologically), every field's own
// worst-case contribution -- including any nested record field's own
// record_max_encoded_size -- is already fully resolved.
[[nodiscard]] std::uint64_t
compute_record_max_encoded_size(const std::vector<PlannedField>& fields) {
    constexpr std::uint64_t kHeaderSize = 16U;
    constexpr std::uint64_t kDirectoryEntryOverhead =
        1U + kMaxVaruintBytesForUint64 + kMaxVaruintBytesForUint64; // field_index + offset + length
    std::uint64_t total = kHeaderSize;
    for (const PlannedField& field : fields) {
        total += kDirectoryEntryOverhead;
        if (field.encoding.is_array) {
            total += field.encoding.array_scratch_capacity;
        } else if (field.encoding.is_string) {
            total += field.encoding.string_max_bytes;
        } else if (field.encoding.is_bytes) {
            total += field.encoding.bytes_max_bytes;
        } else if (field.encoding.is_record) {
            total += field.encoding.record_max_encoded_size;
        } else {
            total += field.encoding.width_bytes;
        }
    }
    return total;
}

// Topologically sorts `records` (all declared in the same namespace) by
// same-namespace nested-record declaration dependency, so that any record a
// field embeds by value is always fully declared (and, per
// collect_namespace_files, has its own max_encoded_size already resolved)
// before the record that embeds it. Deterministic tie-break: original
// declaration order among records with no (remaining) unresolved
// dependency, exactly mirroring compiler/backend/backend.cpp's
// order_declarations_topologically. A schema with zero nested-record
// dependencies (every PR-107 through PR-112 schema) is unaffected: Kahn's
// algorithm with this tie-break reduces to a no-op reordering in that case,
// preserving existing generated output exactly.
[[nodiscard]] bool order_records_topologically(std::vector<PlannedRecord>& records,
                                               std::string_view namespace_fqn,
                                               std::string& error_message) {
    std::map<std::uint64_t, std::size_t> index_by_ir_id;
    for (std::size_t index = 0; index < records.size(); ++index) {
        index_by_ir_id.emplace(records[index].ir_id, index);
    }

    std::vector<std::size_t> indegree(records.size(), 0U);
    std::vector<std::vector<std::size_t>> dependents(records.size());
    for (std::size_t index = 0; index < records.size(); ++index) {
        for (const std::uint64_t dependency_ir_id : records[index].same_namespace_dependencies) {
            const auto it = index_by_ir_id.find(dependency_ir_id);
            if (it == index_by_ir_id.end()) {
                // Should not happen: lower_field_encoding only records a
                // same-namespace dependency after confirming the target is
                // in this same namespace's record list.
                error_message = "backend_c: could not resolve a same-namespace record "
                                "declaration dependency in namespace '" +
                                std::string(namespace_fqn) + "'";
                return false;
            }
            ++indegree[index];
            dependents[it->second].push_back(index);
        }
    }

    std::set<std::pair<std::size_t, std::size_t>> ready;
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (indegree[index] == 0U) {
            ready.emplace(index, index); // (source_order, index): original index is the
                                        // declaration order here, matching the C++
                                        // backend's identical tie-break
        }
    }

    std::vector<PlannedRecord> ordered;
    ordered.reserve(records.size());
    while (!ready.empty()) {
        const auto [source_order, index] = *ready.begin();
        (void)source_order;
        ready.erase(ready.begin());
        ordered.push_back(std::move(records[index]));

        for (const std::size_t dependent_index : dependents[index]) {
            if (--indegree[dependent_index] == 0U) {
                ready.emplace(dependent_index, dependent_index);
            }
        }
    }

    if (ordered.size() != records.size()) {
        error_message = "backend_c: detected a cycle in same-namespace nested record "
                        "declaration dependencies in namespace '" +
                        std::string(namespace_fqn) +
                        "' -- a record cannot embed itself, directly or transitively, by value";
        return false;
    }

    records = std::move(ordered);
    return true;
}

// Resolves one field's type, given the whole-schema enum catalog and the
// FQN of the namespace the referencing record belongs to. Returns
// std::nullopt (with error_message set) for every unsupported case: a
// non-scalar, non-enum, non-string, non-bytes, non-supported-array,
// unsupported type; an enum with a negative declared value; or a missing
// declaration id. Does *not* resolve a record-typed field's
// `record_max_encoded_size` (left at 0) -- see collect_namespace_files for
// why that is necessarily a separate, later step.
[[nodiscard]] std::optional<FieldEncoding>
lower_field_encoding(const RecordIR& record_ir, const FieldIR& field_ir,
                     std::string_view current_namespace_fqn, const EnumCatalog& catalog,
                     const RecordCatalog& record_catalog, std::string& error_message) {
    const FieldType& type = field_ir.type();

    if (type.kind_case() == FieldType::kPrimitive) {
        std::optional<FieldEncoding> encoding = lower_scalar_field_type(type);
        if (encoding.has_value()) {
            return encoding;
        }
    } else if (type.kind_case() == FieldType::kString) {
        // Schema validation already guarantees max_bytes > 0 and that it
        // fits uint32_t (compiler/semantic/semantic.cpp's
        // validate_positive_u32) -- nothing to re-validate here.
        FieldEncoding encoding;
        encoding.is_string = true;
        encoding.string_max_bytes = type.string().max_bytes();
        return encoding;
    } else if (type.kind_case() == FieldType::kBytes) {
        // Same schema-validation guarantee as string (validate_positive_u32
        // is called identically for both StringType and BytesType).
        FieldEncoding encoding;
        encoding.is_bytes = true;
        encoding.bytes_max_bytes = type.bytes().max_bytes();
        return encoding;
    } else if (type.kind_case() == FieldType::kEnumType) {
        const std::string context = "field '" + record_ir.fqn() + "." + field_ir.name() + "'";
        std::optional<FieldEncoding> encoding = lower_enum_reference(
            type.enum_type().target_enum_ir_id(), current_namespace_fqn, catalog, context,
            error_message);
        if (encoding.has_value()) {
            return encoding;
        }
        if (!error_message.empty()) {
            return std::nullopt;
        }
        // Catalog miss (enum id not found at all): fall through to the
        // generic diagnostic below, matching this branch's pre-refactor
        // behavior exactly.
    } else if (type.kind_case() == FieldType::kArray) {
        const ::quarry::schema_ir::ArrayType& array_type = type.array();
        const FieldType& element_type = array_type.element_type();
        std::optional<FieldEncoding> element_encoding;
        if (element_type.kind_case() == FieldType::kPrimitive) {
            element_encoding = lower_scalar_field_type(element_type);
        } else if (element_type.kind_case() == FieldType::kEnumType) {
            const std::string context =
                "field '" + record_ir.fqn() + "." + field_ir.name() + "' array element type";
            element_encoding = lower_enum_reference(element_type.enum_type().target_enum_ir_id(),
                                                    current_namespace_fqn, catalog, context,
                                                    error_message);
        } else if (element_type.kind_case() == FieldType::kString) {
            element_encoding = FieldEncoding{};
            element_encoding->is_string = true;
            element_encoding->string_max_bytes = element_type.string().max_bytes();
        } else if (element_type.kind_case() == FieldType::kBytes) {
            element_encoding = FieldEncoding{};
            element_encoding->is_bytes = true;
            element_encoding->bytes_max_bytes = element_type.bytes().max_bytes();
        } else if (element_type.kind_case() == FieldType::kRecord) {
            const std::string context =
                "field '" + record_ir.fqn() + "." + field_ir.name() + "' array element type";
            element_encoding =
                lower_record_reference(element_type.record().target_record_ir_id(),
                                       current_namespace_fqn, record_catalog, context,
                                       error_message);
            if (element_encoding.has_value()) {
                element_encoding->array_element_is_record = true;
            }
        }
        if (!element_encoding.has_value()) {
            if (error_message.empty()) {
                std::ostringstream stream;
                stream << "backend_c: field '" << record_ir.fqn() << "." << field_ir.name()
                       << "' is an array whose element type the C backend does not support yet "
                          "-- only arrays of bool, fixed-width signed/unsigned integer, f32/f64 "
                          "scalar elements, non-negative-valued enum elements, bounded "
                          "string/bytes elements, and record elements "
                          "are supported (see "
                          "docs/design/c-backend.md); nested arrays and recursive record "
                          "elements remain unsupported";
                error_message = stream.str();
            }
            return std::nullopt;
        }
        // Schema validation already guarantees max_elements > 0 and that it
        // fits uint32_t (the same validate_positive_u32 call used for
        // max_bytes) -- nothing to re-validate here.
        FieldEncoding encoding = *element_encoding;
        encoding.is_array = true;
        encoding.array_max_elements = array_type.max_elements();
        if (encoding.array_element_is_record) {
            // array_scratch_capacity depends on the element record's own
            // max_encoded_size, only resolved after topological sorting --
            // see collect_namespace_files Phase 3, which fills this in
            // (mirroring how a plain record field's record_max_encoded_size
            // is deferred the same way).
        } else if (encoding.is_string || encoding.is_bytes) {
            // The element type is assigned after the containing record's
            // field index is known. Its capacity is represented by the
            // generated element struct, not by a runtime allocation.
            encoding.array_scratch_capacity =
                static_cast<std::uint64_t>(kMaxVaruintBytesForUint32) +
                static_cast<std::uint64_t>(array_type.max_elements()) *
                    (static_cast<std::uint64_t>(kMaxVaruintBytesForUint32) +
                     (encoding.is_string ? encoding.string_max_bytes
                                          : static_cast<std::uint64_t>(encoding.bytes_max_bytes)));
        } else {
            encoding.array_scratch_capacity =
                static_cast<std::uint64_t>(kMaxVaruintBytesForUint32) +
                static_cast<std::uint64_t>(array_type.max_elements()) *
                    static_cast<std::uint64_t>(encoding.width_bytes);
        }
        return encoding;
    } else if (type.kind_case() == FieldType::kRecord) {
        const std::string context = "field '" + record_ir.fqn() + "." + field_ir.name() + "'";
        std::optional<FieldEncoding> encoding = lower_record_reference(
            type.record().target_record_ir_id(), current_namespace_fqn, record_catalog, context,
            error_message);
        if (encoding.has_value()) {
            encoding->is_record = true;
            return encoding;
        }
        if (!error_message.empty()) {
            return std::nullopt;
        }
        // Catalog miss (record id not found at all): fall through to the
        // generic diagnostic below, matching the enum catalog-miss
        // precedent exactly.
    }

    // Mixed supported/unsupported records fail as a whole: a struct that
    // silently dropped this field would be partial, misleading output, not
    // a smaller feature set.
    std::ostringstream stream;
    stream << "backend_c: field '" << record_ir.fqn() << "." << field_ir.name()
           << "' has a type the C backend does not support yet -- only bool, fixed-width "
              "signed/unsigned integer, f32/f64 scalar fields, non-negative enum fields "
              "with only non-negative declared values, bounded string/bytes fields, bounded "
              "arrays of those scalar/enum/string/bytes/record kinds, and nested record "
              "fields are supported (see docs/design/c-backend.md); nested arrays and "
              "recursive record fields remain unsupported";
    error_message = stream.str();
    return std::nullopt;
}

// Collects one PlannedNamespaceFile per namespace that directly owns records
// or enums, in Schema IR declaration order (pre-order namespace traversal),
// the same "emit files only for namespaces that directly own records or
// enums" rule compiler/backend/backend.cpp already follows for C++ -- an
// independently reached, not shared, implementation of the same policy.
[[nodiscard]] bool collect_namespace_files(const NamespaceIR& ns, const CodegenOptions& options,
                                           const EnumCatalog& catalog,
                                           RecordCatalog& record_catalog,
                                           const LayoutModel& layout_model,
                                           const OutputSelection* output_selection,
                                           std::vector<PlannedNamespaceFile>& files,
                                           std::string& error_message) {
    if (namespace_emits_file(ns)) {
        PlannedNamespaceFile file;
        file.namespace_fqn = ns.fqn();
        if (output_selection != nullptr) {
            file.emits_output = output_selection->emitting_namespaces.contains(ns.fqn());
        }
        if (output_selection != nullptr &&
            !output_selection->planned_namespaces.contains(ns.fqn())) {
            error_message = "backend_c: namespace '" + ns.fqn() +
                            "' is missing from the compiler output plan";
            return false;
        }
        CIdentifierAllocator enum_constants;
        const std::string stem = file_stem_for_namespace(options, ns.fqn());
        file.relative_header_path = stem + options.header_extension;
        file.relative_source_path = stem + options.source_extension;
        file.generated_include_path = file.relative_header_path;

        for (const EnumIR& enum_ir : ns.enums()) {
            // Already validated and computed once by collect_enum_catalog;
            // reused here rather than recomputed, so there is exactly one
            // place that decides an enum's rendering data.
            const EnumCatalogEntry& entry = catalog.at(enum_ir.ir_id());
            PlannedEnum planned_enum;
            planned_enum.type_name = entry.type_name;
            // Enum value constants use the fully uppercase
            // QUARRY_<NAMESPACE>_<ENUMNAME>_<VALUE> form (docs/design/c-backend.md
            // Section 5): C enumerators are not scoped, so the whole identifier is
            // conventionally upper-cased like a macro constant, unlike the
            // mixed-case quarry_<namespace>_<Record>_t struct/function/type naming.
            planned_enum.symbol_name = entry.value_prefix;
            for (const EnumValueIR& value_ir : enum_ir.values()) {
                const std::string normalized_name = uppercase_alnum_or_underscore(value_ir.name());
                planned_enum.values.push_back(PlannedEnumValue{
                    .name = normalized_name,
                    .c_name = enum_constants.allocate(planned_enum.symbol_name + "_" +
                                                       normalized_name),
                    .value = value_ir.value()});
            }
            file.enums.push_back(std::move(planned_enum));
        }

        const std::string symbol_prefix = symbol_prefix_for_namespace(ns.fqn());

        CIdentifierAllocator type_names;
        for (const PlannedEnum& planned_enum : file.enums) {
            (void)type_names.reserve(planned_enum.type_name);
        }
        for (const RecordIR& record_ir : ns.records()) {
            (void)type_names.reserve(
                safe_c_identifier(symbol_prefix + record_ir.name()) + "_t");
        }

        // Phase 1: lower every field of every record in this namespace, in
        // original declaration order. A record-typed field's
        // record_max_encoded_size cannot be resolved yet (its target may be
        // declared later in this same namespace); lower_field_encoding
        // leaves it at 0 and records the dependency in
        // same_namespace_dependencies instead, for Phase 2 to consume.
        std::vector<PlannedRecord> planned_records;
        planned_records.reserve(static_cast<std::size_t>(ns.records_size()));
        for (const RecordIR& record_ir : ns.records()) {
            const RecordLayout* canonical_layout = layout_model.find_record(record_ir.fqn());
            if (canonical_layout == nullptr ||
                canonical_layout->fields.size() != static_cast<std::size_t>(record_ir.fields_size())) {
                error_message = "backend_c: canonical BRF v2 layout is missing record '" +
                                record_ir.fqn() + "' or has an inconsistent field count";
                return false;
            }
            std::vector<PlannedField> planned_fields;
            CIdentifierAllocator field_names;
            std::vector<std::uint64_t> dependencies;
            planned_fields.reserve(static_cast<std::size_t>(record_ir.fields_size()));
            std::size_t field_position = 0U;
            for (const FieldIR& field_ir : record_ir.fields()) {
                std::optional<FieldEncoding> encoding = lower_field_encoding(
                    record_ir, field_ir, ns.fqn(), catalog, record_catalog, error_message);
                if (!encoding.has_value()) {
                    return false;
                }
                if (encoding->is_record ||
                    (encoding->is_array && encoding->array_element_is_record)) {
                    const std::uint64_t target_id = encoding->record_target_ir_id;
                    if (record_catalog.at(target_id).owning_namespace_fqn == ns.fqn()) {
                        dependencies.push_back(target_id);
                    }
                }
                PlannedField planned_field{
                    .name = allocate_field_name(field_names, field_ir.name(), *encoding),
                    .source_name = field_ir.name(),
                    .field_index = field_ir.field_index(),
                    .presence_bit_index = canonical_layout->fields[field_position].presence_bit_index,
                    .byte_offset = canonical_layout->fields[field_position].location.byte_offset,
                    .slot_size = canonical_layout->fields[field_position].slot_size,
                    .storage = canonical_layout->fields[field_position].storage,
                    .encoding = std::move(*encoding),
                };
                const auto& canonical_field = canonical_layout->fields[field_position];
                const auto* canonical_type = &canonical_field.type;
                if (canonical_field.type.kind ==
                        ::quarry::compiler::layout::LayoutTypeKind::Array &&
                    canonical_field.type.element_type != nullptr) {
                    canonical_type = canonical_field.type.element_type.get();
                }
                if (planned_field.encoding.is_enum &&
                    canonical_type->kind == ::quarry::compiler::layout::LayoutTypeKind::Enum) {
                    planned_field.encoding.width_bytes =
                        static_cast<std::uint8_t>(canonical_type->encoded_width);
                    planned_field.encoding.enum_valid_values.clear();
                    for (const std::uint64_t value : canonical_type->enum_values) {
                        planned_field.encoding.enum_valid_values.push_back(
                            static_cast<std::int64_t>(value));
                    }
                    planned_field.encoding.runtime_verb =
                        unsigned_runtime_verb_for_width(planned_field.encoding.width_bytes);
                }
                if (!planned_field.encoding.is_string && !planned_field.encoding.is_bytes &&
                    !planned_field.encoding.is_record && !planned_field.encoding.array_element_is_record &&
                    canonical_type->encoded_width != 0U) {
                    planned_field.encoding.width_bytes =
                        static_cast<std::uint8_t>(canonical_type->encoded_width);
                    planned_field.encoding.runtime_verb =
                        planned_field.encoding.is_enum
                            ? unsigned_runtime_verb_for_width(planned_field.encoding.width_bytes)
                            : planned_field.encoding.runtime_verb;
                }
                if (planned_field.encoding.is_array &&
                    planned_field.encoding.array_element_is_record &&
                    canonical_field.type.element_type != nullptr) {
                    planned_field.encoding.array_element_is_fixed_record =
                        canonical_field.type.element_type->classification ==
                        ::quarry::compiler::layout::RecordClassification::FixedSize;
                    planned_field.encoding.array_element_fixed_record_size =
                        canonical_field.type.element_type->encoded_width;
                }
                ++field_position;
                const bool references_record =
                    planned_field.encoding.is_record ||
                    planned_field.encoding.array_element_is_record;
                const bool references_enum = planned_field.encoding.is_enum;
                if (references_record || references_enum) {
                    const std::uint64_t target_id =
                        references_record
                            ? planned_field.encoding.record_target_ir_id
                            : (field_ir.type().kind_case() == FieldType::kArray
                                   ? field_ir.type().array().element_type().enum_type()
                                         .target_enum_ir_id()
                                   : field_ir.type().enum_type().target_enum_ir_id());
                    const std::string& owner = references_record
                                                   ? record_catalog.at(target_id).owning_namespace_fqn
                                                   : catalog.at(target_id).owning_namespace_fqn;
                    if (owner != ns.fqn()) {
                        if (output_selection != nullptr &&
                            !output_selection->planned_namespaces.contains(owner)) {
                            error_message = "backend_c: missing planned dependency namespace '" +
                                            owner + "' for field '" + record_ir.fqn() + "." +
                                            field_ir.name() + "'";
                            return false;
                        }
                        file.generated_includes.insert(
                            generated_include_path_for_namespace(options, owner));
                    }
                }
                if (planned_field.encoding.is_array &&
                    (planned_field.encoding.is_string || planned_field.encoding.is_bytes)) {
                    planned_field.encoding.c_type = type_names.allocate(
                        safe_c_identifier(symbol_prefix + record_ir.name()) + "_array_" +
                        std::to_string(planned_field.field_index) +
                        (planned_field.encoding.is_string ? "_string_element_t"
                                                          : "_bytes_element_t"));
                }
                planned_fields.push_back(std::move(planned_field));
            }
            planned_records.push_back(PlannedRecord{
                .ir_id = record_ir.ir_id(),
                .symbol_name = safe_c_identifier(symbol_prefix + record_ir.name()),
                .record_id = record_ir.record_id(),
                .presence_bitmap_size = canonical_layout->presence_bitmap_size,
                .fixed_region_size = canonical_layout->fixed_region_size,
                .fields = std::move(planned_fields),
                .same_namespace_dependencies = std::move(dependencies),
            });
        }

        // Phase 2: reorder so every nested-record dependency is fully
        // declared (and sized) before the record that embeds it -- also
        // where a same-namespace dependency cycle is detected and
        // rejected.
        if (!order_records_topologically(planned_records, ns.fqn(), error_message)) {
            return false;
        }

        // Phase 3: in that dependency order, resolve every record-typed
        // field's record_max_encoded_size (the referenced record, if
        // same-namespace, was necessarily processed earlier in this same
        // loop, so its catalog entry's max_encoded_size is already final)
        // -- and, for a record-element array field, also resolve
        // array_scratch_capacity now (deferred from Phase 1 for exactly the
        // same reason: it depends on the element record's own
        // max_encoded_size, only available at this point) -- and then
        // compute this record's own max_encoded_size for whatever depends
        // on *it* later in the loop.
        for (PlannedRecord& planned_record : planned_records) {
            for (PlannedField& planned_field : planned_record.fields) {
                if (planned_field.encoding.is_record) {
                    planned_field.encoding.record_max_encoded_size =
                        record_catalog.at(planned_field.encoding.record_target_ir_id)
                            .max_encoded_size;
                } else if (planned_field.encoding.is_array &&
                          planned_field.encoding.array_element_is_record) {
                    // Only array_scratch_capacity is set here, not
                    // record_max_encoded_size: every read site of
                    // record_max_encoded_size (compute_record_max_encoded_size,
                    // render_field_scratch_declarations) is gated behind
                    // is_record, which is always false for an array field
                    // (is_array is set instead) -- array_scratch_capacity
                    // is the sole array-field equivalent, already
                    // accounting for the element's own max_encoded_size in
                    // its own formula below.
                    const std::uint64_t element_max_encoded_size =
                        record_catalog.at(planned_field.encoding.record_target_ir_id)
                            .max_encoded_size;
                    planned_field.encoding.array_scratch_capacity =
                        static_cast<std::uint64_t>(kMaxVaruintBytesForUint32) +
                        static_cast<std::uint64_t>(planned_field.encoding.array_max_elements) *
                            (kMaxVaruintBytesForUint64 + element_max_encoded_size);
                }
            }
            record_catalog.at(planned_record.ir_id).max_encoded_size =
                compute_record_max_encoded_size(planned_record.fields);
        }

        file.records = std::move(planned_records);
        files.push_back(std::move(file));
    }

    for (const NamespaceIR& child : ns.namespaces()) {
        if (!collect_namespace_files(child, options, catalog, record_catalog, layout_model,
                                     output_selection,
                                     files,
                                     error_message)) {
            return false;
        }
    }
    return true;
}

// Resolve nested-record scratch sizes after every namespace has been lowered.
// The old same-namespace path happened to resolve these while recursively
// collecting a namespace. Cross-namespace fields can point to a namespace
// collected later, so use the already compiler-resolved record ids and a
// small deterministic DFS over the generated record plans. This is sizing
// metadata only; declaration and codec lowering remain unchanged.
[[nodiscard]] bool resolve_record_sizes(
    std::uint64_t record_id, const std::map<std::uint64_t, PlannedRecord*>& records,
    RecordCatalog& record_catalog, std::map<std::uint64_t, unsigned char>& state,
    std::string& error_message) {
    const auto record_it = records.find(record_id);
    if (record_it == records.end()) {
        error_message = "backend_c: could not resolve record sizing dependency " +
                        std::to_string(record_id);
        return false;
    }
    const unsigned char current_state = state[record_id];
    if (current_state == 2U) {
        return true;
    }
    if (current_state == 1U) {
        error_message = "backend_c: detected a recursive record dependency while sizing " +
                        std::to_string(record_id) +
                        "; recursive by-value record graphs are unsupported";
        return false;
    }
    state[record_id] = 1U;
    PlannedRecord& record = *record_it->second;
    for (PlannedField& field : record.fields) {
        const bool nested_record = field.encoding.is_record ||
                                   field.encoding.array_element_is_record;
        if (!nested_record) {
            continue;
        }
        const std::uint64_t target_id = field.encoding.record_target_ir_id;
        if (!resolve_record_sizes(target_id, records, record_catalog, state, error_message)) {
            return false;
        }
        const std::uint64_t target_size = record_catalog.at(target_id).max_encoded_size;
        if (field.encoding.is_record) {
            field.encoding.record_max_encoded_size = target_size;
        } else {
            field.encoding.array_scratch_capacity =
                static_cast<std::uint64_t>(kMaxVaruintBytesForUint32) +
                static_cast<std::uint64_t>(field.encoding.array_max_elements) *
                    (kMaxVaruintBytesForUint64 + target_size);
        }
    }
    record_catalog.at(record_id).max_encoded_size =
        compute_record_max_encoded_size(record.fields);
    state[record_id] = 2U;
    return true;
}

// Single source of truth for file planning, shared by plan() and generate()
// so the two modes cannot diverge -- the same discipline
// compiler/backend/backend.cpp already documents for the C++ backend.
[[nodiscard]] bool build_generation_plan(const schema_ir::SchemaIrModel& schema_ir,
                                         const CodegenOptions& options,
                                         const output_planning::OutputPlan* output_plan,
                                         std::vector<PlannedNamespaceFile>& files,
                                         std::string& error_message) {
    OutputSelection output_selection;
    if (output_plan != nullptr) {
        for (const output_planning::PlannedSourceUnit& unit : output_plan->units) {
            output_selection.planned_namespaces.insert(unit.namespace_fqn);
            if (unit.emits_output) {
                output_selection.emitting_namespaces.insert(unit.namespace_fqn);
            }
        }
    }
    const OutputSelection* output_selection_ptr = output_plan == nullptr ? nullptr : &output_selection;
    EnumCatalog catalog;
    if (!collect_enum_catalog(schema_ir.root_namespace(), catalog, error_message)) {
        return false;
    }
    RecordCatalog record_catalog;
    collect_record_catalog(schema_ir.root_namespace(), record_catalog);
    ::quarry::compiler::diagnostics::DiagnosticEngine layout_diagnostics;
    ::quarry::compiler::layout::LayoutComputer layout_computer;
    const LayoutModel layout_model = layout_computer.compute(schema_ir, layout_diagnostics);
    if (layout_diagnostics.has_errors()) {
        error_message = "backend_c: BRF v2 layout computation failed";
        if (!layout_diagnostics.diagnostics().empty()) {
            error_message += ": " + layout_diagnostics.diagnostics().front().message();
            if (error_message.find("recursive record") != std::string::npos) {
                error_message += " (cycle in by-value record dependency)";
            }
        }
        // Preserve the C backend's established, field-specific diagnostic
        // for negative enum values even though the canonical layout pass
        // rejects them before planning can begin.
        std::vector<const NamespaceIR*> namespaces{&schema_ir.root_namespace()};
        while (!namespaces.empty()) {
            const NamespaceIR* current = namespaces.back();
            namespaces.pop_back();
            for (const RecordIR& record_ir : current->records()) {
                for (const FieldIR& field_ir : record_ir.fields()) {
                    const FieldType* enum_type = nullptr;
                    if (field_ir.type().kind_case() == FieldType::kEnumType) {
                        enum_type = &field_ir.type();
                    } else if (field_ir.type().kind_case() == FieldType::kArray &&
                               field_ir.type().array().element_type().kind_case() ==
                                   FieldType::kEnumType) {
                        enum_type = &field_ir.type().array().element_type();
                    }
                    if (enum_type == nullptr) continue;
                    const auto enum_it = catalog.find(
                        enum_type->enum_type().target_enum_ir_id());
                    if (enum_it != catalog.end() && !enum_it->second.all_non_negative) {
                        error_message = "backend_c: field '" + record_ir.fqn() + "." +
                                        field_ir.name() + "' references an enum with a negative "
                                        "declared value; enum fields are only supported when "
                                        "every declared value is non-negative";
                        if (field_ir.type().kind_case() == FieldType::kArray) {
                            error_message += " (array element type)";
                        }
                        return false;
                    }
                }
            }
            for (const NamespaceIR& child : current->namespaces()) namespaces.push_back(&child);
        }
        return false;
    }
    if (!collect_namespace_files(schema_ir.root_namespace(), options, catalog, record_catalog,
                                 layout_model,
                                 output_selection_ptr, files, error_message)) {
        return false;
    }

    std::map<std::uint64_t, PlannedRecord*> planned_records;
    for (PlannedNamespaceFile& file : files) {
        for (PlannedRecord& record : file.records) {
            planned_records.emplace(record.ir_id, &record);
        }
    }
    std::map<std::uint64_t, unsigned char> states;
    for (const auto& [record_id, record] : planned_records) {
        (void)record;
        if (!resolve_record_sizes(record_id, planned_records, record_catalog, states,
                                  error_message)) {
            return false;
        }
    }

    std::set<std::string> seen_paths;
    std::set<std::string> seen_guards;
    std::set<std::string> seen_file_scope_identifiers;
    const auto reserve_file_scope_identifier = [&](const std::string& identifier,
                                                   std::string_view category) {
        if (!seen_file_scope_identifiers.insert(identifier).second) {
            error_message = "backend_c: generated " + std::string(category) + " '" +
                            identifier + "' collides with another generated C identifier";
            return false;
        }
        return true;
    };
    for (const PlannedNamespaceFile& file : files) {
        if (!seen_paths.insert(file.relative_header_path).second) {
            error_message = "backend_c: duplicate generated header path: " +
                            file.relative_header_path;
            return false;
        }
        if (!seen_paths.insert(file.relative_source_path).second) {
            error_message = "backend_c: duplicate generated source path: " +
                            file.relative_source_path;
            return false;
        }
        const std::string guard = header_guard_macro(file.relative_header_path);
        if (!seen_guards.insert(guard).second) {
            error_message = "backend_c: generated header guard '" + guard +
                            "' collides after path normalization";
            return false;
        }
        for (const PlannedEnum& planned_enum : file.enums) {
            if (!reserve_file_scope_identifier(planned_enum.type_name, "enum type")) {
                return false;
            }
            for (const PlannedEnumValue& value : planned_enum.values) {
                if (!reserve_file_scope_identifier(value.c_name, "enum value")) {
                    return false;
                }
            }
        }
        for (const PlannedRecord& record : file.records) {
            const std::string type_name = record.symbol_name + "_t";
            if (!reserve_file_scope_identifier(type_name, "record type") ||
                !reserve_file_scope_identifier(record.symbol_name + "_init", "function") ||
                !reserve_file_scope_identifier(record.symbol_name + "_encoded_size",
                                               "function") ||
                !reserve_file_scope_identifier(record.symbol_name + "_encode_result_t",
                                               "encode result type") ||
                !reserve_file_scope_identifier(record.symbol_name + "_encode", "function") ||
                !reserve_file_scope_identifier(record.symbol_name + "_decode_result_t",
                                               "decode result type") ||
                !reserve_file_scope_identifier(record.symbol_name + "_decode", "function")) {
                return false;
            }
            for (const std::string_view suffix :
                 {"_encoded_size_result_t", "_type", "_members", "_brf_v2_fields", "_brf_v2_layout", "_encode_with_record_id",
                  "_decode_with_record_id"}) {
                if (!reserve_file_scope_identifier(record.symbol_name + std::string(suffix),
                                                   "binding symbol")) {
                    return false;
                }
            }
            for (const PlannedField& field : record.fields) {
                if (field.encoding.is_enum) {
                    const std::string base =
                        record.symbol_name + "_field_" + std::to_string(field.field_index);
                    for (const std::string_view suffix :
                         {"_enum_values", "_enum_load", "_enum_store"}) {
                        if (!reserve_file_scope_identifier(base + std::string(suffix),
                                                           "enum binding symbol"))
                            return false;
                    }
                }
                if (field.encoding.is_array &&
                    (field.encoding.is_string || field.encoding.is_bytes) &&
                    !reserve_file_scope_identifier(field.encoding.c_type, "array element type")) {
                    return false;
                }
            }
        }
    }
    return true;
}

// --- Rendering ------------------------------------------------------------

// Each generated binding describes host layout; serialization lives in runtime_c.
void render_binding(std::ostringstream& stream, const PlannedRecord& record) {
    if (record.fields.empty()) {
        stream << "static const quarry_c_brf_v2_record_layout_t " << record.symbol_name
               << "_brf_v2_layout = { " << record.record_id << "U, " << record.presence_bitmap_size
               << "U, " << record.fixed_region_size << "U, 0U, NULL };\n";
    } else {
        stream << "static const quarry_c_brf_v2_field_layout_t " << record.symbol_name
               << "_brf_v2_fields[] = {\n";
        for (const PlannedField& field : record.fields) {
            const char* storage = "QUARRY_C_BRF_V2_STORAGE_FIXED";
            if (field.storage == FieldStorage::InlineFixedNestedRecord) {
                storage = "QUARRY_C_BRF_V2_STORAGE_INLINE_FIXED_NESTED";
            } else if (field.storage == FieldStorage::VariableDescriptor) {
                storage = "QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR";
            }
            stream << "    { " << field.field_index << "U, " << field.presence_bit_index << "U, "
                   << field.byte_offset << "U, 0U, " << field.encoding.width_bytes * 8U << "U, "
                   << field.slot_size << "U, " << storage << " },\n";
        }
        stream << "};\n";
        stream << "static const quarry_c_brf_v2_record_layout_t " << record.symbol_name
               << "_brf_v2_layout = { " << record.record_id << "U, " << record.presence_bitmap_size
               << "U, " << record.fixed_region_size << "U, " << record.fields.size() << "U, "
               << record.symbol_name << "_brf_v2_fields };\n";
    }

    for (const PlannedField& field : record.fields) {
        if (!field.encoding.is_enum)
            continue;
        const std::string name = record.symbol_name + "_field_" + std::to_string(field.field_index);
        stream << "static const uint64_t " << name << "_enum_values[] = {";
        for (const auto value : field.encoding.enum_valid_values)
            stream << value << "ULL, ";
        stream << "};\n";
        stream << "static inline uint64_t " << name << "_enum_load(const void* p) { "
               << field.encoding.c_type << " v; memcpy(&v, p, sizeof(v)); return (uint64_t)v; }\n";
        stream << "static inline void " << name << "_enum_store(void* p, uint64_t n) { "
               << field.encoding.c_type << " v = (" << field.encoding.c_type
               << ")n; memcpy(p, &v, sizeof(v)); }\n";
    }
    if (!record.fields.empty()) {
        stream << "static const quarry_c_member_descriptor_t " << record.symbol_name
               << "_members[] = {\n";
        for (const PlannedField& field : record.fields) {
            const auto& e = field.encoding;
            const std::string type = record.symbol_name + "_t";
            const std::string member = "offsetof(" + type + ", " + field.name;
            std::string kind = e.runtime_verb;
            std::transform(kind.begin(), kind.end(), kind.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (e.is_enum)
                kind = "ENUM";
            if (e.is_string)
                kind = "STRING";
            if (e.is_bytes)
                kind = "BYTES";
            if (e.is_record || e.array_element_is_record)
                kind = "RECORD";
            const std::string name =
                record.symbol_name + "_field_" + std::to_string(field.field_index);
            stream << "    { offsetof(" << type << ", has_" << field.name << "), " << member
                   << "), ";
            stream << (e.is_array ? member + "_count)" : "0U") << ", " << e.array_max_elements
                   << "U, { QUARRY_C_TYPED_" << kind << ", ";
            stream << "sizeof(((" << type << "*)0)->" << field.name << (e.is_array ? "[0]" : "")
                   << "), ";
            if (e.is_string || e.is_bytes) {
                if (e.is_array)
                    stream << "offsetof(" << e.c_type << ", length)";
                else
                    stream << member << "_length) - " << member << ")";
            } else
                stream << "0U";
            stream << ", " << (e.is_string ? e.string_max_bytes : e.bytes_max_bytes) << "U, "
                   << static_cast<unsigned>(e.width_bytes) << "U, ";
            if (e.is_record || e.array_element_is_record)
                stream << "&" << e.record_symbol_name << "_type";
            else
                stream << "NULL";
            stream << ", " << e.record_target_id << "U, "
                   << (e.array_element_is_fixed_record ? "true" : "false") << ", ";
            if (e.is_enum)
                stream << name << "_enum_values, " << e.enum_valid_values.size() << "U, " << name
                       << "_enum_load, " << name << "_enum_store";
            else
                stream << "NULL, 0U, NULL, NULL";
            stream << " } },\n";
        }
        stream << "};\n";
    }
    const std::string& n = record.symbol_name;
    stream << "static const quarry_c_type_descriptor_t " << n << "_type = { &" << n
           << "_brf_v2_layout, sizeof(" << n << "_t), "
           << (record.fields.empty() ? "NULL" : n + "_members") << " };\n";
    stream << "static inline void " << n << "_init(" << n
           << "_t* record) { memset(record, 0, sizeof(*record)); }\n";
    stream << "typedef struct { quarry_c_status_t status; size_t size; } " << n
           << "_encoded_size_result_t;\n";
    stream << "static inline " << n << "_encoded_size_result_t " << n
           << "_encoded_size(const " << n << "_t* record) {\n"
           << "    quarry_c_typed_encoded_size_result_t measured = quarry_c_typed_encoded_size(&"
           << n << "_type, record);\n"
           << "    " << n << "_encoded_size_result_t result;\n"
           << "    result.status = (quarry_c_status_t)measured.status; result.size = measured.size;\n"
           << "    return result;\n}\n";
    stream << "typedef struct { quarry_c_status_t status; size_t bytes_written; } " << n << "_encode_result_t;\n";
    stream << "static inline " << n << "_encode_result_t " << n << "_encode_with_record_id(const "
           << n << "_t* record, uint32_t record_id, uint8_t* output, size_t output_capacity) {\n"
           << "    quarry_c_typed_encode_result_t encoded = quarry_c_typed_encode(&" << n << "_type, record, record_id, output, output_capacity);\n"
           << "    " << n << "_encode_result_t result; result.status = (quarry_c_status_t)encoded.status; result.bytes_written = encoded.bytes_written; return result;\n}\n";
    stream << "static inline " << n << "_encode_result_t " << n << "_encode(const " << n
           << "_t* record, uint8_t* output, size_t output_capacity) { return " << n
           << "_encode_with_record_id(record, " << record.record_id
           << "U, output, output_capacity); }\n";
    stream << "typedef struct { quarry_c_status_t status; " << n
           << "_t value; bool has_byte_offset; size_t byte_offset; } " << n
           << "_decode_result_t;\n";
    stream << "static inline " << n << "_decode_result_t " << n
           << "_decode_with_record_id(const uint8_t* input, size_t input_length, uint32_t "
              "expected_record_id) {\n"
           << "    " << n << "_decode_result_t result;\n"
           << "    quarry_c_typed_decode_result_t decoded = quarry_c_typed_decode(&" << n
           << "_type, input, input_length, expected_record_id, &result.value);\n"
           << "    result.status = (quarry_c_status_t)decoded.status; result.has_byte_offset = "
              "decoded.has_byte_offset; result.byte_offset = decoded.byte_offset;\n"
           << "    return result;\n}\n";
    stream << "static inline " << n << "_decode_result_t " << n
           << "_decode(const uint8_t* input, size_t input_length) { return " << n
           << "_decode_with_record_id(input, input_length, " << record.record_id << "U); }\n";
}

[[nodiscard]] std::string render_header(const PlannedNamespaceFile& file) {
    const std::string guard = header_guard_macro(file.relative_header_path);

    std::ostringstream stream;
    stream << "/* Generated by Quarry (C backend). */\n";
    stream << "#ifndef " << guard << "\n";
    stream << "#define " << guard << "\n";
    stream << "\n";
    stream << "#include <stdint.h>\n";
    for (const std::string& include_path : file.generated_includes) {
        stream << "#include \"" << include_path << "\"\n";
    }
    if (!file.records.empty()) {
        stream << "#include <stdbool.h>\n";
        stream << "#include <stddef.h>\n";
        stream << "\n";
        stream << "#include <quarry/runtime_c/typed_record.h>\n";
        stream << "\n";
        stream << "#if QUARRY_C_GENERATED_CODE_API_VERSION != " << kGeneratedCodeApiVersionC
               << "U\n";
        stream << "#error \"Generated Quarry C code is incompatible with the installed Quarry "
                  "C runtime. Regenerate the code using a compatible quarry-schema-compiler "
                  "release.\"\n";
        stream << "#endif\n";
    }
    stream << "\n";
    stream << "#ifdef __cplusplus\n";
    stream << "extern \"C\" {\n";
    stream << "#endif\n";

    for (const PlannedEnum& enum_ir : file.enums) {
        stream << "\n";
        stream << "typedef enum {\n";
        for (const PlannedEnumValue& value : enum_ir.values) {
            stream << "    " << value.c_name << " = " << value.value << ",\n";
        }
        stream << "} " << enum_ir.type_name << ";\n";
    }

    // Variable-width array elements get one named fixed-capacity type per
    // field. The field index makes the generated name independent of the
    // source field spelling and collision-free within a record while keeping
    // the storage layout obvious to C callers.
    for (const PlannedRecord& record : file.records) {
        for (const PlannedField& field : record.fields) {
            if (!field.encoding.is_array ||
                (!field.encoding.is_string && !field.encoding.is_bytes)) {
                continue;
            }
            stream << "\ntypedef struct {\n";
            stream << "    " << (field.encoding.is_string ? "char" : "uint8_t") << " value["
                   << (field.encoding.is_string ? field.encoding.string_max_bytes + 1U
                                                : field.encoding.bytes_max_bytes)
                   << "];\n";
            stream << "    uint32_t length;\n";
            stream << "} " << field.encoding.c_type << ";\n";
        }
    }

    for (const PlannedRecord& record : file.records) {
        stream << "\n";
        stream << "typedef struct {\n";
        if (record.fields.empty()) {
            stream << "    /* No schema fields are declared for this record. This member\n";
            stream << "     * exists only to keep the struct valid ISO C (C forbids an empty\n";
            stream << "     * struct body); it is not a schema field. */\n";
            stream << "    uint8_t reserved;\n";
        } else {
            for (const PlannedField& field : record.fields) {
                stream << "    bool has_" << field.name << ";\n";
                if (field.encoding.is_array) {
                    stream << "    " << field.encoding.c_type << " " << field.name << "["
                           << field.encoding.array_max_elements << "];\n";
                    stream << "    uint32_t " << field.name << "_count;\n";
                } else if (field.encoding.is_string) {
                    // Capacity is max_bytes + 1: room for a trailing NUL
                    // the generated decoder always writes, so decoded
                    // content can be handed to C string APIs directly in
                    // the common case with no embedded NUL. `<field>_length`
                    // is the authoritative byte length (docs/design/
                    // c-backend.md Section 2; compiler/backend_c/README.md's
                    // "String fields" section) -- not necessarily equal to
                    // strlen() if the content has an embedded NUL.
                    stream << "    char " << field.name << "["
                           << (field.encoding.string_max_bytes + 1U) << "];\n";
                    stream << "    uint32_t " << field.name << "_length;\n";
                } else if (field.encoding.is_bytes) {
                    // Capacity is exactly max_bytes: unlike string, there is
                    // no NUL-termination convenience to offer for arbitrary
                    // binary data (compiler/backend_c/README.md's "Bytes
                    // fields" section).
                    stream << "    uint8_t " << field.name << "[" << field.encoding.bytes_max_bytes
                           << "];\n";
                    stream << "    uint32_t " << field.name << "_length;\n";
                } else {
                    stream << "    " << field.encoding.c_type << " " << field.name << ";\n";
                }
            }
        }
        stream << "} " << record.symbol_name << "_t;\n";

        render_binding(stream, record);
    }

    stream << "\n";
    stream << "#ifdef __cplusplus\n";
    stream << "}\n";
    stream << "#endif\n";
    stream << "\n";
    stream << "#endif /* " << guard << " */\n";
    return stream.str();
}

} // namespace

std::string output_path_for_planned_file(const CodegenOptions& options,
                                         std::string_view relative_path) {
    if (options.output_directory.empty()) {
        return std::string(relative_path);
    }
    return options.output_directory + "/" + std::string(relative_path);
}

PlanResult Backend::plan(const schema_ir::SchemaIrModel& schema_ir, const CodegenOptions& options,
                         const output_planning::OutputPlan* output_plan) const {
    PlanResult result;

    std::vector<PlannedNamespaceFile> files;
    std::string error_message;
    if (!build_generation_plan(schema_ir, options, output_plan, files, error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    result.plan.files.reserve(files.size());
    for (const PlannedNamespaceFile& file : files) {
        if (!file.emits_output) {
            continue;
        }
        result.plan.files.push_back(PlannedGeneratedFile{
            .relative_header_path = file.relative_header_path,
            .relative_source_path = file.relative_source_path,
            .generated_include_path = file.generated_include_path,
        });
    }
    return result;
}

CodegenResult Backend::generate(const schema_ir::SchemaIrModel& schema_ir,
                                const CodegenOptions& options,
                                const output_planning::OutputPlan* output_plan) const {
    CodegenResult result;

    std::vector<PlannedNamespaceFile> files;
    std::string error_message;
    if (!build_generation_plan(schema_ir, options, output_plan, files, error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    result.files.reserve(files.size() * 2U);
    for (const PlannedNamespaceFile& file : files) {
        if (!file.emits_output) {
            continue;
        }
        result.files.push_back(GeneratedFile{
            .path = output_path_for_planned_file(options, file.relative_header_path),
            .content = render_header(file),
        });
        result.files.push_back(GeneratedFile{
            .path = output_path_for_planned_file(options, file.relative_source_path),
            .content = "/* Compatibility translation unit; only the generated header is required. "
                       "*/\n#include \"" +
                       file.generated_include_path + "\"\n",
        });
    }
    return result;
}

} // namespace quarry::compiler::backend_c
