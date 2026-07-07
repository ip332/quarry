#include "compiler/ast/ast.hpp"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::ast::ArrayTypeSyntax;
using breadcrumbs::compiler::ast::declaration_kind;
using breadcrumbs::compiler::ast::DeclarationSyntax;
using breadcrumbs::compiler::ast::dump_schema_file;
using breadcrumbs::compiler::ast::EnumDeclarationSyntax;
using breadcrumbs::compiler::ast::EnumValueDeclarationSyntax;
using breadcrumbs::compiler::ast::FieldDeclarationSyntax;
using breadcrumbs::compiler::ast::IdentifierSyntax;
using breadcrumbs::compiler::ast::ImportDeclarationSyntax;
using breadcrumbs::compiler::ast::make_declaration;
using breadcrumbs::compiler::ast::NamespaceDeclarationSyntax;
using breadcrumbs::compiler::ast::QualifiedNameSyntax;
using breadcrumbs::compiler::ast::RecordDeclarationSyntax;
using breadcrumbs::compiler::ast::SchemaFileSyntax;
using breadcrumbs::compiler::ast::type_kind;
using breadcrumbs::compiler::ast::TypeReferenceSyntax;
using breadcrumbs::compiler::ast::TypeSyntax;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceLocation;
using breadcrumbs::compiler::support::SourceRange;

[[nodiscard]] SourceRange range(std::size_t begin, std::size_t end) {
    const SourceFileId file_id(0);
    return SourceRange(SourceLocation(file_id, begin), SourceLocation(file_id, end));
}

[[nodiscard]] IdentifierSyntax identifier(std::string text, std::size_t begin, std::size_t end) {
    return IdentifierSyntax{
        .source_range = range(begin, end),
        .text = std::move(text),
    };
}

[[nodiscard]] QualifiedNameSyntax qualified_name(std::vector<IdentifierSyntax> parts,
                                                 std::size_t begin, std::size_t end) {
    return QualifiedNameSyntax{
        .source_range = range(begin, end),
        .parts = std::move(parts),
    };
}

[[nodiscard]] TypeReferenceSyntax type_reference(QualifiedNameSyntax name, std::size_t begin,
                                                 std::size_t end) {
    return TypeReferenceSyntax{
        .source_range = range(begin, end),
        .name = std::move(name),
    };
}

TEST(AstTest, ConstructsEmptySchemaFile) {
    const SchemaFileSyntax ast{
        .source_range = range(0, 0),
        .declarations = {},
    };

    EXPECT_TRUE(ast.declarations.empty());
    EXPECT_EQ(ast.source_range, range(0, 0));
    EXPECT_EQ(dump_schema_file(ast), "schema_file\n");
}

TEST(AstTest, ConstructsImportDeclaration) {
    const ImportDeclarationSyntax import{
        .source_range = range(0, 31),
        .imported_name = qualified_name({identifier("breadcrumbs", 7, 18),
                                         identifier("geo", 19, 22), identifier("Location", 23, 31)},
                                        7, 31),
    };
    const auto declaration = make_declaration(import);

    ASSERT_NE(declaration, nullptr);
    EXPECT_EQ(declaration_kind(*declaration), "import");
    EXPECT_EQ(import.imported_name.text(), "breadcrumbs.geo.Location");
    EXPECT_EQ(import.source_range, range(0, 31));
}

TEST(AstTest, ConstructsNamespaceWithNestedDeclarations) {
    NamespaceDeclarationSyntax namespace_declaration{
        .source_range = range(0, 50),
        .name =
            qualified_name({identifier("breadcrumbs", 10, 21), identifier("geo", 22, 25)}, 10, 25),
        .declarations = {},
        .annotations = {},
    };
    namespace_declaration.declarations.push_back(make_declaration(RecordDeclarationSyntax{
        .source_range = range(26, 50),
        .name = identifier("Location", 33, 41),
        .fields = {},
        .annotations = {},
    }));

    std::vector<std::unique_ptr<DeclarationSyntax>> declarations;
    declarations.push_back(make_declaration(std::move(namespace_declaration)));
    const SchemaFileSyntax ast{
        .source_range = range(0, 50),
        .declarations = std::move(declarations),
    };

    ASSERT_EQ(ast.declarations.size(), 1U);
    const auto& parsed_namespace = std::get<NamespaceDeclarationSyntax>(ast.declarations[0]->value);
    EXPECT_EQ(parsed_namespace.name.text(), "breadcrumbs.geo");
    ASSERT_EQ(parsed_namespace.declarations.size(), 1U);
    EXPECT_EQ(declaration_kind(*parsed_namespace.declarations[0]), "record");
    EXPECT_EQ(dump_schema_file(ast),
              "schema_file\n  namespace breadcrumbs.geo\n    record Location\n");
}

TEST(AstTest, ConstructsRecordWithFields) {
    const TypeSyntax int32_type =
        type_reference(qualified_name({identifier("int32", 25, 30)}, 25, 30), 25, 30);
    const FieldDeclarationSyntax latitude{
        .source_range = range(14, 30),
        .name = identifier("latitude", 14, 22),
        .type = int32_type,
        .annotations = {},
    };
    const RecordDeclarationSyntax record{
        .source_range = range(0, 30),
        .name = identifier("Location", 7, 15),
        .fields = {latitude},
        .annotations = {},
    };

    ASSERT_EQ(record.fields.size(), 1U);
    EXPECT_EQ(record.name.text, "Location");
    EXPECT_EQ(record.fields[0].name.text, "latitude");
    EXPECT_EQ(type_kind(record.fields[0].type), "type_reference");
    EXPECT_EQ(std::get<TypeReferenceSyntax>(record.fields[0].type).name.text(), "int32");
}

TEST(AstTest, ConstructsEnumWithValues) {
    const EnumDeclarationSyntax enum_declaration{
        .source_range = range(0, 40),
        .name = identifier("FixType", 5, 12),
        .values =
            {
                EnumValueDeclarationSyntax{
                    .source_range = range(14, 21),
                    .name = identifier("none", 14, 18),
                    .value = "0",
                    .annotations = {},
                },
                EnumValueDeclarationSyntax{
                    .source_range = range(23, 32),
                    .name = identifier("two_d", 23, 28),
                    .value = "1",
                    .annotations = {},
                },
            },
        .annotations = {},
    };

    ASSERT_EQ(enum_declaration.values.size(), 2U);
    EXPECT_EQ(enum_declaration.name.text, "FixType");
    EXPECT_EQ(enum_declaration.values[0].name.text, "none");
    EXPECT_EQ(enum_declaration.values[0].value, std::optional<std::string>("0"));
}

TEST(AstTest, ConstructsPrimitiveAndQualifiedTypeReferences) {
    const TypeReferenceSyntax primitive =
        type_reference(qualified_name({identifier("uint32", 0, 6)}, 0, 6), 0, 6);
    const TypeReferenceSyntax qualified =
        type_reference(qualified_name({identifier("breadcrumbs", 0, 11), identifier("geo", 12, 15),
                                       identifier("Location", 16, 24)},
                                      0, 24),
                       0, 24);

    EXPECT_EQ(primitive.name.text(), "uint32");
    EXPECT_EQ(qualified.name.text(), "breadcrumbs.geo.Location");
    EXPECT_EQ(primitive.source_range, range(0, 6));
    EXPECT_EQ(qualified.source_range, range(0, 24));
}

TEST(AstTest, ConstructsFixedArrayTypeSyntax) {
    const ArrayTypeSyntax array_type{
        .source_range = range(0, 13),
        .element_type = type_reference(qualified_name({identifier("Satellite", 0, 9)}, 0, 9), 0, 9),
        .fixed_size = 64,
    };
    const TypeSyntax type = array_type;

    EXPECT_EQ(type_kind(type), "array_type");
    const ArrayTypeSyntax& parsed_array = std::get<ArrayTypeSyntax>(type);
    EXPECT_EQ(parsed_array.element_type.name.text(), "Satellite");
    EXPECT_EQ(parsed_array.fixed_size, std::optional<std::size_t>(64));
    EXPECT_EQ(parsed_array.source_range, range(0, 13));
}

TEST(AstTest, PreservesSourceRangesOnDeclarationsAndTypes) {
    const FieldDeclarationSyntax field{
        .source_range = range(10, 20),
        .name = identifier("altitude", 10, 18),
        .type = type_reference(qualified_name({identifier("int32", 19, 24)}, 19, 24), 19, 24),
        .annotations = {},
    };

    EXPECT_EQ(field.source_range, range(10, 20));
    EXPECT_EQ(field.name.source_range, range(10, 18));
    EXPECT_EQ(std::get<TypeReferenceSyntax>(field.type).source_range, range(19, 24));
}

TEST(AstTest, TypeReferencesRemainUnresolvedSyntax) {
    const TypeReferenceSyntax reference =
        type_reference(qualified_name({identifier("breadcrumbs", 0, 11), identifier("geo", 12, 15),
                                       identifier("Location", 16, 24)},
                                      0, 24),
                       0, 24);

    EXPECT_EQ(reference.name.parts.size(), 3U);
    EXPECT_EQ(reference.name.text(), "breadcrumbs.geo.Location");
    EXPECT_EQ(type_kind(TypeSyntax{reference}), "type_reference");
}

} // namespace
