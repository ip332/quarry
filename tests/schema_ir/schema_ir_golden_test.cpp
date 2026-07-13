#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/symbols/symbols.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <google/protobuf/text_format.h>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::context::CompilerContext;
using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::layout::LayoutComputer;
using breadcrumbs::compiler::layout::LayoutModel;
using breadcrumbs::compiler::parser::Parser;
using breadcrumbs::compiler::schema_ir::SchemaIrBuilder;
using breadcrumbs::compiler::schema_ir::SchemaIrModel;
using breadcrumbs::compiler::schema_ir::SchemaIrValidator;
using breadcrumbs::compiler::semantic::SemanticModel;
using breadcrumbs::compiler::semantic::SemanticValidator;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::symbols::NamespaceBuilder;
using breadcrumbs::compiler::symbols::SymbolTable;

struct FrontendOutput {
    CompilerContext context;
    breadcrumbs::compiler::ast::SchemaFileSyntax ast;
    DiagnosticEngine parser_diagnostics;
    DiagnosticEngine symbol_diagnostics;
    DiagnosticEngine semantic_diagnostics;
    DiagnosticEngine layout_diagnostics;
    DiagnosticEngine lowering_diagnostics;
    DiagnosticEngine validation_diagnostics;
    std::unique_ptr<SymbolTable> symbol_table;
    SemanticModel semantic_model;
    LayoutModel layout_model;
    SchemaIrModel schema_ir;
    SourceFileId source_file_id;
};

[[nodiscard]] std::filesystem::path fixtures_root() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" / "schema_ir";
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

void trim_trailing_newlines(std::string& text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
}

[[nodiscard]] FrontendOutput run_frontend(
    const std::string& text,
    const std::function<void(breadcrumbs::compiler::ast::SchemaFileSyntax&)>& ast_mutator = {}) {
    FrontendOutput output;
    output.source_file_id = output.context.source_manager().add_source("/test/schema.brd", text);

    auto parse_result = Parser::parse(output.context.source_manager(), output.source_file_id,
                                      output.parser_diagnostics);
    output.ast = std::move(parse_result.ast);
    if (ast_mutator) {
        ast_mutator(output.ast);
    }

    NamespaceBuilder namespace_builder;
    output.symbol_table = std::make_unique<SymbolTable>(
        namespace_builder.build(output.ast, output.symbol_diagnostics));

    SemanticValidator validator;
    output.semantic_model =
        validator.validate(output.ast, *output.symbol_table, output.semantic_diagnostics);

    if (output.semantic_diagnostics.empty()) {
        LayoutComputer layout_computer;
        output.layout_model = layout_computer.compute(output.semantic_model, output.context,
                                                      output.layout_diagnostics);
    }

    SchemaIrBuilder schema_ir_builder;
    if (output.semantic_diagnostics.empty() && output.layout_diagnostics.empty()) {
        output.schema_ir = schema_ir_builder.build(output.ast, output.semantic_model,
                                                   output.layout_model, *output.symbol_table,
                                                   output.context, output.lowering_diagnostics);
    }

    SchemaIrValidator schema_ir_validator;
    if (output.semantic_diagnostics.empty() && output.layout_diagnostics.empty() &&
        output.lowering_diagnostics.empty()) {
        schema_ir_validator.validate(output.schema_ir, output.context,
                                     output.validation_diagnostics);
    }
    return output;
}

void clear_source_metadata(::breadcrumbs::schema_ir::NamespaceIR* namespace_ir) {
    if (namespace_ir == nullptr) {
        return;
    }

    namespace_ir->clear_source_origin();
    for (int index = 0; index < namespace_ir->namespaces_size(); ++index) {
        clear_source_metadata(namespace_ir->mutable_namespaces(index));
    }
    for (int index = 0; index < namespace_ir->records_size(); ++index) {
        ::breadcrumbs::schema_ir::RecordIR* record = namespace_ir->mutable_records(index);
        record->clear_source_origin();
        for (int field_index = 0; field_index < record->fields_size(); ++field_index) {
            record->mutable_fields(field_index)->clear_source_origin();
        }
    }
    for (int index = 0; index < namespace_ir->enums_size(); ++index) {
        ::breadcrumbs::schema_ir::EnumIR* enum_ir = namespace_ir->mutable_enums(index);
        enum_ir->clear_source_origin();
        for (int value_index = 0; value_index < enum_ir->values_size(); ++value_index) {
            enum_ir->mutable_values(value_index)->clear_source_origin();
        }
    }
}

[[nodiscard]] std::string render_normalized_textproto(SchemaIrModel schema_ir) {
    clear_source_metadata(schema_ir.mutable_root_namespace());

    google::protobuf::TextFormat::Printer printer;
    printer.SetSingleLineMode(false);
    printer.SetPrintMessageFieldsInIndexOrder(true);

    std::string output;
    EXPECT_TRUE(printer.PrintToString(schema_ir, &output));
    trim_trailing_newlines(output);
    return output;
}

[[nodiscard]] std::string golden_name(std::string_view fixture_name) {
    return std::string(fixture_name) + ".textproto";
}

[[nodiscard]] std::string source_name(std::string_view fixture_name) {
    return std::string(fixture_name) + ".brd";
}

[[nodiscard]] std::string fixture_text(std::string_view fixture_name) {
    std::string text = read_file(fixtures_root() / source_name(fixture_name));
    trim_trailing_newlines(text);
    return text;
}

[[nodiscard]] std::string golden_text(std::string_view fixture_name) {
    std::string text = read_file(fixtures_root() / golden_name(fixture_name));
    trim_trailing_newlines(text);
    return text;
}

void expect_fixture_matches_golden(std::string_view fixture_name) {
    const std::string source = fixture_text(fixture_name);
    const std::string expected = golden_text(fixture_name);

    const FrontendOutput output =
        fixture_name == "builtin_fields"
            ? run_frontend(
                  source,
                  [](auto& ast) {
                      auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                          ast.declarations[0]->value);
                      record.fields[2].max_bytes = 16;
                      record.fields[2].max_bytes_source_range = record.fields[2].source_range;
                      record.fields[3].max_bytes = 4;
                      record.fields[3].max_bytes_source_range = record.fields[3].source_range;
                  })
            : run_frontend(source);
    ASSERT_TRUE(output.parser_diagnostics.empty());
    ASSERT_TRUE(output.symbol_diagnostics.empty());
    ASSERT_TRUE(output.semantic_diagnostics.empty());
    ASSERT_TRUE(output.layout_diagnostics.empty());
    ASSERT_TRUE(output.lowering_diagnostics.empty());
    ASSERT_TRUE(output.validation_diagnostics.empty());

    const std::string actual = render_normalized_textproto(output.schema_ir);
    EXPECT_EQ(actual, expected) << "fixture: " << fixture_name;
}

TEST(SchemaIrGoldenTest, EmptySchemaMatchesGolden) { expect_fixture_matches_golden("empty"); }

TEST(SchemaIrGoldenTest, SingleRecordMatchesGolden) {
    expect_fixture_matches_golden("single_record");
}

TEST(SchemaIrGoldenTest, BuiltinFieldsMatchGolden) {
    expect_fixture_matches_golden("builtin_fields");
}

TEST(SchemaIrGoldenTest, NestedNamespaceMatchesGolden) {
    expect_fixture_matches_golden("nested_namespace");
}

TEST(SchemaIrGoldenTest, NamedTypeReferenceMatchesGolden) {
    expect_fixture_matches_golden("named_type_reference");
}

TEST(SchemaIrGoldenTest, MultipleTopLevelNamespacesMatchGolden) {
    expect_fixture_matches_golden("multiple_top_level_namespaces");
}

TEST(SchemaIrGoldenTest, EnumMatchesGolden) { expect_fixture_matches_golden("enum"); }

} // namespace
