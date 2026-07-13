#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/symbols/symbols.hpp"
#include "compiler/yaml/schema_decoder.hpp"
#include "compiler/yaml/source_schema.hpp"
#include "compiler/yaml/yaml_parser.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::context::CompilerContext;
using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::layout::LayoutComputer;
using breadcrumbs::compiler::layout::LayoutModel;
using breadcrumbs::compiler::schema_ir::SchemaIrBuilder;
using breadcrumbs::compiler::schema_ir::SchemaIrModel;
using breadcrumbs::compiler::schema_ir::SchemaIrValidator;
using breadcrumbs::compiler::semantic::SemanticField;
using breadcrumbs::compiler::semantic::SemanticModel;
using breadcrumbs::compiler::semantic::SemanticRecord;
using breadcrumbs::compiler::semantic::SemanticValidator;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceLocation;
using breadcrumbs::compiler::support::SourceManager;
using breadcrumbs::compiler::support::SourceRange;
using breadcrumbs::compiler::symbols::NamespaceBuilder;
using breadcrumbs::compiler::symbols::SymbolTable;
using breadcrumbs::compiler::yaml::YamlDecodeResult;
using breadcrumbs::compiler::yaml::YamlParser;
using breadcrumbs::compiler::yaml::YamlParseResult;

[[nodiscard]] std::string diagnostics_summary(const DiagnosticEngine& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.id().str() << ": " << diagnostic.message() << '\n';
    }
    return stream.str();
}

struct DirectPipelineOutput {
    CompilerContext context;
    SourceFileId source_file_id;
    YamlParseResult parse_result;
    YamlDecodeResult decode_result;
    breadcrumbs::compiler::source_schema::SourceSchemaNormalizationResult normalization_result;
    DiagnosticEngine parser_diagnostics;
    DiagnosticEngine decoder_diagnostics;
    DiagnosticEngine normalization_diagnostics;
    DiagnosticEngine symbol_diagnostics;
    DiagnosticEngine semantic_diagnostics;
    DiagnosticEngine layout_diagnostics;
    DiagnosticEngine schema_ir_diagnostics;
    DiagnosticEngine validation_diagnostics;
    std::unique_ptr<SymbolTable> symbol_table;
    SemanticModel semantic_model;
    LayoutModel layout_model;
    SchemaIrModel schema_ir;
};

[[nodiscard]] DirectPipelineOutput run_direct_pipeline(std::string text) {
    DirectPipelineOutput output;
    output.source_file_id = output.context.source_manager().add_source("/test/schema.yaml",
                                                                       std::move(text));

    output.parse_result =
        YamlParser::parse(output.context.source_manager(), output.source_file_id,
                          output.parser_diagnostics);
    if (output.parser_diagnostics.has_errors() || !output.parse_result.document.has_value()) {
        return output;
    }

    output.decode_result =
        breadcrumbs::compiler::yaml::decode_schema(*output.parse_result.document,
                                                   output.decoder_diagnostics);
    if (output.decoder_diagnostics.has_errors() || !output.decode_result.schema.has_value()) {
        return output;
    }

    output.normalization_result = breadcrumbs::compiler::source_schema::normalize_source_schema(
        *output.decode_result.schema, output.normalization_diagnostics);
    if (output.normalization_diagnostics.has_errors() ||
        !output.normalization_result.document.has_value()) {
        return output;
    }

    NamespaceBuilder namespace_builder;
    output.symbol_table = std::make_unique<SymbolTable>(
        namespace_builder.build(*output.normalization_result.document, output.symbol_diagnostics));
    if (output.symbol_diagnostics.has_errors()) {
        return output;
    }

    SemanticValidator semantic_validator;
    output.semantic_model = semantic_validator.validate(*output.normalization_result.document,
                                                        *output.symbol_table,
                                                        output.semantic_diagnostics);
    if (output.semantic_diagnostics.has_errors()) {
        return output;
    }

    LayoutComputer layout_computer;
    output.layout_model = layout_computer.compute(output.semantic_model, output.context,
                                                  output.layout_diagnostics);
    if (output.layout_diagnostics.has_errors()) {
        return output;
    }

    SchemaIrBuilder schema_ir_builder;
    output.schema_ir = schema_ir_builder.build(*output.normalization_result.document,
                                               output.semantic_model, output.layout_model,
                                               output.context, output.schema_ir_diagnostics);
    if (output.schema_ir_diagnostics.has_errors()) {
        return output;
    }

    SchemaIrValidator schema_ir_validator;
    schema_ir_validator.validate(output.schema_ir, output.context, output.validation_diagnostics);
    return output;
}

[[nodiscard]] DirectPipelineOutput make_direct_builder_fixture() {
    return run_direct_pipeline(R"(namespace: breadcrumbs.telemetry.deep
record: Sample
version: 7
type: data
fields:
  mode:
    type: Mode
  count:
    type: u32
  label:
    type: string
    max_bytes: 16
  payload:
    type: bytes
    max_bytes: 8
  samples:
    type: uint32[]
    max_elements: 64
enums:
  Mode:
    values:
      negative: -1
      zero: 0
      positive: 1
)");
}

[[nodiscard]] SchemaIrModel build_direct_schema_ir(DirectPipelineOutput& input,
                                                   DiagnosticEngine& diagnostics) {
    SchemaIrBuilder schema_ir_builder;
    return schema_ir_builder.build(*input.normalization_result.document, input.semantic_model,
                                   input.layout_model, input.context, diagnostics);
}

void expect_direct_builder_failure(const SchemaIrModel& schema_ir,
                                   const DiagnosticEngine& diagnostics) {
    ASSERT_FALSE(diagnostics.empty());
    ASSERT_FALSE(schema_ir.has_root_namespace());
    ASSERT_FALSE(diagnostics.diagnostics().empty());
    EXPECT_EQ(diagnostics.diagnostics().front().id().str(), "BC1004");
    EXPECT_EQ(diagnostics.diagnostics().front().severity(),
              breadcrumbs::compiler::diagnostics::Severity::InternalCompilerError);
}

[[nodiscard]] const breadcrumbs::schema_ir::NamespaceIR*
find_namespace(const breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.namespaces_size(); ++index) {
        const auto& child = parent.namespaces(index);
        if (child.name() == name) {
            return &child;
        }
    }
    return nullptr;
}

TEST(YamlCompilerPipelineTest, DirectNormalizedSourceReachesValidatedSchemaIr) {
    const DirectPipelineOutput output = run_direct_pipeline(R"(namespace: breadcrumbs.telemetry.deep
record: Sample
version: 7
type: data
fields:
  mode:
    type: Mode
  count:
    type: u32
  label:
    type: string
    max_bytes: 16
  payload:
    type: bytes
    max_bytes: 8
  samples:
    type: uint32[]
    max_elements: 64
enums:
  Mode:
    values:
      negative: -1
      zero: 0
      positive: 1
)");

    ASSERT_TRUE(output.parse_result.document.has_value())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.decode_result.schema.has_value())
        << diagnostics_summary(output.decoder_diagnostics);
    ASSERT_TRUE(output.normalization_result.document.has_value())
        << diagnostics_summary(output.normalization_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty()) << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty()) << diagnostics_summary(output.layout_diagnostics);
    ASSERT_TRUE(output.schema_ir_diagnostics.empty())
        << diagnostics_summary(output.schema_ir_diagnostics);
    ASSERT_TRUE(output.validation_diagnostics.empty())
        << diagnostics_summary(output.validation_diagnostics);

    const SemanticRecord* semantic_record =
        output.semantic_model.find_record("breadcrumbs.telemetry.deep.Sample");
    ASSERT_NE(semantic_record, nullptr);
    ASSERT_TRUE(semantic_record->version.has_value());
    EXPECT_EQ(*semantic_record->version, 7U);
    ASSERT_TRUE(semantic_record->record_type.has_value());
    EXPECT_EQ(*semantic_record->record_type,
              breadcrumbs::compiler::semantic::SemanticRecordType::Data);
    ASSERT_EQ(semantic_record->fields.size(), 5U);
    EXPECT_TRUE(semantic_record->fields[0].type.is_enum_reference());
    EXPECT_TRUE(semantic_record->fields[2].type.is_string());
    EXPECT_TRUE(semantic_record->fields[3].type.is_bytes());
    EXPECT_TRUE(semantic_record->fields[4].type.is_array());
    EXPECT_EQ(
        std::get<breadcrumbs::compiler::semantic::SemanticStringType>(
            semantic_record->fields[2].type.value)
            .max_bytes,
        16U);
    EXPECT_EQ(std::get<breadcrumbs::compiler::semantic::SemanticBytesType>(
                  semantic_record->fields[3].type.value)
                  .max_bytes,
              8U);
    EXPECT_EQ(semantic_record->fields[4].type.array().max_elements, 64U);

    const auto& root = output.schema_ir.root_namespace();
    const auto* breadcrumbs_ns = find_namespace(root, "breadcrumbs");
    ASSERT_NE(breadcrumbs_ns, nullptr);
    ASSERT_EQ(breadcrumbs_ns->namespaces_size(), 1);
    const auto& telemetry_ns = breadcrumbs_ns->namespaces(0);
    ASSERT_EQ(telemetry_ns.namespaces_size(), 1);
    const auto& deep_ns = telemetry_ns.namespaces(0);
    ASSERT_EQ(deep_ns.enums_size(), 1);
    ASSERT_EQ(deep_ns.records_size(), 1);

    const auto& lowered_enum = deep_ns.enums(0);
    ASSERT_EQ(lowered_enum.values_size(), 3);
    EXPECT_EQ(lowered_enum.values(0).value(), -1);
    EXPECT_EQ(lowered_enum.values(1).value(), 0);
    EXPECT_EQ(lowered_enum.values(2).value(), 1);
    ASSERT_EQ(lowered_enum.source_origin().file(), "/test/schema.yaml");

    const auto& lowered_record = deep_ns.records(0);
    EXPECT_TRUE(lowered_record.has_schema_version());
    EXPECT_EQ(lowered_record.schema_version(), 7U);
    EXPECT_TRUE(lowered_record.has_record_type());
    EXPECT_EQ(lowered_record.record_type(), breadcrumbs::schema_ir::RECORD_TYPE_DATA);
    ASSERT_EQ(lowered_record.fields_size(), 5);
    EXPECT_TRUE(lowered_record.fields(0).type().has_enum_type());
    EXPECT_EQ(lowered_record.fields(0).type().enum_type().target_enum_ir_id(),
              lowered_enum.ir_id());
    EXPECT_TRUE(lowered_record.fields(2).type().has_string());
    EXPECT_EQ(lowered_record.fields(2).type().string().max_bytes(), 16U);
    EXPECT_TRUE(lowered_record.fields(3).type().has_bytes());
    EXPECT_EQ(lowered_record.fields(3).type().bytes().max_bytes(), 8U);
    EXPECT_TRUE(lowered_record.fields(4).type().has_array());
    EXPECT_EQ(lowered_record.fields(4).type().array().max_elements(), 64U);
}

TEST(SchemaIrBuilderDirectNormalizedSourceTest,
     BuildsSyntheticRootHierarchyMetadataAndBoundsWithoutAST) {
    DirectPipelineOutput output = make_direct_builder_fixture();

    ASSERT_TRUE(output.parse_result.document.has_value())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.decode_result.schema.has_value())
        << diagnostics_summary(output.decoder_diagnostics);
    ASSERT_TRUE(output.normalization_result.document.has_value())
        << diagnostics_summary(output.normalization_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty()) << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty()) << diagnostics_summary(output.layout_diagnostics);

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = build_direct_schema_ir(output, lowering_diagnostics);

    ASSERT_TRUE(lowering_diagnostics.empty()) << diagnostics_summary(lowering_diagnostics);
    ASSERT_TRUE(schema_ir.has_root_namespace());

    const auto& root = schema_ir.root_namespace();
    EXPECT_EQ(root.name(), "");
    EXPECT_EQ(root.fqn(), "");
    EXPECT_EQ(root.source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(root.source_origin().span().start_line(), 1U);
    ASSERT_EQ(root.namespaces_size(), 1);

    const auto& breadcrumbs_ns = root.namespaces(0);
    EXPECT_EQ(breadcrumbs_ns.name(), "breadcrumbs");
    EXPECT_EQ(breadcrumbs_ns.source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(breadcrumbs_ns.source_origin().span().start_line(), 1U);
    ASSERT_EQ(breadcrumbs_ns.namespaces_size(), 1);

    const auto& telemetry_ns = breadcrumbs_ns.namespaces(0);
    EXPECT_EQ(telemetry_ns.name(), "telemetry");
    EXPECT_EQ(telemetry_ns.source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(telemetry_ns.source_origin().span().start_line(), 1U);
    ASSERT_EQ(telemetry_ns.namespaces_size(), 1);

    const auto& deep_ns = telemetry_ns.namespaces(0);
    EXPECT_EQ(deep_ns.name(), "deep");
    EXPECT_EQ(deep_ns.source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(deep_ns.source_origin().span().start_line(), 1U);
    ASSERT_EQ(deep_ns.enums_size(), 1);
    ASSERT_EQ(deep_ns.records_size(), 1);

    const auto& lowered_enum = deep_ns.enums(0);
    EXPECT_EQ(lowered_enum.name(), "Mode");
    EXPECT_EQ(lowered_enum.source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(lowered_enum.source_origin().span().start_line(), 20U);
    ASSERT_EQ(lowered_enum.values_size(), 3);
    EXPECT_EQ(lowered_enum.values(0).name(), "negative");
    EXPECT_EQ(lowered_enum.values(0).value(), -1);
    EXPECT_EQ(lowered_enum.values(0).source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(lowered_enum.values(0).source_origin().span().start_line(), 22U);
    EXPECT_EQ(lowered_enum.values(1).name(), "zero");
    EXPECT_EQ(lowered_enum.values(1).value(), 0);
    EXPECT_EQ(lowered_enum.values(1).source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(lowered_enum.values(1).source_origin().span().start_line(), 23U);
    EXPECT_EQ(lowered_enum.values(2).name(), "positive");
    EXPECT_EQ(lowered_enum.values(2).value(), 1);
    EXPECT_EQ(lowered_enum.values(2).source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(lowered_enum.values(2).source_origin().span().start_line(), 24U);

    const auto& lowered_record = deep_ns.records(0);
    EXPECT_EQ(lowered_record.name(), "Sample");
    EXPECT_EQ(lowered_record.source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(lowered_record.source_origin().span().start_line(), 2U);
    ASSERT_TRUE(lowered_record.has_schema_version());
    EXPECT_EQ(lowered_record.schema_version(), 7U);
    ASSERT_TRUE(lowered_record.has_record_type());
    EXPECT_EQ(lowered_record.record_type(), breadcrumbs::schema_ir::RECORD_TYPE_DATA);
    EXPECT_EQ(lowered_record.record_id(), 1U);
    ASSERT_EQ(lowered_record.fields_size(), 5);

    EXPECT_EQ(lowered_record.fields(0).name(), "mode");
    EXPECT_EQ(lowered_record.fields(0).source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(lowered_record.fields(0).source_origin().span().start_line(), 6U);
    EXPECT_EQ(lowered_record.fields(0).field_index(), 0U);
    ASSERT_TRUE(lowered_record.fields(0).type().has_enum_type());
    EXPECT_EQ(lowered_record.fields(0).type().enum_type().target_enum_ir_id(),
              lowered_enum.ir_id());

    EXPECT_EQ(lowered_record.fields(1).name(), "count");
    EXPECT_EQ(lowered_record.fields(1).field_index(), 1U);
    ASSERT_TRUE(lowered_record.fields(1).type().has_primitive());
    EXPECT_EQ(lowered_record.fields(1).type().primitive(),
              ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    EXPECT_EQ(lowered_record.fields(2).name(), "label");
    EXPECT_EQ(lowered_record.fields(2).field_index(), 2U);
    EXPECT_TRUE(lowered_record.fields(2).type().has_string());
    EXPECT_EQ(lowered_record.fields(2).type().string().max_bytes(), 16U);

    EXPECT_EQ(lowered_record.fields(3).name(), "payload");
    EXPECT_EQ(lowered_record.fields(3).field_index(), 3U);
    EXPECT_TRUE(lowered_record.fields(3).type().has_bytes());
    EXPECT_EQ(lowered_record.fields(3).type().bytes().max_bytes(), 8U);

    EXPECT_EQ(lowered_record.fields(4).name(), "samples");
    EXPECT_EQ(lowered_record.fields(4).field_index(), 4U);
    ASSERT_TRUE(lowered_record.fields(4).type().has_array());
    EXPECT_EQ(lowered_record.fields(4).type().array().max_elements(), 64U);
}

TEST(SchemaIrBuilderDirectNormalizedSourceTest, FailsWhenSemanticRecordIsMissing) {
    DirectPipelineOutput output = make_direct_builder_fixture();
    auto* record = const_cast<SemanticRecord*>(
        output.semantic_model.find_record("breadcrumbs.telemetry.deep.Sample"));
    ASSERT_NE(record, nullptr);
    output.semantic_model.records.clear();

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = build_direct_schema_ir(output, lowering_diagnostics);

    expect_direct_builder_failure(schema_ir, lowering_diagnostics);
}

TEST(SchemaIrBuilderDirectNormalizedSourceTest, FailsWhenLayoutRecordIsMissing) {
    DirectPipelineOutput output = make_direct_builder_fixture();
    auto* layout_record = const_cast<breadcrumbs::compiler::layout::RecordLayout*>(
        output.layout_model.find_record("breadcrumbs.telemetry.deep.Sample"));
    ASSERT_NE(layout_record, nullptr);
    output.layout_model.records.clear();

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = build_direct_schema_ir(output, lowering_diagnostics);

    expect_direct_builder_failure(schema_ir, lowering_diagnostics);
}

TEST(SchemaIrBuilderDirectNormalizedSourceTest, FailsWhenSemanticFieldCountDiffers) {
    DirectPipelineOutput output = make_direct_builder_fixture();
    auto* record = const_cast<SemanticRecord*>(
        output.semantic_model.find_record("breadcrumbs.telemetry.deep.Sample"));
    ASSERT_NE(record, nullptr);
    ASSERT_GE(record->fields.size(), 2U);
    record->fields.pop_back();

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = build_direct_schema_ir(output, lowering_diagnostics);

    expect_direct_builder_failure(schema_ir, lowering_diagnostics);
}

TEST(SchemaIrBuilderDirectNormalizedSourceTest, FailsWhenSemanticFieldOrderDiffers) {
    DirectPipelineOutput output = make_direct_builder_fixture();
    auto* record = const_cast<SemanticRecord*>(
        output.semantic_model.find_record("breadcrumbs.telemetry.deep.Sample"));
    ASSERT_NE(record, nullptr);
    ASSERT_GE(record->fields.size(), 2U);
    std::swap(record->fields[0], record->fields[1]);

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = build_direct_schema_ir(output, lowering_diagnostics);

    expect_direct_builder_failure(schema_ir, lowering_diagnostics);
}

TEST(SchemaIrBuilderDirectNormalizedSourceTest, FailsWhenLayoutFieldCountDiffers) {
    DirectPipelineOutput output = make_direct_builder_fixture();
    auto* layout_record = const_cast<breadcrumbs::compiler::layout::RecordLayout*>(
        output.layout_model.find_record("breadcrumbs.telemetry.deep.Sample"));
    ASSERT_NE(layout_record, nullptr);
    ASSERT_GE(layout_record->fields.size(), 2U);
    layout_record->fields.pop_back();

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = build_direct_schema_ir(output, lowering_diagnostics);

    expect_direct_builder_failure(schema_ir, lowering_diagnostics);
}

TEST(SchemaIrBuilderDirectNormalizedSourceTest, FailsWhenLayoutFieldIndexDiffers) {
    DirectPipelineOutput output = make_direct_builder_fixture();
    auto* layout_record = const_cast<breadcrumbs::compiler::layout::RecordLayout*>(
        output.layout_model.find_record("breadcrumbs.telemetry.deep.Sample"));
    ASSERT_NE(layout_record, nullptr);
    ASSERT_GE(layout_record->fields.size(), 2U);
    layout_record->fields[1].field_index = 7U;

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = build_direct_schema_ir(output, lowering_diagnostics);

    expect_direct_builder_failure(schema_ir, lowering_diagnostics);
}

TEST(SchemaIrBuilderDirectNormalizedSourceTest, FailsWhenLayoutRecordIdIsZero) {
    DirectPipelineOutput output = make_direct_builder_fixture();
    auto* layout_record = const_cast<breadcrumbs::compiler::layout::RecordLayout*>(
        output.layout_model.find_record("breadcrumbs.telemetry.deep.Sample"));
    ASSERT_NE(layout_record, nullptr);
    layout_record->record_id = 0U;

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = build_direct_schema_ir(output, lowering_diagnostics);

    expect_direct_builder_failure(schema_ir, lowering_diagnostics);
}

} // namespace
