#include "compiler/symbols/symbols.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>

namespace breadcrumbs::compiler::symbols {
namespace {

constexpr std::string_view symbol_pass = "symbols";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] std::string_view symbol_kind_name(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Namespace:
        return "namespace";
    case SymbolKind::Record:
        return "record";
    case SymbolKind::Enum:
        return "enum";
    }
    return "symbol";
}

[[nodiscard]] std::string qualified_name_text(const ast::QualifiedNameSyntax& name) {
    return name.text();
}

void emit_duplicate(diagnostics::DiagnosticEngine& diagnostics, SymbolKind kind,
                    std::string_view name, support::SourceRange current_range,
                    support::SourceRange previous_range) {
    diagnostics.emit(
        diagnostics::Diagnostic::create(diagnostic_id("BC4001"), diagnostics::Severity::Error,
                                        "duplicate " + std::string(symbol_kind_name(kind)) +
                                            " declaration '" + std::string(name) + "'")
            .at(current_range)
            .with_related(diagnostics::RelatedLocation::at_range(previous_range,
                                                                 "previous declaration is here"))
            .from_pass(std::string(symbol_pass))
            .build());
}

void emit_unresolved(diagnostics::DiagnosticEngine& diagnostics,
                     const ast::QualifiedNameSyntax& name) {
    diagnostics.emit(
        diagnostics::Diagnostic::create(diagnostic_id("BC4002"), diagnostics::Severity::Error,
                                        "unresolved name '" + qualified_name_text(name) + "'")
            .at(name.source_range)
            .from_pass(std::string(symbol_pass))
            .build());
}

[[nodiscard]] bool is_namespace_symbol(const Symbol* symbol) {
    return symbol != nullptr && symbol->kind == SymbolKind::Namespace &&
           symbol->child_scope != nullptr;
}

[[nodiscard]] const ast::DeclarationSyntax& as_declaration(const ast::DeclarationPtr& declaration) {
    return *declaration;
}

} // namespace

Scope::Scope() = default;

Scope::Scope(ScopeKind kind, std::string name, const Scope* parent,
             const ast::NamespaceDeclarationSyntax* namespace_declaration)
    : kind_(kind), name_(std::move(name)), parent_(parent),
      namespace_declaration_(namespace_declaration) {}

ScopeKind Scope::kind() const { return kind_; }

const std::string& Scope::name() const { return name_; }

const Scope* Scope::parent() const { return parent_; }

const ast::NamespaceDeclarationSyntax* Scope::namespace_declaration() const {
    return namespace_declaration_;
}

const std::deque<Symbol>& Scope::symbols() const { return symbols_; }

const Symbol* Scope::find_local(std::string_view name) const {
    const auto found = std::find_if(symbols_.begin(), symbols_.end(),
                                    [name](const Symbol& symbol) { return symbol.name == name; });
    if (found == symbols_.end()) {
        return nullptr;
    }
    return &*found;
}

const Symbol* Scope::find_enclosing(std::string_view name) const {
    for (const Scope* current = this; current != nullptr; current = current->parent()) {
        if (const Symbol* symbol = current->find_local(name); symbol != nullptr) {
            return symbol;
        }
    }
    return nullptr;
}

Symbol& Scope::add_symbol(Symbol symbol) {
    symbols_.push_back(std::move(symbol));
    return symbols_.back();
}

Scope& Scope::add_child_scope(std::string name,
                              const ast::NamespaceDeclarationSyntax* namespace_declaration) {
    child_scopes_.push_back(std::make_unique<Scope>(ScopeKind::Namespace, std::move(name), this,
                                                    namespace_declaration));
    return *child_scopes_.back();
}

const Scope& SymbolModel::global_scope() const { return global_scope_; }

Scope& SymbolModel::global_scope() { return global_scope_; }

const Symbol* SymbolModel::resolve_unqualified(std::string_view name, const Scope& scope) const {
    return scope.find_enclosing(name);
}

const Symbol* SymbolModel::resolve_qualified(const ast::QualifiedNameSyntax& name,
                                             const Scope& scope) const {
    if (name.parts.empty()) {
        return nullptr;
    }

    const Symbol* symbol = resolve_unqualified(name.parts.front().text, scope);
    if (symbol == nullptr) {
        return nullptr;
    }

    for (std::size_t index = 1; index < name.parts.size(); ++index) {
        if (!is_namespace_symbol(symbol)) {
            return nullptr;
        }

        symbol = symbol->child_scope->find_local(name.parts[index].text);
        if (symbol == nullptr) {
            return nullptr;
        }
    }

    return symbol;
}

const Symbol* SymbolModel::resolve(const ast::QualifiedNameSyntax& name, const Scope& scope) const {
    if (name.parts.size() == 1) {
        return resolve_unqualified(name.parts.front().text, scope);
    }
    return resolve_qualified(name, scope);
}

const Symbol* SymbolModel::resolve_or_diagnostic(const ast::QualifiedNameSyntax& name,
                                                 const Scope& scope,
                                                 diagnostics::DiagnosticEngine& diagnostics) const {
    const Symbol* symbol = resolve(name, scope);
    if (symbol != nullptr) {
        return symbol;
    }

    emit_unresolved(diagnostics, name);
    return nullptr;
}

SymbolModel NamespaceBuilder::build(const imports::CompilationUnit& compilation_unit,
                                    diagnostics::DiagnosticEngine& diagnostics) const {
    SymbolModel model;
    for (const ast::Ast* ast : compilation_unit.asts) {
        if (ast != nullptr) {
            collect_schema_file(*ast, model.global_scope(), diagnostics);
        }
    }
    return model;
}

void NamespaceBuilder::collect_schema_file(const ast::SchemaFileSyntax& schema_file, Scope& scope,
                                           diagnostics::DiagnosticEngine& diagnostics) const {
    for (const ast::DeclarationPtr& declaration : schema_file.declarations) {
        if (declaration != nullptr) {
            collect_declaration(as_declaration(declaration), scope, diagnostics);
        }
    }
}

void NamespaceBuilder::collect_declaration(const ast::DeclarationSyntax& declaration, Scope& scope,
                                           diagnostics::DiagnosticEngine& diagnostics) const {
    std::visit(
        [&](const auto& typed) {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, ast::NamespaceDeclarationSyntax>) {
                Scope& namespace_scope = ensure_namespace_path(scope, typed, declaration,
                                                               typed.source_range, diagnostics);
                for (const ast::DeclarationPtr& nested : typed.declarations) {
                    if (nested != nullptr) {
                        collect_declaration(as_declaration(nested), namespace_scope, diagnostics);
                    }
                }
            } else if constexpr (std::is_same_v<Type, ast::RecordDeclarationSyntax>) {
                register_named_declaration(scope, SymbolKind::Record, typed.name.text,
                                           typed.source_range, declaration, diagnostics);
            } else if constexpr (std::is_same_v<Type, ast::EnumDeclarationSyntax>) {
                register_named_declaration(scope, SymbolKind::Enum, typed.name.text,
                                           typed.source_range, declaration, diagnostics);
            } else if constexpr (std::is_same_v<Type, ast::ImportDeclarationSyntax>) {
                (void)typed;
            }
        },
        declaration.value);
}

Scope& NamespaceBuilder::ensure_namespace_path(Scope& scope,
                                               const ast::NamespaceDeclarationSyntax& declaration,
                                               const ast::DeclarationSyntax& wrapper,
                                               support::SourceRange declaration_range,
                                               diagnostics::DiagnosticEngine& diagnostics) const {
    Scope* current_scope = &scope;

    for (std::size_t index = 0; index < declaration.name.parts.size(); ++index) {
        const ast::IdentifierSyntax& part = declaration.name.parts[index];
        const bool is_last = index + 1 == declaration.name.parts.size();
        const Symbol* existing = current_scope->find_local(part.text);

        if (existing == nullptr) {
            Symbol& symbol = current_scope->add_symbol(Symbol{
                .kind = SymbolKind::Namespace,
                .name = part.text,
                .source_range = declaration_range,
                .declaration = &wrapper,
                .child_scope = nullptr,
            });
            Scope& child_scope = current_scope->add_child_scope(part.text, &declaration);
            symbol.child_scope = &child_scope;
            current_scope = &child_scope;
            continue;
        }

        if (!is_namespace_symbol(existing)) {
            emit_duplicate(diagnostics, SymbolKind::Namespace, part.text, declaration_range,
                           existing->source_range);
            return *current_scope;
        }

        if (is_last) {
            emit_duplicate(diagnostics, SymbolKind::Namespace, part.text, declaration_range,
                           existing->source_range);
        }

        current_scope = const_cast<Scope*>(existing->child_scope);
    }

    return *current_scope;
}

void NamespaceBuilder::register_named_declaration(
    Scope& scope, SymbolKind kind, const std::string& name, support::SourceRange declaration_range,
    const ast::DeclarationSyntax& declaration, diagnostics::DiagnosticEngine& diagnostics) const {
    const Symbol* existing = scope.find_local(name);
    if (existing != nullptr) {
        emit_duplicate(diagnostics, kind, name, declaration_range, existing->source_range);
        return;
    }

    Symbol& symbol = scope.add_symbol(Symbol{
        .kind = kind,
        .name = name,
        .source_range = declaration_range,
        .declaration = &declaration,
        .child_scope = nullptr,
    });
    (void)symbol;
}

} // namespace breadcrumbs::compiler::symbols
