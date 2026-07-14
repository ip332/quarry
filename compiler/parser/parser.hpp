#pragma once

#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/support/source_manager.hpp"

namespace breadcrumbs::compiler::parser {

struct ParseResult {
    support::SourceFileId source_file_id;
    ast::Ast ast;
};

class Parser {
public:
    [[nodiscard]] static ParseResult parse(const support::SourceManager& source_manager,
                                           support::SourceFileId source_file_id,
                                           diagnostics::DiagnosticEngine& diagnostics);
};

} // namespace breadcrumbs::compiler::parser
