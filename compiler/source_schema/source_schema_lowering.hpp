#pragma once

#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/source_schema/source_schema.hpp"

#include <optional>

namespace breadcrumbs::compiler::source_schema {

struct SourceSchemaLoweringResult {
    std::optional<ast::Ast> ast;
};

[[nodiscard]] SourceSchemaLoweringResult
lower_source_schema(const NormalizedSourceSchemaDocument& schema,
                    diagnostics::DiagnosticEngine& diagnostics);

} // namespace breadcrumbs::compiler::source_schema
