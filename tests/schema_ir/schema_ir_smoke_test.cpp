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
using breadcrumbs::compiler::layout::FieldLayout;
using breadcrumbs::compiler::layout::LayoutComputer;
using breadcrumbs::compiler::layout::LayoutModel;
using breadcrumbs::compiler::parser::Parser;
using breadcrumbs::compiler::schema_ir::SchemaIrBuilder;
using breadcrumbs::compiler::schema_ir::SchemaIrModel;
using breadcrumbs::compiler::semantic::SemanticArrayType;
using breadcrumbs::compiler::semantic::SemanticBytesType;
using breadcrumbs::compiler::semantic::SemanticEnumReferenceType;
using breadcrumbs::compiler::semantic::SemanticField;
using breadcrumbs::compiler::semantic::SemanticModel;
using breadcrumbs::compiler::semantic::SemanticPrimitiveType;
using breadcrumbs::compiler::semantic::SemanticRecord;
using breadcrumbs::compiler::semantic::SemanticRecordReferenceType;
using breadcrumbs::compiler::semantic::SemanticStringType;
using breadcrumbs::compiler::semantic::SemanticType;
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

[[nodiscard]] const SemanticRecord* find_semantic_record(const SemanticModel& model,
                                                         std::string_view fqn) {
    return model.find_record(fqn);
}

[[nodiscard]] const SemanticField* find_semantic_field(const SemanticRecord& record,
                                                       std::string_view name) {
    return record.find_field(name);
}

[[nodiscard]] SemanticType make_array_type(SemanticType element_type) {
    SemanticArrayType array;
    array.element_type = std::make_unique<SemanticType>(std::move(element_type));
    return SemanticType(std::move(array));
}

[[nodiscard]] SchemaIrModel lower_schema_ir(FrontendOutput& output,
                                            DiagnosticEngine& lowering_diagnostics) {
    SchemaIrBuilder schema_ir_builder;
    return schema_ir_builder.build(output.ast, output.semantic_model, output.layout_model,
                                   *output.symbol_table, output.context, lowering_diagnostics);
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

TEST(SchemaIrSmokeTest, PrimitiveAliasesProduceTheSameSchemaIrPrimitiveKind) {
    const FrontendOutput canonical = run_clean_frontend(R"(record Example {
  value: u32
}
)");
    const FrontendOutput alias = run_clean_frontend(R"(record Example {
  value: uint32
}
)");

    ASSERT_TRUE(canonical.parser_diagnostics.empty());
    ASSERT_TRUE(alias.parser_diagnostics.empty());
    ASSERT_TRUE(canonical.symbol_diagnostics.empty());
    ASSERT_TRUE(alias.symbol_diagnostics.empty());
    ASSERT_TRUE(canonical.semantic_diagnostics.empty());
    ASSERT_TRUE(alias.semantic_diagnostics.empty());
    ASSERT_TRUE(canonical.layout_diagnostics.empty());
    ASSERT_TRUE(alias.layout_diagnostics.empty());
    ASSERT_TRUE(canonical.lowering_diagnostics.empty());
    ASSERT_TRUE(alias.lowering_diagnostics.empty());

    ASSERT_EQ(canonical.schema_ir.root_namespace().records_size(), 1);
    ASSERT_EQ(alias.schema_ir.root_namespace().records_size(), 1);
    EXPECT_TRUE(canonical.schema_ir.root_namespace().records(0).fields(0).type().has_primitive());
    EXPECT_TRUE(alias.schema_ir.root_namespace().records(0).fields(0).type().has_primitive());
    EXPECT_EQ(canonical.schema_ir.root_namespace().records(0).fields(0).type().primitive(),
              ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);
    EXPECT_EQ(alias.schema_ir.root_namespace().records(0).fields(0).type().primitive(),
              ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);
}

TEST(SchemaIrSmokeTest, LowersFieldTypesFromTheSemanticModel) {
    FrontendOutput output = run_clean_frontend(R"(record Location {
}

record Route {
  destination: int32
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);

    SemanticRecord* route =
        const_cast<SemanticRecord*>(find_semantic_record(output.semantic_model, "Route"));
    ASSERT_NE(route, nullptr);
    SemanticField* destination =
        const_cast<SemanticField*>(find_semantic_field(*route, "destination"));
    ASSERT_NE(destination, nullptr);
    destination->type =
        SemanticType(SemanticRecordReferenceType{.canonical_target_fqn = "Location"});

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = lower_schema_ir(output, lowering_diagnostics);

    ASSERT_TRUE(lowering_diagnostics.empty()) << diagnostics_summary(lowering_diagnostics);
    ASSERT_TRUE(schema_ir.has_root_namespace());
    ASSERT_EQ(schema_ir.root_namespace().records_size(), 2);
    const auto* lowered_route = find_record(schema_ir.root_namespace(), "Route");
    const auto* lowered_location = find_record(schema_ir.root_namespace(), "Location");
    ASSERT_NE(lowered_route, nullptr);
    ASSERT_NE(lowered_location, nullptr);
    ASSERT_EQ(lowered_route->fields_size(), 1);
    EXPECT_TRUE(lowered_route->fields(0).type().has_record());
    EXPECT_EQ(lowered_route->fields(0).type().record().target_record_ir_id(),
              lowered_location->ir_id());
}

TEST(SchemaIrSmokeTest, LowersArrayTypeSyntax) {
    FrontendOutput output = run_frontend(R"(record Route {
  samples: bytes[16]
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_EQ(output.layout_model.records.size(), 1U);
    ASSERT_EQ(output.layout_model.records[0].fqn, "Route");

    const auto& route_ast = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
        output.ast.declarations[0]->value);
    SemanticRecord* route =
        const_cast<SemanticRecord*>(find_semantic_record(output.semantic_model, "Route"));
    if (route == nullptr) {
        output.semantic_model.records.push_back(SemanticRecord{
            .source_range = route_ast.source_range,
            .fqn = "Route",
            .fields =
                {
                    SemanticField{
                        .source_range = route_ast.fields[0].source_range,
                        .name = "samples",
                        .type = make_array_type(SemanticType(SemanticBytesType{})),
                    },
                },
        });
        route = &output.semantic_model.records.back();
    } else {
        route->fields.clear();
        route->fields.push_back(SemanticField{
            .source_range = route_ast.fields[0].source_range,
            .name = "samples",
            .type = make_array_type(SemanticType(SemanticBytesType{})),
        });
    }

    output.layout_model.records[0].fields.clear();
    output.layout_model.records[0].fields.push_back(FieldLayout{.field_index = 0U});

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel lowered = lower_schema_ir(output, lowering_diagnostics);

    ASSERT_TRUE(lowering_diagnostics.empty()) << diagnostics_summary(lowering_diagnostics);

    const auto& root = lowered.root_namespace();
    ASSERT_EQ(root.records_size(), 1);
    const auto& record = root.records(0);
    ASSERT_EQ(record.fields_size(), 1);
    EXPECT_TRUE(record.fields(0).type().has_array());
    EXPECT_EQ(record.fields(0).type().array().count(), 16U);
    EXPECT_TRUE(record.fields(0).type().array().element_type().has_bytes());
    EXPECT_EQ(record.fields(0).field_index(), 0U);
}

TEST(SchemaIrSmokeTest, LowersRecursiveSemanticArraysAndPreservesCounts) {
    FrontendOutput output = run_clean_frontend(R"(record Route {
  samples: bool
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);

    SemanticRecord* route =
        const_cast<SemanticRecord*>(find_semantic_record(output.semantic_model, "Route"));
    ASSERT_NE(route, nullptr);
    SemanticField* samples = const_cast<SemanticField*>(find_semantic_field(*route, "samples"));
    ASSERT_NE(samples, nullptr);
    samples->type = make_array_type(SemanticType(SemanticBytesType{}));

    auto& route_ast = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
        output.ast.declarations[0]->value);
    route_ast.fields[0].type = breadcrumbs::compiler::ast::ArrayTypeSyntax{
        .source_range = route_ast.fields[0].source_range,
        .element_type =
            breadcrumbs::compiler::ast::TypeReferenceSyntax{
                .source_range = route_ast.fields[0].source_range,
                .name =
                    breadcrumbs::compiler::ast::QualifiedNameSyntax{
                        .source_range = route_ast.fields[0].source_range,
                        .parts =
                            {
                                breadcrumbs::compiler::ast::IdentifierSyntax{
                                    .source_range = route_ast.fields[0].source_range,
                                    .text = "bytes",
                                },
                            },
                    },
            },
        .fixed_size = 16U,
    };

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = lower_schema_ir(output, lowering_diagnostics);

    ASSERT_TRUE(lowering_diagnostics.empty()) << diagnostics_summary(lowering_diagnostics);
    const auto* lowered_route = find_record(schema_ir.root_namespace(), "Route");
    ASSERT_NE(lowered_route, nullptr);
    ASSERT_EQ(lowered_route->fields_size(), 1);
    EXPECT_TRUE(lowered_route->fields(0).type().has_array());
    EXPECT_TRUE(lowered_route->fields(0).type().array().element_type().has_bytes());
    EXPECT_EQ(lowered_route->fields(0).type().array().count(), 16U);
}

TEST(SchemaIrSmokeTest, LowersMultipleSemanticFieldsAfterRepairingTheModel) {
    FrontendOutput output = run_frontend(R"(record Known {
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

    const auto& example_ast = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
        output.ast.declarations[1]->value);

    SemanticRecord* example =
        const_cast<SemanticRecord*>(find_semantic_record(output.semantic_model, "Example"));
    ASSERT_NE(example, nullptr);
    example->fields.clear();
    example->fields.push_back(SemanticField{
        .source_range = example_ast.fields[0].source_range,
        .name = "good",
        .type = SemanticType(SemanticPrimitiveType::Bool),
    });
    example->fields.push_back(SemanticField{
        .source_range = example_ast.fields[1].source_range,
        .name = "bad",
        .type = SemanticType(SemanticRecordReferenceType{.canonical_target_fqn = "Known"}),
    });
    example->fields.push_back(SemanticField{
        .source_range = example_ast.fields[2].source_range,
        .name = "also_good",
        .type = SemanticType(SemanticPrimitiveType::U32),
    });

    ASSERT_EQ(output.layout_model.records.size(), 2U);
    breadcrumbs::compiler::layout::RecordLayout* layout_example = nullptr;
    for (auto& candidate : output.layout_model.records) {
        if (candidate.fqn == "Example") {
            layout_example = &candidate;
            break;
        }
    }
    ASSERT_NE(layout_example, nullptr);
    layout_example->fields.clear();
    layout_example->fields.push_back(FieldLayout{.field_index = 0U});
    layout_example->fields.push_back(FieldLayout{.field_index = 1U});
    layout_example->fields.push_back(FieldLayout{.field_index = 2U});

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel lowered = lower_schema_ir(output, lowering_diagnostics);

    ASSERT_TRUE(lowering_diagnostics.empty()) << diagnostics_summary(lowering_diagnostics);
    const auto& root = lowered.root_namespace();
    ASSERT_EQ(root.records_size(), 2);
    const auto& known = root.records(0);
    const auto* lowered_example = find_record(root, "Example");
    ASSERT_NE(lowered_example, nullptr);
    ASSERT_EQ(known.name(), "Known");
    ASSERT_EQ(lowered_example->name(), "Example");
    ASSERT_EQ(lowered_example->fields_size(), 3);
    EXPECT_EQ(lowered_example->fields(0).field_index(), 0U);
    EXPECT_TRUE(lowered_example->fields(0).type().has_primitive());
    EXPECT_EQ(lowered_example->fields(1).field_index(), 1U);
    EXPECT_TRUE(lowered_example->fields(1).type().has_record());
    EXPECT_EQ(lowered_example->fields(1).type().record().target_record_ir_id(), known.ir_id());
    EXPECT_EQ(lowered_example->fields(2).field_index(), 2U);
    EXPECT_TRUE(lowered_example->fields(2).type().has_primitive());
    EXPECT_EQ(lowered_example->fields(2).type().primitive(),
              ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);
}

TEST(SchemaIrSmokeTest, FailsAtomicallyWhenSemanticFieldOrderDiffersFromAstOrder) {
    FrontendOutput output = run_clean_frontend(R"(record Example {
  first: bool
  second: u32
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);

    SemanticRecord* record =
        const_cast<SemanticRecord*>(find_semantic_record(output.semantic_model, "Example"));
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->fields.size(), 2U);
    std::swap(record->fields[0], record->fields[1]);

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = lower_schema_ir(output, lowering_diagnostics);

    ASSERT_FALSE(lowering_diagnostics.empty());
    EXPECT_FALSE(schema_ir.has_root_namespace());
    EXPECT_EQ(lowering_diagnostics.diagnostics()[0].severity(),
              breadcrumbs::compiler::diagnostics::Severity::InternalCompilerError);
}

TEST(SchemaIrSmokeTest, FailsAtomicallyWhenSemanticFieldIsMissing) {
    FrontendOutput output = run_clean_frontend(R"(record Example {
  first: bool
  second: u32
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);

    SemanticRecord* record =
        const_cast<SemanticRecord*>(find_semantic_record(output.semantic_model, "Example"));
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->fields.size(), 2U);
    record->fields.pop_back();

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = lower_schema_ir(output, lowering_diagnostics);

    ASSERT_FALSE(lowering_diagnostics.empty());
    EXPECT_FALSE(schema_ir.has_root_namespace());
    EXPECT_EQ(lowering_diagnostics.diagnostics()[0].severity(),
              breadcrumbs::compiler::diagnostics::Severity::InternalCompilerError);
}

TEST(SchemaIrSmokeTest, FailsAtomicallyWhenSemanticTypeIsInvalid) {
    FrontendOutput output = run_clean_frontend(R"(record Example {
  value: bool
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);

    SemanticRecord* record =
        const_cast<SemanticRecord*>(find_semantic_record(output.semantic_model, "Example"));
    ASSERT_NE(record, nullptr);
    SemanticField* field = const_cast<SemanticField*>(find_semantic_field(*record, "value"));
    ASSERT_NE(field, nullptr);
    field->type = SemanticType();

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = lower_schema_ir(output, lowering_diagnostics);

    ASSERT_FALSE(lowering_diagnostics.empty());
    EXPECT_FALSE(schema_ir.has_root_namespace());
    EXPECT_EQ(lowering_diagnostics.diagnostics()[0].severity(),
              breadcrumbs::compiler::diagnostics::Severity::InternalCompilerError);
}

TEST(SchemaIrSmokeTest, FailsAtomicallyWhenRecursiveSemanticArrayElementIsNull) {
    FrontendOutput output = run_clean_frontend(R"(record Route {
  samples: bool
}
)");

    ASSERT_TRUE(output.parser_diagnostics.empty())
        << diagnostics_summary(output.parser_diagnostics);
    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);

    SemanticRecord* record =
        const_cast<SemanticRecord*>(find_semantic_record(output.semantic_model, "Route"));
    ASSERT_NE(record, nullptr);
    SemanticField* field = const_cast<SemanticField*>(find_semantic_field(*record, "samples"));
    ASSERT_NE(field, nullptr);
    SemanticArrayType array;
    field->type = SemanticType(std::move(array));

    auto& route_ast = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
        output.ast.declarations[0]->value);
    route_ast.fields[0].type = breadcrumbs::compiler::ast::ArrayTypeSyntax{
        .source_range = route_ast.fields[0].source_range,
        .element_type =
            breadcrumbs::compiler::ast::TypeReferenceSyntax{
                .source_range = route_ast.fields[0].source_range,
                .name =
                    breadcrumbs::compiler::ast::QualifiedNameSyntax{
                        .source_range = route_ast.fields[0].source_range,
                        .parts =
                            {
                                breadcrumbs::compiler::ast::IdentifierSyntax{
                                    .source_range = route_ast.fields[0].source_range,
                                    .text = "bytes",
                                },
                            },
                    },
            },
        .fixed_size = 16U,
    };

    DiagnosticEngine lowering_diagnostics;
    const SchemaIrModel schema_ir = lower_schema_ir(output, lowering_diagnostics);

    ASSERT_FALSE(lowering_diagnostics.empty());
    EXPECT_FALSE(schema_ir.has_root_namespace());
    EXPECT_EQ(lowering_diagnostics.diagnostics()[0].severity(),
              breadcrumbs::compiler::diagnostics::Severity::InternalCompilerError);
}

} // namespace
