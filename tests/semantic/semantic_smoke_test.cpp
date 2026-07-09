#include "compiler/ast/ast.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/symbols/symbols.hpp"

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
using breadcrumbs::compiler::semantic::SemanticModel;
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

[[nodiscard]] AnalysisOutput analyze(std::string text) {
    AnalysisOutput output;
    output.source_file_id = output.source_manager.add_source("/test/schema.brd", std::move(text));

    auto parse_result =
        Parser::parse(output.source_manager, output.source_file_id, output.parser_diagnostics);
    output.ast = std::move(parse_result.ast);

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

TEST(SemanticSmokeTest, AcceptsBuiltinFieldTypes) {
    const AnalysisOutput output = analyze(R"(record Example {
  active: bool
  count: int32
  total: uint64
  ratio: float64
  label: string
}
)");

    ASSERT_TRUE(expect_clean_pipeline(output));
    EXPECT_TRUE(output.semantic_diagnostics.empty())
        << diagnostics_summary(output.semantic_diagnostics);
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
}

} // namespace
