#include "compiler/schema_ir/schema_ir.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace breadcrumbs::compiler::schema_ir {
namespace {

constexpr std::string_view schema_ir_pass = "schema_ir";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] std::string qualify_fqn(std::string_view parent_fqn, std::string_view name) {
    if (parent_fqn.empty()) {
        return std::string(name);
    }
    std::string qualified = std::string(parent_fqn);
    qualified.push_back('.');
    qualified.append(name);
    return qualified;
}

[[nodiscard]] std::optional<::breadcrumbs::schema_ir::PrimitiveType>
primitive_type_for_name(std::string_view name) {
    if (name == "bool") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL;
    }
    if (name == "int8" || name == "i8") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_I8;
    }
    if (name == "uint8" || name == "u8") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U8;
    }
    if (name == "int16" || name == "i16") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_I16;
    }
    if (name == "uint16" || name == "u16") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U16;
    }
    if (name == "int32" || name == "i32") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_I32;
    }
    if (name == "uint32" || name == "u32") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32;
    }
    if (name == "int64" || name == "i64") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_I64;
    }
    if (name == "uint64" || name == "u64") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U64;
    }
    if (name == "float32" || name == "f32") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_F32;
    }
    if (name == "float64" || name == "f64") {
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_F64;
    }
    return std::nullopt;
}

[[nodiscard]] bool is_string_type(std::string_view name) { return name == "string"; }

[[nodiscard]] bool is_bytes_type(std::string_view name) { return name == "bytes"; }

void emit_internal_error(diagnostics::DiagnosticCollection& diagnostics, std::string message,
                         support::SourceRange range = support::SourceRange::invalid()) {
    auto builder = diagnostics::Diagnostic::create(
        diagnostic_id("BC1004"), diagnostics::Severity::InternalCompilerError, std::move(message));
    builder.from_pass(std::string(schema_ir_pass));
    if (range.is_valid()) {
        builder.at(range);
    }
    diagnostics.emit(builder.build());
}

void populate_source_origin(::breadcrumbs::schema_ir::SourceOrigin* origin,
                            const support::SourceManager& source_manager,
                            support::SourceRange range) {
    if (origin == nullptr || !range.is_valid()) {
        return;
    }

    const std::optional<std::string_view> source_path =
        source_manager.source_path(range.begin().file_id());
    if (source_path.has_value()) {
        origin->set_source_unit(std::string(*source_path));
        origin->set_file(std::string(*source_path));
    }

    ::breadcrumbs::schema_ir::SourceSpan* span = origin->mutable_span();
    span->set_start_offset(static_cast<std::uint32_t>(range.begin().byte_offset()));
    span->set_end_offset(static_cast<std::uint32_t>(range.end().byte_offset()));

    const std::optional<support::LineColumn> begin = source_manager.line_column(range.begin());
    const std::optional<support::LineColumn> end = source_manager.line_column(range.end());
    if (begin.has_value()) {
        span->set_start_line(static_cast<std::uint32_t>(begin->line));
        span->set_start_column(static_cast<std::uint32_t>(begin->column));
    }
    if (end.has_value()) {
        span->set_end_line(static_cast<std::uint32_t>(end->line));
        span->set_end_column(static_cast<std::uint32_t>(end->column));
    }
}

[[nodiscard]] ::breadcrumbs::schema_ir::NamespaceIR*
find_namespace_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.namespaces_size(); ++index) {
        ::breadcrumbs::schema_ir::NamespaceIR* child = parent.mutable_namespaces(index);
        if (child != nullptr && child->name() == name) {
            return child;
        }
    }
    return nullptr;
}

[[nodiscard]] ::breadcrumbs::schema_ir::RecordIR*
find_record_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.records_size(); ++index) {
        ::breadcrumbs::schema_ir::RecordIR* record = parent.mutable_records(index);
        if (record != nullptr && record->name() == name) {
            return record;
        }
    }
    return nullptr;
}

[[nodiscard]] ::breadcrumbs::schema_ir::EnumIR*
find_enum_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.enums_size(); ++index) {
        ::breadcrumbs::schema_ir::EnumIR* enum_ir = parent.mutable_enums(index);
        if (enum_ir != nullptr && enum_ir->name() == name) {
            return enum_ir;
        }
    }
    return nullptr;
}

[[nodiscard]] ::breadcrumbs::schema_ir::NamespaceIR&
ensure_namespace_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string name,
                       const support::SourceManager& source_manager, support::SourceRange range) {
    if (::breadcrumbs::schema_ir::NamespaceIR* existing = find_namespace_child(parent, name);
        existing != nullptr) {
        return *existing;
    }

    ::breadcrumbs::schema_ir::NamespaceIR* child = parent.add_namespaces();
    child->set_ir_id(0);
    child->set_name(std::move(name));
    child->set_fqn(qualify_fqn(parent.fqn(), child->name()));
    populate_source_origin(child->mutable_source_origin(), source_manager, range);
    return *child;
}

[[nodiscard]] ::breadcrumbs::schema_ir::RecordIR&
ensure_record_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string name,
                    const support::SourceManager& source_manager, support::SourceRange range) {
    if (::breadcrumbs::schema_ir::RecordIR* existing = find_record_child(parent, name);
        existing != nullptr) {
        return *existing;
    }

    ::breadcrumbs::schema_ir::RecordIR* record = parent.add_records();
    record->set_ir_id(0);
    record->set_name(std::move(name));
    record->set_fqn(qualify_fqn(parent.fqn(), record->name()));
    populate_source_origin(record->mutable_source_origin(), source_manager, range);
    return *record;
}

[[nodiscard]] ::breadcrumbs::schema_ir::EnumIR&
ensure_enum_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string name,
                  const support::SourceManager& source_manager, support::SourceRange range) {
    if (::breadcrumbs::schema_ir::EnumIR* existing = find_enum_child(parent, name);
        existing != nullptr) {
        return *existing;
    }

    ::breadcrumbs::schema_ir::EnumIR* enum_ir = parent.add_enums();
    enum_ir->set_ir_id(0);
    enum_ir->set_name(std::move(name));
    enum_ir->set_fqn(qualify_fqn(parent.fqn(), enum_ir->name()));
    populate_source_origin(enum_ir->mutable_source_origin(), source_manager, range);
    return *enum_ir;
}

struct NamespaceTraversal {
    ::breadcrumbs::schema_ir::NamespaceIR* namespace_ir = nullptr;
    const symbols::Scope* scope = nullptr;
};

class SchemaIrBuilderImpl {
public:
    SchemaIrBuilderImpl(const ast::Ast& ast, const semantic::SemanticModel& semantic_model,
                        const layout::LayoutModel& layout_model,
                        const symbols::SymbolTable& symbol_model, context::CompilerContext& context,
                        diagnostics::DiagnosticCollection& diagnostics)
        : ast_(ast), semantic_model_(semantic_model), layout_model_(layout_model),
          symbol_model_(symbol_model), source_manager_(context.source_manager()),
          diagnostics_(diagnostics) {}

    [[nodiscard]] SchemaIrModel build() {
        SchemaIrModel schema_ir;
        schema_ir.set_schema_ir_version(1);

        ::breadcrumbs::schema_ir::NamespaceIR* root = schema_ir.mutable_root_namespace();
        root->set_ir_id(next_ir_id_++);
        root->set_name("");
        root->set_fqn("");
        populate_source_origin(root->mutable_source_origin(), source_manager_, ast_.source_range);

        collect_skeletons(ast_.declarations, *root, symbol_model_.global_scope(), {});
        populate_declarations(ast_.declarations, *root, symbol_model_.global_scope(), {});

        if (layout_failed_) {
            return {};
        }
        return schema_ir;
    }

private:
    [[nodiscard]] NamespaceTraversal
    ensure_namespace_path(::breadcrumbs::schema_ir::NamespaceIR& parent_ir,
                          const symbols::Scope& parent_scope, const ast::QualifiedNameSyntax& name,
                          support::SourceRange range) {
        NamespaceTraversal traversal{.namespace_ir = &parent_ir, .scope = &parent_scope};
        if (name.parts.empty()) {
            emit_internal_error(diagnostics_,
                                "schema IR lowering encountered an empty namespace name", range);
            layout_failed_ = true;
            return {};
        }

        for (const ast::IdentifierSyntax& part : name.parts) {
            const symbols::Symbol* symbol = traversal.scope->find_local(part.text);
            if (symbol == nullptr || symbol->kind != symbols::SymbolKind::Namespace ||
                symbol->child_scope == nullptr) {
                emit_internal_error(
                    diagnostics_,
                    "schema IR lowering could not resolve namespace '" + name.text() + "'", range);
                layout_failed_ = true;
                return {};
            }

            ::breadcrumbs::schema_ir::NamespaceIR& child =
                ensure_namespace_child(*traversal.namespace_ir, part.text, source_manager_, range);
            if (child.ir_id() == 0) {
                child.set_ir_id(next_ir_id_++);
            }

            traversal.namespace_ir = &child;
            traversal.scope = symbol->child_scope;
        }

        return traversal;
    }

    void collect_skeletons(const std::vector<ast::DeclarationPtr>& declarations,
                           ::breadcrumbs::schema_ir::NamespaceIR& namespace_ir,
                           const symbols::Scope& scope, std::string_view current_namespace_fqn) {
        for (const ast::DeclarationPtr& declaration : declarations) {
            if (declaration != nullptr) {
                collect_skeleton(*declaration, namespace_ir, scope, current_namespace_fqn);
            }
        }
    }

    void collect_skeletons(const ast::NamespaceDeclarationSyntax& namespace_declaration,
                           ::breadcrumbs::schema_ir::NamespaceIR& namespace_ir,
                           const symbols::Scope& scope, std::string_view current_namespace_fqn) {
        collect_skeletons(namespace_declaration.declarations, namespace_ir, scope,
                          current_namespace_fqn);
    }

    void collect_skeleton(const ast::DeclarationSyntax& declaration,
                          ::breadcrumbs::schema_ir::NamespaceIR& namespace_ir,
                          const symbols::Scope& scope, std::string_view current_namespace_fqn) {
        std::visit(
            [&](const auto& typed) {
                using Type = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<Type, ast::NamespaceDeclarationSyntax>) {
                    NamespaceTraversal traversal =
                        ensure_namespace_path(namespace_ir, scope, typed.name, typed.source_range);
                    if (traversal.namespace_ir == nullptr || traversal.scope == nullptr) {
                        return;
                    }

                    collect_skeletons(typed.declarations, *traversal.namespace_ir, *traversal.scope,
                                      qualify_fqn(current_namespace_fqn, typed.name.text()));
                } else if constexpr (std::is_same_v<Type, ast::RecordDeclarationSyntax>) {
                    const symbols::Symbol* symbol = scope.find_local(typed.name.text);
                    if (symbol == nullptr || symbol->kind != symbols::SymbolKind::Record) {
                        emit_internal_error(diagnostics_,
                                            "schema IR lowering could not resolve record '" +
                                                typed.name.text + "'",
                                            typed.source_range);
                        layout_failed_ = true;
                        return;
                    }

                    const uint64_t ir_id = next_ir_id_++;
                    declaration_ids_[&declaration] = ir_id;
                    ::breadcrumbs::schema_ir::RecordIR& record = ensure_record_child(
                        namespace_ir, typed.name.text, source_manager_, typed.source_range);
                    record.set_ir_id(ir_id);
                } else if constexpr (std::is_same_v<Type, ast::EnumDeclarationSyntax>) {
                    const symbols::Symbol* symbol = scope.find_local(typed.name.text);
                    if (symbol == nullptr || symbol->kind != symbols::SymbolKind::Enum) {
                        emit_internal_error(diagnostics_,
                                            "schema IR lowering could not resolve enum '" +
                                                typed.name.text + "'",
                                            typed.source_range);
                        layout_failed_ = true;
                        return;
                    }

                    const uint64_t ir_id = next_ir_id_++;
                    declaration_ids_[&declaration] = ir_id;
                    ::breadcrumbs::schema_ir::EnumIR& enum_ir = ensure_enum_child(
                        namespace_ir, typed.name.text, source_manager_, typed.source_range);
                    enum_ir.set_ir_id(ir_id);
                } else if constexpr (std::is_same_v<Type, ast::ImportDeclarationSyntax>) {
                    (void)typed;
                }
            },
            declaration.value);
    }

    [[nodiscard]] ::breadcrumbs::schema_ir::FieldType
    lower_type_reference(const ast::TypeReferenceSyntax& type_reference,
                         const symbols::Scope& scope) {
        ::breadcrumbs::schema_ir::FieldType field_type;

        if (const std::optional<::breadcrumbs::schema_ir::PrimitiveType> primitive =
                primitive_type_for_name(type_reference.name.text());
            primitive.has_value()) {
            field_type.set_primitive(*primitive);
            return field_type;
        }

        if (is_string_type(type_reference.name.text())) {
            ::breadcrumbs::schema_ir::StringType* string_type = field_type.mutable_string();
            string_type->set_max_bytes(0);
            return field_type;
        }

        if (is_bytes_type(type_reference.name.text())) {
            ::breadcrumbs::schema_ir::BytesType* bytes_type = field_type.mutable_bytes();
            bytes_type->set_max_bytes(0);
            return field_type;
        }

        const symbols::Symbol* symbol = symbol_model_.resolve(type_reference.name, scope);
        if (symbol == nullptr) {
            emit_internal_error(diagnostics_,
                                "schema IR lowering could not resolve type '" +
                                    type_reference.name.text() + "'",
                                type_reference.source_range);
            return field_type;
        }

        const auto declared_id = declaration_ids_.find(symbol->declaration);
        if (declared_id == declaration_ids_.end()) {
            emit_internal_error(diagnostics_,
                                "schema IR lowering is missing an IR id for '" +
                                    type_reference.name.text() + "'",
                                type_reference.source_range);
            layout_failed_ = true;
            return field_type;
        }

        switch (symbol->kind) {
        case symbols::SymbolKind::Record:
            field_type.mutable_record()->set_target_record_ir_id(declared_id->second);
            break;
        case symbols::SymbolKind::Enum:
            field_type.mutable_enum_type()->set_target_enum_ir_id(declared_id->second);
            break;
        case symbols::SymbolKind::Namespace:
            emit_internal_error(diagnostics_,
                                "schema IR lowering cannot use namespace '" +
                                    type_reference.name.text() + "' as a type",
                                type_reference.source_range);
            layout_failed_ = true;
            break;
        }

        return field_type;
    }

    [[nodiscard]] ::breadcrumbs::schema_ir::FieldType lower_type(const ast::TypeSyntax& type,
                                                                 const symbols::Scope& scope) {
        return std::visit(
            [&](const auto& typed) -> ::breadcrumbs::schema_ir::FieldType {
                using Type = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<Type, ast::TypeReferenceSyntax>) {
                    return lower_type_reference(typed, scope);
                } else if constexpr (std::is_same_v<Type, ast::ArrayTypeSyntax>) {
                    ::breadcrumbs::schema_ir::FieldType field_type;
                    ::breadcrumbs::schema_ir::ArrayType* array_type = field_type.mutable_array();
                    if (typed.fixed_size.has_value()) {
                        if (*typed.fixed_size > std::numeric_limits<std::uint32_t>::max()) {
                            emit_internal_error(diagnostics_,
                                                "schema IR lowering encountered an array size "
                                                "that does not fit in uint32_t",
                                                typed.source_range);
                        } else {
                            array_type->set_count(static_cast<std::uint32_t>(*typed.fixed_size));
                        }
                    } else {
                        emit_internal_error(diagnostics_,
                                            "schema IR lowering encountered an array without a "
                                            "fixed size",
                                            typed.source_range);
                    }

                    array_type->mutable_element_type()->CopyFrom(
                        lower_type(typed.element_type, scope));
                    return field_type;
                }

                return {};
            },
            type);
    }

    [[nodiscard]] const semantic::SemanticRecord*
    find_semantic_record(std::string_view record_fqn) const {
        return semantic_model_.find_record(record_fqn);
    }

    [[nodiscard]] const layout::RecordLayout*
    find_layout_record(std::string_view record_fqn) const {
        return layout_model_.find_record(record_fqn);
    }

    bool populate_record_fields(::breadcrumbs::schema_ir::RecordIR& record,
                                const ast::RecordDeclarationSyntax& declaration,
                                const symbols::Scope& scope, std::string_view record_fqn) {
        const semantic::SemanticRecord* semantic_record = find_semantic_record(record_fqn);
        const layout::RecordLayout* layout_record = find_layout_record(record_fqn);
        if (semantic_record == nullptr || layout_record == nullptr) {
            emit_internal_error(diagnostics_,
                                "schema IR lowering could not locate layout for record '" +
                                    std::string(record_fqn) + "'",
                                declaration.source_range);
            layout_failed_ = true;
            return false;
        }

        if (layout_record->record_id == 0U) {
            emit_internal_error(diagnostics_,
                                "schema IR lowering observed a zero record_id for record '" +
                                    std::string(record_fqn) + "'",
                                declaration.source_range);
            layout_failed_ = true;
            return false;
        }

        if (semantic_record->fields.size() != declaration.fields.size() ||
            layout_record->fields.size() != declaration.fields.size()) {
            emit_internal_error(
                diagnostics_,
                "schema IR lowering observed inconsistent field counts for record '" +
                    std::string(record_fqn) + "'",
                declaration.source_range);
            layout_failed_ = true;
            return false;
        }

        record.set_record_id(layout_record->record_id);
        for (std::size_t field_index = 0; field_index < declaration.fields.size(); ++field_index) {
            const ast::FieldDeclarationSyntax& field = declaration.fields[field_index];
            const semantic::SemanticField& semantic_field = semantic_record->fields[field_index];
            const layout::FieldLayout& layout_field = layout_record->fields[field_index];
            if (semantic_field.name != field.name.text) {
                emit_internal_error(
                    diagnostics_,
                    "schema IR lowering observed inconsistent field ordering for record '" +
                        std::string(record_fqn) + "'",
                    field.source_range);
                layout_failed_ = true;
                return false;
            }

            ::breadcrumbs::schema_ir::FieldIR* field_ir = record.add_fields();
            field_ir->set_name(field.name.text);
            field_ir->set_field_index(layout_field.field_index);
            field_ir->mutable_type()->CopyFrom(lower_type(field.type, scope));
            populate_source_origin(field_ir->mutable_source_origin(), source_manager_,
                                   field.source_range);
        }
        return true;
    }

    void populate_enum_values(::breadcrumbs::schema_ir::EnumIR& enum_ir,
                              const ast::EnumDeclarationSyntax& declaration) {
        for (const ast::EnumValueDeclarationSyntax& value : declaration.values) {
            ::breadcrumbs::schema_ir::EnumValueIR* value_ir = enum_ir.add_values();
            value_ir->set_name(value.name.text);
            populate_source_origin(value_ir->mutable_source_origin(), source_manager_,
                                   value.source_range);

            if (!value.value.has_value()) {
                emit_internal_error(diagnostics_,
                                    "schema IR lowering encountered an enum value without an "
                                    "explicit numeric literal",
                                    value.source_range);
                value_ir->set_value(0);
                continue;
            }

            try {
                std::size_t consumed = 0;
                const long long parsed = std::stoll(*value.value, &consumed, 10);
                if (consumed != value.value->size()) {
                    throw std::invalid_argument("trailing characters");
                }
                value_ir->set_value(static_cast<std::int64_t>(parsed));
            } catch (const std::exception&) {
                emit_internal_error(diagnostics_,
                                    "schema IR lowering encountered an invalid enum value '" +
                                        *value.value + "'",
                                    value.source_range);
                value_ir->set_value(0);
            }
        }
    }

    void populate_declarations(const std::vector<ast::DeclarationPtr>& declarations,
                               ::breadcrumbs::schema_ir::NamespaceIR& namespace_ir,
                               const symbols::Scope& scope,
                               std::string_view current_namespace_fqn) {
        for (const ast::DeclarationPtr& declaration : declarations) {
            if (declaration != nullptr) {
                populate_declaration(*declaration, namespace_ir, scope, current_namespace_fqn);
            }
        }
    }

    void populate_declarations(const ast::NamespaceDeclarationSyntax& namespace_declaration,
                               ::breadcrumbs::schema_ir::NamespaceIR& namespace_ir,
                               const symbols::Scope& scope,
                               std::string_view current_namespace_fqn) {
        populate_declarations(namespace_declaration.declarations, namespace_ir, scope,
                              current_namespace_fqn);
    }

    void populate_declaration(const ast::DeclarationSyntax& declaration,
                              ::breadcrumbs::schema_ir::NamespaceIR& namespace_ir,
                              const symbols::Scope& scope, std::string_view current_namespace_fqn) {
        std::visit(
            [&](const auto& typed) {
                using Type = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<Type, ast::NamespaceDeclarationSyntax>) {
                    NamespaceTraversal traversal =
                        ensure_namespace_path(namespace_ir, scope, typed.name, typed.source_range);
                    if (traversal.namespace_ir == nullptr || traversal.scope == nullptr) {
                        return;
                    }

                    populate_declarations(typed.declarations, *traversal.namespace_ir,
                                          *traversal.scope,
                                          qualify_fqn(current_namespace_fqn, typed.name.text()));
                } else if constexpr (std::is_same_v<Type, ast::RecordDeclarationSyntax>) {
                    ::breadcrumbs::schema_ir::RecordIR* record =
                        find_record_child(namespace_ir, typed.name.text);
                    if (record == nullptr) {
                        emit_internal_error(diagnostics_,
                                            "schema IR lowering could not locate record '" +
                                                typed.name.text + "'",
                                            typed.source_range);
                        layout_failed_ = true;
                        return;
                    }

                    const std::string record_fqn =
                        qualify_fqn(current_namespace_fqn, typed.name.text);
                    if (!populate_record_fields(*record, typed, scope, record_fqn)) {
                        return;
                    }
                } else if constexpr (std::is_same_v<Type, ast::EnumDeclarationSyntax>) {
                    ::breadcrumbs::schema_ir::EnumIR* enum_ir =
                        find_enum_child(namespace_ir, typed.name.text);
                    if (enum_ir == nullptr) {
                        emit_internal_error(diagnostics_,
                                            "schema IR lowering could not locate enum '" +
                                                typed.name.text + "'",
                                            typed.source_range);
                        layout_failed_ = true;
                        return;
                    }

                    populate_enum_values(*enum_ir, typed);
                } else if constexpr (std::is_same_v<Type, ast::ImportDeclarationSyntax>) {
                    (void)typed;
                }
            },
            declaration.value);
    }

    const ast::Ast& ast_;
    const semantic::SemanticModel& semantic_model_;
    const layout::LayoutModel& layout_model_;
    const symbols::SymbolTable& symbol_model_;
    const support::SourceManager& source_manager_;
    diagnostics::DiagnosticCollection& diagnostics_;
    std::uint64_t next_ir_id_ = 1;
    std::unordered_map<const ast::DeclarationSyntax*, std::uint64_t> declaration_ids_;
    bool layout_failed_ = false;
};

} // namespace

SchemaIrModel SchemaIrBuilder::build(const ast::Ast& ast,
                                     const semantic::SemanticModel& semantic_model,
                                     const layout::LayoutModel& layout_model,
                                     const symbols::SymbolTable& symbol_model,
                                     context::CompilerContext& context,
                                     diagnostics::DiagnosticCollection& diagnostics) const {
    SchemaIrBuilderImpl builder(ast, semantic_model, layout_model, symbol_model, context,
                                diagnostics);
    return builder.build();
}

} // namespace breadcrumbs::compiler::schema_ir
