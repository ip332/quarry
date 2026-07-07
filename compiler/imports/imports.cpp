#include "compiler/imports/imports.hpp"

namespace breadcrumbs::compiler::imports {

CompilationUnit ImportResolver::resolve(const std::vector<ast::Ast>& asts,
                                        context::CompilerContext&,
                                        diagnostics::DiagnosticCollection&) const {
    return CompilationUnit{.asts = asts};
}

} // namespace breadcrumbs::compiler::imports
