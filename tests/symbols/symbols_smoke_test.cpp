#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/imports/imports.hpp"
#include "compiler/symbols/symbols.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::ast::DeclarationPtr;
using breadcrumbs::compiler::ast::EnumDeclarationSyntax;
using breadcrumbs::compiler::ast::IdentifierSyntax;
using breadcrumbs::compiler::ast::NamespaceDeclarationSyntax;
using breadcrumbs::compiler::ast::QualifiedNameSyntax;
using breadcrumbs::compiler::ast::RecordDeclarationSyntax;
using breadcrumbs::compiler::ast::SchemaFileSyntax;
using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::imports::CompilationUnit;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceLocation;
using breadcrumbs::compiler::support::SourceRange;
using breadcrumbs::compiler::symbols::NamespaceBuilder;
using breadcrumbs::compiler::symbols::Scope;
using breadcrumbs::compiler::symbols::Symbol;
using breadcrumbs::compiler::symbols::SymbolKind;

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

[[nodiscard]] DeclarationPtr make_record(std::string name, std::size_t begin, std::size_t end) {
    return breadcrumbs::compiler::ast::make_declaration(RecordDeclarationSyntax{
        .source_range = range(begin, end),
        .name = identifier(name, begin + 7, begin + 7 + name.size()),
        .fields = {},
        .annotations = {},
    });
}

[[nodiscard]] DeclarationPtr make_enum(std::string name, std::size_t begin, std::size_t end) {
    return breadcrumbs::compiler::ast::make_declaration(EnumDeclarationSyntax{
        .source_range = range(begin, end),
        .name = identifier(name, begin + 5, begin + 5 + name.size()),
        .values = {},
        .annotations = {},
    });
}

[[nodiscard]] DeclarationPtr make_namespace(std::string name, std::size_t begin, std::size_t end,
                                            std::vector<DeclarationPtr> declarations = {}) {
    NamespaceDeclarationSyntax namespace_declaration{
        .source_range = range(begin, end),
        .name = qualified_name({identifier(name, begin + 10, begin + 10 + name.size())}, begin + 10,
                               begin + 10 + name.size()),
        .declarations = std::move(declarations),
        .annotations = {},
    };
    return breadcrumbs::compiler::ast::make_declaration(std::move(namespace_declaration));
}

[[nodiscard]] SchemaFileSyntax schema_file(std::vector<DeclarationPtr> declarations) {
    return SchemaFileSyntax{
        .source_range = range(0, 100),
        .declarations = std::move(declarations),
    };
}

[[nodiscard]] CompilationUnit unit_from(const SchemaFileSyntax& ast) {
    CompilationUnit unit;
    unit.asts.push_back(&ast);
    return unit;
}

[[nodiscard]] const Symbol* find_symbol(const Scope& scope, std::string_view name) {
    return scope.find_local(name);
}

TEST(SymbolsSmokeTest, CollectsTopLevelDeclarations) {
    std::vector<DeclarationPtr> declarations;
    declarations.push_back(make_namespace("breadcrumbs", 0, 31));
    declarations.push_back(make_record("Location", 32, 48));
    declarations.push_back(make_enum("FixType", 49, 61));
    const auto ast = schema_file(std::move(declarations));
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(unit_from(ast), diagnostics);

    ASSERT_TRUE(diagnostics.empty());
    const Scope& global = model.global_scope();
    ASSERT_EQ(global.symbols().size(), 3U);
    EXPECT_EQ(global.symbols()[0].kind, SymbolKind::Namespace);
    EXPECT_EQ(global.symbols()[1].kind, SymbolKind::Record);
    EXPECT_EQ(global.symbols()[2].kind, SymbolKind::Enum);
    EXPECT_EQ(global.symbols()[0].name, "breadcrumbs");
    EXPECT_EQ(global.symbols()[1].name, "Location");
    EXPECT_EQ(global.symbols()[2].name, "FixType");
}

TEST(SymbolsSmokeTest, BuildsNestedNamespaceScopes) {
    std::vector<DeclarationPtr> declarations;
    std::vector<DeclarationPtr> nested_declarations;
    nested_declarations.push_back(
        breadcrumbs::compiler::ast::make_declaration(RecordDeclarationSyntax{
            .source_range = range(26, 44),
            .name = identifier("Location", 33, 41),
            .fields = {},
            .annotations = {},
        }));
    declarations.push_back(breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
        .source_range = range(0, 62),
        .name =
            qualified_name({identifier("breadcrumbs", 10, 21), identifier("geo", 22, 25)}, 10, 25),
        .declarations = std::move(nested_declarations),
        .annotations = {},
    }));
    const auto ast = schema_file(std::move(declarations));
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(unit_from(ast), diagnostics);

    ASSERT_TRUE(diagnostics.empty());
    const Scope& global = model.global_scope();
    const Symbol* breadcrumbs = find_symbol(global, "breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    ASSERT_EQ(breadcrumbs->kind, SymbolKind::Namespace);
    ASSERT_NE(breadcrumbs->child_scope, nullptr);

    const Scope& breadcrumbs_scope = *breadcrumbs->child_scope;
    const Symbol* geo = find_symbol(breadcrumbs_scope, "geo");
    ASSERT_NE(geo, nullptr);
    ASSERT_NE(geo->child_scope, nullptr);

    const Scope& geo_scope = *geo->child_scope;
    const Symbol* location = find_symbol(geo_scope, "Location");
    ASSERT_NE(location, nullptr);
    EXPECT_EQ(location->kind, SymbolKind::Record);
}

TEST(SymbolsSmokeTest, ResolvesCurrentAndEnclosingScopeNames) {
    std::vector<DeclarationPtr> declarations;
    std::vector<DeclarationPtr> nested_declarations;
    nested_declarations.push_back(
        breadcrumbs::compiler::ast::make_declaration(RecordDeclarationSyntax{
            .source_range = range(26, 44),
            .name = identifier("Location", 33, 41),
            .fields = {},
            .annotations = {},
        }));
    declarations.push_back(breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
        .source_range = range(0, 70),
        .name =
            qualified_name({identifier("breadcrumbs", 10, 21), identifier("geo", 22, 25)}, 10, 25),
        .declarations = std::move(nested_declarations),
        .annotations = {},
    }));
    const auto ast = schema_file(std::move(declarations));
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(unit_from(ast), diagnostics);

    const Scope& global = model.global_scope();
    const Symbol* breadcrumbs = find_symbol(global, "breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    const Scope& breadcrumbs_scope = *breadcrumbs->child_scope;
    const Symbol* geo = find_symbol(breadcrumbs_scope, "geo");
    ASSERT_NE(geo, nullptr);
    const Scope& geo_scope = *geo->child_scope;

    EXPECT_EQ(model.resolve_unqualified("Location", geo_scope)->name, "Location");
    EXPECT_EQ(model.resolve_unqualified("geo", geo_scope)->name, "geo");
    EXPECT_EQ(model.resolve_unqualified("breadcrumbs", geo_scope)->name, "breadcrumbs");
}

TEST(SymbolsSmokeTest, ResolvesQualifiedNames) {
    std::vector<DeclarationPtr> declarations;
    std::vector<DeclarationPtr> nested_declarations;
    nested_declarations.push_back(
        breadcrumbs::compiler::ast::make_declaration(RecordDeclarationSyntax{
            .source_range = range(26, 44),
            .name = identifier("Location", 33, 41),
            .fields = {},
            .annotations = {},
        }));
    declarations.push_back(breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
        .source_range = range(0, 80),
        .name =
            qualified_name({identifier("breadcrumbs", 10, 21), identifier("geo", 22, 25)}, 10, 25),
        .declarations = std::move(nested_declarations),
        .annotations = {},
    }));
    const auto ast = schema_file(std::move(declarations));
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(unit_from(ast), diagnostics);

    const Scope& global = model.global_scope();
    const Symbol* result =
        model.resolve(qualified_name({identifier("breadcrumbs", 0, 11), identifier("geo", 12, 15),
                                      identifier("Location", 16, 24)},
                                     0, 24),
                      global);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->kind, SymbolKind::Record);
    EXPECT_EQ(result->name, "Location");
}

TEST(SymbolsSmokeTest, ResolvesQualifiedNamesLexicallyFromEnclosingScopes) {
    std::vector<DeclarationPtr> vehicle_declarations;
    vehicle_declarations.push_back(
        breadcrumbs::compiler::ast::make_declaration(RecordDeclarationSyntax{
            .source_range = range(62, 80),
            .name = identifier("Route", 69, 74),
            .fields = {},
            .annotations = {},
        }));

    std::vector<DeclarationPtr> breadcrumbs_declarations;
    std::vector<DeclarationPtr> geo_declarations;
    geo_declarations.push_back(breadcrumbs::compiler::ast::make_declaration(RecordDeclarationSyntax{
        .source_range = range(40, 58),
        .name = identifier("Location", 47, 55),
        .fields = {},
        .annotations = {},
    }));
    breadcrumbs_declarations.push_back(
        breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
            .source_range = range(26, 64),
            .name = qualified_name({identifier("geo", 36, 39)}, 36, 39),
            .declarations = std::move(geo_declarations),
            .annotations = {},
        }));
    breadcrumbs_declarations.push_back(
        breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
            .source_range = range(65, 120),
            .name = qualified_name({identifier("vehicle", 75, 82)}, 75, 82),
            .declarations = std::move(vehicle_declarations),
            .annotations = {},
        }));

    std::vector<DeclarationPtr> declarations;
    declarations.push_back(breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
        .source_range = range(0, 121),
        .name = qualified_name({identifier("breadcrumbs", 10, 21)}, 10, 21),
        .declarations = std::move(breadcrumbs_declarations),
        .annotations = {},
    }));

    const auto ast = schema_file(std::move(declarations));
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(unit_from(ast), diagnostics);

    ASSERT_TRUE(diagnostics.empty());
    const Scope& global = model.global_scope();
    const Symbol* breadcrumbs = find_symbol(global, "breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    const Scope& breadcrumbs_scope = *breadcrumbs->child_scope;
    const Symbol* vehicle = find_symbol(breadcrumbs_scope, "vehicle");
    ASSERT_NE(vehicle, nullptr);

    const Symbol* result = model.resolve(
        qualified_name({identifier("geo", 0, 3), identifier("Location", 4, 12)}, 0, 12),
        *vehicle->child_scope);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->kind, SymbolKind::Record);
    EXPECT_EQ(result->name, "Location");
}

TEST(SymbolsSmokeTest, QualifiedNamesRespectLexicalShadowing) {
    std::vector<DeclarationPtr> vehicle_declarations;
    vehicle_declarations.push_back(make_record("geo", 62, 77));

    std::vector<DeclarationPtr> breadcrumbs_declarations;
    std::vector<DeclarationPtr> geo_declarations;
    geo_declarations.push_back(breadcrumbs::compiler::ast::make_declaration(RecordDeclarationSyntax{
        .source_range = range(40, 58),
        .name = identifier("Location", 47, 55),
        .fields = {},
        .annotations = {},
    }));
    breadcrumbs_declarations.push_back(
        breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
            .source_range = range(26, 64),
            .name = qualified_name({identifier("geo", 36, 39)}, 36, 39),
            .declarations = std::move(geo_declarations),
            .annotations = {},
        }));
    breadcrumbs_declarations.push_back(
        breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
            .source_range = range(65, 120),
            .name = qualified_name({identifier("vehicle", 75, 82)}, 75, 82),
            .declarations = std::move(vehicle_declarations),
            .annotations = {},
        }));

    std::vector<DeclarationPtr> declarations;
    declarations.push_back(breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
        .source_range = range(0, 121),
        .name = qualified_name({identifier("breadcrumbs", 10, 21)}, 10, 21),
        .declarations = std::move(breadcrumbs_declarations),
        .annotations = {},
    }));

    const auto ast = schema_file(std::move(declarations));
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(unit_from(ast), diagnostics);

    ASSERT_TRUE(diagnostics.empty());
    const Scope& global = model.global_scope();
    const Symbol* breadcrumbs = find_symbol(global, "breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    const Scope& breadcrumbs_scope = *breadcrumbs->child_scope;
    const Symbol* vehicle = find_symbol(breadcrumbs_scope, "vehicle");
    ASSERT_NE(vehicle, nullptr);

    const Symbol* result = model.resolve(
        qualified_name({identifier("geo", 0, 3), identifier("Location", 4, 12)}, 0, 12),
        *vehicle->child_scope);
    EXPECT_EQ(result, nullptr);
}

TEST(SymbolsSmokeTest, DetectsDuplicateDeclarationsInSameScope) {
    std::vector<DeclarationPtr> declarations;
    declarations.push_back(make_record("Location", 0, 18));
    declarations.push_back(make_record("Location", 19, 37));
    const auto ast = schema_file(std::move(declarations));
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(unit_from(ast), diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC4001");
    EXPECT_EQ(diagnostics.diagnostics()[0].compiler_pass(), "symbols");
    EXPECT_EQ(diagnostics.diagnostics()[0].source_range(),
              std::optional<SourceRange>(range(19, 37)));
    ASSERT_EQ(diagnostics.diagnostics()[0].related_locations().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].related_locations()[0].range(),
              std::optional<SourceRange>(range(0, 18)));

    const Scope& global = model.global_scope();
    ASSERT_NE(find_symbol(global, "Location"), nullptr);
}

TEST(SymbolsSmokeTest, DetectsUnresolvedNamesWhenAsked) {
    std::vector<DeclarationPtr> declarations;
    declarations.push_back(make_record("Location", 0, 18));
    const auto ast = schema_file(std::move(declarations));
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(unit_from(ast), diagnostics);

    const Scope& global = model.global_scope();
    const auto name = qualified_name({identifier("Missing", 0, 7)}, 0, 7);
    EXPECT_EQ(model.resolve_or_diagnostic(name, global, diagnostics), nullptr);
    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC4002");
    EXPECT_EQ(diagnostics.diagnostics()[0].source_range(), std::optional<SourceRange>(range(0, 7)));
}

TEST(SymbolsSmokeTest, AllowsSameNameInDifferentNamespaces) {
    std::vector<DeclarationPtr> declarations;

    std::vector<DeclarationPtr> geo_declarations;
    geo_declarations.push_back(breadcrumbs::compiler::ast::make_declaration(RecordDeclarationSyntax{
        .source_range = range(26, 44),
        .name = identifier("Location", 33, 41),
        .fields = {},
        .annotations = {},
    }));
    declarations.push_back(breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
        .source_range = range(0, 80),
        .name =
            qualified_name({identifier("breadcrumbs", 10, 21), identifier("geo", 22, 25)}, 10, 25),
        .declarations = std::move(geo_declarations),
        .annotations = {},
    }));

    std::vector<DeclarationPtr> telemetry_declarations;
    telemetry_declarations.push_back(
        breadcrumbs::compiler::ast::make_declaration(RecordDeclarationSyntax{
            .source_range = range(113, 131),
            .name = identifier("Location", 120, 128),
            .fields = {},
            .annotations = {},
        }));
    declarations.push_back(breadcrumbs::compiler::ast::make_declaration(NamespaceDeclarationSyntax{
        .source_range = range(81, 150),
        .name = qualified_name(
            {identifier("breadcrumbs", 91, 102), identifier("telemetry", 103, 112)}, 91, 112),
        .declarations = std::move(telemetry_declarations),
        .annotations = {},
    }));

    const auto ast = schema_file(std::move(declarations));
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(unit_from(ast), diagnostics);

    ASSERT_TRUE(diagnostics.empty());
    const Scope& global = model.global_scope();
    const Symbol* breadcrumbs = find_symbol(global, "breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    const Scope& breadcrumbs_scope = *breadcrumbs->child_scope;
    const Symbol* geo = find_symbol(breadcrumbs_scope, "geo");
    const Symbol* telemetry = find_symbol(breadcrumbs_scope, "telemetry");
    ASSERT_NE(geo, nullptr);
    ASSERT_NE(telemetry, nullptr);

    EXPECT_EQ(model.resolve_unqualified("Location", *geo->child_scope)->source_range,
              range(26, 44));
    EXPECT_EQ(model.resolve_unqualified("Location", *telemetry->child_scope)->source_range,
              range(113, 131));
}

} // namespace
