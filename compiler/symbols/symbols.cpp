#include "compiler/symbols/symbols.hpp"

namespace breadcrumbs::compiler::symbols {

SymbolModel NamespaceBuilder::build(const imports::CompilationUnit&, context::CompilerContext&,
                                    diagnostics::DiagnosticCollection&) const {
    return {};
}

} // namespace breadcrumbs::compiler::symbols
