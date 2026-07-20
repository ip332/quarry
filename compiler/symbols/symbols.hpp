#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/support/source_location.hpp"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quarry::compiler::symbols {

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
    std::string fqn;
    support::SourceRange source_range;
    const Scope* child_scope = nullptr;
};

class Scope {
public:
    Scope();
    Scope(ScopeKind kind, std::string name, const Scope* parent = nullptr);

    [[nodiscard]] ScopeKind kind() const;
    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] const Scope* parent() const;
    [[nodiscard]] const std::deque<Symbol>& symbols() const;
    [[nodiscard]] const Symbol* find_local(std::string_view name) const;
    [[nodiscard]] const Symbol* find_enclosing(std::string_view name) const;

private:
    friend class NamespaceBuilder;
    friend class SymbolTable;

    [[nodiscard]] Symbol& add_symbol(Symbol symbol);
    [[nodiscard]] Scope& add_child_scope(std::string name);
    void rebind_parent(const Scope* parent);

    ScopeKind kind_ = ScopeKind::Global;
    std::string name_;
    const Scope* parent_ = nullptr;
    std::deque<Symbol> symbols_;
    std::vector<std::unique_ptr<Scope>> child_scopes_;
};

class SymbolTable {
public:
    SymbolTable() = default;
    SymbolTable(SymbolTable&& other) noexcept;
    SymbolTable& operator=(SymbolTable&& other) noexcept;

    [[nodiscard]] const Scope& global_scope() const;
    [[nodiscard]] Scope& global_scope();

    [[nodiscard]] const Symbol* resolve_unqualified(std::string_view name,
                                                    const Scope& scope) const;
    [[nodiscard]] const Symbol*
    resolve_qualified(const source_schema::SourceSchemaQualifiedName& name,
                      const Scope& scope) const;
    [[nodiscard]] const Symbol* resolve(const source_schema::SourceSchemaQualifiedName& name,
                                        const Scope& scope) const;
    [[nodiscard]] const Symbol*
    resolve_or_diagnostic(const source_schema::SourceSchemaQualifiedName& name,
                          const Scope& scope, diagnostics::DiagnosticEngine& diagnostics) const;

    [[nodiscard]] const Symbol* lookup_unqualified(std::string_view name,
                                                   const Scope& scope) const {
        return resolve_unqualified(name, scope);
    }
    [[nodiscard]] const Symbol*
    lookup(const source_schema::SourceSchemaQualifiedName& name, const Scope& scope) const {
        return resolve(name, scope);
    }
    [[nodiscard]] const Symbol*
    lookup_or_diagnostic(const source_schema::SourceSchemaQualifiedName& name,
                         const Scope& scope, diagnostics::DiagnosticEngine& diagnostics) const;

private:
    Scope global_scope_;
};

class NamespaceBuilder {
public:
    [[nodiscard]] SymbolTable
    build(const source_schema::NormalizedSourceSchemaDocument& schema,
          diagnostics::DiagnosticEngine& diagnostics) const;

private:
    void collect_source_schema(const source_schema::NormalizedSourceSchemaDocument& schema,
                               Scope& scope, diagnostics::DiagnosticEngine& diagnostics) const;
    Scope& ensure_namespace_path(Scope& scope, const source_schema::SourceSchemaQualifiedName& name,
                                 support::SourceRange declaration_range,
                                 diagnostics::DiagnosticEngine& diagnostics) const;
    void register_named_declaration(
        Scope& scope, SymbolKind kind, const source_schema::SourceSchemaIdentifier& name,
        support::SourceRange declaration_range, diagnostics::DiagnosticEngine& diagnostics) const;
};

} // namespace quarry::compiler::symbols
