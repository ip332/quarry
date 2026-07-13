#pragma once

#include "compiler/source_schema/source_schema.hpp"
#include "compiler/source_schema/source_schema_lowering.hpp"

namespace breadcrumbs::compiler::yaml {

using SourceSchemaDocument = source_schema::SourceSchemaDocument;
using SourceSchemaLoweringResult = source_schema::SourceSchemaLoweringResult;
[[nodiscard]] SourceSchemaLoweringResult
lower_source_schema(const SourceSchemaDocument& schema, diagnostics::DiagnosticEngine& diagnostics);
using source_schema::lower_source_schema;

} // namespace breadcrumbs::compiler::yaml
