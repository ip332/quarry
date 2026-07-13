#pragma once

#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/yaml/source_schema.hpp"

#include <optional>

namespace breadcrumbs::compiler::yaml {

struct SourceSchemaLoweringResult {
    std::optional<ast::Ast> ast;
};

[[nodiscard]] SourceSchemaLoweringResult
lower_source_schema(const SourceSchemaDocument& schema, diagnostics::DiagnosticEngine& diagnostics);

} // namespace breadcrumbs::compiler::yaml
