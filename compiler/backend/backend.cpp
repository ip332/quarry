#include "compiler/backend/backend.hpp"
#include "compiler/backend/generated_code_api_version.hpp"

#include <cstdint>

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quarry::compiler::backend {
namespace {

struct NamedTypeInfo {
    enum class Kind {
        Record,
        Enum,
    };

    Kind kind = Kind::Record;
    std::string namespace_fqn;
    std::string file_path;
    std::string include_path;
    std::string cpp_name;
    const ::quarry::schema_ir::RecordIR* record_ir = nullptr;
    const ::quarry::schema_ir::EnumIR* enum_ir = nullptr;
};

struct TypeCatalog {
    std::map<std::uint64_t, NamedTypeInfo> named_types;
};

struct PrimitiveMapping {
    std::string cpp_type;
    bool needs_cstdint = false;
};

struct TypeLoweringResult {
    std::string cpp_type;
    std::set<std::string> standard_includes;
    std::set<std::string> generated_includes;
    std::vector<std::uint64_t> same_namespace_declaration_dependencies;
};

enum class RuntimeScalarEncoder {
    Unsupported,
    Bool,
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64,
    F32,
    F64,
};

struct RuntimeEnumEncoding {
    std::string cpp_type;
    std::uint8_t width_bytes = 0U;
    std::vector<std::int64_t> valid_values;
};

struct RuntimeRecordEncoding {
    std::string encode_function;
    std::string decode_function;
};

enum class RuntimeVariableEncoding {
    Unsupported,
    String,
    Bytes,
};

struct RuntimeArrayEncoding {
    RuntimeScalarEncoder scalar = RuntimeScalarEncoder::Unsupported;
    std::optional<RuntimeEnumEncoding> enum_encoding;
    std::optional<RuntimeRecordEncoding> record_encoding;
    RuntimeVariableEncoding variable = RuntimeVariableEncoding::Unsupported;
    std::uint32_t max_elements = 0U;
    std::uint32_t max_bytes = 0U;
};

struct RuntimeFieldEncoding {
    RuntimeScalarEncoder scalar = RuntimeScalarEncoder::Unsupported;
    std::optional<RuntimeEnumEncoding> enum_encoding;
    std::optional<RuntimeRecordEncoding> record_encoding;
    std::optional<RuntimeArrayEncoding> array_encoding;
    RuntimeVariableEncoding variable = RuntimeVariableEncoding::Unsupported;
    std::uint32_t max_bytes = 0U;
};

struct FieldPlan {
    const ::quarry::schema_ir::FieldIR* field = nullptr;
    std::string name;
    std::string cpp_type;
    RuntimeFieldEncoding runtime_encoding;
};

struct RecordPlan {
    const ::quarry::schema_ir::RecordIR* record = nullptr;
    std::size_t source_order = 0;
    std::vector<FieldPlan> fields;
    std::vector<std::uint64_t> same_namespace_declaration_dependencies;
};

struct DeclarationPlan {
    enum class Kind {
        Enum,
        Record,
    };

    Kind kind = Kind::Enum;
    const ::quarry::schema_ir::EnumIR* enum_ir = nullptr;
    RecordPlan record;
    std::size_t source_order = 0;
    std::vector<std::uint64_t> same_namespace_declaration_dependencies;
};

struct NamespacePlan {
    const ::quarry::schema_ir::NamespaceIR* namespace_ir = nullptr;
    std::string fqn;
    std::string file_path;
    std::string include_path;
    bool emits_file = false;
    std::set<std::string> standard_includes;
    std::set<std::string> includes;
    std::vector<DeclarationPlan> declarations;
    std::vector<NamespacePlan> children;
};

struct PlannedRenderFile {
    PlannedGeneratedFile file;
    const NamespacePlan* namespace_plan = nullptr;
};

struct RenderGenerationPlan {
    std::vector<PlannedRenderFile> files;
};

[[nodiscard]] std::string indent(std::size_t level) { return std::string(level * 2, ' '); }

[[nodiscard]] std::vector<std::string> namespace_parts(std::string_view fqn) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : fqn) {
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

[[nodiscard]] std::string join_path(const std::vector<std::string>& parts) {
    std::string path;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            path.push_back('/');
        }
        path.append(parts[index]);
    }
    return path;
}

[[nodiscard]] std::string file_stem_for_namespace(const CodegenOptions& options,
                                                  std::string_view namespace_fqn) {
    if (namespace_fqn.empty()) {
        return options.root_file_stem + options.file_extension;
    }
    return join_path(namespace_parts(namespace_fqn)) + options.file_extension;
}

[[nodiscard]] std::string file_path_for_namespace(const CodegenOptions& options,
                                                  std::string_view namespace_fqn) {
    const std::string file_stem = file_stem_for_namespace(options, namespace_fqn);
    if (options.output_directory.empty()) {
        return file_stem;
    }
    return options.output_directory + "/" + file_stem;
}

[[nodiscard]] std::string include_path_for_namespace(const CodegenOptions& options,
                                                     std::string_view namespace_fqn) {
    return file_stem_for_namespace(options, namespace_fqn);
}

[[nodiscard]] std::string cpp_qualified_name(std::string_view namespace_fqn,
                                             std::string_view name) {
    std::string cpp_name = "::";
    if (namespace_fqn.empty()) {
        cpp_name.append(name);
        return cpp_name;
    }

    const std::vector<std::string> parts = namespace_parts(namespace_fqn);
    for (const std::string& part : parts) {
        cpp_name.append(part);
        cpp_name.append("::");
    }
    cpp_name.append(name);
    return cpp_name;
}

[[nodiscard]] std::string cpp_namespace_prefix(std::string_view namespace_fqn) {
    std::string prefix = "::";
    if (namespace_fqn.empty()) {
        return prefix;
    }

    const std::vector<std::string> parts = namespace_parts(namespace_fqn);
    for (const std::string& part : parts) {
        prefix.append(part);
        prefix.append("::");
    }
    return prefix;
}

[[nodiscard]] std::string namespace_comment(std::string_view fqn) {
    if (fqn.empty()) {
        return "// Namespace: <root>\n";
    }
    return "// Namespace: " + std::string(fqn) + "\n";
}

[[nodiscard]] bool emits_records(const NamespacePlan& plan) {
    return std::any_of(plan.declarations.begin(), plan.declarations.end(), [](const auto& decl) {
        return decl.kind == DeclarationPlan::Kind::Record;
    });
}

[[nodiscard]] std::string field_member_name(std::string_view field_name) {
    return std::string(field_name) + "_";
}

void append_namespace_open(std::ostringstream& stream,
                           const std::vector<std::string>& namespace_stack) {
    for (const std::string& part : namespace_stack) {
        stream << "namespace " << part << " {\n";
    }
    if (!namespace_stack.empty()) {
        stream << '\n';
    }
}

void append_namespace_close(std::ostringstream& stream,
                            const std::vector<std::string>& namespace_stack) {
    for (auto it = namespace_stack.rbegin(); it != namespace_stack.rend(); ++it) {
        stream << "} // namespace " << *it << "\n";
    }
}

[[nodiscard]] std::optional<PrimitiveMapping>
primitive_mapping(::quarry::schema_ir::PrimitiveType primitive, std::string& error_message) {
    using ::quarry::schema_ir::PrimitiveType;

    if (primitive == PrimitiveType::PRIMITIVE_TYPE_BOOL) {
        return PrimitiveMapping{.cpp_type = "bool"};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_I8) {
        return PrimitiveMapping{.cpp_type = "std::int8_t", .needs_cstdint = true};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_U8) {
        return PrimitiveMapping{.cpp_type = "std::uint8_t", .needs_cstdint = true};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_I16) {
        return PrimitiveMapping{.cpp_type = "std::int16_t", .needs_cstdint = true};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_U16) {
        return PrimitiveMapping{.cpp_type = "std::uint16_t", .needs_cstdint = true};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_I32) {
        return PrimitiveMapping{.cpp_type = "std::int32_t", .needs_cstdint = true};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_U32) {
        return PrimitiveMapping{.cpp_type = "std::uint32_t", .needs_cstdint = true};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_I64) {
        return PrimitiveMapping{.cpp_type = "std::int64_t", .needs_cstdint = true};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_U64) {
        return PrimitiveMapping{.cpp_type = "std::uint64_t", .needs_cstdint = true};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_F32) {
        return PrimitiveMapping{.cpp_type = "float"};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_F64) {
        return PrimitiveMapping{.cpp_type = "double"};
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_UNSPECIFIED) {
        error_message = "backend codegen encountered an unspecified primitive field type";
        return std::nullopt;
    }

    error_message = "backend codegen encountered an unsupported primitive field type";
    return std::nullopt;
}

[[nodiscard]] RuntimeScalarEncoder
runtime_scalar_encoder(::quarry::schema_ir::PrimitiveType primitive) {
    using ::quarry::schema_ir::PrimitiveType;

    if (primitive == PrimitiveType::PRIMITIVE_TYPE_BOOL) {
        return RuntimeScalarEncoder::Bool;
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_I8) {
        return RuntimeScalarEncoder::I8;
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_U8) {
        return RuntimeScalarEncoder::U8;
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_I16) {
        return RuntimeScalarEncoder::I16;
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_U16) {
        return RuntimeScalarEncoder::U16;
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_I32) {
        return RuntimeScalarEncoder::I32;
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_U32) {
        return RuntimeScalarEncoder::U32;
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_I64) {
        return RuntimeScalarEncoder::I64;
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_U64) {
        return RuntimeScalarEncoder::U64;
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_F32) {
        return RuntimeScalarEncoder::F32;
    }
    if (primitive == PrimitiveType::PRIMITIVE_TYPE_F64) {
        return RuntimeScalarEncoder::F64;
    }
    return RuntimeScalarEncoder::Unsupported;
}

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

[[nodiscard]] std::optional<RuntimeEnumEncoding>
runtime_enum_encoding(const ::quarry::schema_ir::EnumIR& enum_ir, std::string cpp_type) {
    RuntimeEnumEncoding encoding;
    encoding.cpp_type = std::move(cpp_type);
    std::uint64_t max_value = 0U;
    encoding.valid_values.reserve(static_cast<std::size_t>(enum_ir.values_size()));
    for (const ::quarry::schema_ir::EnumValueIR& value : enum_ir.values()) {
        if (value.value() < 0) {
            return std::nullopt;
        }
        const auto unsigned_value = static_cast<std::uint64_t>(value.value());
        max_value = std::max(max_value, unsigned_value);
        encoding.valid_values.push_back(value.value());
    }
    encoding.width_bytes = enum_width_for_max_value(max_value);
    return encoding;
}

void add_record_requirements(std::set<std::string>& standard_includes) {
    standard_includes.insert("<cstddef>");
    standard_includes.insert("<optional>");
    standard_includes.insert("<span>");
    standard_includes.insert("<utility>");
    standard_includes.insert("<vector>");
}

void add_primitive_requirements(TypeLoweringResult& result, const PrimitiveMapping& mapping) {
    if (mapping.needs_cstdint) {
        result.standard_includes.insert("<cstdint>");
    }
}

void merge_type_requirements(TypeLoweringResult& target, const TypeLoweringResult& source) {
    target.standard_includes.insert(source.standard_includes.begin(),
                                    source.standard_includes.end());
    target.generated_includes.insert(source.generated_includes.begin(),
                                     source.generated_includes.end());
    target.same_namespace_declaration_dependencies.insert(
        target.same_namespace_declaration_dependencies.end(),
        source.same_namespace_declaration_dependencies.begin(),
        source.same_namespace_declaration_dependencies.end());
}

[[nodiscard]] const NamedTypeInfo*
lookup_named_type(const TypeCatalog& catalog, std::uint64_t ir_id, std::string& error_message);

[[nodiscard]] TypeLoweringResult
lower_field_type(const ::quarry::schema_ir::FieldType& field_type,
                 const std::string& current_namespace_fqn,
                 const std::map<std::uint64_t, std::size_t>& local_declaration_ids,
                 const TypeCatalog& catalog, std::string& error_message) {
    TypeLoweringResult result;

    switch (field_type.kind_case()) {
    case ::quarry::schema_ir::FieldType::kPrimitive: {
        const std::optional<PrimitiveMapping> mapping =
            primitive_mapping(field_type.primitive(), error_message);
        if (!mapping.has_value()) {
            return {};
        }
        result.cpp_type = mapping->cpp_type;
        add_primitive_requirements(result, *mapping);
        return result;
    }
    case ::quarry::schema_ir::FieldType::kString:
        result.cpp_type = "std::string";
        result.standard_includes.insert("<string>");
        return result;
    case ::quarry::schema_ir::FieldType::kBytes:
        result.cpp_type = "std::vector<std::byte>";
        result.standard_includes.insert("<cstddef>");
        result.standard_includes.insert("<vector>");
        return result;
    case ::quarry::schema_ir::FieldType::kRecord: {
        const NamedTypeInfo* target =
            lookup_named_type(catalog, field_type.record().target_record_ir_id(), error_message);
        if (target == nullptr) {
            return {};
        }
        if (target->kind != NamedTypeInfo::Kind::Record) {
            error_message = "backend codegen expected Schema IR record id " +
                            std::to_string(field_type.record().target_record_ir_id()) +
                            " to refer to a record";
            return {};
        }
        result.cpp_type = target->cpp_name;
        if (target->namespace_fqn == current_namespace_fqn) {
            const auto it = local_declaration_ids.find(field_type.record().target_record_ir_id());
            if (it == local_declaration_ids.end()) {
                error_message = "backend codegen could not order a same-namespace declaration "
                                "dependency";
                return {};
            }
            result.same_namespace_declaration_dependencies.push_back(
                field_type.record().target_record_ir_id());
        } else {
            result.generated_includes.insert(target->include_path);
        }
        return result;
    }
    case ::quarry::schema_ir::FieldType::kEnumType: {
        const NamedTypeInfo* target =
            lookup_named_type(catalog, field_type.enum_type().target_enum_ir_id(), error_message);
        if (target == nullptr) {
            return {};
        }
        if (target->kind != NamedTypeInfo::Kind::Enum) {
            error_message = "backend codegen expected Schema IR enum id " +
                            std::to_string(field_type.enum_type().target_enum_ir_id()) +
                            " to refer to an enum";
            return {};
        }
        result.cpp_type = target->cpp_name;
        if (target->namespace_fqn != current_namespace_fqn) {
            result.generated_includes.insert(target->include_path);
        } else {
            const auto it = local_declaration_ids.find(field_type.enum_type().target_enum_ir_id());
            if (it == local_declaration_ids.end()) {
                error_message = "backend codegen could not order a same-namespace declaration "
                                "dependency";
                return {};
            }
            result.same_namespace_declaration_dependencies.push_back(
                field_type.enum_type().target_enum_ir_id());
        }
        return result;
    }
    case ::quarry::schema_ir::FieldType::kArray: {
        TypeLoweringResult element =
            lower_field_type(field_type.array().element_type(), current_namespace_fqn,
                             local_declaration_ids, catalog, error_message);
        if (!error_message.empty()) {
            return {};
        }
        merge_type_requirements(result, element);
        result.cpp_type = std::move(element.cpp_type);
        result.cpp_type = "std::vector<" + result.cpp_type + ">";
        result.standard_includes.insert("<vector>");
        return result;
    }
    case ::quarry::schema_ir::FieldType::KIND_NOT_SET:
        error_message = "backend codegen encountered a field without a type";
        return {};
    }

    error_message = "backend codegen encountered an unknown field type";
    return {};
}

[[nodiscard]] RuntimeFieldEncoding
lower_runtime_field_encoding(const ::quarry::schema_ir::FieldType& field_type,
                             const TypeCatalog& catalog) {
    RuntimeFieldEncoding encoding;
    switch (field_type.kind_case()) {
    case ::quarry::schema_ir::FieldType::kPrimitive:
        encoding.scalar = runtime_scalar_encoder(field_type.primitive());
        return encoding;
    case ::quarry::schema_ir::FieldType::kEnumType: {
        const auto type_it = catalog.named_types.find(field_type.enum_type().target_enum_ir_id());
        if (type_it == catalog.named_types.end() ||
            type_it->second.kind != NamedTypeInfo::Kind::Enum ||
            type_it->second.enum_ir == nullptr) {
            return encoding;
        }
        encoding.enum_encoding =
            runtime_enum_encoding(*type_it->second.enum_ir, type_it->second.cpp_name);
        return encoding;
    }
    case ::quarry::schema_ir::FieldType::kString:
        encoding.variable = RuntimeVariableEncoding::String;
        encoding.max_bytes = field_type.string().max_bytes();
        return encoding;
    case ::quarry::schema_ir::FieldType::kBytes:
        encoding.variable = RuntimeVariableEncoding::Bytes;
        encoding.max_bytes = field_type.bytes().max_bytes();
        return encoding;
    case ::quarry::schema_ir::FieldType::kRecord: {
        const auto type_it = catalog.named_types.find(field_type.record().target_record_ir_id());
        if (type_it == catalog.named_types.end() ||
            type_it->second.kind != NamedTypeInfo::Kind::Record ||
            type_it->second.record_ir == nullptr) {
            return encoding;
        }
        const std::string namespace_prefix = cpp_namespace_prefix(type_it->second.namespace_fqn);
        encoding.record_encoding = RuntimeRecordEncoding{
            .encode_function = namespace_prefix + "encode_result",
            .decode_function =
                namespace_prefix + "decode_" + type_it->second.record_ir->name() + "_result",
        };
        return encoding;
    }
    case ::quarry::schema_ir::FieldType::kArray: {
        const ::quarry::schema_ir::ArrayType& array_type = field_type.array();
        RuntimeArrayEncoding array_encoding;
        array_encoding.max_elements = array_type.max_elements();
        switch (array_type.element_type().kind_case()) {
        case ::quarry::schema_ir::FieldType::kPrimitive:
            array_encoding.scalar = runtime_scalar_encoder(array_type.element_type().primitive());
            if (array_encoding.scalar != RuntimeScalarEncoder::Unsupported) {
                encoding.array_encoding = std::move(array_encoding);
            }
            return encoding;
        case ::quarry::schema_ir::FieldType::kEnumType: {
            const auto type_it =
                catalog.named_types.find(array_type.element_type().enum_type().target_enum_ir_id());
            if (type_it == catalog.named_types.end() ||
                type_it->second.kind != NamedTypeInfo::Kind::Enum ||
                type_it->second.enum_ir == nullptr) {
                return encoding;
            }
            array_encoding.enum_encoding =
                runtime_enum_encoding(*type_it->second.enum_ir, type_it->second.cpp_name);
            if (array_encoding.enum_encoding.has_value()) {
                encoding.array_encoding = std::move(array_encoding);
            }
            return encoding;
        }
        case ::quarry::schema_ir::FieldType::kString:
            array_encoding.variable = RuntimeVariableEncoding::String;
            array_encoding.max_bytes = array_type.element_type().string().max_bytes();
            encoding.array_encoding = std::move(array_encoding);
            return encoding;
        case ::quarry::schema_ir::FieldType::kBytes:
            array_encoding.variable = RuntimeVariableEncoding::Bytes;
            array_encoding.max_bytes = array_type.element_type().bytes().max_bytes();
            encoding.array_encoding = std::move(array_encoding);
            return encoding;
        case ::quarry::schema_ir::FieldType::kRecord: {
            const auto type_it =
                catalog.named_types.find(array_type.element_type().record().target_record_ir_id());
            if (type_it == catalog.named_types.end() ||
                type_it->second.kind != NamedTypeInfo::Kind::Record ||
                type_it->second.record_ir == nullptr) {
                return encoding;
            }
            const std::string namespace_prefix = cpp_namespace_prefix(type_it->second.namespace_fqn);
            array_encoding.record_encoding = RuntimeRecordEncoding{
                .encode_function = namespace_prefix + "encode_result",
                .decode_function =
                    namespace_prefix + "decode_" + type_it->second.record_ir->name() + "_result",
            };
            encoding.array_encoding = std::move(array_encoding);
            return encoding;
        }
        case ::quarry::schema_ir::FieldType::kArray:
        case ::quarry::schema_ir::FieldType::KIND_NOT_SET:
            return encoding;
        }
        return encoding;
    }
    case ::quarry::schema_ir::FieldType::KIND_NOT_SET:
        return encoding;
    }

    return encoding;
}

[[nodiscard]] bool
render_field_validation_statements(const ::quarry::schema_ir::FieldType& field_type,
                                   std::string_view value_expression, std::size_t indent_level,
                                   std::size_t element_depth, std::ostringstream& stream,
                                   std::string& error_message) {
    switch (field_type.kind_case()) {
    case ::quarry::schema_ir::FieldType::kPrimitive:
    case ::quarry::schema_ir::FieldType::kRecord:
    case ::quarry::schema_ir::FieldType::kEnumType:
        return true;
    case ::quarry::schema_ir::FieldType::kString: {
        const std::uint32_t max_bytes = field_type.string().max_bytes();
        if (max_bytes > 0) {
            stream << indent(indent_level) << "if (" << value_expression << ".size() > "
                   << max_bytes << ") {\n";
            stream << indent(indent_level + 1) << "return false;\n";
            stream << indent(indent_level) << "}\n";
        }
        return true;
    }
    case ::quarry::schema_ir::FieldType::kBytes: {
        const std::uint32_t max_bytes = field_type.bytes().max_bytes();
        if (max_bytes > 0) {
            stream << indent(indent_level) << "if (" << value_expression << ".size() > "
                   << max_bytes << ") {\n";
            stream << indent(indent_level + 1) << "return false;\n";
            stream << indent(indent_level) << "}\n";
        }
        return true;
    }
    case ::quarry::schema_ir::FieldType::kArray: {
        const std::uint32_t max_count = field_type.array().max_elements();
        const std::string element_name = "element_" + std::to_string(element_depth);
        stream << indent(indent_level) << "if (" << value_expression << ".size() > " << max_count
               << ") {\n";
        stream << indent(indent_level + 1) << "return false;\n";
        stream << indent(indent_level) << "}\n";
        stream << indent(indent_level) << "for (const auto& " << element_name << " : "
               << value_expression << ") {\n";
        if (!render_field_validation_statements(field_type.array().element_type(), element_name,
                                                indent_level + 1, element_depth + 1, stream,
                                                error_message)) {
            return false;
        }
        stream << indent(indent_level) << "}\n";
        return true;
    }
    case ::quarry::schema_ir::FieldType::KIND_NOT_SET:
        error_message = "backend codegen encountered a field without a type";
        return false;
    }

    error_message = "backend codegen encountered an unknown field type";
    return false;
}

[[nodiscard]] bool render_field_validation_function(const FieldPlan& field,
                                                    std::size_t indent_level,
                                                    std::ostringstream& stream,
                                                    std::string& error_message) {
    if (field.field == nullptr) {
        error_message = "backend codegen encountered a missing field IR";
        return false;
    }
    stream << indent(indent_level) << "static bool validate_" << field.name << "(const "
           << field.cpp_type << "& value) {\n";
    if (!render_field_validation_statements(field.field->type(), "value", indent_level + 1, 0,
                                            stream, error_message)) {
        return false;
    }
    stream << indent(indent_level + 1) << "return true;\n";
    stream << indent(indent_level) << "}\n";
    return true;
}

[[nodiscard]] const NamedTypeInfo*
lookup_named_type(const TypeCatalog& catalog, std::uint64_t ir_id, std::string& error_message) {
    const auto it = catalog.named_types.find(ir_id);
    if (it == catalog.named_types.end()) {
        error_message =
            "backend codegen could not resolve Schema IR type id " + std::to_string(ir_id);
        return nullptr;
    }
    return &it->second;
}

[[nodiscard]] bool order_declarations_topologically(std::vector<DeclarationPlan>& declarations,
                                                    std::string& error_message) {
    std::map<std::uint64_t, std::size_t> declaration_index_by_id;
    for (std::size_t index = 0; index < declarations.size(); ++index) {
        if (declarations[index].kind == DeclarationPlan::Kind::Record) {
            declaration_index_by_id.emplace(declarations[index].record.record->ir_id(), index);
        } else {
            declaration_index_by_id.emplace(declarations[index].enum_ir->ir_id(), index);
        }
    }

    std::vector<std::size_t> indegree(declarations.size(), 0);
    std::vector<std::vector<std::size_t>> dependents(declarations.size());
    for (std::size_t index = 0; index < declarations.size(); ++index) {
        const std::vector<std::uint64_t>* dependencies = nullptr;
        if (declarations[index].kind == DeclarationPlan::Kind::Record) {
            dependencies = &declarations[index].record.same_namespace_declaration_dependencies;
        }
        if (dependencies == nullptr) {
            continue;
        }

        for (std::uint64_t dependency_id : *dependencies) {
            const auto dependency_it = declaration_index_by_id.find(dependency_id);
            if (dependency_it == declaration_index_by_id.end()) {
                error_message = "backend codegen could not resolve a same-namespace declaration "
                                "dependency";
                return false;
            }
            const std::size_t dependency_index = dependency_it->second;
            ++indegree[index];
            dependents[dependency_index].push_back(index);
        }
    }

    std::set<std::pair<std::size_t, std::size_t>> ready;
    for (std::size_t index = 0; index < declarations.size(); ++index) {
        if (indegree[index] == 0) {
            ready.emplace(declarations[index].source_order, index);
        }
    }

    std::vector<DeclarationPlan> ordered_declarations;
    ordered_declarations.reserve(declarations.size());
    while (!ready.empty()) {
        const auto [ignored_order, index] = *ready.begin();
        (void)ignored_order;
        ready.erase(ready.begin());
        ordered_declarations.push_back(declarations[index]);

        for (std::size_t dependent_index : dependents[index]) {
            if (--indegree[dependent_index] == 0) {
                ready.emplace(declarations[dependent_index].source_order, dependent_index);
            }
        }
    }

    if (ordered_declarations.size() != declarations.size()) {
        error_message = "backend codegen detected a cycle in declaration dependencies";
        return false;
    }

    declarations = std::move(ordered_declarations);
    return true;
}

void collect_named_types(const ::quarry::schema_ir::NamespaceIR& ns,
                         const CodegenOptions& options, TypeCatalog& catalog) {
    const std::string file_path = file_path_for_namespace(options, ns.fqn());
    const std::string include_path = include_path_for_namespace(options, ns.fqn());

    for (int index = 0; index < ns.records_size(); ++index) {
        const ::quarry::schema_ir::RecordIR& record = ns.records(index);
        catalog.named_types.emplace(record.ir_id(),
                                    NamedTypeInfo{
                                        .kind = NamedTypeInfo::Kind::Record,
                                        .namespace_fqn = ns.fqn(),
                                        .file_path = file_path,
                                        .include_path = include_path,
                                        .cpp_name = cpp_qualified_name(ns.fqn(), record.name()),
                                        .record_ir = &record,
                                        .enum_ir = nullptr,
                                    });
    }

    for (int index = 0; index < ns.enums_size(); ++index) {
        const ::quarry::schema_ir::EnumIR& enum_ir = ns.enums(index);
        catalog.named_types.emplace(enum_ir.ir_id(),
                                    NamedTypeInfo{
                                        .kind = NamedTypeInfo::Kind::Enum,
                                        .namespace_fqn = ns.fqn(),
                                        .file_path = file_path,
                                        .include_path = include_path,
                                        .cpp_name = cpp_qualified_name(ns.fqn(), enum_ir.name()),
                                        .record_ir = nullptr,
                                        .enum_ir = &enum_ir,
                                    });
    }

    for (int index = 0; index < ns.namespaces_size(); ++index) {
        collect_named_types(ns.namespaces(index), options, catalog);
    }
}

[[nodiscard]] bool analyze_namespace(const ::quarry::schema_ir::NamespaceIR& ns,
                                     const CodegenOptions& options, const TypeCatalog& catalog,
                                     NamespacePlan& plan, std::string& error_message) {
    plan.namespace_ir = &ns;
    plan.fqn = ns.fqn();
    plan.file_path = file_path_for_namespace(options, ns.fqn());
    plan.include_path = file_stem_for_namespace(options, ns.fqn());
    plan.emits_file = ns.records_size() > 0 || ns.enums_size() > 0;
    if (ns.enums_size() > 0) {
        plan.standard_includes.insert("<cstdint>");
    }

    if (plan.emits_file) {
        std::map<std::uint64_t, std::size_t> local_declaration_ids;
        plan.declarations.reserve(static_cast<std::size_t>(ns.enums_size() + ns.records_size()));

        for (int index = 0; index < ns.enums_size(); ++index) {
            const ::quarry::schema_ir::EnumIR& enum_ir = ns.enums(index);
            plan.declarations.push_back(DeclarationPlan{
                .kind = DeclarationPlan::Kind::Enum,
                .enum_ir = &enum_ir,
                .record = {},
                .source_order = static_cast<std::size_t>(index),
                .same_namespace_declaration_dependencies = {},
            });
        }

        for (int index = 0; index < ns.records_size(); ++index) {
            const ::quarry::schema_ir::RecordIR& record = ns.records(index);
            add_record_requirements(plan.standard_includes);
            plan.includes.insert("quarry/runtime/binary_record.hpp");
            plan.declarations.push_back(DeclarationPlan{
                .kind = DeclarationPlan::Kind::Record,
                .enum_ir = nullptr,
                .record =
                    RecordPlan{
                        .record = &record,
                        .source_order = static_cast<std::size_t>(index),
                        .fields = {},
                        .same_namespace_declaration_dependencies = {},
                    },
                .source_order = static_cast<std::size_t>(ns.enums_size() + index),
                .same_namespace_declaration_dependencies = {},
            });
        }

        for (std::size_t index = 0; index < plan.declarations.size(); ++index) {
            const DeclarationPlan& declaration_plan = plan.declarations[index];
            if (declaration_plan.kind == DeclarationPlan::Kind::Enum) {
                local_declaration_ids.emplace(declaration_plan.enum_ir->ir_id(), index);
            } else {
                local_declaration_ids.emplace(declaration_plan.record.record->ir_id(), index);
            }
        }

        for (DeclarationPlan& declaration_plan : plan.declarations) {
            if (declaration_plan.kind != DeclarationPlan::Kind::Record) {
                continue;
            }

            RecordPlan& record_plan = declaration_plan.record;
            record_plan.fields.reserve(static_cast<std::size_t>(record_plan.record->fields_size()));
            for (int field_index = 0; field_index < record_plan.record->fields_size();
                 ++field_index) {
                const ::quarry::schema_ir::FieldIR& field =
                    record_plan.record->fields(field_index);
                const TypeLoweringResult lowered = lower_field_type(
                    field.type(), ns.fqn(), local_declaration_ids, catalog, error_message);
                if (!error_message.empty()) {
                    return false;
                }
                const RuntimeFieldEncoding runtime_encoding =
                    lower_runtime_field_encoding(field.type(), catalog);
                record_plan.fields.push_back(
                    FieldPlan{.field = &field,
                              .name = field.name(),
                              .cpp_type = lowered.cpp_type,
                              .runtime_encoding = runtime_encoding});
                plan.standard_includes.insert(lowered.standard_includes.begin(),
                                              lowered.standard_includes.end());
                plan.includes.insert(lowered.generated_includes.begin(),
                                     lowered.generated_includes.end());
                record_plan.same_namespace_declaration_dependencies.insert(
                    record_plan.same_namespace_declaration_dependencies.end(),
                    lowered.same_namespace_declaration_dependencies.begin(),
                    lowered.same_namespace_declaration_dependencies.end());
            }
        }

        if (!order_declarations_topologically(plan.declarations, error_message)) {
            return false;
        }
    }

    plan.children.reserve(static_cast<std::size_t>(ns.namespaces_size()));
    for (int index = 0; index < ns.namespaces_size(); ++index) {
        NamespacePlan child_plan;
        if (!analyze_namespace(ns.namespaces(index), options, catalog, child_plan, error_message)) {
            return false;
        }
        plan.children.push_back(std::move(child_plan));
    }

    return true;
}

void collect_planned_files(const NamespacePlan& plan, RenderGenerationPlan& generation_plan) {
    if (plan.emits_file) {
        generation_plan.files.push_back(PlannedRenderFile{
            .file =
                PlannedGeneratedFile{
                    .language = GeneratedLanguage::Cpp,
                    .role = GeneratedArtifactRole::Header,
                    .relative_output_path = plan.include_path,
                    .generated_include_path = plan.include_path,
                },
            .namespace_plan = &plan,
        });
    }
    for (const NamespacePlan& child : plan.children) {
        collect_planned_files(child, generation_plan);
    }
}

[[nodiscard]] bool validate_generation_plan(const RenderGenerationPlan& generation_plan,
                                            std::string& error_message) {
    std::set<std::string> output_paths;
    for (const PlannedRenderFile& file : generation_plan.files) {
        if (file.file.language != GeneratedLanguage::Cpp) {
            error_message = "backend codegen planned an unsupported generated language";
            return false;
        }
        if (file.file.role != GeneratedArtifactRole::Header) {
            error_message = "backend codegen planned an unsupported generated artifact role";
            return false;
        }
        if (file.namespace_plan == nullptr) {
            error_message = "backend codegen planned an output without render context";
            return false;
        }
        if (!output_paths.insert(file.file.relative_output_path).second) {
            error_message =
                "backend codegen planned duplicate output path: " + file.file.relative_output_path;
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool detect_file_cycles(const RenderGenerationPlan& generation_plan,
                                      std::string& error_message) {
    std::map<std::string, std::size_t> index_by_path;
    for (std::size_t index = 0; index < generation_plan.files.size(); ++index) {
        index_by_path.emplace(generation_plan.files[index].file.generated_include_path, index);
    }

    enum class VisitState { Unvisited, Visiting, Visited };
    std::vector<VisitState> state(generation_plan.files.size(), VisitState::Unvisited);

    auto visit = [&](auto&& self, std::size_t index) -> bool {
        state[index] = VisitState::Visiting;
        const NamespacePlan& namespace_plan = *generation_plan.files[index].namespace_plan;
        for (const std::string& include_path : namespace_plan.includes) {
            const auto it = index_by_path.find(include_path);
            if (it == index_by_path.end()) {
                continue;
            }

            const std::size_t dependency_index = it->second;
            if (state[dependency_index] == VisitState::Visiting) {
                error_message = "backend codegen detected a namespace cycle between '" +
                                generation_plan.files[index].file.generated_include_path +
                                "' and '" +
                                generation_plan.files[dependency_index].file
                                    .generated_include_path +
                                "'";
                return false;
            }
            if (state[dependency_index] == VisitState::Unvisited) {
                if (!self(self, dependency_index)) {
                    return false;
                }
            }
        }
        state[index] = VisitState::Visited;
        return true;
    };

    for (std::size_t index = 0; index < generation_plan.files.size(); ++index) {
        if (state[index] == VisitState::Unvisited) {
            if (!visit(visit, index)) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] GenerationPlan to_public_generation_plan(
    const RenderGenerationPlan& render_generation_plan) {
    GenerationPlan generation_plan;
    generation_plan.files.reserve(render_generation_plan.files.size());
    for (const PlannedRenderFile& file : render_generation_plan.files) {
        generation_plan.files.push_back(file.file);
    }
    return generation_plan;
}

[[nodiscard]] bool build_render_generation_plan(const schema_ir::SchemaIrModel& schema_ir,
                                                const CodegenOptions& options,
                                                NamespacePlan& root_plan,
                                                RenderGenerationPlan& generation_plan,
                                                std::string& error_message) {
    TypeCatalog catalog;
    collect_named_types(schema_ir.root_namespace(), options, catalog);

    if (!analyze_namespace(schema_ir.root_namespace(), options, catalog, root_plan,
                           error_message)) {
        return false;
    }

    collect_planned_files(root_plan, generation_plan);
    if (!validate_generation_plan(generation_plan, error_message)) {
        return false;
    }
    if (!detect_file_cycles(generation_plan, error_message)) {
        return false;
    }

    return true;
}

[[nodiscard]] std::string render_enum_definition(const ::quarry::schema_ir::EnumIR& enum_ir,
                                                 std::size_t indent_level) {
    std::ostringstream stream;
    stream << indent(indent_level) << "enum class " << enum_ir.name() << " : std::int64_t"
           << " {\n";
    for (int index = 0; index < enum_ir.values_size(); ++index) {
        const ::quarry::schema_ir::EnumValueIR& value = enum_ir.values(index);
        stream << indent(indent_level + 1) << value.name() << " = " << value.value() << ",\n";
    }
    stream << indent(indent_level) << "};\n";
    return stream.str();
}

[[nodiscard]] bool render_record_builder_definition(const RecordPlan& record_plan,
                                                    std::size_t indent_level,
                                                    std::ostringstream& stream,
                                                    std::string& error_message) {
    const std::string& record_name = record_plan.record->name();
    stream << indent(indent_level) << "class " << record_name << "Builder {\n";
    stream << indent(indent_level) << "public:\n";

    if (record_plan.fields.empty()) {
        stream << indent(indent_level + 1) << record_name << " build() const {\n";
        stream << indent(indent_level + 2) << "return " << record_name << "{};\n";
        stream << indent(indent_level + 1) << "}\n";
        stream << indent(indent_level) << "};\n";
        return true;
    }

    for (const FieldPlan& field : record_plan.fields) {
        stream << indent(indent_level + 1) << "bool has_" << field.name << "() const {\n";
        stream << indent(indent_level + 2) << "return " << field_member_name(field.name)
               << ".has_value();\n";
        stream << indent(indent_level + 1) << "}\n";
    }
    if (!record_plan.fields.empty()) {
        stream << '\n';
    }

    for (const FieldPlan& field : record_plan.fields) {
        stream << indent(indent_level + 1) << "bool set_" << field.name << "(const "
               << field.cpp_type << "& value) {\n";
        stream << indent(indent_level + 2) << "if (!validate_" << field.name << "(value)) {\n";
        stream << indent(indent_level + 3) << "return false;\n";
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2) << field_member_name(field.name) << " = value;\n";
        stream << indent(indent_level + 2) << "return true;\n";
        stream << indent(indent_level + 1) << "}\n";
    }

    if (!record_plan.fields.empty()) {
        stream << '\n';
    }

    stream << indent(indent_level + 1) << record_name << " build() const {\n";
    stream << indent(indent_level + 2) << record_name << " value;\n";
    for (const FieldPlan& field : record_plan.fields) {
        stream << indent(indent_level + 2) << "value." << field_member_name(field.name) << " = "
               << field_member_name(field.name) << ";\n";
    }
    stream << indent(indent_level + 2) << "return value;\n";
    stream << indent(indent_level + 1) << "}\n";

    if (!record_plan.fields.empty()) {
        stream << '\n';
        stream << indent(indent_level) << "private:\n";
        for (const FieldPlan& field : record_plan.fields) {
            if (!render_field_validation_function(field, indent_level + 1, stream, error_message)) {
                return false;
            }
        }

        stream << '\n';
        for (const FieldPlan& field : record_plan.fields) {
            stream << indent(indent_level + 1) << "std::optional<" << field.cpp_type << "> "
                   << field_member_name(field.name) << ";\n";
        }
    }

    stream << indent(indent_level) << "};\n";
    return true;
}

[[nodiscard]] std::string runtime_append_function(RuntimeScalarEncoder encoder) {
    switch (encoder) {
    case RuntimeScalarEncoder::Bool:
        return "append_bool";
    case RuntimeScalarEncoder::I8:
        return "append_i8";
    case RuntimeScalarEncoder::U8:
        return "append_u8";
    case RuntimeScalarEncoder::I16:
        return "append_i16";
    case RuntimeScalarEncoder::U16:
        return "append_u16";
    case RuntimeScalarEncoder::I32:
        return "append_i32";
    case RuntimeScalarEncoder::U32:
        return "append_u32";
    case RuntimeScalarEncoder::I64:
        return "append_i64";
    case RuntimeScalarEncoder::U64:
        return "append_u64";
    case RuntimeScalarEncoder::F32:
        return "append_f32";
    case RuntimeScalarEncoder::F64:
        return "append_f64";
    case RuntimeScalarEncoder::Unsupported:
        return {};
    }
    return {};
}

[[nodiscard]] std::string runtime_read_function(RuntimeScalarEncoder encoder) {
    switch (encoder) {
    case RuntimeScalarEncoder::Bool:
        return "read_bool";
    case RuntimeScalarEncoder::I8:
        return "read_i8";
    case RuntimeScalarEncoder::U8:
        return "read_u8";
    case RuntimeScalarEncoder::I16:
        return "read_i16";
    case RuntimeScalarEncoder::U16:
        return "read_u16";
    case RuntimeScalarEncoder::I32:
        return "read_i32";
    case RuntimeScalarEncoder::U32:
        return "read_u32";
    case RuntimeScalarEncoder::I64:
        return "read_i64";
    case RuntimeScalarEncoder::U64:
        return "read_u64";
    case RuntimeScalarEncoder::F32:
        return "read_f32";
    case RuntimeScalarEncoder::F64:
        return "read_f64";
    case RuntimeScalarEncoder::Unsupported:
        return {};
    }
    return {};
}

[[nodiscard]] std::uint8_t runtime_scalar_width_bytes(RuntimeScalarEncoder encoder) {
    switch (encoder) {
    case RuntimeScalarEncoder::Bool:
    case RuntimeScalarEncoder::I8:
    case RuntimeScalarEncoder::U8:
        return 1U;
    case RuntimeScalarEncoder::I16:
    case RuntimeScalarEncoder::U16:
        return 2U;
    case RuntimeScalarEncoder::I32:
    case RuntimeScalarEncoder::U32:
    case RuntimeScalarEncoder::F32:
        return 4U;
    case RuntimeScalarEncoder::I64:
    case RuntimeScalarEncoder::U64:
    case RuntimeScalarEncoder::F64:
        return 8U;
    case RuntimeScalarEncoder::Unsupported:
        return 0U;
    }
    return 0U;
}

[[nodiscard]] std::string enum_append_function(std::uint8_t width_bytes) {
    if (width_bytes == 1U) {
        return "append_u8";
    }
    if (width_bytes == 2U) {
        return "append_u16";
    }
    if (width_bytes == 4U) {
        return "append_u32";
    }
    return "append_u64";
}

[[nodiscard]] std::string enum_read_function(std::uint8_t width_bytes) {
    if (width_bytes == 1U) {
        return "read_u8";
    }
    if (width_bytes == 2U) {
        return "read_u16";
    }
    if (width_bytes == 4U) {
        return "read_u32";
    }
    return "read_u64";
}

[[nodiscard]] std::string enum_unsigned_type(std::uint8_t width_bytes) {
    if (width_bytes == 1U) {
        return "std::uint8_t";
    }
    if (width_bytes == 2U) {
        return "std::uint16_t";
    }
    if (width_bytes == 4U) {
        return "std::uint32_t";
    }
    return "std::uint64_t";
}

// Emits a fresh (non-propagated) encode failure return with a single-frame
// path. `array_index_expression` is the literal C++ text for the
// PathElement's array_index (e.g. "std::nullopt" or
// "static_cast<std::uint32_t>(element_index)").
void render_encode_failure_return(std::ostringstream& stream, std::size_t indent_level,
                                  std::string_view error_expression, unsigned int field_index,
                                  std::string_view array_index_expression) {
    stream << indent(indent_level)
           << "return ::quarry::runtime::encode_failure<std::vector<std::byte>>("
           << error_expression << ", "
           << "{{.field_index = " << field_index << "U, .array_index = " << array_index_expression
           << "}});\n";
}

// Emits the identical loop-open shared by every render_array_field_encoding
// element-kind branch: an indexed for-loop plus the element reference.
void render_array_encode_loop_open(std::ostringstream& stream, std::size_t indent_level,
                                   std::string_view field_name) {
    stream << indent(indent_level) << "for (std::size_t element_index = 0U; element_index < value."
           << field_name << "()->size(); ++element_index) {\n";
    stream << indent(indent_level + 1) << "const auto& element = (*value." << field_name
           << "())[element_index];\n";
}

void render_unsupported_present_field_encoding(const FieldPlan& field, std::size_t indent_level,
                                               std::ostringstream& stream) {
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (value.has_" << field.name << "()) {\n";
    render_encode_failure_return(stream, indent_level + 1,
                                 "::quarry::runtime::EncodeError::unsupported_field_type", field_index,
                                 "std::nullopt");
    stream << indent(indent_level) << "}\n";
}

void render_scalar_field_encoding(const FieldPlan& field, std::size_t indent_level,
                                  std::ostringstream& stream) {
    const std::string append_function = runtime_append_function(field.runtime_encoding.scalar);
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (value.has_" << field.name << "()) {\n";
    stream << indent(indent_level + 1) << "std::vector<std::byte> field_bytes;\n";
    stream << indent(indent_level + 1) << "if (!::quarry::runtime::" << append_function
           << "(field_bytes, *value." << field.name << "())) {\n";
    render_encode_failure_return(stream, indent_level + 2, "::quarry::runtime::EncodeError::overflow",
                                 field_index, "std::nullopt");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "fields.push_back(::quarry::runtime::FieldBytes{\n";
    stream << indent(indent_level + 2) << ".field_index = "
           << static_cast<unsigned int>(field.field->field_index()) << "U,\n";
    stream << indent(indent_level + 2) << ".bytes = std::move(field_bytes),\n";
    stream << indent(indent_level + 1) << "});\n";
    stream << indent(indent_level) << "}\n";
}

void render_enum_field_encoding(const FieldPlan& field, const RuntimeEnumEncoding& enum_encoding,
                                std::size_t indent_level, std::ostringstream& stream) {
    const std::string append_function = enum_append_function(enum_encoding.width_bytes);
    const std::string unsigned_type = enum_unsigned_type(enum_encoding.width_bytes);
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (value.has_" << field.name << "()) {\n";
    stream << indent(indent_level + 1) << "const auto enum_numeric = static_cast<std::int64_t>(*value."
           << field.name << "());\n";
    stream << indent(indent_level + 1) << "if (!(";
    for (std::size_t index = 0; index < enum_encoding.valid_values.size(); ++index) {
        if (index > 0) {
            stream << " || ";
        }
        stream << "enum_numeric == " << enum_encoding.valid_values[index];
    }
    if (enum_encoding.valid_values.empty()) {
        stream << "false";
    }
    stream << ")) {\n";
    render_encode_failure_return(stream, indent_level + 2,
                                 "::quarry::runtime::EncodeError::unknown_enum_value", field_index,
                                 "std::nullopt");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "std::vector<std::byte> field_bytes;\n";
    stream << indent(indent_level + 1) << "::quarry::runtime::" << append_function
           << "(field_bytes, static_cast<" << unsigned_type << ">(enum_numeric));\n";
    stream << indent(indent_level + 1) << "fields.push_back(::quarry::runtime::FieldBytes{\n";
    stream << indent(indent_level + 2) << ".field_index = "
           << static_cast<unsigned int>(field.field->field_index()) << "U,\n";
    stream << indent(indent_level + 2) << ".bytes = std::move(field_bytes),\n";
    stream << indent(indent_level + 1) << "});\n";
    stream << indent(indent_level) << "}\n";
}

void render_string_field_encoding(const FieldPlan& field, std::size_t indent_level,
                                  std::ostringstream& stream) {
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (value.has_" << field.name << "()) {\n";
    stream << indent(indent_level + 1) << "if (value." << field.name << "()->size() > "
           << field.runtime_encoding.max_bytes << "U) {\n";
    render_encode_failure_return(stream, indent_level + 2,
                                 "::quarry::runtime::EncodeError::bounds_exceeded", field_index,
                                 "std::nullopt");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "std::vector<std::byte> field_bytes;\n";
    stream << indent(indent_level + 1)
           << "if (!::quarry::runtime::append_string_utf8(field_bytes, *value."
           << field.name << "())) {\n";
    render_encode_failure_return(stream, indent_level + 2,
                                 "::quarry::runtime::EncodeError::invalid_utf8", field_index,
                                 "std::nullopt");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "fields.push_back(::quarry::runtime::FieldBytes{\n";
    stream << indent(indent_level + 2) << ".field_index = "
           << static_cast<unsigned int>(field.field->field_index()) << "U,\n";
    stream << indent(indent_level + 2) << ".bytes = std::move(field_bytes),\n";
    stream << indent(indent_level + 1) << "});\n";
    stream << indent(indent_level) << "}\n";
}

void render_bytes_field_encoding(const FieldPlan& field, std::size_t indent_level,
                                 std::ostringstream& stream) {
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (value.has_" << field.name << "()) {\n";
    stream << indent(indent_level + 1) << "if (value." << field.name << "()->size() > "
           << field.runtime_encoding.max_bytes << "U) {\n";
    render_encode_failure_return(stream, indent_level + 2,
                                 "::quarry::runtime::EncodeError::bounds_exceeded", field_index,
                                 "std::nullopt");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "std::vector<std::byte> field_bytes;\n";
    stream << indent(indent_level + 1) << "::quarry::runtime::append_bytes(field_bytes, "
           << "std::span<const std::byte>(value." << field.name << "()->data(), value."
           << field.name << "()->size()));\n";
    stream << indent(indent_level + 1) << "fields.push_back(::quarry::runtime::FieldBytes{\n";
    stream << indent(indent_level + 2) << ".field_index = "
           << static_cast<unsigned int>(field.field->field_index()) << "U,\n";
    stream << indent(indent_level + 2) << ".bytes = std::move(field_bytes),\n";
    stream << indent(indent_level + 1) << "});\n";
    stream << indent(indent_level) << "}\n";
}

void render_record_field_encoding(const FieldPlan& field, const RuntimeRecordEncoding& record_encoding,
                                  std::size_t indent_level, std::ostringstream& stream) {
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (value.has_" << field.name << "()) {\n";
    stream << indent(indent_level + 1) << "auto field_bytes = " << record_encoding.encode_function
           << "(*value." << field.name << "());\n";
    stream << indent(indent_level + 1) << "if (!field_bytes.has_value()) {\n";
    stream << indent(indent_level + 2) << "auto nested_path = std::move(field_bytes.path);\n";
    stream << indent(indent_level + 2) << "nested_path.push_back(::quarry::runtime::PathElement{\n";
    stream << indent(indent_level + 3) << ".field_index = " << field_index << "U,\n";
    stream << indent(indent_level + 3) << ".array_index = std::nullopt,\n";
    stream << indent(indent_level + 2) << "});\n";
    stream << indent(indent_level + 2)
           << "return ::quarry::runtime::encode_failure<std::vector<std::byte>>("
           << "field_bytes.error, std::move(nested_path));\n";
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "fields.push_back(::quarry::runtime::FieldBytes{\n";
    stream << indent(indent_level + 2) << ".field_index = "
           << static_cast<unsigned int>(field.field->field_index()) << "U,\n";
    stream << indent(indent_level + 2) << ".bytes = std::move(*field_bytes.value),\n";
    stream << indent(indent_level + 1) << "});\n";
    stream << indent(indent_level) << "}\n";
}

void render_array_field_encoding(const FieldPlan& field, const RuntimeArrayEncoding& array_encoding,
                                 std::size_t indent_level, std::ostringstream& stream) {
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (value.has_" << field.name << "()) {\n";
    stream << indent(indent_level + 1) << "if (value." << field.name << "()->size() > "
           << array_encoding.max_elements << "U) {\n";
    render_encode_failure_return(stream, indent_level + 2,
                                 "::quarry::runtime::EncodeError::bounds_exceeded", field_index,
                                 "std::nullopt");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "std::vector<std::byte> field_bytes;\n";
    stream << indent(indent_level + 1) << "::quarry::runtime::append_varuint(field_bytes, "
           << "value." << field.name << "()->size());\n";

    if (array_encoding.record_encoding.has_value()) {
        const RuntimeRecordEncoding& record_encoding = *array_encoding.record_encoding;
        render_array_encode_loop_open(stream, indent_level + 1, field.name);
        stream << indent(indent_level + 2) << "auto element_bytes = "
               << record_encoding.encode_function << "(element);\n";
        stream << indent(indent_level + 2) << "if (!element_bytes.has_value()) {\n";
        stream << indent(indent_level + 3) << "auto nested_path = std::move(element_bytes.path);\n";
        stream << indent(indent_level + 3)
               << "nested_path.push_back(::quarry::runtime::PathElement{\n";
        stream << indent(indent_level + 4) << ".field_index = " << field_index << "U,\n";
        stream << indent(indent_level + 4)
               << ".array_index = static_cast<std::uint32_t>(element_index),\n";
        stream << indent(indent_level + 3) << "});\n";
        stream << indent(indent_level + 3)
               << "return ::quarry::runtime::encode_failure<std::vector<std::byte>>("
               << "element_bytes.error, std::move(nested_path));\n";
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2)
               << "::quarry::runtime::append_varuint(field_bytes, "
               << "element_bytes.value->size());\n";
        stream << indent(indent_level + 2)
               << "::quarry::runtime::append_bytes(field_bytes, "
               << "std::span<const std::byte>(element_bytes.value->data(), "
               << "element_bytes.value->size()));\n";
        stream << indent(indent_level + 1) << "}\n";
    } else if (array_encoding.variable == RuntimeVariableEncoding::String) {
        render_array_encode_loop_open(stream, indent_level + 1, field.name);
        stream << indent(indent_level + 2) << "if (element.size() > " << array_encoding.max_bytes
               << "U) {\n";
        render_encode_failure_return(stream, indent_level + 3,
                                     "::quarry::runtime::EncodeError::bounds_exceeded", field_index,
                                     "static_cast<std::uint32_t>(element_index)");
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2)
               << "::quarry::runtime::append_varuint(field_bytes, element.size());\n";
        stream << indent(indent_level + 2)
               << "if (!::quarry::runtime::append_string_utf8(field_bytes, element)) {\n";
        render_encode_failure_return(stream, indent_level + 3,
                                     "::quarry::runtime::EncodeError::invalid_utf8", field_index,
                                     "static_cast<std::uint32_t>(element_index)");
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 1) << "}\n";
    } else if (array_encoding.variable == RuntimeVariableEncoding::Bytes) {
        render_array_encode_loop_open(stream, indent_level + 1, field.name);
        stream << indent(indent_level + 2) << "if (element.size() > " << array_encoding.max_bytes
               << "U) {\n";
        render_encode_failure_return(stream, indent_level + 3,
                                     "::quarry::runtime::EncodeError::bounds_exceeded", field_index,
                                     "static_cast<std::uint32_t>(element_index)");
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2)
               << "::quarry::runtime::append_varuint(field_bytes, element.size());\n";
        stream << indent(indent_level + 2) << "::quarry::runtime::append_bytes(field_bytes, "
               << "std::span<const std::byte>(element.data(), element.size()));\n";
        stream << indent(indent_level + 1) << "}\n";
    } else if (array_encoding.enum_encoding.has_value()) {
        const RuntimeEnumEncoding& enum_encoding = *array_encoding.enum_encoding;
        const std::string append_function = enum_append_function(enum_encoding.width_bytes);
        const std::string unsigned_type = enum_unsigned_type(enum_encoding.width_bytes);
        render_array_encode_loop_open(stream, indent_level + 1, field.name);
        stream << indent(indent_level + 2)
               << "const auto enum_numeric = static_cast<std::int64_t>(element);\n";
        stream << indent(indent_level + 2) << "if (!(";
        for (std::size_t index = 0; index < enum_encoding.valid_values.size(); ++index) {
            if (index > 0) {
                stream << " || ";
            }
            stream << "enum_numeric == " << enum_encoding.valid_values[index];
        }
        if (enum_encoding.valid_values.empty()) {
            stream << "false";
        }
        stream << ")) {\n";
        render_encode_failure_return(stream, indent_level + 3,
                                     "::quarry::runtime::EncodeError::unknown_enum_value", field_index,
                                     "static_cast<std::uint32_t>(element_index)");
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2) << "::quarry::runtime::" << append_function
               << "(field_bytes, static_cast<" << unsigned_type << ">(enum_numeric));\n";
        stream << indent(indent_level + 1) << "}\n";
    } else {
        const std::string append_function = runtime_append_function(array_encoding.scalar);
        render_array_encode_loop_open(stream, indent_level + 1, field.name);
        stream << indent(indent_level + 2) << "if (!::quarry::runtime::" << append_function
               << "(field_bytes, element)) {\n";
        render_encode_failure_return(stream, indent_level + 3,
                                     "::quarry::runtime::EncodeError::overflow", field_index,
                                     "static_cast<std::uint32_t>(element_index)");
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 1) << "}\n";
    }

    stream << indent(indent_level + 1) << "fields.push_back(::quarry::runtime::FieldBytes{\n";
    stream << indent(indent_level + 2) << ".field_index = "
           << static_cast<unsigned int>(field.field->field_index()) << "U,\n";
    stream << indent(indent_level + 2) << ".bytes = std::move(field_bytes),\n";
    stream << indent(indent_level + 1) << "});\n";
    stream << indent(indent_level) << "}\n";
}

void render_field_encoding(const FieldPlan& field, std::size_t indent_level,
                           std::ostringstream& stream) {
    if (field.runtime_encoding.enum_encoding.has_value()) {
        render_enum_field_encoding(field, *field.runtime_encoding.enum_encoding, indent_level,
                                   stream);
        return;
    }
    if (field.runtime_encoding.variable == RuntimeVariableEncoding::String) {
        render_string_field_encoding(field, indent_level, stream);
        return;
    }
    if (field.runtime_encoding.variable == RuntimeVariableEncoding::Bytes) {
        render_bytes_field_encoding(field, indent_level, stream);
        return;
    }
    if (field.runtime_encoding.record_encoding.has_value()) {
        render_record_field_encoding(field, *field.runtime_encoding.record_encoding, indent_level,
                                     stream);
        return;
    }
    if (field.runtime_encoding.array_encoding.has_value()) {
        render_array_field_encoding(field, *field.runtime_encoding.array_encoding, indent_level,
                                    stream);
        return;
    }
    if (field.runtime_encoding.scalar != RuntimeScalarEncoder::Unsupported) {
        render_scalar_field_encoding(field, indent_level, stream);
        return;
    }
    render_unsupported_present_field_encoding(field, indent_level, stream);
}

void render_record_encoder_definition(const RecordPlan& record_plan, std::size_t indent_level,
                                      std::ostringstream& stream) {
    const std::string& record_name = record_plan.record->name();
    stream << indent(indent_level)
           << "::quarry::runtime::EncodeResult<std::vector<std::byte>> encode_result(const "
           << record_name << "& value) {\n";
    stream << indent(indent_level + 1) << "std::vector<::quarry::runtime::FieldBytes> fields;\n";
    if (!record_plan.fields.empty()) {
        stream << indent(indent_level + 1) << "fields.reserve(" << record_plan.fields.size()
               << "U);\n";
        for (const FieldPlan& field : record_plan.fields) {
            render_field_encoding(field, indent_level + 1, stream);
        }
    } else {
        stream << indent(indent_level + 1) << "(void)value;\n";
    }
    stream << indent(indent_level + 1) << "return ::quarry::runtime::encode_record_result("
           << record_plan.record->record_id() << "U, fields);\n";
    stream << indent(indent_level) << "}\n";
    stream << "\n\n";
    stream << indent(indent_level) << "std::optional<std::vector<std::byte>> encode(const "
           << record_name << "& value) {\n";
    stream << indent(indent_level + 1) << "auto encoded = encode_result(value);\n";
    stream << indent(indent_level + 1) << "if (!encoded.value.has_value()) {\n";
    stream << indent(indent_level + 2) << "return std::nullopt;\n";
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "return std::move(encoded.value);\n";
    stream << indent(indent_level) << "}\n";
}

// Emits a fresh (non-propagated) decode failure return with a single-frame
// path. `array_index_expression` is the literal C++ text for the
// PathElement's array_index (e.g. "std::nullopt" or
// "static_cast<std::uint32_t>(index)"). `offset_expression` is the literal
// C++ text for the byte offset (e.g. "field->field_offset" or
// "field->field_offset + element_offset"); this helper does not compute or
// interpret offsets, only emits whatever expression the caller supplies.
// Nested-child failure forwarding (which propagates an existing child
// error/path/offset rather than raising a fresh one) does not use this
// helper.
void render_decode_failure_return(std::ostringstream& stream, std::size_t indent_level,
                                  std::string_view record_name, std::string_view error_expression,
                                  unsigned int field_index, std::string_view array_index_expression,
                                  std::string_view offset_expression) {
    stream << indent(indent_level) << "return ::quarry::runtime::decode_failure<" << record_name
           << ">(" << error_expression << ", "
           << "{{.field_index = " << field_index << "U, .array_index = " << array_index_expression
           << "}}, " << offset_expression << ");\n";
}

void render_unsupported_present_field_decoding(const FieldPlan& field, std::string_view record_name,
                                               std::size_t indent_level, std::ostringstream& stream) {
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (const auto* field = "
           << "::quarry::runtime::find_field(*parsed.record, " << field_index
           << "U); field != nullptr) {\n";
    render_decode_failure_return(stream, indent_level + 1, record_name,
                                 "::quarry::runtime::DecodeError::unsupported_field_type",
                                 field_index, "std::nullopt", "field->field_offset");
    stream << indent(indent_level) << "}\n";
}

void render_scalar_field_decoding(const FieldPlan& field, std::string_view record_name,
                                  std::size_t indent_level, std::ostringstream& stream) {
    const std::string read_function = runtime_read_function(field.runtime_encoding.scalar);
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (const auto* field = "
           << "::quarry::runtime::find_field(*parsed.record, " << field_index
           << "U); field != nullptr) {\n";
    stream << indent(indent_level + 1) << "const auto decoded = ::quarry::runtime::"
           << read_function << "(field->bytes);\n";
    stream << indent(indent_level + 1) << "if (!decoded.value.has_value()) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name, "decoded.error", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "if (!builder.set_" << field.name
           << "(*decoded.value)) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name,
                                 "::quarry::runtime::DecodeError::bounds_exceeded", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level) << "}\n";
}

void render_enum_field_decoding(const FieldPlan& field, const RuntimeEnumEncoding& enum_encoding,
                                std::string_view record_name, std::size_t indent_level,
                                std::ostringstream& stream) {
    const std::string read_function = enum_read_function(enum_encoding.width_bytes);
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (const auto* field = "
           << "::quarry::runtime::find_field(*parsed.record, " << field_index
           << "U); field != nullptr) {\n";
    stream << indent(indent_level + 1) << "const auto decoded = ::quarry::runtime::"
           << read_function << "(field->bytes);\n";
    stream << indent(indent_level + 1) << "if (!decoded.value.has_value()) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name, "decoded.error", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1)
           << "const auto enum_numeric = static_cast<std::int64_t>(*decoded.value);\n";
    stream << indent(indent_level + 1) << "if (!(";
    for (std::size_t index = 0; index < enum_encoding.valid_values.size(); ++index) {
        if (index > 0) {
            stream << " || ";
        }
        stream << "enum_numeric == " << enum_encoding.valid_values[index];
    }
    if (enum_encoding.valid_values.empty()) {
        stream << "false";
    }
    stream << ")) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name,
                                 "::quarry::runtime::DecodeError::unknown_enum_value", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "if (!builder.set_" << field.name
           << "(static_cast<" << enum_encoding.cpp_type << ">(enum_numeric))) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name,
                                 "::quarry::runtime::DecodeError::bounds_exceeded", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level) << "}\n";
}

void render_string_field_decoding(const FieldPlan& field, std::string_view record_name,
                                  std::size_t indent_level, std::ostringstream& stream) {
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (const auto* field = "
           << "::quarry::runtime::find_field(*parsed.record, " << field_index
           << "U); field != nullptr) {\n";
    stream << indent(indent_level + 1) << "if (field->bytes.size() > "
           << field.runtime_encoding.max_bytes << "U) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name,
                                 "::quarry::runtime::DecodeError::bounds_exceeded", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1)
           << "const auto decoded = ::quarry::runtime::read_string_utf8(field->bytes);\n";
    stream << indent(indent_level + 1) << "if (!decoded.value.has_value()) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name, "decoded.error", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "if (!builder.set_" << field.name
           << "(*decoded.value)) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name,
                                 "::quarry::runtime::DecodeError::bounds_exceeded", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level) << "}\n";
}

void render_bytes_field_decoding(const FieldPlan& field, std::string_view record_name,
                                 std::size_t indent_level, std::ostringstream& stream) {
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (const auto* field = "
           << "::quarry::runtime::find_field(*parsed.record, " << field_index
           << "U); field != nullptr) {\n";
    stream << indent(indent_level + 1) << "if (field->bytes.size() > "
           << field.runtime_encoding.max_bytes << "U) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name,
                                 "::quarry::runtime::DecodeError::bounds_exceeded", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1)
           << "const auto decoded = ::quarry::runtime::read_bytes(field->bytes);\n";
    stream << indent(indent_level + 1) << "if (!decoded.value.has_value()) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name, "decoded.error", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "if (!builder.set_" << field.name
           << "(*decoded.value)) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name,
                                 "::quarry::runtime::DecodeError::bounds_exceeded", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level) << "}\n";
}

void render_record_field_decoding(const FieldPlan& field,
                                  const RuntimeRecordEncoding& record_encoding,
                                  std::string_view record_name, std::size_t indent_level,
                                  std::ostringstream& stream) {
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    stream << indent(indent_level) << "if (const auto* field = "
           << "::quarry::runtime::find_field(*parsed.record, " << field_index
           << "U); field != nullptr) {\n";
    stream << indent(indent_level + 1) << "auto decoded = "
           << record_encoding.decode_function << "(field->bytes);\n";
    stream << indent(indent_level + 1) << "if (!decoded.value.has_value()) {\n";
    stream << indent(indent_level + 2) << "auto nested_path = std::move(decoded.path);\n";
    stream << indent(indent_level + 2) << "nested_path.push_back(::quarry::runtime::PathElement{\n";
    stream << indent(indent_level + 3) << ".field_index = " << field_index << "U,\n";
    stream << indent(indent_level + 3) << ".array_index = std::nullopt,\n";
    stream << indent(indent_level + 2) << "});\n";
    stream << indent(indent_level + 2)
           << "const std::optional<std::uint64_t> nested_offset = decoded.byte_offset.has_value()\n";
    stream << indent(indent_level + 3)
           << "? std::optional<std::uint64_t>(field->field_offset + *decoded.byte_offset)\n";
    stream << indent(indent_level + 3) << ": std::nullopt;\n";
    stream << indent(indent_level + 2) << "return ::quarry::runtime::decode_failure<"
           << record_name
           << ">(decoded.error, std::move(nested_path), nested_offset);\n";
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "if (!builder.set_" << field.name
           << "(*decoded.value)) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name,
                                 "::quarry::runtime::DecodeError::bounds_exceeded", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level) << "}\n";
}

void render_array_field_decoding(const FieldPlan& field, const RuntimeArrayEncoding& array_encoding,
                                 std::string_view record_name, std::size_t indent_level,
                                 std::ostringstream& stream) {
    const unsigned int field_index = static_cast<unsigned int>(field.field->field_index());
    const bool is_string_array = array_encoding.variable == RuntimeVariableEncoding::String;
    const bool is_bytes_array = array_encoding.variable == RuntimeVariableEncoding::Bytes;
    const bool is_enum_array = array_encoding.enum_encoding.has_value();
    const bool is_record_array = array_encoding.record_encoding.has_value();
    const std::string read_function =
        is_enum_array ? enum_read_function(array_encoding.enum_encoding->width_bytes)
                      : runtime_read_function(array_encoding.scalar);
    const std::uint8_t element_width =
        is_enum_array ? array_encoding.enum_encoding->width_bytes
                      : runtime_scalar_width_bytes(array_encoding.scalar);
    stream << indent(indent_level) << "if (const auto* field = "
           << "::quarry::runtime::find_field(*parsed.record, " << field_index
           << "U); field != nullptr) {\n";
    stream << indent(indent_level + 1) << "std::size_t element_offset = 0U;\n";
    stream << indent(indent_level + 1)
           << "const auto decoded_count = ::quarry::runtime::read_varuint(field->bytes, "
           << "element_offset);\n";
    stream << indent(indent_level + 1) << "if (!decoded_count.value.has_value() || "
           << "*decoded_count.value > " << array_encoding.max_elements << "U) {\n";
    stream << indent(indent_level + 2) << "if (!decoded_count.value.has_value()) {\n";
    render_decode_failure_return(stream, indent_level + 3, record_name, "decoded_count.error",
                                 field_index, "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 2) << "}\n";
    render_decode_failure_return(stream, indent_level + 2, record_name,
                                 "::quarry::runtime::DecodeError::bounds_exceeded", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1)
           << "const auto element_count = static_cast<std::size_t>(*decoded_count.value);\n";
    stream << indent(indent_level + 1) << field.cpp_type << " elements;\n";
    stream << indent(indent_level + 1) << "elements.reserve(element_count);\n";

    if (is_record_array) {
        const RuntimeRecordEncoding& record_encoding = *array_encoding.record_encoding;
        stream << indent(indent_level + 1)
               << "for (std::size_t index = 0U; index < element_count; ++index) {\n";
        stream << indent(indent_level + 2) << "const std::size_t element_length_start = "
               << "element_offset;\n";
        stream << indent(indent_level + 2)
               << "const auto decoded_length = ::quarry::runtime::read_varuint("
               << "field->bytes, element_offset);\n";
        stream << indent(indent_level + 2)
               << "if (!decoded_length.value.has_value()) {\n";
        render_decode_failure_return(stream, indent_level + 3, record_name, "decoded_length.error",
                                     field_index, "static_cast<std::uint32_t>(index)",
                                     "field->field_offset + element_length_start");
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2)
               << "if (*decoded_length.value > field->bytes.size() - element_offset) {\n";
        render_decode_failure_return(stream, indent_level + 3, record_name,
                                     "::quarry::runtime::DecodeError::invalid_field_length",
                                     field_index, "static_cast<std::uint32_t>(index)",
                                     "field->field_offset + element_offset");
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2)
               << "const auto element_length = static_cast<std::size_t>(*decoded_length.value);\n";
        stream << indent(indent_level + 2) << "const auto element_bytes = "
               << "field->bytes.subspan(element_offset, element_length);\n";
        stream << indent(indent_level + 2) << "auto decoded = "
               << record_encoding.decode_function << "(element_bytes);\n";
        stream << indent(indent_level + 2) << "if (!decoded.value.has_value()) {\n";
        stream << indent(indent_level + 3) << "auto nested_path = std::move(decoded.path);\n";
        stream << indent(indent_level + 3)
               << "nested_path.push_back(::quarry::runtime::PathElement{\n";
        stream << indent(indent_level + 4) << ".field_index = " << field_index << "U,\n";
        stream << indent(indent_level + 4)
               << ".array_index = static_cast<std::uint32_t>(index),\n";
        stream << indent(indent_level + 3) << "});\n";
        stream << indent(indent_level + 3)
               << "const std::optional<std::uint64_t> nested_offset = "
               << "decoded.byte_offset.has_value()\n";
        stream << indent(indent_level + 4)
               << "? std::optional<std::uint64_t>(field->field_offset + element_offset + "
               << "*decoded.byte_offset)\n";
        stream << indent(indent_level + 4) << ": std::nullopt;\n";
        stream << indent(indent_level + 3) << "return ::quarry::runtime::decode_failure<"
               << record_name
               << ">(decoded.error, std::move(nested_path), nested_offset);\n";
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2) << "elements.push_back(*decoded.value);\n";
        stream << indent(indent_level + 2) << "element_offset += element_length;\n";
        stream << indent(indent_level + 1) << "}\n";
        stream << indent(indent_level + 1) << "if (element_offset != field->bytes.size()) {\n";
        render_decode_failure_return(stream, indent_level + 2, record_name,
                                     "::quarry::runtime::DecodeError::invalid_field_length",
                                     field_index, "std::nullopt", "field->field_offset + element_offset");
        stream << indent(indent_level + 1) << "}\n";
    } else if (is_string_array || is_bytes_array) {
        stream << indent(indent_level + 1)
               << "for (std::size_t index = 0U; index < element_count; ++index) {\n";
        stream << indent(indent_level + 2) << "const std::size_t element_length_start = "
               << "element_offset;\n";
        stream << indent(indent_level + 2)
               << "const auto decoded_length = ::quarry::runtime::read_varuint("
               << "field->bytes, element_offset);\n";
        stream << indent(indent_level + 2)
               << "if (!decoded_length.value.has_value() || *decoded_length.value > "
               << array_encoding.max_bytes << "U) {\n";
        stream << indent(indent_level + 3) << "if (!decoded_length.value.has_value()) {\n";
        render_decode_failure_return(stream, indent_level + 4, record_name, "decoded_length.error",
                                     field_index, "static_cast<std::uint32_t>(index)",
                                     "field->field_offset + element_length_start");
        stream << indent(indent_level + 3) << "}\n";
        render_decode_failure_return(stream, indent_level + 3, record_name,
                                     "::quarry::runtime::DecodeError::bounds_exceeded", field_index,
                                     "static_cast<std::uint32_t>(index)",
                                     "field->field_offset + element_length_start");
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2)
               << "const auto element_length = static_cast<std::size_t>(*decoded_length.value);\n";
        stream << indent(indent_level + 2)
               << "if (element_length > field->bytes.size() - element_offset) {\n";
        render_decode_failure_return(stream, indent_level + 3, record_name,
                                     "::quarry::runtime::DecodeError::invalid_field_length",
                                     field_index, "static_cast<std::uint32_t>(index)",
                                     "field->field_offset + element_offset");
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2) << "const auto element_bytes = "
               << "field->bytes.subspan(element_offset, element_length);\n";
        if (is_string_array) {
            stream << indent(indent_level + 2)
                   << "const auto decoded = ::quarry::runtime::read_string_utf8("
                   << "element_bytes);\n";
        } else {
            stream << indent(indent_level + 2)
                   << "const auto decoded = ::quarry::runtime::read_bytes(element_bytes);\n";
        }
        stream << indent(indent_level + 2) << "if (!decoded.value.has_value()) {\n";
        render_decode_failure_return(stream, indent_level + 3, record_name, "decoded.error",
                                     field_index, "static_cast<std::uint32_t>(index)",
                                     "field->field_offset + element_offset");
        stream << indent(indent_level + 2) << "}\n";
        stream << indent(indent_level + 2) << "elements.push_back(std::move(*decoded.value));\n";
        stream << indent(indent_level + 2) << "element_offset += element_length;\n";
        stream << indent(indent_level + 1) << "}\n";
        stream << indent(indent_level + 1) << "if (element_offset != field->bytes.size()) {\n";
        render_decode_failure_return(stream, indent_level + 2, record_name,
                                     "::quarry::runtime::DecodeError::invalid_field_length",
                                     field_index, "std::nullopt", "field->field_offset + element_offset");
        stream << indent(indent_level + 1) << "}\n";
    } else {
        stream << indent(indent_level + 1) << "const std::size_t element_width = "
               << static_cast<unsigned int>(element_width) << "U;\n";
        stream << indent(indent_level + 1)
               << "const std::size_t remaining_bytes = field->bytes.size() - element_offset;\n";
        stream << indent(indent_level + 1)
               << "if (element_width == 0U || element_count > remaining_bytes / element_width || "
               << "remaining_bytes != element_count * element_width) {\n";
        render_decode_failure_return(stream, indent_level + 2, record_name,
                                     "::quarry::runtime::DecodeError::invalid_field_length",
                                     field_index, "std::nullopt", "field->field_offset");
        stream << indent(indent_level + 1) << "}\n";
        stream << indent(indent_level + 1)
               << "for (std::size_t index = 0U; index < element_count; ++index) {\n";
        stream << indent(indent_level + 2) << "const auto decoded = ::quarry::runtime::"
               << read_function << "(field->bytes.subspan(element_offset, element_width));\n";
        stream << indent(indent_level + 2) << "if (!decoded.value.has_value()) {\n";
        render_decode_failure_return(stream, indent_level + 3, record_name, "decoded.error",
                                     field_index, "static_cast<std::uint32_t>(index)",
                                     "field->field_offset + element_offset");
        stream << indent(indent_level + 2) << "}\n";
        if (is_enum_array) {
            const RuntimeEnumEncoding& enum_encoding = *array_encoding.enum_encoding;
            stream << indent(indent_level + 2)
                   << "const auto enum_numeric = static_cast<std::int64_t>(*decoded.value);\n";
            stream << indent(indent_level + 2) << "if (!(";
            for (std::size_t index = 0; index < enum_encoding.valid_values.size(); ++index) {
                if (index > 0) {
                    stream << " || ";
                }
                stream << "enum_numeric == " << enum_encoding.valid_values[index];
            }
            if (enum_encoding.valid_values.empty()) {
                stream << "false";
            }
            stream << ")) {\n";
            render_decode_failure_return(stream, indent_level + 3, record_name,
                                         "::quarry::runtime::DecodeError::unknown_enum_value",
                                         field_index, "static_cast<std::uint32_t>(index)",
                                         "field->field_offset + element_offset");
            stream << indent(indent_level + 2) << "}\n";
            stream << indent(indent_level + 2) << "elements.push_back(static_cast<"
                   << enum_encoding.cpp_type << ">(enum_numeric));\n";
        } else {
            stream << indent(indent_level + 2) << "elements.push_back(*decoded.value);\n";
        }
        stream << indent(indent_level + 2) << "element_offset += element_width;\n";
        stream << indent(indent_level + 1) << "}\n";
    }
    stream << indent(indent_level + 1) << "if (!builder.set_" << field.name
           << "(std::move(elements))) {\n";
    render_decode_failure_return(stream, indent_level + 2, record_name,
                                 "::quarry::runtime::DecodeError::bounds_exceeded", field_index,
                                 "std::nullopt", "field->field_offset");
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level) << "}\n";
}

void render_field_decoding(const FieldPlan& field, std::string_view record_name,
                           std::size_t indent_level,
                           std::ostringstream& stream) {
    if (field.runtime_encoding.enum_encoding.has_value()) {
        render_enum_field_decoding(field, *field.runtime_encoding.enum_encoding, record_name,
                                   indent_level,
                                   stream);
        return;
    }
    if (field.runtime_encoding.variable == RuntimeVariableEncoding::String) {
        render_string_field_decoding(field, record_name, indent_level, stream);
        return;
    }
    if (field.runtime_encoding.variable == RuntimeVariableEncoding::Bytes) {
        render_bytes_field_decoding(field, record_name, indent_level, stream);
        return;
    }
    if (field.runtime_encoding.record_encoding.has_value()) {
        render_record_field_decoding(field, *field.runtime_encoding.record_encoding, record_name,
                                     indent_level, stream);
        return;
    }
    if (field.runtime_encoding.array_encoding.has_value()) {
        render_array_field_decoding(field, *field.runtime_encoding.array_encoding, record_name,
                                    indent_level,
                                    stream);
        return;
    }
    if (field.runtime_encoding.scalar != RuntimeScalarEncoder::Unsupported) {
        render_scalar_field_decoding(field, record_name, indent_level, stream);
        return;
    }
    render_unsupported_present_field_decoding(field, record_name, indent_level, stream);
}

void render_record_decoder_definition(const RecordPlan& record_plan, std::size_t indent_level,
                                      std::ostringstream& stream) {
    const std::string& record_name = record_plan.record->name();
    stream << indent(indent_level) << "::quarry::runtime::DecodeResult<" << record_name
           << "> decode_" << record_name << "_result(std::span<const std::byte> input) {\n";
    stream << indent(indent_level + 1)
           << "const auto parsed = ::quarry::runtime::parse_record(input);\n";
    stream << indent(indent_level + 1) << "if (!parsed.record.has_value()) {\n";
    stream << indent(indent_level + 2) << "return ::quarry::runtime::decode_failure<"
           << record_name << ">(parsed.error, {}, parsed.offset);\n";
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "if (parsed.record->record_id != "
           << record_plan.record->record_id() << "U) {\n";
    stream << indent(indent_level + 2) << "return ::quarry::runtime::decode_failure<"
           << record_name << ">(::quarry::runtime::DecodeError::unexpected_record_id, {}, 0U);\n";
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << record_name << "Builder builder;\n";
    for (const FieldPlan& field : record_plan.fields) {
        render_field_decoding(field, record_name, indent_level + 1, stream);
    }
    stream << indent(indent_level + 1) << "return ::quarry::runtime::decoded_value<"
           << record_name << ">(builder.build());\n";
    stream << indent(indent_level) << "}\n";
    stream << "\n\n";
    stream << indent(indent_level) << "std::optional<" << record_name
           << "> decode_" << record_name << "(std::span<const std::byte> input) {\n";
    stream << indent(indent_level + 1) << "auto decoded = decode_" << record_name
           << "_result(input);\n";
    stream << indent(indent_level + 1) << "if (!decoded.value.has_value()) {\n";
    stream << indent(indent_level + 2) << "return std::nullopt;\n";
    stream << indent(indent_level + 1) << "}\n";
    stream << indent(indent_level + 1) << "return std::move(decoded.value);\n";
    stream << indent(indent_level) << "}\n";
}

[[nodiscard]] bool render_record_definition(const RecordPlan& record_plan, std::size_t indent_level,
                                            std::ostringstream& stream,
                                            std::string& error_message) {
    const std::string& record_name = record_plan.record->name();
    if (record_plan.fields.empty()) {
        stream << indent(indent_level) << "struct " << record_name << " {};\n\n";
        if (!render_record_builder_definition(record_plan, indent_level, stream, error_message)) {
            return false;
        }
        stream << '\n';
        render_record_encoder_definition(record_plan, indent_level, stream);
        stream << "\n\n";
        render_record_decoder_definition(record_plan, indent_level, stream);
        return true;
    }

    stream << indent(indent_level) << "struct " << record_name << " {\n";
    stream << indent(indent_level + 1) << record_name << "() = default;\n\n";
    for (const FieldPlan& field : record_plan.fields) {
        stream << indent(indent_level + 1) << "bool has_" << field.name << "() const {\n";
        stream << indent(indent_level + 2) << "return " << field_member_name(field.name)
               << ".has_value();\n";
        stream << indent(indent_level + 1) << "}\n";
        stream << indent(indent_level + 1) << "const " << field.cpp_type << "* " << field.name
               << "() const {\n";
        stream << indent(indent_level + 2) << "return " << field_member_name(field.name) << " ? &*"
               << field_member_name(field.name) << " : nullptr;\n";
        stream << indent(indent_level + 1) << "}\n";
    }
    stream << '\n' << indent(indent_level) << "private:\n";
    stream << indent(indent_level + 1) << "friend class " << record_name << "Builder;\n";
    for (const FieldPlan& field : record_plan.fields) {
        stream << indent(indent_level + 1) << "std::optional<" << field.cpp_type << "> "
               << field_member_name(field.name) << ";\n";
    }
    stream << indent(indent_level) << "};\n\n";

    if (!render_record_builder_definition(record_plan, indent_level, stream, error_message)) {
        return false;
    }
    stream << '\n';
    render_record_encoder_definition(record_plan, indent_level, stream);
    stream << "\n\n";
    render_record_decoder_definition(record_plan, indent_level, stream);
    return true;
}

[[nodiscard]] bool render_namespace_file(const NamespacePlan& plan, std::string& output,
                                         std::string& error_message) {
    std::ostringstream stream;
    stream << "// Generated by Quarry.\n";
    stream << namespace_comment(plan.fqn);
    stream << '\n';

    for (const std::string& include : plan.standard_includes) {
        stream << "#include " << include << "\n";
    }
    if (!plan.standard_includes.empty() && !plan.includes.empty()) {
        stream << '\n';
    }
    for (const std::string& include_path : plan.includes) {
        stream << "#include \"" << include_path << "\"\n";
    }
    if (!plan.standard_includes.empty() || !plan.includes.empty()) {
        stream << '\n';
    }
    if (emits_records(plan)) {
        stream << "static_assert(::quarry::runtime::kGeneratedCodeApiVersion == "
               << kGeneratedCodeApiVersion << "U,\n";
        stream << "              \"Generated Quarry code is incompatible with the installed "
                  "Quarry runtime. Regenerate the code using a compatible "
                  "quarry-schema-compiler release.\");\n";
        stream << '\n';
    }

    const std::vector<std::string> parts = namespace_parts(plan.fqn);
    append_namespace_open(stream, parts);

    bool wrote_declaration = false;
    for (std::size_t index = 0; index < plan.declarations.size(); ++index) {
        const DeclarationPlan& declaration = plan.declarations[index];
        if (declaration.kind == DeclarationPlan::Kind::Enum) {
            stream << render_enum_definition(*declaration.enum_ir, parts.size());
        } else {
            if (!render_record_definition(declaration.record, parts.size(), stream,
                                          error_message)) {
                return false;
            }
        }
        wrote_declaration = true;
        if (index + 1 < plan.declarations.size()) {
            stream << '\n';
        }
    }

    if (wrote_declaration && !parts.empty()) {
        stream << '\n';
    }

    append_namespace_close(stream, parts);

    output = stream.str();
    if (!output.empty() && output.back() != '\n') {
        output.push_back('\n');
    }

    return true;
}

[[nodiscard]] bool render_generation_plan(const RenderGenerationPlan& generation_plan,
                                          const CodegenOptions& options,
                                          std::vector<GeneratedFile>& files,
                                          std::string& error_message) {
    files.reserve(generation_plan.files.size());
    for (const PlannedRenderFile& planned_file : generation_plan.files) {
        GeneratedFile file;
        file.path =
            output_path_for_planned_file(options, planned_file.file.relative_output_path);
        if (!render_namespace_file(*planned_file.namespace_plan, file.content, error_message)) {
            return false;
        }
        files.push_back(std::move(file));
    }

    return true;
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

    NamespacePlan root_plan;
    RenderGenerationPlan generation_plan;
    std::string error_message;
    if (!build_render_generation_plan(schema_ir, options, root_plan, generation_plan,
                                      error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    result.plan = to_public_generation_plan(generation_plan);
    return result;
}

CodegenResult Backend::generate(const schema_ir::SchemaIrModel& schema_ir,
                                const CodegenOptions& options) const {
    CodegenResult result;

    NamespacePlan root_plan;
    RenderGenerationPlan generation_plan;
    std::string error_message;
    if (!build_render_generation_plan(schema_ir, options, root_plan, generation_plan,
                                      error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    std::vector<GeneratedFile> files;
    if (!render_generation_plan(generation_plan, options, files, error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    result.files = std::move(files);
    return result;
}

} // namespace quarry::compiler::backend
