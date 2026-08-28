#include "compiler/qbs/parser.hpp"
#include "quarry/runtime/qbs_brf_reader.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
        if (line.empty() || line[0] == '#')
            continue;
        std::stringstream fields(line);
        std::string kind, name, offset, replacement;
        std::getline(fields, kind, '|');
        std::getline(fields, name, '|');
        std::getline(fields, offset, '|');
        std::getline(fields, replacement, '|');
        if (replacement.starts_with("TRUNCATE:"))
            result.push_back({kind, name, std::stoul(offset), {}});
        else
            result.push_back({kind, name, std::stoul(offset), hex_bytes(replacement)});
    }
    return result;
}

std::vector<std::uint8_t> mutate(std::vector<std::uint8_t> bytes, const Mutation& mutation) {
    if (mutation.name == "truncation")
        return {bytes.begin(), bytes.begin() + mutation.offset};
    EXPECT_LE(mutation.offset + mutation.replacement.size(), bytes.size());
    std::copy(mutation.replacement.begin(), mutation.replacement.end(),
              bytes.begin() + mutation.offset);
    return bytes;
}

std::filesystem::path fixture_dir() { return QUARRY_GENERIC_RUNTIME_FIXTURE_DIR; }

std::vector<std::string> expected_trace(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> result;
    std::string line;
    while (std::getline(input, line))
        if (!line.empty() && line.front() != '#')
            result.push_back(line);
    return result;
}

std::string normalized_event(const quarry::runtime::BrfTraversalEvent& event) {
    using Kind = quarry::runtime::BrfTraversalEventKind;
    std::ostringstream out;
    if (event.kind == Kind::record_begin || event.kind == Kind::record_end) {
        out << (event.kind == Kind::record_begin ? "record_begin" : "record_end") << '|'
            << event.depth;
    } else if (event.kind == Kind::field) {
        out << "field|" << event.depth << '|' << event.field.field_index << '|'
            << (event.present ? 1 : 0);
    } else if (event.kind == Kind::array_begin || event.kind == Kind::array_end) {
        out << (event.kind == Kind::array_begin ? "array_begin" : "array_end") << '|'
            << event.depth;
        if (event.kind == Kind::array_begin)
            out << '|' << event.field.field_index;
    } else if (event.kind == Kind::array_element) {
        out << "array_element|" << event.depth << '|' << event.index;
    } else {
        const auto& value = *event.value;
        out << "scalar|" << event.depth << '|'
            << (event.array ? "-" : std::to_string(event.field.field_index)) << '|'
            << (event.array ? std::to_string(event.index) : "-") << '|';
        switch (value.kind()) {
        case quarry::runtime::GenericBrfValueKind::unsigned_integer:
            out << "u:" << *value.as_unsigned();
            break;
        case quarry::runtime::GenericBrfValueKind::signed_integer:
            out << "i:" << *value.as_signed();
            break;
        case quarry::runtime::GenericBrfValueKind::boolean:
            out << "b:" << (*value.as_bool() ? "true" : "false");
            break;
        case quarry::runtime::GenericBrfValueKind::float32:
            out << "f:" << std::setprecision(9) << *value.as_float32();
            break;
        case quarry::runtime::GenericBrfValueKind::float64:
            out << "d:" << std::setprecision(17) << *value.as_float64();
            break;
        case quarry::runtime::GenericBrfValueKind::enumeration:
            out << "e:" << *value.as_enum();
            break;
        case quarry::runtime::GenericBrfValueKind::string: {
            out << "s:";
            const auto string = value.as_string();
            for (const auto byte : *string)
                out << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<unsigned>(static_cast<unsigned char>(byte));
            break;
        }
        case quarry::runtime::GenericBrfValueKind::bytes: {
            out << "x:";
            const auto bytes = value.as_bytes();
            for (const auto byte : *bytes)
                out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
            break;
        }
        default:
            break;
        }
    }
    return out.str();
}

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

TEST(GenericRuntimeConformance, TraversalOrderReference) {
    const auto directory = fixture_dir();
    const auto qbs_bytes = read_file(directory / "schema.qbs");
    const auto brf_bytes = read_file(directory / "record.brf");
    quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs_bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto parent = schema->find_record_by_identity("Parent");
    ASSERT_TRUE(parent.has_value());
    const auto view = quarry::runtime::validate_brf_record(*schema, *parent, brf_bytes);
    ASSERT_TRUE(view.has_value());
    std::vector<std::pair<quarry::runtime::BrfTraversalEventKind, std::uint16_t>> trace;
    const auto result = quarry::runtime::traverse_brf(*view, [&](const auto& event) {
        trace.emplace_back(event.kind, event.kind == quarry::runtime::BrfTraversalEventKind::field
                                           ? event.field.field_index
                                           : 0U);
        return quarry::runtime::BrfTraversalControl::Continue;
    });
    ASSERT_EQ(result, quarry::runtime::BrfTraversalResult::completed);
    ASSERT_FALSE(trace.empty());
    EXPECT_EQ(trace.front().first, quarry::runtime::BrfTraversalEventKind::record_begin);
    EXPECT_EQ(trace.back().first, quarry::runtime::BrfTraversalEventKind::record_end);
    std::vector<std::uint16_t> fields;
    for (const auto& [kind, index] : trace)
        if (kind == quarry::runtime::BrfTraversalEventKind::field)
            fields.push_back(index);
    EXPECT_EQ(fields, (std::vector<std::uint16_t>{0,  1, 2, 3, 4, 5, 6, 7, 8, 9,  0, 1,
                                                  10, 0, 1, 0, 1, 0, 1, 0, 1, 11, 12}));
}

TEST(GenericRuntimeConformance, TraversalMatchesNeutralTrace) {
    const auto directory = fixture_dir();
    const auto qbs_bytes = read_file(directory / "schema.qbs");
    const auto brf_bytes = read_file(directory / "record.brf");
    quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
    const auto schema = parse_qbs(qbs_bytes, diagnostics);
    ASSERT_TRUE(schema.has_value());
    const auto parent = schema->find_record_by_identity("Parent");
    ASSERT_TRUE(parent.has_value());
    const auto view = quarry::runtime::validate_brf_record(*schema, *parent, brf_bytes);
    ASSERT_TRUE(view.has_value());
    std::vector<std::string> actual;
    const auto result = quarry::runtime::traverse_brf(*view, [&](const auto& event) {
        actual.push_back(normalized_event(event));
        return quarry::runtime::BrfTraversalControl::Continue;
    });
    ASSERT_EQ(result, quarry::runtime::BrfTraversalResult::completed);
    const auto expected = expected_trace(directory / "traversal_trace.txt");
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i)
        EXPECT_EQ(actual[i], expected[i]) << "trace index " << i;
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
        if (mutation.kind != "BRF")
            continue;
        const auto bytes = mutate(brf_bytes, mutation);
        GenericBrfError error = GenericBrfError::none;
        EXPECT_FALSE(quarry::runtime::validate_brf_record(*schema, *parent, bytes, {}, &error))
            << mutation.name;
    }
}

TEST(GenericRuntimeConformance, MalformedQbsRejected) {
    const auto directory = fixture_dir();
    const auto qbs_bytes = read_file(directory / "schema.qbs");
    quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
    for (const auto& mutation : mutations(directory / "mutations.txt")) {
        if (mutation.kind != "QBS")
            continue;
        const auto bytes = mutate(qbs_bytes, mutation);
        EXPECT_FALSE(parse_qbs(bytes, diagnostics)) << mutation.name;
        diagnostics = {};
    }
}
} // namespace
