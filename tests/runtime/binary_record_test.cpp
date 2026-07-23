#include "runtime/binary_record.hpp"
#include "runtime/version.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#ifndef QUARRY_TEST_GENERATED_CODE_API_VERSION
#error "QUARRY_TEST_GENERATED_CODE_API_VERSION must be defined"
#endif

static_assert(::quarry::runtime::kGeneratedCodeApiVersion ==
              QUARRY_TEST_GENERATED_CODE_API_VERSION);

namespace {

using quarry::runtime::FieldBytes;
using quarry::runtime::DecodeError;
using quarry::runtime::EncodeError;
using quarry::runtime::append_bool;
using quarry::runtime::append_bytes;
using quarry::runtime::append_f32;
using quarry::runtime::append_f64;
using quarry::runtime::append_i16;
using quarry::runtime::append_i32;
using quarry::runtime::append_string_utf8;
using quarry::runtime::append_u32;
using quarry::runtime::append_varuint;
using quarry::runtime::decoded_value;
using quarry::runtime::decode_failure;
using quarry::runtime::encode_record;
using quarry::runtime::encode_record_result;
using quarry::runtime::encode_success;
using quarry::runtime::encode_failure;
using quarry::runtime::find_field;
using quarry::runtime::is_valid_utf8;
using quarry::runtime::parse_record;
using quarry::runtime::read_bytes;
using quarry::runtime::read_bool;
using quarry::runtime::read_f32;
using quarry::runtime::read_f64;
using quarry::runtime::read_i16;
using quarry::runtime::read_i32;
using quarry::runtime::read_string_utf8;
using quarry::runtime::read_u32;
using quarry::runtime::read_varuint;

[[nodiscard]] std::byte b(unsigned int value) {
    return static_cast<std::byte>(static_cast<std::uint8_t>(value));
}

template <typename T>
[[nodiscard]] const T& require_value(const std::optional<T>& value) {
    if (!value.has_value()) {
        std::abort();
    }
    return value.value();
}

TEST(BinaryRecordRuntimeTest, EncodesEmptyTopLevelRecordHeader) {
    const std::optional<std::vector<std::byte>> encoded = encode_record(1U, {});

    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(require_value(encoded), (std::vector<std::byte>{
                                          b(0x01), b(0x00), b(0x00), b(0x00), b(0x00),
                                          b(0x00), b(0x00), b(0x01), b(0x00), b(0x00),
                                          b(0x00), b(0x00), b(0x00), b(0x00), b(0x00),
                                          b(0x00),
                                      }));
}

TEST(BinaryRecordRuntimeTest, EncodesDirectoryAndPayloadForOneField) {
    const std::vector<FieldBytes> fields{FieldBytes{
        .field_index = 1U,
        .bytes = {b(0xDE), b(0xAD), b(0xBE), b(0xEF)},
    }};

    const std::optional<std::vector<std::byte>> encoded = encode_record(0x01020304U, fields);

    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(require_value(encoded), (std::vector<std::byte>{
                                          b(0x01), b(0x00), b(0x01), b(0x00), b(0x01),
                                          b(0x02), b(0x03), b(0x04), b(0x00), b(0x00),
                                          b(0x00), b(0x00), b(0x00), b(0x00), b(0x00),
                                          b(0x07), b(0x01), b(0x00), b(0x04), b(0xDE),
                                          b(0xAD), b(0xBE), b(0xEF),
                                      }));
}

TEST(BinaryRecordRuntimeTest, SortsDirectoryByFieldIndexAndUsesSortedPayloadOrder) {
    const std::vector<FieldBytes> fields{
        FieldBytes{.field_index = 2U, .bytes = {b(0xAA)}},
        FieldBytes{.field_index = 0U, .bytes = {b(0xBB), b(0xCC)}},
    };

    const std::optional<std::vector<std::byte>> encoded = encode_record(1U, fields);

    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(require_value(encoded), (std::vector<std::byte>{
                                          b(0x01), b(0x00), b(0x02), b(0x00), b(0x00),
                                          b(0x00), b(0x00), b(0x01), b(0x00), b(0x00),
                                          b(0x00), b(0x00), b(0x00), b(0x00), b(0x00),
                                          b(0x09), b(0x00), b(0x00), b(0x02), b(0x02),
                                          b(0x02), b(0x01), b(0xBB), b(0xCC), b(0xAA),
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

TEST(BinaryRecordRuntimeTest, ReportsStructuredEncodeResults) {
    const auto success = encode_record_result(1U, {});
    ASSERT_TRUE(success.has_value());
    EXPECT_EQ(success.error, EncodeError::none);
    ASSERT_TRUE(success.value.has_value());
    EXPECT_EQ(require_value(success.value), (std::vector<std::byte>{
                                                b(0x01), b(0x00), b(0x00), b(0x00),
                                                b(0x00), b(0x00), b(0x00), b(0x01),
                                                b(0x00), b(0x00), b(0x00), b(0x00),
                                                b(0x00), b(0x00), b(0x00), b(0x00),
                                            }));

    const std::vector<FieldBytes> duplicate_fields{
        FieldBytes{.field_index = 1U, .bytes = {b(0x01)}},
        FieldBytes{.field_index = 1U, .bytes = {b(0x02)}},
    };
    const auto duplicate = encode_record_result(1U, duplicate_fields);
    EXPECT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error, EncodeError::overflow);

    const auto invalid_record_id = encode_record_result(0U, {});
    EXPECT_FALSE(invalid_record_id.has_value());
    EXPECT_EQ(invalid_record_id.error, EncodeError::overflow);
}

TEST(BinaryRecordRuntimeTest, CodecResultHelpersReportSuccessAndFailure) {
    auto encoded = encode_success<std::vector<std::byte>>(std::vector<std::byte>{b(0xAA)});
    ASSERT_TRUE(encoded);
    EXPECT_EQ(encoded.error, EncodeError::none);
    ASSERT_TRUE(encoded.value.has_value());
    EXPECT_EQ(require_value(encoded.value), (std::vector<std::byte>{b(0xAA)}));

    auto encode_error =
        encode_failure<std::vector<std::byte>>(EncodeError::unsupported_field_type);
    EXPECT_FALSE(encode_error);
    EXPECT_FALSE(encode_error.value.has_value());
    EXPECT_EQ(encode_error.error, EncodeError::unsupported_field_type);

    auto decoded = decoded_value<std::vector<std::byte>>(std::vector<std::byte>{b(0xBB)});
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.error, DecodeError::none);
    ASSERT_TRUE(decoded.value.has_value());
    EXPECT_EQ(require_value(decoded.value), (std::vector<std::byte>{b(0xBB)}));

    auto decode_error = decode_failure<std::vector<std::byte>>(DecodeError::invalid_utf8);
    EXPECT_FALSE(decode_error);
    EXPECT_FALSE(decode_error.value.has_value());
    EXPECT_EQ(decode_error.error, DecodeError::invalid_utf8);
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

TEST(BinaryRecordRuntimeTest, EncodesRawBytesAndUtf8Strings) {
    std::vector<std::byte> output;
    const std::vector<std::byte> arbitrary_bytes{b(0x00), b(0xFF), b(0x80)};

    ASSERT_TRUE(append_bytes(output, arbitrary_bytes));
    ASSERT_TRUE(append_string_utf8(output, std::string_view("hi\0", 3U)));
    ASSERT_TRUE(append_string_utf8(output, std::string_view("\xC2\xA2\xE2\x82\xAC\xF0\x9F\x98\x80", 9U)));
    EXPECT_FALSE(append_string_utf8(output, std::string_view("\xC0\x80", 2U)));

    EXPECT_EQ(output, (std::vector<std::byte>{
                          b(0x00), b(0xFF), b(0x80), b('h'), b('i'), b(0x00), b(0xC2),
                          b(0xA2), b(0xE2), b(0x82), b(0xAC), b(0xF0), b(0x9F), b(0x98),
                          b(0x80),
                      }));
}

TEST(BinaryRecordRuntimeTest, ValidatesUtf8Precisely) {
    EXPECT_TRUE(is_valid_utf8({}));
    EXPECT_TRUE(is_valid_utf8(std::as_bytes(std::span<const char>("ascii", 5U))));
    EXPECT_TRUE(is_valid_utf8(std::as_bytes(std::span<const char>("\0", 1U))));
    EXPECT_TRUE(is_valid_utf8(std::as_bytes(std::span<const char>("\xC2\xA2", 2U))));
    EXPECT_TRUE(is_valid_utf8(std::as_bytes(std::span<const char>("\xE2\x82\xAC", 3U))));
    EXPECT_TRUE(is_valid_utf8(std::as_bytes(std::span<const char>("\xF0\x9F\x98\x80", 4U))));

    EXPECT_FALSE(is_valid_utf8(std::as_bytes(std::span<const char>("\x80", 1U))));
    EXPECT_FALSE(is_valid_utf8(std::as_bytes(std::span<const char>("\xC2", 1U))));
    EXPECT_FALSE(is_valid_utf8(std::as_bytes(std::span<const char>("\xC2\x20", 2U))));
    EXPECT_FALSE(is_valid_utf8(std::as_bytes(std::span<const char>("\xC0\x80", 2U))));
    EXPECT_FALSE(is_valid_utf8(std::as_bytes(std::span<const char>("\xE0\x80\x80", 3U))));
    EXPECT_FALSE(is_valid_utf8(std::as_bytes(std::span<const char>("\xED\xA0\x80", 3U))));
    EXPECT_FALSE(is_valid_utf8(std::as_bytes(std::span<const char>("\xF4\x90\x80\x80", 4U))));
}

TEST(BinaryRecordRuntimeTest, ParsesValidEmptyRecord) {
    const std::vector<std::byte> input{
        b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x2A),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00),
    };

    const auto parsed = parse_record(input);

    ASSERT_TRUE(parsed.record.has_value());
    const auto& record = require_value(parsed.record);
    EXPECT_EQ(record.record_id, 42U);
    EXPECT_TRUE(record.fields.empty());
    EXPECT_EQ(parsed.error, DecodeError::none);
}

TEST(BinaryRecordRuntimeTest, ParsesDirectoryAndFindsFields) {
    const std::vector<std::byte> input{
        b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x09),
        b(0x00), b(0x01), b(0x01), b(0x02), b(0x00), b(0x01), b(0xBB), b(0xAA),
        b(0xCC),
    };

    const auto parsed = parse_record(input);

    ASSERT_TRUE(parsed.record.has_value());
    const auto& record = require_value(parsed.record);
    const auto* first = find_field(record, 0U);
    const auto* second = find_field(record, 2U);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->bytes.size(), 1U);
    EXPECT_EQ(first->bytes[0], b(0xAA));
    EXPECT_EQ(second->bytes.size(), 1U);
    EXPECT_EQ(second->bytes[0], b(0xBB));
    EXPECT_EQ(find_field(record, 1U), nullptr);
}

TEST(BinaryRecordRuntimeTest, ParsesZeroLengthFieldData) {
    const std::vector<std::byte> input{
        b(0x01), b(0x00), b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x03),
        b(0x00), b(0x00), b(0x00),
    };

    const auto parsed = parse_record(input);

    ASSERT_TRUE(parsed.record.has_value());
    const auto& record = require_value(parsed.record);
    const auto* field = find_field(record, 0U);
    ASSERT_NE(field, nullptr);
    EXPECT_TRUE(field->bytes.empty());
}

TEST(BinaryRecordRuntimeTest, RejectsMalformedRecordStructure) {
    const std::vector<std::byte> valid_empty{
        b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00),
    };

    EXPECT_EQ(parse_record(std::span<const std::byte>(valid_empty).first(15U)).error,
              DecodeError::truncated_header);

    std::vector<std::byte> unsupported_version = valid_empty;
    unsupported_version[0] = b(0x02);
    EXPECT_EQ(parse_record(unsupported_version).error, DecodeError::unsupported_version);

    std::vector<std::byte> nonzero_flags = valid_empty;
    nonzero_flags[1] = b(0x01);
    EXPECT_EQ(parse_record(nonzero_flags).error, DecodeError::unsupported_flags);

    std::vector<std::byte> nonzero_reserved = valid_empty;
    nonzero_reserved[3] = b(0x01);
    EXPECT_EQ(parse_record(nonzero_reserved).error, DecodeError::invalid_header);

    std::vector<std::byte> too_long_payload = valid_empty;
    too_long_payload[15] = b(0x01);
    EXPECT_EQ(parse_record(too_long_payload).error, DecodeError::invalid_payload_length);

    std::vector<std::byte> trailing = valid_empty;
    trailing.push_back(b(0x00));
    EXPECT_EQ(parse_record(trailing).error, DecodeError::invalid_payload_length);
}

TEST(BinaryRecordRuntimeTest, RejectsMalformedDirectoryAndRanges) {
    const auto expect_error = [](std::vector<std::byte> input, DecodeError error) {
        EXPECT_EQ(parse_record(input).error, error);
    };

    expect_error({
                     b(0x01), b(0x00), b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
                     b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
                     b(0x00),
                 },
                 DecodeError::malformed_varuint);

    expect_error({
                     b(0x01), b(0x00), b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
                     b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x0C),
                     b(0x00), b(0x80), b(0x80), b(0x80), b(0x80), b(0x80), b(0x80), b(0x80),
                     b(0x80), b(0x80), b(0x80), b(0x00),
                 },
                 DecodeError::malformed_varuint);

    expect_error({
                     b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
                     b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x07),
                     b(0x01), b(0x00), b(0x00), b(0x01), b(0x00), b(0x00), b(0xAA),
                 },
                 DecodeError::duplicate_field);

    expect_error({
                     b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
                     b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x07),
                     b(0x02), b(0x00), b(0x00), b(0x01), b(0x00), b(0x00), b(0xAA),
                 },
                 DecodeError::unsorted_directory);

    expect_error({
                     b(0x01), b(0x00), b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
                     b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x04),
                     b(0x00), b(0x01), b(0x04), b(0xAA),
                 },
                 DecodeError::invalid_field_range);

    expect_error({
                     b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
                     b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x0A),
                     b(0x00), b(0x00), b(0x02), b(0x01), b(0x01), b(0x02), b(0xAA), b(0xBB),
                     b(0xCC), b(0xDD),
                 },
                 DecodeError::overlapping_field_range);
}

TEST(BinaryRecordRuntimeTest, RejectsOverflowingVaruintDirectly) {
    const std::vector<std::byte> input{
        b(0xFF), b(0xFF), b(0xFF), b(0xFF), b(0xFF),
        b(0xFF), b(0xFF), b(0xFF), b(0xFF), b(0x02),
    };
    std::size_t offset = 0U;

    const auto decoded = read_varuint(input, offset);

    EXPECT_FALSE(decoded.value.has_value());
    EXPECT_EQ(decoded.error, DecodeError::malformed_varuint);
}

TEST(BinaryRecordRuntimeTest, RejectsDirectoryCountWithoutEnoughBytes) {
    const std::vector<std::byte> input{
        b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x03),
        b(0x00), b(0x00), b(0x00),
    };

    EXPECT_EQ(parse_record(input).error, DecodeError::malformed_directory);
}

TEST(BinaryRecordRuntimeTest, RejectsMaximumOffsetPlusLengthRange) {
    const std::vector<std::byte> input{
        b(0x01), b(0x00), b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x0C),
        b(0x00), b(0xFF), b(0xFF), b(0xFF), b(0xFF), b(0xFF), b(0xFF), b(0xFF),
        b(0xFF), b(0xFF), b(0x01), b(0x01),
    };

    EXPECT_EQ(parse_record(input).error, DecodeError::invalid_field_range);
}

TEST(BinaryRecordRuntimeTest, AllowsDistinctZeroLengthFieldsAtSameOffset) {
    const std::vector<std::byte> input{
        b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x06),
        b(0x00), b(0x00), b(0x00), b(0x01), b(0x00), b(0x00),
    };

    const auto parsed = parse_record(input);

    ASSERT_TRUE(parsed.record.has_value());
    const auto& record = require_value(parsed.record);
    ASSERT_EQ(record.fields.size(), 2U);
    EXPECT_TRUE(record.fields[0].bytes.empty());
    EXPECT_TRUE(record.fields[1].bytes.empty());
}

TEST(BinaryRecordRuntimeTest, ReportsByteOffsetForTopLevelHeaderFailures) {
    const std::vector<std::byte> valid_empty{
        b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00),
    };

    const auto truncated = parse_record(std::span<const std::byte>(valid_empty).first(15U));
    EXPECT_EQ(truncated.error, DecodeError::truncated_header);
    ASSERT_TRUE(truncated.offset.has_value());
    EXPECT_EQ(*truncated.offset, 0U);

    std::vector<std::byte> unsupported_version = valid_empty;
    unsupported_version[0] = b(0x02);
    const auto version_result = parse_record(unsupported_version);
    EXPECT_EQ(version_result.error, DecodeError::unsupported_version);
    ASSERT_TRUE(version_result.offset.has_value());
    EXPECT_EQ(*version_result.offset, 0U);

    std::vector<std::byte> nonzero_flags = valid_empty;
    nonzero_flags[1] = b(0x01);
    const auto flags_result = parse_record(nonzero_flags);
    EXPECT_EQ(flags_result.error, DecodeError::unsupported_flags);
    ASSERT_TRUE(flags_result.offset.has_value());
    EXPECT_EQ(*flags_result.offset, 1U);

    std::vector<std::byte> nonzero_reserved0 = valid_empty;
    nonzero_reserved0[3] = b(0x01);
    const auto reserved0_result = parse_record(nonzero_reserved0);
    EXPECT_EQ(reserved0_result.error, DecodeError::invalid_header);
    ASSERT_TRUE(reserved0_result.offset.has_value());
    EXPECT_EQ(*reserved0_result.offset, 3U);

    std::vector<std::byte> nonzero_reserved1 = valid_empty;
    nonzero_reserved1[8] = b(0x01);
    const auto reserved1_result = parse_record(nonzero_reserved1);
    EXPECT_EQ(reserved1_result.error, DecodeError::invalid_header);
    ASSERT_TRUE(reserved1_result.offset.has_value());
    EXPECT_EQ(*reserved1_result.offset, 8U);

    std::vector<std::byte> too_long_payload = valid_empty;
    too_long_payload[15] = b(0x01);
    const auto too_long_result = parse_record(too_long_payload);
    EXPECT_EQ(too_long_result.error, DecodeError::invalid_payload_length);
    ASSERT_TRUE(too_long_result.offset.has_value());
    EXPECT_EQ(*too_long_result.offset, 12U);

    std::vector<std::byte> trailing = valid_empty;
    trailing.push_back(b(0x00));
    const auto trailing_result = parse_record(trailing);
    EXPECT_EQ(trailing_result.error, DecodeError::invalid_payload_length);
    ASSERT_TRUE(trailing_result.offset.has_value());
    EXPECT_EQ(*trailing_result.offset, 12U);
}

TEST(BinaryRecordRuntimeTest, ReportsByteOffsetForFieldDirectoryAndVaruintFailures) {
    const auto truncated_field_offset_varuint = parse_record(std::vector<std::byte>{
        b(0x01), b(0x00), b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00),
    });
    EXPECT_EQ(truncated_field_offset_varuint.error, DecodeError::malformed_varuint);
    ASSERT_TRUE(truncated_field_offset_varuint.offset.has_value());
    EXPECT_EQ(*truncated_field_offset_varuint.offset, 17U);

    const auto overflowing_field_offset_varuint = parse_record(std::vector<std::byte>{
        b(0x01), b(0x00), b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x0C),
        b(0x00), b(0x80), b(0x80), b(0x80), b(0x80), b(0x80), b(0x80), b(0x80),
        b(0x80), b(0x80), b(0x80), b(0x00),
    });
    EXPECT_EQ(overflowing_field_offset_varuint.error, DecodeError::malformed_varuint);
    ASSERT_TRUE(overflowing_field_offset_varuint.offset.has_value());
    EXPECT_EQ(*overflowing_field_offset_varuint.offset, 17U);

    const auto duplicate = parse_record(std::vector<std::byte>{
        b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x07),
        b(0x01), b(0x00), b(0x00), b(0x01), b(0x00), b(0x00), b(0xAA),
    });
    EXPECT_EQ(duplicate.error, DecodeError::duplicate_field);
    ASSERT_TRUE(duplicate.offset.has_value());
    EXPECT_EQ(*duplicate.offset, 19U);

    const auto unsorted = parse_record(std::vector<std::byte>{
        b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x07),
        b(0x02), b(0x00), b(0x00), b(0x01), b(0x00), b(0x00), b(0xAA),
    });
    EXPECT_EQ(unsorted.error, DecodeError::unsorted_directory);
    ASSERT_TRUE(unsorted.offset.has_value());
    EXPECT_EQ(*unsorted.offset, 19U);

    const auto missing_directory_bytes = parse_record(std::vector<std::byte>{
        b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x03),
        b(0x00), b(0x00), b(0x00),
    });
    EXPECT_EQ(missing_directory_bytes.error, DecodeError::malformed_directory);
    ASSERT_TRUE(missing_directory_bytes.offset.has_value());
    EXPECT_EQ(*missing_directory_bytes.offset, 19U);
}

TEST(BinaryRecordRuntimeTest, ReportsByteOffsetForFieldRangeFailures) {
    const auto out_of_range = parse_record(std::vector<std::byte>{
        b(0x01), b(0x00), b(0x01), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x04),
        b(0x00), b(0x01), b(0x04), b(0xAA),
    });
    EXPECT_EQ(out_of_range.error, DecodeError::invalid_field_range);
    ASSERT_TRUE(out_of_range.offset.has_value());
    EXPECT_EQ(*out_of_range.offset, 20U);

    const auto overlapping = parse_record(std::vector<std::byte>{
        b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x0A),
        b(0x00), b(0x00), b(0x02), b(0x01), b(0x01), b(0x02), b(0xAA), b(0xBB),
        b(0xCC), b(0xDD),
    });
    EXPECT_EQ(overlapping.error, DecodeError::overlapping_field_range);
    ASSERT_TRUE(overlapping.offset.has_value());
    EXPECT_EQ(*overlapping.offset, 23U);
}

TEST(BinaryRecordRuntimeTest, PopulatesFieldViewOffsetOnSuccessfulParse) {
    const std::vector<std::byte> input{
        b(0x01), b(0x00), b(0x02), b(0x00), b(0x00), b(0x00), b(0x00), b(0x01),
        b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x00), b(0x09),
        b(0x00), b(0x01), b(0x01), b(0x02), b(0x00), b(0x01), b(0xBB), b(0xAA),
        b(0xCC),
    };

    const auto parsed = parse_record(input);

    ASSERT_TRUE(parsed.record.has_value());
    const auto& record = require_value(parsed.record);
    const auto* first = find_field(record, 0U);
    const auto* second = find_field(record, 2U);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    // Directory: fieldIndex(1) + offset(1) + length(1) per entry, 2 entries = 6 bytes.
    // Payload region starts at 16 (header) + 6 (directory) = 22.
    // Field 0 claims payload offset 1 (its bytes live at absolute 23).
    // Field 2 claims payload offset 0 (its bytes live at absolute 22).
    EXPECT_EQ(first->field_offset, 23U);
    EXPECT_EQ(second->field_offset, 22U);
}

TEST(BinaryRecordRuntimeTest, DecodesScalarValuesBigEndian) {
    const std::vector<std::byte> false_value{b(0x00)};
    const std::vector<std::byte> true_value{b(0x01)};
    const std::vector<std::byte> invalid_bool{b(0x02)};
    const std::vector<std::byte> negative_i16{b(0xFF), b(0xFE)};
    const std::vector<std::byte> negative_i32{b(0xFF), b(0xFF), b(0xFF), b(0xFF)};
    const std::vector<std::byte> positive_u32{b(0x01), b(0x02), b(0x03), b(0x04)};
    const std::vector<std::byte> f32_value{b(0x3F), b(0xC0), b(0x00), b(0x00)};
    const std::vector<std::byte> f64_value{b(0xC0), b(0x00), b(0x00), b(0x00),
                                           b(0x00), b(0x00), b(0x00), b(0x00)};
    const std::vector<std::byte> short_u32{b(0x01)};

    EXPECT_EQ(read_bool(false_value).value, false);
    EXPECT_EQ(read_bool(true_value).value, true);
    EXPECT_EQ(read_bool(invalid_bool).error, DecodeError::invalid_bool);
    EXPECT_EQ(read_i16(negative_i16).value, static_cast<std::int16_t>(-2));
    EXPECT_EQ(read_i32(negative_i32).value, static_cast<std::int32_t>(-1));
    EXPECT_EQ(read_u32(positive_u32).value, 0x01020304U);
    EXPECT_EQ(read_f32(f32_value).value, 1.5F);
    EXPECT_EQ(read_f64(f64_value).value, -2.0);
    EXPECT_EQ(read_u32(short_u32).error, DecodeError::invalid_field_length);
}

TEST(BinaryRecordRuntimeTest, DecodesRawBytesAndUtf8Strings) {
    const std::vector<std::byte> bytes{b(0x00), b(0xFF), b(0x80)};
    const std::vector<std::byte> text{b('h'), b('i'), b(0x00), b(0xC2), b(0xA2)};
    const std::vector<std::byte> invalid_text{b(0xED), b(0xA0), b(0x80)};

    const auto decoded_bytes = read_bytes(bytes);
    ASSERT_TRUE(decoded_bytes.value.has_value());
    EXPECT_EQ(require_value(decoded_bytes.value), bytes);

    const auto decoded_text = read_string_utf8(text);
    ASSERT_TRUE(decoded_text.value.has_value());
    EXPECT_EQ(require_value(decoded_text.value), std::string("hi\0\xC2\xA2", 5U));
    EXPECT_EQ(read_string_utf8(invalid_text).error, DecodeError::invalid_utf8);
}

} // namespace
