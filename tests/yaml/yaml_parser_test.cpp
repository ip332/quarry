#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/support/source_location.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/yaml/yaml_document.hpp"
#include "compiler/yaml/yaml_parser.hpp"

#include <optional>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::support::LineColumn;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceLocation;
using breadcrumbs::compiler::support::SourceManager;
using breadcrumbs::compiler::support::SourceRange;
using breadcrumbs::compiler::yaml::YamlDocument;
using breadcrumbs::compiler::yaml::YamlMappingEntry;
using breadcrumbs::compiler::yaml::YamlMappingNode;
using breadcrumbs::compiler::yaml::YamlNode;
using breadcrumbs::compiler::yaml::YamlParseResult;
using breadcrumbs::compiler::yaml::YamlParser;
using breadcrumbs::compiler::yaml::YamlScalarNode;
using breadcrumbs::compiler::yaml::YamlScalarKind;
using breadcrumbs::compiler::yaml::YamlSequenceNode;

struct ParseOutput {
    SourceManager source_manager;
    SourceFileId source_file_id;
    DiagnosticEngine diagnostics;
    YamlParseResult result;
};

[[nodiscard]] ParseOutput parse(std::string text) {
    ParseOutput output;
    output.source_file_id =
        output.source_manager.add_source("/test/schema.yaml", std::move(text));
    output.result =
        YamlParser::parse(output.source_manager, output.source_file_id, output.diagnostics);
    return output;
}

[[nodiscard]] const YamlScalarNode& scalar(const YamlNode& node) {
    return std::get<YamlScalarNode>(node.value);
}

[[nodiscard]] const YamlSequenceNode& sequence(const YamlNode& node) {
    return std::get<YamlSequenceNode>(node.value);
}

[[nodiscard]] const YamlMappingNode& mapping(const YamlNode& node) {
    return std::get<YamlMappingNode>(node.value);
}

[[nodiscard]] const YamlNode& node(const std::unique_ptr<YamlNode>& ptr) {
    return *ptr;
}

[[nodiscard]] SourceRange range(SourceFileId file_id, std::size_t begin, std::size_t end) {
    return SourceRange(SourceLocation(file_id, begin), SourceLocation(file_id, end));
}

[[nodiscard]] std::string diagnostic_summary(const DiagnosticEngine& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.id().str() << ": " << diagnostic.message() << '\n';
    }
    return stream.str();
}

TEST(YamlParserTest, ParsesScalarRoot) {
    const ParseOutput output = parse("hello");

    ASSERT_TRUE(output.result.document.has_value()) << diagnostic_summary(output.diagnostics);
    ASSERT_NE(output.result.document->root, nullptr);
    EXPECT_EQ(scalar(*output.result.document->root).value, "hello");
    EXPECT_EQ(scalar(*output.result.document->root).kind, YamlScalarKind::Plain);
    EXPECT_EQ(output.result.document->source_range, range(output.source_file_id, 0, 5));
    EXPECT_EQ(output.result.document->root->source_range, range(output.source_file_id, 0, 5));
    EXPECT_TRUE(output.diagnostics.empty());
}

TEST(YamlParserTest, ClassifiesNativeIntegersAndQuotedStrings) {
    const ParseOutput plain = parse("1");
    ASSERT_TRUE(plain.result.document.has_value()) << diagnostic_summary(plain.diagnostics);
    EXPECT_EQ(scalar(*plain.result.document->root).kind, YamlScalarKind::Plain);

    const ParseOutput double_quoted = parse("\"1\"");
    ASSERT_TRUE(double_quoted.result.document.has_value())
        << diagnostic_summary(double_quoted.diagnostics);
    EXPECT_EQ(scalar(*double_quoted.result.document->root).kind, YamlScalarKind::DoubleQuoted);

    const ParseOutput single_quoted = parse(R"('1')");
    ASSERT_TRUE(single_quoted.result.document.has_value())
        << diagnostic_summary(single_quoted.diagnostics);
    EXPECT_EQ(scalar(*single_quoted.result.document->root).kind, YamlScalarKind::SingleQuoted);
}

TEST(YamlParserTest, ParsesSequenceAndPreservesOrder) {
    const ParseOutput output = parse(R"(- first
- second
- third
)");

    ASSERT_TRUE(output.result.document.has_value()) << diagnostic_summary(output.diagnostics);
    ASSERT_NE(output.result.document->root, nullptr);
    const auto& parsed_sequence = sequence(*output.result.document->root);
    ASSERT_EQ(parsed_sequence.elements.size(), 3U);
    EXPECT_EQ(scalar(node(parsed_sequence.elements[0])).value, "first");
    EXPECT_EQ(scalar(node(parsed_sequence.elements[1])).value, "second");
    EXPECT_EQ(scalar(node(parsed_sequence.elements[2])).value, "third");
    EXPECT_TRUE(output.diagnostics.empty());
}

TEST(YamlParserTest, ParsesMappingAndPreservesOrder) {
    const ParseOutput output = parse(R"(first: one
second: two
)");

    ASSERT_TRUE(output.result.document.has_value()) << diagnostic_summary(output.diagnostics);
    ASSERT_NE(output.result.document->root, nullptr);
    const auto& parsed_mapping = mapping(*output.result.document->root);
    ASSERT_EQ(parsed_mapping.entries.size(), 2U);
    EXPECT_EQ(scalar(node(parsed_mapping.entries[0].key)).value, "first");
    EXPECT_EQ(scalar(node(parsed_mapping.entries[0].value)).value, "one");
    EXPECT_EQ(scalar(node(parsed_mapping.entries[1].key)).value, "second");
    EXPECT_EQ(scalar(node(parsed_mapping.entries[1].value)).value, "two");
    EXPECT_TRUE(output.diagnostics.empty());
}

TEST(YamlParserTest, PreservesDuplicateMappingEntries) {
    const ParseOutput output = parse(R"(key: one
key: two
)");

    ASSERT_TRUE(output.result.document.has_value()) << diagnostic_summary(output.diagnostics);
    const auto& parsed_mapping = mapping(*output.result.document->root);
    ASSERT_EQ(parsed_mapping.entries.size(), 2U);
    EXPECT_EQ(scalar(node(parsed_mapping.entries[0].key)).value, "key");
    EXPECT_EQ(scalar(node(parsed_mapping.entries[1].key)).value, "key");
    EXPECT_EQ(scalar(node(parsed_mapping.entries[0].value)).value, "one");
    EXPECT_EQ(scalar(node(parsed_mapping.entries[1].value)).value, "two");
}

TEST(YamlParserTest, ParsesNestedMappingsAndSequences) {
    const ParseOutput output = parse(R"(root:
  list:
    - first
    - second
)");

    ASSERT_TRUE(output.result.document.has_value()) << diagnostic_summary(output.diagnostics);
    const auto& root_mapping = mapping(*output.result.document->root);
    ASSERT_EQ(root_mapping.entries.size(), 1U);
    ASSERT_EQ(scalar(node(root_mapping.entries[0].key)).value, "root");

    const auto& nested_mapping = mapping(node(root_mapping.entries[0].value));
    ASSERT_EQ(nested_mapping.entries.size(), 1U);
    EXPECT_EQ(scalar(node(nested_mapping.entries[0].key)).value, "list");

    const auto& nested_sequence = sequence(node(nested_mapping.entries[0].value));
    ASSERT_EQ(nested_sequence.elements.size(), 2U);
    EXPECT_EQ(scalar(node(nested_sequence.elements[0])).value, "first");
    EXPECT_EQ(scalar(node(nested_sequence.elements[1])).value, "second");
}

TEST(YamlParserTest, PreservesSourceRangesAndLineColumnInformation) {
    const ParseOutput output = parse(R"(alpha: one
beta:
  gamma: two
)");

    ASSERT_TRUE(output.result.document.has_value()) << diagnostic_summary(output.diagnostics);
    const auto& root_mapping = mapping(*output.result.document->root);
    ASSERT_EQ(root_mapping.entries.size(), 2U);

    const SourceRange alpha_key_range = root_mapping.entries[0].key->source_range;
    const SourceRange gamma_key_range =
        mapping(node(root_mapping.entries[1].value)).entries[0].key->source_range;
    EXPECT_EQ(output.source_manager.line_column(alpha_key_range.begin()),
              std::optional<LineColumn>({1, 1}));
    EXPECT_EQ(output.source_manager.line_column(gamma_key_range.begin()),
              std::optional<LineColumn>({3, 3}));
}

TEST(YamlParserTest, ReportsMalformedYamlWithNormalizedDiagnostic) {
    const ParseOutput output = parse("foo: [1, 2");

    EXPECT_FALSE(output.result.document.has_value());
    ASSERT_EQ(output.diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.diagnostics.diagnostics()[0].id().str(), "BC2101");
    EXPECT_EQ(output.diagnostics.diagnostics()[0].compiler_pass(), "yaml-parser");
}

TEST(YamlParserTest, RejectsMultipleYamlDocuments) {
    const ParseOutput output = parse(R"(---
a: 1
---
b: 2
)");

    ASSERT_FALSE(output.result.document.has_value());
    ASSERT_EQ(output.diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.diagnostics.diagnostics()[0].id().str(), "BC2102");
    EXPECT_EQ(output.diagnostics.diagnostics()[0].compiler_pass(), "yaml-parser");
}

TEST(YamlParserTest, RejectsAnchors) {
    const ParseOutput output = parse("value: &anchor 1");

    ASSERT_FALSE(output.result.document.has_value());
    ASSERT_EQ(output.diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.diagnostics.diagnostics()[0].id().str(), "BC2103");
}

TEST(YamlParserTest, RejectsAliases) {
    const ParseOutput output = parse(R"(value: 1
copy: *anchor
)");

    ASSERT_FALSE(output.result.document.has_value());
    ASSERT_EQ(output.diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.diagnostics.diagnostics()[0].id().str(), "BC2104");
}

TEST(YamlParserTest, RejectsCustomTags) {
    const ParseOutput output = parse("value: !foo 1");

    ASSERT_FALSE(output.result.document.has_value());
    ASSERT_EQ(output.diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.diagnostics.diagnostics()[0].id().str(), "BC2105");
}

TEST(YamlParserTest, RejectsMergeKeys) {
    const ParseOutput output = parse(R"(value:
  <<: 1
)");

    ASSERT_FALSE(output.result.document.has_value());
    ASSERT_EQ(output.diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.diagnostics.diagnostics()[0].id().str(), "BC2106");
}

TEST(YamlParserTest, ParsesSchemaLanguageExampleAsGenericYaml) {
    const ParseOutput output = parse(R"(namespace: breadcrumbs.telemetry
record: Sample
version: 1
type: data
fields:
  samples:
    type: uint32[]
    max_elements: 64
)");

    ASSERT_TRUE(output.result.document.has_value()) << diagnostic_summary(output.diagnostics);
    const auto& root_mapping = mapping(*output.result.document->root);
    ASSERT_EQ(root_mapping.entries.size(), 5U);
    EXPECT_EQ(scalar(node(root_mapping.entries[0].key)).value, "namespace");
    EXPECT_EQ(scalar(node(root_mapping.entries[1].key)).value, "record");
    EXPECT_EQ(scalar(node(root_mapping.entries[2].key)).value, "version");
    EXPECT_EQ(scalar(node(root_mapping.entries[3].key)).value, "type");
    EXPECT_EQ(scalar(node(root_mapping.entries[4].key)).value, "fields");
    EXPECT_TRUE(output.diagnostics.empty());
}

TEST(YamlParserTest, PreservesUnknownBreadcrumbsKeys) {
    const ParseOutput output = parse(R"(unknown_key:
  nested: value
)");

    ASSERT_TRUE(output.result.document.has_value()) << diagnostic_summary(output.diagnostics);
    const auto& root_mapping = mapping(*output.result.document->root);
    ASSERT_EQ(root_mapping.entries.size(), 1U);
    EXPECT_EQ(scalar(node(root_mapping.entries[0].key)).value, "unknown_key");
    EXPECT_TRUE(output.diagnostics.empty());
}

} // namespace
