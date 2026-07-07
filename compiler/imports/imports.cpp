#include "compiler/imports/imports.hpp"

namespace breadcrumbs::compiler::imports {

CompilationUnit ImportResolver::resolve(const std::vector<ast::Ast>& asts,
                                        context::CompilerContext&,
                                        diagnostics::DiagnosticCollection&) const {
    CompilationUnit unit;
    unit.asts.reserve(asts.size());
    for (const ast::Ast& ast : asts) {
        unit.asts.push_back(&ast);
    }
    return unit;
}

} // namespace breadcrumbs::compiler::imports
