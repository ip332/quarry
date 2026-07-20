#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/support/source_location.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/yaml/schema_decoder.hpp"
#include "compiler/yaml/yaml_document.hpp"
#include "compiler/yaml/yaml_parser.hpp"

#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace {

using quarry::compiler::diagnostics::DiagnosticEngine;
using quarry::compiler::support::LineColumn;
using quarry::compiler::support::SourceFileId;
using quarry::compiler::support::SourceLocation;
using quarry::compiler::support::SourceManager;
using quarry::compiler::support::SourceRange;
using quarry::compiler::source_schema::SourceSchemaAnnotation;
using quarry::compiler::source_schema::SourceSchemaDecodeResult;
using quarry::compiler::source_schema::SourceSchemaDocument;
using quarry::compiler::source_schema::SourceSchemaEnum;
using quarry::compiler::source_schema::SourceSchemaEnumValue;
using quarry::compiler::source_schema::SourceSchemaField;
using quarry::compiler::yaml::YamlDocument;
using quarry::compiler::yaml::YamlMappingNode;
using quarry::compiler::yaml::YamlNode;
using quarry::compiler::yaml::YamlParser;
using quarry::compiler::yaml::YamlParseResult;
using quarry::compiler::yaml::decode_schema;

struct PipelineOutput {
    SourceManager source_manager;
    SourceFileId source_file_id;
    DiagnosticEngine diagnostics;
    YamlParseResult parse_result;
    SourceSchemaDecodeResult decode_result;
};

[[nodiscard]] PipelineOutput parse_and_decode(std::string text) {
    PipelineOutput output;
    output.source_file_id =
        output.source_manager.add_source("/test/schema.yaml", std::move(text));
    output.parse_result =
        YamlParser::parse(output.source_manager, output.source_file_id, output.diagnostics);
    if (output.parse_result.document.has_value()) {
        output.decode_result = decode_schema(*output.parse_result.document, output.diagnostics);
    }
    return output;
}

[[nodiscard]] std::string diagnostic_summary(const DiagnosticEngine& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.id().str() << ": " << diagnostic.message() << '\n';
    }
    return stream.str();
}

struct StructuralFailureCase {
    const char* name;
    const char* yaml;
    const char* diagnostic_id;
};

class StructuralFailureTest : public ::testing::TestWithParam<StructuralFailureCase> {};

TEST_P(StructuralFailureTest, RejectsInvalidStructureAtomically) {
    const PipelineOutput output = parse_and_decode(GetParam().yaml);

    ASSERT_TRUE(output.parse_result.document.has_value()) << diagnostic_summary(output.diagnostics);
    EXPECT_FALSE(output.decode_result.schema.has_value()) << diagnostic_summary(output.diagnostics);
    ASSERT_FALSE(output.diagnostics.diagnostics().empty());
    EXPECT_EQ(output.diagnostics.diagnostics().front().id().str(), GetParam().diagnostic_id);
}

INSTANTIATE_TEST_SUITE_P(
    SchemaDecoderStructuralFailures, StructuralFailureTest,
    ::testing::Values(
        StructuralFailureCase{"NonMappingRoot", "hello", "BC2301"},
        StructuralFailureCase{"MissingNamespace", R"(record: Sample
version: 1
type: data
fields: {}
)", "BC2303"},
        StructuralFailureCase{"MissingRecord", R"(namespace: quarry.telemetry
version: 1
type: data
fields: {}
)", "BC2303"},
        StructuralFailureCase{"MissingVersion", R"(namespace: quarry.telemetry
record: Sample
type: data
fields: {}
)", "BC2303"},
        StructuralFailureCase{"MissingType", R"(namespace: quarry.telemetry
record: Sample
version: 1
fields: {}
)", "BC2303"},
        StructuralFailureCase{"MissingFields", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
)", "BC2303"},
        StructuralFailureCase{"DuplicateTopLevelProperty", R"(namespace: quarry.telemetry
record: Sample
record: Duplicate
version: 1
type: data
fields: {}
)", "BC2304"},
        StructuralFailureCase{"UnknownTopLevelProperty", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields: {}
unknown: value
)", "BC2305"},
        StructuralFailureCase{"PluralRecordsProperty", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields: {}
records:
  Position:
    fields: {}
)", "BC2305"},
        StructuralFailureCase{"NonScalarTopLevelPropertyKey", R"(? [bad]
: value
namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields: {}
)", "BC2302"},
        StructuralFailureCase{"WrongVersionShape", R"(namespace: quarry.telemetry
record: Sample
version: [1]
type: data
fields: {}
)", "BC2302"},
        StructuralFailureCase{"WrongFieldsShape", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields: []
)", "BC2301"},
        StructuralFailureCase{"WrongFieldDefinitionShape", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  count: 1
)", "BC2301"},
        StructuralFailureCase{"MissingFieldType", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  count:
    max_bytes: 4
)", "BC2303"},
        StructuralFailureCase{"DuplicateFieldProperty", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  count:
    type: uint32
    type: uint64
)", "BC2304"},
        StructuralFailureCase{"UnknownFieldProperty", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  count:
    type: uint32
    bogus: 1
)", "BC2305"},
        StructuralFailureCase{"InvalidNativeIntegerTypedProperty", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  count:
    type: uint32
    max_bytes: nope
)", "BC2306"},
        StructuralFailureCase{"WrongAnnotationsShape", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
annotations: []
fields:
  count:
    type: uint32
)", "BC2301"},
        StructuralFailureCase{"NonStringAnnotationValue", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
annotations:
  owner: [team]
fields:
  count:
    type: uint32
)", "BC2302"},
        StructuralFailureCase{"WrongEnumsShape", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
enums: []
fields:
  count:
    type: uint32
)", "BC2301"},
        StructuralFailureCase{"WrongEnumDefinitionShape", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
enums:
  Status: 1
fields:
  count:
    type: uint32
)", "BC2301"},
        StructuralFailureCase{"MissingEnumValues", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
enums:
  Status:
    annotations:
      note: state
fields:
  count:
    type: uint32
)", "BC2303"},
        StructuralFailureCase{"DuplicateEnumProperty", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
enums:
  Status:
    values:
      ready: 1
    values:
      done: 2
fields:
  count:
    type: uint32
)", "BC2304"},
        StructuralFailureCase{"UnknownEnumProperty", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
enums:
  Status:
    values:
      ready: 1
    bogus: true
fields:
  count:
    type: uint32
)", "BC2305"},
        StructuralFailureCase{"WrongEnumValuesShape", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
enums:
  Status:
    values: []
fields:
  count:
    type: uint32
)", "BC2301"},
        StructuralFailureCase{"InvalidExplicitEnumValue", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
enums:
  Status:
    values:
      ready: nope
fields:
  count:
    type: uint32
)", "BC2306"}));

struct QuotedIntegerCase {
    const char* name;
    const char* yaml;
};

class QuotedIntegerTest : public ::testing::TestWithParam<QuotedIntegerCase> {};

TEST_P(QuotedIntegerTest, RejectsQuotedNumericStringsWithNativeIntegerDiagnostic) {
    const PipelineOutput output = parse_and_decode(GetParam().yaml);

    ASSERT_TRUE(output.parse_result.document.has_value()) << diagnostic_summary(output.diagnostics);
    EXPECT_FALSE(output.decode_result.schema.has_value()) << diagnostic_summary(output.diagnostics);
    ASSERT_FALSE(output.diagnostics.diagnostics().empty());
    EXPECT_EQ(output.diagnostics.diagnostics().front().id().str(), "BC2306");
}

INSTANTIATE_TEST_SUITE_P(
    SchemaDecoderQuotedIntegers, QuotedIntegerTest,
    ::testing::Values(
        QuotedIntegerCase{"QuotedVersion", R"(namespace: quarry.telemetry
record: Sample
version: "1"
type: data
fields:
  count:
    type: uint32
)",},
        QuotedIntegerCase{"QuotedMaxBytes", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  note:
    type: string
    max_bytes: '32'
)",},
        QuotedIntegerCase{"QuotedMaxElements", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  samples:
    type: uint32[]
    max_elements: "64"
)",},
        QuotedIntegerCase{"QuotedEnumValue", R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
enums:
  Status:
    values:
      ready: "1"
fields:
  count:
    type: uint32
)",}));

TEST(SchemaDecoderTest, DecodesMinimalSchema) {
    const PipelineOutput output = parse_and_decode(R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  count:
    type: uint32
)");

    ASSERT_TRUE(output.parse_result.document.has_value()) << diagnostic_summary(output.diagnostics);
    ASSERT_TRUE(output.decode_result.schema.has_value()) << diagnostic_summary(output.diagnostics);

    const SourceSchemaDocument& schema = *output.decode_result.schema;
    EXPECT_EQ(schema.namespace_spelling, "quarry.telemetry");
    EXPECT_EQ(schema.record_name, "Sample");
    EXPECT_EQ(schema.version, 1);
    EXPECT_EQ(schema.record_type_spelling, "data");
    ASSERT_EQ(schema.fields.size(), 1U);
    EXPECT_EQ(schema.fields[0].name, "count");
    EXPECT_EQ(schema.fields[0].type_spelling, "uint32");
    EXPECT_TRUE(schema.fields[0].annotations.empty());
    EXPECT_TRUE(schema.enums.empty());
    EXPECT_TRUE(schema.annotations.empty());
    EXPECT_FALSE(schema.imports.has_value());
    EXPECT_TRUE(output.diagnostics.empty()) << diagnostic_summary(output.diagnostics);
}

TEST(SchemaDecoderTest, DecodesCompleteSchemaAndPreservesOptionalProperties) {
    const PipelineOutput output = parse_and_decode(R"(namespace: quarry.telemetry
record: Sample
version: 2
type: command
imports:
  - quarry.shared
  - quarry.motion
annotations:
  owner: telemetry
  stage: beta
fields:
  samples:
    type: uint32[]
    max_elements: 64
    max_bytes: 4096
    annotations:
      label: samples
  note:
    type: string
    max_bytes: 32
enums:
  Status:
    annotations:
      note: state
    values:
      first: 1
      second: 2
)");

    ASSERT_TRUE(output.parse_result.document.has_value()) << diagnostic_summary(output.diagnostics);
    ASSERT_TRUE(output.decode_result.schema.has_value()) << diagnostic_summary(output.diagnostics);

    const SourceSchemaDocument& schema = *output.decode_result.schema;
    ASSERT_TRUE(schema.imports.has_value());
    EXPECT_FALSE(schema.imports->empty);
    EXPECT_TRUE(schema.imports->source_range.is_valid());

    ASSERT_EQ(schema.annotations.size(), 2U);
    EXPECT_EQ(schema.annotations[0].name, "owner");
    EXPECT_EQ(schema.annotations[0].value, "telemetry");
    EXPECT_EQ(schema.annotations[1].name, "stage");
    EXPECT_EQ(schema.annotations[1].value, "beta");

    ASSERT_EQ(schema.fields.size(), 2U);
    EXPECT_EQ(schema.fields[0].name, "samples");
    EXPECT_EQ(schema.fields[0].type_spelling, "uint32[]");
    EXPECT_EQ(schema.fields[0].max_elements, 64);
    EXPECT_EQ(schema.fields[0].max_bytes, 4096);
    ASSERT_EQ(schema.fields[0].annotations.size(), 1U);
    EXPECT_EQ(schema.fields[0].annotations[0].name, "label");
    EXPECT_EQ(schema.fields[0].annotations[0].value, "samples");

    EXPECT_EQ(schema.fields[1].name, "note");
    EXPECT_EQ(schema.fields[1].type_spelling, "string");
    EXPECT_EQ(schema.fields[1].max_bytes, 32);
    EXPECT_FALSE(schema.fields[1].max_elements.has_value());

    ASSERT_EQ(schema.enums.size(), 1U);
    EXPECT_EQ(schema.enums[0].name, "Status");
    ASSERT_EQ(schema.enums[0].annotations.size(), 1U);
    EXPECT_EQ(schema.enums[0].annotations[0].name, "note");
    EXPECT_EQ(schema.enums[0].annotations[0].value, "state");
    ASSERT_EQ(schema.enums[0].values.size(), 2U);
    EXPECT_EQ(schema.enums[0].values[0].name, "first");
    EXPECT_EQ(schema.enums[0].values[0].value, 1);
    EXPECT_EQ(schema.enums[0].values[1].name, "second");
    EXPECT_EQ(schema.enums[0].values[1].value, 2);
    EXPECT_TRUE(output.diagnostics.empty()) << diagnostic_summary(output.diagnostics);
}

TEST(SchemaDecoderTest, PreservesSourceRanges) {
    const PipelineOutput output = parse_and_decode(R"(namespace: quarry.telemetry
record: Sample
version: 1
type: data
fields:
  count:
    type: uint32
)");

    ASSERT_TRUE(output.parse_result.document.has_value()) << diagnostic_summary(output.diagnostics);
    ASSERT_TRUE(output.decode_result.schema.has_value()) << diagnostic_summary(output.diagnostics);

    const SourceSchemaDocument& schema = *output.decode_result.schema;
    EXPECT_EQ(output.source_manager.line_column(schema.namespace_range.begin()),
              std::optional<LineColumn>({1, 12}));
    ASSERT_EQ(schema.fields.size(), 1U);
    EXPECT_EQ(output.source_manager.line_column(schema.fields[0].name_range.begin()),
              std::optional<LineColumn>({6, 3}));
    EXPECT_EQ(output.source_manager.line_column(schema.fields[0].type_range.begin()),
              std::optional<LineColumn>({7, 11}));
}

TEST(SchemaDecoderTest, AcceptsSemanticBoundaryFormsWithoutSemantics) {
    const PipelineOutput output = parse_and_decode(R"(namespace: quarry.telemetry
record: Sample
version: 1
type: not_a_real_type
fields:
  unknown_record_type:
    type: OtherRecord
  unknown_field_type:
    type: Widgets.Gizmo
  fixed_array:
    type: uint32[64]
  nested_array:
    type: uint32[][]
  string_without_max:
    type: string
  bytes_with_max_elements:
    type: bytes
    max_elements: 4
  array_without_bounds:
    type: uint32[]
  array_with_max_bytes:
    type: uint32[]
    max_bytes: 4
)");

    ASSERT_TRUE(output.parse_result.document.has_value()) << diagnostic_summary(output.diagnostics);
    ASSERT_TRUE(output.decode_result.schema.has_value()) << diagnostic_summary(output.diagnostics);

    const SourceSchemaDocument& schema = *output.decode_result.schema;
    ASSERT_EQ(schema.fields.size(), 8U);
    EXPECT_EQ(schema.record_type_spelling, "not_a_real_type");
    EXPECT_EQ(schema.fields[0].type_spelling, "OtherRecord");
    EXPECT_EQ(schema.fields[1].type_spelling, "Widgets.Gizmo");
    EXPECT_EQ(schema.fields[2].type_spelling, "uint32[64]");
    EXPECT_EQ(schema.fields[3].type_spelling, "uint32[][]");
    EXPECT_EQ(schema.fields[4].type_spelling, "string");
    EXPECT_EQ(schema.fields[5].type_spelling, "bytes");
    EXPECT_EQ(schema.fields[5].max_elements, 4);
    EXPECT_EQ(schema.fields[6].type_spelling, "uint32[]");
    EXPECT_FALSE(schema.fields[6].max_bytes.has_value());
    EXPECT_EQ(schema.fields[7].type_spelling, "uint32[]");
    EXPECT_EQ(schema.fields[7].max_bytes, 4);
    EXPECT_TRUE(output.diagnostics.empty()) << diagnostic_summary(output.diagnostics);
}

} // namespace
