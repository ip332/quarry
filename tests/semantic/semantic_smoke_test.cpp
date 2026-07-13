#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/symbols/symbols.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::ast::SchemaFileSyntax;
using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::parser::Parser;
using breadcrumbs::compiler::semantic::SemanticArrayType;
using breadcrumbs::compiler::semantic::SemanticField;
using breadcrumbs::compiler::semantic::SemanticModel;
using breadcrumbs::compiler::semantic::SemanticPrimitiveType;
using breadcrumbs::compiler::semantic::SemanticRecord;
using breadcrumbs::compiler::semantic::SemanticRecordType;
using breadcrumbs::compiler::semantic::SemanticType;
using breadcrumbs::compiler::semantic::SemanticValidator;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceManager;
using breadcrumbs::compiler::symbols::NamespaceBuilder;
using breadcrumbs::compiler::symbols::SymbolTable;

struct AnalysisOutput {
    SchemaFileSyntax ast;
    SourceManager source_manager;
    SourceFileId source_file_id;
    DiagnosticEngine parser_diagnostics;
    DiagnosticEngine symbol_diagnostics;
    DiagnosticEngine semantic_diagnostics;
    std::unique_ptr<SymbolTable> symbol_table;
    SemanticModel semantic_model;
};

[[nodiscard]] AnalysisOutput analyze(std::string text,
                                     const std::function<void(SchemaFileSyntax&)>& ast_mutator);

[[nodiscard]] AnalysisOutput analyze(std::string text) { return analyze(std::move(text), {}); }

[[nodiscard]] AnalysisOutput analyze(std::string text,
                                     const std::function<void(SchemaFileSyntax&)>& ast_mutator) {
    AnalysisOutput output;
    output.source_file_id = output.source_manager.add_source("/test/schema.brd", std::move(text));

    auto parse_result =
        Parser::parse(output.source_manager, output.source_file_id, output.parser_diagnostics);
    output.ast = std::move(parse_result.ast);
    if (ast_mutator) {
        ast_mutator(output.ast);
    }

    NamespaceBuilder namespace_builder;
    output.symbol_table = std::make_unique<SymbolTable>(
        namespace_builder.build(output.ast, output.symbol_diagnostics));

    SemanticValidator validator;
    output.semantic_model =
        validator.validate(output.ast, *output.symbol_table, output.semantic_diagnostics);
    return output;
}

[[nodiscard]] bool expect_clean_pipeline(const AnalysisOutput& output) {
    return output.parser_diagnostics.empty() && output.symbol_diagnostics.empty();
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
    const AnalysisOutput output =
        analyze(R"(record Example {
  active: bool
  count: int32
  total: uint64
  ratio: float64
  label: string
  payload: bytes
}
)",
                [](SchemaFileSyntax& ast) {
                    auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                        ast.declarations[0]->value);
                    record.fields[4].max_bytes = 16;
                    record.fields[4].max_bytes_source_range = record.fields[4].source_range;
                    record.fields[5].max_bytes = 4;
                    record.fields[5].max_bytes_source_range = record.fields[5].source_range;
                });

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_EQ(output.semantic_model.records.size(), 1U);
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
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
    const AnalysisOutput output =
        analyze(R"(record Example {
  bool_value: bool
  i8_short: i8
  i8_long: int8
  u8_short: u8
  u8_long: uint8
  i16_short: i16
  i16_long: int16
  u16_short: u16
  u16_long: uint16
  i32_short: i32
  i32_long: int32
  u32_short: u32
  u32_long: uint32
  i64_short: i64
  i64_long: int64
  u64_short: u64
  u64_long: uint64
  f32_short: f32
  f32_long: float32
  f64_short: f64
  f64_long: float64
  text: string
  payload: bytes
}
)",
                [](SchemaFileSyntax& ast) {
                    auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                        ast.declarations[0]->value);
                    record.fields[21].max_bytes = 16;
                    record.fields[21].max_bytes_source_range = record.fields[21].source_range;
                    record.fields[22].max_bytes = 4;
                    record.fields[22].max_bytes_source_range = record.fields[22].source_range;
                });

    ASSERT_TRUE(expect_clean_pipeline(output));
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
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
    const AnalysisOutput output = analyze(
        R"(record Example {
  active: bool
  label: string
  payload: bytes
  samples: u32
}
)",
        [](SchemaFileSyntax& ast) {
            auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                ast.declarations[0]->value);
            record.version = 7;
            record.version_source_range = record.source_range;
            record.record_type_spelling = "data";
            record.record_type_source_range = record.source_range;

            record.fields[1].max_bytes = 16;
            record.fields[1].max_bytes_source_range = record.fields[1].source_range;

            record.fields[2].max_bytes = 4;
            record.fields[2].max_bytes_source_range = record.fields[2].source_range;

            record.fields[3].type = breadcrumbs::compiler::ast::ArrayTypeSyntax{
                .source_range = record.fields[3].source_range,
                .element_type =
                    breadcrumbs::compiler::ast::TypeReferenceSyntax{
                        .source_range = record.fields[3].source_range,
                        .name =
                            breadcrumbs::compiler::ast::QualifiedNameSyntax{
                                .source_range = record.fields[3].source_range,
                                .parts =
                                    {
                                        breadcrumbs::compiler::ast::IdentifierSyntax{
                                            .source_range = record.fields[3].source_range,
                                            .text = "u32",
                                        },
                                    },
                            },
                    },
                .kind = breadcrumbs::compiler::ast::ArrayTypeSyntaxKind::BoundedVariableLength,
                .fixed_size = std::nullopt,
                .fixed_size_source_range = breadcrumbs::compiler::support::SourceRange::invalid(),
            };
            record.fields[3].max_elements = 64;
            record.fields[3].max_elements_source_range = record.fields[3].source_range;
        });

    ASSERT_TRUE(expect_clean_pipeline(output));
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
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

TEST(SemanticSmokeTest, RecognizesVersionWithoutAValidSourceRange) {
    const AnalysisOutput output = analyze(
        R"(record Example {
  active: bool
}
)",
        [](SchemaFileSyntax& ast) {
            auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                ast.declarations[0]->value);
            record.version = 7;
            record.version_source_range = breadcrumbs::compiler::support::SourceRange::invalid();
        });

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->version.has_value());
    EXPECT_EQ(*record->version, 7U);
    EXPECT_FALSE(record->record_type.has_value());
}

TEST(SemanticSmokeTest, RejectsZeroVersionWithAValidSourceRange) {
    const AnalysisOutput output = analyze(
        R"(record Example {
  active: bool
}
)",
        [](SchemaFileSyntax& ast) {
            auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                ast.declarations[0]->value);
            record.version = 0;
            record.version_source_range = record.source_range;
        });

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->version.has_value());
}

TEST(SemanticSmokeTest, RejectsNegativeVersion) {
    const AnalysisOutput output = analyze(
        R"(record Example {
  active: bool
}
)",
        [](SchemaFileSyntax& ast) {
            auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                ast.declarations[0]->value);
            record.version = -1;
            record.version_source_range = record.source_range;
        });

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->version.has_value());
}

TEST(SemanticSmokeTest, RejectsVersionGreaterThanUint32) {
    const AnalysisOutput output = analyze(
        R"(record Example {
  active: bool
}
)",
        [](SchemaFileSyntax& ast) {
            auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                ast.declarations[0]->value);
            record.version = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) +
                            1;
            record.version_source_range = record.source_range;
        });

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->version.has_value());
}

TEST(SemanticSmokeTest, RejectsInvalidLogicalRecordTypeWithAValidSourceRange) {
    const AnalysisOutput output = analyze(
        R"(record Example {
  active: bool
}
)",
        [](SchemaFileSyntax& ast) {
            auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                ast.declarations[0]->value);
            record.version = 1;
            record.version_source_range = record.source_range;
            record.record_type_spelling = "bogus";
            record.record_type_source_range = record.source_range;
        });

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5005");
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->version.has_value());
    EXPECT_FALSE(record->record_type.has_value());
}

TEST(SemanticSmokeTest, RejectsInvalidLogicalRecordTypeWithoutAValidSourceRange) {
    const AnalysisOutput output = analyze(
        R"(record Example {
  active: bool
}
)",
        [](SchemaFileSyntax& ast) {
            auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                ast.declarations[0]->value);
            record.version = 1;
            record.version_source_range = record.source_range;
            record.record_type_spelling = "bogus";
            record.record_type_source_range = breadcrumbs::compiler::support::SourceRange::invalid();
        });

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5005");
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->version.has_value());
    EXPECT_FALSE(record->record_type.has_value());
}

TEST(SemanticSmokeTest, PreservesLegacyAbsentVersionAndRecordTypeWithoutDiagnostics) {
    const AnalysisOutput output = analyze(
        R"(record Example {
  active: bool
}
)",
        [](SchemaFileSyntax& ast) {
            auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                ast.declarations[0]->value);
            record.version = 0;
            record.version_source_range = breadcrumbs::compiler::support::SourceRange::invalid();
            record.record_type_spelling.clear();
            record.record_type_source_range = breadcrumbs::compiler::support::SourceRange::invalid();
        });

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->version.has_value());
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
        const AnalysisOutput output = analyze(
            R"(record Example {
  value: bool
}
)",
            [spelling](SchemaFileSyntax& ast) {
                auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                    ast.declarations[0]->value);
                record.version = 1;
                record.version_source_range = record.source_range;
                record.record_type_spelling = std::string(spelling);
                record.record_type_source_range = record.source_range;
            });

        ASSERT_TRUE(expect_clean_pipeline(output)) << spelling;
        const SemanticRecord* record = find_record(output.semantic_model, "Example");
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
        const AnalysisOutput output = analyze(
            R"(record Example {
  label: string
}
)",
            [test_case](SchemaFileSyntax& ast) {
                auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                    ast.declarations[0]->value);
                if (test_case.max_bytes.has_value()) {
                    record.fields[0].max_bytes = *test_case.max_bytes;
                    record.fields[0].max_bytes_source_range = record.fields[0].source_range;
                }
            });

        ASSERT_TRUE(expect_clean_pipeline(output)) << test_case.name;
        ASSERT_FALSE(output.semantic_diagnostics.empty()) << test_case.name;
        EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(),
                  *test_case.expected_id)
            << test_case.name;
        const SemanticRecord* record = find_record(output.semantic_model, "Example");
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
        const AnalysisOutput output = analyze(
            R"(record Example {
  payload: bytes
}
)",
            [test_case](SchemaFileSyntax& ast) {
                auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                    ast.declarations[0]->value);
                if (test_case.max_bytes.has_value()) {
                    record.fields[0].max_bytes = *test_case.max_bytes;
                    record.fields[0].max_bytes_source_range = record.fields[0].source_range;
                }
            });

        ASSERT_TRUE(expect_clean_pipeline(output)) << test_case.name;
        ASSERT_FALSE(output.semantic_diagnostics.empty()) << test_case.name;
        EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004")
            << test_case.name;
        const SemanticRecord* record = find_record(output.semantic_model, "Example");
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
        const AnalysisOutput output = analyze(
            R"(record Example {
  samples: u32
}
)",
            [test_case](SchemaFileSyntax& ast) {
                auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                    ast.declarations[0]->value);
                record.fields[0].type = breadcrumbs::compiler::ast::ArrayTypeSyntax{
                    .source_range = record.fields[0].source_range,
                    .element_type =
                        breadcrumbs::compiler::ast::TypeReferenceSyntax{
                            .source_range = record.fields[0].source_range,
                            .name =
                                breadcrumbs::compiler::ast::QualifiedNameSyntax{
                                    .source_range = record.fields[0].source_range,
                                    .parts =
                                        {
                                            breadcrumbs::compiler::ast::IdentifierSyntax{
                                                .source_range = record.fields[0].source_range,
                                                .text = "u32",
                                            },
                                        },
                                },
                        },
                    .kind = breadcrumbs::compiler::ast::ArrayTypeSyntaxKind::BoundedVariableLength,
                    .fixed_size = std::nullopt,
                    .fixed_size_source_range =
                        breadcrumbs::compiler::support::SourceRange::invalid(),
                };
                if (test_case.max_elements.has_value()) {
                    record.fields[0].max_elements = *test_case.max_elements;
                    record.fields[0].max_elements_source_range = record.fields[0].source_range;
                }
            });

        ASSERT_TRUE(expect_clean_pipeline(output)) << test_case.name;
        ASSERT_FALSE(output.semantic_diagnostics.empty()) << test_case.name;
        EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004")
            << test_case.name;
        const SemanticRecord* record = find_record(output.semantic_model, "Example");
        ASSERT_NE(record, nullptr);
        EXPECT_TRUE(record->fields.empty()) << test_case.name;
    }
}

TEST(SemanticSmokeTest, ReportsInvalidBoundPlacementOnOtherFieldKinds) {
    const AnalysisOutput output =
        analyze(R"(record Example {
  active: bool
}
)",
                [](SchemaFileSyntax& ast) {
                    auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                        ast.declarations[0]->value);
                    record.fields[0].max_bytes = 16;
                    record.fields[0].max_bytes_source_range = record.fields[0].source_range;
                });

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->fields.empty());
}

TEST(SemanticSmokeTest, ReportsInvalidMaxElementsOnNonArrayFields) {
    const AnalysisOutput output =
        analyze(R"(record Example {
  count: u32
}
)",
                [](SchemaFileSyntax& ast) {
                    auto& record = std::get<breadcrumbs::compiler::ast::RecordDeclarationSyntax>(
                        ast.declarations[0]->value);
                    record.fields[0].max_elements = 4;
                    record.fields[0].max_elements_source_range = record.fields[0].source_range;
                });

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_FALSE(output.semantic_diagnostics.empty());
    EXPECT_EQ(output.semantic_diagnostics.diagnostics().front().id().str(), "BC5004");
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->fields.empty());
}

TEST(SemanticSmokeTest, PreservesRecordAndFieldDeclarationOrder) {
    const AnalysisOutput output = analyze(R"(record Beta {
  second: u32
  first: bool
}

record Alpha {
  left: bool
  right: bool
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_EQ(output.semantic_model.records.size(), 2U);
    EXPECT_EQ(output.semantic_model.records[0].fqn, "Beta");
    EXPECT_EQ(output.semantic_model.records[1].fqn, "Alpha");
    ASSERT_EQ(output.semantic_model.records[0].fields.size(), 2U);
    EXPECT_EQ(output.semantic_model.records[0].fields[0].name, "second");
    EXPECT_EQ(output.semantic_model.records[0].fields[1].name, "first");
    ASSERT_EQ(output.semantic_model.records[1].fields.size(), 2U);
    EXPECT_EQ(output.semantic_model.records[1].fields[0].name, "left");
    EXPECT_EQ(output.semantic_model.records[1].fields[1].name, "right");
}

TEST(SemanticSmokeTest, CollectsEmptyFileIntoEmptySymbolTable) {
    const AnalysisOutput output = analyze("");

    ASSERT_TRUE(expect_clean_pipeline(output));
    EXPECT_TRUE(output.symbol_table->global_scope().symbols().empty());
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, CollectsSingleTopLevelDeclaration) {
    const AnalysisOutput output = analyze(R"(record Example {
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    const auto& global = output.symbol_table->global_scope();
    ASSERT_EQ(global.symbols().size(), 1U);
    EXPECT_EQ(global.symbols()[0].name, "Example");
    EXPECT_NE(output.symbol_table->lookup(
                  breadcrumbs::compiler::ast::QualifiedNameSyntax{
                      .source_range = {},
                      .parts =
                          {
                              breadcrumbs::compiler::ast::IdentifierSyntax{
                                  .source_range = {},
                                  .text = "Example",
                              },
                          },
                  },
                  global),
              nullptr);
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, CollectsNestedNamespaceDeclarations) {
    const AnalysisOutput output = analyze(R"(namespace breadcrumbs.geo {
  record Location {
  }
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    const auto& global = output.symbol_table->global_scope();
    const auto* breadcrumbs = global.find_local("breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    ASSERT_NE(breadcrumbs->child_scope, nullptr);
    const auto* geo = breadcrumbs->child_scope->find_local("geo");
    ASSERT_NE(geo, nullptr);
    ASSERT_NE(geo->child_scope, nullptr);
    EXPECT_NE(output.symbol_table->lookup_qualified(
                  breadcrumbs::compiler::ast::QualifiedNameSyntax{
                      .source_range = {},
                      .parts =
                          {
                              breadcrumbs::compiler::ast::IdentifierSyntax{
                                  .source_range = {},
                                  .text = "breadcrumbs",
                              },
                              breadcrumbs::compiler::ast::IdentifierSyntax{
                                  .source_range = {},
                                  .text = "geo",
                              },
                              breadcrumbs::compiler::ast::IdentifierSyntax{
                                  .source_range = {},
                                  .text = "Location",
                              },
                          },
                  },
                  global),
              nullptr);
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, ReportsDuplicateDeclarationsInTheSameScope) {
    const AnalysisOutput output = analyze(R"(record Example {
}

record Example {
}
)");

    EXPECT_TRUE(output.parser_diagnostics.empty());
    ASSERT_EQ(output.symbol_diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.symbol_diagnostics.diagnostics()[0].id().str(), "BC4001");
    EXPECT_EQ(output.symbol_diagnostics.diagnostics()[0].compiler_pass(), "symbols");
}

TEST(SemanticSmokeTest, ResolvesSimpleAndQualifiedNames) {
    const AnalysisOutput output = analyze(R"(namespace breadcrumbs.geo {
  record Location {
  }

  record Example {
    simple: Location
    qualified: breadcrumbs.geo.Location
  }
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    const auto& global = output.symbol_table->global_scope();
    const auto* breadcrumbs = global.find_local("breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    ASSERT_NE(breadcrumbs->child_scope, nullptr);
    const auto* geo = breadcrumbs->child_scope->find_local("geo");
    ASSERT_NE(geo, nullptr);
    ASSERT_NE(geo->child_scope, nullptr);

    EXPECT_NE(output.symbol_table->lookup_unqualified("Location", *geo->child_scope), nullptr);
    EXPECT_NE(output.symbol_table->lookup_unqualified("Example", *geo->child_scope), nullptr);
    EXPECT_NE(output.symbol_table->lookup(
                  breadcrumbs::compiler::ast::QualifiedNameSyntax{
                      .source_range = {},
                      .parts =
                          {
                              breadcrumbs::compiler::ast::IdentifierSyntax{
                                  .source_range = {},
                                  .text = "breadcrumbs",
                              },
                              breadcrumbs::compiler::ast::IdentifierSyntax{
                                  .source_range = {},
                                  .text = "geo",
                              },
                              breadcrumbs::compiler::ast::IdentifierSyntax{
                                  .source_range = {},
                                  .text = "Location",
                              },
                          },
                  },
                  global),
              nullptr);
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, ResolvesUnqualifiedNamedTypesInCurrentScope) {
    const AnalysisOutput output = analyze(R"(namespace breadcrumbs.geo {
  record Location {
  }

  record Route {
    origin: Location
  }
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    const SemanticRecord* route = find_record(output.semantic_model, "breadcrumbs.geo.Route");
    ASSERT_NE(route, nullptr);
    ASSERT_EQ(route->fields.size(), 1U);
    expect_record_reference_type(route->fields[0], "breadcrumbs.geo.Location");
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, ResolvesNamedTypesThroughEnclosingScopes) {
    const AnalysisOutput output = analyze(R"(namespace breadcrumbs.geo {
  record Location {
  }

  namespace detail {
    record Path {
      start: Location
    }
  }
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    const SemanticRecord* path = find_record(output.semantic_model, "breadcrumbs.geo.detail.Path");
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(path->fields.size(), 1U);
    expect_record_reference_type(path->fields[0], "breadcrumbs.geo.Location");
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, ResolvesQualifiedNamedTypes) {
    const AnalysisOutput output = analyze(R"(namespace breadcrumbs.vehicle {
  record Journey {
    destination: breadcrumbs.geo.Location
  }
}

namespace breadcrumbs.geo {
  record Location {
  }
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    const auto* breadcrumbs = output.symbol_table->global_scope().find_local("breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    ASSERT_NE(breadcrumbs->child_scope, nullptr);
    const auto* vehicle = breadcrumbs->child_scope->find_local("vehicle");
    ASSERT_NE(vehicle, nullptr);
    ASSERT_NE(vehicle->child_scope, nullptr);

    const breadcrumbs::compiler::ast::QualifiedNameSyntax name{
        .source_range = {},
        .parts =
            {
                breadcrumbs::compiler::ast::IdentifierSyntax{
                    .source_range = {},
                    .text = "breadcrumbs",
                },
                breadcrumbs::compiler::ast::IdentifierSyntax{
                    .source_range = {},
                    .text = "geo",
                },
                breadcrumbs::compiler::ast::IdentifierSyntax{
                    .source_range = {},
                    .text = "Location",
                },
            },
    };
    ASSERT_NE(output.symbol_table->resolve(name, *vehicle->child_scope), nullptr);

    const SemanticRecord* journey =
        find_record(output.semantic_model, "breadcrumbs.vehicle.Journey");
    ASSERT_NE(journey, nullptr);
    ASSERT_EQ(journey->fields.size(), 1U);
    expect_record_reference_type(journey->fields[0], "breadcrumbs.geo.Location");

    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, ResolvesRecordAndEnumReferencesToCanonicalFQNs) {
    const AnalysisOutput output = analyze(R"(namespace breadcrumbs.geo {
  enum Mode {
    automatic = 0
    manual = 1
  }

  record Location {
  }

  record Route {
    relative_location: Location
    qualified_location: breadcrumbs.geo.Location
    relative_mode: Mode
    qualified_mode: breadcrumbs.geo.Mode
  }
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    const SemanticRecord* route = find_record(output.semantic_model, "breadcrumbs.geo.Route");
    ASSERT_NE(route, nullptr);
    ASSERT_EQ(route->fields.size(), 4U);
    expect_record_reference_type(route->fields[0], "breadcrumbs.geo.Location");
    expect_record_reference_type(route->fields[1], "breadcrumbs.geo.Location");
    expect_enum_reference_type(route->fields[2], "breadcrumbs.geo.Mode");
    expect_enum_reference_type(route->fields[3], "breadcrumbs.geo.Mode");
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, AcceptsSameUnqualifiedTypeNameInDifferentNamespaces) {
    const AnalysisOutput output = analyze(R"(namespace breadcrumbs.geo {
  record Location {
  }

  record Route {
    origin: Location
  }
}

namespace breadcrumbs.telemetry {
  record Location {
  }

  record Event {
    source: Location
  }
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    const SemanticRecord* route = find_record(output.semantic_model, "breadcrumbs.geo.Route");
    ASSERT_NE(route, nullptr);
    ASSERT_EQ(route->fields.size(), 1U);
    expect_record_reference_type(route->fields[0], "breadcrumbs.geo.Location");
    const SemanticRecord* event = find_record(output.semantic_model, "breadcrumbs.telemetry.Event");
    ASSERT_NE(event, nullptr);
    ASSERT_EQ(event->fields.size(), 1U);
    expect_record_reference_type(event->fields[0], "breadcrumbs.telemetry.Location");
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
}

TEST(SemanticSmokeTest, ReportsUnresolvedNamedTypeDiagnostics) {
    const AnalysisOutput output = analyze(R"(record Example {
  missing: MissingType
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_EQ(output.semantic_diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].id().str(), "BC5001");
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].compiler_pass(), "semantic");
    const SemanticRecord* record = find_record(output.semantic_model, "Example");
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->fields.empty());
}

TEST(SemanticSmokeTest, ReportsNamespaceUsedAsTypeDiagnostics) {
    const AnalysisOutput output = analyze(R"(namespace breadcrumbs.vehicle {
  namespace geo {
  }

  record Journey {
    destination: geo
  }
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_EQ(output.semantic_diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].id().str(), "BC5002");
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].compiler_pass(), "semantic");
    const SemanticRecord* journey =
        find_record(output.semantic_model, "breadcrumbs.vehicle.Journey");
    ASSERT_NE(journey, nullptr);
    EXPECT_TRUE(journey->fields.empty());
}

TEST(SemanticSmokeTest, ReportsLexicalShadowingInQualifiedTypeResolution) {
    const AnalysisOutput output = analyze(R"(namespace breadcrumbs.geo {
  record Location {
  }
}

namespace breadcrumbs.vehicle {
  record geo {
  }

  record Journey {
    destination: geo.Location
  }
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_EQ(output.semantic_diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].id().str(), "BC5001");
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].compiler_pass(), "semantic");
    const SemanticRecord* journey =
        find_record(output.semantic_model, "breadcrumbs.vehicle.Journey");
    ASSERT_NE(journey, nullptr);
    EXPECT_TRUE(journey->fields.empty());
}

TEST(SemanticSmokeTest, ContinuesAfterMultipleSemanticErrors) {
    const AnalysisOutput output = analyze(R"(namespace breadcrumbs.geo {
  record Location {
  }
}

namespace breadcrumbs.vehicle {
  record geo {
  }

  record Journey {
    destination: breadcrumbs.geo.Location
    shadowed: geo.Location
    missing: MissingType
    samples: bytes[16]
    home: breadcrumbs.geo.Location
  }
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    ASSERT_EQ(output.semantic_diagnostics.diagnostics().size(), 3U);
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[0].id().str(), "BC5001");
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[1].id().str(), "BC5001");
    EXPECT_EQ(output.semantic_diagnostics.diagnostics()[2].id().str(), "BC5003");
    const SemanticRecord* journey =
        find_record(output.semantic_model, "breadcrumbs.vehicle.Journey");
    ASSERT_NE(journey, nullptr);
    ASSERT_EQ(journey->fields.size(), 2U);
    EXPECT_EQ(journey->fields[0].name, "destination");
    expect_record_reference_type(journey->fields[0], "breadcrumbs.geo.Location");
    EXPECT_EQ(journey->fields[1].name, "home");
    expect_record_reference_type(journey->fields[1], "breadcrumbs.geo.Location");
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
