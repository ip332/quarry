#include "compiler/ast/ast.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/symbols/symbols.hpp"
#include "compiler/yaml/schema_decoder.hpp"
#include "compiler/yaml/source_schema.hpp"
#include "compiler/yaml/source_schema_lowering.hpp"
#include "compiler/yaml/yaml_document.hpp"
#include "compiler/yaml/yaml_parser.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::ast::ArrayTypeSyntax;
using breadcrumbs::compiler::ast::ArrayTypeSyntaxKind;
using breadcrumbs::compiler::ast::DeclarationPtr;
using breadcrumbs::compiler::ast::EnumDeclarationSyntax;
using breadcrumbs::compiler::ast::EnumValueDeclarationSyntax;
using breadcrumbs::compiler::ast::FieldDeclarationSyntax;
using breadcrumbs::compiler::ast::IdentifierSyntax;
using breadcrumbs::compiler::ast::NamespaceDeclarationSyntax;
using breadcrumbs::compiler::ast::QualifiedNameSyntax;
using breadcrumbs::compiler::ast::RecordDeclarationSyntax;
using breadcrumbs::compiler::ast::SchemaFileSyntax;
using breadcrumbs::compiler::ast::TypeReferenceSyntax;
using breadcrumbs::compiler::ast::TypeSyntax;
using breadcrumbs::compiler::context::CompilerContext;
using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::diagnostics::DiagnosticFormatter;
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
using breadcrumbs::compiler::symbols::Scope;
using breadcrumbs::compiler::symbols::Symbol;
using breadcrumbs::compiler::symbols::SymbolKind;
using breadcrumbs::compiler::symbols::SymbolTable;
using breadcrumbs::compiler::yaml::decode_schema;
using breadcrumbs::compiler::yaml::lower_source_schema;
using breadcrumbs::compiler::yaml::SourceSchemaAnnotation;
using breadcrumbs::compiler::yaml::SourceSchemaDocument;
using breadcrumbs::compiler::yaml::SourceSchemaEnum;
using breadcrumbs::compiler::yaml::SourceSchemaEnumValue;
using breadcrumbs::compiler::yaml::SourceSchemaField;
using breadcrumbs::compiler::yaml::SourceSchemaLoweringResult;
using breadcrumbs::compiler::yaml::YamlDecodeResult;
using breadcrumbs::compiler::yaml::YamlDocument;
using breadcrumbs::compiler::yaml::YamlMappingEntry;
using breadcrumbs::compiler::yaml::YamlMappingNode;
using breadcrumbs::compiler::yaml::YamlNode;
using breadcrumbs::compiler::yaml::YamlNodePtr;
using breadcrumbs::compiler::yaml::YamlParser;
using breadcrumbs::compiler::yaml::YamlParseResult;
using breadcrumbs::compiler::yaml::YamlScalarKind;

[[nodiscard]] SourceRange range(std::size_t begin, std::size_t end) {
    const SourceFileId file_id(0);
    return SourceRange(SourceLocation(file_id, begin), SourceLocation(file_id, end));
}

[[nodiscard]] SourceSchemaAnnotation annotation(std::string name, std::string value,
                                                std::size_t begin, std::size_t end) {
    return SourceSchemaAnnotation{
        .name = std::move(name),
        .value = std::move(value),
        .source_range = range(begin, end),
        .name_range = range(begin, begin + 1),
        .value_range = range(end - 1, end),
    };
}

[[nodiscard]] SourceSchemaField field(std::string name, std::string type_spelling,
                                      std::size_t begin, std::size_t end) {
    return SourceSchemaField{
        .name = std::move(name),
        .source_range = range(begin, end),
        .name_range = range(begin, begin + 1),
        .type_spelling = std::move(type_spelling),
        .type_range = range(begin + 2, begin + 2 + type_spelling.size()),
        .max_bytes = std::nullopt,
        .max_bytes_range = range(0, 0),
        .max_elements = std::nullopt,
        .max_elements_range = range(0, 0),
        .annotations = {},
    };
}

[[nodiscard]] SourceSchemaEnumValue enum_value(std::string name, std::int64_t value,
                                               std::size_t begin, std::size_t end) {
    return SourceSchemaEnumValue{
        .name = std::move(name),
        .source_range = range(begin, end),
        .name_range = range(begin, begin + 1),
        .value = value,
        .value_range = range(end - 1, end),
    };
}

[[nodiscard]] std::unique_ptr<YamlNode> scalar_node(std::string value, YamlScalarKind kind,
                                                    std::size_t begin, std::size_t end) {
    auto node = std::make_unique<YamlNode>();
    node->source_range = range(begin, end);
    node->value = breadcrumbs::compiler::yaml::YamlScalarNode{
        .value = std::move(value),
        .kind = kind,
    };
    return node;
}

[[nodiscard]] std::string diagnostics_summary(const DiagnosticEngine& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.id().str() << ": " << diagnostic.message() << '\n';
    }
    return stream.str();
}

struct PipelineOutput {
    CompilerContext context;
    SourceFileId source_file_id;
    YamlParseResult parse_result;
    YamlDecodeResult decode_result;
    SourceSchemaLoweringResult lowering_result;
    DiagnosticEngine parser_diagnostics;
    DiagnosticEngine decoder_diagnostics;
    DiagnosticEngine lowering_diagnostics;
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

[[nodiscard]] const NamespaceDeclarationSyntax&
namespace_declaration(const SchemaFileSyntax& schema_file) {
    return std::get<NamespaceDeclarationSyntax>(schema_file.declarations[0]->value);
}

[[nodiscard]] const EnumDeclarationSyntax&
enum_declaration(const NamespaceDeclarationSyntax& namespace_declaration, std::size_t index) {
    return std::get<EnumDeclarationSyntax>(namespace_declaration.declarations[index]->value);
}

[[nodiscard]] const RecordDeclarationSyntax&
record_declaration(const NamespaceDeclarationSyntax& namespace_declaration) {
    return std::get<RecordDeclarationSyntax>(namespace_declaration.declarations.back()->value);
}

TEST(SourceSchemaLoweringTest, PreservesOrderMetadataAndBounds) {
    SourceSchemaDocument schema;
    schema.source_range = range(0, 300);
    schema.namespace_spelling = "breadcrumbs.telemetry";
    schema.namespace_range = range(0, 24);
    schema.record_name = "Sample";
    schema.record_range = range(25, 31);
    schema.version = 7;
    schema.version_range = range(32, 33);
    schema.record_type_spelling = "data";
    schema.record_type_range = range(34, 38);
    schema.annotations.push_back(annotation("owner", "telemetry", 39, 56));
    schema.fields.push_back(field("samples", "uint32[]", 57, 90));
    schema.fields.back().max_elements = 64;
    schema.fields.back().max_elements_range = range(79, 81);
    schema.fields.back().annotations.push_back(annotation("storage", "hot", 82, 95));
    schema.fields.push_back(field("label", "string", 96, 120));
    schema.fields.back().max_bytes = 32;
    schema.fields.back().max_bytes_range = range(113, 115);
    schema.enums.push_back(SourceSchemaEnum{
        .name = "Mode",
        .source_range = range(121, 160),
        .name_range = range(121, 125),
        .values =
            {
                enum_value("zero", 0, 126, 134),
                enum_value("negative", -1, 135, 148),
                enum_value("positive", 1, 149, 162),
            },
        .annotations = {annotation("state", "operational", 163, 185)},
    });
    schema.enums.push_back(SourceSchemaEnum{
        .name = "Status",
        .source_range = range(186, 220),
        .name_range = range(186, 192),
        .values = {enum_value("ready", 1, 193, 201)},
        .annotations = {},
    });

    DiagnosticEngine diagnostics;
    const SourceSchemaLoweringResult result = lower_source_schema(schema, diagnostics);

    ASSERT_TRUE(diagnostics.empty()) << diagnostics_summary(diagnostics);
    ASSERT_TRUE(result.ast.has_value());

    const auto& file = *result.ast;
    ASSERT_EQ(file.declarations.size(), 1U);
    const auto& lowered_namespace = namespace_declaration(file);
    EXPECT_EQ(lowered_namespace.name.text(), "breadcrumbs.telemetry");
    ASSERT_EQ(lowered_namespace.annotations.size(), 1U);
    EXPECT_EQ(lowered_namespace.annotations[0].name.text(), "owner");
    EXPECT_EQ(lowered_namespace.annotations[0].value, std::optional<std::string>("telemetry"));
    ASSERT_EQ(lowered_namespace.declarations.size(), 3U);

    const auto& first_enum = enum_declaration(lowered_namespace, 0);
    const auto& second_enum = enum_declaration(lowered_namespace, 1);
    const auto& record = record_declaration(lowered_namespace);

    EXPECT_EQ(first_enum.name.text, "Mode");
    ASSERT_EQ(first_enum.values.size(), 3U);
    EXPECT_EQ(first_enum.values[0].name.text, "zero");
    EXPECT_EQ(first_enum.values[0].value, std::optional<std::string>("0"));
    EXPECT_EQ(first_enum.values[1].name.text, "negative");
    EXPECT_EQ(first_enum.values[1].value, std::optional<std::string>("-1"));
    EXPECT_EQ(first_enum.values[2].name.text, "positive");
    EXPECT_EQ(first_enum.values[2].value, std::optional<std::string>("1"));
    ASSERT_EQ(first_enum.annotations.size(), 1U);
    EXPECT_EQ(first_enum.annotations[0].name.text(), "state");
    EXPECT_EQ(first_enum.annotations[0].value, std::optional<std::string>("operational"));

    EXPECT_EQ(second_enum.name.text, "Status");
    ASSERT_EQ(second_enum.values.size(), 1U);
    EXPECT_EQ(second_enum.values[0].name.text, "ready");

    EXPECT_EQ(record.name.text, "Sample");
    EXPECT_EQ(record.version, 7);
    EXPECT_EQ(record.record_type_spelling, "data");
    ASSERT_EQ(record.fields.size(), 2U);
    EXPECT_EQ(record.fields[0].name.text, "samples");
    EXPECT_EQ(record.fields[0].max_elements, std::optional<std::int64_t>(64));
    EXPECT_EQ(record.fields[0].max_elements_source_range, range(79, 81));
    EXPECT_EQ(record.fields[0].annotations[0].name.text(), "storage");
    EXPECT_EQ(record.fields[0].annotations[0].value, std::optional<std::string>("hot"));
    const auto& samples_type = std::get<ArrayTypeSyntax>(record.fields[0].type);
    EXPECT_EQ(samples_type.kind, ArrayTypeSyntaxKind::BoundedVariableLength);
    EXPECT_FALSE(samples_type.fixed_size.has_value());
    EXPECT_EQ(samples_type.element_type.name.text(), "uint32");

    EXPECT_EQ(record.fields[1].name.text, "label");
    EXPECT_EQ(record.fields[1].max_bytes, std::optional<std::int64_t>(32));
    EXPECT_EQ(record.fields[1].max_bytes_source_range, range(113, 115));
    EXPECT_EQ(std::get<TypeReferenceSyntax>(record.fields[1].type).name.text(), "string");
}

TEST(SourceSchemaLoweringTest, RejectsMalformedNamespaceAndIdentifiersAtomically) {
    SourceSchemaDocument schema;
    schema.source_range = range(0, 80);
    schema.namespace_spelling = "breadcrumbs..telemetry";
    schema.namespace_range = range(0, 24);
    schema.record_name = "Sample";
    schema.record_range = range(25, 31);
    schema.version = 1;
    schema.version_range = range(32, 33);
    schema.record_type_spelling = "data";
    schema.record_type_range = range(34, 38);
    schema.fields.push_back(field("label", "string", 39, 60));
    schema.fields.back().max_bytes = 16;
    schema.fields.back().max_bytes_range = range(52, 54);

    DiagnosticEngine diagnostics;
    const SourceSchemaLoweringResult result = lower_source_schema(schema, diagnostics);

    ASSERT_FALSE(result.ast.has_value());
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.diagnostics().front().id().str(), "BC2401");
}

TEST(SourceSchemaLoweringTest, RejectsInvalidRecordIdentifierAtomically) {
    SourceSchemaDocument schema;
    schema.source_range = range(0, 80);
    schema.namespace_spelling = "breadcrumbs.telemetry";
    schema.namespace_range = range(0, 24);
    schema.record_name = "Bad.Name";
    schema.record_range = range(25, 33);
    schema.version = 1;
    schema.version_range = range(34, 35);
    schema.record_type_spelling = "data";
    schema.record_type_range = range(36, 40);
    schema.fields.push_back(field("label", "string", 41, 62));
    schema.fields.back().max_bytes = 8;
    schema.fields.back().max_bytes_range = range(55, 56);

    DiagnosticEngine diagnostics;
    const SourceSchemaLoweringResult result = lower_source_schema(schema, diagnostics);

    ASSERT_FALSE(result.ast.has_value());
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.diagnostics().front().id().str(), "BC2401");
}

TEST(SourceSchemaLoweringTest, RejectsInvalidFieldIdentifierAtomically) {
    SourceSchemaDocument schema;
    schema.source_range = range(0, 80);
    schema.namespace_spelling = "breadcrumbs.telemetry";
    schema.namespace_range = range(0, 24);
    schema.record_name = "Sample";
    schema.record_range = range(25, 31);
    schema.version = 1;
    schema.version_range = range(32, 33);
    schema.record_type_spelling = "data";
    schema.record_type_range = range(34, 38);
    schema.fields.push_back(field("bad.name", "string", 41, 62));
    schema.fields.back().max_bytes = 8;
    schema.fields.back().max_bytes_range = range(55, 56);

    DiagnosticEngine diagnostics;
    const SourceSchemaLoweringResult result = lower_source_schema(schema, diagnostics);

    ASSERT_FALSE(result.ast.has_value());
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.diagnostics().front().id().str(), "BC2401");
}

TEST(SourceSchemaLoweringTest, RejectsInvalidEnumIdentifierAtomically) {
    SourceSchemaDocument schema;
    schema.source_range = range(0, 80);
    schema.namespace_spelling = "breadcrumbs.telemetry";
    schema.namespace_range = range(0, 24);
    schema.record_name = "Sample";
    schema.record_range = range(25, 31);
    schema.version = 1;
    schema.version_range = range(32, 33);
    schema.record_type_spelling = "data";
    schema.record_type_range = range(34, 38);
    schema.fields.push_back(field("label", "string", 39, 60));
    schema.fields.back().max_bytes = 16;
    schema.fields.back().max_bytes_range = range(52, 54);
    schema.enums.push_back(SourceSchemaEnum{
        .name = "Bad.Name",
        .source_range = range(61, 80),
        .name_range = range(61, 69),
        .values = {enum_value("ready", 1, 70, 78)},
        .annotations = {},
    });

    DiagnosticEngine diagnostics;
    const SourceSchemaLoweringResult result = lower_source_schema(schema, diagnostics);

    ASSERT_FALSE(result.ast.has_value());
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.diagnostics().front().id().str(), "BC2401");
}

TEST(SourceSchemaLoweringTest, RejectsInvalidEnumValueIdentifierAtomically) {
    SourceSchemaDocument schema;
    schema.source_range = range(0, 80);
    schema.namespace_spelling = "breadcrumbs.telemetry";
    schema.namespace_range = range(0, 24);
    schema.record_name = "Sample";
    schema.record_range = range(25, 31);
    schema.version = 1;
    schema.version_range = range(32, 33);
    schema.record_type_spelling = "data";
    schema.record_type_range = range(34, 38);
    schema.fields.push_back(field("label", "string", 39, 60));
    schema.fields.back().max_bytes = 16;
    schema.fields.back().max_bytes_range = range(52, 54);
    schema.enums.push_back(SourceSchemaEnum{
        .name = "Mode",
        .source_range = range(61, 80),
        .name_range = range(61, 65),
        .values = {enum_value("bad.name", 1, 66, 78)},
        .annotations = {},
    });

    DiagnosticEngine diagnostics;
    const SourceSchemaLoweringResult result = lower_source_schema(schema, diagnostics);

    ASSERT_FALSE(result.ast.has_value());
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.diagnostics().front().id().str(), "BC2401");
}

TEST(SourceSchemaLoweringTest, RejectsInvalidAnnotationIdentifierAtomically) {
    SourceSchemaDocument schema;
    schema.source_range = range(0, 80);
    schema.namespace_spelling = "breadcrumbs.telemetry";
    schema.namespace_range = range(0, 24);
    schema.record_name = "Sample";
    schema.record_range = range(25, 31);
    schema.version = 1;
    schema.version_range = range(32, 33);
    schema.record_type_spelling = "data";
    schema.record_type_range = range(34, 38);
    schema.fields.push_back(field("label", "string", 39, 60));
    schema.fields.back().max_bytes = 16;
    schema.fields.back().max_bytes_range = range(52, 54);
    schema.annotations.push_back(annotation("bad-name", "telemetry", 61, 78));

    DiagnosticEngine diagnostics;
    const SourceSchemaLoweringResult result = lower_source_schema(schema, diagnostics);

    ASSERT_FALSE(result.ast.has_value());
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.diagnostics().front().id().str(), "BC2401");
}

TEST(SourceSchemaLoweringTest, RejectsMalformedTypeSpellingsAtomically) {
    for (const std::string_view spelling : {"uint32[64]", "uint32[][]"}) {
        SourceSchemaDocument schema;
        schema.source_range = range(0, 80);
        schema.namespace_spelling = "breadcrumbs.telemetry";
        schema.namespace_range = range(0, 24);
        schema.record_name = "Sample";
        schema.record_range = range(25, 31);
        schema.version = 1;
        schema.version_range = range(32, 33);
        schema.record_type_spelling = "data";
        schema.record_type_range = range(34, 38);
        schema.fields.push_back(field("samples", std::string(spelling), 39, 60));

        DiagnosticEngine diagnostics;
        const SourceSchemaLoweringResult result = lower_source_schema(schema, diagnostics);

        ASSERT_FALSE(result.ast.has_value());
        ASSERT_FALSE(diagnostics.empty()) << spelling;
        EXPECT_EQ(diagnostics.diagnostics().front().id().str(), "BC2402") << spelling;
    }
}

TEST(SourceSchemaLoweringTest, RejectsUnsupportedImportsAtomically) {
    SourceSchemaDocument schema;
    schema.source_range = range(0, 80);
    schema.namespace_spelling = "breadcrumbs.telemetry";
    schema.namespace_range = range(0, 24);
    schema.record_name = "Sample";
    schema.record_range = range(25, 31);
    schema.version = 1;
    schema.version_range = range(32, 33);
    schema.record_type_spelling = "data";
    schema.record_type_range = range(34, 38);
    schema.fields.push_back(field("label", "string", 39, 60));
    schema.fields.back().max_bytes = 16;
    schema.fields.back().max_bytes_range = range(52, 54);
    schema.imports =
        scalar_node("core.schema", breadcrumbs::compiler::yaml::YamlScalarKind::Plain, 61, 72);
    schema.imports_range = range(61, 72);

    DiagnosticEngine diagnostics;
    const SourceSchemaLoweringResult result = lower_source_schema(schema, diagnostics);

    ASSERT_FALSE(result.ast.has_value());
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.diagnostics().front().id().str(), "BC2403");
}

TEST(SourceSchemaLoweringTest, PreservesEmptyImports) {
    SourceSchemaDocument schema;
    schema.source_range = range(0, 80);
    schema.namespace_spelling = "breadcrumbs.telemetry";
    schema.namespace_range = range(0, 24);
    schema.record_name = "Sample";
    schema.record_range = range(25, 31);
    schema.version = 1;
    schema.version_range = range(32, 33);
    schema.record_type_spelling = "data";
    schema.record_type_range = range(34, 38);
    schema.fields.push_back(field("label", "string", 39, 60));
    schema.fields.back().max_bytes = 16;
    schema.fields.back().max_bytes_range = range(52, 54);
    schema.imports = scalar_node("", breadcrumbs::compiler::yaml::YamlScalarKind::Plain, 61, 61);
    schema.imports_range = range(61, 61);

    DiagnosticEngine diagnostics;
    const SourceSchemaLoweringResult result = lower_source_schema(schema, diagnostics);

    ASSERT_TRUE(result.ast.has_value()) << diagnostics_summary(diagnostics);
    ASSERT_TRUE(diagnostics.empty());
}

TEST(YamlCompilerPipelineTest, ScalarAndEnumSourceReachesSchemaIr) {
    const std::string source = R"(namespace: breadcrumbs.telemetry
record: Sample
version: 1
type: data
fields:
  mode:
    type: Mode
  count:
    type: u32
  label:
    type: string
    max_bytes: 16
enums:
  Mode:
    values:
      ready: 1
      unknown: 0
      failed: -1
annotations:
  owner: telemetry
)";

    PipelineOutput output;
    output.source_file_id = output.context.source_manager().add_source("/test/schema.yaml", source);

    output.parse_result = YamlParser::parse(output.context.source_manager(), output.source_file_id,
                                            output.parser_diagnostics);
    ASSERT_TRUE(output.parse_result.document.has_value())
        << diagnostics_summary(output.parser_diagnostics);
    EXPECT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);

    output.decode_result = decode_schema(*output.parse_result.document, output.decoder_diagnostics);
    ASSERT_TRUE(output.decode_result.schema.has_value())
        << diagnostics_summary(output.decoder_diagnostics);
    EXPECT_TRUE(output.decoder_diagnostics.empty())
        << diagnostics_summary(output.decoder_diagnostics);

    output.lowering_result =
        lower_source_schema(*output.decode_result.schema, output.lowering_diagnostics);
    ASSERT_TRUE(output.lowering_result.ast.has_value())
        << diagnostics_summary(output.lowering_diagnostics);
    EXPECT_TRUE(output.lowering_diagnostics.empty())
        << diagnostics_summary(output.lowering_diagnostics);

    const auto& ast = *output.lowering_result.ast;
    ASSERT_EQ(ast.declarations.size(), 1U);
    const auto& top_namespace = std::get<NamespaceDeclarationSyntax>(ast.declarations[0]->value);
    EXPECT_EQ(top_namespace.name.text(), "breadcrumbs.telemetry");
    ASSERT_EQ(top_namespace.annotations.size(), 1U);
    EXPECT_EQ(top_namespace.annotations[0].name.text(), "owner");
    EXPECT_EQ(top_namespace.annotations[0].value, std::optional<std::string>("telemetry"));
    ASSERT_EQ(top_namespace.declarations.size(), 2U);

    const auto& lowered_enum =
        std::get<EnumDeclarationSyntax>(top_namespace.declarations[0]->value);
    EXPECT_EQ(lowered_enum.name.text, "Mode");
    ASSERT_EQ(lowered_enum.values.size(), 3U);
    EXPECT_EQ(lowered_enum.values[0].name.text, "ready");
    EXPECT_EQ(lowered_enum.values[0].value, std::optional<std::string>("1"));
    EXPECT_EQ(lowered_enum.values[1].name.text, "unknown");
    EXPECT_EQ(lowered_enum.values[1].value, std::optional<std::string>("0"));
    EXPECT_EQ(lowered_enum.values[2].name.text, "failed");
    EXPECT_EQ(lowered_enum.values[2].value, std::optional<std::string>("-1"));

    const auto& lowered_record =
        std::get<RecordDeclarationSyntax>(top_namespace.declarations[1]->value);
    EXPECT_EQ(lowered_record.name.text, "Sample");
    EXPECT_EQ(lowered_record.version, 1);
    EXPECT_EQ(lowered_record.record_type_spelling, "data");
    ASSERT_EQ(lowered_record.fields.size(), 3U);
    EXPECT_EQ(lowered_record.fields[0].name.text, "mode");
    EXPECT_EQ(lowered_record.fields[1].name.text, "count");
    EXPECT_EQ(lowered_record.fields[2].name.text, "label");
    EXPECT_EQ(lowered_record.fields[2].max_bytes, std::optional<std::int64_t>(16));

    NamespaceBuilder namespace_builder;
    output.symbol_table =
        std::make_unique<SymbolTable>(namespace_builder.build(ast, output.symbol_diagnostics));
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);

    SemanticValidator semantic_validator;
    output.semantic_model =
        semantic_validator.validate(ast, *output.symbol_table, output.semantic_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);

    const SemanticRecord* semantic_record =
        output.semantic_model.find_record("breadcrumbs.telemetry.Sample");
    ASSERT_NE(semantic_record, nullptr);
    const SemanticField* semantic_mode = semantic_record->find_field("mode");
    ASSERT_NE(semantic_mode, nullptr);
    EXPECT_TRUE(semantic_mode->type.is_enum_reference());
    EXPECT_EQ(semantic_mode->type.enum_reference().canonical_target_fqn,
              "breadcrumbs.telemetry.Mode");

    LayoutComputer layout_computer;
    output.layout_model =
        layout_computer.compute(output.semantic_model, output.context, output.layout_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);

    const auto* layout_record = output.layout_model.find_record("breadcrumbs.telemetry.Sample");
    ASSERT_NE(layout_record, nullptr);
    EXPECT_EQ(layout_record->record_id, 1U);
    ASSERT_EQ(layout_record->fields.size(), 3U);
    EXPECT_EQ(layout_record->fields[0].field_index, 0U);
    EXPECT_EQ(layout_record->fields[1].field_index, 1U);
    EXPECT_EQ(layout_record->fields[2].field_index, 2U);

    SchemaIrBuilder schema_ir_builder;
    output.schema_ir =
        schema_ir_builder.build(ast, output.semantic_model, output.layout_model,
                                *output.symbol_table, output.context, output.schema_ir_diagnostics);
    ASSERT_TRUE(output.schema_ir_diagnostics.empty())
        << diagnostics_summary(output.schema_ir_diagnostics);

    SchemaIrValidator schema_ir_validator;
    schema_ir_validator.validate(output.schema_ir, output.context, output.validation_diagnostics);
    ASSERT_TRUE(output.validation_diagnostics.empty())
        << diagnostics_summary(output.validation_diagnostics);

    const auto& root = output.schema_ir.root_namespace();
    ASSERT_EQ(root.namespaces_size(), 1);
    const auto& breadcrumbs_ns = root.namespaces(0);
    EXPECT_EQ(breadcrumbs_ns.name(), "breadcrumbs");
    ASSERT_EQ(breadcrumbs_ns.namespaces_size(), 1);
    const auto& telemetry_ns = breadcrumbs_ns.namespaces(0);
    EXPECT_EQ(telemetry_ns.name(), "telemetry");
    ASSERT_EQ(telemetry_ns.enums_size(), 1);
    ASSERT_EQ(telemetry_ns.records_size(), 1);

    const auto& lowered_ir_enum = telemetry_ns.enums(0);
    EXPECT_EQ(lowered_ir_enum.name(), "Mode");
    ASSERT_EQ(lowered_ir_enum.values_size(), 3);
    EXPECT_EQ(lowered_ir_enum.values(0).value(), 1);
    EXPECT_EQ(lowered_ir_enum.values(1).value(), 0);
    EXPECT_EQ(lowered_ir_enum.values(2).value(), -1);
    EXPECT_EQ(lowered_ir_enum.source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(lowered_ir_enum.source_origin().span().start_line(), 14U);

    const auto& lowered_ir_record = telemetry_ns.records(0);
    EXPECT_EQ(lowered_ir_record.name(), "Sample");
    EXPECT_EQ(lowered_ir_record.record_id(), layout_record->record_id);
    EXPECT_NE(lowered_ir_record.ir_id(), lowered_ir_record.record_id());
    EXPECT_EQ(lowered_ir_record.source_origin().file(), "/test/schema.yaml");
    EXPECT_EQ(lowered_ir_record.source_origin().span().start_line(), 1U);
    ASSERT_EQ(lowered_ir_record.fields_size(), 3);
    EXPECT_EQ(lowered_ir_record.fields(0).name(), "mode");
    EXPECT_EQ(lowered_ir_record.fields(1).name(), "count");
    EXPECT_EQ(lowered_ir_record.fields(2).name(), "label");
    EXPECT_EQ(lowered_ir_record.fields(0).field_index(), 0U);
    EXPECT_EQ(lowered_ir_record.fields(1).field_index(), 1U);
    EXPECT_EQ(lowered_ir_record.fields(2).field_index(), 2U);
    EXPECT_TRUE(lowered_ir_record.fields(0).type().has_enum_type());
    EXPECT_EQ(lowered_ir_record.fields(0).type().enum_type().target_enum_ir_id(),
              lowered_ir_enum.ir_id());
}

TEST(YamlCompilerPipelineTest, BoundedVariableArrayReachesSchemaIr) {
    const std::string source = R"(namespace: breadcrumbs.telemetry
record: Samples
version: 1
type: data
fields:
  samples:
    type: uint32[]
    max_elements: 64
  label:
    type: string
    max_bytes: 16
)";

    PipelineOutput output;
    output.source_file_id = output.context.source_manager().add_source("/test/arrays.yaml", source);

    output.parse_result = YamlParser::parse(output.context.source_manager(), output.source_file_id,
                                            output.parser_diagnostics);
    ASSERT_TRUE(output.parse_result.document.has_value())
        << diagnostics_summary(output.parser_diagnostics);
    EXPECT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);

    output.decode_result = decode_schema(*output.parse_result.document, output.decoder_diagnostics);
    ASSERT_TRUE(output.decode_result.schema.has_value())
        << diagnostics_summary(output.decoder_diagnostics);
    EXPECT_TRUE(output.decoder_diagnostics.empty())
        << diagnostics_summary(output.decoder_diagnostics);

    output.lowering_result =
        lower_source_schema(*output.decode_result.schema, output.lowering_diagnostics);
    ASSERT_TRUE(output.lowering_result.ast.has_value())
        << diagnostics_summary(output.lowering_diagnostics);
    EXPECT_TRUE(output.lowering_diagnostics.empty())
        << diagnostics_summary(output.lowering_diagnostics);

    const auto& ast = *output.lowering_result.ast;
    ASSERT_EQ(ast.declarations.size(), 1U);
    const auto& top_namespace = std::get<NamespaceDeclarationSyntax>(ast.declarations[0]->value);
    ASSERT_EQ(top_namespace.declarations.size(), 1U);
    const auto& lowered_record =
        std::get<RecordDeclarationSyntax>(top_namespace.declarations[0]->value);
    ASSERT_EQ(lowered_record.fields.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<ArrayTypeSyntax>(lowered_record.fields[0].type));
    const auto& lowered_array = std::get<ArrayTypeSyntax>(lowered_record.fields[0].type);
    EXPECT_EQ(lowered_array.kind, ArrayTypeSyntaxKind::BoundedVariableLength);
    EXPECT_FALSE(lowered_array.fixed_size.has_value());
    EXPECT_EQ(lowered_record.fields[0].max_elements, std::optional<std::int64_t>(64));
    EXPECT_TRUE(lowered_record.fields[0].max_elements_source_range.is_valid());
    EXPECT_EQ(lowered_record.fields[0].max_elements_source_range.begin().file_id(),
              output.source_file_id);
    EXPECT_EQ(lowered_record.fields[0].max_elements_source_range.end().file_id(),
              output.source_file_id);

    NamespaceBuilder namespace_builder;
    output.symbol_table =
        std::make_unique<SymbolTable>(namespace_builder.build(ast, output.symbol_diagnostics));
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);

    SemanticValidator semantic_validator;
    output.semantic_model =
        semantic_validator.validate(ast, *output.symbol_table, output.semantic_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);

    const SemanticRecord* semantic_record =
        output.semantic_model.find_record("breadcrumbs.telemetry.Samples");
    ASSERT_NE(semantic_record, nullptr);
    ASSERT_TRUE(semantic_record->version.has_value());
    EXPECT_EQ(*semantic_record->version, 1U);
    ASSERT_TRUE(semantic_record->record_type.has_value());
    EXPECT_EQ(*semantic_record->record_type, breadcrumbs::compiler::semantic::SemanticRecordType::Data);
    EXPECT_EQ(semantic_record->fields.size(), 2U);
    EXPECT_TRUE(semantic_record->fields[0].type.is_array());
    EXPECT_EQ(semantic_record->fields[0].type.array().max_elements, 64U);
    EXPECT_TRUE(semantic_record->fields[1].type.is_string());
    EXPECT_EQ(
        std::get<breadcrumbs::compiler::semantic::SemanticStringType>(semantic_record->fields[1].type.value)
            .max_bytes,
        16U);

    LayoutComputer layout_computer;
    output.layout_model =
        layout_computer.compute(output.semantic_model, output.context, output.layout_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);

    SchemaIrBuilder schema_ir_builder;
    output.schema_ir =
        schema_ir_builder.build(ast, output.semantic_model, output.layout_model,
                                *output.symbol_table, output.context, output.schema_ir_diagnostics);
    ASSERT_TRUE(output.schema_ir_diagnostics.empty())
        << diagnostics_summary(output.schema_ir_diagnostics);

    SchemaIrValidator schema_ir_validator;
    schema_ir_validator.validate(output.schema_ir, output.context, output.validation_diagnostics);
    ASSERT_TRUE(output.validation_diagnostics.empty())
        << diagnostics_summary(output.validation_diagnostics);

    const auto& schema_ir_record =
        output.schema_ir.root_namespace().namespaces(0).namespaces(0).records(0);
    EXPECT_TRUE(schema_ir_record.has_schema_version());
    EXPECT_EQ(schema_ir_record.schema_version(), 1U);
    EXPECT_TRUE(schema_ir_record.has_record_type());
    EXPECT_EQ(schema_ir_record.record_type(),
              breadcrumbs::schema_ir::RECORD_TYPE_DATA);
    ASSERT_EQ(schema_ir_record.fields_size(), 2);
    EXPECT_TRUE(schema_ir_record.fields(0).type().has_array());
    EXPECT_EQ(schema_ir_record.fields(0).type().array().max_elements(), 64U);
    EXPECT_TRUE(schema_ir_record.fields(1).type().has_string());
    EXPECT_EQ(schema_ir_record.fields(1).type().string().max_bytes(), 16U);
}

} // namespace
