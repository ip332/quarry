#include "compiler/ast/ast.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/symbols/symbols.hpp"

#include <functional>
#include <memory>
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
using breadcrumbs::compiler::parser::Parser;
using breadcrumbs::compiler::schema_ir::SchemaIrBuilder;
using breadcrumbs::compiler::schema_ir::SchemaIrModel;
using breadcrumbs::compiler::semantic::SemanticModel;
using breadcrumbs::compiler::semantic::SemanticValidator;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::symbols::NamespaceBuilder;
using breadcrumbs::compiler::symbols::SymbolTable;

struct LegacyOutput {
    CompilerContext context;
    breadcrumbs::compiler::ast::SchemaFileSyntax ast;
    DiagnosticEngine parser_diagnostics;
    DiagnosticEngine symbol_diagnostics;
    DiagnosticEngine semantic_diagnostics;
    DiagnosticEngine layout_diagnostics;
    DiagnosticEngine lowering_diagnostics;
    std::unique_ptr<SymbolTable> symbol_table;
    SemanticModel semantic_model;
    LayoutModel layout_model;
    SchemaIrModel schema_ir;
    SourceFileId source_file_id;
};

[[nodiscard]] LegacyOutput run_legacy_pipeline(
    std::string text,
    const std::function<void(breadcrumbs::compiler::ast::SchemaFileSyntax&)>& ast_mutator = {}) {
    LegacyOutput output;
    output.source_file_id =
        output.context.source_manager().add_source("/test/schema.brd", std::move(text));

    auto parse_result = Parser::parse(output.context.source_manager(), output.source_file_id,
                                      output.parser_diagnostics);
    output.ast = std::move(parse_result.ast);
    if (ast_mutator) {
        ast_mutator(output.ast);
    }

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

    return output;
}

[[nodiscard]] std::string diagnostics_summary(const DiagnosticEngine& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.id().str() << ": " << diagnostic.message() << '\n';
    }
    return stream.str();
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

[[nodiscard]] const breadcrumbs::schema_ir::RecordIR*
find_record(const breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.records_size(); ++index) {
        const auto& record = parent.records(index);
        if (record.name() == name) {
            return &record;
        }
    }
    return nullptr;
}

TEST(SchemaIrLegacyBuilderTest, LowersMultipleTopLevelNamespacesAsSiblings) {
    const LegacyOutput output = run_legacy_pipeline(R"(namespace alpha.one {
  record First {
  }
}

namespace beta.two {
  record Second {
  }
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty()) << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty()) << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty()) << diagnostics_summary(output.layout_diagnostics);
    ASSERT_TRUE(output.lowering_diagnostics.empty())
        << diagnostics_summary(output.lowering_diagnostics);

    const auto& root = output.schema_ir.root_namespace();
    ASSERT_EQ(root.namespaces_size(), 2);

    const auto* alpha = find_namespace(root, "alpha");
    const auto* beta = find_namespace(root, "beta");
    ASSERT_NE(alpha, nullptr);
    ASSERT_NE(beta, nullptr);
    ASSERT_EQ(alpha->namespaces_size(), 1);
    ASSERT_EQ(beta->namespaces_size(), 1);
    EXPECT_EQ(alpha->namespaces(0).name(), "one");
    EXPECT_EQ(beta->namespaces(0).name(), "two");
    ASSERT_NE(find_record(alpha->namespaces(0), "First"), nullptr);
    ASSERT_NE(find_record(beta->namespaces(0), "Second"), nullptr);
}

} // namespace
