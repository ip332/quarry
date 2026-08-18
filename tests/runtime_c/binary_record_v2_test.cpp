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
