#include "compiler/qbs/parser.hpp"
#include "quarry/runtime/qbs_brf_reader.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace quarry::compiler::qbs;
using quarry::runtime::GenericBrfError;

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

struct Mutation {
    std::string kind;
    std::string name;
    std::size_t offset;
    std::vector<std::uint8_t> replacement;
};

std::vector<std::uint8_t> hex_bytes(const std::string& hex) {
    std::vector<std::uint8_t> result;
    for (std::size_t i = 0; i < hex.size(); i += 2U)
        result.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2U), nullptr, 16)));
    return result;
}

std::vector<Mutation> mutations(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<Mutation> result;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream fields(line);
        std::string kind, name, offset, replacement;
        std::getline(fields, kind, '|'); std::getline(fields, name, '|');
        std::getline(fields, offset, '|'); std::getline(fields, replacement, '|');
        if (replacement.starts_with("TRUNCATE:"))
            result.push_back({kind, name, std::stoul(offset), {}});
        else
            result.push_back({kind, name, std::stoul(offset), hex_bytes(replacement)});
    }
    return result;
}

std::vector<std::uint8_t> mutate(std::vector<std::uint8_t> bytes, const Mutation& mutation) {
    if (mutation.name == "truncation") return {bytes.begin(), bytes.begin() + mutation.offset};
    EXPECT_LE(mutation.offset + mutation.replacement.size(), bytes.size());
    std::copy(mutation.replacement.begin(), mutation.replacement.end(), bytes.begin() + mutation.offset);
    return bytes;
}

std::filesystem::path fixture_dir() { return QUARRY_GENERIC_RUNTIME_FIXTURE_DIR; }

TEST(GenericRuntimeConformance, ValidFixture) {
    const auto directory = fixture_dir();
    const auto qbs_bytes = read_file(directory / "schema.qbs");
    const auto brf_bytes = read_file(directory / "record.brf");
    quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs_bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto parent = schema->find_record_by_identity("Parent");
    ASSERT_TRUE(parent.has_value());
    GenericBrfError error = GenericBrfError::none;
    const auto view = quarry::runtime::validate_brf_record(*schema, *parent, brf_bytes, {}, &error);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->field(0)->as_unsigned(), 42U);
    EXPECT_EQ(view->field(1)->as_signed(), -17);
    EXPECT_EQ(view->field(2)->as_bool(), true);
    EXPECT_FLOAT_EQ(*view->field(3)->as_float32(), 12.5F);
    EXPECT_DOUBLE_EQ(*view->field(4)->as_float64(), -3.25);
    EXPECT_EQ(view->field(5)->as_enum(), 1U);
    EXPECT_EQ(*view->field(6)->as_string(), "quarry");
    const auto payload = *view->field(7)->as_bytes();
    EXPECT_EQ(std::vector<std::uint8_t>(payload.begin(), payload.end()),
              (std::vector<std::uint8_t>{1, 2, 0xff}));
    ASSERT_TRUE(view->array(8));
    ASSERT_EQ(view->array(8)->size(), 3U);
    EXPECT_EQ(view->array(8)->element(0)->as_unsigned(), 1U);
    EXPECT_EQ(view->array(8)->element(2)->as_unsigned(), 3U);
    ASSERT_TRUE(view->nested_record(9));
    EXPECT_EQ(view->nested_record(9)->field(0)->as_unsigned(), 100U);
    EXPECT_EQ(*view->nested_record(9)->field(1)->as_string(), "child");
    ASSERT_TRUE(view->record_array(10));
    ASSERT_EQ(view->record_array(10)->size(), 2U);
    EXPECT_EQ(view->record_array(10)->element(0)->field(0)->as_signed(), -4);
    EXPECT_EQ(view->record_array(10)->element(1)->field(0)->as_signed(), 8);
    EXPECT_EQ(view->record_array(10)->element(0)->nested_record(1)->field(0)->as_unsigned(), 101U);
    EXPECT_FALSE(view->is_present(*schema->find_field(parent->record_id, 11U)));
    ASSERT_TRUE(view->array(12));
    EXPECT_EQ(view->array(12)->size(), 0U);
}

TEST(GenericRuntimeConformance, MalformedBrfRejected) {
    const auto directory = fixture_dir();
    const auto qbs_bytes = read_file(directory / "schema.qbs");
    const auto brf_bytes = read_file(directory / "record.brf");
    quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs_bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto parent = schema->find_record_by_identity("Parent");
    ASSERT_TRUE(parent.has_value());
    for (const auto& mutation : mutations(directory / "mutations.txt")) {
        if (mutation.kind != "BRF") continue;
        const auto bytes = mutate(brf_bytes, mutation);
        GenericBrfError error = GenericBrfError::none;
        EXPECT_FALSE(quarry::runtime::validate_brf_record(*schema, *parent, bytes, {}, &error)) << mutation.name;
    }
}

TEST(GenericRuntimeConformance, MalformedQbsRejected) {
    const auto directory = fixture_dir();
    const auto qbs_bytes = read_file(directory / "schema.qbs");
    quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
    for (const auto& mutation : mutations(directory / "mutations.txt")) {
        if (mutation.kind != "QBS") continue;
        const auto bytes = mutate(qbs_bytes, mutation);
        EXPECT_FALSE(parse_qbs(bytes, diagnostics)) << mutation.name;
        diagnostics = {};
    }
}
}
