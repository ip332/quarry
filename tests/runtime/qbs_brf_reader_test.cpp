#include "compiler/layout/layout.hpp"
#include "compiler/qbs/parser.hpp"
#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"
#include "quarry/runtime/qbs_brf_reader.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::schema_ir::SchemaIR;
using namespace quarry::compiler::qbs;
using namespace quarry::runtime;

SchemaIR nested_schema_ir() {
    SchemaIR schema;
    schema.set_schema_ir_version(1U);
    schema.mutable_root_namespace()->set_ir_id(1U);
    const auto add_record = [&](std::uint64_t ir_id, std::uint32_t record_id,
                                std::string_view name) {
        auto* record = schema.mutable_root_namespace()->add_records();
        record->set_ir_id(ir_id);
        record->set_record_id(record_id);
        record->set_name(std::string(name));
        record->set_fqn(std::string(name));
        return record;
    };
    auto* grandchild = add_record(3U, 3U, "Grandchild");
    auto* value = grandchild->add_fields();
    value->set_name("value");
    value->set_field_index(0U);
    value->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
    auto* child = add_record(2U, 2U, "Child");
    auto* grandchild_field = child->add_fields();
    grandchild_field->set_name("grandchild");
    grandchild_field->set_field_index(0U);
    grandchild_field->mutable_type()->mutable_record()->set_target_record_ir_id(3U);
    auto* parent = add_record(1U, 1U, "Parent");
    auto* child_field = parent->add_fields();
    child_field->set_name("child");
    child_field->set_field_index(0U);
    child_field->mutable_type()->mutable_record()->set_target_record_ir_id(2U);
    return schema;
}

SchemaIR variable_nested_schema_ir() {
    SchemaIR schema;
    schema.set_schema_ir_version(1U);
    schema.mutable_root_namespace()->set_ir_id(1U);
    auto add_record = [&](std::uint64_t ir_id, std::uint32_t record_id, std::string_view fqn) {
        auto* record = schema.mutable_root_namespace()->add_records();
        record->set_ir_id(ir_id);
        record->set_record_id(record_id);
        record->set_name(std::string(fqn));
        record->set_fqn(std::string(fqn));
        return record;
    };
    auto* child = add_record(2U, 2U, "Child");
    auto* value = child->add_fields();
    value->set_name("value");
    value->set_field_index(0U);
    value->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
    auto* name = child->add_fields();
    name->set_name("name");
    name->set_field_index(1U);
    name->mutable_type()->mutable_string()->set_max_bytes(16U);
    auto* parent = add_record(1U, 1U, "Parent");
    auto* state = parent->add_fields();
    state->set_name("state");
    state->set_field_index(0U);
    state->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U16);
    auto* child_field = parent->add_fields();
    child_field->set_name("child");
    child_field->set_field_index(1U);
    child_field->mutable_type()->mutable_record()->set_target_record_ir_id(2U);
    return schema;
}

SchemaIR record_array_schema_ir(bool variable_item) {
    SchemaIR schema;
    schema.set_schema_ir_version(1U);
    schema.mutable_root_namespace()->set_ir_id(1U);
    auto add_record = [&](std::uint64_t ir_id, std::uint32_t record_id, std::string_view fqn) {
        auto* record = schema.mutable_root_namespace()->add_records();
        record->set_ir_id(ir_id);
        record->set_record_id(record_id);
        record->set_name(std::string(fqn));
        record->set_fqn(std::string(fqn));
        return record;
    };
    auto* item = add_record(2U, 2U, "Item");
    auto* value = item->add_fields();
    value->set_name("value");
    value->set_field_index(0U);
    value->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
    if (variable_item) {
        auto* name = item->add_fields();
        name->set_name("name");
        name->set_field_index(1U);
        name->mutable_type()->mutable_string()->set_max_bytes(16U);
    }
    auto* parent = add_record(1U, 1U, "Parent");
    auto* items = parent->add_fields();
    items->set_name("items");
    items->set_field_index(0U);
    auto* array = items->mutable_type()->mutable_array();
    array->set_max_elements(4U);
    array->mutable_element_type()->mutable_record()->set_target_record_ir_id(2U);
    return schema;
}

std::vector<std::uint8_t> qbs_image() {
    constexpr std::string_view hex =
        "51425300010000280101001068a731750346a0ad2e665f81260ea18300040000000000280000012d"
        "00010000000000580000001d00020000000000750000007000030000000000e50000004000060000"
        "000001250000000800000001000000000004000100000001000000170000000000ffff0000000000"
        "000000001100000000002000010000000000000004ffff0000000100060000001500000000004000"
        "020001000000000008ffff0000000200000000001d00000000001000000002000000000002ffff00"
        "00000300060000001f00000000004000030003000000000008ffff00000501000200000000000000"
        "0000000000070100040000000000000000000000000d020000000000000000000000000040100200"
        "000000000000000008000000004578616d706c6500";
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0U; i < hex.size(); i += 2U) {
        const auto digit = [](char c) -> std::uint8_t {
            return static_cast<std::uint8_t>(c >= 'a' ? c - 'a' + 10 : c - '0');
        };
        bytes.push_back(static_cast<std::uint8_t>((digit(hex[i]) << 4U) | digit(hex[i + 1U])));
    }
    return bytes;
}

TEST(QbsBrfReaderTest, ReadsThreeLevelFixedNestedRecordsFromNormalPipeline) {
    const auto ir = nested_schema_ir();
    quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
    quarry::compiler::layout::LayoutComputer computer;
    const auto layout = computer.compute(ir, diagnostics);
    ASSERT_TRUE(diagnostics.empty());
    const auto model =
        QbsModelBuilder{}.build(ir, layout, {.mode = BuildMode::Minimal}, diagnostics);
    ASSERT_TRUE(model.has_value());
    for (const auto& record : model->records) {
        EXPECT_FALSE(record.variable_size);
        ASSERT_TRUE(record.complete_fixed_record_size.has_value());
    }
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    for (std::size_t i = 0U; i < schema->record_count(); ++i)
        EXPECT_FALSE(schema->record(i).variable_size);

    const auto make_record = [](std::uint32_t id, std::uint32_t fixed_size,
                                std::uint32_t total_size, std::span<const std::uint8_t> child,
                                std::uint8_t value) {
        std::vector<std::uint8_t> bytes(total_size, 0U);
        bytes[0] = 2U;
        bytes[3] = 16U;
        bytes[4] = static_cast<std::uint8_t>(id >> 24U);
        bytes[5] = static_cast<std::uint8_t>(id >> 16U);
        bytes[6] = static_cast<std::uint8_t>(id >> 8U);
        bytes[7] = static_cast<std::uint8_t>(id);
        bytes[11] = static_cast<std::uint8_t>(fixed_size);
        bytes[15] = static_cast<std::uint8_t>(total_size);
        bytes[16] = 1U;
        if (!child.empty())
            std::copy(child.begin(), child.end(), bytes.begin() + 17U);
        else
            bytes[20] = value;
        return bytes;
    };
    const auto* grandchild_layout = layout.find_record("Grandchild");
    const auto* child_layout = layout.find_record("Child");
    const auto* parent_layout = layout.find_record("Parent");
    ASSERT_NE(grandchild_layout, nullptr);
    ASSERT_NE(child_layout, nullptr);
    ASSERT_NE(parent_layout, nullptr);
    const auto grandchild_bytes =
        make_record(3U, grandchild_layout->fixed_region_size,
                    *grandchild_layout->complete_fixed_record_size, {}, 7U);
    const auto child_bytes =
        make_record(2U, child_layout->fixed_region_size, *child_layout->complete_fixed_record_size,
                    grandchild_bytes, 0U);
    const auto parent_bytes =
        make_record(1U, parent_layout->fixed_region_size,
                    *parent_layout->complete_fixed_record_size, child_bytes, 0U);

    const auto parent_schema = schema->find_record_by_identity("Parent");
    ASSERT_TRUE(parent_schema.has_value());
    GenericBrfError error = GenericBrfError::none;
    const auto view = validate_brf_record(*schema, *parent_schema, parent_bytes, {}, &error);
    ASSERT_TRUE(view.has_value());
    ASSERT_EQ(error, GenericBrfError::none);
    const auto child_view = view->nested_record(0U);
    ASSERT_TRUE(child_view.has_value());
    const auto grandchild_view = child_view->nested_record(0U);
    ASSERT_TRUE(grandchild_view.has_value());
    ASSERT_EQ(grandchild_view->field(0U)->as_unsigned(), 7U);

    auto absent_parent = make_record(1U, parent_layout->fixed_region_size,
                                     *parent_layout->complete_fixed_record_size, {}, 0U);
    absent_parent[16] = 0U;
    EXPECT_TRUE(
        validate_brf_record(*schema, *parent_schema, absent_parent, {}, &error).has_value());
    const auto absent_view =
        validate_brf_record(*schema, *parent_schema, absent_parent, {}, &error);
    ASSERT_TRUE(absent_view.has_value());
    EXPECT_FALSE(absent_view->nested_record(0U).has_value());
    absent_parent[17] = 1U;
    EXPECT_FALSE(validate_brf_record(*schema, *parent_schema, absent_parent, {}, &error));
    EXPECT_EQ(error, GenericBrfError::invalid_presence);

    auto wrong_id = parent_bytes;
    wrong_id[17U + 7U] = 9U;
    EXPECT_FALSE(validate_brf_record(*schema, *parent_schema, wrong_id, {}, &error));
    EXPECT_EQ(error, GenericBrfError::unexpected_record_id);
}

TEST(QbsBrfReaderTest, ReadsVariableNestedRecordFromNormalPipeline) {
    const auto ir = variable_nested_schema_ir();
    DiagnosticCollection diagnostics;
    quarry::compiler::layout::LayoutComputer computer;
    const auto layout = computer.compute(ir, diagnostics);
    ASSERT_TRUE(diagnostics.empty());
    const auto model =
        QbsModelBuilder{}.build(ir, layout, {.mode = BuildMode::Minimal}, diagnostics);
    ASSERT_TRUE(model.has_value());
    for (const auto& record : model->records)
        EXPECT_TRUE(record.variable_size);
    const auto* child_layout = layout.find_record("Child");
    const auto* parent_layout = layout.find_record("Parent");
    ASSERT_NE(child_layout, nullptr);
    ASSERT_NE(parent_layout, nullptr);
    EXPECT_EQ(child_layout->classification,
              quarry::compiler::layout::RecordClassification::VariableSize);
    EXPECT_EQ(parent_layout->classification,
              quarry::compiler::layout::RecordClassification::VariableSize);
    ASSERT_EQ(child_layout->fields.size(), 2U);
    ASSERT_EQ(parent_layout->fields.size(), 2U);
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto child_schema = schema->find_record_by_identity("Child");
    const auto parent_schema = schema->find_record_by_identity("Parent");
    ASSERT_TRUE(child_schema.has_value());
    ASSERT_TRUE(parent_schema.has_value());
    EXPECT_TRUE(child_schema->variable_size);
    EXPECT_TRUE(parent_schema->variable_size);

    const auto put_u16 = [](std::vector<std::uint8_t>& bytes, std::size_t offset,
                            std::uint16_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value);
    };
    const auto put_u32 = [](std::vector<std::uint8_t>& bytes, std::size_t offset,
                            std::uint32_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[offset + 3U] = static_cast<std::uint8_t>(value);
    };
    const auto make_header = [&](std::vector<std::uint8_t>& bytes, std::uint32_t record_id,
                                 std::uint32_t fixed_size) {
        bytes[0] = 2U;
        put_u16(bytes, 2U, 16U);
        put_u32(bytes, 4U, record_id);
        put_u32(bytes, 8U, fixed_size);
        put_u32(bytes, 12U, static_cast<std::uint32_t>(bytes.size()));
    };

    constexpr std::size_t name_size = 3U;
    const auto child_tail = static_cast<std::size_t>(16U + child_layout->fixed_region_size);
    std::vector<std::uint8_t> child_bytes(child_tail + name_size, 0U);
    make_header(child_bytes, child_layout->record_id, child_layout->fixed_region_size);
    child_bytes[16U] = 0x03U;
    put_u32(child_bytes, child_layout->fields[0].location.byte_offset, 7U);
    put_u32(child_bytes, child_layout->fields[1].location.byte_offset,
            static_cast<std::uint32_t>(child_tail));
    put_u32(child_bytes, child_layout->fields[1].location.byte_offset + 4U,
            static_cast<std::uint32_t>(name_size));
    std::copy_n(std::string_view("abc").begin(), name_size,
                child_bytes.begin() + static_cast<std::ptrdiff_t>(child_tail));

    const auto parent_tail = static_cast<std::size_t>(16U + parent_layout->fixed_region_size);
    std::vector<std::uint8_t> parent_bytes(parent_tail + child_bytes.size(), 0U);
    make_header(parent_bytes, parent_layout->record_id, parent_layout->fixed_region_size);
    parent_bytes[16U] = 0x03U;
    put_u16(parent_bytes, parent_layout->fields[0].location.byte_offset, 2U);
    put_u32(parent_bytes, parent_layout->fields[1].location.byte_offset,
            static_cast<std::uint32_t>(parent_tail));
    put_u32(parent_bytes, parent_layout->fields[1].location.byte_offset + 4U,
            static_cast<std::uint32_t>(child_bytes.size()));
    std::copy(child_bytes.begin(), child_bytes.end(),
              parent_bytes.begin() + static_cast<std::ptrdiff_t>(parent_tail));

    GenericBrfError error = GenericBrfError::none;
    const auto view = validate_brf_record(*schema, *parent_schema, parent_bytes, {}, &error);
    ASSERT_TRUE(view.has_value());
    ASSERT_EQ(error, GenericBrfError::none);
    ASSERT_EQ(view->field(0U)->as_unsigned(), 2U);
    const auto nested = view->nested_record(1U);
    ASSERT_TRUE(nested.has_value());
    ASSERT_EQ(nested->field(0U)->as_unsigned(), 7U);
    ASSERT_EQ(*nested->field(1U)->as_string(), "abc");

    auto child_relative_error = parent_bytes;
    put_u32(child_relative_error, parent_tail + child_layout->fields[1].location.byte_offset,
            static_cast<std::uint32_t>(parent_tail));
    EXPECT_FALSE(validate_brf_record(*schema, *parent_schema, child_relative_error, {}, &error));
    EXPECT_EQ(error, GenericBrfError::invalid_variable_range);

    auto empty_child = parent_bytes;
    put_u32(empty_child, parent_layout->fields[1].location.byte_offset + 4U, 0U);
    EXPECT_FALSE(validate_brf_record(*schema, *parent_schema, empty_child, {}, &error));
    EXPECT_EQ(error, GenericBrfError::invalid_variable_range);

    auto parent_gap = parent_bytes;
    put_u32(parent_gap, parent_layout->fields[1].location.byte_offset,
            static_cast<std::uint32_t>(parent_tail + 1U));
    EXPECT_FALSE(validate_brf_record(*schema, *parent_schema, parent_gap, {}, &error));
    EXPECT_EQ(error, GenericBrfError::invalid_variable_range);
}

TEST(QbsBrfReaderTest, ReadsFixedAndVariableRecordArrays) {
    std::vector<std::uint8_t> qbs_storage;
    const auto put_u32 = [](std::vector<std::uint8_t>& bytes, std::size_t offset,
                            std::uint32_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[offset + 3U] = static_cast<std::uint8_t>(value);
    };
    const auto make_image = [&](bool variable_item, std::vector<std::uint8_t>& parent_bytes,
                                const std::vector<std::uint32_t>& values)
        -> std::optional<quarry::compiler::qbs::ValidatedQbsView> {
        DiagnosticCollection diagnostics;
        const auto ir = record_array_schema_ir(variable_item);
        quarry::compiler::layout::LayoutComputer computer;
        const auto layout = computer.compute(ir, diagnostics);
        EXPECT_TRUE(diagnostics.empty());
        const auto model =
            QbsModelBuilder{}.build(ir, layout, {.mode = BuildMode::Minimal}, diagnostics);
        if (!model.has_value())
            return std::nullopt;
        const auto image = serialize_qbs(*model, diagnostics);
        if (!image.has_value())
            return std::nullopt;
        qbs_storage = image->bytes;
        const auto schema = parse_qbs(qbs_storage, diagnostics);
        if (!schema.has_value())
            return std::nullopt;
        const auto* item_layout = layout.find_record("Item");
        const auto* parent_layout = layout.find_record("Parent");
        EXPECT_NE(item_layout, nullptr);
        EXPECT_NE(parent_layout, nullptr);
        std::vector<std::vector<std::uint8_t>> items;
        for (std::size_t i = 0U; i < values.size(); ++i) {
            const auto tail = static_cast<std::size_t>(16U + item_layout->fixed_region_size);
            const auto item_size = tail + (variable_item ? 3U : 0U);
            std::vector<std::uint8_t> item(item_size, 0U);
            item[0] = 2U;
            item[3] = 16U;
            put_u32(item, 4U, item_layout->record_id);
            put_u32(item, 8U, item_layout->fixed_region_size);
            put_u32(item, 12U, item_size);
            item[16U] = variable_item ? 0x03U : 0x01U;
            put_u32(item, item_layout->fields[0].location.byte_offset, values[i]);
            if (variable_item) {
                put_u32(item, item_layout->fields[1].location.byte_offset,
                        static_cast<std::uint32_t>(tail));
                put_u32(item, item_layout->fields[1].location.byte_offset + 4U, 3U);
                std::copy_n(std::string_view("abc").begin(), 3U,
                            item.begin() + static_cast<std::ptrdiff_t>(tail));
            }
            items.push_back(std::move(item));
        }
        const auto parent_tail = static_cast<std::size_t>(16U + parent_layout->fixed_region_size);
        std::size_t payload_size = 1U;
        for (const auto& item : items)
            payload_size += item.size() + (variable_item ? 1U : 0U);
        parent_bytes.assign(parent_tail + payload_size, 0U);
        parent_bytes[0] = 2U;
        parent_bytes[3] = 16U;
        put_u32(parent_bytes, 4U, parent_layout->record_id);
        put_u32(parent_bytes, 8U, parent_layout->fixed_region_size);
        put_u32(parent_bytes, 12U, parent_bytes.size());
        parent_bytes[16U] = values.empty() ? 0U : 0x01U;
        put_u32(parent_bytes, parent_layout->fields[0].location.byte_offset,
                static_cast<std::uint32_t>(parent_tail));
        put_u32(parent_bytes, parent_layout->fields[0].location.byte_offset + 4U,
                static_cast<std::uint32_t>(payload_size));
        std::size_t cursor = parent_tail;
        parent_bytes[cursor++] = static_cast<std::uint8_t>(values.size());
        for (const auto& item : items) {
            if (variable_item)
                parent_bytes[cursor++] = static_cast<std::uint8_t>(item.size());
            std::copy(item.begin(), item.end(),
                      parent_bytes.begin() + static_cast<std::ptrdiff_t>(cursor));
            cursor += item.size();
        }
        return schema;
    };

    for (const bool variable_item : {false, true}) {
        std::vector<std::uint8_t> parent_bytes;
        const auto schema = make_image(variable_item, parent_bytes, {10U, 20U});
        ASSERT_TRUE(schema.has_value());
        std::optional<quarry::compiler::qbs::QbsRecordView> parent;
        for (std::size_t i = 0U; i < schema->record_count(); ++i) {
            const auto candidate = schema->record(i);
            if (candidate.field_count == 1U &&
                schema->find_field(static_cast<std::uint32_t>(i), 0U).has_value() &&
                schema->type(schema->find_field(static_cast<std::uint32_t>(i), 0U)->type_index)
                        .code == 16U)
                parent = candidate;
        }
        ASSERT_TRUE(parent.has_value());
        GenericBrfError error = GenericBrfError::none;
        const auto view = validate_brf_record(*schema, *parent, parent_bytes, {}, &error);
        ASSERT_TRUE(view.has_value()) << static_cast<int>(error);
        const auto array = view->record_array(0U);
        ASSERT_TRUE(array.has_value());
        ASSERT_EQ(array->size(), 2U);
        const auto first = array->element(0U);
        const auto second = array->element(1U);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(first->field(0U)->as_unsigned(), 10U);
        EXPECT_EQ(second->field(0U)->as_unsigned(), 20U);
        if (variable_item) {
            EXPECT_EQ(*first->field(1U)->as_string(), "abc");
            EXPECT_EQ(*second->field(1U)->as_string(), "abc");
        }
        EXPECT_EQ(array->element(1U)->field(0U)->as_unsigned(), 20U);

        const auto parent_tail = static_cast<std::size_t>(16U + parent->fixed_region_size);
        auto present_empty = std::vector<std::uint8_t>(parent_tail + 1U, 0U);
        present_empty[0] = 2U;
        present_empty[3] = 16U;
        put_u32(present_empty, 4U, parent->record_id);
        put_u32(present_empty, 8U, parent->fixed_region_size);
        put_u32(present_empty, 12U, present_empty.size());
        present_empty[16U] = 1U;
        put_u32(present_empty, 17U, static_cast<std::uint32_t>(parent_tail));
        put_u32(present_empty, 21U, 1U);
        const auto empty_view = validate_brf_record(*schema, *parent, present_empty, {}, &error);
        ASSERT_TRUE(empty_view.has_value());
        ASSERT_TRUE(empty_view->record_array(0U).has_value());
        EXPECT_EQ(empty_view->record_array(0U)->size(), 0U);

        auto absent = std::vector<std::uint8_t>(parent_tail, 0U);
        absent[0] = 2U;
        absent[3] = 16U;
        put_u32(absent, 4U, parent->record_id);
        put_u32(absent, 8U, parent->fixed_region_size);
        put_u32(absent, 12U, absent.size());
        const auto absent_view = validate_brf_record(*schema, *parent, absent, {}, &error);
        ASSERT_TRUE(absent_view.has_value());
        EXPECT_FALSE(absent_view->record_array(0U).has_value());

        auto malformed = parent_bytes;
        malformed[16U] = 0U;
        put_u32(malformed, 20U, 0U);
        EXPECT_FALSE(validate_brf_record(*schema, *parent, malformed, {}, &error));

        auto wrong_id = parent_bytes;
        const auto first_child = parent_tail + 1U + (variable_item ? 1U : 0U);
        put_u32(wrong_id, first_child + 4U, 99U);
        EXPECT_FALSE(validate_brf_record(*schema, *parent, wrong_id, {}, &error));
        EXPECT_EQ(error, GenericBrfError::unexpected_record_id);

        BrfReadLimits one_element;
        one_element.max_array_elements_traversed = 1U;
        EXPECT_FALSE(validate_brf_record(*schema, *parent, parent_bytes, one_element, &error));
        EXPECT_EQ(error, GenericBrfError::bounds_exceeded);

        auto trailing = parent_bytes;
        trailing.push_back(0U);
        put_u32(trailing, 12U, trailing.size());
        EXPECT_FALSE(validate_brf_record(*schema, *parent, trailing, {}, &error));
    }
}

TEST(QbsBrfReaderTest, ReadsCanonicalExampleWithoutGeneratedCode) {
    auto qbs = qbs_image();
    DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_identity("Example");
    ASSERT_TRUE(record.has_value());

    // Header, presence bitmap, fixed region, and the canonical string tail.
    std::vector<std::uint8_t> brf(42U, 0U);
    brf[0] = 2U;
    brf[2] = 0U;
    brf[3] = 16U;
    brf[7] = 1U;
    brf[11] = 23U;
    brf[15] = 42U;
    brf[16] = 0x07U; // timestamp, name, and state present; samples absent.
    brf[20] = 1U;    // timestamp at fixed offset 17.
    brf[24] = 39U;   // name descriptor: tail offset.
    brf[28] = 3U;
    brf[30] = 2U; // state at fixed offset 29.
    brf[39] = 'a';
    brf[40] = 'b';
    brf[41] = 'c';

    GenericBrfError error = GenericBrfError::none;
    const auto view = validate_brf_record(*schema, *record, brf, {}, &error);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(error, GenericBrfError::none);
    EXPECT_TRUE(view->is_present(*schema->find_field(0U, 0U)));
    EXPECT_EQ(view->field(0U)->as_unsigned(), 1U);
    ASSERT_TRUE(view->field(1U).has_value());
    EXPECT_EQ(*view->field(1U)->as_string(), "abc");
    EXPECT_EQ(view->field(2U)->as_unsigned(), 2U);
    EXPECT_FALSE(view->is_present(*schema->find_field(0U, 3U)));
    EXPECT_FALSE(view->field(3U).has_value());
}

TEST(QbsBrfReaderTest, RejectsMalformedHeaderAndCanonicalPresence) {
    auto qbs = qbs_image();
    DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_id(1U);
    ASSERT_TRUE(record.has_value());
    std::vector<std::uint8_t> brf(42U, 0U);
    brf[0] = 2U;
    brf[3] = 16U;
    brf[7] = 1U;
    brf[11] = 23U;
    brf[15] = 42U;
    brf[16] = 0x07U;
    brf[20] = 1U;
    brf[24] = 39U;
    brf[28] = 3U;
    brf[30] = 2U;
    brf[39] = 'a';
    brf[40] = 'b';
    brf[41] = 'c';

    GenericBrfError error = GenericBrfError::none;
    auto malformed = brf;
    malformed[0] = 1U;
    EXPECT_FALSE(validate_brf_record(*schema, *record, malformed, {}, &error));
    EXPECT_EQ(error, GenericBrfError::unsupported_version);

    malformed = brf;
    malformed[16] |= 0x80U;
    EXPECT_FALSE(validate_brf_record(*schema, *record, malformed, {}, &error));
    EXPECT_EQ(error, GenericBrfError::invalid_presence);
}

TEST(QbsBrfReaderTest, ReadsPrimitiveArrayAndRejectsInvalidEnum) {
    auto qbs = qbs_image();
    DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_id(1U);
    ASSERT_TRUE(record.has_value());
    std::vector<std::uint8_t> brf(47U, 0U);
    brf[0] = 2U;
    brf[3] = 16U;
    brf[7] = 1U;
    brf[11] = 23U;
    brf[15] = 47U;
    brf[16] = 0x0fU;
    brf[20] = 1U;
    brf[24] = 39U;
    brf[28] = 3U;
    brf[30] = 2U;
    brf[34] = 42U;
    brf[38] = 5U;
    brf[39] = 'a';
    brf[40] = 'b';
    brf[41] = 'c';
    brf[42] = 2U;
    brf[44] = 10U;
    brf[46] = 20U;

    GenericBrfError error = GenericBrfError::none;
    const auto view = validate_brf_record(*schema, *record, brf, {}, &error);
    ASSERT_TRUE(view.has_value());
    const auto array = view->array(3U);
    ASSERT_TRUE(array.has_value());
    ASSERT_EQ(array->size(), 2U);
    EXPECT_EQ(array->element(0U)->as_unsigned(), 10U);
    EXPECT_EQ(array->element(1U)->as_unsigned(), 20U);

    auto malformed = brf;
    malformed[42] = 3U;
    EXPECT_FALSE(validate_brf_record(*schema, *record, malformed, {}, &error));
    EXPECT_EQ(error, GenericBrfError::malformed_array);
}

TEST(QbsBrfReaderTest, DistinguishesAbsentAndPresentEmptyVariableValues) {
    auto qbs = qbs_image();
    DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_id(1U);
    ASSERT_TRUE(record.has_value());

    std::vector<std::uint8_t> empty_string(39U, 0U);
    empty_string[0] = 2U;
    empty_string[3] = 16U;
    empty_string[7] = 1U;
    empty_string[11] = 23U;
    empty_string[15] = 39U;
    empty_string[16] = 0x07U;
    empty_string[20] = 1U;
    empty_string[24] = 39U;
    empty_string[30] = 2U;
    GenericBrfError error = GenericBrfError::none;
    const auto string_view = validate_brf_record(*schema, *record, empty_string, {}, &error);
    ASSERT_TRUE(string_view.has_value());
    ASSERT_TRUE(string_view->field(1U).has_value());
    EXPECT_EQ(*string_view->field(1U)->as_string(), "");

    std::vector<std::uint8_t> empty_array(40U, 0U);
    empty_array[0] = 2U;
    empty_array[3] = 16U;
    empty_array[7] = 1U;
    empty_array[11] = 23U;
    empty_array[15] = 40U;
    empty_array[16] = 0x0fU;
    empty_array[20] = 1U;
    empty_array[24] = 39U;
    empty_array[30] = 2U;
    empty_array[34] = 39U;
    empty_array[38] = 1U;
    const auto array_view = validate_brf_record(*schema, *record, empty_array, {}, &error);
    ASSERT_TRUE(array_view.has_value());
    ASSERT_TRUE(array_view->array(3U).has_value());
    EXPECT_EQ(array_view->array(3U)->size(), 0U);
}

TEST(QbsBrfReaderTest, EnforcesReaderWorkAndArrayLimits) {
    auto qbs = qbs_image();
    DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_id(1U);
    ASSERT_TRUE(record.has_value());
    std::vector<std::uint8_t> brf(47U, 0U);
    brf[0] = 2U;
    brf[3] = 16U;
    brf[7] = 1U;
    brf[11] = 23U;
    brf[15] = 47U;
    brf[16] = 0x0fU;
    brf[20] = 1U;
    brf[24] = 39U;
    brf[28] = 3U;
    brf[30] = 2U;
    brf[34] = 42U;
    brf[38] = 5U;
    brf[42] = 2U;
    brf[44] = 10U;
    brf[46] = 20U;
    GenericBrfError error = GenericBrfError::none;
    BrfReadLimits no_work;
    no_work.max_work_items = 0U;
    EXPECT_FALSE(validate_brf_record(*schema, *record, brf, no_work, &error));
    EXPECT_EQ(error, GenericBrfError::resource_limit_exceeded);
    BrfReadLimits one_element;
    one_element.max_array_elements_traversed = 1U;
    EXPECT_FALSE(validate_brf_record(*schema, *record, brf, one_element, &error));
    EXPECT_EQ(error, GenericBrfError::bounds_exceeded);
}

std::vector<std::uint8_t> string_record(std::string_view value) {
    std::vector<std::uint8_t> bytes(39U + value.size(), 0U);
    bytes[0] = 2U;
    bytes[3] = 16U;
    bytes[7] = 1U;
    bytes[11] = 23U;
    bytes[15] = static_cast<std::uint8_t>(bytes.size());
    bytes[16] = 0x02U;
    bytes[24] = 39U;
    bytes[28] = static_cast<std::uint8_t>(value.size());
    std::copy(value.begin(), value.end(), bytes.begin() + 39U);
    return bytes;
}

TEST(QbsBrfReaderTest, ValidatesUnicodeScalarValueBoundaries) {
    auto qbs = qbs_image();
    DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_id(1U);
    ASSERT_TRUE(record.has_value());
    const std::vector<std::string> valid = {
        "ASCII", "\xC2\xA2",     "\xE2\x82\xAC", "\xF0\x9F\x92\xA9",
        "\x00",  "\xED\x9F\xBF", "\xEE\x80\x80", "\xF4\x8F\xBF\xBF"};
    for (const auto& value : valid) {
        GenericBrfError error = GenericBrfError::none;
        EXPECT_TRUE(validate_brf_record(*schema, *record, string_record(value), {}, &error));
    }
    const std::vector<std::string> invalid = {"\x80",
                                              "\xC2",
                                              "\xE2\x82",
                                              "\xF0\x9F\x92",
                                              "\xC0\x80",
                                              "\xC1\xBF",
                                              "\xE0\x80\x80",
                                              "\xED\xA0\x80",
                                              "\xED\xBF\xBF",
                                              "\xF0\x80\x80\x80",
                                              "\xF4\x90\x80\x80",
                                              "\xF5\x80\x80\x80",
                                              "\xFF"};
    for (const auto& value : invalid) {
        GenericBrfError error = GenericBrfError::none;
        EXPECT_FALSE(validate_brf_record(*schema, *record, string_record(value), {}, &error));
        EXPECT_EQ(error, GenericBrfError::invalid_utf8);
    }
}

TEST(QbsBrfReaderTest, AcceptsTenByteVaruintPayloadBeforeSchemaBoundsCheck) {
    auto qbs = qbs_image();
    DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_id(1U);
    ASSERT_TRUE(record.has_value());
    auto make = [](std::uint8_t final_payload) {
        std::vector<std::uint8_t> bytes(49U, 0U);
        bytes[0] = 2U;
        bytes[3] = 16U;
        bytes[7] = 1U;
        bytes[11] = 23U;
        bytes[15] = 49U;
        bytes[16] = 0x08U;
        bytes[34] = 39U;
        bytes[38] = 10U;
        for (std::size_t i = 39U; i < 48U; ++i)
            bytes[i] = 0x80U;
        bytes[48] = final_payload;
        return bytes;
    };
    GenericBrfError error = GenericBrfError::none;
    // A zero tenth payload is only valid when the preceding payload already
    // requires the tenth byte; this all-zero prefix is correctly rejected as
    // an overlong encoding.
    EXPECT_FALSE(validate_brf_record(*schema, *record, make(0U), {}, &error));
    EXPECT_EQ(error, GenericBrfError::malformed_array);
    EXPECT_FALSE(validate_brf_record(*schema, *record, make(1U), {}, &error));
    EXPECT_EQ(error, GenericBrfError::bounds_exceeded);
    EXPECT_FALSE(validate_brf_record(*schema, *record, make(2U), {}, &error));
    EXPECT_EQ(error, GenericBrfError::malformed_array);
}

TEST(QbsBrfReaderTest, InternalRecordSpanUsesLocalOffsets) {
    auto qbs = qbs_image();
    DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_id(1U);
    ASSERT_TRUE(record.has_value());
    auto record_bytes = string_record("local");
    std::vector<std::uint8_t> enclosing(17U + record_bytes.size() + 9U, 0xA5U);
    std::copy(record_bytes.begin(), record_bytes.end(), enclosing.begin() + 17U);
    GenericBrfError error = GenericBrfError::none;
    const auto view = validate_record_span(
        *schema, *record,
        std::span<const std::uint8_t>(enclosing).subspan(17U, record_bytes.size()), {}, &error);
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->field(1U).has_value());
    EXPECT_EQ(*view->field(1U)->as_string(), "local");
}

} // namespace
