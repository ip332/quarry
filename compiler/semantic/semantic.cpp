#include "compiler/semantic/semantic.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace breadcrumbs::compiler::semantic {
namespace {

constexpr std::string_view semantic_pass = "semantic";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] std::optional<SemanticPrimitiveType> canonical_primitive_type(std::string_view name) {
    if (name == "bool") {
        return SemanticPrimitiveType::Bool;
    }
    if (name == "int8" || name == "i8") {
        return SemanticPrimitiveType::I8;
    }
    if (name == "uint8" || name == "u8") {
        return SemanticPrimitiveType::U8;
    }
    if (name == "int16" || name == "i16") {
        return SemanticPrimitiveType::I16;
    }
    if (name == "uint16" || name == "u16") {
        return SemanticPrimitiveType::U16;
    }
    if (name == "int32" || name == "i32") {
        return SemanticPrimitiveType::I32;
    }
    if (name == "uint32" || name == "u32") {
        return SemanticPrimitiveType::U32;
    }
    if (name == "int64" || name == "i64") {
        return SemanticPrimitiveType::I64;
    }
    if (name == "uint64" || name == "u64") {
        return SemanticPrimitiveType::U64;
    }
    if (name == "float32" || name == "f32") {
        return SemanticPrimitiveType::F32;
    }
    if (name == "float64" || name == "f64") {
        return SemanticPrimitiveType::F64;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SemanticRecordType> canonical_record_type(std::string_view name) {
    if (name == "data") {
        return SemanticRecordType::Data;
    }
    if (name == "command") {
        return SemanticRecordType::Command;
    }
    if (name == "event") {
        return SemanticRecordType::Event;
    }
    if (name == "configuration") {
        return SemanticRecordType::Configuration;
    }
    if (name == "diagnostics") {
        return SemanticRecordType::Diagnostics;
    }
    return std::nullopt;
}

[[nodiscard]] bool is_type_symbol(const symbols::Symbol* symbol) {
    if (symbol == nullptr) {
        return false;
    }

    switch (symbol->kind) {
    case symbols::SymbolKind::Record:
    case symbols::SymbolKind::Enum:
        return true;
    case symbols::SymbolKind::Namespace:
        return false;
    }

    return false;
}

[[nodiscard]] std::string qualify_fqn(std::string_view parent_fqn, std::string_view name) {
    if (parent_fqn.empty()) {
        return std::string(name);
    }

    std::string qualified(parent_fqn);
    qualified.push_back('.');
    qualified.append(name);
    return qualified;
}

void emit_unresolved_type(diagnostics::DiagnosticEngine& diagnostics,
                          const ast::TypeReferenceSyntax& type_reference) {
    diagnostics.emit(
        diagnostics::Diagnostic::create(diagnostic_id("BC5001"), diagnostics::Severity::Error,
                                        "unresolved type '" + type_reference.name.text() + "'")
            .at(type_reference.source_range)
            .from_pass(std::string(semantic_pass))
            .build());
}

void emit_invalid_type_position(diagnostics::DiagnosticEngine& diagnostics,
                                const ast::TypeReferenceSyntax& type_reference,
                                const symbols::Symbol& symbol) {
    diagnostics.emit(diagnostics::Diagnostic::create(
                         diagnostic_id("BC5002"), diagnostics::Severity::Error,
                         "'" + type_reference.name.text() + "' resolves to a " +
                             std::string(symbol.kind == symbols::SymbolKind::Namespace ? "namespace"
                                                                                       : "symbol") +
                             ", not a type")
                         .at(type_reference.source_range)
                         .with_related(diagnostics::RelatedLocation::at_range(
                             symbol.source_range, "resolved declaration is here"))
                         .from_pass(std::string(semantic_pass))
                         .build());
}

void emit_invalid_array(diagnostics::DiagnosticEngine& diagnostics,
                        const ast::ArrayTypeSyntax& array_type, std::string_view reason) {
    diagnostics.emit(diagnostics::Diagnostic::create(
                         diagnostic_id("BC5003"), diagnostics::Severity::Error, std::string(reason))
                         .at(array_type.source_range)
                         .from_pass(std::string(semantic_pass))
                         .build());
}

[[nodiscard]] std::optional<SemanticType>
resolve_named_type(const ast::TypeReferenceSyntax& type_reference, const symbols::Scope& scope,
                   const symbols::SymbolTable& symbol_model,
                   diagnostics::DiagnosticEngine& diagnostics) {
    if (const std::optional<SemanticPrimitiveType> primitive =
            canonical_primitive_type(type_reference.name.text());
        primitive.has_value()) {
        return SemanticType(*primitive);
    }

    ast::QualifiedNameSyntax normalized_name{
        .source_range = type_reference.name.source_range,
        .parts = type_reference.name.parts,
    };
    const symbols::Symbol* symbol = symbol_model.resolve(normalized_name, scope);
    if (symbol == nullptr) {
        emit_unresolved_type(diagnostics, type_reference);
        return std::nullopt;
    }

    if (!is_type_symbol(symbol)) {
        emit_invalid_type_position(diagnostics, type_reference, *symbol);
        return std::nullopt;
    }

    if (symbol->kind == symbols::SymbolKind::Record) {
        return SemanticType(SemanticRecordReferenceType{.canonical_target_fqn = symbol->fqn});
    }

    if (symbol->kind == symbols::SymbolKind::Enum) {
        return SemanticType(SemanticEnumReferenceType{.canonical_target_fqn = symbol->fqn});
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t>
validate_positive_u32(std::optional<std::int64_t> value, support::SourceRange range,
                      diagnostics::DiagnosticEngine& diagnostics, std::string_view context,
                      std::string_view property_name) {
    if (!value.has_value()) {
        auto builder = diagnostics::Diagnostic::create(
            diagnostic_id("BC5004"), diagnostics::Severity::Error,
            std::string(context) + " is missing required property '" + std::string(property_name) +
                "'");
        builder.from_pass(std::string(semantic_pass));
        if (range.is_valid()) {
            builder.at(range);
        }
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    if (*value <= 0) {
        auto builder = diagnostics::Diagnostic::create(
            diagnostic_id("BC5004"), diagnostics::Severity::Error,
            std::string(context) + " property '" + std::string(property_name) +
                "' must be a positive integer");
        builder.from_pass(std::string(semantic_pass));
        if (range.is_valid()) {
            builder.at(range);
        }
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    if (*value > std::numeric_limits<std::uint32_t>::max()) {
        auto builder = diagnostics::Diagnostic::create(
            diagnostic_id("BC5004"), diagnostics::Severity::Error,
            std::string(context) + " property '" + std::string(property_name) +
                "' does not fit in uint32");
        builder.from_pass(std::string(semantic_pass));
        if (range.is_valid()) {
            builder.at(range);
        }
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    return static_cast<std::uint32_t>(*value);
}

void emit_invalid_bound_position(diagnostics::DiagnosticEngine& diagnostics,
                                 support::SourceRange range, std::string_view context,
                                 std::string_view property_name) {
    diagnostics.emit(
        diagnostics::Diagnostic::create(diagnostic_id("BC5004"), diagnostics::Severity::Error,
                                        std::string(context) + " cannot use property '" +
                                            std::string(property_name) + "'")
            .at(range)
            .from_pass(std::string(semantic_pass))
            .build());
}

[[nodiscard]] std::optional<SemanticType> resolve_type(const ast::FieldDeclarationSyntax& field,
                                                       const ast::TypeSyntax& type,
                                                       const symbols::Scope& scope,
                                                       const symbols::SymbolTable& symbol_model,
                                                       diagnostics::DiagnosticEngine& diagnostics) {
    return std::visit(
        [&](const auto& typed) -> std::optional<SemanticType> {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, ast::TypeReferenceSyntax>) {
                if (typed.name.text() == "string") {
                    const std::optional<std::uint32_t> max_bytes = validate_positive_u32(
                        field.max_bytes, field.max_bytes_source_range, diagnostics,
                        "field '" + field.name.text + "'", "max_bytes");
                    if (!max_bytes.has_value()) {
                        return std::nullopt;
                    }
                    if (field.max_elements.has_value()) {
                        emit_invalid_bound_position(diagnostics, field.max_elements_source_range,
                                                    "field '" + field.name.text + "'",
                                                    "max_elements");
                        return std::nullopt;
                    }
                    return SemanticType(SemanticStringType{.max_bytes = *max_bytes});
                }

                if (typed.name.text() == "bytes") {
                    const std::optional<std::uint32_t> max_bytes = validate_positive_u32(
                        field.max_bytes, field.max_bytes_source_range, diagnostics,
                        "field '" + field.name.text + "'", "max_bytes");
                    if (!max_bytes.has_value()) {
                        return std::nullopt;
                    }
                    if (field.max_elements.has_value()) {
                        emit_invalid_bound_position(diagnostics, field.max_elements_source_range,
                                                    "field '" + field.name.text + "'",
                                                    "max_elements");
                        return std::nullopt;
                    }
                    return SemanticType(SemanticBytesType{.max_bytes = *max_bytes});
                }

                if (field.max_bytes.has_value()) {
                    emit_invalid_bound_position(diagnostics, field.max_bytes_source_range,
                                                "field '" + field.name.text + "'", "max_bytes");
                    return std::nullopt;
                }
                if (field.max_elements.has_value()) {
                    emit_invalid_bound_position(diagnostics, field.max_elements_source_range,
                                                "field '" + field.name.text + "'", "max_elements");
                    return std::nullopt;
                }
                return resolve_named_type(typed, scope, symbol_model, diagnostics);
            } else if constexpr (std::is_same_v<Type, ast::ArrayTypeSyntax>) {
                if (typed.kind == ast::ArrayTypeSyntaxKind::LegacyFixedSize) {
                    emit_invalid_array(diagnostics, typed,
                                       "fixed-size arrays are not supported in version 0.1");
                    return std::nullopt;
                }

                if (field.max_bytes.has_value()) {
                    emit_invalid_bound_position(diagnostics, field.max_bytes_source_range,
                                                "field '" + field.name.text + "'", "max_bytes");
                    return std::nullopt;
                }

                const std::optional<std::uint32_t> max_elements = validate_positive_u32(
                    field.max_elements, field.max_elements_source_range, diagnostics,
                    "field '" + field.name.text + "'", "max_elements");
                if (!max_elements.has_value()) {
                    return std::nullopt;
                }

                const std::optional<SemanticType> element_type =
                    [&]() -> std::optional<SemanticType> {
                    ast::FieldDeclarationSyntax element_field = field;
                    element_field.max_bytes = std::nullopt;
                    element_field.max_bytes_source_range = support::SourceRange::invalid();
                    element_field.max_elements = std::nullopt;
                    element_field.max_elements_source_range = support::SourceRange::invalid();
                    return resolve_type(element_field, typed.element_type, scope, symbol_model,
                                        diagnostics);
                }();
                if (!element_type.has_value()) {
                    return std::nullopt;
                }
                if (element_type->is_array()) {
                    emit_invalid_array(diagnostics, typed,
                                       "nested arrays are not supported in version 0.1");
                    return std::nullopt;
                }

                SemanticArrayType array;
                array.max_elements = *max_elements;
                array.element_type = std::make_unique<SemanticType>(*element_type);
                return SemanticType(std::move(array));
            }
            return std::nullopt;
        },
        type);
}

const symbols::Scope* scope_for_namespace(const ast::NamespaceDeclarationSyntax& declaration,
                                          const symbols::Scope& current_scope,
                                          const symbols::SymbolTable& symbol_model) {
    const symbols::Symbol* symbol = symbol_model.resolve(declaration.name, current_scope);
    if (symbol == nullptr || symbol->kind != symbols::SymbolKind::Namespace ||
        symbol->child_scope == nullptr) {
        return nullptr;
    }

    return symbol->child_scope;
}

void collect_semantic_record(const ast::RecordDeclarationSyntax& declaration,
                             std::string_view current_namespace_fqn, const symbols::Scope& scope,
                             const symbols::SymbolTable& symbol_model,
                             diagnostics::DiagnosticEngine& diagnostics, SemanticModel& model) {
    SemanticRecord record;
    record.source_range = declaration.source_range;
    record.fqn = qualify_fqn(current_namespace_fqn, declaration.name.text);

    if (declaration.version != 0 || declaration.version_source_range.is_valid()) {
        const std::optional<std::uint32_t> version =
            validate_positive_u32(declaration.version, declaration.version_source_range,
                                  diagnostics, "record '" + record.fqn + "'", "version");
        if (version.has_value()) {
            record.version = *version;
        }
    }

    if (!declaration.record_type_spelling.empty() ||
        declaration.record_type_source_range.is_valid()) {
        const std::optional<SemanticRecordType> record_type =
            canonical_record_type(declaration.record_type_spelling);
        if (record_type.has_value()) {
            record.record_type = *record_type;
        } else {
            auto builder = diagnostics::Diagnostic::create(
                diagnostic_id("BC5005"), diagnostics::Severity::Error,
                "record '" + record.fqn + "' has an invalid logical record type '" +
                    declaration.record_type_spelling + "'");
            builder.from_pass(std::string(semantic_pass));
            if (declaration.record_type_source_range.is_valid()) {
                builder.at(declaration.record_type_source_range);
            }
            diagnostics.emit(builder.build());
        }
    }

    record.fields.reserve(declaration.fields.size());
    for (const ast::FieldDeclarationSyntax& field : declaration.fields) {
        const std::optional<SemanticType> resolved =
            resolve_type(field, field.type, scope, symbol_model, diagnostics);
        if (!resolved.has_value()) {
            continue;
        }

        record.fields.push_back(SemanticField{
            .source_range = field.source_range, .name = field.name.text, .type = *resolved});
    }
    model.records.push_back(std::move(record));
}

void collect_semantic_declaration(const ast::DeclarationSyntax& declaration,
                                  const symbols::Scope& scope,
                                  const symbols::SymbolTable& symbol_model,
                                  diagnostics::DiagnosticEngine& diagnostics, SemanticModel& model,
                                  std::string current_namespace_fqn) {
    std::visit(
        [&](const auto& typed) {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, ast::NamespaceDeclarationSyntax>) {
                const symbols::Scope* namespace_scope =
                    scope_for_namespace(typed, scope, symbol_model);
                if (namespace_scope == nullptr) {
                    return;
                }

                const std::string namespace_fqn =
                    qualify_fqn(current_namespace_fqn, typed.name.text());
                for (const ast::DeclarationPtr& nested : typed.declarations) {
                    if (nested != nullptr) {
                        collect_semantic_declaration(*nested, *namespace_scope, symbol_model,
                                                     diagnostics, model, namespace_fqn);
                    }
                }
            } else if constexpr (std::is_same_v<Type, ast::RecordDeclarationSyntax>) {
                collect_semantic_record(typed, current_namespace_fqn, scope, symbol_model,
                                        diagnostics, model);
            } else if constexpr (std::is_same_v<Type, ast::EnumDeclarationSyntax>) {
                (void)typed;
            } else if constexpr (std::is_same_v<Type, ast::ImportDeclarationSyntax>) {
                (void)typed;
            }
        },
        declaration.value);
}

SemanticModel validate_schema_file(const ast::SchemaFileSyntax& ast,
                                   const symbols::SymbolTable& symbol_model,
                                   diagnostics::DiagnosticEngine& diagnostics) {
    SemanticModel model;
    const symbols::Scope& global_scope = symbol_model.global_scope();
    for (const ast::DeclarationPtr& declaration : ast.declarations) {
        if (declaration != nullptr) {
            collect_semantic_declaration(*declaration, global_scope, symbol_model, diagnostics,
                                         model, {});
        }
    }
    return model;
}

} // namespace

const SemanticRecord* SemanticModel::find_record(std::string_view fqn) const {
    const auto found =
        std::find_if(records.begin(), records.end(),
                     [fqn](const SemanticRecord& record) { return record.fqn == fqn; });
    if (found == records.end()) {
        return nullptr;
    }
    return &*found;
}

const SemanticField* SemanticRecord::find_field(std::string_view name) const {
    const auto found =
        std::find_if(fields.begin(), fields.end(),
                     [name](const SemanticField& field) { return field.name == name; });
    if (found == fields.end()) {
        return nullptr;
    }
    return &*found;
}

SemanticModel SemanticValidator::validate(const ast::Ast& ast,
                                          const symbols::SymbolTable& symbol_model,
                                          diagnostics::DiagnosticCollection& diagnostics) const {
    return validate_schema_file(ast, symbol_model, diagnostics);
}

SemanticArrayType::SemanticArrayType() = default;

SemanticArrayType::SemanticArrayType(const SemanticArrayType& other) {
    max_elements = other.max_elements;
    if (other.element_type != nullptr) {
        element_type = std::make_unique<SemanticType>(*other.element_type);
    }
}

SemanticArrayType& SemanticArrayType::operator=(const SemanticArrayType& other) {
    if (this != &other) {
        max_elements = other.max_elements;
        element_type.reset();
        if (other.element_type != nullptr) {
            element_type = std::make_unique<SemanticType>(*other.element_type);
        }
    }
    return *this;
}

SemanticType::SemanticType() = default;

SemanticType::SemanticType(SemanticPrimitiveType primitive) : value(primitive) {}

SemanticType::SemanticType(SemanticStringType string_type) : value(std::move(string_type)) {}

SemanticType::SemanticType(SemanticBytesType bytes_type) : value(std::move(bytes_type)) {}

SemanticType::SemanticType(SemanticRecordReferenceType record_reference)
    : value(std::move(record_reference)) {}

SemanticType::SemanticType(SemanticEnumReferenceType enum_reference)
    : value(std::move(enum_reference)) {}

SemanticType::SemanticType(SemanticArrayType array) : value(std::move(array)) {}

bool SemanticType::is_primitive() const {
    return std::holds_alternative<SemanticPrimitiveType>(value);
}

SemanticPrimitiveType SemanticType::primitive() const {
    return std::get<SemanticPrimitiveType>(value);
}

bool SemanticType::is_valid() const { return !std::holds_alternative<std::monostate>(value); }

bool SemanticType::is_string() const { return std::holds_alternative<SemanticStringType>(value); }

bool SemanticType::is_bytes() const { return std::holds_alternative<SemanticBytesType>(value); }

bool SemanticType::is_record_reference() const {
    return std::holds_alternative<SemanticRecordReferenceType>(value);
}

bool SemanticType::is_enum_reference() const {
    return std::holds_alternative<SemanticEnumReferenceType>(value);
}

bool SemanticType::is_array() const { return std::holds_alternative<SemanticArrayType>(value); }

const SemanticRecordReferenceType& SemanticType::record_reference() const {
    return std::get<SemanticRecordReferenceType>(value);
}

const SemanticEnumReferenceType& SemanticType::enum_reference() const {
    return std::get<SemanticEnumReferenceType>(value);
}

const SemanticArrayType& SemanticType::array() const { return std::get<SemanticArrayType>(value); }

} // namespace breadcrumbs::compiler::semantic
