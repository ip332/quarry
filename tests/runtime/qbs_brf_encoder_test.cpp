#include "compiler/layout/layout.hpp"
#include "compiler/qbs/parser.hpp"
#include "compiler/qbs/qbs.hpp"
#include "compiler/qbs/serializer.hpp"
#include "quarry/runtime/qbs_brf_encoder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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

SchemaIR variable_schema() {
    auto schema = fixed_schema();
    auto* packet = schema.mutable_root_namespace()->mutable_records(0);
    auto* name = packet->add_fields();
    name->set_name("name");
    name->set_field_index(3U);
    name->mutable_type()->mutable_string()->set_max_bytes(32U);
    auto* payload = packet->add_fields();
    payload->set_name("payload");
    payload->set_field_index(4U);
    payload->mutable_type()->mutable_bytes()->set_max_bytes(32U);
    return schema;
}

SchemaIR array_schema() {
    auto schema = fixed_schema();
    auto* packet = schema.mutable_root_namespace()->mutable_records(0);
    auto* samples = packet->add_fields();
    samples->set_name("samples");
    samples->set_field_index(3U);
    samples->mutable_type()->mutable_array()->set_max_elements(4U);
    samples->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        quarry::schema_ir::PRIMITIVE_TYPE_U16);
    auto* flags = packet->add_fields();
    flags->set_name("flags");
    flags->set_field_index(4U);
    flags->mutable_type()->mutable_array()->set_max_elements(4U);
    flags->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        quarry::schema_ir::PRIMITIVE_TYPE_BOOL);
    auto* states = packet->add_fields();
    states->set_name("states");
    states->set_field_index(5U);
    states->mutable_type()->mutable_array()->set_max_elements(4U);
    states->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_enum_type()
        ->set_target_enum_ir_id(2U);
    auto* ratios = packet->add_fields();
    ratios->set_name("ratios");
    ratios->set_field_index(6U);
    ratios->mutable_type()->mutable_array()->set_max_elements(4U);
    ratios->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        quarry::schema_ir::PRIMITIVE_TYPE_F32);
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

TEST(QbsBrfEncoderTest, EncodesStringsBytesAndCanonicalVariableTail) {
    DiagnosticCollection diagnostics;
    quarry::compiler::layout::LayoutComputer computer;
    const auto source = variable_schema();
    const auto layout = computer.compute(source, diagnostics);
    ASSERT_TRUE(diagnostics.empty());
    const auto model =
        QbsModelBuilder{}.build(source, layout, {.mode = BuildMode::Minimal}, diagnostics);
    ASSERT_TRUE(model.has_value());
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_identity("Packet");
    ASSERT_TRUE(record.has_value());
    ASSERT_TRUE(record->variable_size);

    const std::vector<std::optional<BrfEncodeValue>> fields{
        BrfEncodeValue{true}, BrfEncodeValue{std::uint64_t{42}}, BrfEncodeValue{std::uint64_t{1}},
        BrfEncodeValue{std::string("abc")},
        BrfEncodeValue{std::vector<std::uint8_t>{0x10U, 0x20U, 0x30U}}};
    GenericBrfEncodeError error = GenericBrfEncodeError::none;
    const auto bytes = encode_brf_record(*schema, *record, fields, &error);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_EQ(error, GenericBrfEncodeError::none);

    GenericBrfError read_error = GenericBrfError::none;
    const auto view = validate_brf_record(*schema, *record, *bytes, {}, &read_error);
    ASSERT_TRUE(view.has_value());
    ASSERT_EQ(read_error, GenericBrfError::none);
    ASSERT_EQ(view->field(3U)->as_string(), std::optional<std::string_view>("abc"));
    ASSERT_EQ(view->field(4U)->bytes().size(), 3U);
    EXPECT_EQ(view->field(4U)->bytes()[0], 0x10U);
    EXPECT_EQ(view->field(4U)->bytes()[1], 0x20U);
    EXPECT_EQ(view->field(4U)->bytes()[2], 0x30U);

    const auto name_field = schema->find_field(0U, 3U);
    const auto payload_field = schema->find_field(0U, 4U);
    ASSERT_TRUE(name_field.has_value());
    ASSERT_TRUE(payload_field.has_value());
    const auto name_offset =
        (static_cast<std::uint32_t>((*bytes)[name_field->byte_offset]) << 24U) |
        (static_cast<std::uint32_t>((*bytes)[name_field->byte_offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>((*bytes)[name_field->byte_offset + 2U]) << 8U) |
        (*bytes)[name_field->byte_offset + 3U];
    const auto payload_offset =
        (static_cast<std::uint32_t>((*bytes)[payload_field->byte_offset]) << 24U) |
        (static_cast<std::uint32_t>((*bytes)[payload_field->byte_offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>((*bytes)[payload_field->byte_offset + 2U]) << 8U) |
        (*bytes)[payload_field->byte_offset + 3U];
    EXPECT_LT(name_offset, payload_offset);
    EXPECT_EQ(payload_offset - name_offset, 3U);
    EXPECT_EQ(bytes->size(), payload_offset + 3U);
}

TEST(QbsBrfEncoderTest, EncodesPrimitiveArraysWithCanonicalVaruint) {
    DiagnosticCollection diagnostics;
    quarry::compiler::layout::LayoutComputer computer;
    const auto source = array_schema();
    const auto layout = computer.compute(source, diagnostics);
    ASSERT_TRUE(diagnostics.empty());
    const auto model =
        QbsModelBuilder{}.build(source, layout, {.mode = BuildMode::Minimal}, diagnostics);
    ASSERT_TRUE(model.has_value());
    const auto image = serialize_qbs(*model, diagnostics);
    ASSERT_TRUE(image.has_value());
    const auto schema = parse_qbs(image->bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto record = schema->find_record_by_identity("Packet");
    ASSERT_TRUE(record.has_value());

    const std::vector<std::optional<BrfEncodeValue>> fields{
        BrfEncodeValue{true},
        BrfEncodeValue{std::uint64_t{42}},
        BrfEncodeValue{std::uint64_t{1}},
        BrfEncodeValue{BrfEncodeArray{BrfUnsignedArray{10U, 20U, 65535U}}},
        BrfEncodeValue{BrfEncodeArray{BrfBoolArray{true, false, true}}},
        BrfEncodeValue{BrfEncodeArray{BrfUnsignedArray{0U, 1U}}},
        BrfEncodeValue{BrfEncodeArray{BrfFloat32Array{1.5F, -2.0F}}}};
    GenericBrfEncodeError error = GenericBrfEncodeError::none;
    const auto bytes = encode_brf_record(*schema, *record, fields, &error);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_EQ(error, GenericBrfEncodeError::none);

    GenericBrfError read_error = GenericBrfError::none;
    const auto view = validate_brf_record(*schema, *record, *bytes, {}, &read_error);
    ASSERT_TRUE(view.has_value());
    ASSERT_EQ(read_error, GenericBrfError::none);
    const auto array = view->array(3U);
    ASSERT_TRUE(array.has_value());
    ASSERT_EQ(array->size(), 3U);
    EXPECT_EQ(array->element(0U)->as_unsigned(), 10U);
    EXPECT_EQ(array->element(1U)->as_unsigned(), 20U);
    EXPECT_EQ(array->element(2U)->as_unsigned(), 65535U);
    ASSERT_TRUE(view->array(4U).has_value());
    EXPECT_TRUE(view->array(4U)->element(0U)->as_bool().value());
    EXPECT_FALSE(view->array(4U)->element(1U)->as_bool().value());
    ASSERT_TRUE(view->array(5U).has_value());
    EXPECT_EQ(view->array(5U)->element(1U)->as_unsigned(), 1U);
    ASSERT_TRUE(view->array(6U).has_value());
    EXPECT_EQ(view->array(6U)->element(0U)->kind(), GenericBrfValueKind::float32);

    auto too_many = fields;
    too_many[3U] = BrfEncodeValue{BrfEncodeArray{BrfUnsignedArray{1U, 2U, 3U, 4U, 5U}}};
    error = GenericBrfEncodeError::none;
    EXPECT_FALSE(encode_brf_record(*schema, *record, too_many, &error));
    EXPECT_EQ(error, GenericBrfEncodeError::invalid_value);

    auto invalid_enum = fields;
    const auto enum_array_field = schema->find_field(0U, 5U);
    ASSERT_TRUE(enum_array_field.has_value());
    ASSERT_EQ(enum_array_field->storage, 2U);
    const auto enum_array_type = schema->type(enum_array_field->type_index);
    ASSERT_EQ(enum_array_type.code, 16U);
    ASSERT_EQ(schema->type(enum_array_type.reference).code, 12U);
    EXPECT_EQ(enum_array_type.max_elements, 4U);
    invalid_enum[5U] = BrfEncodeValue{BrfEncodeArray{BrfUnsignedArray{0U, 999U}}};
    ASSERT_TRUE(std::holds_alternative<BrfEncodeArray>(*invalid_enum[5U]));
    ASSERT_TRUE(
        std::holds_alternative<BrfUnsignedArray>(std::get<BrfEncodeArray>(*invalid_enum[5U])));
    error = GenericBrfEncodeError::none;
    EXPECT_FALSE(encode_brf_record(*schema, *record, invalid_enum, &error));
    EXPECT_EQ(error, GenericBrfEncodeError::invalid_enum);
}

} // namespace
