#include "compiler/qbs/parser.hpp"
#include "quarry/runtime/qbs_brf_encoder.hpp"
#include "quarry/runtime/qbs_brf_reader.hpp"
#include "quarry/runtime_c/generic_brf_encoding.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
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

struct CEncodingFixture {
    quarry_qbs_record_view_t records[8]{};
    quarry_qbs_field_view_t fields[32]{};
    quarry_qbs_type_view_t types[32]{};
    quarry_qbs_enum_view_t enums[4]{};
    std::uint64_t enum_values[16]{};
    std::uint64_t enum_values_copy[16]{};
    quarry_brf_record_node_t nodes[32]{};
    quarry_brf_field_state_t states[128]{};
    std::uint32_t maps[128]{};
    quarry_brf_child_relation_t children[32]{};
    quarry_brf_record_array_relation_t arrays[16]{};
    std::uint32_t elements[32]{};
    quarry_brf_validation_frame_t frames[32]{};
    quarry_workspace_t qbs_workspace{records,     8U,  fields, 32U, types,    32U,  enums,  4U,
                                     enum_values, 16U, nodes,  32U, states,   128U, maps,   128U,
                                     children,    32U, arrays, 16U, elements, 32U,  frames, 32U,
                                     0U,          0U,  0U,     0U,  0U,       0U,   0U};
    quarry_brf_encoder_field_t planned[32]{};
    quarry_brf_encoder_array_element_t c_elements[32]{};
    quarry_brf_nested_record_plan_t nested_records[8]{};
    quarry_brf_nested_frame_t nested_frames[8]{};
    quarry_brf_nested_field_plan_t nested_fields[64]{};
    quarry_brf_nested_record_array_plan_t nested_arrays[4]{};
    quarry_brf_encoder_workspace_t encoder_workspace{planned,
                                                     32U,
                                                     0U,
                                                     0U,
                                                     c_elements,
                                                     32U,
                                                     0U,
                                                     {nested_records, 8U, nested_frames, 8U,
                                                      nested_fields, 64U, nested_arrays, 4U, 0U, 0U,
                                                      0U, 0U}};
    quarry_qbs_view_t schema{};
};

struct CProvider {
    std::vector<quarry_brf_value_t> values;
    std::vector<std::size_t> calls;
};

quarry_generic_status_t c_record_field(const quarry_brf_record_provider_t* provider,
                                       std::uint16_t index, quarry_brf_value_t* out) {
    const auto* context = static_cast<const CProvider*>(provider->context);
    if (out == nullptr || index >= context->values.size())
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    *out = context->values[index];
    return QUARRY_GENERIC_OK;
}

struct CArray {
    const quarry_brf_value_t* values;
    std::size_t count;
};

quarry_generic_status_t c_array_element(const quarry_brf_array_provider_t* provider,
                                        std::size_t index, quarry_brf_value_t* out) {
    const auto* array = static_cast<const CArray*>(provider->context);
    if (out == nullptr || index >= array->count)
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    *out = array->values[index];
    return QUARRY_GENERIC_OK;
}

quarry_generic_status_t c_provider_field(const quarry_brf_value_provider_t* provider,
                                         std::uint16_t index, quarry_brf_value_t* out) {
    auto* context = static_cast<CProvider*>(const_cast<void*>(provider->context));
    if (out == nullptr || index >= context->values.size())
        return QUARRY_GENERIC_INVALID_ARGUMENT;
    ++context->calls[index];
    *out = context->values[index];
    return QUARRY_GENERIC_OK;
}

TEST(GenericRuntimeConformance, GenericCEncodingMatchesCppAndDoesNotWriteOnFailure) {
    const auto directory = fixture_dir();
    const auto qbs_bytes = read_file(directory / "schema.qbs");
    quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
    const auto cpp_schema = parse_qbs(qbs_bytes, diagnostics);
    ASSERT_TRUE(cpp_schema.has_value());
    const auto cpp_parent = cpp_schema->find_record_by_identity("Parent");
    ASSERT_TRUE(cpp_parent.has_value());
    CEncodingFixture c;
    quarry_generic_limits_t qbs_limits{1U << 20U, 1U << 20U, 1U << 20U, 1U << 20U, 1U << 20U};
    ASSERT_EQ(quarry_qbs_parse(qbs_bytes.data(), qbs_bytes.size(), &c.schema, &c.qbs_workspace,
                               &qbs_limits),
              QUARRY_GENERIC_OK);
    const quarry_qbs_record_view_t* c_parent = nullptr;
    ASSERT_EQ(quarry_qbs_find_record_by_id(&c.schema, cpp_parent->record_id, &c_parent),
              QUARRY_GENERIC_OK);
    CProvider context{std::vector<quarry_brf_value_t>(c_parent->field_count),
                      std::vector<std::size_t>(c_parent->field_count)};
    context.values[0] = {};
    context.values[0].kind = QUARRY_BRF_ENCODE_UINT;
    context.values[0].uint_value = 42U;
    context.values[1] = {};
    context.values[1].kind = QUARRY_BRF_ENCODE_INT;
    context.values[1].int_value = -17;
    context.values[2] = {};
    context.values[2].kind = QUARRY_BRF_ENCODE_BOOL;
    context.values[2].bool_value = true;
    context.values[3] = {};
    context.values[3].kind = QUARRY_BRF_ENCODE_FLOAT;
    context.values[3].float_value = 12.5F;
    context.values[4] = {};
    context.values[4].kind = QUARRY_BRF_ENCODE_DOUBLE;
    context.values[4].double_value = -3.25;
    context.values[5] = {};
    context.values[5].kind = QUARRY_BRF_ENCODE_ENUM;
    context.values[5].int_value = 1;
    context.values[6] = {};
    context.values[6].kind = QUARRY_BRF_ENCODE_STRING;
    context.values[6].string_value = {"quarry", 6U};
    const std::uint8_t payload[] = {1U, 2U, 0U, 0xffU};
    context.values[7] = {};
    context.values[7].kind = QUARRY_BRF_ENCODE_BYTES;
    context.values[7].bytes_value = {payload, sizeof(payload)};
    for (std::size_t i = 8U; i < context.values.size(); ++i)
        context.values[i].kind = QUARRY_BRF_ENCODE_ABSENT;
    quarry_brf_value_t array_values[3]{};
    for (std::size_t i = 0U; i < 3U; ++i) {
        array_values[i].kind = QUARRY_BRF_ENCODE_UINT;
        array_values[i].uint_value = i + 1U;
    }
    const CArray array_context{array_values, 3U};
    const quarry_brf_array_provider_t array_provider{c_array_element, 3U, &array_context};
    context.values[8].kind = QUARRY_BRF_ENCODE_ARRAY;
    context.values[8].aggregate = &array_provider;
    CProvider child_context{std::vector<quarry_brf_value_t>(2), std::vector<std::size_t>(2)};
    child_context.values[0].kind = QUARRY_BRF_ENCODE_UINT;
    child_context.values[0].uint_value = 100U;
    child_context.values[1].kind = QUARRY_BRF_ENCODE_STRING;
    child_context.values[1].string_value = {"child", 5U};
    const quarry_brf_record_provider_t child_provider{c_record_field, &child_context};
    context.values[9].kind = QUARRY_BRF_ENCODE_RECORD;
    context.values[9].aggregate = &child_provider;
    const quarry_brf_value_provider_t provider{c_provider_field, &context};
    std::vector<std::optional<quarry::runtime::BrfEncodeValue>> cpp_values(cpp_parent->field_count);
    cpp_values[0] = std::uint64_t{42};
    cpp_values[1] = std::int64_t{-17};
    cpp_values[2] = true;
    cpp_values[3] = 12.5F;
    cpp_values[4] = -3.25;
    cpp_values[5] = std::uint64_t{1};
    cpp_values[6] = std::string("quarry");
    cpp_values[7] = std::vector<std::uint8_t>{1U, 2U, 0U, 0xffU};
    cpp_values[8] = quarry::runtime::BrfEncodeValue{
        quarry::runtime::BrfEncodeArray{quarry::runtime::BrfUnsignedArray{1U, 2U, 3U}}};
    auto child_input = std::make_shared<quarry::runtime::BrfRecordInput>();
    child_input->record_id = cpp_schema->record(0U).record_id;
    child_input->identity = std::string(cpp_schema->record(0U).identity);
    child_input->fields.resize(2U);
    child_input->fields[0] = std::uint64_t{100U};
    child_input->fields[1] = std::string("child");
    cpp_values[9] = quarry::runtime::BrfEncodeValue{child_input};
    quarry::runtime::GenericBrfEncodeError cpp_error = quarry::runtime::GenericBrfEncodeError::none;
    const auto cpp_bytes = encode_brf_record(*cpp_schema, *cpp_parent, cpp_values, &cpp_error);
    ASSERT_TRUE(cpp_bytes.has_value());
    std::vector<std::uint8_t> c_bytes(cpp_bytes->size(), 0xa5U);
    std::size_t required = 0U;
    ASSERT_EQ(quarry_brf_encode(&c.schema, c_parent, &provider, c_bytes.data(), c_bytes.size(),
                                &required, &c.encoder_workspace, nullptr),
              QUARRY_GENERIC_OK);
    ASSERT_EQ(required, cpp_bytes->size());
    EXPECT_EQ(c_bytes, *cpp_bytes);
    EXPECT_EQ(c_bytes[2], 0U);
    EXPECT_EQ(c_bytes[3], 16U);
    EXPECT_EQ(c_bytes[4], 0U);
    EXPECT_EQ(c_bytes[5], 0U);
    EXPECT_EQ(c_bytes[6], 0U);
    EXPECT_EQ(c_bytes[7], 1U);
    for (const auto calls : context.calls)
        EXPECT_EQ(calls, 1U);
    std::vector<std::uint8_t> sentinel(required - 1U, 0x5aU);
    quarry_brf_encoder_workspace_reset(&c.encoder_workspace);
    EXPECT_EQ(quarry_brf_encode(&c.schema, c_parent, &provider, sentinel.data(), sentinel.size(),
                                &required, &c.encoder_workspace, nullptr),
              QUARRY_GENERIC_BUFFER_TOO_SMALL);
    EXPECT_EQ(sentinel, std::vector<std::uint8_t>(sentinel.size(), 0x5aU));

    const auto preserves_output_on_failure = [&](quarry_generic_status_t expected) {
        std::vector<std::uint8_t> bytes(required, 0x3cU);
        quarry_brf_encoder_workspace_reset(&c.encoder_workspace);
        EXPECT_EQ(quarry_brf_encode(&c.schema, c_parent, &provider, bytes.data(), bytes.size(),
                                    &required, &c.encoder_workspace, nullptr),
                  expected);
        EXPECT_EQ(bytes, std::vector<std::uint8_t>(bytes.size(), 0x3cU));
    };
    context.values[2].kind = QUARRY_BRF_ENCODE_UINT;
    preserves_output_on_failure(QUARRY_GENERIC_TYPE_MISMATCH);
    context.values[2].kind = QUARRY_BRF_ENCODE_BOOL;
    context.values[6].string_value = {"\xc0\x80", 2U};
    preserves_output_on_failure(QUARRY_GENERIC_INVALID_ARGUMENT);

    context.values[6].string_value = {"quarry", 6U};
    context.values[0].uint_value = UINT64_C(0xffffffff);
    EXPECT_EQ(quarry_brf_encode(&c.schema, c_parent, &provider, c_bytes.data(), c_bytes.size(),
                                &required, &c.encoder_workspace, nullptr),
              QUARRY_GENERIC_OK);
    context.values[0].uint_value = UINT64_C(0x100000000);
    preserves_output_on_failure(QUARRY_GENERIC_VALUE_OUT_OF_RANGE);
    context.values[0].uint_value = 42U;

    context.values[1].int_value = INT64_C(-2147483648);
    EXPECT_EQ(quarry_brf_encode(&c.schema, c_parent, &provider, c_bytes.data(), c_bytes.size(),
                                &required, &c.encoder_workspace, nullptr),
              QUARRY_GENERIC_OK);
    context.values[1].int_value = INT64_C(-2147483649);
    preserves_output_on_failure(QUARRY_GENERIC_VALUE_OUT_OF_RANGE);
    context.values[1].int_value = INT64_C(2147483647);
    EXPECT_EQ(quarry_brf_encode(&c.schema, c_parent, &provider, c_bytes.data(), c_bytes.size(),
                                &required, &c.encoder_workspace, nullptr),
              QUARRY_GENERIC_OK);
    context.values[1].int_value = INT64_C(2147483648);
    preserves_output_on_failure(QUARRY_GENERIC_VALUE_OUT_OF_RANGE);
    context.values[1].int_value = -17;

    context.values[5].int_value = 0;
    EXPECT_EQ(quarry_brf_encode(&c.schema, c_parent, &provider, c_bytes.data(), c_bytes.size(),
                                &required, &c.encoder_workspace, nullptr),
              QUARRY_GENERIC_OK);
    context.values[5].int_value = 2;
    preserves_output_on_failure(QUARRY_GENERIC_VALUE_OUT_OF_RANGE);
    context.values[5].int_value = 1;

    context.values[7].kind = QUARRY_BRF_ENCODE_UINT;
    preserves_output_on_failure(QUARRY_GENERIC_TYPE_MISMATCH);
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
