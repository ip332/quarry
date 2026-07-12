#include "compiler/backend/backend.hpp"

#include <cstdint>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace breadcrumbs::compiler::backend {
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
};

struct TypeCatalog {
    std::map<std::uint64_t, NamedTypeInfo> named_types;
};

struct PrimitiveMapping {
    std::string cpp_type;
    bool needs_cstdint = false;
};

struct RecordPlan {
    const ::breadcrumbs::schema_ir::RecordIR* record = nullptr;
    std::size_t source_order = 0;
    std::vector<std::uint64_t> same_namespace_declaration_dependencies;
    std::set<std::string> includes;
    bool needs_cstdint = false;
};

struct DeclarationPlan {
    enum class Kind {
        Enum,
        Record,
    };

    Kind kind = Kind::Enum;
    const ::breadcrumbs::schema_ir::EnumIR* enum_ir = nullptr;
    RecordPlan record;
    std::size_t source_order = 0;
    std::vector<std::uint64_t> same_namespace_declaration_dependencies;
};

struct NamespacePlan {
    const ::breadcrumbs::schema_ir::NamespaceIR* namespace_ir = nullptr;
    std::string fqn;
    std::string file_path;
    std::string include_path;
    bool emits_file = false;
    bool needs_cstdint = false;
    std::set<std::string> includes;
    std::vector<DeclarationPlan> declarations;
    std::vector<NamespacePlan> children;
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

[[nodiscard]] std::string namespace_comment(std::string_view fqn) {
    if (fqn.empty()) {
        return "// Namespace: <root>\n";
    }
    return "// Namespace: " + std::string(fqn) + "\n";
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
primitive_mapping(::breadcrumbs::schema_ir::PrimitiveType primitive, std::string& error_message) {
    using ::breadcrumbs::schema_ir::PrimitiveType;

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

[[nodiscard]] bool
analyze_field_type(const ::breadcrumbs::schema_ir::FieldType& field_type,
                   const std::string& current_namespace_fqn,
                   const std::map<std::uint64_t, std::size_t>& local_declaration_ids,
                   const TypeCatalog& catalog, RecordPlan& plan, std::string& error_message) {
    switch (field_type.kind_case()) {
    case ::breadcrumbs::schema_ir::FieldType::kPrimitive: {
        const std::optional<PrimitiveMapping> mapping =
            primitive_mapping(field_type.primitive(), error_message);
        if (!mapping.has_value()) {
            return false;
        }
        plan.needs_cstdint = plan.needs_cstdint || mapping->needs_cstdint;
        return true;
    }
    case ::breadcrumbs::schema_ir::FieldType::kRecord: {
        const NamedTypeInfo* target =
            lookup_named_type(catalog, field_type.record().target_record_ir_id(), error_message);
        if (target == nullptr) {
            return false;
        }
        if (target->kind != NamedTypeInfo::Kind::Record) {
            error_message = "backend codegen expected Schema IR record id " +
                            std::to_string(field_type.record().target_record_ir_id()) +
                            " to refer to a record";
            return false;
        }
        if (target->namespace_fqn == current_namespace_fqn) {
            const auto it = local_declaration_ids.find(field_type.record().target_record_ir_id());
            if (it == local_declaration_ids.end()) {
                error_message = "backend codegen could not order a same-namespace declaration "
                                "dependency";
                return false;
            }
            plan.same_namespace_declaration_dependencies.push_back(
                field_type.record().target_record_ir_id());
        } else {
            plan.includes.insert(target->include_path);
        }
        return true;
    }
    case ::breadcrumbs::schema_ir::FieldType::kEnumType: {
        const NamedTypeInfo* target =
            lookup_named_type(catalog, field_type.enum_type().target_enum_ir_id(), error_message);
        if (target == nullptr) {
            return false;
        }
        if (target->kind != NamedTypeInfo::Kind::Enum) {
            error_message = "backend codegen expected Schema IR enum id " +
                            std::to_string(field_type.enum_type().target_enum_ir_id()) +
                            " to refer to an enum";
            return false;
        }
        if (target->namespace_fqn != current_namespace_fqn) {
            plan.includes.insert(target->include_path);
        } else {
            const auto it = local_declaration_ids.find(field_type.enum_type().target_enum_ir_id());
            if (it == local_declaration_ids.end()) {
                error_message = "backend codegen could not order a same-namespace declaration "
                                "dependency";
                return false;
            }
            plan.same_namespace_declaration_dependencies.push_back(
                field_type.enum_type().target_enum_ir_id());
        }
        return true;
    }
    case ::breadcrumbs::schema_ir::FieldType::kBytes:
        error_message = "backend codegen does not yet support bytes fields";
        return false;
    case ::breadcrumbs::schema_ir::FieldType::kString:
        error_message = "backend codegen does not yet support string fields";
        return false;
    case ::breadcrumbs::schema_ir::FieldType::kArray:
        error_message = "backend codegen does not yet support array fields";
        return false;
    case ::breadcrumbs::schema_ir::FieldType::KIND_NOT_SET:
        error_message = "backend codegen encountered a field without a type";
        return false;
    }

    error_message = "backend codegen encountered an unknown field type";
    return false;
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

void collect_named_types(const ::breadcrumbs::schema_ir::NamespaceIR& ns,
                         const CodegenOptions& options, TypeCatalog& catalog) {
    const std::string file_path = file_path_for_namespace(options, ns.fqn());
    const std::string include_path = include_path_for_namespace(options, ns.fqn());

    for (int index = 0; index < ns.records_size(); ++index) {
        const ::breadcrumbs::schema_ir::RecordIR& record = ns.records(index);
        catalog.named_types.emplace(record.ir_id(),
                                    NamedTypeInfo{
                                        .kind = NamedTypeInfo::Kind::Record,
                                        .namespace_fqn = ns.fqn(),
                                        .file_path = file_path,
                                        .include_path = include_path,
                                        .cpp_name = cpp_qualified_name(ns.fqn(), record.name()),
                                    });
    }

    for (int index = 0; index < ns.enums_size(); ++index) {
        const ::breadcrumbs::schema_ir::EnumIR& enum_ir = ns.enums(index);
        catalog.named_types.emplace(enum_ir.ir_id(),
                                    NamedTypeInfo{
                                        .kind = NamedTypeInfo::Kind::Enum,
                                        .namespace_fqn = ns.fqn(),
                                        .file_path = file_path,
                                        .include_path = include_path,
                                        .cpp_name = cpp_qualified_name(ns.fqn(), enum_ir.name()),
                                    });
    }

    for (int index = 0; index < ns.namespaces_size(); ++index) {
        collect_named_types(ns.namespaces(index), options, catalog);
    }
}

[[nodiscard]] bool analyze_namespace(const ::breadcrumbs::schema_ir::NamespaceIR& ns,
                                     const CodegenOptions& options, const TypeCatalog& catalog,
                                     NamespacePlan& plan, std::string& error_message) {
    plan.namespace_ir = &ns;
    plan.fqn = ns.fqn();
    plan.file_path = file_path_for_namespace(options, ns.fqn());
    plan.include_path = file_stem_for_namespace(options, ns.fqn());
    plan.emits_file = ns.records_size() > 0 || ns.enums_size() > 0;

    if (plan.emits_file) {
        std::map<std::uint64_t, std::size_t> local_declaration_ids;
        plan.declarations.reserve(static_cast<std::size_t>(ns.enums_size() + ns.records_size()));

        for (int index = 0; index < ns.enums_size(); ++index) {
            const ::breadcrumbs::schema_ir::EnumIR& enum_ir = ns.enums(index);
            local_declaration_ids.emplace(enum_ir.ir_id(),
                                          static_cast<std::size_t>(plan.declarations.size()));
            plan.declarations.push_back(DeclarationPlan{
                .kind = DeclarationPlan::Kind::Enum,
                .enum_ir = &enum_ir,
                .source_order = static_cast<std::size_t>(index),
            });
        }

        for (int index = 0; index < ns.records_size(); ++index) {
            const ::breadcrumbs::schema_ir::RecordIR& record = ns.records(index);
            plan.declarations.push_back(DeclarationPlan{
                .kind = DeclarationPlan::Kind::Record,
                .record =
                    RecordPlan{
                        .record = &record,
                        .source_order = static_cast<std::size_t>(index),
                    },
                .source_order = static_cast<std::size_t>(ns.enums_size() + index),
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
            for (int field_index = 0; field_index < record_plan.record->fields_size();
                 ++field_index) {
                const ::breadcrumbs::schema_ir::FieldIR& field =
                    record_plan.record->fields(field_index);
                if (!analyze_field_type(field.type(), ns.fqn(), local_declaration_ids, catalog,
                                        record_plan, error_message)) {
                    return false;
                }
            }

            plan.needs_cstdint = plan.needs_cstdint || record_plan.needs_cstdint;
            plan.includes.insert(record_plan.includes.begin(), record_plan.includes.end());
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

void collect_emitted_files(const NamespacePlan& plan, std::vector<const NamespacePlan*>& files) {
    if (plan.emits_file) {
        files.push_back(&plan);
    }
    for (const NamespacePlan& child : plan.children) {
        collect_emitted_files(child, files);
    }
}

[[nodiscard]] bool detect_file_cycles(const std::vector<const NamespacePlan*>& files,
                                      std::string& error_message) {
    std::map<std::string, std::size_t> index_by_path;
    for (std::size_t index = 0; index < files.size(); ++index) {
        index_by_path.emplace(files[index]->include_path, index);
    }

    enum class VisitState { Unvisited, Visiting, Visited };
    std::vector<VisitState> state(files.size(), VisitState::Unvisited);

    auto visit = [&](auto&& self, std::size_t index) -> bool {
        state[index] = VisitState::Visiting;
        for (const std::string& include_path : files[index]->includes) {
            const auto it = index_by_path.find(include_path);
            if (it == index_by_path.end()) {
                continue;
            }

            const std::size_t dependency_index = it->second;
            if (state[dependency_index] == VisitState::Visiting) {
                error_message = "backend codegen detected a namespace cycle between '" +
                                files[index]->include_path + "' and '" +
                                files[dependency_index]->include_path + "'";
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

    for (std::size_t index = 0; index < files.size(); ++index) {
        if (state[index] == VisitState::Unvisited) {
            if (!visit(visit, index)) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] std::string render_field_type(const ::breadcrumbs::schema_ir::FieldType& field_type,
                                            const TypeCatalog& catalog,
                                            std::string& error_message) {
    switch (field_type.kind_case()) {
    case ::breadcrumbs::schema_ir::FieldType::kPrimitive: {
        const std::optional<PrimitiveMapping> mapping =
            primitive_mapping(field_type.primitive(), error_message);
        if (!mapping.has_value()) {
            return {};
        }
        return mapping->cpp_type;
    }
    case ::breadcrumbs::schema_ir::FieldType::kRecord: {
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
        return target->cpp_name;
    }
    case ::breadcrumbs::schema_ir::FieldType::kEnumType: {
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
        return target->cpp_name;
    }
    case ::breadcrumbs::schema_ir::FieldType::kBytes:
        error_message = "backend codegen does not yet support bytes fields";
        return {};
    case ::breadcrumbs::schema_ir::FieldType::kString:
        error_message = "backend codegen does not yet support string fields";
        return {};
    case ::breadcrumbs::schema_ir::FieldType::kArray:
        error_message = "backend codegen does not yet support array fields";
        return {};
    case ::breadcrumbs::schema_ir::FieldType::KIND_NOT_SET:
        error_message = "backend codegen encountered a field without a type";
        return {};
    }

    error_message = "backend codegen encountered an unknown field type";
    return {};
}

[[nodiscard]] std::string render_enum_definition(const ::breadcrumbs::schema_ir::EnumIR& enum_ir,
                                                 std::size_t indent_level) {
    std::ostringstream stream;
    stream << indent(indent_level) << "enum class " << enum_ir.name() << " {\n";
    for (int index = 0; index < enum_ir.values_size(); ++index) {
        const ::breadcrumbs::schema_ir::EnumValueIR& value = enum_ir.values(index);
        stream << indent(indent_level + 1) << value.name() << " = " << value.value() << ",\n";
    }
    stream << indent(indent_level) << "};\n";
    return stream.str();
}

[[nodiscard]] std::string render_record_definition(const ::breadcrumbs::schema_ir::RecordIR& record,
                                                   const TypeCatalog& catalog,
                                                   std::size_t indent_level,
                                                   std::string& error_message) {
    std::ostringstream stream;
    stream << indent(indent_level) << "struct " << record.name();
    if (record.fields_size() == 0) {
        stream << " {};\n";
        return stream.str();
    }

    stream << " {\n";
    for (int field_index = 0; field_index < record.fields_size(); ++field_index) {
        const ::breadcrumbs::schema_ir::FieldIR& field = record.fields(field_index);
        const std::string field_type = render_field_type(field.type(), catalog, error_message);
        if (!error_message.empty()) {
            return {};
        }
        stream << indent(indent_level + 1) << field_type << ' ' << field.name() << ";\n";
    }
    stream << indent(indent_level) << "};\n";
    return stream.str();
}

[[nodiscard]] std::string render_namespace_file(const NamespacePlan& plan,
                                                const CodegenOptions& options,
                                                const TypeCatalog& catalog,
                                                std::string& error_message) {
    std::ostringstream stream;
    stream << "// Generated by Breadcrumbs.\n";
    stream << namespace_comment(plan.fqn);
    stream << '\n';

    if (plan.needs_cstdint) {
        stream << "#include <cstdint>\n";
    }
    if (!plan.includes.empty()) {
        if (plan.needs_cstdint) {
            stream << '\n';
        }
        for (const std::string& include_path : plan.includes) {
            stream << "#include \"" << include_path << "\"\n";
        }
    }
    if (plan.needs_cstdint || !plan.includes.empty()) {
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
            const std::string record_definition = render_record_definition(
                *declaration.record.record, catalog, parts.size(), error_message);
            if (!error_message.empty()) {
                return {};
            }
            stream << record_definition;
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

    std::string output = stream.str();
    if (!output.empty() && output.back() != '\n') {
        output.push_back('\n');
    }

    (void)options;
    return output;
}

void collect_namespace_files(const NamespacePlan& plan, const CodegenOptions& options,
                             const TypeCatalog& catalog, std::vector<GeneratedFile>& files,
                             std::string& error_message) {
    if (plan.emits_file) {
        GeneratedFile file;
        file.path = plan.file_path;
        file.content = render_namespace_file(plan, options, catalog, error_message);
        if (!error_message.empty()) {
            return;
        }
        files.push_back(std::move(file));
    }

    for (const NamespacePlan& child : plan.children) {
        collect_namespace_files(child, options, catalog, files, error_message);
        if (!error_message.empty()) {
            return;
        }
    }
}

} // namespace

CodegenResult Backend::generate(const schema_ir::SchemaIrModel& schema_ir,
                                const CodegenOptions& options) const {
    CodegenResult result;

    TypeCatalog catalog;
    collect_named_types(schema_ir.root_namespace(), options, catalog);

    NamespacePlan root_plan;
    std::string error_message;
    if (!analyze_namespace(schema_ir.root_namespace(), options, catalog, root_plan,
                           error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    std::vector<const NamespacePlan*> emitted_files;
    collect_emitted_files(root_plan, emitted_files);
    if (!detect_file_cycles(emitted_files, error_message)) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    std::vector<GeneratedFile> files;
    collect_namespace_files(root_plan, options, catalog, files, error_message);
    if (!error_message.empty()) {
        result.success = false;
        result.error_message = std::move(error_message);
        return result;
    }

    result.files = std::move(files);
    return result;
}

} // namespace breadcrumbs::compiler::backend
