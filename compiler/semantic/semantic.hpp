#pragma once

#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/symbols/symbols.hpp"

namespace breadcrumbs::compiler::semantic {

struct SemanticModel {};

class SemanticValidator {
public:
    [[nodiscard]] SemanticModel validate(const ast::Ast& ast,
                                         const symbols::SymbolModel& symbol_model,
                                         diagnostics::DiagnosticEngine& diagnostics) const;
};

} // namespace breadcrumbs::compiler::semantic
