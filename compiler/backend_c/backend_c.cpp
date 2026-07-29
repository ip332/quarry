#include "compiler/backend_c/backend_c.hpp"
#include "compiler/backend_c/generated_code_api_version_c.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
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

[[nodiscard]] std::string header_guard_macro(std::string_view relative_header_path) {
    return "QUARRY_GENERATED_C_" + uppercase_alnum_or_underscore(relative_header_path) + "_";
}

// --- Field type lowering --------------------------------------------------
//
// PR-108 scope was scalar primitive fields only. PR-109 added enum fields,
// restricted to enums declared in the *same* namespace as the referencing
// record (see collect_enum_catalog below) and whose declared values are all
// non-negative -- matching the C++ backend's own field-support boundary
// exactly (compiler/backend/backend.cpp's runtime_enum_encoding rejects any
// enum with a negative value as a field type; the BRF spec's "Enum
// Encoding" section states the same constraint). PR-110 added bounded
// (fixed-capacity) string fields (see "String fields" below). Cross-
// namespace enum field references, bytes, arrays, and nested/record-
// reference fields remain unsupported; no new schema types are invented
// here.
//
// --- String fields ---
//
// A string field's Schema IR type carries a schema-declared, semantic-
// validator-enforced positive max_bytes bound (compiler/semantic/
// semantic.cpp's validate_positive_u32; StringType.max_bytes in
// schema_ir.proto) -- by the time backend_c sees a string field, max_bytes
// is always > 0 and always fits uint32_t. This backend generates
// fixed-capacity, caller-owned storage sized directly from that bound:
// `char <field>[max_bytes + 1]` (the "+1" reserves room for a trailing NUL
// terminator the generated decoder always writes, so decoded content can be
// handed to C string APIs directly in the common case that has no embedded
// NUL) plus an explicit `uint32_t <field>_length` byte-length member (the
// wire's actual field length, per docs/specifications/binary-record-
// format.md's "string" section: "Embedded U+0000 is valid string data", so
// `<field>_length` is authoritative -- it is not necessarily equal to
// `strlen(<field>)` if the content contains an embedded NUL). This is
// exactly docs/design/c-backend.md Section 2's investigated "Strings"
// recommendation, now implemented rather than proposed. String content is
// validated as UTF-8 on both encode and decode
// (quarry_c_is_valid_utf8, include/quarry/runtime_c/binary_record.h),
// matching the C++ backend and the BRF spec's "String data bytes SHALL be
// valid UTF-8" requirement exactly -- see compiler/backend_c/README.md's
// "String fields" section for the full rationale (representation
// alternatives considered and rejected, NUL-termination policy, empty-vs-
// absent semantics, decode commit-on-success ordering).

struct FieldEncoding {
    std::string c_type;       // e.g. "int32_t", "float", "bool", or an enum's own typedef name
    std::string runtime_verb; // e.g. "i32" -> quarry_c_write_i32 / quarry_c_read_i32
    std::uint8_t width_bytes = 0U;
    bool is_enum = false;
    std::vector<std::int64_t> enum_valid_values; // only meaningful when is_enum
    bool is_string = false;
    std::uint64_t string_max_bytes = 0U; // only meaningful when is_string; widened to
                                         // std::uint64_t so max_bytes + 1 cannot overflow
                                         // while computing the generated buffer's capacity,
                                         // even for a (pathological) max_bytes == UINT32_MAX
};

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

[[nodiscard]] std::string unsigned_c_type_for_width(std::uint8_t width_bytes) {
    switch (width_bytes) {
    case 1U:
        return "uint8_t";
    case 2U:
        return "uint16_t";
    case 4U:
        return "uint32_t";
    default:
        return "uint64_t";
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
        entry.type_name = symbol_prefix + enum_ir.name() + "_t";
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
    std::int64_t value = 0;
};

struct PlannedEnum {
    std::string type_name;
    std::string symbol_name;
    std::vector<PlannedEnumValue> values;
};

struct PlannedField {
    std::string name;
    std::uint32_t field_index = 0U;
    FieldEncoding encoding;
};

struct PlannedRecord {
    std::string symbol_name;
    std::uint32_t record_id = 0U;
    std::vector<PlannedField> fields;
};

struct PlannedNamespaceFile {
    std::string relative_header_path;
    std::string relative_source_path;
    std::string generated_include_path;
    std::vector<PlannedEnum> enums;
    std::vector<PlannedRecord> records;
};

[[nodiscard]] bool namespace_emits_file(const NamespaceIR& ns) {
    return ns.records_size() > 0 || ns.enums_size() > 0;
}

// Resolves one field's type, given the whole-schema enum catalog and the
// FQN of the namespace the referencing record belongs to. Returns
// std::nullopt (with error_message set) for every unsupported case: a
// non-scalar, non-enum type; an enum declared in a different namespace; or
// an enum with a negative declared value.
[[nodiscard]] std::optional<FieldEncoding>
lower_field_encoding(const RecordIR& record_ir, const FieldIR& field_ir,
                     std::string_view current_namespace_fqn, const EnumCatalog& catalog,
                     std::string& error_message) {
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
    } else if (type.kind_case() == FieldType::kEnumType) {
        const auto catalog_it = catalog.find(type.enum_type().target_enum_ir_id());
        if (catalog_it != catalog.end()) {
            const EnumCatalogEntry& entry = catalog_it->second;
            if (entry.owning_namespace_fqn != current_namespace_fqn) {
                std::ostringstream stream;
                stream << "backend_c: field '" << record_ir.fqn() << "." << field_ir.name()
                       << "' references enum '" << entry.type_name
                       << "' declared in a different namespace ('" << entry.owning_namespace_fqn
                       << "'); cross-namespace enum field references are not yet supported "
                          "(see docs/design/c-backend.md)";
                error_message = stream.str();
                return std::nullopt;
            }
            if (!entry.all_non_negative) {
                std::ostringstream stream;
                stream << "backend_c: field '" << record_ir.fqn() << "." << field_ir.name()
                       << "' references an enum with a negative declared value; enum fields "
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
    }

    // Mixed supported/unsupported records fail as a whole: a struct that
    // silently dropped this field would be partial, misleading output, not
    // a smaller feature set.
    std::ostringstream stream;
    stream << "backend_c: field '" << record_ir.fqn() << "." << field_ir.name()
           << "' has a type the C backend does not support yet -- only bool, fixed-width "
              "signed/unsigned integer, f32/f64 scalar fields, same-namespace enum fields "
              "with only non-negative declared values, and bounded string fields are "
              "supported (see docs/design/c-backend.md); bytes, array, and record-reference "
              "fields remain unsupported";
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
                                           std::vector<PlannedNamespaceFile>& files,
                                           std::string& error_message) {
    if (namespace_emits_file(ns)) {
        PlannedNamespaceFile file;
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
                planned_enum.values.push_back(PlannedEnumValue{
                    .name = uppercase_alnum_or_underscore(value_ir.name()),
                    .value = value_ir.value()});
            }
            file.enums.push_back(std::move(planned_enum));
        }

        const std::string symbol_prefix = symbol_prefix_for_namespace(ns.fqn());
        for (const RecordIR& record_ir : ns.records()) {
            std::vector<PlannedField> planned_fields;
            planned_fields.reserve(static_cast<std::size_t>(record_ir.fields_size()));
            for (const FieldIR& field_ir : record_ir.fields()) {
                std::optional<FieldEncoding> encoding =
                    lower_field_encoding(record_ir, field_ir, ns.fqn(), catalog, error_message);
                if (!encoding.has_value()) {
                    return false;
                }
                planned_fields.push_back(PlannedField{
                    .name = field_ir.name(),
                    .field_index = field_ir.field_index(),
                    .encoding = std::move(*encoding),
                });
            }
            file.records.push_back(PlannedRecord{
                .symbol_name = symbol_prefix + record_ir.name(),
                .record_id = record_ir.record_id(),
                .fields = std::move(planned_fields),
            });
        }

        files.push_back(std::move(file));
    }

    for (const NamespaceIR& child : ns.namespaces()) {
        if (!collect_namespace_files(child, options, catalog, files, error_message)) {
            return false;
        }
    }
    return true;
}

// Single source of truth for file planning, shared by plan() and generate()
// so the two modes cannot diverge -- the same discipline
// compiler/backend/backend.cpp already documents for the C++ backend.
[[nodiscard]] bool build_generation_plan(const schema_ir::SchemaIrModel& schema_ir,
                                         const CodegenOptions& options,
                                         std::vector<PlannedNamespaceFile>& files,
                                         std::string& error_message) {
    EnumCatalog catalog;
    if (!collect_enum_catalog(schema_ir.root_namespace(), catalog, error_message)) {
        return false;
    }
    if (!collect_namespace_files(schema_ir.root_namespace(), options, catalog, files,
                                 error_message)) {
        return false;
    }

    std::set<std::string> seen_paths;
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
    }
    return true;
}

// --- Rendering ------------------------------------------------------------

[[nodiscard]] std::string render_header(const PlannedNamespaceFile& file) {
    const std::string guard = header_guard_macro(file.relative_header_path);

    std::ostringstream stream;
    stream << "/* Generated by Quarry (C backend -- scalar codec slice, PR-108). */\n";
    stream << "#ifndef " << guard << "\n";
    stream << "#define " << guard << "\n";
    stream << "\n";
    stream << "#include <stdint.h>\n";
    if (!file.records.empty()) {
        stream << "#include <stdbool.h>\n";
        stream << "#include <stddef.h>\n";
        stream << "\n";
        stream << "#include <quarry/runtime_c/binary_record.h>\n";
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
            stream << "    " << enum_ir.symbol_name << "_" << value.name << " = " << value.value
                   << ",\n";
        }
        stream << "} " << enum_ir.type_name << ";\n";
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
                if (field.encoding.is_string) {
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
                } else {
                    stream << "    " << field.encoding.c_type << " " << field.name << ";\n";
                }
            }
        }
        stream << "} " << record.symbol_name << "_t;\n";

        stream << "\n";
        stream << "void " << record.symbol_name << "_init(" << record.symbol_name
               << "_t* record);\n";

        stream << "\n";
        stream << "size_t " << record.symbol_name << "_encoded_size(const "
               << record.symbol_name << "_t* record);\n";

        stream << "\n";
        stream << "typedef struct {\n";
        stream << "    quarry_c_status_t status;\n";
        stream << "    size_t bytes_written;\n";
        stream << "} " << record.symbol_name << "_encode_result_t;\n";

        stream << "\n";
        stream << record.symbol_name << "_encode_result_t " << record.symbol_name
               << "_encode(const " << record.symbol_name
               << "_t* record, uint8_t* output, size_t output_capacity);\n";

        stream << "\n";
        stream << "typedef struct {\n";
        stream << "    quarry_c_status_t status;\n";
        stream << "    " << record.symbol_name << "_t value;\n";
        stream << "    bool has_byte_offset;\n";
        stream << "    size_t byte_offset;\n";
        stream << "} " << record.symbol_name << "_decode_result_t;\n";

        stream << "\n";
        stream << record.symbol_name << "_decode_result_t " << record.symbol_name
               << "_decode(const uint8_t* input, size_t input_length);\n";
    }

    stream << "\n";
    stream << "#ifdef __cplusplus\n";
    stream << "}\n";
    stream << "#endif\n";
    stream << "\n";
    stream << "#endif /* " << guard << " */\n";
    return stream.str();
}

// Declares one `uint8_t <field.name>_bytes[width];` scratch buffer per
// fixed-width (scalar/enum) field, used to hold one field's encoded bytes
// before it is added to the quarry_c_field_t array passed to the runtime.
// String fields need no scratch buffer: their wire bytes are exactly the
// record's own `<field>` storage (already raw UTF-8, no big-endian
// transformation needed), so the field-building loop below points the
// quarry_c_field_t entry directly at `record-><field>` instead. Shared by
// _encoded_size and _encode, which otherwise independently render their own
// field-building loop (a small, deliberate duplication -- see
// compiler/backend_c/README.md).
void render_field_scratch_declarations(std::ostringstream& stream,
                                       const std::vector<PlannedField>& fields) {
    for (const PlannedField& field : fields) {
        if (field.encoding.is_string) {
            continue;
        }
        stream << "    uint8_t " << field.name << "_bytes["
               << static_cast<unsigned int>(field.encoding.width_bytes) << "];\n";
    }
}

// Renders `expr == V0 || expr == V1 || ...` over an enum's declared values,
// matching the C++ backend's generated membership check exactly (see e.g.
// tests/fixtures/backend/enum_reference.txt's `enum_numeric == 1 ||
// enum_numeric == 2`) -- raw numeric literals, not the enumerator names, so
// this needs only the value list, not a name lookup.
[[nodiscard]] std::string render_enum_membership_condition(const std::string& expression,
                                                            const std::vector<std::int64_t>& values) {
    std::string condition;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            condition += " || ";
        }
        condition += expression + " == " + std::to_string(values[index]);
    }
    return condition;
}

// Renders one string field's contribution to the fields[]/field_count
// array-building loop. Order matches the C++ backend's
// render_string_field_encoding exactly: bounds check (declared length vs.
// schema max_bytes) first, then UTF-8 validation -- both using
// record-><field>_length directly, *before* anything reads record-><field>
// content, so an out-of-range length can never cause an out-of-bounds read
// (record-><field>'s actual storage is only max_bytes + 1 bytes). No
// scratch buffer: record-><field> already holds exactly the wire bytes.
void render_string_field_build(std::ostringstream& stream, const PlannedField& field,
                               bool check_write_status) {
    if (check_write_status) {
        // _encoded_size() does not validate bounds/UTF-8, matching the
        // enum-membership precedent above: encoded size is read directly
        // from the field's current length regardless of validity; the real
        // _encode() call below is where an invalid value is rejected.
        stream << "        if (record->" << field.name << "_length > "
               << field.encoding.string_max_bytes << "U) {\n";
        stream << "            result.status = QUARRY_C_STATUS_BOUNDS_EXCEEDED;\n";
        stream << "            return result;\n";
        stream << "        }\n";
        stream << "        if (!quarry_c_is_valid_utf8((const uint8_t*)record->" << field.name
               << ", record->" << field.name << "_length)) {\n";
        stream << "            result.status = QUARRY_C_STATUS_INVALID_UTF8;\n";
        stream << "            return result;\n";
        stream << "        }\n";
    }
    stream << "        fields[field_count].field_index = " << field.field_index << "U;\n";
    stream << "        fields[field_count].bytes = (const uint8_t*)record->" << field.name
           << ";\n";
    stream << "        fields[field_count].length = record->" << field.name << "_length;\n";
    stream << "        field_count += 1U;\n";
}

void render_scalar_or_enum_field_build(std::ostringstream& stream, const PlannedField& field,
                                       bool check_write_status) {
    if (field.encoding.is_enum && check_write_status) {
        // _encoded_size() does not validate membership: the encoded
        // size depends only on the field's fixed wire width, not on
        // whether the current value happens to be one of the enum's
        // declared values -- the actual _encode() call below is where
        // an invalid value is rejected.
        stream << "        if (!(" << render_enum_membership_condition(
                                          "record->" + field.name, field.encoding.enum_valid_values)
               << ")) {\n";
        stream << "            result.status = QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE;\n";
        stream << "            return result;\n";
        stream << "        }\n";
    }
    stream << "        quarry_c_writer_t writer;\n";
    stream << "        quarry_c_writer_init(&writer, " << field.name << "_bytes, sizeof("
           << field.name << "_bytes));\n";
    const std::string write_value =
        field.encoding.is_enum
            ? ("(" + unsigned_c_type_for_width(field.encoding.width_bytes) + ")record->" +
              field.name)
            : ("record->" + field.name);
    if (check_write_status) {
        stream << "        const quarry_c_status_t field_status = quarry_c_write_"
               << field.encoding.runtime_verb << "(&writer, " << write_value << ");\n";
        stream << "        if (field_status != QUARRY_C_STATUS_OK) {\n";
        stream << "            result.status = field_status;\n";
        stream << "            return result;\n";
        stream << "        }\n";
    } else {
        stream << "        (void)quarry_c_write_" << field.encoding.runtime_verb << "(&writer, "
               << write_value << ");\n";
    }
    stream << "        fields[field_count].field_index = " << field.field_index << "U;\n";
    stream << "        fields[field_count].bytes = " << field.name << "_bytes;\n";
    stream << "        fields[field_count].length = writer.length;\n";
    stream << "        field_count += 1U;\n";
}

void render_build_fields_loop(std::ostringstream& stream, const std::vector<PlannedField>& fields,
                              bool check_write_status) {
    for (const PlannedField& field : fields) {
        stream << "    if (record->has_" << field.name << ") {\n";
        if (field.encoding.is_string) {
            render_string_field_build(stream, field, check_write_status);
        } else {
            render_scalar_or_enum_field_build(stream, field, check_write_status);
        }
        stream << "    }\n";
    }
}

// Renders one scalar/enum field's decode block (fixed wire width, read
// directly -- or via a raw temporary for enums, see PR-109 -- into the
// result). Caller has already opened the `if (field_found) {` block and
// renders the trailing `result.value.has_<field> = true;` itself, shared
// with the string decode path below.
void render_scalar_or_enum_field_decode(std::ostringstream& stream, const PlannedField& field) {
    stream << "            if (field_view.length != "
           << static_cast<unsigned int>(field.encoding.width_bytes) << "U) {\n";
    stream << "                result.status = QUARRY_C_STATUS_INVALID_FIELD_LENGTH;\n";
    stream << "                result.has_byte_offset = true;\n";
    stream << "                result.byte_offset = field_view.byte_offset;\n";
    stream << "                return result;\n";
    stream << "            }\n";
    stream << "            quarry_c_reader_t field_reader;\n";
    stream << "            quarry_c_reader_init(&field_reader, field_view.bytes, "
              "field_view.length);\n";
    if (field.encoding.is_enum) {
        const std::string raw_name = field.name + "_raw";
        stream << "            " << unsigned_c_type_for_width(field.encoding.width_bytes) << " "
               << raw_name << " = 0;\n";
        stream << "            const quarry_c_status_t field_status = quarry_c_read_"
               << field.encoding.runtime_verb << "(&field_reader, &" << raw_name << ");\n";
        stream << "            if (field_status != QUARRY_C_STATUS_OK) {\n";
        stream << "                result.status = field_status;\n";
        stream << "                result.has_byte_offset = true;\n";
        stream << "                result.byte_offset = field_view.byte_offset;\n";
        stream << "                return result;\n";
        stream << "            }\n";
        stream << "            if (!("
               << render_enum_membership_condition(raw_name, field.encoding.enum_valid_values)
               << ")) {\n";
        stream << "                result.status = QUARRY_C_STATUS_UNKNOWN_ENUM_VALUE;\n";
        stream << "                result.has_byte_offset = true;\n";
        stream << "                result.byte_offset = field_view.byte_offset;\n";
        stream << "                return result;\n";
        stream << "            }\n";
        stream << "            result.value." << field.name << " = (" << field.encoding.c_type
               << ")" << raw_name << ";\n";
    } else {
        stream << "            const quarry_c_status_t field_status = quarry_c_read_"
               << field.encoding.runtime_verb << "(&field_reader, &result.value." << field.name
               << ");\n";
        stream << "            if (field_status != QUARRY_C_STATUS_OK) {\n";
        stream << "                result.status = field_status;\n";
        stream << "                result.has_byte_offset = true;\n";
        stream << "                result.byte_offset = field_view.byte_offset;\n";
        stream << "                return result;\n";
        stream << "            }\n";
    }
}

// Renders one string field's decode block. Order matches the C++ backend's
// render_string_field_decoding: a bounds check against the field's
// generated capacity (max_bytes) first, then UTF-8 validation, and only
// then is the field committed (NUL-terminated, length set) -- matching
// docs/design/c-backend.md's "decode into a temporary and commit only on
// success" choice (compiler/backend_c/README.md's "String fields" section
// documents this decision in full; the copy itself is safe to perform
// before the UTF-8 check because `result.value` is documented as "only
// meaningful when status == OK", so an early return on invalid-UTF-8 after
// the copy but before has_<field> is set never exposes unvalidated content
// as if it were a successful decode). quarry_c_copy_bounded folds the
// bounds check and the copy into one runtime call, so generated code never
// contains a bare/unchecked memcpy.
void render_string_field_decode(std::ostringstream& stream, const PlannedField& field) {
    stream << "            const quarry_c_status_t field_status = quarry_c_copy_bounded(\n";
    stream << "                (uint8_t*)result.value." << field.name << ", "
           << field.encoding.string_max_bytes << "U, field_view.bytes, field_view.length);\n";
    stream << "            if (field_status != QUARRY_C_STATUS_OK) {\n";
    stream << "                result.status = field_status;\n";
    stream << "                result.has_byte_offset = true;\n";
    stream << "                result.byte_offset = field_view.byte_offset;\n";
    stream << "                return result;\n";
    stream << "            }\n";
    stream << "            if (!quarry_c_is_valid_utf8(field_view.bytes, field_view.length)) {\n";
    stream << "                result.status = QUARRY_C_STATUS_INVALID_UTF8;\n";
    stream << "                result.has_byte_offset = true;\n";
    stream << "                result.byte_offset = field_view.byte_offset;\n";
    stream << "                return result;\n";
    stream << "            }\n";
    stream << "            result.value." << field.name << "[field_view.length] = '\\0';\n";
    stream << "            result.value." << field.name
           << "_length = (uint32_t)field_view.length;\n";
}

[[nodiscard]] std::string render_source(const PlannedNamespaceFile& file) {
    std::ostringstream stream;
    stream << "/* Generated by Quarry (C backend -- scalar codec slice, PR-108). */\n";
    stream << "#include \"" << file.generated_include_path << "\"\n";

    if (!file.records.empty()) {
        stream << "\n";
        stream << "#include <string.h>\n";
    }

    for (const PlannedRecord& record : file.records) {
        stream << "\n";
        stream << "void " << record.symbol_name << "_init(" << record.symbol_name
               << "_t* record) {\n";
        stream << "    memset(record, 0, sizeof(*record));\n";
        stream << "}\n";

        stream << "\n";
        stream << "size_t " << record.symbol_name << "_encoded_size(const "
               << record.symbol_name << "_t* record) {\n";
        if (record.fields.empty()) {
            stream << "    (void)record;\n";
            stream << "    size_t size = 0U;\n";
            stream << "    (void)quarry_c_record_encoded_size(NULL, 0U, &size);\n";
            stream << "    return size;\n";
        } else {
            stream << "    quarry_c_field_t fields[" << record.fields.size() << "];\n";
            stream << "    size_t field_count = 0U;\n";
            render_field_scratch_declarations(stream, record.fields);
            render_build_fields_loop(stream, record.fields, /*check_write_status=*/false);
            stream << "    size_t size = 0U;\n";
            stream << "    (void)quarry_c_record_encoded_size(fields, field_count, &size);\n";
            stream << "    return size;\n";
        }
        stream << "}\n";

        stream << "\n";
        stream << record.symbol_name << "_encode_result_t " << record.symbol_name
               << "_encode(const " << record.symbol_name
               << "_t* record, uint8_t* output, size_t output_capacity) {\n";
        stream << "    " << record.symbol_name << "_encode_result_t result;\n";
        stream << "    result.status = QUARRY_C_STATUS_OK;\n";
        stream << "    result.bytes_written = 0U;\n";
        if (record.fields.empty()) {
            stream << "    (void)record;\n";
            stream << "    result.status = quarry_c_encode_record(" << record.record_id
                   << "U, NULL, 0U, output, output_capacity, &result.bytes_written);\n";
        } else {
            stream << "    quarry_c_field_t fields[" << record.fields.size() << "];\n";
            stream << "    size_t field_count = 0U;\n";
            render_field_scratch_declarations(stream, record.fields);
            render_build_fields_loop(stream, record.fields, /*check_write_status=*/true);
            stream << "    result.status = quarry_c_encode_record(" << record.record_id
                   << "U, fields, field_count, output, output_capacity, "
                      "&result.bytes_written);\n";
        }
        stream << "    return result;\n";
        stream << "}\n";

        stream << "\n";
        stream << record.symbol_name << "_decode_result_t " << record.symbol_name
               << "_decode(const uint8_t* input, size_t input_length) {\n";
        stream << "    " << record.symbol_name << "_decode_result_t result;\n";
        stream << "    result.status = QUARRY_C_STATUS_OK;\n";
        stream << "    result.has_byte_offset = false;\n";
        stream << "    result.byte_offset = 0U;\n";
        stream << "    memset(&result.value, 0, sizeof(result.value));\n";
        stream << "\n";
        stream << "    quarry_c_parsed_record_t parsed;\n";
        stream << "    size_t error_offset = 0U;\n";
        stream << "    const quarry_c_status_t parse_status =\n";
        stream << "        quarry_c_parse_record(input, input_length, &parsed, "
                  "&error_offset);\n";
        stream << "    if (parse_status != QUARRY_C_STATUS_OK) {\n";
        stream << "        result.status = parse_status;\n";
        stream << "        if (parse_status != QUARRY_C_STATUS_UNSUPPORTED_FIELD_COUNT) {\n";
        stream << "            result.has_byte_offset = true;\n";
        stream << "            result.byte_offset = error_offset;\n";
        stream << "        }\n";
        stream << "        return result;\n";
        stream << "    }\n";
        stream << "    if (parsed.record_id != " << record.record_id << "U) {\n";
        stream << "        result.status = QUARRY_C_STATUS_UNEXPECTED_RECORD_ID;\n";
        stream << "        result.has_byte_offset = true;\n";
        stream << "        result.byte_offset = 0U;\n";
        stream << "        return result;\n";
        stream << "    }\n";

        for (const PlannedField& field : record.fields) {
            stream << "\n";
            stream << "    {\n";
            stream << "        quarry_c_field_view_t field_view;\n";
            stream << "        bool field_found = false;\n";
            stream << "        (void)quarry_c_find_field(&parsed, " << field.field_index
                   << "U, &field_view, &field_found);\n";
            stream << "        if (field_found) {\n";
            if (field.encoding.is_string) {
                render_string_field_decode(stream, field);
            } else {
                render_scalar_or_enum_field_decode(stream, field);
            }
            stream << "            result.value.has_" << field.name << " = true;\n";
            stream << "        }\n";
            stream << "    }\n";
        }

        stream << "\n";
        stream << "    return result;\n";
        stream << "}\n";
    }

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

PlanResult Backend::plan(const schema_ir::SchemaIrModel& schema_ir,
                         const CodegenOptions& options) const {
    PlanResult result;

    std::vector<PlannedNamespaceFile> files;
    std::string error_message;
    if (!build_generation_plan(schema_ir, options, files, error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    result.plan.files.reserve(files.size());
    for (const PlannedNamespaceFile& file : files) {
        result.plan.files.push_back(PlannedGeneratedFile{
            .relative_header_path = file.relative_header_path,
            .relative_source_path = file.relative_source_path,
            .generated_include_path = file.generated_include_path,
        });
    }
    return result;
}

CodegenResult Backend::generate(const schema_ir::SchemaIrModel& schema_ir,
                                const CodegenOptions& options) const {
    CodegenResult result;

    std::vector<PlannedNamespaceFile> files;
    std::string error_message;
    if (!build_generation_plan(schema_ir, options, files, error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    result.files.reserve(files.size() * 2U);
    for (const PlannedNamespaceFile& file : files) {
        result.files.push_back(GeneratedFile{
            .path = output_path_for_planned_file(options, file.relative_header_path),
            .content = render_header(file),
        });
        result.files.push_back(GeneratedFile{
            .path = output_path_for_planned_file(options, file.relative_source_path),
            .content = render_source(file),
        });
    }
    return result;
}

} // namespace quarry::compiler::backend_c
