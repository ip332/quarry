#include "compiler/layout/layout.hpp"
#include "compiler/semantic/semantic.hpp"

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::diagnostics::DiagnosticCollection;
using breadcrumbs::compiler::layout::LayoutComputer;
using breadcrumbs::compiler::layout::LayoutModel;
using breadcrumbs::compiler::semantic::SemanticField;
using breadcrumbs::compiler::semantic::SemanticModel;
using breadcrumbs::compiler::semantic::SemanticPrimitiveType;
using breadcrumbs::compiler::semantic::SemanticRecord;
using breadcrumbs::compiler::semantic::SemanticType;

struct LayoutOutput {
    breadcrumbs::compiler::context::CompilerContext context;
    DiagnosticCollection layout_diagnostics;
    LayoutModel layout_model;
};

[[nodiscard]] LayoutOutput run_layout_pipeline(const SemanticModel& semantic_model) {
    LayoutOutput output;
    LayoutComputer computer;
    output.layout_model =
        computer.compute(semantic_model, output.context, output.layout_diagnostics);
    return output;
}

[[nodiscard]] std::string diagnostics_summary(const DiagnosticCollection& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.id().str() << ": " << diagnostic.message() << '\n';
    }
    return stream.str();
}

[[nodiscard]] SemanticField make_field(std::string name) {
    return SemanticField{
        .source_range = {},
        .name = std::move(name),
        .type = SemanticType(SemanticPrimitiveType::U32),
    };
}

[[nodiscard]] SemanticRecord make_record(std::string fqn, std::size_t field_count = 0U) {
    SemanticRecord record;
    record.fqn = std::move(fqn);
    record.source_range = {};
    record.fields.reserve(field_count);
    for (std::size_t index = 0; index < field_count; ++index) {
        record.fields.push_back(make_field("field" + std::to_string(index)));
    }
    return record;
}

[[nodiscard]] SemanticModel make_model(std::vector<SemanticRecord> records) {
    SemanticModel model;
    model.records = std::move(records);
    return model;
}

[[nodiscard]] const breadcrumbs::compiler::layout::RecordLayout*
find_record(const LayoutModel& model, std::string_view fqn) {
    return model.find_record(fqn);
}

TEST(LayoutSmokeTest, EmptySemanticModelProducesNoLayouts) {
    const LayoutOutput output = run_layout_pipeline(SemanticModel{});

    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    EXPECT_TRUE(output.layout_model.records.empty());
}

TEST(LayoutSmokeTest, OneEmptyRecordGetsInitialIdentity) {
    const LayoutOutput output = run_layout_pipeline(make_model({make_record("Example")}));

    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    ASSERT_EQ(output.layout_model.records.size(), 1U);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->record_id, 1U);
    EXPECT_TRUE(layout->fields.empty());
}

TEST(LayoutSmokeTest, MultipleFieldsFollowDeclarationOrder) {
    const LayoutOutput output =
        run_layout_pipeline(make_model({make_record("Example", 3U)}));

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
    const LayoutOutput output = run_layout_pipeline(
        make_model({make_record("Zeta", 1U), make_record("alpha.Alpha", 1U)}));

    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    ASSERT_EQ(output.layout_model.records.size(), 2U);
    ASSERT_NE(find_record(output.layout_model, "Zeta"), nullptr);
    ASSERT_NE(find_record(output.layout_model, "alpha.Alpha"), nullptr);
    EXPECT_EQ(output.layout_model.records[0].fqn, "Zeta");
    EXPECT_EQ(output.layout_model.records[1].fqn, "alpha.Alpha");
    EXPECT_EQ(output.layout_model.records[0].record_id, 1U);
    EXPECT_EQ(output.layout_model.records[1].record_id, 2U);
}

TEST(LayoutSmokeTest, NestedNamespaceRecordsAreIndependent) {
    const LayoutOutput output = run_layout_pipeline(make_model(
        {make_record("breadcrumbs.geo.Location", 2U), make_record("breadcrumbs.geo.vehicle.Route", 1U)}));

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
    const std::vector<SemanticRecord> records = {
        make_record("alpha.First", 1U),
        make_record("Second", 1U),
    };

    const LayoutOutput first = run_layout_pipeline(make_model(records));
    const LayoutOutput second = run_layout_pipeline(make_model(records));

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
    const LayoutOutput output = run_layout_pipeline(make_model({make_record("Example", 256U)}));

    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->fields.size(), 256U);
    EXPECT_EQ(layout->fields.front().field_index, 0U);
    EXPECT_EQ(layout->fields.back().field_index, 255U);
}

TEST(LayoutSmokeTest, TooManyFieldsFailsClearly) {
    const LayoutOutput output = run_layout_pipeline(make_model({make_record("Example", 257U)}));

    ASSERT_FALSE(output.layout_diagnostics.empty());
    EXPECT_TRUE(output.layout_model.records.empty());
    EXPECT_NE(output.layout_diagnostics.diagnostics().front().message().find("more than 256"),
              std::string::npos);
}

} // namespace
