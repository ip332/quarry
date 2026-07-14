#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/symbols/symbols.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::source_schema::NormalizedSourceSchemaDocument;
using breadcrumbs::compiler::source_schema::NormalizedSourceSchemaEnum;
using breadcrumbs::compiler::source_schema::SourceSchemaIdentifier;
using breadcrumbs::compiler::source_schema::SourceSchemaQualifiedName;
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
    auto schema = make_normalized_schema("breadcrumbs.geo", "Location");
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
    const Symbol* fix_type = find_symbol(geo_scope, "FixType");
    ASSERT_NE(fix_type, nullptr);
    EXPECT_EQ(fix_type->kind, SymbolKind::Enum);
}

TEST(SymbolsSmokeTest, ResolvesCurrentAndEnclosingScopeNames) {
    const auto schema = make_normalized_schema("breadcrumbs.geo", "Location");
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(schema, diagnostics);

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
    const auto schema = make_normalized_schema("breadcrumbs.geo", "Location");
    DiagnosticEngine diagnostics;
    NamespaceBuilder builder;
    const auto model = builder.build(schema, diagnostics);

    const Scope& global = model.global_scope();
    const Symbol* result = model.resolve(source_qualified_name("breadcrumbs.geo.Location", 0, 24),
                                         global);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->kind, SymbolKind::Record);
    EXPECT_EQ(result->name, "Location");
}

TEST(SymbolsSmokeTest, DetectsDuplicateDeclarationsInSameScope) {
    auto schema = make_normalized_schema("breadcrumbs.geo", "Location");
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

    const Symbol* breadcrumbs = find_symbol(model.global_scope(), "breadcrumbs");
    ASSERT_NE(breadcrumbs, nullptr);
    const Symbol* geo = find_symbol(*breadcrumbs->child_scope, "geo");
    ASSERT_NE(geo, nullptr);
    ASSERT_NE(find_symbol(*geo->child_scope, "Location"), nullptr);
}

TEST(SymbolsSmokeTest, DetectsUnresolvedNamesWhenAsked) {
    const auto schema = make_normalized_schema("breadcrumbs.geo", "Location");
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
