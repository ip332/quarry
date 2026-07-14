#include "compiler/ast/ast.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/imports/imports.hpp"

#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(ImportsSmokeTest, PreservesInputAstOrderInCompilationUnit) {
    breadcrumbs::compiler::imports::ImportResolver resolver;
    breadcrumbs::compiler::context::CompilerContext context;
    breadcrumbs::compiler::diagnostics::DiagnosticCollection diagnostics;

    std::vector<breadcrumbs::compiler::ast::Ast> asts(2);
    const breadcrumbs::compiler::imports::CompilationUnit unit =
        resolver.resolve(asts, context, diagnostics);

    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(unit.asts.size(), 2U);
    EXPECT_EQ(unit.asts[0], asts.data());
    EXPECT_EQ(unit.asts[1], asts.data() + 1);
}

} // namespace
