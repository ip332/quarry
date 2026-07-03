#include "compiler/layout/layout.hpp"

namespace breadcrumbs::compiler::layout {

LayoutModel LayoutComputer::compute(const semantic::SemanticModel&, support::CompilerContext&,
                                    diagnostics::DiagnosticCollection&) const {
    return {};
}

} // namespace breadcrumbs::compiler::layout
