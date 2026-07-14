#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/symbols/symbols.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::semantic::SemanticArrayType;
using breadcrumbs::compiler::semantic::SemanticField;
using breadcrumbs::compiler::semantic::SemanticModel;
using breadcrumbs::compiler::semantic::SemanticPrimitiveType;
using breadcrumbs::compiler::semantic::SemanticRecord;
using breadcrumbs::compiler::semantic::SemanticRecordType;
using breadcrumbs::compiler::semantic::SemanticType;
using breadcrumbs::compiler::semantic::SemanticValidator;
using breadcrumbs::compiler::source_schema::NormalizedSourceSchemaDocument;
using breadcrumbs::compiler::source_schema::NormalizedSourceSchemaEnum;
using breadcrumbs::compiler::source_schema::NormalizedSourceSchemaField;
using breadcrumbs::compiler::source_schema::NormalizedSourceSchemaType;
using breadcrumbs::compiler::source_schema::NormalizedSourceSchemaTypeReference;
using breadcrumbs::compiler::source_schema::SourceSchemaIdentifier;
using breadcrumbs::compiler::source_schema::SourceSchemaQualifiedName;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::symbols::NamespaceBuilder;
using breadcrumbs::compiler::symbols::SymbolTable;

struct NormalizedAnalysisOutput {
    DiagnosticEngine symbol_diagnostics;
    DiagnosticEngine semantic_diagnostics;
    std::unique_ptr<SymbolTable> symbol_table;
    SemanticModel semantic_model;
};

[[nodiscard]] SourceSchemaIdentifier normalized_identifier(std::string text, std::size_t begin,
                                                           std::size_t end) {
    return SourceSchemaIdentifier{
        .text = std::move(text),
        .source_range = breadcrumbs::compiler::support::SourceRange(
            breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), begin),
            breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), end)),
    };
}

[[nodiscard]] SourceSchemaQualifiedName normalized_qualified_name(std::string_view text,
                                                                  std::size_t begin,
                                                                  std::size_t end) {
    SourceSchemaQualifiedName name;
    name.source_range = breadcrumbs::compiler::support::SourceRange(
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), begin),
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), end));

    std::size_t part_begin = 0;
    while (part_begin <= text.size()) {
        const std::size_t part_end = text.find('.', part_begin);
        const std::string_view part = part_end == std::string_view::npos
                                          ? text.substr(part_begin)
                                          : text.substr(part_begin, part_end - part_begin);
        name.parts.push_back(normalized_identifier(std::string(part), begin + part_begin,
                                                   begin + part_begin + part.size()));
        if (part_end == std::string_view::npos) {
            break;
        }
        part_begin = part_end + 1;
    }

    return name;
}

[[nodiscard]] NormalizedSourceSchemaField normalized_field(std::string name,
                                                           std::string type_spelling) {
    NormalizedSourceSchemaField field;
    field.name = normalized_identifier(name, 0, name.size());
    field.source_range = breadcrumbs::compiler::support::SourceRange(
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), 0),
        breadcrumbs::compiler::support::SourceLocation(
            SourceFileId(0), name.size() + type_spelling.size() + 2U));
    if (type_spelling.size() >= 2 && type_spelling.ends_with("[]")) {
        const std::string_view element_spelling = std::string_view(type_spelling).substr(
            0, type_spelling.size() - 2);
        breadcrumbs::compiler::source_schema::NormalizedSourceSchemaArrayType array;
        array.source_range = field.source_range;
        NormalizedSourceSchemaTypeReference element_reference;
        element_reference.name =
            normalized_qualified_name(element_spelling, 0, element_spelling.size());
        element_reference.source_range = array.source_range;
        array.element_type = std::make_unique<NormalizedSourceSchemaType>(
            NormalizedSourceSchemaType{std::move(element_reference)});
        field.type = NormalizedSourceSchemaType{std::move(array)};
        return field;
    }

    NormalizedSourceSchemaTypeReference type_reference;
    type_reference.name = normalized_qualified_name(type_spelling, 0, type_spelling.size());
    type_reference.source_range = field.source_range;
    field.type = NormalizedSourceSchemaType{std::move(type_reference)};
    return field;
}

[[nodiscard]] NormalizedSourceSchemaDocument normalized_schema(std::string_view namespace_name,
                                                               std::string_view record_name) {
    NormalizedSourceSchemaDocument schema;
    schema.source_range = breadcrumbs::compiler::support::SourceRange(
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), 0),
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), 0));
    schema.namespace_name = normalized_qualified_name(namespace_name, 0, namespace_name.size());
    schema.record_name = normalized_identifier(std::string(record_name), 0, record_name.size());
    schema.record_source_range = breadcrumbs::compiler::support::SourceRange(
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), 0),
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), record_name.size()));
    schema.version = 1;
    schema.version_range = breadcrumbs::compiler::support::SourceRange(
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), 0),
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), 1));
    schema.record_type_spelling = "data";
    schema.record_type_range = breadcrumbs::compiler::support::SourceRange(
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), 0),
        breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), 4));
    return schema;
}

[[nodiscard]] NormalizedSourceSchemaEnum normalized_enum(std::string name, std::size_t begin,
                                                         std::size_t end) {
    return NormalizedSourceSchemaEnum{
        .name = normalized_identifier(std::move(name), begin, end),
        .source_range = breadcrumbs::compiler::support::SourceRange(
            breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), begin),
            breadcrumbs::compiler::support::SourceLocation(SourceFileId(0), end)),
        .values = {},
        .annotations = {},
    };
}

[[nodiscard]] NormalizedAnalysisOutput analyze_normalized(
    const NormalizedSourceSchemaDocument& schema) {
    NormalizedAnalysisOutput output;
    NamespaceBuilder namespace_builder;
    output.symbol_table = std::make_unique<SymbolTable>(
        namespace_builder.build(schema, output.symbol_diagnostics));

    SemanticValidator validator;
    output.semantic_model =
        validator.validate(schema, *output.symbol_table, output.semantic_diagnostics);
    return output;
}

[[nodiscard]] std::string diagnostics_summary(const DiagnosticEngine& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.id().str() << ": " << diagnostic.message() << '\n';
    }
    return stream.str();
}

[[nodiscard]] const SemanticRecord* find_record(const SemanticModel& model, std::string_view fqn) {
    return model.find_record(fqn);
}

[[nodiscard]] const SemanticField* find_field(const SemanticRecord& record, std::string_view name) {
    const auto found =
        std::find_if(record.fields.begin(), record.fields.end(),
                     [name](const SemanticField& field) { return field.name == name; });
    if (found == record.fields.end()) {
        return nullptr;
    }
    return &*found;
}

void expect_primitive_type(const SemanticField& field, SemanticPrimitiveType expected) {
    ASSERT_TRUE(field.type.is_primitive()) << field.name;
    EXPECT_EQ(field.type.primitive(), expected) << field.name;
}

void expect_string_type(const SemanticField& field) {
    EXPECT_TRUE(field.type.is_string()) << field.name;
}

void expect_bytes_type(const SemanticField& field) {
    EXPECT_TRUE(field.type.is_bytes()) << field.name;
}

void expect_record_reference_type(const SemanticField& field, std::string_view expected_fqn) {
    ASSERT_TRUE(field.type.is_record_reference()) << field.name;
    EXPECT_EQ(field.type.record_reference().canonical_target_fqn, expected_fqn) << field.name;
}

void expect_enum_reference_type(const SemanticField& field, std::string_view expected_fqn) {
    ASSERT_TRUE(field.type.is_enum_reference()) << field.name;
    EXPECT_EQ(field.type.enum_reference().canonical_target_fqn, expected_fqn) << field.name;
}

[[nodiscard]] SemanticType make_array_type(SemanticType element_type, std::uint32_t max_elements) {
    SemanticArrayType array;
    array.max_elements = max_elements;
    array.element_type = std::make_unique<SemanticType>(std::move(element_type));
    return SemanticType(std::move(array));
}

TEST(SemanticSmokeTest, AcceptsBuiltinFieldTypes) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.fields = {
        normalized_field("active", "bool"),
        normalized_field("count", "int32"),
        normalized_field("total", "uint64"),
        normalized_field("ratio", "float64"),
        normalized_field("label", "string"),
        normalized_field("payload", "bytes"),
    };
    schema.fields[4].max_bytes = 16;
    schema.fields[4].max_bytes_range = schema.fields[4].source_range;
    schema.fields[5].max_bytes = 4;
    schema.fields[5].max_bytes_range = schema.fields[5].source_range;

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    ASSERT_EQ(output.semantic_model.records.size(), 1U);
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->fields.size(), 6U);
    const SemanticField* label = find_field(*record, "label");
    ASSERT_NE(label, nullptr);
    expect_string_type(*label);
    const SemanticField* payload = find_field(*record, "payload");
    ASSERT_NE(payload, nullptr);
    expect_bytes_type(*payload);
    expect_primitive_type(record->fields[0], SemanticPrimitiveType::Bool);
    expect_primitive_type(record->fields[1], SemanticPrimitiveType::I32);
    expect_primitive_type(record->fields[2], SemanticPrimitiveType::U64);
    expect_primitive_type(record->fields[3], SemanticPrimitiveType::F64);
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, NormalizesPrimitiveAliasesToCanonicalKinds) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.fields = {
        normalized_field("bool_value", "bool"),      normalized_field("i8_short", "i8"),
        normalized_field("i8_long", "int8"),         normalized_field("u8_short", "u8"),
        normalized_field("u8_long", "uint8"),        normalized_field("i16_short", "i16"),
        normalized_field("i16_long", "int16"),       normalized_field("u16_short", "u16"),
        normalized_field("u16_long", "uint16"),      normalized_field("i32_short", "i32"),
        normalized_field("i32_long", "int32"),       normalized_field("u32_short", "u32"),
        normalized_field("u32_long", "uint32"),      normalized_field("i64_short", "i64"),
        normalized_field("i64_long", "int64"),       normalized_field("u64_short", "u64"),
        normalized_field("u64_long", "uint64"),      normalized_field("f32_short", "f32"),
        normalized_field("f32_long", "float32"),      normalized_field("f64_short", "f64"),
        normalized_field("f64_long", "float64"),      normalized_field("text", "string"),
        normalized_field("payload", "bytes"),
    };
    schema.fields[21].max_bytes = 16;
    schema.fields[21].max_bytes_range = schema.fields[21].source_range;
    schema.fields[22].max_bytes = 4;
    schema.fields[22].max_bytes_range = schema.fields[22].source_range;

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->fields.size(), 23U);
    expect_primitive_type(record->fields[0], SemanticPrimitiveType::Bool);
    expect_primitive_type(record->fields[1], SemanticPrimitiveType::I8);
    expect_primitive_type(record->fields[2], SemanticPrimitiveType::I8);
    expect_primitive_type(record->fields[3], SemanticPrimitiveType::U8);
    expect_primitive_type(record->fields[4], SemanticPrimitiveType::U8);
    expect_primitive_type(record->fields[5], SemanticPrimitiveType::I16);
    expect_primitive_type(record->fields[6], SemanticPrimitiveType::I16);
    expect_primitive_type(record->fields[7], SemanticPrimitiveType::U16);
    expect_primitive_type(record->fields[8], SemanticPrimitiveType::U16);
    expect_primitive_type(record->fields[9], SemanticPrimitiveType::I32);
    expect_primitive_type(record->fields[10], SemanticPrimitiveType::I32);
    expect_primitive_type(record->fields[11], SemanticPrimitiveType::U32);
    expect_primitive_type(record->fields[12], SemanticPrimitiveType::U32);
    expect_primitive_type(record->fields[13], SemanticPrimitiveType::I64);
    expect_primitive_type(record->fields[14], SemanticPrimitiveType::I64);
    expect_primitive_type(record->fields[15], SemanticPrimitiveType::U64);
    expect_primitive_type(record->fields[16], SemanticPrimitiveType::U64);
    expect_primitive_type(record->fields[17], SemanticPrimitiveType::F32);
    expect_primitive_type(record->fields[18], SemanticPrimitiveType::F32);
    expect_primitive_type(record->fields[19], SemanticPrimitiveType::F64);
    expect_primitive_type(record->fields[20], SemanticPrimitiveType::F64);
    expect_string_type(record->fields[21]);
    expect_bytes_type(record->fields[22]);
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, PreservesRecordMetadataAndBoundedFieldTypes) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.version = 7;
    schema.version_range = schema.record_source_range;
    schema.record_type_spelling = "data";
    schema.record_type_range = schema.record_source_range;
    schema.fields = {normalized_field("active", "bool"), normalized_field("label", "string"),
                     normalized_field("payload", "bytes"), normalized_field("samples", "u32[]")};
    schema.fields[1].max_bytes = 16;
    schema.fields[1].max_bytes_range = schema.fields[1].source_range;
    schema.fields[2].max_bytes = 4;
    schema.fields[2].max_bytes_range = schema.fields[2].source_range;
    schema.fields[3].max_elements = 64;
    schema.fields[3].max_elements_range = schema.fields[3].source_range;

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->version.has_value());
    EXPECT_EQ(record->version, 7U);
    ASSERT_TRUE(record->record_type.has_value());
    EXPECT_EQ(record->record_type, SemanticRecordType::Data);
    ASSERT_EQ(record->fields.size(), 4U);
    ASSERT_TRUE(record->fields[1].type.is_string());
    ASSERT_TRUE(record->fields[2].type.is_bytes());
    ASSERT_TRUE(record->fields[3].type.is_array());
    EXPECT_EQ(
        std::get<breadcrumbs::compiler::semantic::SemanticStringType>(record->fields[1].type.value)
            .max_bytes,
        16U);
    EXPECT_EQ(
        std::get<breadcrumbs::compiler::semantic::SemanticBytesType>(record->fields[2].type.value)
            .max_bytes,
        4U);
    EXPECT_EQ(record->fields[3].type.array().max_elements, 64U);
}

TEST(SemanticSmokeTest, RejectsZeroVersionWithAValidSourceRange) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.version = 0;
    schema.version_range = schema.record_source_range;
    schema.fields = {normalized_field("active", "bool")};

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->version.has_value());
}

TEST(SemanticSmokeTest, RejectsNegativeVersion) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.version = -1;
    schema.version_range = schema.record_source_range;
    schema.fields = {normalized_field("active", "bool")};

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->version.has_value());
}

TEST(SemanticSmokeTest, RejectsVersionGreaterThanUint32) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.version = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 1;
    schema.version_range = schema.record_source_range;
    schema.fields = {normalized_field("active", "bool")};

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->version.has_value());
}

TEST(SemanticSmokeTest, RejectsInvalidLogicalRecordTypeWithAValidSourceRange) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.record_type_spelling = "bogus";
    schema.record_type_range = schema.record_source_range;
    schema.fields = {normalized_field("active", "bool")};

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5005");
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->record_type.has_value());
}

TEST(SemanticSmokeTest, RejectsInvalidLogicalRecordTypeWithoutAValidSourceRange) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.record_type_spelling = "bogus";
    schema.record_type_range = breadcrumbs::compiler::support::SourceRange::invalid();
    schema.fields = {normalized_field("active", "bool")};

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5005");
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->record_type.has_value());
}

TEST(SemanticSmokeTest, NormalizesLogicalRecordTypesToCanonicalKinds) {
    const std::array<std::pair<std::string_view, SemanticRecordType>, 5> cases = {
        std::pair{"data", SemanticRecordType::Data},
        std::pair{"command", SemanticRecordType::Command},
        std::pair{"event", SemanticRecordType::Event},
        std::pair{"configuration", SemanticRecordType::Configuration},
        std::pair{"diagnostics", SemanticRecordType::Diagnostics},
    };

    for (const auto& [spelling, expected] : cases) {
        auto schema = normalized_schema("breadcrumbs.geo", "Example");
        schema.record_type_spelling = std::string(spelling);
        schema.record_type_range = schema.record_source_range;
        schema.fields = {normalized_field("value", "bool")};

        const NormalizedAnalysisOutput output = analyze_normalized(schema);

        ASSERT_TRUE(output.symbol_diagnostics.empty())
            << diagnostics_summary(output.symbol_diagnostics) << spelling;
        ASSERT_TRUE(output.semantic_diagnostics.empty())
            << diagnostics_summary(output.semantic_diagnostics) << spelling;
        const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
        ASSERT_NE(record, nullptr);
        ASSERT_TRUE(record->record_type.has_value());
        EXPECT_EQ(*record->record_type, expected) << spelling;
    }
}

TEST(SemanticSmokeTest, ReportsMissingZeroAndOverflowingStringBounds) {
    struct Case {
        const char* name;
        std::optional<std::int64_t> max_bytes;
        std::optional<std::string> expected_id;
    };

    const std::array<Case, 3> cases = {
        {{"missing", std::nullopt, std::string("BC5004")},
         {"zero", 0, std::string("BC5004")},
         {"overflow", static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 1,
          std::string("BC5004")}}};

    for (const Case& test_case : cases) {
        auto schema = normalized_schema("breadcrumbs.geo", "Example");
        schema.fields = {normalized_field("label", "string")};
        if (test_case.max_bytes.has_value()) {
            schema.fields[0].max_bytes = *test_case.max_bytes;
            schema.fields[0].max_bytes_range = schema.fields[0].source_range;
        }

        const NormalizedAnalysisOutput output = analyze_normalized(schema);

        ASSERT_TRUE(output.symbol_diagnostics.empty())
            << diagnostics_summary(output.symbol_diagnostics) << test_case.name;
        ASSERT_FALSE(output.semantic_diagnostics.empty()) << test_case.name;
        EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(),
                  *test_case.expected_id)
            << test_case.name;
        const SemanticRecord* record =
            find_record(output.semantic_model, "breadcrumbs.geo.Example");
        ASSERT_NE(record, nullptr);
        EXPECT_TRUE(record->fields.empty()) << test_case.name;
    }
}

TEST(SemanticSmokeTest, ReportsMissingZeroAndOverflowingBytesBounds) {
    struct Case {
        const char* name;
        std::optional<std::int64_t> max_bytes;
    };

    const std::array<Case, 3> cases = {
        {{"missing", std::nullopt},
         {"zero", 0},
         {"overflow", static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 1}}};

    for (const Case& test_case : cases) {
        auto schema = normalized_schema("breadcrumbs.geo", "Example");
        schema.fields = {normalized_field("payload", "bytes")};
        if (test_case.max_bytes.has_value()) {
            schema.fields[0].max_bytes = *test_case.max_bytes;
            schema.fields[0].max_bytes_range = schema.fields[0].source_range;
        }

        const NormalizedAnalysisOutput output = analyze_normalized(schema);

        ASSERT_TRUE(output.symbol_diagnostics.empty())
            << diagnostics_summary(output.symbol_diagnostics) << test_case.name;
        ASSERT_FALSE(output.semantic_diagnostics.empty()) << test_case.name;
        EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004")
            << test_case.name;
        const SemanticRecord* record =
            find_record(output.semantic_model, "breadcrumbs.geo.Example");
        ASSERT_NE(record, nullptr);
        EXPECT_TRUE(record->fields.empty()) << test_case.name;
    }
}

TEST(SemanticSmokeTest, ReportsMissingZeroAndOverflowingArrayBounds) {
    struct Case {
        const char* name;
        std::optional<std::int64_t> max_elements;
    };

    const std::array<Case, 3> cases = {
        {{"missing", std::nullopt},
         {"zero", 0},
         {"overflow", static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 1}}};

    for (const Case& test_case : cases) {
        auto schema = normalized_schema("breadcrumbs.geo", "Example");
        schema.fields = {normalized_field("samples", "u32[]")};
        if (test_case.max_elements.has_value()) {
            schema.fields[0].max_elements = *test_case.max_elements;
            schema.fields[0].max_elements_range = schema.fields[0].source_range;
        }

        const NormalizedAnalysisOutput output = analyze_normalized(schema);

        ASSERT_TRUE(output.symbol_diagnostics.empty())
            << diagnostics_summary(output.symbol_diagnostics) << test_case.name;
        ASSERT_FALSE(output.semantic_diagnostics.empty()) << test_case.name;
        EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004")
            << test_case.name;
        const SemanticRecord* record =
            find_record(output.semantic_model, "breadcrumbs.geo.Example");
        ASSERT_NE(record, nullptr);
        EXPECT_TRUE(record->fields.empty()) << test_case.name;
    }
}

TEST(SemanticSmokeTest, ReportsInvalidBoundPlacementOnOtherFieldKinds) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.fields = {normalized_field("active", "bool")};
    schema.fields[0].max_bytes = 16;
    schema.fields[0].max_bytes_range = schema.fields[0].source_range;

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->fields.empty());
}

TEST(SemanticSmokeTest, ReportsInvalidMaxElementsOnNonArrayFields) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.fields = {normalized_field("count", "u32")};
    schema.fields[0].max_elements = 4;
    schema.fields[0].max_elements_range = schema.fields[0].source_range;

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->fields.empty());
}

TEST(SemanticSmokeTest, CollectsNestedNamespaceDeclarations) {
    auto schema = normalized_schema("breadcrumbs.geo", "Location");
    schema.fields = {};

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Location");
    ASSERT_NE(record, nullptr);
    const auto& global = output.symbol_table->global_scope();
    const auto* breadcrumbs = global.find_local("breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    ASSERT_NE(breadcrumbs->child_scope, nullptr);
    const auto* geo = breadcrumbs->child_scope->find_local("geo");
    ASSERT_NE(geo, nullptr);
    ASSERT_NE(geo->child_scope, nullptr);
    EXPECT_NE(output.symbol_table->lookup(
                  normalized_qualified_name("breadcrumbs.geo.Location", 0, 24), global),
              nullptr);
}

TEST(SemanticSmokeTest, ReportsDuplicateDeclarationsInTheSameScope) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.enums.push_back(normalized_enum("Example", 64, 71));

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_EQ(output.symbol_diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.symbol_diagnostics.diagnostics()[0].id().str(), "BC4001");
    EXPECT_EQ(output.symbol_diagnostics.diagnostics()[0].compiler_pass(), "symbols");
}

TEST(SemanticSmokeTest, ResolvesRecordAndEnumReferencesToCanonicalFQNs) {
    auto schema = normalized_schema("breadcrumbs.geo", "Route");
    schema.enums.push_back(normalized_enum("Mode", 64, 68));
    schema.fields = {
        normalized_field("relative_location", "Route"),
        normalized_field("qualified_location", "breadcrumbs.geo.Route"),
        normalized_field("relative_mode", "Mode"),
        normalized_field("qualified_mode", "breadcrumbs.geo.Mode"),
    };

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    const SemanticRecord* route = find_record(output.semantic_model, "breadcrumbs.geo.Route");
    ASSERT_NE(route, nullptr);
    ASSERT_EQ(route->fields.size(), 4U);
    expect_record_reference_type(route->fields[0], "breadcrumbs.geo.Route");
    expect_record_reference_type(route->fields[1], "breadcrumbs.geo.Route");
    expect_enum_reference_type(route->fields[2], "breadcrumbs.geo.Mode");
    expect_enum_reference_type(route->fields[3], "breadcrumbs.geo.Mode");
}

TEST(SemanticSmokeTest, ReportsUnresolvedNamedTypeDiagnostics) {
    auto schema = normalized_schema("breadcrumbs.geo", "Example");
    schema.fields = {normalized_field("missing", "MissingType")};

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_EQ(output.semantic_diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].id().str(), "BC5001");
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].compiler_pass(), "semantic");
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.geo.Example");
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->fields.empty());
}

TEST(SemanticSmokeTest, ReportsNamespaceUsedAsTypeDiagnostics) {
    auto schema = normalized_schema("breadcrumbs.vehicle.geo", "Journey");
    schema.fields = {normalized_field("destination", "geo")};

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_EQ(output.semantic_diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].id().str(), "BC5002");
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].compiler_pass(), "semantic");
    const SemanticRecord* journey =
        find_record(output.semantic_model, "breadcrumbs.vehicle.geo.Journey");
    ASSERT_NE(journey, nullptr);
    EXPECT_TRUE(journey->fields.empty());
}

TEST(SemanticSmokeTest, ReportsLexicalShadowingInQualifiedTypeResolution) {
    auto schema = normalized_schema("breadcrumbs.vehicle", "geo");
    schema.fields = {normalized_field("destination", "geo.Location")};

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_EQ(output.semantic_diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].id().str(), "BC5001");
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].compiler_pass(), "semantic");
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.vehicle.geo");
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->fields.empty());
}

TEST(SemanticSmokeTest, ContinuesAfterMultipleSemanticErrors) {
    auto schema = normalized_schema("breadcrumbs.vehicle", "geo");
    schema.fields = {
        normalized_field("shadowed", "geo.Location"),
        normalized_field("missing", "MissingType"),
        normalized_field("payload", "bytes"),
        normalized_field("home", "breadcrumbs.vehicle.geo"),
    };

    const NormalizedAnalysisOutput output = analyze_normalized(schema);

    ASSERT_TRUE(output.symbol_diagnostics.empty())
        << diagnostics_summary(output.symbol_diagnostics);
    ASSERT_EQ(output.semantic_diagnostics.diagnostics().size(), 3U);
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].id().str(), "BC5001");
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[1].id().str(), "BC5001");
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[2].id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "breadcrumbs.vehicle.geo");
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->fields.size(), 1U);
    EXPECT_EQ(record->fields[0].name, "home");
    expect_record_reference_type(record->fields[0], "breadcrumbs.vehicle.geo");
}

TEST(SemanticSmokeTest, SupportsRecursiveArraySemanticTypes) {
    SemanticType leaf(SemanticPrimitiveType::U32);
    SemanticType middle = make_array_type(std::move(leaf), 1);
    SemanticType inner = make_array_type(std::move(middle), 2);
    const SemanticType recursive = make_array_type(std::move(inner), 3);

    ASSERT_TRUE(recursive.is_array());
    ASSERT_TRUE(recursive.array().element_type != nullptr);
    ASSERT_TRUE(recursive.array().element_type->is_array());
    ASSERT_TRUE(recursive.array().element_type->array().element_type != nullptr);
    ASSERT_TRUE(recursive.array().element_type->array().element_type->is_array());
    ASSERT_TRUE(recursive.array().element_type->array().element_type->array().element_type !=
                nullptr);
    ASSERT_TRUE(
        recursive.array().element_type->array().element_type->array().element_type->is_primitive());
    EXPECT_EQ(
        recursive.array().element_type->array().element_type->array().element_type->primitive(),
        SemanticPrimitiveType::U32);

    const SemanticType copied = recursive;
    ASSERT_TRUE(copied.is_array());
    ASSERT_TRUE(copied.array().element_type != nullptr);
    ASSERT_TRUE(copied.array().element_type->is_array());
    ASSERT_TRUE(copied.array().element_type->array().element_type != nullptr);
    ASSERT_TRUE(copied.array().element_type->array().element_type->is_array());
    ASSERT_TRUE(copied.array().element_type->array().element_type->array().element_type != nullptr);
    EXPECT_EQ(copied.array().element_type->array().element_type->array().element_type->primitive(),
              SemanticPrimitiveType::U32);
}

} // namespace
