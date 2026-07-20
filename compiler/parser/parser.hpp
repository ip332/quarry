#pragma once

#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/support/source_manager.hpp"

namespace quarry::compiler::parser {

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

} // namespace quarry::compiler::parser
