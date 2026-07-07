#include "compiler/parser/parser.hpp"

namespace breadcrumbs::compiler::parser {

ParseResult Parser::parse(std::string_view, context::CompilerContext&,
                          diagnostics::DiagnosticCollection&) const {
    return {};
}

} // namespace breadcrumbs::compiler::parser
