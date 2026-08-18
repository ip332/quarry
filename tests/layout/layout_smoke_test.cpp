#include "compiler/layout/layout.hpp"
#include "compiler/semantic/semantic.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::compiler::layout::LayoutComputer;
using quarry::compiler::layout::LayoutModel;
using quarry::compiler::layout::DescriptorKind;
using quarry::compiler::layout::FieldStorage;
using quarry::compiler::layout::LayoutTypeKind;
using quarry::compiler::layout::RecordClassification;
using quarry::compiler::semantic::SemanticField;
using quarry::compiler::semantic::SemanticModel;
using quarry::compiler::semantic::SemanticPrimitiveType;
using quarry::compiler::semantic::SemanticRecord;
using quarry::compiler::semantic::SemanticType;
using quarry::schema_ir::EnumIR;
using quarry::schema_ir::FieldIR;
using quarry::schema_ir::FieldType;
using quarry::schema_ir::NamespaceIR;
using quarry::schema_ir::PrimitiveType;
using quarry::schema_ir::RecordIR;
using quarry::schema_ir::SchemaIR;

struct LayoutOutput {
    quarry::compiler::context::CompilerContext context;
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

[[nodiscard]] const quarry::compiler::layout::RecordLayout*
find_record(const LayoutModel& model, std::string_view fqn) {
    return model.find_record(fqn);
}

struct BrfV2LayoutOutput {
    DiagnosticCollection diagnostics;
    LayoutModel layout_model;
};

[[nodiscard]] BrfV2LayoutOutput run_brf_v2_layout(const SchemaIR& schema_ir) {
    BrfV2LayoutOutput output;
    LayoutComputer computer;
    output.layout_model = computer.compute(schema_ir, output.diagnostics);
    return output;
}

[[nodiscard]] SchemaIR make_schema() {
    SchemaIR schema;
    schema.set_schema_ir_version(1U);
    schema.mutable_root_namespace()->set_ir_id(1U);
    return schema;
}

[[nodiscard]] RecordIR* add_schema_record(SchemaIR& schema, std::uint64_t ir_id,
                                           std::uint32_t record_id, std::string_view fqn) {
    NamespaceIR* root = schema.mutable_root_namespace();
    RecordIR* record = root->add_records();
    record->set_ir_id(ir_id);
    record->set_record_id(record_id);
    record->set_name(std::string(fqn));
    record->set_fqn(std::string(fqn));
    return record;
}

void add_primitive_field(RecordIR& record, std::string_view name, std::uint32_t field_index,
                         PrimitiveType primitive) {
    FieldIR* field = record.add_fields();
    field->set_name(std::string(name));
    field->set_field_index(field_index);
    field->mutable_type()->set_primitive(primitive);
}

void add_string_field(RecordIR& record, std::string_view name, std::uint32_t field_index,
                      std::uint32_t max_bytes) {
    FieldIR* field = record.add_fields();
    field->set_name(std::string(name));
    field->set_field_index(field_index);
    field->mutable_type()->mutable_string()->set_max_bytes(max_bytes);
}

void add_bytes_field(RecordIR& record, std::string_view name, std::uint32_t field_index,
                     std::uint32_t max_bytes) {
    FieldIR* field = record.add_fields();
    field->set_name(std::string(name));
    field->set_field_index(field_index);
    field->mutable_type()->mutable_bytes()->set_max_bytes(max_bytes);
}

void add_record_field(RecordIR& record, std::string_view name, std::uint32_t field_index,
                      std::uint64_t target_ir_id) {
    FieldIR* field = record.add_fields();
    field->set_name(std::string(name));
    field->set_field_index(field_index);
    field->mutable_type()->mutable_record()->set_target_record_ir_id(target_ir_id);
}

void add_array_field(RecordIR& record, std::string_view name, std::uint32_t field_index,
                     PrimitiveType element_type, std::uint32_t max_elements) {
    FieldIR* field = record.add_fields();
    field->set_name(std::string(name));
    field->set_field_index(field_index);
    auto* array = field->mutable_type()->mutable_array();
    array->set_max_elements(max_elements);
    array->mutable_element_type()->set_primitive(element_type);
}

void add_record_array_field(RecordIR& record, std::string_view name, std::uint32_t field_index,
                            std::uint64_t element_ir_id, std::uint32_t max_elements) {
    FieldIR* field = record.add_fields();
    field->set_name(std::string(name));
    field->set_field_index(field_index);
    auto* array = field->mutable_type()->mutable_array();
    array->set_max_elements(max_elements);
    array->mutable_element_type()->mutable_record()->set_target_record_ir_id(element_ir_id);
}

[[nodiscard]] EnumIR* add_schema_enum(SchemaIR& schema, std::uint64_t ir_id,
                                       std::string_view fqn,
                                       std::initializer_list<std::int64_t> values) {
    EnumIR* enumeration = schema.mutable_root_namespace()->add_enums();
    enumeration->set_ir_id(ir_id);
    enumeration->set_name(std::string(fqn));
    enumeration->set_fqn(std::string(fqn));
    std::size_t index = 0U;
    for (const std::int64_t value : values) {
        auto* enum_value = enumeration->add_values();
        enum_value->set_name("value" + std::to_string(index++));
        enum_value->set_value(value);
    }
    return enumeration;
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
        {make_record("quarry.geo.Location", 2U), make_record("quarry.geo.vehicle.Route", 1U)}));

    ASSERT_TRUE(output.layout_diagnostics.empty())
        << diagnostics_summary(output.layout_diagnostics);
    const auto* location = find_record(output.layout_model, "quarry.geo.Location");
    const auto* route = find_record(output.layout_model, "quarry.geo.vehicle.Route");
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

TEST(BrfV2LayoutTest, EmptyRecordUsesHeaderOnlyFixedRecord) {
    SchemaIR schema = make_schema();
    ASSERT_NE(add_schema_record(schema, 1U, 42U, "Example"), nullptr);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->header_size, 16U);
    EXPECT_EQ(layout->presence_bitmap_size, 0U);
    EXPECT_EQ(layout->fixed_region_size, 0U);
    ASSERT_TRUE(layout->complete_fixed_record_size.has_value());
    EXPECT_EQ(*layout->complete_fixed_record_size, 16U);
    EXPECT_EQ(layout->classification, RecordClassification::FixedSize);
}

TEST(BrfV2LayoutTest, FixedFieldsUsePackedAbsoluteOffsets) {
    SchemaIR schema = make_schema();
    RecordIR* record = add_schema_record(schema, 1U, 42U, "Example");
    add_primitive_field(*record, "a", 0U, PrimitiveType::PRIMITIVE_TYPE_BOOL);
    add_primitive_field(*record, "b", 1U, PrimitiveType::PRIMITIVE_TYPE_U16);
    add_primitive_field(*record, "c", 2U, PrimitiveType::PRIMITIVE_TYPE_F64);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->fields.size(), 3U);
    EXPECT_EQ(layout->fields[0].location.byte_offset, 17U);
    EXPECT_EQ(layout->fields[0].slot_size, 1U);
    EXPECT_EQ(layout->fields[0].location.bit_width, 8U);
    EXPECT_EQ(layout->fields[1].location.byte_offset, 18U);
    EXPECT_EQ(layout->fields[1].slot_size, 2U);
    EXPECT_EQ(layout->fields[2].location.byte_offset, 20U);
    EXPECT_EQ(layout->fields[2].slot_size, 8U);
    EXPECT_EQ(layout->fixed_region_size, 12U);
    EXPECT_EQ(*layout->complete_fixed_record_size, 28U);
}

TEST(BrfV2LayoutTest, EnumWidthUsesCanonicalUnsignedWidth) {
    SchemaIR schema = make_schema();
    ASSERT_NE(add_schema_enum(schema, 10U, "Status", {0, 1, 255}), nullptr);
    RecordIR* record = add_schema_record(schema, 1U, 42U, "Example");
    FieldIR* field = record->add_fields();
    field->set_name("status");
    field->set_field_index(0U);
    field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(10U);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->fields.size(), 1U);
    EXPECT_EQ(layout->fields[0].type.kind, LayoutTypeKind::Enum);
    EXPECT_EQ(layout->fields[0].type.encoded_width, 1U);
    EXPECT_EQ(layout->fields[0].location.byte_offset, 17U);
}

TEST(BrfV2LayoutTest, VariableFieldsReserveDescriptorsAndKeepLaterFieldsStable) {
    SchemaIR schema = make_schema();
    RecordIR* record = add_schema_record(schema, 1U, 42U, "Example");
    add_string_field(*record, "name", 0U, 64U);
    add_bytes_field(*record, "blob", 1U, 128U);
    add_primitive_field(*record, "state", 2U, PrimitiveType::PRIMITIVE_TYPE_U16);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->fields.size(), 3U);
    EXPECT_EQ(layout->classification, RecordClassification::VariableSize);
    EXPECT_FALSE(layout->complete_fixed_record_size.has_value());
    EXPECT_EQ(layout->fields[0].location.byte_offset, 17U);
    EXPECT_EQ(layout->fields[1].location.byte_offset, 25U);
    EXPECT_EQ(layout->fields[2].location.byte_offset, 33U);
    EXPECT_EQ(layout->fields[0].slot_size, 8U);
    EXPECT_EQ(layout->fields[1].descriptor_kind, DescriptorKind::DataOffsetByteLength);
    EXPECT_EQ(layout->fields[2].slot_size, 2U);
    EXPECT_EQ(layout->fixed_region_size, 19U);
}

TEST(BrfV2LayoutTest, PresenceBitsFollowDeclarationOrderAcrossBytes) {
    SchemaIR schema = make_schema();
    RecordIR* record = add_schema_record(schema, 1U, 42U, "Example");
    for (std::uint32_t index = 0U; index < 9U; ++index) {
        add_primitive_field(*record, "field" + std::to_string(index), index,
                            PrimitiveType::PRIMITIVE_TYPE_U8);
    }

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->presence_bitmap_size, 2U);
    ASSERT_EQ(layout->fields.size(), 9U);
    EXPECT_EQ(layout->fields[0].presence_bit_index, 0U);
    EXPECT_EQ(layout->fields[7].presence_bit_index, 7U);
    EXPECT_EQ(layout->fields[8].presence_bit_index, 8U);
    EXPECT_EQ(layout->fields[0].location.byte_offset, 18U);
    EXPECT_EQ(layout->fields[8].location.byte_offset, 26U);
}

TEST(BrfV2LayoutTest, VariableArrayUsesDescriptorAndRetainsElementMetadata) {
    SchemaIR schema = make_schema();
    RecordIR* record = add_schema_record(schema, 1U, 42U, "Example");
    add_array_field(*record, "samples", 0U, PrimitiveType::PRIMITIVE_TYPE_U16, 100U);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->fields.size(), 1U);
    const auto& field = layout->fields[0];
    EXPECT_EQ(field.storage, FieldStorage::VariableDescriptor);
    EXPECT_EQ(field.descriptor_kind, DescriptorKind::DataOffsetByteLength);
    EXPECT_EQ(field.slot_size, 8U);
    EXPECT_EQ(field.type.kind, LayoutTypeKind::Array);
    EXPECT_EQ(field.type.max_elements, 100U);
    ASSERT_NE(field.type.element_type, nullptr);
    EXPECT_EQ(field.type.element_type->kind, LayoutTypeKind::U16);
    EXPECT_EQ(field.type.element_type->encoded_width, 2U);
}

TEST(BrfV2LayoutTest, MixedExampleMatchesFixedRegionModel) {
    SchemaIR schema = make_schema();
    RecordIR* record = add_schema_record(schema, 1U, 42U, "Example");
    add_primitive_field(*record, "timestamp", 0U, PrimitiveType::PRIMITIVE_TYPE_U32);
    add_string_field(*record, "name", 1U, 64U);
    add_primitive_field(*record, "state", 2U, PrimitiveType::PRIMITIVE_TYPE_U16);
    add_array_field(*record, "samples", 3U, PrimitiveType::PRIMITIVE_TYPE_U16, 100U);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    const auto* layout = find_record(output.layout_model, "Example");
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->fields.size(), 4U);
    EXPECT_EQ(layout->presence_bitmap_size, 1U);
    EXPECT_EQ(layout->fields[0].location.byte_offset, 17U);
    EXPECT_EQ(layout->fields[1].location.byte_offset, 21U);
    EXPECT_EQ(layout->fields[2].location.byte_offset, 29U);
    EXPECT_EQ(layout->fields[3].location.byte_offset, 31U);
    EXPECT_EQ(layout->fixed_region_size, 23U);
    EXPECT_EQ(layout->header_size + layout->fixed_region_size, 39U);
}

TEST(BrfV2LayoutTest, FixedNestedRecordsAreInlineCompleteRecords) {
    SchemaIR schema = make_schema();
    RecordIR* child = add_schema_record(schema, 2U, 7U, "Child");
    add_primitive_field(*child, "value", 0U, PrimitiveType::PRIMITIVE_TYPE_U16);
    RecordIR* parent = add_schema_record(schema, 1U, 42U, "Parent");
    add_primitive_field(*parent, "prefix", 0U, PrimitiveType::PRIMITIVE_TYPE_U8);
    add_record_field(*parent, "child", 1U, 2U);
    add_primitive_field(*parent, "suffix", 2U, PrimitiveType::PRIMITIVE_TYPE_U32);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    const auto* child_layout = find_record(output.layout_model, "Child");
    const auto* parent_layout = find_record(output.layout_model, "Parent");
    ASSERT_NE(child_layout, nullptr);
    ASSERT_NE(parent_layout, nullptr);
    EXPECT_EQ(*child_layout->complete_fixed_record_size, 19U);
    ASSERT_EQ(parent_layout->fields.size(), 3U);
    EXPECT_EQ(parent_layout->fields[1].storage, FieldStorage::InlineFixedNestedRecord);
    EXPECT_EQ(parent_layout->fields[1].location.byte_offset, 18U);
    EXPECT_EQ(parent_layout->fields[1].slot_size, 19U);
    EXPECT_EQ(parent_layout->fields[2].location.byte_offset, 37U);
    EXPECT_EQ(parent_layout->fixed_region_size, 25U);
    EXPECT_EQ(*parent_layout->complete_fixed_record_size, 41U);
}

TEST(BrfV2LayoutTest, FixedNestedClassificationPropagatesRecursively) {
    SchemaIR schema = make_schema();
    RecordIR* grandchild = add_schema_record(schema, 3U, 3U, "Grandchild");
    add_primitive_field(*grandchild, "value", 0U, PrimitiveType::PRIMITIVE_TYPE_U8);
    RecordIR* child = add_schema_record(schema, 2U, 2U, "Child");
    add_record_field(*child, "grandchild", 0U, 3U);
    add_primitive_field(*child, "tail", 1U, PrimitiveType::PRIMITIVE_TYPE_U16);
    RecordIR* parent = add_schema_record(schema, 1U, 1U, "Parent");
    add_record_field(*parent, "child", 0U, 2U);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    EXPECT_EQ(find_record(output.layout_model, "Grandchild")->classification,
              RecordClassification::FixedSize);
    EXPECT_EQ(find_record(output.layout_model, "Child")->classification,
              RecordClassification::FixedSize);
    EXPECT_EQ(find_record(output.layout_model, "Parent")->classification,
              RecordClassification::FixedSize);
}

TEST(BrfV2LayoutTest, VariableNestedRecordsUseDescriptorsAndPropagateClassification) {
    SchemaIR schema = make_schema();
    RecordIR* child = add_schema_record(schema, 2U, 7U, "Child");
    add_string_field(*child, "text", 0U, 32U);
    RecordIR* parent = add_schema_record(schema, 1U, 42U, "Parent");
    add_record_field(*parent, "child", 0U, 2U);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    const auto* child_layout = find_record(output.layout_model, "Child");
    const auto* parent_layout = find_record(output.layout_model, "Parent");
    ASSERT_NE(child_layout, nullptr);
    ASSERT_NE(parent_layout, nullptr);
    EXPECT_EQ(child_layout->classification, RecordClassification::VariableSize);
    EXPECT_EQ(parent_layout->classification, RecordClassification::VariableSize);
    ASSERT_EQ(parent_layout->fields.size(), 1U);
    EXPECT_EQ(parent_layout->fields[0].storage, FieldStorage::VariableDescriptor);
    EXPECT_EQ(parent_layout->fields[0].slot_size, 8U);
}

TEST(BrfV2LayoutTest, RecordArraysAreVariableButPreserveElementLayout) {
    SchemaIR schema = make_schema();
    RecordIR* fixed_item = add_schema_record(schema, 2U, 7U, "FixedItem");
    add_primitive_field(*fixed_item, "value", 0U, PrimitiveType::PRIMITIVE_TYPE_U32);
    RecordIR* variable_item = add_schema_record(schema, 3U, 8U, "VariableItem");
    add_string_field(*variable_item, "text", 0U, 32U);
    RecordIR* parent = add_schema_record(schema, 1U, 42U, "Parent");
    add_record_array_field(*parent, "fixed_items", 0U, 2U, 4U);
    add_record_array_field(*parent, "variable_items", 1U, 3U, 4U);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    const auto* layout = find_record(output.layout_model, "Parent");
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->fields.size(), 2U);
    for (const auto& field : layout->fields) {
        EXPECT_EQ(field.storage, FieldStorage::VariableDescriptor);
        ASSERT_NE(field.type.element_type, nullptr);
        EXPECT_EQ(field.type.kind, LayoutTypeKind::Array);
    }
    EXPECT_EQ(layout->fields[0].type.element_type->classification,
              RecordClassification::FixedSize);
    EXPECT_EQ(layout->fields[0].type.element_type->encoded_width, 21U);
    EXPECT_EQ(layout->fields[1].type.element_type->classification,
              RecordClassification::VariableSize);
}

TEST(BrfV2LayoutTest, InvalidReferenceAndRecursiveReferenceFail) {
    SchemaIR missing_schema = make_schema();
    RecordIR* missing = add_schema_record(missing_schema, 1U, 1U, "Missing");
    add_record_field(*missing, "child", 0U, 99U);
    const BrfV2LayoutOutput missing_output = run_brf_v2_layout(missing_schema);
    EXPECT_FALSE(missing_output.diagnostics.empty());
    EXPECT_TRUE(missing_output.layout_model.records.empty());

    SchemaIR recursive_schema = make_schema();
    RecordIR* recursive = add_schema_record(recursive_schema, 1U, 1U, "Recursive");
    add_record_field(*recursive, "self", 0U, 1U);
    const BrfV2LayoutOutput recursive_output = run_brf_v2_layout(recursive_schema);
    EXPECT_FALSE(recursive_output.diagnostics.empty());
    EXPECT_TRUE(recursive_output.layout_model.records.empty());
}

TEST(BrfV2LayoutTest, RepeatedSchemaIrLayoutIsDeterministic) {
    SchemaIR schema = make_schema();
    RecordIR* first = add_schema_record(schema, 1U, 10U, "First");
    add_string_field(*first, "name", 0U, 16U);
    RecordIR* second = add_schema_record(schema, 2U, 11U, "Second");
    add_primitive_field(*second, "value", 0U, PrimitiveType::PRIMITIVE_TYPE_I64);

    const BrfV2LayoutOutput one = run_brf_v2_layout(schema);
    const BrfV2LayoutOutput two = run_brf_v2_layout(schema);
    ASSERT_TRUE(one.diagnostics.empty()) << diagnostics_summary(one.diagnostics);
    ASSERT_TRUE(two.diagnostics.empty()) << diagnostics_summary(two.diagnostics);
    ASSERT_EQ(one.layout_model.records.size(), two.layout_model.records.size());
    for (std::size_t record_index = 0; record_index < one.layout_model.records.size();
         ++record_index) {
        const auto& lhs = one.layout_model.records[record_index];
        const auto& rhs = two.layout_model.records[record_index];
        EXPECT_EQ(lhs.fqn, rhs.fqn);
        EXPECT_EQ(lhs.record_id, rhs.record_id);
        EXPECT_EQ(lhs.presence_bitmap_size, rhs.presence_bitmap_size);
        EXPECT_EQ(lhs.fixed_region_size, rhs.fixed_region_size);
        ASSERT_EQ(lhs.fields.size(), rhs.fields.size());
        for (std::size_t field_index = 0; field_index < lhs.fields.size(); ++field_index) {
            EXPECT_EQ(lhs.fields[field_index].location.byte_offset,
                      rhs.fields[field_index].location.byte_offset);
            EXPECT_EQ(lhs.fields[field_index].slot_size, rhs.fields[field_index].slot_size);
            EXPECT_EQ(lhs.fields[field_index].storage, rhs.fields[field_index].storage);
        }
    }
}

TEST(BrfV2LayoutTest, InvalidVariableBoundsFailBeforeLayoutIsPublished) {
    SchemaIR schema = make_schema();
    RecordIR* record = add_schema_record(schema, 1U, 1U, "Example");
    add_string_field(*record, "name", 0U, 0U);

    const BrfV2LayoutOutput output = run_brf_v2_layout(schema);
    EXPECT_FALSE(output.diagnostics.empty());
    EXPECT_TRUE(output.layout_model.records.empty());
}

} // namespace
