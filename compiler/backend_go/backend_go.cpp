#include "compiler/backend_go/backend_go.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

namespace quarry::compiler::backend_go {

namespace {

using ::quarry::compiler::layout::FieldStorage;
using ::quarry::compiler::layout::LayoutModel;
using ::quarry::compiler::layout::RecordClassification;
using ::quarry::schema_ir::EnumIR;
using ::quarry::schema_ir::EnumValueIR;
using ::quarry::schema_ir::FieldIR;
using ::quarry::schema_ir::FieldType;
using ::quarry::schema_ir::NamespaceIR;
using ::quarry::schema_ir::PrimitiveType;
using ::quarry::schema_ir::RecordIR;

// --- Go naming ------------------------------------------------------------
//
// Schema identifiers become exported Go identifiers (records, enums, enum
// constants, struct fields) and lowercase package names. Each namespace
// segment is one directory; the last segment names the package.

[[nodiscard]] bool is_go_keyword(std::string_view name) {
    static constexpr std::string_view keywords[] = {
        "break",  "case",        "chan", "const",   "continue", "default", "defer",
        "else",   "fallthrough", "for",  "func",    "go",       "goto",    "if",
        "import", "interface",   "map",  "package", "range",    "return",  "select",
        "struct", "switch",      "type", "var",
    };
    return std::find(std::begin(keywords), std::end(keywords), name) != std::end(keywords);
}

[[nodiscard]] bool is_ascii_alnum(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// "sensor_reading" -> "SensorReading", "fooBar" -> "FooBar", "id" -> "Id".
// Any non-alphanumeric character separates words; a leading digit gains an
// "X" prefix so the result is always an exported identifier.
[[nodiscard]] std::string exported_identifier(std::string_view original) {
    std::string result;
    bool start_word = true;
    for (const unsigned char c : original) {
        if (!is_ascii_alnum(c)) {
            start_word = true;
            continue;
        }
        result.push_back(start_word ? static_cast<char>(std::toupper(c)) : static_cast<char>(c));
        start_word = false;
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(result.begin(), 'X');
    }
    return result;
}

// Lowercase package name for a namespace segment (directory names keep the
// segment's own spelling).
[[nodiscard]] std::string package_identifier(std::string_view segment) {
    std::string result;
    for (const unsigned char c : segment) {
        if (is_ascii_alnum(c)) {
            result.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == '_') {
            result.push_back('_');
        }
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(result.begin(), 'x');
    }
    if (is_go_keyword(result) || result == "_") {
        result.push_back('_');
    }
    return result;
}

[[nodiscard]] bool is_safe_path_segment(std::string_view segment) {
    return !segment.empty() && segment != "." && segment != ".." &&
           std::all_of(segment.begin(), segment.end(),
                       [](unsigned char c) { return is_ascii_alnum(c) || c == '_' || c == '-'; });
}

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

[[nodiscard]] std::string join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string joined;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            joined.append(separator);
        }
        joined.append(parts[i]);
    }
    return joined;
}

// Methods of every generated record type; struct fields may not share them.
[[nodiscard]] const std::set<std::string>& record_method_names() {
    static const std::set<std::string> names = {
        "MarshalBRF",
        "AppendBRF",
        "AppendBRFWithRecordID",
        "UnmarshalBRF",
        "UnmarshalBRFWithRecordID",
    };
    return names;
}

[[nodiscard]] bool insert_unique(std::set<std::string>& names, const std::string& generated,
                                 std::string_view original, std::string_view context,
                                 std::string& error_message) {
    if (names.insert(generated).second) {
        return true;
    }
    error_message = "backend_go: " + std::string(context) + " '" + std::string(original) +
                    "' and another schema identifier both map to Go identifier '" + generated +
                    "'; generation is ambiguous";
    return false;
}

// --- Type lowering ----------------------------------------------------------

struct ScalarInfo {
    std::string_view kind; // bool, i8, u8, i16, u16, i32, u32, i64, u64, f32, f64
    std::string_view go_type;
    std::uint32_t width = 0U;
};

[[nodiscard]] std::optional<ScalarInfo> lower_scalar(const FieldType& type) {
    if (type.kind_case() != FieldType::kPrimitive) {
        return std::nullopt;
    }
    switch (type.primitive()) {
    case PrimitiveType::PRIMITIVE_TYPE_BOOL:
        return ScalarInfo{"bool", "bool", 1U};
    case PrimitiveType::PRIMITIVE_TYPE_I8:
        return ScalarInfo{"i8", "int8", 1U};
    case PrimitiveType::PRIMITIVE_TYPE_U8:
        return ScalarInfo{"u8", "uint8", 1U};
    case PrimitiveType::PRIMITIVE_TYPE_I16:
        return ScalarInfo{"i16", "int16", 2U};
    case PrimitiveType::PRIMITIVE_TYPE_U16:
        return ScalarInfo{"u16", "uint16", 2U};
    case PrimitiveType::PRIMITIVE_TYPE_I32:
        return ScalarInfo{"i32", "int32", 4U};
    case PrimitiveType::PRIMITIVE_TYPE_U32:
        return ScalarInfo{"u32", "uint32", 4U};
    case PrimitiveType::PRIMITIVE_TYPE_I64:
        return ScalarInfo{"i64", "int64", 8U};
    case PrimitiveType::PRIMITIVE_TYPE_U64:
        return ScalarInfo{"u64", "uint64", 8U};
    case PrimitiveType::PRIMITIVE_TYPE_F32:
        return ScalarInfo{"f32", "float32", 4U};
    case PrimitiveType::PRIMITIVE_TYPE_F64:
        return ScalarInfo{"f64", "float64", 8U};
    default:
        return std::nullopt;
    }
}

struct EnumEntry {
    std::string go_name;
    std::string namespace_fqn;
    std::string schema_name;
    bool all_non_negative = true;
    std::uint32_t width = 1U;
    std::vector<std::pair<std::string, std::int64_t>> values;
};

struct RecordEntry {
    std::string go_name;
    std::string namespace_fqn;
    std::uint32_t record_id = 0U;
};

using EnumCatalog = std::unordered_map<std::uint64_t, EnumEntry>;
using RecordCatalog = std::unordered_map<std::uint64_t, RecordEntry>;

void collect_catalogs(const NamespaceIR& ns, EnumCatalog& enums, RecordCatalog& records) {
    for (const EnumIR& enum_ir : ns.enums()) {
        EnumEntry entry;
        entry.go_name = exported_identifier(enum_ir.name());
        entry.namespace_fqn = ns.fqn();
        entry.schema_name = enum_ir.name();
        std::uint64_t max_value = 0U;
        for (const EnumValueIR& value : enum_ir.values()) {
            entry.values.emplace_back(value.name(), value.value());
            if (value.value() < 0) {
                entry.all_non_negative = false;
            } else {
                max_value = std::max(max_value, static_cast<std::uint64_t>(value.value()));
            }
        }
        // Smallest unsigned width holding every declared value: identical to
        // the C, C++ and Python backends, which keeps enum wire bytes equal.
        if (max_value > std::numeric_limits<std::uint32_t>::max()) {
            entry.width = 8U;
        } else if (max_value > std::numeric_limits<std::uint16_t>::max()) {
            entry.width = 4U;
        } else if (max_value > std::numeric_limits<std::uint8_t>::max()) {
            entry.width = 2U;
        }
        enums.emplace(enum_ir.ir_id(), std::move(entry));
    }
    for (const RecordIR& record_ir : ns.records()) {
        records.emplace(record_ir.ir_id(), RecordEntry{exported_identifier(record_ir.name()),
                                                       ns.fqn(), record_ir.record_id()});
    }
    for (const NamespaceIR& child : ns.namespaces()) {
        collect_catalogs(child, enums, records);
    }
}

enum class Kind { Scalar, Enum, String, Bytes, Record };

struct PlannedField {
    std::string schema_name; // error paths use the schema spelling
    std::string go_name;
    std::uint32_t field_index = 0U;
    Kind kind = Kind::Scalar;
    bool is_array = false;
    std::uint32_t max_elements = 0U;
    ScalarInfo scalar;                   // Kind::Scalar
    std::uint32_t enum_width = 0U;       // Kind::Enum
    std::string enum_schema_name;        // Kind::Enum, for error text
    std::uint32_t max_bytes = 0U;        // Kind::String / Kind::Bytes
    std::uint32_t record_target_id = 0U; // Kind::Record: ID resolved by this schema
    std::string type_name;               // Enum/Record: Go type name
    std::string target_namespace_fqn;    // Enum/Record
    std::string type_expression;         // Enum/Record: possibly package-qualified
    // Canonical BRF v2 layout.
    std::uint32_t byte_offset = 0U;
    std::uint32_t slot_size = 0U;
    std::uint32_t presence_bit = 0U;
    FieldStorage storage = FieldStorage::Fixed;
    bool element_fixed_record = false;
    std::uint32_t element_size = 0U;
};

struct PlannedRecord {
    std::string go_name;
    std::string schema_fqn;
    std::uint32_t record_id = 0U;
    std::uint32_t presence_bitmap_size = 0U;
    std::uint32_t fixed_region_size = 0U;
    std::vector<PlannedField> fields;
};

struct PlannedEnumValue {
    std::string go_name;
    std::int64_t value = 0;
};

struct PlannedEnum {
    std::string go_name;
    std::string schema_fqn;
    std::uint32_t width = 1U;
    std::vector<PlannedEnumValue> values;
};

struct PlannedImport {
    std::string alias;
    std::string path;
};

struct PlannedPackageFile {
    std::string namespace_fqn;
    std::vector<std::string> directories;
    std::string package_name;
    std::string relative_path;
    std::vector<PlannedEnum> enums;
    std::vector<PlannedRecord> records;
    std::vector<PlannedImport> imports;
};

[[nodiscard]] std::string field_context(const RecordIR& record, const FieldIR& field) {
    return "field '" + record.fqn() + "." + field.name() + "'";
}

[[nodiscard]] std::optional<PlannedField>
lower_field(const RecordIR& record_ir, const FieldIR& field_ir, const EnumCatalog& enums,
            const RecordCatalog& records, std::string& error_message) {
    PlannedField field;
    field.schema_name = field_ir.name();
    field.field_index = field_ir.field_index();
    const FieldType* type = &field_ir.type();
    if (type->kind_case() == FieldType::kArray) {
        field.is_array = true;
        field.max_elements = type->array().max_elements();
        type = &type->array().element_type();
        if (type->kind_case() == FieldType::kArray) {
            error_message = "backend_go: " + field_context(record_ir, field_ir) +
                            " is a nested array, which the schema model does not support";
            return std::nullopt;
        }
    }
    if (const auto scalar = lower_scalar(*type)) {
        field.kind = Kind::Scalar;
        field.scalar = *scalar;
        return field;
    }
    switch (type->kind_case()) {
    case FieldType::kString:
        field.kind = Kind::String;
        field.max_bytes = type->string().max_bytes();
        return field;
    case FieldType::kBytes:
        field.kind = Kind::Bytes;
        field.max_bytes = type->bytes().max_bytes();
        return field;
    case FieldType::kEnumType: {
        const auto it = enums.find(type->enum_type().target_enum_ir_id());
        if (it == enums.end()) {
            break;
        }
        if (!it->second.all_non_negative) {
            error_message = "backend_go: " + field_context(record_ir, field_ir) +
                            " references an enum with a negative declared value; BRF enums "
                            "are unsigned";
            return std::nullopt;
        }
        field.kind = Kind::Enum;
        field.enum_width = it->second.width;
        field.enum_schema_name = it->second.schema_name;
        field.type_name = it->second.go_name;
        field.target_namespace_fqn = it->second.namespace_fqn;
        return field;
    }
    case FieldType::kRecord: {
        const auto it = records.find(type->record().target_record_ir_id());
        if (it == records.end()) {
            break;
        }
        field.kind = Kind::Record;
        field.type_name = it->second.go_name;
        field.target_namespace_fqn = it->second.namespace_fqn;
        field.record_target_id = it->second.record_id;
        return field;
    }
    default:
        break;
    }
    error_message = "backend_go: " + field_context(record_ir, field_ir) +
                    " has a type the Go backend does not support";
    return std::nullopt;
}

[[nodiscard]] bool attach_layout(const RecordIR& record_ir, PlannedRecord& record,
                                 const LayoutModel& layout_model, std::string& error_message) {
    const auto* layout = layout_model.find_record(record_ir.fqn());
    if (layout == nullptr || layout->fields.size() != record.fields.size()) {
        error_message = "backend_go: canonical BRF v2 layout is missing record '" +
                        record_ir.fqn() + "' or has an unexpected field count";
        return false;
    }
    record.presence_bitmap_size = layout->presence_bitmap_size;
    record.fixed_region_size = layout->fixed_region_size;
    for (std::size_t i = 0; i < record.fields.size(); ++i) {
        const auto& canonical = layout->fields[i];
        PlannedField& field = record.fields[i];
        if (canonical.field_index != field.field_index || canonical.name != field.schema_name) {
            error_message = "backend_go: canonical BRF v2 layout field order disagrees with "
                            "Schema IR for record '" +
                            record_ir.fqn() + "'";
            return false;
        }
        field.byte_offset = canonical.location.byte_offset;
        field.slot_size = canonical.slot_size;
        field.presence_bit = canonical.presence_bit_index;
        field.storage = canonical.storage;
        field.element_fixed_record =
            field.is_array && field.kind == Kind::Record &&
            canonical.type.element_type != nullptr &&
            canonical.type.element_type->classification == RecordClassification::FixedSize;
        if (field.element_fixed_record) {
            field.element_size = canonical.type.element_type->encoded_width;
        }
        const bool variable = field.storage == FieldStorage::VariableDescriptor;
        const bool expect_variable =
            field.is_array || field.kind == Kind::String || field.kind == Kind::Bytes;
        if ((expect_variable && !variable) ||
            (field.kind == Kind::Scalar && !field.is_array &&
             field.slot_size != field.scalar.width) ||
            (field.kind == Kind::Enum && !field.is_array && field.slot_size != field.enum_width) ||
            (variable && field.slot_size != 8U)) {
            error_message = "backend_go: unexpected canonical slot for field '" + record_ir.fqn() +
                            "." + field.schema_name + "'";
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool plan_namespace(const NamespaceIR& ns, const CodegenOptions& options,
                                  const EnumCatalog& enums, const RecordCatalog& records,
                                  const LayoutModel& layout_model,
                                  std::vector<PlannedPackageFile>& files,
                                  std::string& error_message) {
    if (ns.records_size() > 0 || ns.enums_size() > 0) {
        PlannedPackageFile file;
        file.namespace_fqn = ns.fqn();
        file.directories = namespace_parts(ns.fqn());
        for (const std::string& segment : file.directories) {
            if (!is_safe_path_segment(segment)) {
                error_message = "backend_go: namespace segment '" + segment + "' of '" + ns.fqn() +
                                "' is not a safe Go package directory name";
                return false;
            }
        }
        file.package_name = package_identifier(file.directories.empty() ? options.root_file_stem
                                                                        : file.directories.back());
        file.relative_path = (file.directories.empty() ? "" : join(file.directories, "/") + "/") +
                             options.root_file_stem + ".quarry.go";

        std::set<std::string> package_names; // package-level identifiers
        for (const EnumIR& enum_ir : ns.enums()) {
            const EnumEntry& entry = enums.at(enum_ir.ir_id());
            if (!entry.all_non_negative) {
                error_message = "backend_go: enum '" + ns.fqn() + "." + enum_ir.name() +
                                "' has a negative declared value; BRF enums are unsigned";
                return false;
            }
            if (!insert_unique(package_names, entry.go_name, enum_ir.name(), "enum",
                               error_message)) {
                return false;
            }
            PlannedEnum planned;
            planned.go_name = entry.go_name;
            planned.schema_fqn =
                ns.fqn().empty() ? enum_ir.name() : ns.fqn() + "." + enum_ir.name();
            planned.width = entry.width;
            for (const auto& [name, value] : entry.values) {
                const std::string constant = entry.go_name + exported_identifier(name);
                if (!insert_unique(package_names, constant, name, "enum value", error_message)) {
                    return false;
                }
                planned.values.push_back(PlannedEnumValue{constant, value});
            }
            file.enums.push_back(std::move(planned));
        }

        for (const RecordIR& record_ir : ns.records()) {
            PlannedRecord record;
            record.go_name = records.at(record_ir.ir_id()).go_name;
            record.schema_fqn = record_ir.fqn();
            record.record_id = record_ir.record_id();
            if (!insert_unique(package_names, record.go_name, record_ir.name(), "record",
                               error_message) ||
                !insert_unique(package_names, record.go_name + "RecordID", record_ir.name(),
                               "record ID constant for record", error_message)) {
                return false;
            }
            std::set<std::string> field_names = record_method_names();
            for (const FieldIR& field_ir : record_ir.fields()) {
                std::optional<PlannedField> field =
                    lower_field(record_ir, field_ir, enums, records, error_message);
                if (!field.has_value()) {
                    return false;
                }
                field->go_name = exported_identifier(field_ir.name());
                if (record_method_names().contains(field->go_name)) {
                    field->go_name.push_back('_');
                }
                if (!insert_unique(field_names, field->go_name, field_ir.name(),
                                   "field in record '" + record_ir.fqn() + "'", error_message)) {
                    return false;
                }
                record.fields.push_back(std::move(*field));
            }
            if (!attach_layout(record_ir, record, layout_model, error_message)) {
                return false;
            }
            file.records.push_back(std::move(record));
        }
        files.push_back(std::move(file));
    }
    for (const NamespaceIR& child : ns.namespaces()) {
        if (!plan_namespace(child, options, enums, records, layout_model, files, error_message)) {
            return false;
        }
    }
    return true;
}

// Resolves cross-namespace type references to package-qualified expressions
// and drops files the compiler's output plan does not emit.
[[nodiscard]] bool resolve_references(std::vector<PlannedPackageFile>& files,
                                      const CodegenOptions& options,
                                      const output_planning::OutputPlan* output_plan,
                                      std::string& error_message) {
    std::map<std::string, std::vector<std::string>> directories_by_namespace;
    for (const PlannedPackageFile& file : files) {
        directories_by_namespace.emplace(file.namespace_fqn, file.directories);
    }
    std::set<std::string> planned;
    std::set<std::string> emitting;
    if (output_plan != nullptr) {
        for (const auto& unit : output_plan->units) {
            planned.insert(unit.namespace_fqn);
            if (unit.emits_output) {
                emitting.insert(unit.namespace_fqn);
            }
        }
    }
    for (PlannedPackageFile& file : files) {
        std::map<std::string, std::string> alias_by_namespace;
        for (PlannedRecord& record : file.records) {
            for (PlannedField& field : record.fields) {
                if (field.kind != Kind::Enum && field.kind != Kind::Record) {
                    continue;
                }
                if (field.target_namespace_fqn == file.namespace_fqn) {
                    field.type_expression = field.type_name;
                    continue;
                }
                const auto dirs = directories_by_namespace.find(field.target_namespace_fqn);
                if (dirs == directories_by_namespace.end() ||
                    (output_plan != nullptr && !planned.contains(field.target_namespace_fqn))) {
                    error_message = "backend_go: field '" + record.schema_fqn + "." +
                                    field.schema_name + "' references namespace '" +
                                    field.target_namespace_fqn +
                                    "', which is not in the compiler output plan";
                    return false;
                }
                if (options.go_import_prefix.empty()) {
                    error_message = "backend_go: field '" + record.schema_fqn + "." +
                                    field.schema_name + "' references namespace '" +
                                    field.target_namespace_fqn +
                                    "'; pass --go-import-prefix with the Go import path of the "
                                    "output directory so the generated package can import it";
                    return false;
                }
                std::string& alias = alias_by_namespace[field.target_namespace_fqn];
                if (alias.empty()) {
                    alias = "quarrydep" + std::to_string(alias_by_namespace.size() - 1U);
                    std::string path = options.go_import_prefix;
                    if (!dirs->second.empty()) {
                        path += "/" + join(dirs->second, "/");
                    }
                    file.imports.push_back(PlannedImport{alias, path});
                }
                field.type_expression = alias + "." + field.type_name;
            }
        }
    }
    if (output_plan != nullptr) {
        files.erase(std::remove_if(files.begin(), files.end(),
                                   [&emitting](const PlannedPackageFile& file) {
                                       return !emitting.contains(file.namespace_fqn);
                                   }),
                    files.end());
    }
    std::map<std::string, std::string> seen;
    for (const PlannedPackageFile& file : files) {
        const auto [it, inserted] = seen.emplace(file.relative_path, file.namespace_fqn);
        if (!inserted) {
            error_message = "backend_go: namespaces '" + it->second + "' and '" +
                            file.namespace_fqn + "' both map to generated path '" +
                            file.relative_path + "'";
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool build_plan(const schema_ir::SchemaIrModel& schema_ir,
                              const CodegenOptions& options,
                              const output_planning::OutputPlan* output_plan,
                              std::vector<PlannedPackageFile>& files, std::string& error_message) {
    if (!is_safe_path_segment(options.root_file_stem)) {
        error_message =
            "backend_go: root file stem '" + options.root_file_stem + "' is not a safe file name";
        return false;
    }
    diagnostics::DiagnosticEngine layout_diagnostics;
    layout::LayoutComputer layout_computer;
    const LayoutModel layout_model = layout_computer.compute(schema_ir, layout_diagnostics);
    if (layout_diagnostics.has_errors()) {
        error_message = "backend_go: BRF v2 layout computation failed";
        if (!layout_diagnostics.diagnostics().empty()) {
            error_message += ": " + layout_diagnostics.diagnostics().front().message();
        }
        return false;
    }
    EnumCatalog enums;
    RecordCatalog records;
    collect_catalogs(schema_ir.root_namespace(), enums, records);
    return plan_namespace(schema_ir.root_namespace(), options, enums, records, layout_model, files,
                          error_message) &&
           resolve_references(files, options, output_plan, error_message);
}

// --- Rendering --------------------------------------------------------------
//
// Output is gofmt-clean: tabs for indentation, single spaces elsewhere, and
// struct fields / const specs aligned in columns as gofmt's tabwriter does.

[[nodiscard]] std::string go_string_literal(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

[[nodiscard]] std::string unsigned_type(std::uint32_t width) {
    return "uint" + std::to_string(width * 8U);
}

[[nodiscard]] std::string field_type(const PlannedField& field) {
    std::string element;
    switch (field.kind) {
    case Kind::Scalar:
        element = std::string(field.scalar.go_type);
        break;
    case Kind::Enum:
    case Kind::Record:
        element = field.type_expression;
        break;
    case Kind::String:
        element = "string";
        break;
    case Kind::Bytes:
        element = "[]byte";
        break;
    }
    if (field.is_array) {
        return "[]" + element;
    }
    if (field.kind == Kind::String || field.kind == Kind::Bytes) {
        return element;
    }
    return "*" + element;
}

// Statement writing one fixed-width value `v` of `width` bytes (scalar kind
// or enum) at slot p; `p` is a Go slice expression.
[[nodiscard]] std::string put_statement(const PlannedField& field, const std::string& p,
                                        const std::string& v) {
    if (field.kind == Kind::Enum) {
        if (field.enum_width == 1U) {
            return "brf.PutUint8(" + p + ", uint8(" + v + "))";
        }
        return "brf.PutUint" + std::to_string(field.enum_width * 8U) + "(" + p + ", " +
               unsigned_type(field.enum_width) + "(" + v + "))";
    }
    const std::string_view k = field.scalar.kind;
    if (k == "bool")
        return "brf.PutBool(" + p + ", " + v + ")";
    if (k == "u8")
        return "brf.PutUint8(" + p + ", " + v + ")";
    if (k == "i8")
        return "brf.PutUint8(" + p + ", uint8(" + v + "))";
    if (k == "f32")
        return "brf.PutFloat32(" + p + ", " + v + ")";
    if (k == "f64")
        return "brf.PutFloat64(" + p + ", " + v + ")";
    const std::string bits = std::to_string(field.scalar.width * 8U);
    if (k.front() == 'u')
        return "brf.PutUint" + bits + "(" + p + ", " + v + ")";
    return "brf.PutUint" + bits + "(" + p + ", uint" + bits + "(" + v + "))";
}

// Expression appending one fixed-width array element `v` to b.
[[nodiscard]] std::string append_expression(const PlannedField& field, const std::string& v) {
    if (field.kind == Kind::Enum) {
        if (field.enum_width == 1U) {
            return "append(b, byte(" + v + "))";
        }
        return "brf.AppendUint" + std::to_string(field.enum_width * 8U) + "(b, " +
               unsigned_type(field.enum_width) + "(" + v + "))";
    }
    const std::string_view k = field.scalar.kind;
    if (k == "bool")
        return "brf.AppendBool(b, " + v + ")";
    if (k == "u8")
        return "append(b, " + v + ")";
    if (k == "i8")
        return "append(b, byte(" + v + "))";
    if (k == "f32")
        return "brf.AppendFloat32(b, " + v + ")";
    if (k == "f64")
        return "brf.AppendFloat64(b, " + v + ")";
    const std::string bits = std::to_string(field.scalar.width * 8U);
    if (k.front() == 'u')
        return "brf.AppendUint" + bits + "(b, " + v + ")";
    return "brf.AppendUint" + bits + "(b, uint" + bits + "(" + v + "))";
}

// Expression reading a non-bool fixed-width value: from the record reader at
// a slot offset (reader == true), or from array element bytes `e`.
[[nodiscard]] std::string read_expression(const PlannedField& field, bool reader,
                                          std::uint32_t at) {
    const std::string offset = std::to_string(at);
    const auto read_unsigned = [&](std::uint32_t width) -> std::string {
        if (width == 1U) {
            return reader ? "r.Uint8(" + offset + ")" : "e[0]";
        }
        const std::string bits = std::to_string(width * 8U);
        return reader ? "r.Uint" + bits + "(" + offset + ")" : "brf.Uint" + bits + "(e)";
    };
    if (field.kind == Kind::Enum) {
        return field.type_expression + "(" + read_unsigned(field.enum_width) + ")";
    }
    const std::string_view k = field.scalar.kind;
    if (k == "f32")
        return reader ? "r.Float32(" + offset + ")" : "brf.Float32(e)";
    if (k == "f64")
        return reader ? "r.Float64(" + offset + ")" : "brf.Float64(e)";
    const std::string raw = read_unsigned(field.scalar.width);
    if (k.front() == 'u')
        return raw;
    return std::string(field.scalar.go_type) + "(" + raw + ")";
}

[[nodiscard]] std::uint32_t element_width(const PlannedField& field) {
    return field.kind == Kind::Enum ? field.enum_width : field.scalar.width;
}

void render_enum(std::ostringstream& out, const PlannedEnum& planned) {
    out << "// " << planned.go_name << " is the schema enum " << planned.schema_fqn << ".\n";
    out << "type " << planned.go_name << " " << unsigned_type(planned.width) << "\n\n";
    if (!planned.values.empty()) {
        std::size_t width = 0U;
        for (const auto& value : planned.values) {
            width = std::max(width, value.go_name.size());
        }
        out << "const (\n";
        for (const auto& value : planned.values) {
            out << "\t" << value.go_name << std::string(width - value.go_name.size() + 1U, ' ')
                << planned.go_name << " = " << value.value << "\n";
        }
        out << ")\n\n";
    }
    out << "// Valid reports whether v is a value the schema declares.\n";
    out << "func (v " << planned.go_name << ") Valid() bool {\n";
    std::vector<std::string> cases;
    std::set<std::int64_t> seen;
    for (const auto& value : planned.values) {
        if (seen.insert(value.value).second) {
            cases.push_back(value.go_name);
        }
    }
    if (cases.empty()) {
        out << "\treturn false\n}\n";
        return;
    }
    out << "\tswitch v {\n\tcase " << join(cases, ", ") << ":\n\t\treturn true\n\t}\n";
    out << "\treturn false\n}\n";
}

void render_struct(std::ostringstream& out, const PlannedRecord& record) {
    out << "// " << record.go_name << "RecordID is the BRF record ID of " << record.go_name
        << " in this schema.\n";
    out << "const " << record.go_name << "RecordID = " << record.record_id << "\n\n";
    out << "// " << record.go_name << " is the schema record " << record.schema_fqn << ".\n";
    out << "//\n";
    out << "// Scalar, enum and nested-record fields are nil when absent. String, bytes\n";
    out << "// and array fields are absent when empty.\n";
    if (record.fields.empty()) {
        out << "type " << record.go_name << " struct{}\n";
        return;
    }
    std::size_t width = 0U;
    for (const auto& field : record.fields) {
        width = std::max(width, field.go_name.size());
    }
    out << "type " << record.go_name << " struct {\n";
    for (const auto& field : record.fields) {
        out << "\t" << field.go_name << std::string(width - field.go_name.size() + 1U, ' ')
            << field_type(field) << "\n";
    }
    out << "}\n";
}

void render_encode_field(std::ostringstream& out, const PlannedField& field) {
    const std::string name = go_string_literal(field.schema_name);
    const std::string m = "m." + field.go_name;
    const std::string at = std::to_string(field.byte_offset);
    const std::string bit = std::to_string(field.presence_bit);
    const std::string wrap = "brf.WrapField(" + name + ", err)";
    const auto end_variable = [&](const std::string& indent) {
        out << indent << "if err := brf.EndVariable(b, base, " << at << ", start, " << bit
            << "); err != nil {\n"
            << indent << "\treturn b, " << wrap << "\n"
            << indent << "}\n";
    };

    if (!field.is_array && (field.kind == Kind::Scalar || field.kind == Kind::Enum)) {
        out << "\tif " << m << " != nil {\n";
        if (field.kind == Kind::Enum) {
            out << "\t\tif !" << m << ".Valid() {\n"
                << "\t\t\treturn b, brf.WrapField(" << name << ", brf.InvalidEnum("
                << go_string_literal(field.enum_schema_name) << ", uint64(*" << m << ")))\n"
                << "\t\t}\n";
        }
        out << "\t\t" << put_statement(field, "b[base+" + at + ":]", "*" + m) << "\n";
        out << "\t\tbrf.SetPresent(b[base:], " << bit << ")\n";
        out << "\t}\n";
        return;
    }
    if (!field.is_array && (field.kind == Kind::String || field.kind == Kind::Bytes)) {
        out << "\tif len(" << m << ") != 0 {\n";
        out << "\t\tif err := brf." << (field.kind == Kind::String ? "CheckString" : "CheckBytes")
            << "(" << m << ", " << field.max_bytes << "); err != nil {\n"
            << "\t\t\treturn b, " << wrap << "\n\t\t}\n";
        out << "\t\tstart := len(b)\n";
        out << "\t\tb = append(b, " << m << "...)\n";
        end_variable("\t\t");
        out << "\t}\n";
        return;
    }
    if (!field.is_array && field.kind == Kind::Record) {
        const std::string id = std::to_string(field.record_target_id);
        out << "\tif " << m << " != nil {\n";
        out << "\t\tstart := len(b)\n";
        out << "\t\tvar err error\n";
        out << "\t\tif b, err = " << m << ".AppendBRFWithRecordID(b, " << id << "); err != nil {\n"
            << "\t\t\treturn b, " << wrap << "\n\t\t}\n";
        if (field.storage == FieldStorage::InlineFixedNestedRecord) {
            const std::string size = std::to_string(field.slot_size);
            out << "\t\tif len(b)-start != " << size << " {\n"
                << "\t\t\treturn b, brf.WrapField(" << name << ", brf.WrongSize(len(b)-start, "
                << size << "))\n\t\t}\n";
            out << "\t\tcopy(b[base+" << at << ":], b[start:])\n";
            out << "\t\tb = b[:start]\n";
            out << "\t\tbrf.SetPresent(b[base:], " << bit << ")\n";
        } else {
            end_variable("\t\t");
        }
        out << "\t}\n";
        return;
    }

    // Arrays: a varuint count, then fixed-width or length-prefixed elements.
    out << "\tif len(" << m << ") != 0 {\n";
    out << "\t\tif err := brf.CheckCount(len(" << m << "), " << field.max_elements
        << "); err != nil {\n"
        << "\t\t\treturn b, " << wrap << "\n\t\t}\n";
    out << "\t\tstart := len(b)\n";
    out << "\t\tb = brf.AppendVaruint(b, uint64(len(" << m << ")))\n";
    const std::string wrap_element = "brf.WrapElement(" + name + ", i, err)";
    switch (field.kind) {
    case Kind::Scalar:
        out << "\t\tfor _, v := range " << m << " {\n";
        out << "\t\t\tb = " << append_expression(field, "v") << "\n";
        out << "\t\t}\n";
        break;
    case Kind::Enum:
        out << "\t\tfor i, v := range " << m << " {\n";
        out << "\t\t\tif !v.Valid() {\n"
            << "\t\t\t\treturn b, brf.WrapElement(" << name << ", i, brf.InvalidEnum("
            << go_string_literal(field.enum_schema_name) << ", uint64(v)))\n"
            << "\t\t\t}\n";
        out << "\t\t\tb = " << append_expression(field, "v") << "\n";
        out << "\t\t}\n";
        break;
    case Kind::String:
    case Kind::Bytes:
        out << "\t\tfor i, v := range " << m << " {\n";
        out << "\t\t\tif err := brf." << (field.kind == Kind::String ? "CheckString" : "CheckBytes")
            << "(v, " << field.max_bytes << "); err != nil {\n"
            << "\t\t\t\treturn b, " << wrap_element << "\n\t\t\t}\n";
        out << "\t\t\tb = brf.AppendVaruint(b, uint64(len(v)))\n";
        out << "\t\t\tb = append(b, v...)\n";
        out << "\t\t}\n";
        break;
    case Kind::Record: {
        const std::string id = std::to_string(field.record_target_id);
        out << "\t\tfor i := range " << m << " {\n";
        out << "\t\t\tvar err error\n";
        if (field.element_fixed_record) {
            const std::string size = std::to_string(field.element_size);
            out << "\t\t\telement := len(b)\n";
            out << "\t\t\tif b, err = " << m << "[i].AppendBRFWithRecordID(b, " << id
                << "); err != nil {\n"
                << "\t\t\t\treturn b, " << wrap_element << "\n\t\t\t}\n";
            out << "\t\t\tif len(b)-element != " << size << " {\n"
                << "\t\t\t\treturn b, brf.WrapElement(" << name
                << ", i, brf.WrongSize(len(b)-element, " << size << "))\n\t\t\t}\n";
        } else {
            out << "\t\t\tvar prefix int\n";
            out << "\t\t\tb, prefix = brf.ReserveLengthPrefix(b)\n";
            out << "\t\t\tif b, err = " << m << "[i].AppendBRFWithRecordID(b, " << id
                << "); err != nil {\n"
                << "\t\t\t\treturn b, " << wrap_element << "\n\t\t\t}\n";
            out << "\t\t\tb = brf.FinishLengthPrefix(b, prefix)\n";
        }
        out << "\t\t}\n";
        break;
    }
    }
    end_variable("\t\t");
    out << "\t}\n";
}

void render_decode_field(std::ostringstream& out, const PlannedField& field) {
    const std::string name = go_string_literal(field.schema_name);
    const std::string m = "m." + field.go_name;
    const std::string at = std::to_string(field.byte_offset);
    const std::string wrap = "brf.WrapField(" + name + ", err)";
    const std::string wrap_element = "brf.WrapElement(" + name + ", i, err)";
    out << "\tif r.Present(" << field.presence_bit << ") {\n";

    if (!field.is_array && (field.kind == Kind::Scalar || field.kind == Kind::Enum)) {
        if (field.kind == Kind::Scalar && field.scalar.kind == "bool") {
            out << "\t\tv, err := r.Bool(" << at << ")\n"
                << "\t\tif err != nil {\n\t\t\treturn " << wrap << "\n\t\t}\n";
        } else {
            out << "\t\tv := " << read_expression(field, true, field.byte_offset) << "\n";
        }
        if (field.kind == Kind::Enum) {
            out << "\t\tif !v.Valid() {\n"
                << "\t\t\treturn brf.WrapField(" << name << ", brf.UnknownEnum("
                << go_string_literal(field.enum_schema_name) << ", uint64(v)))\n"
                << "\t\t}\n";
        }
        out << "\t\t" << m << " = &v\n";
    } else if (!field.is_array && field.kind == Kind::Record &&
               field.storage == FieldStorage::InlineFixedNestedRecord) {
        out << "\t\t" << m << " = &" << field.type_expression << "{}\n";
        out << "\t\tif err := " << m << ".UnmarshalBRFWithRecordID(r.Slot(" << at << ", "
            << field.slot_size << "), " << field.record_target_id << "); err != nil {\n"
            << "\t\t\treturn " << wrap << "\n\t\t}\n";
    } else {
        out << "\t\tp, err := r.Variable(" << at << ")\n"
            << "\t\tif err != nil {\n\t\t\treturn " << wrap << "\n\t\t}\n";
        if (!field.is_array) {
            switch (field.kind) {
            case Kind::String:
            case Kind::Bytes:
                out << "\t\tif " << m << ", err = brf."
                    << (field.kind == Kind::String ? "String" : "Bytes") << "(p, "
                    << field.max_bytes << "); err != nil {\n"
                    << "\t\t\treturn " << wrap << "\n\t\t}\n";
                break;
            default: // variable-size nested record
                out << "\t\t" << m << " = &" << field.type_expression << "{}\n";
                out << "\t\tif err := " << m << ".UnmarshalBRFWithRecordID(p, "
                    << field.record_target_id << "); err != nil {\n"
                    << "\t\t\treturn " << wrap << "\n\t\t}\n";
                break;
            }
        } else {
            out << "\t\ta, n, err := brf.NewArray(p, " << field.max_elements << ")\n"
                << "\t\tif err != nil {\n\t\t\treturn " << wrap << "\n\t\t}\n";
            const bool fixed_elements = field.kind == Kind::Scalar || field.kind == Kind::Enum ||
                                        (field.kind == Kind::Record && field.element_fixed_record);
            const std::uint32_t width =
                field.kind == Kind::Record ? field.element_size : element_width(field);
            if (fixed_elements) {
                out << "\t\tif err := a.ExpectFixed(n, " << width << "); err != nil {\n"
                    << "\t\t\treturn " << wrap << "\n\t\t}\n";
            }
            std::string element_type;
            switch (field.kind) {
            case Kind::Scalar:
                element_type = std::string(field.scalar.go_type);
                break;
            case Kind::Enum:
            case Kind::Record:
                element_type = field.type_expression;
                break;
            case Kind::String:
                element_type = "string";
                break;
            case Kind::Bytes:
                element_type = "[]byte";
                break;
            }
            out << "\t\t" << m << " = make([]" << element_type << ", n)\n";
            out << "\t\tfor i := range " << m << " {\n";
            if (fixed_elements) {
                out << "\t\t\te, err := a.Fixed(" << width << ")\n";
            } else {
                out << "\t\t\te, err := a.Prefixed()\n";
            }
            out << "\t\t\tif err != nil {\n\t\t\t\treturn " << wrap_element << "\n\t\t\t}\n";
            switch (field.kind) {
            case Kind::Scalar:
                if (field.scalar.kind == "bool") {
                    out << "\t\t\tif " << m << "[i], err = brf.Bool(e); err != nil {\n"
                        << "\t\t\t\treturn " << wrap_element << "\n\t\t\t}\n";
                } else {
                    out << "\t\t\t" << m << "[i] = " << read_expression(field, false, 0U) << "\n";
                }
                break;
            case Kind::Enum:
                out << "\t\t\tv := " << read_expression(field, false, 0U) << "\n";
                out << "\t\t\tif !v.Valid() {\n"
                    << "\t\t\t\treturn brf.WrapElement(" << name << ", i, brf.UnknownEnum("
                    << go_string_literal(field.enum_schema_name) << ", uint64(v)))\n"
                    << "\t\t\t}\n";
                out << "\t\t\t" << m << "[i] = v\n";
                break;
            case Kind::String:
            case Kind::Bytes:
                out << "\t\t\tif " << m << "[i], err = brf."
                    << (field.kind == Kind::String ? "String" : "Bytes") << "(e, "
                    << field.max_bytes << "); err != nil {\n"
                    << "\t\t\t\treturn " << wrap_element << "\n\t\t\t}\n";
                break;
            case Kind::Record:
                out << "\t\t\tif err := " << m << "[i].UnmarshalBRFWithRecordID(e, "
                    << field.record_target_id << "); err != nil {\n"
                    << "\t\t\t\treturn " << wrap_element << "\n\t\t\t}\n";
                break;
            }
            out << "\t\t}\n";
            out << "\t\tif err := a.Finish(); err != nil {\n\t\t\treturn " << wrap << "\n\t\t}\n";
        }
    }
    out << "\t} else if err := r.Absent(" << at << ", " << field.slot_size
        << "); err != nil {\n\t\treturn " << wrap << "\n\t}\n";
}

void render_methods(std::ostringstream& out, const PlannedRecord& record) {
    const std::string& t = record.go_name;
    out << "// MarshalBRF returns the BRF v2 encoding of m.\n";
    out << "func (m *" << t << ") MarshalBRF() ([]byte, error) {\n";
    out << "\treturn m.AppendBRFWithRecordID(nil, " << t << "RecordID)\n}\n\n";

    out << "// AppendBRF appends the BRF v2 encoding of m to b.\n";
    out << "func (m *" << t << ") AppendBRF(b []byte) ([]byte, error) {\n";
    out << "\treturn m.AppendBRFWithRecordID(b, " << t << "RecordID)\n}\n\n";

    out << "// AppendBRFWithRecordID appends m encoded with recordID. Records nested in\n";
    out << "// another schema's records carry the record ID that schema assigns.\n";
    out << "func (m *" << t
        << ") AppendBRFWithRecordID(b []byte, recordID uint32) ([]byte, error) {\n";
    out << "\tbase := len(b)\n";
    out << "\tb = brf.BeginRecord(b, recordID, " << record.fixed_region_size << ")\n";
    for (const PlannedField& field : record.fields) {
        render_encode_field(out, field);
    }
    out << "\treturn brf.FinishRecord(b, base)\n}\n\n";

    out << "// UnmarshalBRF decodes and validates a BRF v2 " << t
        << " record, replacing the\n// contents of m.\n";
    out << "func (m *" << t << ") UnmarshalBRF(data []byte) error {\n";
    out << "\treturn m.UnmarshalBRFWithRecordID(data, " << t << "RecordID)\n}\n\n";

    out << "// UnmarshalBRFWithRecordID decodes a record that must carry recordID.\n";
    out << "func (m *" << t << ") UnmarshalBRFWithRecordID(data []byte, recordID uint32) error {\n";
    out << "\tr, err := brf.NewReader(data, recordID, " << record.fixed_region_size << ", "
        << record.presence_bitmap_size << ", " << record.fields.size() << ")\n";
    out << "\tif err != nil {\n\t\treturn err\n\t}\n";
    out << "\t*m = " << t << "{}\n";
    for (const PlannedField& field : record.fields) {
        render_decode_field(out, field);
    }
    out << "\treturn r.Finish()\n}\n";
}

[[nodiscard]] std::string render_file(const PlannedPackageFile& file) {
    std::ostringstream out;
    out << "// Code generated by quarry-schema-compiler. DO NOT EDIT.\n";
    out << "// Schema namespace: " << (file.namespace_fqn.empty() ? "(root)" : file.namespace_fqn)
        << "\n\n";
    out << "package " << file.package_name << "\n\n";
    std::vector<PlannedImport> imports = file.imports;
    imports.push_back(PlannedImport{"", std::string(kRuntimeImportPath)});
    std::sort(imports.begin(), imports.end(),
              [](const PlannedImport& a, const PlannedImport& b) { return a.path < b.path; });
    if (imports.size() == 1U) {
        out << "import " << go_string_literal(imports.front().path) << "\n\n";
    } else {
        out << "import (\n";
        for (const PlannedImport& import : imports) {
            out << "\t" << (import.alias.empty() ? "" : import.alias + " ")
                << go_string_literal(import.path) << "\n";
        }
        out << ")\n\n";
    }
    out << "// The generated code requires runtime API epoch " << kGeneratedCodeApiVersionGo
        << "; a different runtime\n// fails to compile here. Regenerate the code or use a "
           "matching runtime.\n";
    out << "const _ = uint(brf.GeneratedCodeAPIVersion - " << kGeneratedCodeApiVersionGo << ")\n";
    out << "const _ = uint(" << kGeneratedCodeApiVersionGo << " - brf.GeneratedCodeAPIVersion)\n";
    for (const PlannedEnum& planned : file.enums) {
        out << "\n";
        render_enum(out, planned);
    }
    for (const PlannedRecord& record : file.records) {
        out << "\n";
        render_struct(out, record);
        out << "\n";
        render_methods(out, record);
    }
    return out.str();
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
    std::vector<PlannedPackageFile> files;
    if (!build_plan(schema_ir, options, output_plan, files, result.error_message)) {
        result.success = false;
        return result;
    }
    for (const PlannedPackageFile& file : files) {
        PlannedGeneratedFile planned;
        planned.relative_output_path = file.relative_path;
        result.plan.files.push_back(std::move(planned));
    }
    return result;
}

CodegenResult Backend::generate(const schema_ir::SchemaIrModel& schema_ir,
                                const CodegenOptions& options,
                                const output_planning::OutputPlan* output_plan) const {
    CodegenResult result;
    std::vector<PlannedPackageFile> files;
    if (!build_plan(schema_ir, options, output_plan, files, result.error_message)) {
        result.success = false;
        return result;
    }
    for (const PlannedPackageFile& file : files) {
        GeneratedFile generated;
        generated.path = output_path_for_planned_file(options, file.relative_path);
        generated.content = render_file(file);
        result.files.push_back(std::move(generated));
    }
    return result;
}

} // namespace quarry::compiler::backend_go
