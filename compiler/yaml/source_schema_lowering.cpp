#include "compiler/yaml/source_schema_lowering.hpp"

namespace breadcrumbs::compiler::yaml {

SourceSchemaLoweringResult lower_source_schema(const SourceSchemaDocument& schema,
                                               diagnostics::DiagnosticEngine& diagnostics) {
    const source_schema::SourceSchemaNormalizationResult normalization_result =
        source_schema::normalize_source_schema(schema, diagnostics);
    if (!normalization_result.document.has_value()) {
        return {};
    }

    return source_schema::lower_source_schema(*normalization_result.document, diagnostics);
}

} // namespace breadcrumbs::compiler::yaml
