#include "compiler/layout/brf_v2_runtime.hpp"
#include "compiler/layout/layout.hpp"
#include "quarry/runtime/binary_record_v2.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::compiler::layout::LayoutComputer;
using quarry::compiler::layout::LayoutModel;
using quarry::runtime::BrfV2Builder;
using quarry::runtime::BrfV2Error;
using quarry::runtime::BrfV2LayoutRegistry;
using quarry::runtime::BrfV2RecordLayout;
using quarry::runtime::BrfV2ValidationResult;
using quarry::schema_ir::FieldIR;
using quarry::schema_ir::PrimitiveType;
using quarry::schema_ir::RecordIR;
using quarry::schema_ir::SchemaIR;

using Bytes = std::vector<std::byte>;

[[nodiscard]] std::byte b(std::uint8_t value) { return static_cast<std::byte>(value); }

[[nodiscard]] SchemaIR make_schema() {
    SchemaIR schema;
    schema.set_schema_ir_version(1U);
    schema.mutable_root_namespace()->set_ir_id(1U);
    return schema;
}

[[nodiscard]] RecordIR* add_record(SchemaIR& schema, std::uint64_t ir_id, std::uint32_t record_id,
                                   std::string name) {
    RecordIR* record = schema.mutable_root_namespace()->add_records();
    record->set_ir_id(ir_id);
    record->set_record_id(record_id);
    record->set_name(name);
    record->set_fqn(name);
    return record;
}

void add_primitive(RecordIR& record, std::string name, std::uint32_t index,
                   PrimitiveType primitive) {
    FieldIR* field = record.add_fields();
    field->set_name(std::move(name));
    field->set_field_index(index);
    field->mutable_type()->set_primitive(primitive);
}

void add_string(RecordIR& record, std::string name, std::uint32_t index, std::uint32_t max_bytes) {
    FieldIR* field = record.add_fields();
    field->set_name(std::move(name));
    field->set_field_index(index);
    field->mutable_type()->mutable_string()->set_max_bytes(max_bytes);
}

void add_record_field(RecordIR& record, std::string name, std::uint32_t index,
                      std::uint64_t target_ir_id) {
    FieldIR* field = record.add_fields();
    field->set_name(std::move(name));
    field->set_field_index(index);
    field->mutable_type()->mutable_record()->set_target_record_ir_id(target_ir_id);
}

void add_record_array_field(RecordIR& record, std::string name, std::uint32_t index,
                            std::uint64_t target_ir_id, std::uint32_t max_elements) {
    FieldIR* field = record.add_fields();
    field->set_name(std::move(name));
    field->set_field_index(index);
    field->mutable_type()->mutable_array()->set_max_elements(max_elements);
    field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(target_ir_id);
}

[[nodiscard]] std::optional<BrfV2LayoutRegistry> example_layout() {
    SchemaIR schema = make_schema();
    RecordIR* example = add_record(schema, 1U, 1U, "Example");
    add_primitive(*example, "timestamp", 0U, PrimitiveType::PRIMITIVE_TYPE_U32);
    add_string(*example, "name", 1U, 64U);
    add_primitive(*example, "state", 2U, PrimitiveType::PRIMITIVE_TYPE_U16);
    FieldIR* samples = example->add_fields();
    samples->set_name("samples");
    samples->set_field_index(3U);
    samples->mutable_type()->mutable_array()->set_max_elements(16U);
    samples->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        PrimitiveType::PRIMITIVE_TYPE_U16);

    LayoutComputer computer;
    DiagnosticCollection diagnostics;
    const LayoutModel layout = computer.compute(schema, diagnostics);
    if (!diagnostics.empty()) {
        return std::nullopt;
    }
    return quarry::compiler::layout::to_brf_v2_runtime_layout(layout);
}

TEST(BinaryRecordV2RuntimeTest, EncodesApprovedExampleByteForByte) {
    auto registry = example_layout();
    ASSERT_TRUE(registry.has_value());
    const BrfV2RecordLayout* layout = registry->find(1U);
    ASSERT_NE(layout, nullptr);
    BrfV2Builder builder(*layout, *registry);

    const Bytes timestamp{b(0x00), b(0x00), b(0x00), b(0x01)};
    const Bytes name{b(0x61), b(0x62), b(0x63)};
    const Bytes state{b(0x00), b(0x02)};
    const Bytes samples{b(0x02), b(0x00), b(0x0A), b(0x00), b(0x14)};
    ASSERT_TRUE(builder.set_field(0U, timestamp));
    ASSERT_TRUE(builder.set_field(1U, name));
    ASSERT_TRUE(builder.set_field(2U, state));
    ASSERT_TRUE(builder.set_field(3U, samples));

    const auto result = builder.finalize();
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value->size(), 47U);
    const Bytes expected{
        b(0x02), b(0x00), b(0x00), b(0x10), b(0x00), b(0x00), b(0x00), b(0x01), b(0x00), b(0x00),
        b(0x00), b(0x17), b(0x00), b(0x00), b(0x00), b(0x2F), b(0x0F), b(0x00), b(0x00), b(0x00),
        b(0x01), b(0x00), b(0x00), b(0x00), b(0x27), b(0x00), b(0x00), b(0x00), b(0x03), b(0x00),
        b(0x02), b(0x00), b(0x00), b(0x00), b(0x2A), b(0x00), b(0x00), b(0x00), b(0x05), b(0x61),
        b(0x62), b(0x63), b(0x02), b(0x00), b(0x0A), b(0x00), b(0x14),
    };
    ASSERT_EQ(result.value->size(), expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        ASSERT_EQ((*result.value)[index], expected[index]) << "at byte " << index;
    }
    EXPECT_EQ(
        quarry::runtime::read_raw_u32(std::span<const std::byte>(result.value->data() + 21U, 4U)),
        39U);
    EXPECT_EQ(
        quarry::runtime::read_raw_u32(std::span<const std::byte>(result.value->data() + 31U, 4U)),
        42U);
}

TEST(BinaryRecordV2RuntimeTest, AbsenceDiffersFromPresentEmptyValues) {
    auto registry = example_layout();
    ASSERT_TRUE(registry.has_value());
    const BrfV2RecordLayout* layout = registry->find(1U);
    ASSERT_NE(layout, nullptr);

    BrfV2Builder absent(*layout, *registry);
    const auto absent_result = absent.finalize();
    ASSERT_TRUE(absent_result.ok());
    EXPECT_EQ(absent_result.value->size(), 39U);
    EXPECT_EQ((*absent_result.value)[16], b(0x00));
    EXPECT_TRUE(std::all_of(absent_result.value->begin() + 21U, absent_result.value->begin() + 29U,
                            [](std::byte value) { return value == b(0x00); }));

    BrfV2Builder present_empty(*layout, *registry);
    const Bytes empty;
    ASSERT_TRUE(present_empty.set_field(1U, empty));
    ASSERT_TRUE(present_empty.set_field(3U, Bytes{b(0x00)}));
    const auto present_result = present_empty.finalize();
    ASSERT_TRUE(present_result.ok());
    EXPECT_EQ((*present_result.value)[16], b(0x0A));
    EXPECT_EQ(present_result.value->size(), 40U);
    EXPECT_EQ(quarry::runtime::read_raw_u32(
                  std::span<const std::byte>(present_result.value->data() + 21U, 4U)),
              39U);
    EXPECT_EQ(quarry::runtime::read_raw_u32(
                  std::span<const std::byte>(present_result.value->data() + 25U, 4U)),
              0U);
    EXPECT_EQ(quarry::runtime::read_raw_u32(
                  std::span<const std::byte>(present_result.value->data() + 31U, 4U)),
              39U);
    EXPECT_EQ(quarry::runtime::read_raw_u32(
                  std::span<const std::byte>(present_result.value->data() + 35U, 4U)),
              1U);
}

TEST(BinaryRecordV2RuntimeTest, RejectsMalformedHeaderAndDescriptor) {
    auto registry = example_layout();
    ASSERT_TRUE(registry.has_value());
    const BrfV2RecordLayout* layout = registry->find(1U);
    ASSERT_NE(layout, nullptr);
    BrfV2Builder builder(*layout, *registry);
    const Bytes name{b(0x61)};
    ASSERT_TRUE(builder.set_field(1U, name));
    const auto encoded = builder.finalize();
    ASSERT_TRUE(encoded.ok());

    Bytes bad_version = *encoded.value;
    bad_version[0] = b(0x01);
    EXPECT_EQ(quarry::runtime::validate_brf_v2(bad_version, *layout, *registry).error,
              BrfV2Error::unsupported_version);

    Bytes bad_flags = *encoded.value;
    bad_flags[1] = b(0x01);
    EXPECT_EQ(quarry::runtime::validate_brf_v2(bad_flags, *layout, *registry).error,
              BrfV2Error::unsupported_flags);

    Bytes bad_descriptor = *encoded.value;
    bad_descriptor[24] = b(0x00);
    bad_descriptor[25] = b(0x00);
    bad_descriptor[26] = b(0x00);
    bad_descriptor[27] = b(0x10);
    EXPECT_EQ(quarry::runtime::validate_brf_v2(bad_descriptor, *layout, *registry).error,
              BrfV2Error::invalid_variable_range);
}

TEST(BinaryRecordV2RuntimeTest, ValidatesInlineAndVariableNestedRecords) {
    SchemaIR schema = make_schema();
    RecordIR* child = add_record(schema, 2U, 2U, "Child");
    add_primitive(*child, "value", 0U, PrimitiveType::PRIMITIVE_TYPE_U16);
    RecordIR* parent = add_record(schema, 1U, 1U, "Parent");
    add_record_field(*parent, "child", 0U, 2U);

    LayoutComputer computer;
    DiagnosticCollection diagnostics;
    const LayoutModel layout = computer.compute(schema, diagnostics);
    ASSERT_TRUE(diagnostics.empty());
    auto registry = quarry::compiler::layout::to_brf_v2_runtime_layout(layout);
    ASSERT_TRUE(registry.has_value());
    const BrfV2RecordLayout* child_layout = registry->find(2U);
    const BrfV2RecordLayout* parent_layout = registry->find(1U);
    ASSERT_NE(child_layout, nullptr);
    ASSERT_NE(parent_layout, nullptr);
    BrfV2Builder child_builder(*child_layout, *registry);
    ASSERT_TRUE(child_builder.set_field(0U, Bytes{b(0x00), b(0x07)}));
    const auto child_bytes = child_builder.finalize();
    ASSERT_TRUE(child_bytes.ok());
    BrfV2Builder parent_builder(*parent_layout, *registry);
    ASSERT_TRUE(parent_builder.set_field(0U, *child_bytes.value));
    const auto parent_bytes = parent_builder.finalize();
    ASSERT_TRUE(parent_bytes.ok());
    EXPECT_EQ(
        quarry::runtime::validate_brf_v2(*parent_bytes.value, *parent_layout, *registry).error,
        BrfV2Error::none);
}

TEST(BinaryRecordV2RuntimeTest, ValidatesVariableNestedRecordAndRecordArrays) {
    SchemaIR schema = make_schema();
    RecordIR* fixed_child = add_record(schema, 2U, 2U, "FixedChild");
    add_primitive(*fixed_child, "value", 0U, PrimitiveType::PRIMITIVE_TYPE_U16);
    RecordIR* variable_child = add_record(schema, 3U, 3U, "VariableChild");
    add_string(*variable_child, "text", 0U, 8U);
    RecordIR* parent = add_record(schema, 1U, 1U, "Parent");
    add_record_field(*parent, "variable", 0U, 3U);
    add_record_array_field(*parent, "fixed_children", 1U, 2U, 4U);

    LayoutComputer computer;
    DiagnosticCollection diagnostics;
    const LayoutModel layout = computer.compute(schema, diagnostics);
    ASSERT_TRUE(diagnostics.empty());
    auto registry = quarry::compiler::layout::to_brf_v2_runtime_layout(layout);
    ASSERT_TRUE(registry.has_value());

    const BrfV2RecordLayout* fixed_layout = registry->find(2U);
    const BrfV2RecordLayout* variable_layout = registry->find(3U);
    const BrfV2RecordLayout* parent_layout = registry->find(1U);
    ASSERT_NE(fixed_layout, nullptr);
    ASSERT_NE(variable_layout, nullptr);
    ASSERT_NE(parent_layout, nullptr);

    BrfV2Builder fixed_builder(*fixed_layout, *registry);
    ASSERT_TRUE(fixed_builder.set_field(0U, Bytes{b(0x00), b(0x07)}));
    const auto fixed_bytes = fixed_builder.finalize();
    ASSERT_TRUE(fixed_bytes.ok());

    BrfV2Builder variable_builder(*variable_layout, *registry);
    ASSERT_TRUE(variable_builder.set_field(0U, Bytes{b(0x78)}));
    const auto variable_bytes = variable_builder.finalize();
    ASSERT_TRUE(variable_bytes.ok());

    Bytes array_value{b(0x02)};
    array_value.insert(array_value.end(), fixed_bytes.value->begin(), fixed_bytes.value->end());
    array_value.insert(array_value.end(), fixed_bytes.value->begin(), fixed_bytes.value->end());
    BrfV2Builder parent_builder(*parent_layout, *registry);
    ASSERT_TRUE(parent_builder.set_field(0U, *variable_bytes.value));
    ASSERT_TRUE(parent_builder.set_field(1U, array_value));
    const auto parent_bytes = parent_builder.finalize();
    ASSERT_TRUE(parent_bytes.ok());
    EXPECT_EQ(
        quarry::runtime::validate_brf_v2(*parent_bytes.value, *parent_layout, *registry).error,
        BrfV2Error::none);
}

TEST(BinaryRecordV2RuntimeTest, DeepNestedChainUsesIterativeValidation) {
    constexpr std::size_t depth = 1024U;
    SchemaIR schema = make_schema();
    std::vector<RecordIR*> records;
    records.reserve(depth);
    for (std::size_t index = 0U; index < depth; ++index) {
        records.push_back(add_record(schema, index + 1U, static_cast<std::uint32_t>(index + 1U),
                                     "R" + std::to_string(index)));
    }
    for (std::size_t index = 0U; index + 1U < depth; ++index) {
        add_record_field(*records[index], "next", 0U, index + 2U);
    }
    add_primitive(*records.back(), "value", 0U, PrimitiveType::PRIMITIVE_TYPE_U8);

    LayoutComputer computer;
    DiagnosticCollection diagnostics;
    const LayoutModel layout = computer.compute(schema, diagnostics);
    ASSERT_TRUE(diagnostics.empty());
    auto registry = quarry::compiler::layout::to_brf_v2_runtime_layout(layout);
    ASSERT_TRUE(registry.has_value());

    std::vector<std::byte> current{b(0x00)};
    BrfV2Builder leaf(*registry->find(static_cast<std::uint32_t>(depth)), *registry);
    ASSERT_TRUE(leaf.set_field(0U, current));
    auto encoded = leaf.finalize();
    ASSERT_TRUE(encoded.ok());
    for (std::size_t index = depth - 1U; index > 0U; --index) {
        BrfV2Builder builder(*registry->find(static_cast<std::uint32_t>(index)), *registry);
        ASSERT_TRUE(builder.set_field(0U, *encoded.value));
        encoded = builder.finalize();
        ASSERT_TRUE(encoded.ok());
    }
    const BrfV2ValidationResult validation =
        quarry::runtime::validate_brf_v2(*encoded.value, *registry->find(1U), *registry);
    EXPECT_EQ(validation.error, BrfV2Error::none);
}

} // namespace
