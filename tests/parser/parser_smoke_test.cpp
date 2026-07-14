#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/support/source_manager.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::ast::ArrayTypeSyntax;
using breadcrumbs::compiler::ast::DeclarationSyntax;
using breadcrumbs::compiler::ast::EnumDeclarationSyntax;
using breadcrumbs::compiler::ast::EnumValueDeclarationSyntax;
using breadcrumbs::compiler::ast::FieldDeclarationSyntax;
using breadcrumbs::compiler::ast::ImportDeclarationSyntax;
using breadcrumbs::compiler::ast::NamespaceDeclarationSyntax;
using breadcrumbs::compiler::ast::QualifiedNameSyntax;
using breadcrumbs::compiler::ast::RecordDeclarationSyntax;
using breadcrumbs::compiler::ast::SchemaFileSyntax;
using breadcrumbs::compiler::ast::TypeReferenceSyntax;
using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::parser::Parser;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceLocation;
using breadcrumbs::compiler::support::SourceManager;
using breadcrumbs::compiler::support::SourceRange;

class ParserTest : public testing::Test {
protected:
    struct ParseOutput {
        SchemaFileSyntax ast;
        DiagnosticEngine diagnostics;
        SourceManager source_manager;
        SourceFileId source_file_id;
    };

    [[nodiscard]] static ParseOutput parse(std::string text) {
        ParseOutput output;
        output.source_file_id =
            output.source_manager.add_source("/test/schema.bc", std::move(text));

        auto result =
            Parser::parse(output.source_manager, output.source_file_id, output.diagnostics);
        output.ast = std::move(result.ast);
        return output;
    }
};

[[nodiscard]] SourceRange range(SourceFileId source_file_id, std::size_t begin, std::size_t end) {
    return SourceRange(SourceLocation(source_file_id, begin), SourceLocation(source_file_id, end));
}

[[nodiscard]] const ImportDeclarationSyntax& as_import(const DeclarationSyntax& declaration) {
    return std::get<ImportDeclarationSyntax>(declaration.value);
}

[[nodiscard]] const NamespaceDeclarationSyntax& as_namespace(const DeclarationSyntax& declaration) {
    return std::get<NamespaceDeclarationSyntax>(declaration.value);
}

[[nodiscard]] const RecordDeclarationSyntax& as_record(const DeclarationSyntax& declaration) {
    return std::get<RecordDeclarationSyntax>(declaration.value);
}

[[nodiscard]] const EnumDeclarationSyntax& as_enum(const DeclarationSyntax& declaration) {
    return std::get<EnumDeclarationSyntax>(declaration.value);
}

TEST_F(ParserTest, ParsesEmptyFile) {
    const ParseOutput output = parse("");

    EXPECT_TRUE(output.diagnostics.empty());
    EXPECT_TRUE(output.ast.declarations.empty());
    EXPECT_EQ(output.ast.source_range, range(output.source_file_id, 0, 0));
}

TEST_F(ParserTest, ParsesMultipleSourcesThroughOneSourceManagerAndPreservesFileIds) {
    SourceManager source_manager;
    DiagnosticEngine diagnostics;

    const SourceFileId first_file =
        source_manager.add_source("/project/first.bc", "record First {\n}\n");
    const SourceFileId second_file =
        source_manager.add_source("/project/second.bc", "record Second {\n}\n");

    const auto first_result = Parser::parse(source_manager, first_file, diagnostics);
    const auto second_result = Parser::parse(source_manager, second_file, diagnostics);

    ASSERT_TRUE(diagnostics.empty());
    ASSERT_EQ(first_result.ast.declarations.size(), 1U);
    ASSERT_EQ(second_result.ast.declarations.size(), 1U);

    const auto& first_record = as_record(*first_result.ast.declarations[0]);
    const auto& second_record = as_record(*second_result.ast.declarations[0]);

    EXPECT_EQ(first_record.name.text, "First");
    EXPECT_EQ(second_record.name.text, "Second");
    EXPECT_EQ(first_result.ast.source_range.begin().file_id(), first_file);
    EXPECT_EQ(second_result.ast.source_range.begin().file_id(), second_file);
    EXPECT_EQ(first_record.source_range.begin().file_id(), first_file);
    EXPECT_EQ(second_record.source_range.begin().file_id(), second_file);
}

TEST_F(ParserTest, ParsesImportNamespaceRecordEnumAndArrays) {
    const ParseOutput output = parse(R"(import breadcrumbs.geo.Location
namespace breadcrumbs.geo {
  record Location {
    latitude: f64
    samples: bytes[16]
  }

  enum FixType {
    none = 0
    two_d = 1
  }
}
)");

    ASSERT_TRUE(output.diagnostics.empty());
    ASSERT_EQ(output.ast.declarations.size(), 2U);

    const auto& import = as_import(*output.ast.declarations[0]);
    EXPECT_EQ(import.imported_name.text(), "breadcrumbs.geo.Location");
    EXPECT_EQ(import.source_range, range(output.source_file_id, 0, 31));

    const auto& top_level_namespace = as_namespace(*output.ast.declarations[1]);
    EXPECT_EQ(top_level_namespace.name.text(), "breadcrumbs.geo");
    ASSERT_EQ(top_level_namespace.declarations.size(), 2U);

    const auto& record = as_record(*top_level_namespace.declarations[0]);
    EXPECT_EQ(record.name.text, "Location");
    ASSERT_EQ(record.fields.size(), 2U);
    EXPECT_EQ(record.fields[0].name.text, "latitude");
    EXPECT_EQ(std::get<TypeReferenceSyntax>(record.fields[0].type).name.text(), "f64");
    EXPECT_EQ(record.fields[1].name.text, "samples");
    const auto& array_type = std::get<ArrayTypeSyntax>(record.fields[1].type);
    EXPECT_EQ(array_type.element_type.name.text(), "bytes");
    EXPECT_EQ(array_type.fixed_size, std::optional<std::size_t>(16));

    const auto& enum_declaration = as_enum(*top_level_namespace.declarations[1]);
    EXPECT_EQ(enum_declaration.name.text, "FixType");
    ASSERT_EQ(enum_declaration.values.size(), 2U);
    EXPECT_EQ(enum_declaration.values[0].name.text, "none");
    EXPECT_EQ(enum_declaration.values[0].value, std::optional<std::string>("0"));
    EXPECT_EQ(enum_declaration.values[1].name.text, "two_d");
    EXPECT_EQ(enum_declaration.values[1].value, std::optional<std::string>("1"));
}

TEST_F(ParserTest, ParsesImportInsideNamespaceAndPreservesOrder) {
    const ParseOutput output = parse(R"(namespace breadcrumbs.geo {
  import breadcrumbs.shared.Location
  record Sample {
    count: uint32
  }
}
)");

    ASSERT_TRUE(output.diagnostics.empty());
    ASSERT_EQ(output.ast.declarations.size(), 1U);

    const auto& top_level_namespace = as_namespace(*output.ast.declarations[0]);
    ASSERT_EQ(top_level_namespace.declarations.size(), 2U);

    const auto& import = as_import(*top_level_namespace.declarations[0]);
    EXPECT_EQ(import.imported_name.text(), "breadcrumbs.shared.Location");
    EXPECT_EQ(import.source_range, range(output.source_file_id, 30, 64));

    const auto& record = as_record(*top_level_namespace.declarations[1]);
    EXPECT_EQ(record.name.text, "Sample");
}

TEST_F(ParserTest, ReportsMalformedImportDeclarationAndRecovers) {
    const ParseOutput output = parse(R"(@deprecated("old")
import breadcrumbs.shared.Location
record Sample {
  count: uint32
}
)");

    ASSERT_EQ(output.diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.diagnostics.diagnostics()[0].id().str(), "BC3001");
    EXPECT_EQ(output.diagnostics.diagnostics()[0].compiler_pass(), "parser");
    EXPECT_EQ(output.diagnostics.diagnostics()[0].message(),
              "annotations are not supported on import declarations");

    ASSERT_EQ(output.ast.declarations.size(), 2U);
    const auto& import = as_import(*output.ast.declarations[0]);
    EXPECT_EQ(import.imported_name.text(), "breadcrumbs.shared.Location");
    const auto& record = as_record(*output.ast.declarations[1]);
    EXPECT_EQ(record.name.text, "Sample");
}

TEST_F(ParserTest, PreservesSourceRangesOnDeclarationsAndTypes) {
    const ParseOutput output = parse(R"(record Location {
  altitude: breadcrumbs.geo.Altitude
}
)");

    ASSERT_TRUE(output.diagnostics.empty());
    ASSERT_EQ(output.ast.declarations.size(), 1U);

    const auto& record = as_record(*output.ast.declarations[0]);
    EXPECT_EQ(record.source_range, range(output.source_file_id, 0, 56));
    EXPECT_EQ(record.fields[0].source_range, range(output.source_file_id, 20, 54));
    EXPECT_EQ(record.fields[0].name.source_range, range(output.source_file_id, 20, 28));

    const auto& type = std::get<TypeReferenceSyntax>(record.fields[0].type);
    EXPECT_EQ(type.source_range, range(output.source_file_id, 30, 54));
    EXPECT_EQ(type.name.text(), "breadcrumbs.geo.Altitude");
}

TEST_F(ParserTest, ParsesAnnotationsOnSyntaxNodes) {
    const ParseOutput output = parse(R"(@deprecated("old")
record Location {
  @unit("m") altitude: f64
}
)");

    ASSERT_TRUE(output.diagnostics.empty());
    ASSERT_EQ(output.ast.declarations.size(), 1U);

    const auto& record = as_record(*output.ast.declarations[0]);
    ASSERT_EQ(record.annotations.size(), 1U);
    EXPECT_EQ(record.annotations[0].name.text(), "deprecated");
    EXPECT_EQ(record.annotations[0].value, std::optional<std::string>("old"));

    ASSERT_EQ(record.fields.size(), 1U);
    ASSERT_EQ(record.fields[0].annotations.size(), 1U);
    EXPECT_EQ(record.fields[0].annotations[0].name.text(), "unit");
    EXPECT_EQ(record.fields[0].annotations[0].value, std::optional<std::string>("m"));
}

TEST_F(ParserTest, ReportsMissingColonInField) {
    const ParseOutput output = parse(R"(record Location {
  latitude f64
}
)");

    ASSERT_EQ(output.diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.diagnostics.diagnostics()[0].id().str(), "BC3002");
    EXPECT_EQ(output.diagnostics.diagnostics()[0].compiler_pass(), "parser");
    EXPECT_EQ(output.diagnostics.diagnostics()[0].source_range(),
              std::optional<SourceRange>(range(output.source_file_id, 29, 32)));
}

TEST_F(ParserTest, ReportsMissingClosingBrace) {
    const ParseOutput output = parse(R"(record Location {
  latitude: f64
)");

    ASSERT_EQ(output.diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(output.diagnostics.diagnostics()[0].id().str(), "BC3002");
    EXPECT_EQ(output.diagnostics.diagnostics()[0].compiler_pass(), "parser");
    EXPECT_EQ(output.diagnostics.diagnostics()[0].message(), "expected '}'");
    ASSERT_EQ(output.ast.declarations.size(), 1U);
}

} // namespace
