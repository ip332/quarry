#include "compiler/semantic/semantic.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace breadcrumbs::compiler::semantic {
namespace {

constexpr std::string_view semantic_pass = "semantic";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] bool is_builtin_type(std::string_view name) {
    static constexpr std::array<std::string_view, 23> builtin_types = {
        "bool",   "int8",    "int16",   "int32", "int64", "uint8",  "uint16", "uint32",
        "uint64", "i8",      "i16",     "i32",   "i64",   "u8",     "u16",    "u32",
        "u64",    "float32", "float64", "f32",   "f64",   "string", "bytes",
    };

    return std::find(builtin_types.begin(), builtin_types.end(), name) != builtin_types.end();
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
                        const ast::ArrayTypeSyntax& array_type) {
    if (!array_type.fixed_size.has_value()) {
        return;
    }

    diagnostics.emit(diagnostics::Diagnostic::create(diagnostic_id("BC5003"),
                                                     diagnostics::Severity::Error,
                                                     "fixed-size arrays are not supported")
                         .at(array_type.source_range)
                         .from_pass(std::string(semantic_pass))
                         .build());
}

const symbols::Scope* scope_for_namespace(const ast::NamespaceDeclarationSyntax& declaration,
                                          const symbols::Scope& current_scope,
                                          const symbols::SymbolModel& symbol_model) {
    const symbols::Symbol* symbol = symbol_model.resolve(declaration.name, current_scope);
    if (symbol == nullptr || symbol->kind != symbols::SymbolKind::Namespace ||
        symbol->child_scope == nullptr) {
        return nullptr;
    }

    return symbol->child_scope;
}

void validate_type_reference(const ast::TypeReferenceSyntax& type_reference,
                             const symbols::Scope& scope, const symbols::SymbolModel& symbol_model,
                             diagnostics::DiagnosticEngine& diagnostics) {
    if (is_builtin_type(type_reference.name.text())) {
        return;
    }

    ast::QualifiedNameSyntax normalized_name{
        .source_range = type_reference.name.source_range,
        .parts = type_reference.name.parts,
    };
    const symbols::Symbol* symbol = symbol_model.resolve(normalized_name, scope);
    if (symbol == nullptr) {
        emit_unresolved_type(diagnostics, type_reference);
        return;
    }

    if (!is_type_symbol(symbol)) {
        emit_invalid_type_position(diagnostics, type_reference, *symbol);
    }
}

void validate_type(const ast::TypeSyntax& type, const symbols::Scope& scope,
                   const symbols::SymbolModel& symbol_model,
                   diagnostics::DiagnosticEngine& diagnostics) {
    std::visit(
        [&](const auto& typed) {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, ast::TypeReferenceSyntax>) {
                validate_type_reference(typed, scope, symbol_model, diagnostics);
            } else if constexpr (std::is_same_v<Type, ast::ArrayTypeSyntax>) {
                emit_invalid_array(diagnostics, typed);
                validate_type_reference(typed.element_type, scope, symbol_model, diagnostics);
            }
        },
        type);
}

void validate_declaration(const ast::DeclarationSyntax& declaration, const symbols::Scope& scope,
                          const symbols::SymbolModel& symbol_model,
                          diagnostics::DiagnosticEngine& diagnostics) {
    std::visit(
        [&](const auto& typed) {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, ast::NamespaceDeclarationSyntax>) {
                const symbols::Scope* namespace_scope =
                    scope_for_namespace(typed, scope, symbol_model);
                if (namespace_scope == nullptr) {
                    return;
                }

                for (const ast::DeclarationPtr& nested : typed.declarations) {
                    if (nested != nullptr) {
                        validate_declaration(*nested, *namespace_scope, symbol_model, diagnostics);
                    }
                }
            } else if constexpr (std::is_same_v<Type, ast::RecordDeclarationSyntax>) {
                for (const ast::FieldDeclarationSyntax& field : typed.fields) {
                    validate_type(field.type, scope, symbol_model, diagnostics);
                }
            } else if constexpr (std::is_same_v<Type, ast::EnumDeclarationSyntax>) {
                (void)typed;
            } else if constexpr (std::is_same_v<Type, ast::ImportDeclarationSyntax>) {
                (void)typed;
            }
        },
        declaration.value);
}

void validate_schema_file(const ast::SchemaFileSyntax& ast,
                          const symbols::SymbolModel& symbol_model,
                          diagnostics::DiagnosticEngine& diagnostics) {
    const symbols::Scope& global_scope = symbol_model.global_scope();
    for (const ast::DeclarationPtr& declaration : ast.declarations) {
        if (declaration != nullptr) {
            validate_declaration(*declaration, global_scope, symbol_model, diagnostics);
        }
    }
}

} // namespace

SemanticModel SemanticValidator::validate(const ast::Ast& ast,
                                          const symbols::SymbolModel& symbol_model,
                                          diagnostics::DiagnosticEngine& diagnostics) const {
    validate_schema_file(ast, symbol_model, diagnostics);
    return {};
}

} // namespace breadcrumbs::compiler::semantic
