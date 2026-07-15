#include "runtime/binary_record.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::runtime::FieldBytes;
using breadcrumbs::runtime::append_bool;
using breadcrumbs::runtime::append_f32;
using breadcrumbs::runtime::append_f64;
using breadcrumbs::runtime::append_i16;
using breadcrumbs::runtime::append_i32;
using breadcrumbs::runtime::append_u32;
using breadcrumbs::runtime::append_varuint;
using breadcrumbs::runtime::encode_record;

[[nodiscard]] std::byte b(unsigned int value) {
    return static_cast<std::byte>(static_cast<std::uint8_t>(value));
}

TEST(BinaryRecordRuntimeTest, EncodesEmptyTopLevelRecordHeader) {
    const std::optional<std::vector<std::byte>> encoded = encode_record(1U, {});

    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, (std::vector<std::byte>{
                            b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00),
                            b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00),
                            b(0x00), b(0x00),
                        }));
}

TEST(BinaryRecordRuntimeTest, EncodesDirectoryAndPayloadForOneField) {
    const std::vector<FieldBytes> fields{FieldBytes{
        .field_index = 1U,
        .bytes = {b(0xDE), b(0xAD), b(0xBE), b(0xEF)},
    }};

    const std::optional<std::vector<std::byte>> encoded = encode_record(0x01020304U, fields);

    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, (std::vector<std::byte>{
                            b(0x01), b(0x00), b(0x01), b(0x00), b(0x01), b(0x02), b(0x03),
                            b(0x04), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00),
                            b(0x00), b(0x07), b(0x01), b(0x00), b(0x04), b(0xDE), b(0xAD),
                            b(0xBE), b(0xEF),
                        }));
}

TEST(BinaryRecordRuntimeTest, SortsDirectoryByFieldIndexAndUsesSortedPayloadOrder) {
    const std::vector<FieldBytes> fields{
        FieldBytes{.field_index = 2U, .bytes = {b(0xAA)}},
        FieldBytes{.field_index = 0U, .bytes = {b(0xBB), b(0xCC)}},
    };

    const std::optional<std::vector<std::byte>> encoded = encode_record(1U, fields);

    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, (std::vector<std::byte>{
                            b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00),
                            b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00),
                            b(0x00), b(0x09), b(0x00), b(0x00), b(0x02), b(0x02), b(0x02),
                            b(0x01), b(0xBB), b(0xCC), b(0xAA),
                        }));
}

TEST(BinaryRecordRuntimeTest, RejectsDuplicateFieldIndexesAndInvalidRecordId) {
    const std::vector<FieldBytes> duplicate_fields{
        FieldBytes{.field_index = 1U, .bytes = {b(0x01)}},
        FieldBytes{.field_index = 1U, .bytes = {b(0x02)}},
    };

    EXPECT_FALSE(encode_record(1U, duplicate_fields).has_value());
    EXPECT_FALSE(encode_record(0U, {}).has_value());
}

TEST(BinaryRecordRuntimeTest, RejectsTooManyPresentFields) {
    std::vector<FieldBytes> fields;
    for (std::uint16_t index = 0; index <= std::numeric_limits<std::uint8_t>::max(); ++index) {
        fields.push_back(FieldBytes{.field_index = static_cast<std::uint8_t>(index),
                                    .bytes = {b(0x00)}});
    }

    EXPECT_FALSE(encode_record(1U, fields).has_value());
}

TEST(BinaryRecordRuntimeTest, EncodesUnsignedLeb128Varuint) {
    std::vector<std::byte> output;

    append_varuint(output, 0U);
    append_varuint(output, 127U);
    append_varuint(output, 128U);
    append_varuint(output, 16'384U);

    EXPECT_EQ(output, (std::vector<std::byte>{b(0x00), b(0x7F), b(0x80), b(0x01), b(0x80),
                                             b(0x80), b(0x01)}));
}

TEST(BinaryRecordRuntimeTest, EncodesScalarValuesBigEndian) {
    std::vector<std::byte> output;

    ASSERT_TRUE(append_bool(output, false));
    ASSERT_TRUE(append_bool(output, true));
    append_i16(output, static_cast<std::int16_t>(-2));
    append_i32(output, static_cast<std::int32_t>(-1));
    append_u32(output, 0x01020304U);
    ASSERT_TRUE(append_f32(output, 1.5F));
    ASSERT_TRUE(append_f64(output, -2.0));

    EXPECT_EQ(output, (std::vector<std::byte>{
                          b(0x00), b(0x01), b(0xFF), b(0xFE), b(0xFF), b(0xFF), b(0xFF),
                          b(0xFF), b(0x01), b(0x02), b(0x03), b(0x04), b(0x3F), b(0xC0),
                          b(0x00), b(0x00), b(0xC0), b(0x00), b(0x00), b(0x00), b(0x00),
                          b(0x00), b(0x00), b(0x00),
                      }));
}

} // namespace
