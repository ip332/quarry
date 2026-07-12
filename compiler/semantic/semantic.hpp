#pragma once

#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/support/source_location.hpp"
#include "compiler/symbols/symbols.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace breadcrumbs::compiler::semantic {

struct SemanticField {
    support::SourceRange source_range;
    std::string name;
};

struct SemanticRecord {
    support::SourceRange source_range;
    std::string fqn;
    std::vector<SemanticField> fields;
};

struct SemanticModel {
    std::vector<SemanticRecord> records;

    [[nodiscard]] const SemanticRecord* find_record(std::string_view fqn) const;
};

class SemanticValidator {
public:
    [[nodiscard]] SemanticModel validate(const ast::Ast& ast,
                                         const symbols::SymbolTable& symbol_model,
                                         diagnostics::DiagnosticEngine& diagnostics) const;
};

} // namespace breadcrumbs::compiler::semantic
