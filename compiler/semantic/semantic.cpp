#include "compiler/semantic/semantic.hpp"

namespace breadcrumbs::compiler::semantic {

SemanticModel SemanticValidator::validate(const symbols::SymbolModel&, support::CompilerContext&,
                                          diagnostics::DiagnosticCollection&) const {
    return {};
}

} // namespace breadcrumbs::compiler::semantic
