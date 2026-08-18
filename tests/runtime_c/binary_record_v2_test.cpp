#include "quarry/runtime_c/binary_record_v2.h"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

TEST(BinaryRecordV2RuntimeCTest, EncodesTheReferenceExampleByteExactly) {
    const quarry_c_brf_v2_field_layout_t fields[] = {
        {0U, 0U, 17U, 0U, 32U, 4U, QUARRY_C_BRF_V2_STORAGE_FIXED},
        {1U, 1U, 21U, 0U, 0U, 8U, QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR},
        {2U, 2U, 29U, 0U, 16U, 2U, QUARRY_C_BRF_V2_STORAGE_FIXED},
        {3U, 3U, 31U, 0U, 0U, 8U, QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR},
    };
    const quarry_c_brf_v2_record_layout_t layout = {1U, 1U, 23U, 4U, fields};
    const uint8_t timestamp[] = {0x00U, 0x00U, 0x00U, 0x01U};
    const uint8_t name[] = {0x61U, 0x62U, 0x63U};
    const uint8_t state[] = {0x00U, 0x02U};
    const uint8_t samples[] = {0x02U, 0x00U, 0x0AU, 0x00U, 0x14U};
    const quarry_c_brf_v2_field_value_t values[] = {
        {0U, timestamp, sizeof(timestamp)}, {1U, name, sizeof(name)},
        {2U, state, sizeof(state)},         {3U, samples, sizeof(samples)},
    };
    std::array<uint8_t, 47U> actual{};
    size_t written = 0U;
    ASSERT_EQ(quarry_c_brf_v2_encode_record(&layout, 1U, values, 4U, actual.data(),
                                            actual.size(), &written),
              QUARRY_C_STATUS_OK);
    ASSERT_EQ(written, actual.size());

    const std::array<uint8_t, 47U> expected = {
        0x02U, 0x00U, 0x00U, 0x10U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x17U,
        0x00U, 0x00U, 0x00U, 0x2FU, 0x0FU, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
        0x27U, 0x00U, 0x00U, 0x00U, 0x03U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x2AU, 0x00U,
        0x00U, 0x00U, 0x05U, 0x61U, 0x62U, 0x63U, 0x02U, 0x00U, 0x0AU, 0x00U, 0x14U};
    EXPECT_EQ(actual, expected);

    quarry_c_brf_v2_parsed_record_t parsed{};
    size_t error_offset = 0U;
    EXPECT_EQ(quarry_c_brf_v2_parse_record(actual.data(), actual.size(), &layout, &parsed,
                                            &error_offset),
              QUARRY_C_STATUS_OK);
}

namespace {

const quarry_c_brf_v2_field_layout_t kTestFields[] = {
    {0U, 0U, 17U, 0U, 32U, 4U, QUARRY_C_BRF_V2_STORAGE_FIXED},
    {1U, 1U, 21U, 0U, 0U, 8U, QUARRY_C_BRF_V2_STORAGE_VARIABLE_DESCRIPTOR},
};

const quarry_c_brf_v2_record_layout_t kTestLayout = {7U, 1U, 13U, 2U, kTestFields};

std::array<uint8_t, 31U> make_test_record() {
    const uint8_t scalar[] = {0x00U, 0x00U, 0x00U, 0x2AU};
    const uint8_t text[] = {0x78U, 0x79U};
    const quarry_c_brf_v2_field_value_t values[] = {
        {0U, scalar, sizeof(scalar)}, {1U, text, sizeof(text)},
    };
    std::array<uint8_t, 31U> record{};
    size_t written = 0U;
    EXPECT_EQ(quarry_c_brf_v2_encode_record(&kTestLayout, 7U, values, 2U, record.data(),
                                            record.size(), &written),
              QUARRY_C_STATUS_OK);
    EXPECT_EQ(written, 31U);
    return record;
}

}  // namespace

TEST(BinaryRecordV2RuntimeCTest, HandlesPresenceAndFieldLookup) {
    std::array<uint8_t, 32U> record{};
    size_t written = 0U;
    ASSERT_EQ(quarry_c_brf_v2_encode_record(&kTestLayout, 7U, nullptr, 0U, record.data(),
                                            record.size(), &written),
              QUARRY_C_STATUS_OK);
    EXPECT_EQ(written, 29U);
    EXPECT_FALSE(quarry_c_brf_v2_present(record.data(), 0U));
    quarry_c_brf_v2_set_present(record.data(), 0U);
    EXPECT_TRUE(quarry_c_brf_v2_present_bit(record.data(), 0U));
    quarry_c_brf_v2_clear_present(record.data(), 0U);
    EXPECT_FALSE(quarry_c_brf_v2_present_bit(record.data(), 0U));

    const auto populated = make_test_record();
    quarry_c_brf_v2_parsed_record_t parsed{};
    size_t error_offset = 0U;
    ASSERT_EQ(quarry_c_brf_v2_parse_record(populated.data(), populated.size(), &kTestLayout,
                                           &parsed, &error_offset),
              QUARRY_C_STATUS_OK);
    quarry_c_brf_v2_field_view_t view{};
    ASSERT_EQ(quarry_c_brf_v2_find_field(&parsed, &kTestLayout, 0U, &view),
              QUARRY_C_STATUS_OK);
    ASSERT_TRUE(view.present);
    ASSERT_EQ(view.length, 4U);
    EXPECT_EQ(view.bytes[3], 0x2AU);
    ASSERT_EQ(quarry_c_brf_v2_find_field(&parsed, &kTestLayout, 1U, &view),
              QUARRY_C_STATUS_OK);
    ASSERT_EQ(view.length, 2U);
    EXPECT_EQ(view.bytes[0], 0x78U);
    EXPECT_EQ(quarry_c_brf_v2_find_field(&parsed, &kTestLayout, 99U, &view),
              QUARRY_C_STATUS_INVALID_FIELD_RANGE);
}

TEST(BinaryRecordV2RuntimeCTest, RejectsMalformedHeadersAndDescriptors) {
    auto record = make_test_record();
    size_t error_offset = 0U;
    quarry_c_brf_v2_parsed_record_t parsed{};

    auto bad = record;
    bad[0] = 1U;
    EXPECT_EQ(quarry_c_brf_v2_parse_record(bad.data(), bad.size(), &kTestLayout, &parsed,
                                           &error_offset),
              QUARRY_C_STATUS_INVALID_HEADER);
    EXPECT_EQ(quarry_c_brf_v2_parse_record(record.data(), 15U, &kTestLayout, &parsed,
                                           &error_offset),
              QUARRY_C_STATUS_INVALID_HEADER);

    bad = record;
    bad[16] = 0x80U;
    EXPECT_EQ(quarry_c_brf_v2_parse_record(bad.data(), bad.size(), &kTestLayout, &parsed,
                                           &error_offset),
              QUARRY_C_STATUS_INVALID_FIELD_RANGE);

    bad = record;
    bad[21] = 0U;
    bad[22] = 0U;
    bad[23] = 0U;
    bad[24] = 0x10U;
    EXPECT_EQ(quarry_c_brf_v2_parse_record(bad.data(), bad.size(), &kTestLayout, &parsed,
                                           &error_offset),
              QUARRY_C_STATUS_INVALID_FIELD_LENGTH);

    bad = record;
    bad[25] = 0U;
    bad[26] = 0U;
    bad[27] = 0U;
    bad[28] = 0U;
    EXPECT_EQ(quarry_c_brf_v2_parse_record(bad.data(), bad.size(), &kTestLayout, &parsed,
                                           &error_offset),
              QUARRY_C_STATUS_INVALID_FIELD_LENGTH);
}

TEST(BinaryRecordV2RuntimeCTest, RejectsInvalidEncodeArgumentsAndRanges) {
    std::array<uint8_t, 64U> output{};
    size_t size = 0U;
    EXPECT_EQ(quarry_c_brf_v2_record_size(nullptr, nullptr, 0U, &size),
              QUARRY_C_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(quarry_c_brf_v2_record_size(&kTestLayout, nullptr, 1U, &size),
              QUARRY_C_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(quarry_c_brf_v2_encode_record(&kTestLayout, 8U, nullptr, 0U, output.data(),
                                            output.size(), &size),
              QUARRY_C_STATUS_INVALID_ARGUMENT);
    std::array<uint8_t, 8U> small_output{};
    EXPECT_EQ(quarry_c_brf_v2_encode_record(&kTestLayout, 7U, nullptr, 0U, small_output.data(),
                                            small_output.size(), &size),
              QUARRY_C_STATUS_INSUFFICIENT_CAPACITY);

    const uint8_t bad_value[] = {0U};
    const quarry_c_brf_v2_field_value_t unknown[] = {{99U, bad_value, sizeof(bad_value)}};
    EXPECT_EQ(quarry_c_brf_v2_record_size(&kTestLayout, unknown, 1U, &size),
              QUARRY_C_STATUS_OK);
    EXPECT_EQ(quarry_c_brf_v2_encode_record(&kTestLayout, 7U, unknown, 1U, output.data(),
                                            output.size(), &size),
              QUARRY_C_STATUS_OK);
}
