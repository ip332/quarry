#pragma once

#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/imports/imports.hpp"

#include <string>
#include <vector>

namespace breadcrumbs::compiler::symbols {

struct Symbol {
    std::string fully_qualified_name;
};

struct SymbolModel {
    std::vector<Symbol> symbols;
};

class NamespaceBuilder {
public:
    [[nodiscard]] SymbolModel build(const imports::CompilationUnit& compilation_unit,
                                    context::CompilerContext& context,
                                    diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace breadcrumbs::compiler::symbols
