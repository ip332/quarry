#include "compiler/qbs/parser.hpp"
#include "quarry/runtime/qbs_brf_reader.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

using quarry::compiler::diagnostics::DiagnosticCollection;
using namespace quarry::compiler::qbs;
using namespace quarry::runtime;

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
