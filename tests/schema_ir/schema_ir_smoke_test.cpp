#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/symbols/symbols.hpp"

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

struct FrontendOutput {
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

[[nodiscard]] FrontendOutput run_frontend(std::string text) {
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

    LayoutComputer layout_computer;
    output.layout_model =
        layout_computer.compute(output.semantic_model, output.context, output.layout_diagnostics);

    SchemaIrBuilder schema_ir_builder;
    if (output.layout_diagnostics.empty()) {
        output.schema_ir = schema_ir_builder.build(output.ast, output.semantic_model,
                                                   output.layout_model, *output.symbol_table,
                                                   output.context, output.lowering_diagnostics);
    }
    return output;
}

[[nodiscard]] FrontendOutput run_clean_frontend(std::string text) {
    return run_frontend(std::move(text));
}

[[nodiscard]] bool clean(const FrontendOutput& output) {
    return output.parser_diagnostics.empty() && output.symbol_diagnostics.empty() &&
           output.semantic_diagnostics.empty() && output.layout_diagnostics.empty() &&
           output.lowering_diagnostics.empty();
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

TEST(SchemaIrSmokeTest, BuildsSyntheticRootForEmptyInput) {
    const FrontendOutput output = run_clean_frontend("");

    ASSERT_TRUE(clean(output)) << diagnostics_summary(output.lowering_diagnostics);
    EXPECT_EQ(output.schema_ir.schema_ir_version(), 1U);
    EXPECT_TRUE(output.schema_ir.compiler_version().empty());

    const auto& root = output.schema_ir.root_namespace();
    EXPECT_TRUE(root.name().empty());
    EXPECT_TRUE(root.fqn().empty());
    EXPECT_TRUE(root.namespaces().empty());
    EXPECT_TRUE(root.records().empty());
    EXPECT_TRUE(root.enums().empty());
}

TEST(SchemaIrSmokeTest, LowersMultipleTopLevelNamespacesAsSiblings) {
    const FrontendOutput output = run_clean_frontend(R"(namespace alpha.one {
  record First {
  }
}

namespace beta.two {
  record Second {
  }
}
)");

    ASSERT_TRUE(clean(output)) << diagnostics_summary(output.lowering_diagnostics);

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

TEST(SchemaIrSmokeTest, LowersSingleRecordWithBuiltinFieldsAndSourceMetadata) {
    const FrontendOutput output = run_clean_frontend(R"(
record Example {
  active: bool
  count: u32
  label: string
  blob: bytes
}
)");

    ASSERT_TRUE(clean(output)) << diagnostics_summary(output.lowering_diagnostics);

    const auto& root = output.schema_ir.root_namespace();
    ASSERT_EQ(root.records_size(), 1);
    const auto& record = root.records(0);
    EXPECT_EQ(record.name(), "Example");
    EXPECT_EQ(record.ir_id(), 2U);
    EXPECT_EQ(record.record_id(), 1U);
    ASSERT_EQ(output.layout_model.records.size(), 1U);
    EXPECT_EQ(record.record_id(), output.layout_model.records[0].record_id);
    EXPECT_NE(record.ir_id(), record.record_id());
    EXPECT_EQ(record.source_origin().file(), "/test/schema.brd");
    EXPECT_EQ(record.source_origin().span().start_line(), 2U);
    EXPECT_EQ(record.source_origin().span().start_column(), 1U);
    ASSERT_EQ(record.fields_size(), 4);

    EXPECT_TRUE(record.fields(0).type().has_primitive());
    EXPECT_EQ(record.fields(0).type().primitive(), ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL);
    EXPECT_EQ(record.fields(0).field_index(), 0U);
    EXPECT_TRUE(record.fields(1).type().has_primitive());
    EXPECT_EQ(record.fields(1).type().primitive(), ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);
    EXPECT_EQ(record.fields(1).field_index(), 1U);
    EXPECT_TRUE(record.fields(2).type().has_string());
    EXPECT_EQ(record.fields(2).type().string().max_bytes(), 0U);
    EXPECT_EQ(record.fields(2).field_index(), 2U);
    EXPECT_TRUE(record.fields(3).type().has_bytes());
    EXPECT_EQ(record.fields(3).type().bytes().max_bytes(), 0U);
    EXPECT_EQ(record.fields(3).field_index(), 3U);
}

TEST(SchemaIrSmokeTest, LowersEnumsWithExplicitValues) {
    const FrontendOutput output = run_clean_frontend(R"(enum FixType {
  none = 0
  two_d = 1
  three_d = 2
}
)");

    ASSERT_TRUE(clean(output)) << diagnostics_summary(output.lowering_diagnostics);

    const auto& root = output.schema_ir.root_namespace();
    ASSERT_EQ(root.enums_size(), 1);
    const auto& enum_ir = root.enums(0);
    EXPECT_EQ(enum_ir.name(), "FixType");
    ASSERT_EQ(enum_ir.values_size(), 3);
    EXPECT_EQ(enum_ir.values(0).name(), "none");
    EXPECT_EQ(enum_ir.values(0).value(), 0);
    EXPECT_EQ(enum_ir.values(1).name(), "two_d");
    EXPECT_EQ(enum_ir.values(1).value(), 1);
    EXPECT_EQ(enum_ir.values(2).name(), "three_d");
    EXPECT_EQ(enum_ir.values(2).value(), 2);
}

TEST(SchemaIrSmokeTest, LowersNamedAndQualifiedRecordReferencesLexically) {
    const FrontendOutput output = run_clean_frontend(R"(namespace breadcrumbs.geo {
  record Location {
  }

  record Route {
    origin: Location
  }
}

namespace breadcrumbs.vehicle {
  record Location {
  }

  record Journey {
    local_destination: Location
    global_destination: breadcrumbs.geo.Location
  }
}
)");

    ASSERT_TRUE(clean(output)) << diagnostics_summary(output.lowering_diagnostics);

    const auto& root = output.schema_ir.root_namespace();
    const auto* breadcrumbs = find_namespace(root, "breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    ASSERT_EQ(breadcrumbs->namespaces_size(), 2);

    const auto* geo = find_namespace(*breadcrumbs, "geo");
    const auto* vehicle = find_namespace(*breadcrumbs, "vehicle");
    ASSERT_NE(geo, nullptr);
    ASSERT_NE(vehicle, nullptr);

    const auto* geo_location = find_record(*geo, "Location");
    const auto* route = find_record(*geo, "Route");
    const auto* vehicle_location = find_record(*vehicle, "Location");
    const auto* journey = find_record(*vehicle, "Journey");
    ASSERT_NE(geo_location, nullptr);
    ASSERT_NE(route, nullptr);
    ASSERT_NE(vehicle_location, nullptr);
    ASSERT_NE(journey, nullptr);

    ASSERT_EQ(route->fields_size(), 1);
    ASSERT_EQ(journey->fields_size(), 2);
    EXPECT_EQ(route->fields(0).field_index(), 0U);
    EXPECT_EQ(route->fields(0).type().record().target_record_ir_id(), geo_location->ir_id());
    EXPECT_EQ(journey->fields(0).field_index(), 0U);
    EXPECT_EQ(journey->fields(0).type().record().target_record_ir_id(), vehicle_location->ir_id());
    EXPECT_EQ(journey->fields(1).field_index(), 1U);
    EXPECT_EQ(journey->fields(1).type().record().target_record_ir_id(), geo_location->ir_id());
}

TEST(SchemaIrSmokeTest, LowersArrayTypeSyntax) {
    const FrontendOutput output = run_clean_frontend(R"(record Route {
  samples: bytes[16]
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_model.records.size() == 1U);
    ASSERT_EQ(output.layout_model.records.size(), 1U);
    ASSERT_EQ(output.layout_model.records[0].fqn, "Route");
    ASSERT_TRUE(output.lowering_diagnostics.empty())
        << diagnostics_summary(output.lowering_diagnostics);

    const auto& root = output.schema_ir.root_namespace();
    ASSERT_EQ(root.records_size(), 1);
    const auto& record = root.records(0);
    ASSERT_EQ(record.fields_size(), 1);
    EXPECT_TRUE(record.fields(0).type().has_array());
    EXPECT_EQ(record.fields(0).type().array().count(), 16U);
    EXPECT_TRUE(record.fields(0).type().array().element_type().has_bytes());
    EXPECT_EQ(record.fields(0).field_index(), 0U);
}

TEST(SchemaIrSmokeTest, ContinuesAfterInvalidFieldReferenceInTheSameCompilation) {
    const FrontendOutput output = run_clean_frontend(R"(record Known {
}

record Example {
  good: bool
  bad: MissingType
  also_good: u32
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_EQ(output.layout_model.records.size(), 2U);
    ASSERT_EQ(output.lowering_diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.lowering_diagnostics.diagnostics()[0].severity(),
              breadcrumbs::compiler::diagnostics::Severity::InternalCompilerError);

    const auto& root = output.schema_ir.root_namespace();
    ASSERT_EQ(root.records_size(), 2);
    const auto& example = root.records(1);
    ASSERT_EQ(example.fields_size(), 3);
    EXPECT_EQ(example.fields(0).field_index(), 0U);
    EXPECT_TRUE(example.fields(0).type().has_primitive());
    EXPECT_EQ(example.fields(1).field_index(), 1U);
    EXPECT_EQ(example.fields(2).field_index(), 2U);
    EXPECT_TRUE(example.fields(2).type().has_primitive());
    EXPECT_EQ(example.fields(2).type().primitive(), ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);
}

} // namespace
