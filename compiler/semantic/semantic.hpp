#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/symbols/symbols.hpp"

#include <string>
#include <vector>

namespace breadcrumbs::compiler::semantic {

struct SemanticRecord {
    std::string fully_qualified_name;
};

struct SemanticModel {
    std::vector<SemanticRecord> records;
};

class SemanticValidator {
public:
    [[nodiscard]] SemanticModel validate(const symbols::SymbolModel& symbol_model,
                                         context::CompilerContext& context,
                                         diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace breadcrumbs::compiler::semantic
