#pragma once

#include "compiler/ast/ast.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"

#include <vector>

namespace breadcrumbs::compiler::imports {

struct CompilationUnit {
    std::vector<ast::Ast> asts;
};

class ImportResolver {
public:
    [[nodiscard]] CompilationUnit resolve(const std::vector<ast::Ast>& asts,
                                          context::CompilerContext& context,
                                          diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace breadcrumbs::compiler::imports
