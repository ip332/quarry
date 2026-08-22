#include "compiler/qbs/parser.hpp"
#include "quarry/runtime/qbs_brf_reader.hpp"

#include <gtest/gtest.h>

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

} // namespace
