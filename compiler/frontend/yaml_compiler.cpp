#include "compiler/frontend/yaml_compiler.hpp"

#include "compiler/layout/layout.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/symbols/symbols.hpp"
#include "compiler/yaml/schema_decoder.hpp"
#include "compiler/yaml/yaml_parser.hpp"

#include <optional>
#include <utility>

namespace breadcrumbs::compiler::frontend {
namespace {

[[nodiscard]] bool has_fatal_diagnostics(const diagnostics::DiagnosticCollection& diagnostics) {
    return diagnostics.has_errors();
}

} // namespace

YamlCompilationResult YamlCompiler::compile(support::SourceFileId source_file_id,
                                            context::CompilerContext& context,
                                            diagnostics::DiagnosticCollection& diagnostics) const {
    YamlCompilationResult result;

    const yaml::YamlParseResult parse_result =
        yaml::YamlParser::parse(context.source_manager(), source_file_id, diagnostics);
    if (has_fatal_diagnostics(diagnostics) || !parse_result.document.has_value()) {
        return result;
    }

    const source_schema::SourceSchemaDecodeResult decode_result =
        yaml::decode_schema(*parse_result.document, diagnostics);
    if (has_fatal_diagnostics(diagnostics) || !decode_result.schema.has_value()) {
        return result;
    }

    const source_schema::SourceSchemaNormalizationResult normalization_result =
        source_schema::normalize_source_schema(*decode_result.schema, diagnostics);
    if (has_fatal_diagnostics(diagnostics) || !normalization_result.document.has_value()) {
        return result;
    }

    symbols::NamespaceBuilder namespace_builder;
    const symbols::SymbolTable symbol_table =
        namespace_builder.build(*normalization_result.document, diagnostics);
    if (has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    semantic::SemanticValidator semantic_validator;
    const semantic::SemanticModel semantic_model =
        semantic_validator.validate(*normalization_result.document, symbol_table, diagnostics);
    if (has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    layout::LayoutComputer layout_computer;
    const layout::LayoutModel layout_model =
        layout_computer.compute(semantic_model, context, diagnostics);
    if (has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    schema_ir::SchemaIrBuilder schema_ir_builder;
    const schema_ir::SchemaIrModel schema_ir = schema_ir_builder.build(
        *normalization_result.document, semantic_model, layout_model, context, diagnostics);
    if (has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    schema_ir::SchemaIrValidator schema_ir_validator;
    schema_ir_validator.validate(schema_ir, context, diagnostics);
    if (has_fatal_diagnostics(diagnostics)) {
        return result;
    }

    result.schema_ir = std::move(schema_ir);
    return result;
}

} // namespace breadcrumbs::compiler::frontend
