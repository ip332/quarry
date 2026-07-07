#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/support/source_location.hpp"
#include "compiler/support/source_manager.hpp"

#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::context::CompilerContext;
using breadcrumbs::compiler::diagnostics::Diagnostic;
using breadcrumbs::compiler::diagnostics::DiagnosticEngine;
using breadcrumbs::compiler::diagnostics::DiagnosticFormatter;
using breadcrumbs::compiler::diagnostics::DiagnosticId;
using breadcrumbs::compiler::diagnostics::RelatedLocation;
using breadcrumbs::compiler::diagnostics::Severity;
using breadcrumbs::compiler::diagnostics::to_string;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::support::SourceLocation;
using breadcrumbs::compiler::support::SourceManager;
using breadcrumbs::compiler::support::SourceRange;

[[nodiscard]] DiagnosticId id(std::string_view value) {
    const auto parsed = DiagnosticId::parse(value);
    EXPECT_TRUE(parsed.has_value());
    return parsed.value_or(DiagnosticId::invalid());
}

TEST(DiagnosticIdTest, ValidatesFormatsAndComparesIds) {
    const DiagnosticId valid("BC1001");
    const DiagnosticId same("BC1001");
    const DiagnosticId different("BC1002");
    const DiagnosticId invalid("BC10");

    EXPECT_TRUE(valid.is_valid());
    EXPECT_EQ(valid.str(), "BC1001");
    EXPECT_EQ(valid, same);
    EXPECT_NE(valid, different);
    EXPECT_FALSE(invalid.is_valid());
    EXPECT_TRUE(DiagnosticId::parse("BC1001").has_value());
    EXPECT_FALSE(DiagnosticId::parse("BC10").has_value());
    EXPECT_FALSE(DiagnosticId::parse("XX1001").has_value());
    EXPECT_FALSE(DiagnosticId::parse("BC10A1").has_value());
    EXPECT_FALSE(DiagnosticId::invalid().is_valid());
}

TEST(SeverityTest, FormatsStableStrings) {
    EXPECT_EQ(to_string(Severity::Error), "error");
    EXPECT_EQ(to_string(Severity::Warning), "warning");
    EXPECT_EQ(to_string(Severity::Note), "note");
    EXPECT_EQ(to_string(Severity::InternalCompilerError), "internal compiler error");
}

TEST(DiagnosticTest, BuildsDiagnosticWithOptionalContext) {
    const SourceFileId file_id(0);
    const SourceLocation primary(file_id, 4);
    const SourceRange range(SourceLocation(file_id, 4), SourceLocation(file_id, 8));
    const RelatedLocation related =
        RelatedLocation::at_location(SourceLocation(file_id, 1), "previous definition is here");

    const Diagnostic diagnostic =
        Diagnostic::create(id("BC1001"), Severity::Error, "duplicate name")
            .at(range)
            .with_related(related)
            .with_note("names must be unique")
            .with_suggested_fix("rename this declaration")
            .from_pass("namespace")
            .build();

    ASSERT_TRUE(diagnostic.primary_location().has_value());
    ASSERT_TRUE(diagnostic.source_range().has_value());
    ASSERT_TRUE(diagnostic.suggested_fix().has_value());
    EXPECT_EQ(diagnostic.id().str(), "BC1001");
    EXPECT_EQ(diagnostic.severity(), Severity::Error);
    EXPECT_EQ(diagnostic.message(), "duplicate name");
    EXPECT_EQ(*diagnostic.primary_location(), primary);
    EXPECT_EQ(*diagnostic.source_range(), range);
    ASSERT_EQ(diagnostic.related_locations().size(), 1U);
    EXPECT_EQ(diagnostic.related_locations()[0].message(), "previous definition is here");
    ASSERT_EQ(diagnostic.notes().size(), 1U);
    EXPECT_EQ(diagnostic.notes()[0], "names must be unique");
    EXPECT_EQ(*diagnostic.suggested_fix(), "rename this declaration");
    EXPECT_EQ(diagnostic.compiler_pass(), "namespace");

    const Diagnostic location_only =
        Diagnostic::create(id("BC1002"), Severity::Warning, "location only").at(primary).build();
    EXPECT_TRUE(location_only.primary_location().has_value());
    EXPECT_FALSE(location_only.source_range().has_value());
}

TEST(DiagnosticEngineTest, CollectsCountsAndClearsDiagnostics) {
    DiagnosticEngine engine;

    engine.emit(Diagnostic::create(id("BC1001"), Severity::Error, "error").build());
    engine.emit(Diagnostic::create(id("BC1002"), Severity::Warning, "warning").build());
    engine.emit(Diagnostic::create(id("BC1003"), Severity::Note, "note").build());
    engine.emit(Diagnostic::create(id("BC1004"), Severity::InternalCompilerError, "ice").build());

    const std::vector<Diagnostic>& diagnostics = engine.diagnostics();
    EXPECT_EQ(diagnostics.size(), 4U);
    EXPECT_EQ(engine.all().size(), 4U);
    EXPECT_EQ(engine.error_count(), 1U);
    EXPECT_EQ(engine.warning_count(), 1U);
    EXPECT_EQ(engine.internal_error_count(), 1U);
    EXPECT_TRUE(engine.has_fatal_diagnostics());
    EXPECT_TRUE(engine.has_errors());

    engine.clear();
    EXPECT_TRUE(engine.empty());
    EXPECT_EQ(engine.error_count(), 0U);
}

TEST(DiagnosticEngineTest, SortsDeterministicallyWithoutUsingMessageText) {
    SourceManager source_manager;
    const SourceFileId beta = source_manager.add_source("/project/beta.bc", "abcdef");
    const SourceFileId alpha = source_manager.add_source("/project/alpha.bc", "abcdef");

    DiagnosticEngine engine;
    engine.emit(Diagnostic::create(id("BC2002"), Severity::Error, "unlocated")
                    .from_pass("semantic")
                    .build());
    engine.emit(Diagnostic::create(id("BC1003"), Severity::Error, "later")
                    .at(SourceLocation(alpha, 4))
                    .from_pass("parser")
                    .build());
    engine.emit(Diagnostic::create(id("BC1001"), Severity::Error, "first file")
                    .at(SourceLocation(alpha, 1))
                    .from_pass("parser")
                    .build());
    engine.emit(Diagnostic::create(id("BC1002"), Severity::Error, "second file")
                    .at(SourceLocation(beta, 0))
                    .from_pass("parser")
                    .build());
    engine.emit(Diagnostic::create(id("BC1000"), Severity::Error, "same location")
                    .at(SourceLocation(alpha, 1))
                    .from_pass("imports")
                    .build());
    engine.emit(Diagnostic::create(id("BC3000"), Severity::Error, "z message")
                    .at(SourceLocation(alpha, 2))
                    .from_pass("parser")
                    .build());
    engine.emit(Diagnostic::create(id("BC3000"), Severity::Error, "a message")
                    .at(SourceLocation(alpha, 2))
                    .from_pass("parser")
                    .build());

    const std::vector<const Diagnostic*> sorted = engine.sorted_diagnostics(source_manager);
    ASSERT_EQ(sorted.size(), 7U);
    EXPECT_EQ(sorted[0]->id().str(), "BC1000");
    EXPECT_EQ(sorted[1]->id().str(), "BC1001");
    EXPECT_EQ(sorted[2]->message(), "z message");
    EXPECT_EQ(sorted[3]->message(), "a message");
    EXPECT_EQ(sorted[4]->id().str(), "BC1003");
    EXPECT_EQ(sorted[5]->id().str(), "BC1002");
    EXPECT_EQ(sorted[6]->id().str(), "BC2002");
}

TEST(DiagnosticFormatterTest, FormatsHumanReadableDiagnostics) {
    SourceManager source_manager;
    const SourceFileId file_id = source_manager.add_source("/project/main.bc", "abcd\nefgh");

    const Diagnostic one_line = Diagnostic::create(id("BC1001"), Severity::Error, "basic error")
                                    .from_pass("parser")
                                    .build();
    EXPECT_EQ(DiagnosticFormatter::format(one_line, source_manager),
              "<unknown>: error BC1001: basic error [parser]");

    const Diagnostic with_location = Diagnostic::create(id("BC1002"), Severity::Warning, "located")
                                         .at(SourceLocation(file_id, 5))
                                         .from_pass("semantic")
                                         .build();
    EXPECT_EQ(DiagnosticFormatter::format(with_location, source_manager),
              "/project/main.bc:2:1: warning BC1002: located [semantic]");

    const SourceRange range(SourceLocation(file_id, 0), SourceLocation(file_id, 4));
    const Diagnostic with_range = Diagnostic::create(id("BC1003"), Severity::Error, "range")
                                      .at(range)
                                      .with_related(RelatedLocation::at_location(
                                          SourceLocation(file_id, 5), "related location"))
                                      .with_note("additional context")
                                      .with_suggested_fix("replace with valid schema")
                                      .from_pass("parser")
                                      .build();
    EXPECT_EQ(DiagnosticFormatter::format(with_range, source_manager),
              "/project/main.bc:1:1-1:5: error BC1003: range [parser]\n"
              "  related: /project/main.bc:2:1: related location\n"
              "  note: additional context\n"
              "  fix: replace with valid schema");

    const Diagnostic invalid_id =
        Diagnostic::create(DiagnosticId::invalid(), Severity::Note, "unknown id").build();
    EXPECT_EQ(DiagnosticFormatter::format(invalid_id, source_manager),
              "<unknown>: note invalid: unknown id");
}

TEST(DiagnosticFormatterTest, FormatsAllDiagnosticsInSortedOrder) {
    SourceManager source_manager;
    const SourceFileId file_id = source_manager.add_source("/project/main.bc", "abcdef");

    DiagnosticEngine engine;
    engine.emit(Diagnostic::create(id("BC1002"), Severity::Error, "later")
                    .at(SourceLocation(file_id, 3))
                    .build());
    engine.emit(Diagnostic::create(id("BC1001"), Severity::Error, "earlier")
                    .at(SourceLocation(file_id, 1))
                    .build());

    EXPECT_EQ(DiagnosticFormatter::format_all(engine, source_manager),
              "/project/main.bc:1:2: error BC1001: earlier\n"
              "/project/main.bc:1:4: error BC1002: later");
}

TEST(CompilerContextTest, ProvidesAccessToDiagnosticEngine) {
    CompilerContext context;
    EXPECT_TRUE(context.diagnostic_engine().empty());

    context.diagnostic_engine().emit(
        Diagnostic::create(id("BC1001"), Severity::Error, "context diagnostic").build());
    EXPECT_EQ(context.diagnostic_engine().error_count(), 1U);
    EXPECT_TRUE(context.diagnostic_engine().has_fatal_diagnostics());
}

} // namespace
