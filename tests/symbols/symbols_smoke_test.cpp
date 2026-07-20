#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/symbols/symbols.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace {

using quarry::compiler::diagnostics::DiagnosticEngine;
using quarry::compiler::source_schema::NormalizedSourceSchemaDocument;
using quarry::compiler::source_schema::NormalizedSourceSchemaEnum;
using quarry::compiler::source_schema::SourceSchemaIdentifier;
using quarry::compiler::source_schema::SourceSchemaQualifiedName;
using quarry::compiler::support::SourceFileId;
using quarry::compiler::support::SourceLocation;
using quarry::compiler::support::SourceRange;
using quarry::compiler::symbols::NamespaceBuilder;
using quarry::compiler::symbols::Scope;
using quarry::compiler::symbols::Symbol;
using quarry::compiler::symbols::SymbolKind;

[[nodiscard]] SourceRange range(std::size_t begin, std::size_t end) {
    const SourceFileId file_id(0);
    return SourceRange(SourceLocation(file_id, begin), SourceLocation(file_id, end));
}

[[nodiscard]] const Symbol* find_symbol(const Scope& scope, std::string_view name) {
    return scope.find_local(name);
}

[[nodiscard]] SourceSchemaIdentifier source_identifier(std::string text, std::size_t begin,
                                                      std::size_t end) {
    return SourceSchemaIdentifier{
        .text = std::move(text),
        .source_range = range(begin, end),
    };
}

[[nodiscard]] SourceSchemaQualifiedName source_qualified_name(std::string_view text,
                                                              std::size_t begin,
                                                              std::size_t end) {
    SourceSchemaQualifiedName name;
    name.source_range = range(begin, end);

    std::size_t part_begin = 0;
    while (part_begin <= text.size()) {
        const std::size_t part_end = text.find('.', part_begin);
        const std::string_view part = part_end == std::string_view::npos
                                          ? text.substr(part_begin)
                                          : text.substr(part_begin, part_end - part_begin);
        name.parts.push_back(source_identifier(std::string(part), begin + part_begin,
                                               begin + part_begin + part.size()));
        if (part_end == std::string_view::npos) {
            break;
        }
        part_begin = part_end + 1;
    }

    return name;
}

[[nodiscard]] NormalizedSourceSchemaDocument make_normalized_schema(std::string_view namespace_name,
                                                                    std::string_view record_name) {
    NormalizedSourceSchemaDocument schema;
    schema.source_range = range(0, 128);
    schema.namespace_name = source_qualified_name(namespace_name, 0, namespace_name.size());
    schema.record_name = source_identifier(std::string(record_name), 0, record_name.size());
    schema.record_source_range = range(32, 32 + record_name.size());
    return schema;
}

TEST(SymbolsSmokeTest, BuildsNestedNamespaceScopes) {
    auto schema = make_normalized_schema("quarry.geo", "Location");
    schema.enums.push_back(NormalizedSourceSchemaEnum{
        .name = source_identifier("FixType", 64, 71),
        .source_range = range(64, 71),
        .values = {},
        .annotations = {},
    });

    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(schema, diagnostics);

    ASSERT_TRUE(diagnostics.empty());
    const Scope& global = model.global_scope();
    const Symbol* quarry = find_symbol(global, "quarry");
    ASSERT_NE(quarry, nullptr);
    ASSERT_EQ(quarry->kind, SymbolKind::Namespace);
    ASSERT_NE(quarry->child_scope, nullptr);

    const Scope& quarry_scope = *quarry->child_scope;
    const Symbol* geo = find_symbol(quarry_scope, "geo");
    ASSERT_NE(geo, nullptr);
    ASSERT_NE(geo->child_scope, nullptr);

    const Scope& geo_scope = *geo->child_scope;
    const Symbol* location = find_symbol(geo_scope, "Location");
    ASSERT_NE(location, nullptr);
    EXPECT_EQ(location->kind, SymbolKind::Record);
    const Symbol* fix_type = find_symbol(geo_scope, "FixType");
    ASSERT_NE(fix_type, nullptr);
    EXPECT_EQ(fix_type->kind, SymbolKind::Enum);
}

TEST(SymbolsSmokeTest, ResolvesCurrentAndEnclosingScopeNames) {
    const auto schema = make_normalized_schema("quarry.geo", "Location");
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(schema, diagnostics);

    const Scope& global = model.global_scope();
    const Symbol* quarry = find_symbol(global, "quarry");
    ASSERT_NE(quarry, nullptr);
    const Scope& quarry_scope = *quarry->child_scope;
    const Symbol* geo = find_symbol(quarry_scope, "geo");
    ASSERT_NE(geo, nullptr);
    const Scope& geo_scope = *geo->child_scope;

    EXPECT_EQ(model.resolve_unqualified("Location", geo_scope)->name, "Location");
    EXPECT_EQ(model.resolve_unqualified("geo", geo_scope)->name, "geo");
    EXPECT_EQ(model.resolve_unqualified("quarry", geo_scope)->name, "quarry");
}

TEST(SymbolsSmokeTest, ResolvesQualifiedNames) {
    const auto schema = make_normalized_schema("quarry.geo", "Location");
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(schema, diagnostics);

    const Scope& global = model.global_scope();
    const Symbol* result = model.resolve(source_qualified_name("quarry.geo.Location", 0, 24),
                                         global);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->kind, SymbolKind::Record);
    EXPECT_EQ(result->name, "Location");
}

TEST(SymbolsSmokeTest, DetectsDuplicateDeclarationsInSameScope) {
    auto schema = make_normalized_schema("quarry.geo", "Location");
    schema.enums.push_back(NormalizedSourceSchemaEnum{
        .name = source_identifier("Location", 64, 72),
        .source_range = range(64, 72),
        .values = {},
        .annotations = {},
    });

    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(schema, diagnostics);

    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC4001");
    EXPECT_EQ(diagnostics.diagnostics()[0].compiler_pass(), "symbols");
    EXPECT_EQ(diagnostics.diagnostics()[0].source_range(),
              std::optional<SourceRange>(range(32, 40)));
    ASSERT_EQ(diagnostics.diagnostics()[0].related_locations().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].related_locations()[0].range(),
              std::optional<SourceRange>(range(64, 72)));

    const Symbol* quarry = find_symbol(model.global_scope(), "quarry");
    ASSERT_NE(quarry, nullptr);
    const Symbol* geo = find_symbol(*quarry->child_scope, "geo");
    ASSERT_NE(geo, nullptr);
    ASSERT_NE(find_symbol(*geo->child_scope, "Location"), nullptr);
}

TEST(SymbolsSmokeTest, DetectsUnresolvedNamesWhenAsked) {
    const auto schema = make_normalized_schema("quarry.geo", "Location");
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(schema, diagnostics);

    const Scope& global = model.global_scope();
    const auto name = source_qualified_name("Missing", 0, 7);
    EXPECT_EQ(model.resolve_or_diagnostic(name, global, diagnostics), nullptr);
    ASSERT_EQ(diagnostics.diagnostics().size(), 1U);
    EXPECT_EQ(diagnostics.diagnostics()[0].id().str(), "BC4002");
    EXPECT_EQ(diagnostics.diagnostics()[0].source_range(), std::optional<SourceRange>(range(0, 7)));
}

} // namespace
