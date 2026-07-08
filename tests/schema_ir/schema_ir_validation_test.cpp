#include "compiler/ast/ast.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/imports/imports.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/symbols/symbols.hpp"

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::context::CompilerContext;
using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::diagnostics::DiagnosticFormatter;
using breadcrumbs::compiler::imports::CompilationUnit;
using breadcrumbs::compiler::layout::LayoutModel;
using breadcrumbs::compiler::parser::Parser;
using breadcrumbs::compiler::schema_ir::SchemaIrBuilder;
using breadcrumbs::compiler::schema_ir::SchemaIrModel;
using breadcrumbs::compiler::schema_ir::SchemaIrValidator;
using breadcrumbs::compiler::semantic::SemanticModel;
using breadcrumbs::compiler::semantic::SemanticValidator;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceManager;
using breadcrumbs::compiler::symbols::NamespaceBuilder;
using breadcrumbs::compiler::symbols::SymbolModel;

struct FrontendOutput {
    CompilerContext context;
    breadcrumbs::compiler::ast::SchemaFileSyntax ast;
    DiagnosticEngine parser_diagnostics;
    DiagnosticEngine symbol_diagnostics;
    DiagnosticEngine semantic_diagnostics;
    DiagnosticEngine lowering_diagnostics;
    DiagnosticEngine validation_diagnostics;
    std::unique_ptr<SymbolModel> symbol_model;
    SemanticModel semantic_model;
    LayoutModel layout_model;
    SchemaIrModel schema_ir;
    SourceFileId source_file_id;
};

[[nodiscard]] SourceFileId add_test_source(CompilerContext& context) {
    return context.source_manager().add_source("/test/schema.brd", "");
}

[[nodiscard]] FrontendOutput run_frontend(std::string text, bool run_semantic) {
    FrontendOutput output;
    output.source_file_id =
        output.context.source_manager().add_source("/test/schema.brd", std::move(text));

    auto parse_result = Parser::parse(output.context.source_manager(), output.source_file_id,
                                      output.parser_diagnostics);
    output.ast = std::move(parse_result.ast);

    CompilationUnit compilation_unit;
    compilation_unit.asts.push_back(&output.ast);

    NamespaceBuilder namespace_builder;
    output.symbol_model = std::make_unique<SymbolModel>(
        namespace_builder.build(compilation_unit, output.symbol_diagnostics));

    if (run_semantic) {
        SemanticValidator validator;
        output.semantic_model =
            validator.validate(output.ast, *output.symbol_model, output.semantic_diagnostics);
    }

    SchemaIrBuilder schema_ir_builder;
    output.schema_ir =
        schema_ir_builder.build(output.ast, output.semantic_model, output.layout_model,
                                *output.symbol_model, output.context, output.lowering_diagnostics);
    return output;
}

[[nodiscard]] FrontendOutput run_valid_frontend(std::string text) {
    return run_frontend(std::move(text), true);
}

[[nodiscard]] std::string diagnostics_summary(const DiagnosticEngine& diagnostics,
                                              const SourceManager& source_manager) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << DiagnosticFormatter::format(diagnostic, source_manager) << '\n';
    }
    return stream.str();
}

[[nodiscard]] SchemaIrModel make_schema_ir() {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    schema_ir.mutable_root_namespace()->set_ir_id(1);
    schema_ir.mutable_root_namespace()->set_name("");
    schema_ir.mutable_root_namespace()->set_fqn("");
    return schema_ir;
}

void set_source_origin(::breadcrumbs::schema_ir::SourceOrigin* origin, std::string_view path,
                       std::uint32_t start_offset, std::uint32_t end_offset,
                       std::uint32_t start_line = 1, std::uint32_t start_column = 1,
                       std::uint32_t end_line = 1, std::uint32_t end_column = 1) {
    origin->set_source_unit(std::string(path));
    origin->set_file(std::string(path));
    origin->mutable_span()->set_start_offset(start_offset);
    origin->mutable_span()->set_end_offset(end_offset);
    origin->mutable_span()->set_start_line(start_line);
    origin->mutable_span()->set_start_column(start_column);
    origin->mutable_span()->set_end_line(end_line);
    origin->mutable_span()->set_end_column(end_column);
}

template <typename Message>
void set_origin(Message* message, std::string_view path, std::uint32_t start_offset,
                std::uint32_t end_offset, std::uint32_t start_line = 1,
                std::uint32_t start_column = 1, std::uint32_t end_line = 1,
                std::uint32_t end_column = 1) {
    set_source_origin(message->mutable_source_origin(), path, start_offset, end_offset, start_line,
                      start_column, end_line, end_column);
}

void validate_schema_ir(const SchemaIrModel& schema_ir, CompilerContext& context,
                        DiagnosticEngine& diagnostics) {
    SchemaIrValidator validator;
    validator.validate(schema_ir, context, diagnostics);
}

TEST(SchemaIrValidationTest, AcceptsValidatedFrontendOutput) {
    FrontendOutput output = run_valid_frontend(R"(record Example {
  active: bool
  count: u32
  label: string
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty());
    ASSERT_TRUE(output.symbol_diagnostics.empty());
    ASSERT_TRUE(output.semantic_diagnostics.empty());
    ASSERT_TRUE(output.lowering_diagnostics.empty())
        << diagnostics_summary(output.lowering_diagnostics, output.context.source_manager());

    validate_schema_ir(output.schema_ir, output.context, output.validation_diagnostics);
    EXPECT_TRUE(output.validation_diagnostics.empty())
        << diagnostics_summary(output.validation_diagnostics, output.context.source_manager());
}

TEST(SchemaIrValidationTest, RejectsDuplicateNamespaceNamesInTheSameScope) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();
    auto* first = root->add_namespaces();
    first->set_ir_id(2);
    first->set_name("geo");
    first->set_fqn("geo");
    set_origin(first, "/test/schema.brd", 0, 0);

    auto* second = root->add_namespaces();
    second->set_ir_id(3);
    second->set_name("geo");
    second->set_fqn("geo");
    set_origin(second, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6002");
}

TEST(SchemaIrValidationTest, RejectsDuplicateRecordNamesInTheSameScope) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* first = root->add_records();
    first->set_ir_id(2);
    first->set_name("Location");
    first->set_fqn("Location");
    set_origin(first, "/test/schema.brd", 0, 0);

    auto* second = root->add_records();
    second->set_ir_id(3);
    second->set_name("Location");
    second->set_fqn("Location");
    set_origin(second, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6002");
}

TEST(SchemaIrValidationTest, AllowsTheSameNameInDifferentNamespaces) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* geo = root->add_namespaces();
    geo->set_ir_id(2);
    geo->set_name("geo");
    geo->set_fqn("geo");
    set_origin(geo, "/test/schema.brd", 0, 0);

    auto* geo_record = geo->add_records();
    geo_record->set_ir_id(3);
    geo_record->set_name("Location");
    geo_record->set_fqn("geo.Location");
    set_origin(geo_record, "/test/schema.brd", 0, 0);

    auto* telemetry = root->add_namespaces();
    telemetry->set_ir_id(4);
    telemetry->set_name("telemetry");
    telemetry->set_fqn("telemetry");
    set_origin(telemetry, "/test/schema.brd", 0, 0);

    auto* telemetry_record = telemetry->add_records();
    telemetry_record->set_ir_id(5);
    telemetry_record->set_name("Location");
    telemetry_record->set_fqn("telemetry.Location");
    set_origin(telemetry_record, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    EXPECT_TRUE(diagnostics.empty()) << diagnostics_summary(diagnostics, context.source_manager());
}

TEST(SchemaIrValidationTest, RejectsDuplicateFieldNames) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("origin");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL);

    auto* duplicate_field = record->add_fields();
    duplicate_field->set_name("origin");
    set_origin(duplicate_field, "/test/schema.brd", 0, 0);
    duplicate_field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6003");
}

TEST(SchemaIrValidationTest, RejectsDuplicateEnumValueNames) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* enum_ir = schema_ir.mutable_root_namespace()->add_enums();
    enum_ir->set_ir_id(2);
    enum_ir->set_name("Direction");
    enum_ir->set_fqn("Direction");
    set_origin(enum_ir, "/test/schema.brd", 0, 0);

    auto* first = enum_ir->add_values();
    first->set_name("north");
    first->set_value(0);
    set_origin(first, "/test/schema.brd", 0, 0);

    auto* duplicate = enum_ir->add_values();
    duplicate->set_name("north");
    duplicate->set_value(1);
    set_origin(duplicate, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6003");
}

TEST(SchemaIrValidationTest, RejectsDuplicateIds) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* first = root->add_records();
    first->set_ir_id(2);
    first->set_name("Location");
    first->set_fqn("Location");
    set_origin(first, "/test/schema.brd", 0, 0);

    auto* second = root->add_enums();
    second->set_ir_id(2);
    second->set_name("Mode");
    second->set_fqn("Mode");
    set_origin(second, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6004");
}

TEST(SchemaIrValidationTest, RejectsMissingRecordReferences) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("origin");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->mutable_record()->set_target_record_ir_id(999);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6005");
}

TEST(SchemaIrValidationTest, RejectsWrongKindRecordReferences) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* enum_ir = root->add_enums();
    enum_ir->set_ir_id(2);
    enum_ir->set_name("Mode");
    enum_ir->set_fqn("Mode");
    set_origin(enum_ir, "/test/schema.brd", 0, 0);

    auto* record = root->add_records();
    record->set_ir_id(3);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("mode");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->mutable_record()->set_target_record_ir_id(2);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6006");
}

TEST(SchemaIrValidationTest, RejectsMissingEnumReferences) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("mode");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(999);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6005");
}

TEST(SchemaIrValidationTest, RejectsWrongKindEnumReferences) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* root = schema_ir.mutable_root_namespace();

    auto* record = root->add_records();
    record->set_ir_id(2);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* enum_ir = root->add_enums();
    enum_ir->set_ir_id(3);
    enum_ir->set_name("Mode");
    enum_ir->set_fqn("Mode");
    set_origin(enum_ir, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("mode");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(2);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6006");
}

TEST(SchemaIrValidationTest, RejectsMissingFieldTypes) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("origin");
    set_origin(field, "/test/schema.brd", 0, 0);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6007");
}

TEST(SchemaIrValidationTest, RejectsInvalidArrayElementTypes) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0);

    auto* field = record->add_fields();
    field->set_name("samples");
    set_origin(field, "/test/schema.brd", 0, 0);
    field->mutable_type()->mutable_array();

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC6007");
}

TEST(SchemaIrValidationTest, UsesSourceMetadataInDiagnosticsWhenAvailable) {
    CompilerContext context;
    (void)add_test_source(context);

    SchemaIrModel schema_ir = make_schema_ir();
    auto* record = schema_ir.mutable_root_namespace()->add_records();
    record->set_ir_id(2);
    record->set_name("Route");
    record->set_fqn("Route");
    set_origin(record, "/test/schema.brd", 0, 0, 1, 1, 1, 1);

    auto* field = record->add_fields();
    field->set_name("origin");
    set_origin(field, "/test/schema.brd", 0, 0, 1, 1, 1, 1);
    field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL);

    auto* duplicate = record->add_fields();
    duplicate->set_name("origin");
    set_origin(duplicate, "/test/schema.brd", 0, 0, 1, 1, 1, 1);
    duplicate->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    DiagnosticEngine diagnostics;
    validate_schema_ir(schema_ir, context, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    const std::string formatted =
        DiagnosticFormatter::format(diagnostics.diagnostics()[0], context.source_manager());
    EXPECT_NE(formatted.find("/test/schema.brd:1:1"), std::string::npos);
    EXPECT_NE(formatted.find("BC6003"), std::string::npos);
}

} // namespace
