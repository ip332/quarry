#pragma once

#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/context/compiler_context.hpp"

#include <string_view>

namespace breadcrumbs::compiler::parser {

struct ParseResult {
    ast::Ast ast;
};

class Parser {
public:
    [[nodiscard]] ParseResult parse(std::string_view source, context::CompilerContext& context,
                                    diagnostics::DiagnosticCollection& diagnostics) const;
};

} // namespace breadcrumbs::compiler::parser
