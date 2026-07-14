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

[[nodiscard]] std::string diagnostics_summary(const DiagnosticEngine& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.compiler_pass() << ": " << diagnostic.id().str() << ": "
               << diagnostic.message() << '\n';
    }
    return stream.str();
}

struct LegacyGoldenOutput {
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

[[nodiscard]] LegacyGoldenOutput compile_legacy_fixture(std::string_view fixture_name) {
    LegacyGoldenOutput output;
    std::string text = read_file(fixtures_root() / (std::string(fixture_name) + ".brd"));
    trim_trailing_newlines(text);
    output.source_file_id =
        output.context.source_manager().add_source("/test/schema.brd", std::move(text));

    auto parse_result = Parser::parse(output.context.source_manager(), output.source_file_id,
                                      output.parser_diagnostics);
    output.ast = std::move(parse_result.ast);
    NamespaceBuilder namespace_builder;
    output.symbol_table = std::make_unique<SymbolTable>(
        namespace_builder.build(output.ast, output.symbol_diagnostics));
    SemanticValidator semantic_validator;
    output.semantic_model =
        semantic_validator.validate(output.ast, *output.symbol_table, output.semantic_diagnostics);
    if (output.semantic_diagnostics.empty()) {
        LayoutComputer layout_computer;
        output.layout_model = layout_computer.compute(output.semantic_model, output.context,
                                                      output.layout_diagnostics);
    }
    if (output.semantic_diagnostics.empty() && output.layout_diagnostics.empty()) {
        SchemaIrBuilder schema_ir_builder;
        output.schema_ir = schema_ir_builder.build(output.ast, output.semantic_model,
                                                   output.layout_model, *output.symbol_table,
                                                   output.context, output.lowering_diagnostics);
    }
    if (output.semantic_diagnostics.empty() && output.layout_diagnostics.empty() &&
        output.lowering_diagnostics.empty()) {
        SchemaIrValidator schema_ir_validator;
        schema_ir_validator.validate(output.schema_ir, output.context,
                                     output.validation_diagnostics);
    }
    return output;
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

[[nodiscard]] std::string render_normalized_textproto(LegacyGoldenOutput& output) {
    clear_source_metadata(output.schema_ir.mutable_root_namespace());

    google::protobuf::TextFormat::Printer printer;
    printer.SetSingleLineMode(false);
    printer.SetPrintMessageFieldsInIndexOrder(true);

    std::string text;
    EXPECT_TRUE(printer.PrintToString(output.schema_ir, &text));
    trim_trailing_newlines(text);
    return text;
}

[[nodiscard]] std::string golden_text(std::string_view fixture_name) {
    std::string text =
        read_file(fixtures_root() / (std::string(fixture_name) + ".textproto"));
    trim_trailing_newlines(text);
    return text;
}

TEST(SchemaIrLegacyGoldenTest, MultipleTopLevelNamespacesMatchesGolden) {
    LegacyGoldenOutput output = compile_legacy_fixture("multiple_top_level_namespaces");

    ASSERT_TRUE(output.parser_diagnostics.empty()) << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty()) << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty()) << diagnostics_summary(output.layout_diagnostics);
    ASSERT_TRUE(output.lowering_diagnostics.empty())
        << diagnostics_summary(output.lowering_diagnostics);
    ASSERT_TRUE(output.validation_diagnostics.empty())
        << diagnostics_summary(output.validation_diagnostics);

    const std::string actual = render_normalized_textproto(output);
    const std::string expected = golden_text("multiple_top_level_namespaces");
    EXPECT_EQ(actual, expected);

    const auto* alpha = find_namespace(output.schema_ir.root_namespace(), "alpha");
    ASSERT_NE(alpha, nullptr);
    const auto* beta = find_namespace(output.schema_ir.root_namespace(), "beta");
    ASSERT_NE(beta, nullptr);
}

} // namespace
