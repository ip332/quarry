#include "compiler/symbols/symbols.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <sstream>
#include <utility>

namespace quarry::compiler::symbols {
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

[[nodiscard]] std::string scope_fqn(const Scope& scope) {
    std::vector<std::string_view> parts;
    for (const Scope* current = &scope; current != nullptr; current = current->parent()) {
        if (current->kind() == ScopeKind::Namespace && !current->name().empty()) {
            parts.push_back(current->name());
        }
    }

    std::string fqn;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!fqn.empty()) {
            fqn.push_back('.');
        }
        fqn.append(*it);
    }
    return fqn;
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
                     const source_schema::SourceSchemaQualifiedName& name) {
    diagnostics.emit(
        diagnostics::Diagnostic::create(diagnostic_id("BC4002"), diagnostics::Severity::Error,
                                        "unresolved name '" + name.text() + "'")
            .at(name.source_range)
            .from_pass(std::string(symbol_pass))
            .build());
}

[[nodiscard]] bool is_namespace_symbol(const Symbol* symbol) {
    return symbol != nullptr && symbol->kind == SymbolKind::Namespace &&
           symbol->child_scope != nullptr;
}

} // namespace

Scope::Scope() = default;

Scope::Scope(ScopeKind kind, std::string name, const Scope* parent)
    : kind_(kind), name_(std::move(name)), parent_(parent) {}

ScopeKind Scope::kind() const { return kind_; }

const std::string& Scope::name() const { return name_; }

const Scope* Scope::parent() const { return parent_; }

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

Scope& Scope::add_child_scope(std::string name) {
    child_scopes_.push_back(std::make_unique<Scope>(ScopeKind::Namespace, std::move(name), this));
    return *child_scopes_.back();
}

void Scope::rebind_parent(const Scope* parent) {
    parent_ = parent;
    for (const std::unique_ptr<Scope>& child : child_scopes_) {
        if (child != nullptr) {
            child->rebind_parent(this);
        }
    }
}

const Scope& SymbolTable::global_scope() const { return global_scope_; }

Scope& SymbolTable::global_scope() { return global_scope_; }

SymbolTable::SymbolTable(SymbolTable&& other) noexcept
    : global_scope_(std::move(other.global_scope_)) {
    global_scope_.rebind_parent(nullptr);
}

SymbolTable& SymbolTable::operator=(SymbolTable&& other) noexcept {
    if (this != &other) {
        global_scope_ = std::move(other.global_scope_);
        global_scope_.rebind_parent(nullptr);
    }
    return *this;
}

const Symbol* SymbolTable::resolve_unqualified(std::string_view name, const Scope& scope) const {
    return scope.find_enclosing(name);
}

const Scope* SymbolTable::resolve_namespace(
    const source_schema::SourceSchemaQualifiedName& name, const Scope& scope) const {
    if (name.parts.empty()) {
        return nullptr;
    }

    const Symbol* symbol = resolve_unqualified(name.parts.front().text, scope);
    if (!is_namespace_symbol(symbol)) {
        return nullptr;
    }

    for (std::size_t index = 1; index < name.parts.size(); ++index) {
        symbol = symbol->child_scope->find_local(name.parts[index].text);
        if (!is_namespace_symbol(symbol)) {
            return nullptr;
        }
    }
    return symbol->child_scope;
}

const Symbol* SymbolTable::resolve_qualified(const source_schema::SourceSchemaQualifiedName& name,
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

const Symbol* SymbolTable::resolve(const source_schema::SourceSchemaQualifiedName& name,
                                   const Scope& scope) const {
    if (name.parts.size() == 1) {
        return resolve_unqualified(name.parts.front().text, scope);
    }
    return resolve_qualified(name, scope);
}

const Symbol*
SymbolTable::resolve_or_diagnostic(const source_schema::SourceSchemaQualifiedName& name,
                                   const Scope& scope,
                                   diagnostics::DiagnosticEngine& diagnostics) const {
    const Symbol* symbol = resolve(name, scope);
    if (symbol != nullptr) {
        return symbol;
    }

    emit_unresolved(diagnostics, name);
    return nullptr;
}

const Symbol*
SymbolTable::lookup_or_diagnostic(const source_schema::SourceSchemaQualifiedName& name,
                                  const Scope& scope,
                                  diagnostics::DiagnosticEngine& diagnostics) const {
    return resolve_or_diagnostic(name, scope, diagnostics);
}

SymbolTable NamespaceBuilder::build(const source_schema::NormalizedSourceSchemaDocument& schema,
                                    diagnostics::DiagnosticEngine& diagnostics) const {
    const std::vector<const source_schema::NormalizedSourceSchemaDocument*> schemas{&schema};
    return build(schemas, diagnostics);
}

SymbolTable NamespaceBuilder::build(
    const std::vector<const source_schema::NormalizedSourceSchemaDocument*>& schemas,
    diagnostics::DiagnosticEngine& diagnostics) const {
    SymbolTable model;
    for (const source_schema::NormalizedSourceSchemaDocument* schema : schemas) {
        if (schema != nullptr) {
            collect_source_schema(*schema, model.global_scope(), diagnostics);
        }
    }
    return model;
}

void NamespaceBuilder::collect_source_schema(
    const source_schema::NormalizedSourceSchemaDocument& schema, Scope& scope,
    diagnostics::DiagnosticEngine& diagnostics) const {
    Scope& namespace_scope =
        ensure_namespace_path(scope, schema.namespace_name, schema.namespace_name.source_range,
                              diagnostics);

    for (const source_schema::NormalizedSourceSchemaEnum& enumeration : schema.enums) {
        register_named_declaration(namespace_scope, SymbolKind::Enum, enumeration.name,
                                   enumeration.source_range, diagnostics);
    }

    register_named_declaration(namespace_scope, SymbolKind::Record, schema.record_name,
                               schema.record_source_range, diagnostics);
}

Scope& NamespaceBuilder::ensure_namespace_path(
    Scope& scope, const source_schema::SourceSchemaQualifiedName& declaration,
    support::SourceRange declaration_range, diagnostics::DiagnosticEngine& diagnostics) const {
    Scope* current_scope = &scope;

    for (std::size_t index = 0; index < declaration.parts.size(); ++index) {
        const source_schema::SourceSchemaIdentifier& part = declaration.parts[index];
        const Symbol* existing = current_scope->find_local(part.text);

        if (existing == nullptr) {
            const std::string namespace_fqn = current_scope->kind() == ScopeKind::Global
                                                  ? std::string()
                                                  : scope_fqn(*current_scope);
            const std::string part_fqn = namespace_fqn.empty()
                                             ? std::string(part.text)
                                             : namespace_fqn + "." + std::string(part.text);
            Symbol& symbol = current_scope->add_symbol(Symbol{
                .kind = SymbolKind::Namespace,
                .name = part.text,
                .fqn = part_fqn,
                .source_range = declaration_range,
                .child_scope = nullptr,
            });
            Scope& child_scope = current_scope->add_child_scope(part.text);
            symbol.child_scope = &child_scope;
            current_scope = &child_scope;
            continue;
        }

        if (!is_namespace_symbol(existing)) {
            emit_duplicate(diagnostics, SymbolKind::Namespace, part.text, declaration_range,
                           existing->source_range);
            return *current_scope;
        }

        current_scope = const_cast<Scope*>(existing->child_scope);
    }

    return *current_scope;
}

void NamespaceBuilder::register_named_declaration(
    Scope& scope, SymbolKind kind, const source_schema::SourceSchemaIdentifier& name,
    support::SourceRange declaration_range, diagnostics::DiagnosticEngine& diagnostics) const {
    const Symbol* existing = scope.find_local(name.text);
    if (existing != nullptr) {
        emit_duplicate(diagnostics, kind, name.text, declaration_range, existing->source_range);
        return;
    }

    const std::string scope_name = scope_fqn(scope);
    (void)scope.add_symbol(Symbol{
        .kind = kind,
        .name = name.text,
        .fqn = scope_name.empty() ? name.text : scope_name + "." + name.text,
        .source_range = declaration_range,
        .child_scope = nullptr,
    });
}

} // namespace quarry::compiler::symbols
