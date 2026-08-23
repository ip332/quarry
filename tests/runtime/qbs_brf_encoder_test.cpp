#include "compiler/layout/layout.hpp"
#include "compiler/qbs/parser.hpp"
#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"
#include "quarry/runtime/qbs_brf_encoder.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::schema_ir::SchemaIR;
using namespace quarry::compiler::qbs;
using namespace quarry::runtime;

SchemaIR fixed_schema() {
    SchemaIR schema;
    schema.set_schema_ir_version(1U);
    schema.mutable_root_namespace()->set_ir_id(1U);
    auto* packet = schema.mutable_root_namespace()->add_records();
    packet->set_ir_id(1U);
    packet->set_record_id(1U);
    packet->set_name("Packet");
    packet->set_fqn("Packet");
    auto* enabled = packet->add_fields();
    enabled->set_name("enabled");
    enabled->set_field_index(0U);
    enabled->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_BOOL);
    auto* count = packet->add_fields();
    count->set_name("count");
    count->set_field_index(1U);
    count->mutable_type()->set_primitive(quarry::schema_ir::PRIMITIVE_TYPE_U32);
    auto* state = packet->add_fields();
    state->set_name("state");
    state->set_field_index(2U);
    state->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(2U);
    auto* enumeration = schema.mutable_root_namespace()->add_enums();
    enumeration->set_ir_id(2U);
    enumeration->set_name("State");
    enumeration->set_fqn("State");
    for (const auto value : {0, 1}) {
        auto* item = enumeration->add_values();
        item->set_name(value == 0 ? "off" : "on");
        item->set_value(value);
    }
    return schema;
}

TEST(QbsBrfEncoderTest, EncodesFixedScalarsBoolAndEnum) {
    DiagnosticCollection diagnostics;
    quarry::compiler::layout::LayoutComputer computer;
    const auto layout = computer.compute(fixed_schema(), diagnostics);
    ASSERT_TRUE(diagnostics.empty());
    const auto model =
        QbsModelBuilder{}.build(fixed_schema(), layout, {.mode = BuildMode::Minimal}, diagnostics);
    ASSERT_TRUE(model.has_value());
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_identity("Packet");
    ASSERT_TRUE(record.has_value());

    const std::vector<std::optional<BrfEncodeValue>> fields{
        BrfEncodeValue{true}, BrfEncodeValue{std::uint64_t{42}}, BrfEncodeValue{std::uint64_t{1}}};
    GenericBrfEncodeError encode_error = GenericBrfEncodeError::none;
    const auto bytes = encode_brf_record(*schema, *record, fields, &encode_error);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(encode_error, GenericBrfEncodeError::none);

    GenericBrfError read_error = GenericBrfError::none;
    const auto view = validate_brf_record(*schema, *record, *bytes, {}, &read_error);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->field(0U)->as_bool());
    EXPECT_EQ(view->field(1U)->as_unsigned(), 42U);
    EXPECT_EQ(view->field(2U)->as_unsigned(), 1U);
}

TEST(QbsBrfEncoderTest, RejectsInvalidValuesAndUnsupportedVariableRecords) {
    DiagnosticCollection diagnostics;
    quarry::compiler::layout::LayoutComputer computer;
    const auto layout = computer.compute(fixed_schema(), diagnostics);
    const auto model =
        QbsModelBuilder{}.build(fixed_schema(), layout, {.mode = BuildMode::Minimal}, diagnostics);
    ASSERT_TRUE(model.has_value());
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_identity("Packet");
    ASSERT_TRUE(record.has_value());
    const std::vector<std::optional<BrfEncodeValue>> invalid{
        BrfEncodeValue{true}, BrfEncodeValue{std::uint64_t{42}}, BrfEncodeValue{std::uint64_t{9}}};
    GenericBrfEncodeError error = GenericBrfEncodeError::none;
    EXPECT_FALSE(encode_brf_record(*schema, *record, invalid, &error));
    EXPECT_EQ(error, GenericBrfEncodeError::invalid_enum);

    const std::vector<std::optional<BrfEncodeValue>> wrong_count{BrfEncodeValue{true}};
    error = GenericBrfEncodeError::none;
    EXPECT_FALSE(encode_brf_record(*schema, *record, wrong_count, &error));
    EXPECT_EQ(error, GenericBrfEncodeError::field_count_mismatch);

    const std::vector<std::optional<BrfEncodeValue>> wrong_type{BrfEncodeValue{std::uint64_t{1}},
                                                                BrfEncodeValue{std::uint64_t{42}},
                                                                BrfEncodeValue{std::uint64_t{1}}};
    error = GenericBrfEncodeError::none;
    EXPECT_FALSE(encode_brf_record(*schema, *record, wrong_type, &error));
    EXPECT_EQ(error, GenericBrfEncodeError::invalid_value);
}

TEST(QbsBrfEncoderTest, LeavesAbsentFixedSlotsZeroAndClearsPresence) {
    DiagnosticCollection diagnostics;
    quarry::compiler::layout::LayoutComputer computer;
    const auto layout = computer.compute(fixed_schema(), diagnostics);
    ASSERT_TRUE(diagnostics.empty());
    const auto model =
        QbsModelBuilder{}.build(fixed_schema(), layout, {.mode = BuildMode::Minimal}, diagnostics);
    ASSERT_TRUE(model.has_value());
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_identity("Packet");
    ASSERT_TRUE(record.has_value());

    const std::vector<std::optional<BrfEncodeValue>> fields{
        std::nullopt, BrfEncodeValue{std::uint64_t{42}}, std::nullopt};
    GenericBrfEncodeError error = GenericBrfEncodeError::none;
    const auto bytes = encode_brf_record(*schema, *record, fields, &error);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_EQ(error, GenericBrfEncodeError::none);
    EXPECT_EQ((*bytes)[16U] & 0x01U, 0U);
    EXPECT_EQ((*bytes)[16U] & 0x02U, 0x02U);
    EXPECT_EQ((*bytes)[16U] & 0x04U, 0U);
    const auto enabled_field = schema->find_field(0U, 0U);
    const auto state_field = schema->find_field(0U, 2U);
    ASSERT_TRUE(enabled_field.has_value());
    ASSERT_TRUE(state_field.has_value());
    for (std::size_t i = 0U; i < enabled_field->slot_size; ++i)
        EXPECT_EQ((*bytes)[enabled_field->byte_offset + i], 0U);
    for (std::size_t i = 0U; i < state_field->slot_size; ++i)
        EXPECT_EQ((*bytes)[state_field->byte_offset + i], 0U);

    GenericBrfError read_error = GenericBrfError::none;
    ASSERT_TRUE(validate_brf_record(*schema, *record, *bytes, {}, &read_error).has_value());
    EXPECT_EQ(read_error, GenericBrfError::none);
}

} // namespace
