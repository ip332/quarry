#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/symbols/symbols.hpp"

#include <algorithm>
#include <cstddef>
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
    std::unique_ptr<SymbolTable> symbol_table;
    SemanticModel semantic_model;
    LayoutModel layout_model;
    SourceFileId source_file_id;
};

[[nodiscard]] FrontendOutput run_layout_pipeline(std::string text) {
    FrontendOutput output;
    output.source_file_id =
        output.context.source_manager().add_source("/test/schema.brd", std::move(text));

    auto parse_result = Parser::parse(output.context.source_manager(), output.source_file_id,
                                      output.parser_diagnostics);
    output.ast = std::move(parse_result.ast);

    NamespaceBuilder namespace_builder;
    output.symbol_table = std::make_unique<SymbolTable>(
        namespace_builder.build(output.ast, output.symbol_diagnostics));

    SemanticValidator validator;
    output.semantic_model =
        validator.validate(output.ast, *output.symbol_table, output.semantic_diagnostics);

    LayoutComputer computer;
    output.layout_model =
        computer.compute(output.semantic_model, output.context, output.layout_diagnostics);
    return output;
}

[[nodiscard]] std::string diagnostics_summary(const DiagnosticEngine& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.id().str() << ": " << diagnostic.message() << '\n';
    }
    return stream.str();
}

[[nodiscard]] std::string record_fields_source(std::size_t count) {
    std::ostringstream stream;
    stream << "record Example {\n";
    for (std::size_t index = 0; index < count; ++index) {
        stream << "  field" << index << ": u32\n";
    }
    stream << "}\n";
    return stream.str();
}

[[nodiscard]] const breadcrumbs::compiler::layout::RecordLayout*
find_record(const LayoutModel& model, std::string_view fqn) {
    return model.find_record(fqn);
}

TEST(LayoutSmokeTest, EmptySemanticModelProducesNoLayouts) {
    const FrontendOutput output = run_layout_pipeline("");

    ASSERT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    EXPECT_TRUE(output.layout_model.records.empty());
}

TEST(LayoutSmokeTest, OneEmptyRecordGetsInitialIdentity) {
    const FrontendOutput output = run_layout_pipeline("record Example {\n}\n");

    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    ASSERT_EQ(output.layout_model.records.size(), 1U);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->record_id, 1U);
    EXPECT_TRUE(layout->fields.empty());
}

TEST(LayoutSmokeTest, MultipleFieldsFollowDeclarationOrder) {
    const FrontendOutput output =
        run_layout_pipeline("record Example {\n  zeta: u32\n  alpha: u32\n  beta: u32\n}\n");

    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->fields.size(), 3U);
    EXPECT_EQ(layout->fields[0].field_index, 0U);
    EXPECT_EQ(layout->fields[1].field_index, 1U);
    EXPECT_EQ(layout->fields[2].field_index, 2U);
}

TEST(LayoutSmokeTest, CanonicalFqnOrderControlsRecordIds) {
    const FrontendOutput output = run_layout_pipeline(R"(record Zeta {
  a: u32
}

namespace alpha {
  record Alpha {
    b: u32
  }
}
)");

    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    ASSERT_EQ(output.layout_model.records.size(), 2U);
    ASSERT_EQ(output.layout_model.records[0].fqn, "Zeta");
    ASSERT_EQ(output.layout_model.records[1].fqn, "alpha.Alpha");
    EXPECT_EQ(output.layout_model.records[0].record_id, 1U);
    EXPECT_EQ(output.layout_model.records[1].record_id, 2U);
}

TEST(LayoutSmokeTest, NestedNamespaceRecordsAreIndependent) {
    const FrontendOutput output = run_layout_pipeline(R"(namespace breadcrumbs.geo {
  record Location {
    latitude: f64
    longitude: f64
  }

  namespace vehicle {
    record Route {
      distance: u32
    }
  }
}
)");

    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    const auto* location = find_record(output.layout_model, "breadcrumbs.geo.Location");
    const auto* route = find_record(output.layout_model, "breadcrumbs.geo.vehicle.Route");
    ASSERT_NE(location, nullptr);
    ASSERT_NE(route, nullptr);
    ASSERT_EQ(location->fields.size(), 2U);
    ASSERT_EQ(route->fields.size(), 1U);
    EXPECT_EQ(location->fields[0].field_index, 0U);
    EXPECT_EQ(location->fields[1].field_index, 1U);
    EXPECT_EQ(route->fields[0].field_index, 0U);
}

TEST(LayoutSmokeTest, RepeatComputationIsDeterministic) {
    const std::string source = R"(namespace alpha {
  record First {
    a: u32
  }
}

record Second {
  b: u32
}
)";

    const FrontendOutput first = run_layout_pipeline(source);
    const FrontendOutput second = run_layout_pipeline(source);

    ASSERT_TRUE(first.layout_diagnostics.empty()) << diagnostics_summary(first.layout_diagnostics);
    ASSERT_TRUE(second.layout_diagnostics.empty())
        << diagnostics_summary(second.layout_diagnostics);
    ASSERT_EQ(first.layout_model.records.size(), second.layout_model.records.size());
    for (std::size_t index = 0; index < first.layout_model.records.size(); ++index) {
        EXPECT_EQ(first.layout_model.records[index].fqn, second.layout_model.records[index].fqn);
        EXPECT_EQ(first.layout_model.records[index].record_id,
                  second.layout_model.records[index].record_id);
        ASSERT_EQ(first.layout_model.records[index].fields.size(),
                  second.layout_model.records[index].fields.size());
        for (std::size_t field_index = 0;
             field_index < first.layout_model.records[index].fields.size(); ++field_index) {
            EXPECT_EQ(first.layout_model.records[index].fields[field_index].field_index,
                      second.layout_model.records[index].fields[field_index].field_index);
        }
    }
}

TEST(LayoutSmokeTest, Exactly256FieldsSucceeds) {
    const FrontendOutput output = run_layout_pipeline(record_fields_source(256));

    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->fields.size(), 256U);
    EXPECT_EQ(layout->fields.front().field_index, 0U);
    EXPECT_EQ(layout->fields.back().field_index, 255U);
}

TEST(LayoutSmokeTest, TooManyFieldsFailsClearly) {
    const FrontendOutput output = run_layout_pipeline(record_fields_source(257));

    ASSERT_FALSE(output.layout_diagnostics.empty());
    EXPECT_TRUE(output.layout_model.records.empty());
    EXPECT_NE(output.layout_diagnostics.diagnostics().front().message().find("more than 256"),
              std::string::npos);
}

} // namespace
