#include "compiler/qbs/parser.hpp"
#include "compiler/qbs/serializer.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <vector>

namespace {

using quarry::compiler::diagnostics::DiagnosticCollection;
using namespace quarry::compiler::qbs;

std::vector<std::uint8_t> image() {
    const auto hex = std::string_view{
        "51425300010000280101001068a731750346a0ad2e665f81260ea18300040000000000280000012d"
        "00010000000000580000001d00020000000000750000007000030000000000e50000004000060000"
        "000001250000000800000001000000000004000100000001000000170000000000ffff0000000000"
        "000000001100000000002000010000000000000004ffff0000000100060000001500000000004000"
        "020001000000000008ffff0000000200000000001d00000000001000000002000000000002ffff00"
        "00000300060000001f00000000004000030003000000000008ffff00000501000200000000000000"
        "0000000000070100040000000000000000000000000d020000000000000000000000000040100200"
        "000000000000000008000000004578616d706c6500"};
    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2U);
    for (std::size_t i = 0; i < hex.size(); i += 2U) {
        const auto digit = [](char c) -> std::uint8_t {
            return static_cast<std::uint8_t>(c >= 'a' ? c - 'a' + 10 : c - '0');
        };
        bytes.push_back(static_cast<std::uint8_t>((digit(hex[i]) << 4U) | digit(hex[i + 1U])));
    }
    return bytes;
}

std::vector<std::uint8_t> deep_chain_image(std::size_t depth) {
    QbsImageModel model;
    model.mode = BuildMode::Minimal;
    model.schema_identity_input = {0U,  0U,  0U,  17U, 'q', 'u', 'a', 'r', 'r', 'y', '.', 'q', 'b',
                                   's', '.', 's', 'c', 'h', 'e', 'm', 'a', 2U,  0U,  0U,  0U,  0U};
    model.types.reserve(depth + 1U);
    model.types.push_back(
        QbsTypeModel{.code = TypeCode::U16, .fixed_size = true, .encoded_width = 2U});
    for (std::size_t i = 0U; i < depth; ++i) {
        model.types.push_back(QbsTypeModel{.code = TypeCode::Array,
                                           .fixed_size = false,
                                           .reference = static_cast<std::uint16_t>(i),
                                           .max_elements = 1U});
    }
    DiagnosticCollection serializer_diagnostics;
    const auto serialized = serialize_qbs(model, serializer_diagnostics);
    EXPECT_TRUE(serialized.has_value());
    return serialized ? serialized->bytes : std::vector<std::uint8_t>{};
}

TEST(QbsParserTest, ParsesSerializerOutputAndExposesIdentity) {
    auto bytes = image();
    DiagnosticCollection diagnostics;
    const auto view = parse_qbs(bytes, diagnostics);
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        ADD_FAILURE() << diagnostic.message();
    }
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->header().identity_offset_width, 1U);
    ASSERT_EQ(view->record_count(), 1U);
    EXPECT_EQ(view->record(0).identity, "Example");
    EXPECT_EQ(view->field(0).type_index, 1U);
    EXPECT_EQ(view->type(1).encoded_width, 4U);
}

TEST(QbsParserTest, RejectsHostileHeaderAndIdentityReferences) {
    const auto valid = image();
    ASSERT_FALSE(valid.empty());

    const std::vector<std::function<void(std::vector<std::uint8_t>&)>> mutations = {
        [](auto& bytes) { bytes[0] = 'X'; }, [](auto& bytes) { bytes[9] = 2U; },
        [](auto& bytes) { bytes[39] = 0U; }, [](auto& bytes) { bytes[112] = 1U; }};
    for (const auto& mutate : mutations) {
        auto bytes = valid;
        mutate(bytes);
        DiagnosticCollection diagnostics;
        EXPECT_FALSE(parse_qbs(bytes, diagnostics).has_value());
    }
}

TEST(QbsParserTest, RejectsSchemaIdMismatch) {
    auto bytes = image();
    bytes[12] ^= 0x80U;
    DiagnosticCollection diagnostics;
    EXPECT_FALSE(parse_qbs(bytes, diagnostics).has_value());
}

TEST(QbsParserTest, RejectsTruncationAndTrailingBytes) {
    auto bytes = image();
    DiagnosticCollection diagnostics;
    bytes.pop_back();
    EXPECT_FALSE(parse_qbs(bytes, diagnostics).has_value());
    bytes = image();
    bytes.push_back(0U);
    diagnostics.clear();
    EXPECT_FALSE(parse_qbs(bytes, diagnostics).has_value());
}

TEST(QbsParserTest, EnforcesTypeValidationWorkLimit) {
    const auto bytes = image();
    DiagnosticCollection diagnostics;
    QbsParserLimits limits;
    limits.max_work_items = 0U;
    EXPECT_FALSE(parse_qbs(bytes, diagnostics, limits).has_value());

    diagnostics.clear();
    limits.max_work_items = 64U;
    EXPECT_TRUE(parse_qbs(bytes, diagnostics, limits).has_value());
}

TEST(QbsParserTest, ValidatesDeepTypeChainsWithinConfiguredBudgets) {
    const auto bytes = deep_chain_image(256U);
    ASSERT_FALSE(bytes.empty());

    QbsParserLimits adequate;
    adequate.max_work_items = 1024U;
    adequate.max_identity_key_bytes = 2U * 1024U * 1024U;
    DiagnosticCollection diagnostics;
    EXPECT_TRUE(parse_qbs(bytes, diagnostics, adequate).has_value());

    diagnostics.clear();
    adequate.max_work_items = 256U;
    EXPECT_FALSE(parse_qbs(bytes, diagnostics, adequate).has_value());

    diagnostics.clear();
    adequate.max_work_items = 1024U;
    adequate.max_identity_key_bytes = 1024U;
    EXPECT_FALSE(parse_qbs(bytes, diagnostics, adequate).has_value());
}

} // namespace
