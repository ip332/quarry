#include "quarry/runtime_c/binary_record.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#ifndef QUARRY_TEST_GENERATED_CODE_API_VERSION_C
#error "QUARRY_TEST_GENERATED_CODE_API_VERSION_C must be defined"
#endif

namespace {

TEST(BinaryRecordRuntimeCTest, GeneratedCodeApiVersionMatchesConfiguredScalar) {
    EXPECT_EQ(QUARRY_C_GENERATED_CODE_API_VERSION,
              static_cast<uint32_t>(QUARRY_TEST_GENERATED_CODE_API_VERSION_C));
}

TEST(BinaryRecordRuntimeCTest, WritesAndReadsBigEndianScalars) {
    uint8_t buffer[8];
    quarry_c_writer_t writer;

    quarry_c_writer_init(&writer, buffer, sizeof(buffer));
    ASSERT_EQ(quarry_c_write_u32(&writer, 0x01020304U), QUARRY_C_STATUS_OK);
    EXPECT_EQ(writer.length, 4U);
    EXPECT_EQ(buffer[0], 0x01U);
    EXPECT_EQ(buffer[1], 0x02U);
    EXPECT_EQ(buffer[2], 0x03U);
    EXPECT_EQ(buffer[3], 0x04U);

    quarry_c_reader_t reader;
    quarry_c_reader_init(&reader, buffer, 4U);
    uint32_t value = 0U;
    ASSERT_EQ(quarry_c_read_u32(&reader, &value), QUARRY_C_STATUS_OK);
    EXPECT_EQ(value, 0x01020304U);
}

TEST(BinaryRecordRuntimeCTest, RoundTripsEverySupportedScalarKind) {
    uint8_t buffer[8];
    quarry_c_writer_t writer;
    quarry_c_reader_t reader;

    quarry_c_writer_init(&writer, buffer, sizeof(buffer));
    ASSERT_EQ(quarry_c_write_bool(&writer, true), QUARRY_C_STATUS_OK);
    quarry_c_reader_init(&reader, buffer, writer.length);
    bool bool_value = false;
    ASSERT_EQ(quarry_c_read_bool(&reader, &bool_value), QUARRY_C_STATUS_OK);
    EXPECT_TRUE(bool_value);

    quarry_c_writer_init(&writer, buffer, sizeof(buffer));
    ASSERT_EQ(quarry_c_write_i8(&writer, -42), QUARRY_C_STATUS_OK);
    quarry_c_reader_init(&reader, buffer, writer.length);
    int8_t i8_value = 0;
    ASSERT_EQ(quarry_c_read_i8(&reader, &i8_value), QUARRY_C_STATUS_OK);
    EXPECT_EQ(i8_value, -42);

    quarry_c_writer_init(&writer, buffer, sizeof(buffer));
    ASSERT_EQ(quarry_c_write_i64(&writer, INT64_C(-9000000000000000000)), QUARRY_C_STATUS_OK);
    quarry_c_reader_init(&reader, buffer, writer.length);
    int64_t i64_value = 0;
    ASSERT_EQ(quarry_c_read_i64(&reader, &i64_value), QUARRY_C_STATUS_OK);
    EXPECT_EQ(i64_value, INT64_C(-9000000000000000000));

    quarry_c_writer_init(&writer, buffer, sizeof(buffer));
    ASSERT_EQ(quarry_c_write_u64(&writer, UINT64_C(18000000000000000000)), QUARRY_C_STATUS_OK);
    quarry_c_reader_init(&reader, buffer, writer.length);
    uint64_t u64_value = 0U;
    ASSERT_EQ(quarry_c_read_u64(&reader, &u64_value), QUARRY_C_STATUS_OK);
    EXPECT_EQ(u64_value, UINT64_C(18000000000000000000));

    quarry_c_writer_init(&writer, buffer, sizeof(buffer));
    ASSERT_EQ(quarry_c_write_f32(&writer, -1.5F), QUARRY_C_STATUS_OK);
    quarry_c_reader_init(&reader, buffer, writer.length);
    float f32_value = 0.0F;
    ASSERT_EQ(quarry_c_read_f32(&reader, &f32_value), QUARRY_C_STATUS_OK);
    EXPECT_FLOAT_EQ(f32_value, -1.5F);

    quarry_c_writer_init(&writer, buffer, sizeof(buffer));
    ASSERT_EQ(quarry_c_write_f64(&writer, 3.14159), QUARRY_C_STATUS_OK);
    quarry_c_reader_init(&reader, buffer, writer.length);
    double f64_value = 0.0;
    ASSERT_EQ(quarry_c_read_f64(&reader, &f64_value), QUARRY_C_STATUS_OK);
    EXPECT_DOUBLE_EQ(f64_value, 3.14159);
}

TEST(BinaryRecordRuntimeCTest, RejectsInvalidBoolByte) {
    const uint8_t buffer[] = {0x02U};
    quarry_c_reader_t reader;
    quarry_c_reader_init(&reader, buffer, sizeof(buffer));
    bool value = false;
    EXPECT_EQ(quarry_c_read_bool(&reader, &value), QUARRY_C_STATUS_INVALID_BOOL);
}

TEST(BinaryRecordRuntimeCTest, VaruintRoundTripsAndRejectsMalformedInput) {
    uint8_t buffer[16];
    quarry_c_writer_t writer;
    quarry_c_writer_init(&writer, buffer, sizeof(buffer));
    ASSERT_EQ(quarry_c_write_varuint(&writer, 300U), QUARRY_C_STATUS_OK);
    EXPECT_EQ(writer.length, quarry_c_varuint_encoded_size(300U));

    quarry_c_reader_t reader;
    quarry_c_reader_init(&reader, buffer, writer.length);
    uint64_t value = 0U;
    ASSERT_EQ(quarry_c_read_varuint(&reader, &value), QUARRY_C_STATUS_OK);
    EXPECT_EQ(value, 300U);

    // Continuation bit set on every byte, buffer exhausted before a
    // terminal byte -- malformed.
    const uint8_t malformed[] = {0x80U, 0x80U};
    quarry_c_reader_init(&reader, malformed, sizeof(malformed));
    EXPECT_EQ(quarry_c_read_varuint(&reader, &value), QUARRY_C_STATUS_MALFORMED_VARUINT);
}

TEST(BinaryRecordRuntimeCTest, EncodesRecordMatchingBrfWireFormatByteForByte) {
    uint8_t count_bytes[4];
    quarry_c_writer_t writer;
    quarry_c_writer_init(&writer, count_bytes, sizeof(count_bytes));
    ASSERT_EQ(quarry_c_write_u32(&writer, 42U), QUARRY_C_STATUS_OK);

    quarry_c_field_t fields[1];
    fields[0].field_index = 0U;
    fields[0].bytes = count_bytes;
    fields[0].length = 4U;

    uint8_t output[64];
    size_t out_length = 0U;
    ASSERT_EQ(quarry_c_encode_record(1U, fields, 1U, output, sizeof(output), &out_length),
             QUARRY_C_STATUS_OK);

    // Byte-for-byte reference captured from the C++ runtime encoding an
    // identical Sample{count=42} record (verified during PR-105/this PR).
    const uint8_t expected[] = {0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
                                0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x2a};
    ASSERT_EQ(out_length, sizeof(expected));
    EXPECT_EQ(0, std::memcmp(output, expected, sizeof(expected)));
}

TEST(BinaryRecordRuntimeCTest, EncodeSortsOutOfOrderFieldsAndRejectsDuplicates) {
    uint8_t a_bytes[4];
    uint8_t b_bytes[4];
    quarry_c_writer_t writer;
    quarry_c_writer_init(&writer, a_bytes, sizeof(a_bytes));
    ASSERT_EQ(quarry_c_write_u32(&writer, 1U), QUARRY_C_STATUS_OK);
    quarry_c_writer_init(&writer, b_bytes, sizeof(b_bytes));
    ASSERT_EQ(quarry_c_write_u32(&writer, 2U), QUARRY_C_STATUS_OK);

    quarry_c_field_t fields[2];
    fields[0].field_index = 1U;
    fields[0].bytes = b_bytes;
    fields[0].length = 4U;
    fields[1].field_index = 0U;
    fields[1].bytes = a_bytes;
    fields[1].length = 4U;

    uint8_t output[64];
    size_t out_length = 0U;
    ASSERT_EQ(quarry_c_encode_record(1U, fields, 2U, output, sizeof(output), &out_length),
             QUARRY_C_STATUS_OK);

    quarry_c_parsed_record_t parsed;
    size_t error_offset = 0U;
    ASSERT_EQ(quarry_c_parse_record(output, out_length, &parsed, &error_offset),
             QUARRY_C_STATUS_OK);
    ASSERT_EQ(parsed.entry_count, 2U);
    EXPECT_EQ(parsed.entries[0].field_index, 0U);
    EXPECT_EQ(parsed.entries[1].field_index, 1U);

    fields[1].field_index = 1U; // now duplicates fields[0]
    EXPECT_EQ(quarry_c_encode_record(1U, fields, 2U, output, sizeof(output), &out_length),
             QUARRY_C_STATUS_DUPLICATE_FIELD);
}

TEST(BinaryRecordRuntimeCTest, RejectsInsufficientOutputCapacity) {
    uint8_t count_bytes[4] = {0, 0, 0, 42};
    quarry_c_field_t fields[1];
    fields[0].field_index = 0U;
    fields[0].bytes = count_bytes;
    fields[0].length = 4U;

    uint8_t tiny[4];
    size_t out_length = 0U;
    EXPECT_EQ(quarry_c_encode_record(1U, fields, 1U, tiny, sizeof(tiny), &out_length),
             QUARRY_C_STATUS_INSUFFICIENT_CAPACITY);
}

TEST(BinaryRecordRuntimeCTest, EncodedSizeMatchesActualEncodedLength) {
    uint8_t count_bytes[4] = {0, 0, 0, 42};
    quarry_c_field_t fields[1];
    fields[0].field_index = 0U;
    fields[0].bytes = count_bytes;
    fields[0].length = 4U;

    size_t predicted_size = 0U;
    ASSERT_EQ(quarry_c_record_encoded_size(fields, 1U, &predicted_size), QUARRY_C_STATUS_OK);

    uint8_t output[64];
    size_t actual_length = 0U;
    ASSERT_EQ(quarry_c_encode_record(1U, fields, 1U, output, sizeof(output), &actual_length),
             QUARRY_C_STATUS_OK);
    EXPECT_EQ(predicted_size, actual_length);
}

TEST(BinaryRecordRuntimeCTest, RejectsTruncatedHeaderAtOffsetZero) {
    const uint8_t truncated[4] = {0x01, 0x00, 0x00, 0x00};
    quarry_c_parsed_record_t parsed;
    size_t error_offset = 123U;
    EXPECT_EQ(quarry_c_parse_record(truncated, sizeof(truncated), &parsed, &error_offset),
             QUARRY_C_STATUS_TRUNCATED_HEADER);
    EXPECT_EQ(error_offset, 0U);
}

TEST(BinaryRecordRuntimeCTest, RejectsUnsupportedVersionAndBadReservedBytes) {
    uint8_t header[16] = {0};
    header[0] = 2U; // unsupported version
    quarry_c_parsed_record_t parsed;
    size_t error_offset = 0U;
    EXPECT_EQ(quarry_c_parse_record(header, sizeof(header), &parsed, &error_offset),
             QUARRY_C_STATUS_UNSUPPORTED_VERSION);

    header[0] = 1U;
    header[3] = 1U; // reserved0 nonzero
    EXPECT_EQ(quarry_c_parse_record(header, sizeof(header), &parsed, &error_offset),
             QUARRY_C_STATUS_INVALID_HEADER);
    EXPECT_EQ(error_offset, 3U);
}

TEST(BinaryRecordRuntimeCTest, FindFieldReportsAbsenceWithoutError) {
    uint8_t output[64];
    size_t out_length = 0U;
    ASSERT_EQ(quarry_c_encode_record(1U, nullptr, 0U, output, sizeof(output), &out_length),
             QUARRY_C_STATUS_OK);

    quarry_c_parsed_record_t parsed;
    size_t error_offset = 0U;
    ASSERT_EQ(quarry_c_parse_record(output, out_length, &parsed, &error_offset),
             QUARRY_C_STATUS_OK);

    quarry_c_field_view_t view;
    bool found = true;
    ASSERT_EQ(quarry_c_find_field(&parsed, 5U, &view, &found), QUARRY_C_STATUS_OK);
    EXPECT_FALSE(found);
}

TEST(BinaryRecordRuntimeCTest, ValidatesUtf8AcceptingEveryClassOfWellFormedInput) {
    // Empty input (the empty string) is trivially valid, even with a NULL
    // pointer -- length 0 means the pointer is never dereferenced.
    EXPECT_TRUE(quarry_c_is_valid_utf8(nullptr, 0U));

    // ASCII.
    const uint8_t ascii[] = {'h', 'e', 'l', 'l', 'o'};
    EXPECT_TRUE(quarry_c_is_valid_utf8(ascii, sizeof(ascii)));

    // Embedded U+0000 is explicitly valid string data per
    // docs/specifications/binary-record-format.md's "string" section.
    const uint8_t embedded_nul[] = {'a', 'b', 0x00U, 'c', 'd'};
    EXPECT_TRUE(quarry_c_is_valid_utf8(embedded_nul, sizeof(embedded_nul)));

    // 2-byte, 3-byte, and 4-byte sequences: U+00E9 (e), U+4E2D (chinese
    // "middle"), U+1F600 (grinning face emoji).
    const uint8_t two_byte[] = {0xC3U, 0xA9U};
    EXPECT_TRUE(quarry_c_is_valid_utf8(two_byte, sizeof(two_byte)));
    const uint8_t three_byte[] = {0xE4U, 0xB8U, 0xADU};
    EXPECT_TRUE(quarry_c_is_valid_utf8(three_byte, sizeof(three_byte)));
    const uint8_t four_byte[] = {0xF0U, 0x9FU, 0x98U, 0x80U};
    EXPECT_TRUE(quarry_c_is_valid_utf8(four_byte, sizeof(four_byte)));
}

TEST(BinaryRecordRuntimeCTest, RejectsMalformedUtf8) {
    // Lone continuation byte.
    const uint8_t lone_continuation[] = {0x80U};
    EXPECT_FALSE(quarry_c_is_valid_utf8(lone_continuation, sizeof(lone_continuation)));

    // Truncated 2-byte sequence (missing continuation byte).
    const uint8_t truncated[] = {0xC3U};
    EXPECT_FALSE(quarry_c_is_valid_utf8(truncated, sizeof(truncated)));

    // Overlong encoding of U+0041 ('A') using 2 bytes instead of 1.
    const uint8_t overlong[] = {0xC1U, 0x81U};
    EXPECT_FALSE(quarry_c_is_valid_utf8(overlong, sizeof(overlong)));

    // Surrogate half U+D800 encoded directly (not valid UTF-8).
    const uint8_t surrogate[] = {0xEDU, 0xA0U, 0x80U};
    EXPECT_FALSE(quarry_c_is_valid_utf8(surrogate, sizeof(surrogate)));

    // Invalid leading byte 0xFF.
    const uint8_t invalid_leader[] = {0xFFU};
    EXPECT_FALSE(quarry_c_is_valid_utf8(invalid_leader, sizeof(invalid_leader)));
}

TEST(BinaryRecordRuntimeCTest, CopyBoundedCopiesWithinCapacityAndRejectsOverflow) {
    uint8_t destination[8] = {0};
    const uint8_t source[] = {1U, 2U, 3U, 4U};

    ASSERT_EQ(quarry_c_copy_bounded(destination, sizeof(destination), source, sizeof(source)),
             QUARRY_C_STATUS_OK);
    EXPECT_EQ(0, std::memcmp(destination, source, sizeof(source)));

    // Exactly at capacity succeeds.
    uint8_t exact[4] = {0};
    ASSERT_EQ(quarry_c_copy_bounded(exact, sizeof(exact), source, sizeof(source)),
             QUARRY_C_STATUS_OK);
    EXPECT_EQ(0, std::memcmp(exact, source, sizeof(source)));

    // One byte over capacity is rejected, with no partial copy performed.
    uint8_t tiny[3] = {0xAAU, 0xAAU, 0xAAU};
    EXPECT_EQ(quarry_c_copy_bounded(tiny, sizeof(tiny), source, sizeof(source)),
             QUARRY_C_STATUS_BOUNDS_EXCEEDED);
    EXPECT_EQ(tiny[0], 0xAAU) << "destination must be left untouched when length exceeds capacity";

    // Zero-length copy with NULL source/destination is valid (never
    // dereferences either pointer).
    EXPECT_EQ(quarry_c_copy_bounded(nullptr, 0U, nullptr, 0U), QUARRY_C_STATUS_OK);

    // Zero-length copy with a real (non-NULL) capacity but NULL source: the
    // length itself is 0, so this is also valid -- length, not the
    // pointers, controls whether NULL is acceptable.
    EXPECT_EQ(quarry_c_copy_bounded(destination, sizeof(destination), nullptr, 0U),
             QUARRY_C_STATUS_OK);
}

TEST(BinaryRecordRuntimeCTest, CopyBoundedRejectsNullPointersWhenLengthIsNonZero) {
    uint8_t destination[4] = {0};
    const uint8_t source[] = {1U, 2U};
    EXPECT_EQ(quarry_c_copy_bounded(nullptr, sizeof(destination), source, sizeof(source)),
             QUARRY_C_STATUS_NULL_ARGUMENT);
    EXPECT_EQ(quarry_c_copy_bounded(destination, sizeof(destination), nullptr, sizeof(source)),
             QUARRY_C_STATUS_NULL_ARGUMENT);
}

TEST(BinaryRecordRuntimeCTest, RejectsOverlappingFieldRanges) {
    // Hand-built directory with two entries whose payload ranges overlap:
    // field 0 at [0,4), field 1 at [2,6) -- both within a 6-byte payload.
    uint8_t record[16 + 6 + 6] = {0};
    record[0] = 1U;                                    // version
    record[2] = 2U;                                     // directoryEntryCount
    record[7] = 1U;                                     // recordId = 1 (big-endian)
    record[15] = 12U;                                    // payloadLength = 6 (dir) + 6 (payload)
    record[16] = 0U; record[17] = 0U; record[18] = 4U;  // field 0: index=0 offset=0 length=4
    record[19] = 1U; record[20] = 2U; record[21] = 4U;  // field 1: index=1 offset=2 length=4
    quarry_c_parsed_record_t parsed;
    size_t error_offset = 0U;
    EXPECT_EQ(quarry_c_parse_record(record, sizeof(record), &parsed, &error_offset),
             QUARRY_C_STATUS_OVERLAPPING_FIELD_RANGE);
}

} // namespace
