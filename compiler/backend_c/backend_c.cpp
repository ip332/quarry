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

// --- Scalar field type lowering ------------------------------------------
//
// PR-108 scope: scalar primitive fields only (bool, fixed-width signed and
// unsigned integers, f32, f64) -- the exact scalar set Schema IR's
// PrimitiveType enumerates and the C++ backend already supports. Enum
// fields, strings, bytes, arrays, and nested/record-reference fields remain
// unsupported and fail generation with a diagnostic; no new schema types
// are invented here. See compiler/backend_c/README.md and
// docs/design/c-backend.md.

struct ScalarFieldEncoding {
    std::string c_type;       // e.g. "int32_t", "float", "bool"
    std::string runtime_verb; // e.g. "i32" -> quarry_c_write_i32 / quarry_c_read_i32
    std::uint8_t width_bytes;
};

// Returns std::nullopt for any field type this backend does not support yet
// (enum, string, bytes, array, record reference), matching the exact
// scalar/enum-vs-other boundary docs/specifications/binary-record-format.md
// and the C++ backend already draw at the PrimitiveType level.
[[nodiscard]] std::optional<ScalarFieldEncoding> lower_scalar_field_type(const FieldType& type) {
    if (type.kind_case() != FieldType::kPrimitive) {
        return std::nullopt;
    }
    using ::quarry::schema_ir::PrimitiveType;
    switch (type.primitive()) {
    case PrimitiveType::PRIMITIVE_TYPE_BOOL:
        return ScalarFieldEncoding{.c_type = "bool", .runtime_verb = "bool", .width_bytes = 1U};
    case PrimitiveType::PRIMITIVE_TYPE_I8:
        return ScalarFieldEncoding{.c_type = "int8_t", .runtime_verb = "i8", .width_bytes = 1U};
    case PrimitiveType::PRIMITIVE_TYPE_U8:
        return ScalarFieldEncoding{.c_type = "uint8_t", .runtime_verb = "u8", .width_bytes = 1U};
    case PrimitiveType::PRIMITIVE_TYPE_I16:
        return ScalarFieldEncoding{.c_type = "int16_t", .runtime_verb = "i16", .width_bytes = 2U};
    case PrimitiveType::PRIMITIVE_TYPE_U16:
        return ScalarFieldEncoding{.c_type = "uint16_t", .runtime_verb = "u16", .width_bytes = 2U};
    case PrimitiveType::PRIMITIVE_TYPE_I32:
        return ScalarFieldEncoding{.c_type = "int32_t", .runtime_verb = "i32", .width_bytes = 4U};
    case PrimitiveType::PRIMITIVE_TYPE_U32:
        return ScalarFieldEncoding{.c_type = "uint32_t", .runtime_verb = "u32", .width_bytes = 4U};
    case PrimitiveType::PRIMITIVE_TYPE_I64:
        return ScalarFieldEncoding{.c_type = "int64_t", .runtime_verb = "i64", .width_bytes = 8U};
    case PrimitiveType::PRIMITIVE_TYPE_U64:
        return ScalarFieldEncoding{.c_type = "uint64_t", .runtime_verb = "u64", .width_bytes = 8U};
    case PrimitiveType::PRIMITIVE_TYPE_F32:
        return ScalarFieldEncoding{.c_type = "float", .runtime_verb = "f32", .width_bytes = 4U};
    case PrimitiveType::PRIMITIVE_TYPE_F64:
        return ScalarFieldEncoding{.c_type = "double", .runtime_verb = "f64", .width_bytes = 8U};
    case PrimitiveType::PRIMITIVE_TYPE_UNSPECIFIED:
    default:
        return std::nullopt;
    }
}

// --- Planning -----------------------------------------------------------

struct PlannedEnumValue {
    std::string name;
    std::int64_t value = 0;
};

struct PlannedEnum {
    std::string symbol_name;
    std::vector<PlannedEnumValue> values;
};

struct PlannedField {
    std::string name;
    std::uint32_t field_index = 0U;
    ScalarFieldEncoding encoding;
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

// Collects one PlannedNamespaceFile per namespace that directly owns records
// or enums, in Schema IR declaration order (pre-order namespace traversal),
// the same "emit files only for namespaces that directly own records or
// enums" rule compiler/backend/backend.cpp already follows for C++ -- an
// independently reached, not shared, implementation of the same policy.
//
// Deliberately does not attempt to lower any field: PR-107 is an
// architectural skeleton (see compiler/backend_c/README.md and
// docs/design/c-backend.md). A record with one or more fields fails
// generation with a clear diagnostic rather than emitting a struct that
// silently drops schema-declared fields.
[[nodiscard]] bool collect_namespace_files(const NamespaceIR& ns, const CodegenOptions& options,
                                           std::vector<PlannedNamespaceFile>& files,
                                           std::string& error_message) {
    if (namespace_emits_file(ns)) {
        PlannedNamespaceFile file;
        const std::string symbol_prefix = symbol_prefix_for_namespace(ns.fqn());
        const std::string stem = file_stem_for_namespace(options, ns.fqn());
        file.relative_header_path = stem + options.header_extension;
        file.relative_source_path = stem + options.source_extension;
        file.generated_include_path = file.relative_header_path;

        for (const EnumIR& enum_ir : ns.enums()) {
            PlannedEnum planned_enum;
            // Enum value constants use the fully uppercase
            // QUARRY_<NAMESPACE>_<ENUMNAME>_<VALUE> form (docs/design/c-backend.md
            // Section 5): C enumerators are not scoped, so the whole identifier is
            // conventionally upper-cased like a macro constant, unlike the
            // mixed-case quarry_<namespace>_<Record>_t struct/function naming.
            planned_enum.symbol_name = uppercase_alnum_or_underscore(symbol_prefix + enum_ir.name());
            for (const EnumValueIR& value_ir : enum_ir.values()) {
                if (value_ir.value() > std::numeric_limits<std::int32_t>::max() ||
                    value_ir.value() < std::numeric_limits<std::int32_t>::min()) {
                    std::ostringstream stream;
                    stream << "backend_c: enum value out of supported range for '"
                           << enum_ir.fqn() << "." << value_ir.name() << "' (" << value_ir.value()
                           << "): the C backend skeleton does not yet support enum values "
                              "outside the 32-bit signed integer range";
                    error_message = stream.str();
                    return false;
                }
                planned_enum.values.push_back(PlannedEnumValue{
                    .name = uppercase_alnum_or_underscore(value_ir.name()),
                    .value = value_ir.value()});
            }
            file.enums.push_back(std::move(planned_enum));
        }

        for (const RecordIR& record_ir : ns.records()) {
            std::vector<PlannedField> planned_fields;
            planned_fields.reserve(static_cast<std::size_t>(record_ir.fields_size()));
            for (const FieldIR& field_ir : record_ir.fields()) {
                const std::optional<ScalarFieldEncoding> encoding =
                    lower_scalar_field_type(field_ir.type());
                if (!encoding.has_value()) {
                    // Mixed supported/unsupported records fail as a whole: a
                    // struct that silently dropped this field would be
                    // partial, misleading output, not a smaller feature set.
                    std::ostringstream stream;
                    stream << "backend_c: field '" << record_ir.fqn() << "." << field_ir.name()
                           << "' has a type the C backend does not support yet -- only bool, "
                              "fixed-width signed/unsigned integer, and f32/f64 scalar fields "
                              "are supported (see docs/design/c-backend.md); enum, string, "
                              "bytes, array, and record-reference fields remain unsupported";
                    error_message = stream.str();
                    return false;
                }
                planned_fields.push_back(PlannedField{
                    .name = field_ir.name(),
                    .field_index = field_ir.field_index(),
                    .encoding = *encoding,
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
        if (!collect_namespace_files(child, options, files, error_message)) {
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
    if (!collect_namespace_files(schema_ir.root_namespace(), options, files, error_message)) {
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
        stream << "enum {\n";
        for (const PlannedEnumValue& value : enum_ir.values) {
            stream << "    " << enum_ir.symbol_name << "_" << value.name << " = " << value.value
                   << ",\n";
        }
        stream << "};\n";
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
                stream << "    " << field.encoding.c_type << " " << field.name << ";\n";
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

// Declares one `uint8_t <field.name>_bytes[width];` scratch buffer per field,
// used to hold one field's encoded bytes before it is added to the
// quarry_c_field_t array passed to the runtime. Shared by _encoded_size and
// _encode, which otherwise independently render their own field-building
// loop (a small, deliberate duplication -- see compiler/backend_c/README.md).
void render_field_scratch_declarations(std::ostringstream& stream,
                                       const std::vector<PlannedField>& fields) {
    for (const PlannedField& field : fields) {
        stream << "    uint8_t " << field.name << "_bytes["
               << static_cast<unsigned int>(field.encoding.width_bytes) << "];\n";
    }
}

void render_build_fields_loop(std::ostringstream& stream, const std::vector<PlannedField>& fields,
                              bool check_write_status) {
    for (const PlannedField& field : fields) {
        stream << "    if (record->has_" << field.name << ") {\n";
        stream << "        quarry_c_writer_t writer;\n";
        stream << "        quarry_c_writer_init(&writer, " << field.name << "_bytes, sizeof("
               << field.name << "_bytes));\n";
        if (check_write_status) {
            stream << "        const quarry_c_status_t field_status = quarry_c_write_"
                   << field.encoding.runtime_verb << "(&writer, record->" << field.name
                   << ");\n";
            stream << "        if (field_status != QUARRY_C_STATUS_OK) {\n";
            stream << "            result.status = field_status;\n";
            stream << "            return result;\n";
            stream << "        }\n";
        } else {
            stream << "        (void)quarry_c_write_" << field.encoding.runtime_verb
                   << "(&writer, record->" << field.name << ");\n";
        }
        stream << "        fields[field_count].field_index = " << field.field_index << "U;\n";
        stream << "        fields[field_count].bytes = " << field.name << "_bytes;\n";
        stream << "        fields[field_count].length = writer.length;\n";
        stream << "        field_count += 1U;\n";
        stream << "    }\n";
    }
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
            stream << "            const quarry_c_status_t field_status = quarry_c_read_"
                   << field.encoding.runtime_verb << "(&field_reader, &result.value."
                   << field.name << ");\n";
            stream << "            if (field_status != QUARRY_C_STATUS_OK) {\n";
            stream << "                result.status = field_status;\n";
            stream << "                result.has_byte_offset = true;\n";
            stream << "                result.byte_offset = field_view.byte_offset;\n";
            stream << "                return result;\n";
            stream << "            }\n";
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
