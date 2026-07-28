#include "compiler/backend_c/backend_c.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
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

// --- Planning -----------------------------------------------------------

struct PlannedEnumValue {
    std::string name;
    std::int64_t value = 0;
};

struct PlannedEnum {
    std::string symbol_name;
    std::vector<PlannedEnumValue> values;
};

struct PlannedRecord {
    std::string symbol_name;
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
            if (record_ir.fields_size() > 0) {
                std::ostringstream stream;
                stream << "backend_c: record '" << record_ir.fqn() << "' declares "
                       << record_ir.fields_size()
                       << " field(s); the C backend skeleton does not yet support any field "
                          "type -- fields are implemented in a follow-up PR (see "
                          "docs/design/c-backend.md)";
                error_message = stream.str();
                return false;
            }
            file.records.push_back(
                PlannedRecord{.symbol_name = symbol_prefix + record_ir.name()});
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
    stream << "/* Generated by Quarry (C backend architectural skeleton -- PR-107). */\n";
    stream << "/* This file does not yet declare any encode/decode API. See\n";
    stream << " * docs/design/c-backend.md for the full planned C backend design. */\n";
    stream << "#ifndef " << guard << "\n";
    stream << "#define " << guard << "\n";
    stream << "\n";
    stream << "#include <stdint.h>\n";
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
        stream << "    /* No schema fields are supported by the C backend skeleton yet\n";
        stream << "     * (PR-107). This member exists only to keep the struct valid ISO C\n";
        stream << "     * (C forbids an empty struct body); it is not a schema field and will\n";
        stream << "     * be removed once field support lands. */\n";
        stream << "    uint8_t _reserved;\n";
        stream << "} " << record.symbol_name << "_t;\n";
        stream << "\n";
        stream << "void " << record.symbol_name << "_init(" << record.symbol_name
               << "_t* record);\n";
    }

    stream << "\n";
    stream << "#ifdef __cplusplus\n";
    stream << "}\n";
    stream << "#endif\n";
    stream << "\n";
    stream << "#endif /* " << guard << " */\n";
    return stream.str();
}

[[nodiscard]] std::string render_source(const PlannedNamespaceFile& file) {
    std::ostringstream stream;
    stream << "/* Generated by Quarry (C backend architectural skeleton -- PR-107). */\n";
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
