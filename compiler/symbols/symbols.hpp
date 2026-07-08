#pragma once

#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/imports/imports.hpp"
#include "compiler/support/source_location.hpp"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace breadcrumbs::compiler::symbols {

class Scope;

enum class SymbolKind {
    Namespace,
    Record,
    Enum,
};

enum class ScopeKind {
    Global,
    Namespace,
};

struct Symbol {
    SymbolKind kind = SymbolKind::Record;
    std::string name;
    support::SourceRange source_range;
    const ast::DeclarationSyntax* declaration = nullptr;
    const Scope* child_scope = nullptr;
};

class Scope {
public:
    Scope();
    Scope(ScopeKind kind, std::string name, const Scope* parent = nullptr,
          const ast::NamespaceDeclarationSyntax* namespace_declaration = nullptr);

    [[nodiscard]] ScopeKind kind() const;
    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] const Scope* parent() const;
    [[nodiscard]] const ast::NamespaceDeclarationSyntax* namespace_declaration() const;
    [[nodiscard]] const std::deque<Symbol>& symbols() const;
    [[nodiscard]] const Symbol* find_local(std::string_view name) const;
    [[nodiscard]] const Symbol* find_enclosing(std::string_view name) const;

private:
    friend class NamespaceBuilder;
    friend class SymbolModel;

    [[nodiscard]] Symbol& add_symbol(Symbol symbol);
    [[nodiscard]] Scope&
    add_child_scope(std::string name, const ast::NamespaceDeclarationSyntax* namespace_declaration);

    ScopeKind kind_ = ScopeKind::Global;
    std::string name_;
    const Scope* parent_ = nullptr;
    const ast::NamespaceDeclarationSyntax* namespace_declaration_ = nullptr;
    std::deque<Symbol> symbols_;
    std::vector<std::unique_ptr<Scope>> child_scopes_;
};

class SymbolModel {
public:
    [[nodiscard]] const Scope& global_scope() const;
    [[nodiscard]] Scope& global_scope();

    [[nodiscard]] const Symbol* resolve_unqualified(std::string_view name,
                                                    const Scope& scope) const;
    [[nodiscard]] const Symbol* resolve_qualified(const ast::QualifiedNameSyntax& name,
                                                  const Scope& scope) const;
    [[nodiscard]] const Symbol* resolve(const ast::QualifiedNameSyntax& name,
                                        const Scope& scope) const;
    [[nodiscard]] const Symbol*
    resolve_or_diagnostic(const ast::QualifiedNameSyntax& name, const Scope& scope,
                          diagnostics::DiagnosticEngine& diagnostics) const;

private:
    Scope global_scope_;
};

class NamespaceBuilder {
public:
    [[nodiscard]] SymbolModel build(const imports::CompilationUnit& compilation_unit,
                                    diagnostics::DiagnosticEngine& diagnostics) const;

private:
    void collect_schema_file(const ast::SchemaFileSyntax& schema_file, Scope& scope,
                             diagnostics::DiagnosticEngine& diagnostics) const;
    void collect_declaration(const ast::DeclarationSyntax& declaration, Scope& scope,
                             diagnostics::DiagnosticEngine& diagnostics) const;
    Scope& ensure_namespace_path(Scope& scope, const ast::NamespaceDeclarationSyntax& declaration,
                                 const ast::DeclarationSyntax& wrapper,
                                 support::SourceRange declaration_range,
                                 diagnostics::DiagnosticEngine& diagnostics) const;
    void register_named_declaration(Scope& scope, SymbolKind kind, const std::string& name,
                                    support::SourceRange declaration_range,
                                    const ast::DeclarationSyntax& declaration,
                                    diagnostics::DiagnosticEngine& diagnostics) const;
};

} // namespace breadcrumbs::compiler::symbols
