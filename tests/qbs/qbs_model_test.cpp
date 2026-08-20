#include "compiler/qbs/qbs.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::compiler::layout::DescriptorKind;
using quarry::compiler::layout::FieldLayout;
using quarry::compiler::layout::FieldLocation;
using quarry::compiler::layout::FieldStorage;
using quarry::compiler::layout::LayoutTypeKind;
using quarry::compiler::layout::RecordClassification;
using quarry::compiler::layout::RecordLayout;
using quarry::compiler::layout::TypeLayout;
using quarry::compiler::qbs::BuildMode;
using quarry::compiler::qbs::QbsBuildOptions;
using quarry::compiler::qbs::QbsImageModel;
using quarry::compiler::qbs::QbsModelBuilder;
using quarry::compiler::qbs::Storage;
using quarry::compiler::qbs::TypeCode;
using quarry::schema_ir::EnumIR;
using quarry::schema_ir::RecordIR;
using quarry::schema_ir::SchemaIR;

[[nodiscard]] TypeLayout fixed_type(LayoutTypeKind kind, std::uint32_t width) {
    TypeLayout type;
    type.kind = kind;
    type.classification = RecordClassification::FixedSize;
    type.encoded_width = width;
    return type;
}

[[nodiscard]] FieldLayout field(std::uint32_t index, std::string name, TypeLayout type,
                                std::uint32_t offset, std::uint32_t slot,
                                FieldStorage storage = FieldStorage::Fixed) {
    return FieldLayout{
        .field_index = index,
        .presence_bit_index = index,
        .name = std::move(name),
        .type = std::move(type),
        .location = FieldLocation{.byte_offset = offset, .bit_offset = 0, .bit_width = slot * 8U},
        .slot_size = slot,
        .storage = storage,
        .descriptor_kind = storage == FieldStorage::VariableDescriptor
                               ? DescriptorKind::DataOffsetByteLength
                               : DescriptorKind::None,
    };
}

RecordIR* add_record(SchemaIR& schema, std::uint64_t ir_id, std::uint32_t record_id,
                     std::string_view name, std::initializer_list<std::string_view> fields) {
    auto* record = schema.mutable_root_namespace()->add_records();
    record->set_ir_id(ir_id);
    record->set_record_id(record_id);
    record->set_name(std::string(name));
    record->set_fqn(std::string(name));
    std::uint32_t index = 0U;
    for (const auto field_name : fields) {
        auto* field_ir = record->add_fields();
        field_ir->set_name(std::string(field_name));
        field_ir->set_field_index(index++);
    }
    return record;
}

[[nodiscard]] SchemaIR base_schema() {
    SchemaIR schema;
    schema.set_schema_ir_version(1U);
    schema.mutable_root_namespace()->set_ir_id(1U);
    return schema;
}

[[nodiscard]] SchemaIR enum_schema(const std::vector<int>& values) {
    SchemaIR schema = base_schema();
    add_record(schema, 1U, 1U, "Packet", {"state"});
    auto* enumeration = schema.mutable_root_namespace()->add_enums();
    enumeration->set_ir_id(2U);
    enumeration->set_name("State");
    enumeration->set_fqn("State");
    for (const int value : values) {
        auto* item = enumeration->add_values();
        item->set_name("value" + std::to_string(value));
        item->set_value(value);
    }
    return schema;
}

[[nodiscard]] quarry::compiler::layout::LayoutModel
enum_layout(const std::vector<std::uint64_t>& values) {
    TypeLayout type = fixed_type(LayoutTypeKind::Enum, 1U);
    type.referenced_ir_id = 2U;
    type.referenced_fqn = "State";
    type.enum_values = values;
    RecordLayout record;
    record.fqn = "Packet";
    record.record_id = 1U;
    record.presence_bitmap_size = 1U;
    record.fixed_region_size = 2U;
    record.fields = {field(0U, "state", std::move(type), 17U, 1U)};
    return {.records = {std::move(record)}};
}

[[nodiscard]] std::optional<QbsImageModel>
build(const SchemaIR& schema, const quarry::compiler::layout::LayoutModel& layout, BuildMode mode,
      DiagnosticCollection& diagnostics) {
    return QbsModelBuilder{}.build(schema, layout, QbsBuildOptions{.mode = mode}, diagnostics);
}

TEST(QbsModelTest, BuildsExampleFromCanonicalLayoutInMinimalAndReflectiveModes) {
    SchemaIR schema = base_schema();
    add_record(schema, 1U, 1U, "Example", {"timestamp", "name", "state", "samples"});

    TypeLayout string_type;
    string_type.kind = LayoutTypeKind::String;
    string_type.classification = RecordClassification::VariableSize;
    string_type.max_bytes = 64U;
    TypeLayout array_type;
    array_type.kind = LayoutTypeKind::Array;
    array_type.classification = RecordClassification::VariableSize;
    array_type.max_elements = 8U;
    array_type.element_type = std::make_unique<TypeLayout>(fixed_type(LayoutTypeKind::U16, 2U));

    RecordLayout record;
    record.fqn = "Example";
    record.record_id = 1U;
    record.classification = RecordClassification::VariableSize;
    record.presence_bitmap_size = 1U;
    record.fixed_region_size = 23U;
    record.fields = {
        field(0U, "timestamp", fixed_type(LayoutTypeKind::U32, 4U), 17U, 4U),
        field(1U, "name", string_type, 21U, 8U, FieldStorage::VariableDescriptor),
        field(2U, "state", fixed_type(LayoutTypeKind::U16, 2U), 29U, 2U),
        field(3U, "samples", std::move(array_type), 31U, 8U, FieldStorage::VariableDescriptor),
    };
    quarry::compiler::layout::LayoutModel layout{.records = {std::move(record)}};

    DiagnosticCollection minimal_diagnostics;
    const auto minimal = build(schema, layout, BuildMode::Minimal, minimal_diagnostics);
    ASSERT_TRUE(minimal.has_value());
    EXPECT_TRUE(minimal_diagnostics.diagnostics().empty());
    ASSERT_EQ(minimal->records.size(), 1U);
    ASSERT_EQ(minimal->fields.size(), 4U);
    ASSERT_EQ(minimal->types.size(), 4U);
    EXPECT_EQ(minimal->records[0].record_id, 1U);
    EXPECT_EQ(minimal->records[0].fixed_region_size, 23U);
    EXPECT_EQ(minimal->fields[0].byte_offset, 17U);
    EXPECT_EQ(minimal->fields[1].byte_offset, 21U);
    EXPECT_EQ(minimal->fields[2].byte_offset, 29U);
    EXPECT_EQ(minimal->fields[3].byte_offset, 31U);
    EXPECT_EQ(minimal->fields[1].storage, Storage::VariableDescriptor);
    EXPECT_EQ(minimal->fields[3].storage, Storage::VariableDescriptor);
    EXPECT_EQ(minimal->fields[0].presence_bit_index, 0U);
    EXPECT_EQ(minimal->fields[3].presence_bit_index, 3U);
    EXPECT_EQ(minimal->records[0].name_string_index, quarry::compiler::qbs::kQbsNoStringIndex);
    EXPECT_TRUE(minimal->strings.empty());

    DiagnosticCollection reflective_diagnostics;
    const auto reflective = build(schema, layout, BuildMode::Reflective, reflective_diagnostics);
    ASSERT_TRUE(reflective.has_value());
    EXPECT_TRUE(reflective_diagnostics.diagnostics().empty());
    EXPECT_EQ(reflective->strings,
              (std::vector<std::string>{"Example", "name", "samples", "state", "timestamp"}));
    EXPECT_EQ(reflective->records[0].name_string_index, 0U);
    EXPECT_EQ(reflective->fields[0].name_string_index, 4U);
    EXPECT_EQ(reflective->fields[1].name_string_index, 1U);
    EXPECT_EQ(reflective->fields[3].name_string_index, 2U);
    EXPECT_EQ(minimal->schema_identity_input, reflective->schema_identity_input);
}

TEST(QbsModelTest, SharesEnumAndNestedRecordReferences) {
    SchemaIR schema = base_schema();
    add_record(schema, 1U, 7U, "Parent", {"fixed", "variable", "state", "history"});
    add_record(schema, 2U, 9U, "FixedChild", {"value"});
    add_record(schema, 3U, 11U, "VariableChild", {"text"});
    auto* enumeration = schema.mutable_root_namespace()->add_enums();
    enumeration->set_ir_id(20U);
    enumeration->set_name("State");
    enumeration->set_fqn("State");
    for (const auto [name, value] : {std::pair{"Off", 0}, std::pair{"On", 1}}) {
        auto* item = enumeration->add_values();
        item->set_name(name);
        item->set_value(value);
    }

    TypeLayout fixed_child = fixed_type(LayoutTypeKind::Record, 21U);
    fixed_child.referenced_ir_id = 2U;
    fixed_child.referenced_fqn = "FixedChild";
    TypeLayout variable_child = fixed_child;
    variable_child.classification = RecordClassification::VariableSize;
    variable_child.encoded_width = 0U;
    variable_child.referenced_ir_id = 3U;
    variable_child.referenced_fqn = "VariableChild";
    TypeLayout enum_type = fixed_type(LayoutTypeKind::Enum, 1U);
    enum_type.referenced_ir_id = 20U;
    enum_type.referenced_fqn = "State";
    enum_type.enum_values = {0U, 1U};
    TypeLayout enum_array;
    enum_array.kind = LayoutTypeKind::Array;
    enum_array.classification = RecordClassification::VariableSize;
    enum_array.max_elements = 4U;
    enum_array.element_type = std::make_unique<TypeLayout>(enum_type);

    RecordLayout parent;
    parent.fqn = "Parent";
    parent.record_id = 7U;
    parent.classification = RecordClassification::VariableSize;
    parent.presence_bitmap_size = 1U;
    parent.fixed_region_size = 40U;
    parent.fields = {
        field(0U, "fixed", fixed_child, 17U, 22U, FieldStorage::InlineFixedNestedRecord),
        field(1U, "variable", variable_child, 39U, 8U, FieldStorage::VariableDescriptor),
        field(2U, "state", enum_type, 47U, 1U),
        field(3U, "history", std::move(enum_array), 48U, 8U, FieldStorage::VariableDescriptor),
    };
    RecordLayout fixed_record;
    fixed_record.fqn = "FixedChild";
    fixed_record.record_id = 9U;
    fixed_record.presence_bitmap_size = 1U;
    fixed_record.fixed_region_size = 5U;
    fixed_record.complete_fixed_record_size = 22U;
    fixed_record.fields = {field(0U, "value", fixed_type(LayoutTypeKind::U32, 4U), 17U, 4U)};
    RecordLayout variable_record;
    variable_record.fqn = "VariableChild";
    variable_record.record_id = 11U;
    variable_record.classification = RecordClassification::VariableSize;
    variable_record.presence_bitmap_size = 1U;
    variable_record.fixed_region_size = 9U;
    variable_record.fields = {field(0U, "text", fixed_type(LayoutTypeKind::String, 0U), 17U, 8U,
                                    FieldStorage::VariableDescriptor)};
    variable_record.fields[0].type.classification = RecordClassification::VariableSize;
    variable_record.fields[0].type.max_bytes = 32U;
    quarry::compiler::layout::LayoutModel layout{
        .records = {std::move(fixed_record), std::move(parent), std::move(variable_record)}};

    DiagnosticCollection diagnostics;
    const auto model = build(schema, layout, BuildMode::Reflective, diagnostics);
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(diagnostics.diagnostics().empty());
    ASSERT_EQ(model->enums.size(), 1U);
    EXPECT_EQ(model->enums[0].values, (std::vector<std::uint64_t>{0U, 1U}));
    ASSERT_EQ(model->records.size(), 3U);
    ASSERT_EQ(model->fields.size(), 6U);
    const auto parent_it = std::find_if(model->records.begin(), model->records.end(),
                                        [](const auto& record) { return record.fqn == "Parent"; });
    ASSERT_NE(parent_it, model->records.end());
    const auto parent_index = parent_it->table_index;
    const auto first = model->fields.begin() + static_cast<std::ptrdiff_t>(parent_it->field_start);
    EXPECT_EQ(first[0].storage, Storage::InlineFixedNestedRecord);
    EXPECT_EQ(first[1].storage, Storage::VariableDescriptor);
    EXPECT_NE(first[2].type_index, first[3].type_index);
    EXPECT_EQ(model->types[first[3].type_index].reference, first[2].type_index);
    EXPECT_NE(first[0].type_index, first[1].type_index);
    EXPECT_EQ(first[0].owning_record_index, parent_index);
}

TEST(QbsModelTest, PreservesRecordIdsAndRejectsReferenceOverflow) {
    SchemaIR schema = base_schema();
    add_record(schema, 1U, 42U, "zeta", {});
    RecordLayout record;
    record.fqn = "zeta";
    record.record_id = 42U;
    quarry::compiler::layout::LayoutModel layout{.records = {record}};
    DiagnosticCollection diagnostics;
    const auto model = build(schema, layout, BuildMode::Minimal, diagnostics);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->records.size(), 1U);
    EXPECT_EQ(model->records[0].record_id, 42U);

    QbsImageModel too_many_strings;
    too_many_strings.strings.resize(65536U);
    DiagnosticCollection overflow_diagnostics;
    EXPECT_FALSE(QbsModelBuilder{}.validate(too_many_strings, overflow_diagnostics));
    EXPECT_FALSE(overflow_diagnostics.diagnostics().empty());
}

TEST(QbsModelTest, IdentityInputIgnoresReflectiveNames) {
    SchemaIR schema = base_schema();
    add_record(schema, 1U, 1U, "Thing", {"zeta"});
    RecordLayout record;
    record.fqn = "Thing";
    record.record_id = 1U;
    record.presence_bitmap_size = 1U;
    record.fixed_region_size = 5U;
    record.fields = {field(0U, "zeta", fixed_type(LayoutTypeKind::U32, 4U), 17U, 4U)};
    quarry::compiler::layout::LayoutModel layout{.records = {record}};

    DiagnosticCollection minimal_diagnostics;
    DiagnosticCollection reflective_diagnostics;
    const auto minimal = build(schema, layout, BuildMode::Minimal, minimal_diagnostics);
    const auto reflective = build(schema, layout, BuildMode::Reflective, reflective_diagnostics);
    ASSERT_TRUE(minimal.has_value());
    ASSERT_TRUE(reflective.has_value());
    EXPECT_EQ(minimal->schema_identity_input, reflective->schema_identity_input);
    EXPECT_NE(reflective->records[0].name_string_index, quarry::compiler::qbs::kQbsNoStringIndex);
}

TEST(QbsModelTest, EnumIdentityUsesSortedNumericValues) {
    const SchemaIR declaration_order_a = enum_schema({0, 1});
    const SchemaIR declaration_order_b = enum_schema({1, 0});
    const SchemaIR changed_value = enum_schema({0, 2});
    const SchemaIR added_value = enum_schema({0, 1, 2});

    DiagnosticCollection diagnostics_a;
    DiagnosticCollection diagnostics_b;
    DiagnosticCollection diagnostics_changed;
    DiagnosticCollection diagnostics_added;
    const auto model_a =
        build(declaration_order_a, enum_layout({0U, 1U}), BuildMode::Minimal, diagnostics_a);
    const auto model_b =
        build(declaration_order_b, enum_layout({1U, 0U}), BuildMode::Minimal, diagnostics_b);
    const auto model_changed =
        build(changed_value, enum_layout({0U, 2U}), BuildMode::Minimal, diagnostics_changed);
    const auto model_added =
        build(added_value, enum_layout({0U, 1U, 2U}), BuildMode::Minimal, diagnostics_added);
    ASSERT_TRUE(model_a.has_value());
    ASSERT_TRUE(model_b.has_value());
    ASSERT_TRUE(model_changed.has_value());
    ASSERT_TRUE(model_added.has_value());
    EXPECT_EQ(model_a->schema_identity_input, model_b->schema_identity_input);
    EXPECT_NE(model_a->schema_identity_input, model_changed->schema_identity_input);
    EXPECT_NE(model_a->schema_identity_input, model_added->schema_identity_input);
}

TEST(QbsModelTest, ValidatesFieldSlotsAgainstActualFixedRegionEnd) {
    SchemaIR schema = base_schema();
    add_record(schema, 1U, 1U, "Example", {"value", "payload"});
    RecordLayout record;
    record.fqn = "Example";
    record.record_id = 1U;
    record.presence_bitmap_size = 1U;
    record.fixed_region_size = 14U;
    TypeLayout bytes_type;
    bytes_type.kind = LayoutTypeKind::Bytes;
    bytes_type.classification = RecordClassification::VariableSize;
    bytes_type.max_bytes = 32U;
    record.fields = {
        field(0U, "value", fixed_type(LayoutTypeKind::U32, 4U), 17U, 4U),
        field(1U, "payload", std::move(bytes_type), 22U, 8U, FieldStorage::VariableDescriptor),
    };
    const auto layout = quarry::compiler::layout::LayoutModel{.records = {record}};
    DiagnosticCollection build_diagnostics;
    const auto valid = build(schema, layout, BuildMode::Minimal, build_diagnostics);
    ASSERT_TRUE(valid.has_value());
    ASSERT_EQ(valid->fields[1].byte_offset + valid->fields[1].slot_size, 30U);

    QbsImageModel crossing = *valid;
    crossing.fields[1].byte_offset = 23U;
    DiagnosticCollection crossing_diagnostics;
    EXPECT_FALSE(QbsModelBuilder{}.validate(crossing, crossing_diagnostics));

    QbsImageModel at_boundary = *valid;
    at_boundary.fields[1].byte_offset = 22U;
    DiagnosticCollection boundary_diagnostics;
    EXPECT_TRUE(QbsModelBuilder{}.validate(at_boundary, boundary_diagnostics));
}

} // namespace
