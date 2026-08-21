#include "compiler/qbs/serializer.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::compiler::qbs::BuildMode;
using quarry::compiler::qbs::DescriptorKind;
using quarry::compiler::qbs::QbsFieldModel;
using quarry::compiler::qbs::QbsImageModel;
using quarry::compiler::qbs::QbsRecordModel;
using quarry::compiler::qbs::QbsTypeModel;
using quarry::compiler::qbs::Storage;
using quarry::compiler::qbs::TypeCode;

QbsImageModel example(BuildMode mode) {
    QbsImageModel model;
    model.mode = mode;
    model.schema_identity_input = {'a', 'b', 'c'};
    model.records = {QbsRecordModel{.table_index = 0U,
                                    .record_id = 1U,
                                    .field_start = 0U,
                                    .field_count = 4U,
                                    .variable_size = true,
                                    .presence_bitmap_size = 1U,
                                    .fixed_region_size = 23U,
                                    .complete_fixed_record_size = std::nullopt,
                                    .name_string_index = static_cast<std::uint16_t>(
                                        mode == BuildMode::Reflective ? 0U : 0xFFFFU),
                                    .fqn = "Example"}};
    model.fields = {
        QbsFieldModel{.owning_record_index = 0U,
                      .field_index = 0U,
                      .type_index = 1U,
                      .byte_offset = 17U,
                      .bit_offset = 0U,
                      .bit_width = 32U,
                      .presence_bit_index = 0U,
                      .slot_size = 4U,
                      .storage = Storage::Fixed,
                      .descriptor_kind = DescriptorKind::None,
                      .name_string_index =
                          static_cast<std::uint16_t>(mode == BuildMode::Reflective ? 4U : 0xFFFFU)},
        QbsFieldModel{.owning_record_index = 0U,
                      .field_index = 1U,
                      .type_index = 2U,
                      .byte_offset = 21U,
                      .bit_offset = 0U,
                      .bit_width = 64U,
                      .presence_bit_index = 1U,
                      .slot_size = 8U,
                      .storage = Storage::VariableDescriptor,
                      .descriptor_kind = DescriptorKind::DataOffsetByteLength,
                      .name_string_index =
                          static_cast<std::uint16_t>(mode == BuildMode::Reflective ? 1U : 0xFFFFU)},
        QbsFieldModel{.owning_record_index = 0U,
                      .field_index = 2U,
                      .type_index = 0U,
                      .byte_offset = 29U,
                      .bit_offset = 0U,
                      .bit_width = 16U,
                      .presence_bit_index = 2U,
                      .slot_size = 2U,
                      .storage = Storage::Fixed,
                      .descriptor_kind = DescriptorKind::None,
                      .name_string_index =
                          static_cast<std::uint16_t>(mode == BuildMode::Reflective ? 3U : 0xFFFFU)},
        QbsFieldModel{.owning_record_index = 0U,
                      .field_index = 3U,
                      .type_index = 3U,
                      .byte_offset = 31U,
                      .bit_offset = 0U,
                      .bit_width = 64U,
                      .presence_bit_index = 3U,
                      .slot_size = 8U,
                      .storage = Storage::VariableDescriptor,
                      .descriptor_kind = DescriptorKind::DataOffsetByteLength,
                      .name_string_index =
                          static_cast<std::uint16_t>(mode == BuildMode::Reflective ? 2U : 0xFFFFU)},
    };
    model.types = {
        QbsTypeModel{.code = TypeCode::U16, .fixed_size = true, .encoded_width = 2U},
        QbsTypeModel{.code = TypeCode::U32, .fixed_size = true, .encoded_width = 4U},
        QbsTypeModel{.code = TypeCode::String, .fixed_size = false, .max_bytes = 64U},
        QbsTypeModel{
            .code = TypeCode::Array, .fixed_size = false, .reference = 0U, .max_elements = 8U},
    };
    if (mode == BuildMode::Reflective) {
        model.strings = {"Example", "name", "samples", "state", "timestamp"};
    }
    return model;
}

QbsImageModel enum_image() {
    QbsImageModel model;
    model.schema_identity_input = {0x10U, 0x20U};
    model.records = {QbsRecordModel{.table_index = 0U,
                                    .record_id = 7U,
                                    .field_start = 0U,
                                    .field_count = 1U,
                                    .variable_size = false,
                                    .presence_bitmap_size = 1U,
                                    .fixed_region_size = 2U,
                                    .complete_fixed_record_size = 18U,
                                    .name_string_index = 0xFFFFU,
                                    .fqn = "Packet"}};
    model.fields = {QbsFieldModel{.owning_record_index = 0U,
                                  .field_index = 0U,
                                  .type_index = 0U,
                                  .byte_offset = 17U,
                                  .bit_offset = 0U,
                                  .bit_width = 8U,
                                  .presence_bit_index = 0U,
                                  .slot_size = 1U,
                                  .storage = Storage::Fixed,
                                  .descriptor_kind = DescriptorKind::None,
                                  .name_string_index = 0xFFFFU}};
    model.types = {QbsTypeModel{
        .code = TypeCode::Enum, .fixed_size = true, .encoded_width = 1U, .reference = 0U}};
    model.enums = {quarry::compiler::qbs::QbsEnumModel{.table_index = 0U,
                                                       .fqn = "State",
                                                       .encoded_width = 1U,
                                                       .value_start = 0U,
                                                       .values = {0U, 1U},
                                                       .name_string_index = 0xFFFFU}};
    model.enum_values = {0U, 1U};
    return model;
}

std::vector<std::uint8_t> bytes(std::initializer_list<unsigned> values) {
    return {values.begin(), values.end()};
}

std::vector<std::uint8_t> hex_bytes(std::string_view text) {
    std::vector<std::uint8_t> result;
    result.reserve(text.size() / 2U);
    for (std::size_t i = 0; i < text.size(); i += 2U) {
        const auto digit = [](char value) -> std::uint8_t {
            return static_cast<std::uint8_t>(value >= 'a' ? value - 'a' + 10 : value - '0');
        };
        result.push_back(static_cast<std::uint8_t>((digit(text[i]) << 4U) | digit(text[i + 1U])));
    }
    return result;
}

TEST(QbsSerializerTest, Sha256KnownAnswers) {
    const std::vector<std::uint8_t> empty;
    const std::vector<std::uint8_t> abc = {'a', 'b', 'c'};
    EXPECT_EQ(quarry::compiler::qbs::sha256(empty),
              (std::array<std::uint8_t, 32>{0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
                                            0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
                                            0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
                                            0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55}));
    EXPECT_EQ(quarry::compiler::qbs::sha256(abc),
              (std::array<std::uint8_t, 32>{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
                                            0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                                            0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
                                            0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}));
}

TEST(QbsSerializerTest, SerializesExampleMinimalImage) {
    DiagnosticCollection diagnostics;
    const auto result =
        quarry::compiler::qbs::serialize_qbs(example(BuildMode::Minimal), diagnostics);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(diagnostics.diagnostics().empty());
    ASSERT_EQ(result->bytes.size(), 301U);
    const auto expected =
        hex_bytes("514253000100002801010010ba7816bf8f01cfea414140de5dae222300040000000000280000012d"
                  "00010000000000580000001d00020000000000750000007000030000000000e50000004000060000"
                  "000001250000000800000001000000000004000100000001000000170000000000ffff0000000000"
                  "000000001100000000002000010000000000000004ffff0000000100060000001500000000004000"
                  "020001000000000008ffff0000000200000000001d00000000001000000002000000000002ffff00"
                  "00000300060000001f00000000004000030003000000000008ffff00000501000200000000000000"
                  "0000000000070100040000000000000000000000000d020000000000000000000000000040100200"
                  "000000000000000008000000004578616d706c6500");
    ASSERT_EQ(expected.size(), 301U);
    EXPECT_EQ(result->bytes, expected);
    EXPECT_EQ(std::vector<std::uint8_t>(result->bytes.begin(), result->bytes.begin() + 4),
              bytes({0x51, 0x42, 0x53, 0x00}));
    EXPECT_EQ(std::vector<std::uint8_t>(result->bytes.begin() + 4, result->bytes.begin() + 12),
              bytes({1, 0, 0, 40, 1, 1, 0, 16}));
    EXPECT_EQ(std::vector<std::uint8_t>(result->bytes.begin() + 12, result->bytes.begin() + 28),
              bytes({0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
                     0xae, 0x22, 0x23}));
    EXPECT_EQ(std::vector<std::uint8_t>(result->bytes.begin() + 28, result->bytes.begin() + 40),
              bytes({0, 4, 0, 0, 0, 0, 0, 40, 0, 0, 1, 45}));
}

TEST(QbsSerializerTest, ReflectiveImageHasCanonicalStringSectionButSameSchemaId) {
    DiagnosticCollection minimal_diagnostics;
    DiagnosticCollection reflective_diagnostics;
    const auto minimal =
        quarry::compiler::qbs::serialize_qbs(example(BuildMode::Minimal), minimal_diagnostics);
    const auto reflective = quarry::compiler::qbs::serialize_qbs(example(BuildMode::Reflective),
                                                                 reflective_diagnostics);
    ASSERT_TRUE(minimal.has_value());
    ASSERT_TRUE(reflective.has_value());
    ASSERT_EQ(minimal->bytes.size(), 301U);
    ASSERT_EQ(reflective->bytes.size(), 373U);
    EXPECT_EQ(
        std::vector<std::uint8_t>(minimal->bytes.begin() + 12, minimal->bytes.begin() + 28),
        std::vector<std::uint8_t>(reflective->bytes.begin() + 12, reflective->bytes.begin() + 28));
    EXPECT_EQ(std::vector<std::uint8_t>(reflective->bytes.end() - 60, reflective->bytes.end()),
              bytes({0,   0,   0,   5,   0,   0,   0,   0,   0,   0,   0,   7,   0,   0,   0,
                     11,  0,   0,   0,   18,  0,   0,   0,   23,  0,   0,   0,   32,  'E', 'x',
                     'a', 'm', 'p', 'l', 'e', 'n', 'a', 'm', 'e', 's', 'a', 'm', 'p', 'l', 'e',
                     's', 's', 't', 'a', 't', 'e', 't', 'i', 'm', 'e', 's', 't', 'a', 'm', 'p'}));
}

TEST(QbsSerializerTest, RejectsInvalidTableReference) {
    auto model = example(BuildMode::Minimal);
    model.fields[0].type_index = 99U;
    DiagnosticCollection diagnostics;
    EXPECT_FALSE(quarry::compiler::qbs::serialize_qbs(model, diagnostics).has_value());
    EXPECT_FALSE(diagnostics.diagnostics().empty());
}

TEST(QbsSerializerTest, RejectsInvalidFieldEnumValues) {
    auto invalid_storage = example(BuildMode::Minimal);
    invalid_storage.fields[0].storage = static_cast<Storage>(99U);
    DiagnosticCollection storage_diagnostics;
    EXPECT_FALSE(
        quarry::compiler::qbs::serialize_qbs(invalid_storage, storage_diagnostics).has_value());
    EXPECT_FALSE(storage_diagnostics.diagnostics().empty());

    auto invalid_descriptor = example(BuildMode::Minimal);
    invalid_descriptor.fields[0].descriptor_kind = static_cast<DescriptorKind>(99U);
    DiagnosticCollection descriptor_diagnostics;
    EXPECT_FALSE(quarry::compiler::qbs::serialize_qbs(invalid_descriptor, descriptor_diagnostics)
                     .has_value());
    EXPECT_FALSE(descriptor_diagnostics.diagnostics().empty());
}

TEST(QbsSerializerTest, RejectsNonCanonicalTypeOrdering) {
    auto stale = example(BuildMode::Minimal);
    std::swap(stale.types[0], stale.types[1]);
    DiagnosticCollection stale_diagnostics;
    EXPECT_FALSE(quarry::compiler::qbs::serialize_qbs(stale, stale_diagnostics).has_value());

    auto adjusted = example(BuildMode::Minimal);
    std::swap(adjusted.types[0], adjusted.types[1]);
    adjusted.fields[0].type_index = 0U;
    adjusted.fields[2].type_index = 1U;
    DiagnosticCollection adjusted_diagnostics;
    EXPECT_FALSE(quarry::compiler::qbs::serialize_qbs(adjusted, adjusted_diagnostics).has_value());
}

TEST(QbsSerializerTest, ChoosesCanonicalIdentityOffsetWidthAtPayloadBoundaries) {
    const auto width_for_name_length = [](std::size_t length) {
        auto model = example(BuildMode::Minimal);
        model.records[0].fqn.assign(length, 'R');
        DiagnosticCollection diagnostics;
        const auto result = quarry::compiler::qbs::serialize_qbs(model, diagnostics);
        EXPECT_TRUE(result.has_value());
        EXPECT_TRUE(diagnostics.diagnostics().empty());
        return result.has_value() ? result->bytes[9] : 0U;
    };
    EXPECT_EQ(width_for_name_length(255U), 1U);   // ISS payload 256
    EXPECT_EQ(width_for_name_length(256U), 2U);   // ISS payload 257
    EXPECT_EQ(width_for_name_length(65535U), 2U); // ISS payload 65536
    EXPECT_EQ(width_for_name_length(65536U), 4U); // ISS payload 65537
}

TEST(QbsSerializerTest, EmitsCanonicalIdentitySectionForMinimalAndEnumImages) {
    DiagnosticCollection example_diagnostics;
    const auto example_result =
        quarry::compiler::qbs::serialize_qbs(example(BuildMode::Minimal), example_diagnostics);
    ASSERT_TRUE(example_result.has_value());
    EXPECT_EQ(
        std::vector<std::uint8_t>(example_result->bytes.end() - 8, example_result->bytes.end()),
        (std::vector<std::uint8_t>{'E', 'x', 'a', 'm', 'p', 'l', 'e', 0U}));

    DiagnosticCollection enum_diagnostics;
    const auto enum_result = quarry::compiler::qbs::serialize_qbs(enum_image(), enum_diagnostics);
    ASSERT_TRUE(enum_result.has_value());
    EXPECT_EQ(
        std::vector<std::uint8_t>(enum_result->bytes.end() - 13, enum_result->bytes.end()),
        (std::vector<std::uint8_t>{'P', 'a', 'c', 'k', 'e', 't', 0U, 'S', 't', 'a', 't', 'e', 0U}));
}

TEST(QbsSerializerTest, SerializesSharedEnumTableAndRejectsNonCanonicalValues) {
    DiagnosticCollection diagnostics;
    const auto result = quarry::compiler::qbs::serialize_qbs(enum_image(), diagnostics);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(diagnostics.diagnostics().empty());
    EXPECT_EQ(result->bytes.size(), 231U);

    auto invalid = enum_image();
    invalid.enums[0].values = {1U, 0U};
    DiagnosticCollection invalid_diagnostics;
    EXPECT_FALSE(quarry::compiler::qbs::serialize_qbs(invalid, invalid_diagnostics).has_value());
    EXPECT_FALSE(invalid_diagnostics.diagnostics().empty());
}

TEST(QbsSerializerTest, RepeatedSerializationIsByteDeterministic) {
    DiagnosticCollection first_diagnostics;
    DiagnosticCollection second_diagnostics;
    const auto first =
        quarry::compiler::qbs::serialize_qbs(example(BuildMode::Minimal), first_diagnostics);
    const auto second =
        quarry::compiler::qbs::serialize_qbs(example(BuildMode::Minimal), second_diagnostics);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->bytes, second->bytes);
}

} // namespace
