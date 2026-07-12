#include "compiler/backend/backend.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/layout/layout.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"
#include "compiler/semantic/semantic.hpp"
#include "compiler/support/source_manager.hpp"
#include "compiler/symbols/symbols.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace {

using breadcrumbs::compiler::backend::Backend;
using breadcrumbs::compiler::backend::CodegenOptions;
using breadcrumbs::compiler::backend::CodegenResult;
using breadcrumbs::compiler::layout::LayoutModel;
using breadcrumbs::compiler::parser::Parser;
using breadcrumbs::compiler::schema_ir::SchemaIrBuilder;
using breadcrumbs::compiler::schema_ir::SchemaIrModel;
using breadcrumbs::compiler::schema_ir::SchemaIrValidator;
using breadcrumbs::compiler::semantic::SemanticModel;
using breadcrumbs::compiler::semantic::SemanticValidator;
using breadcrumbs::compiler::support::SourceFileId;
using breadcrumbs::compiler::symbols::NamespaceBuilder;
using breadcrumbs::compiler::symbols::SymbolTable;

struct FrontendOutput {
    breadcrumbs::compiler::context::CompilerContext context;
    breadcrumbs::compiler::ast::SchemaFileSyntax ast;
    breadcrumbs::compiler::diagnostics::DiagnosticEngine parser_diagnostics;
    breadcrumbs::compiler::diagnostics::DiagnosticEngine symbol_diagnostics;
    breadcrumbs::compiler::diagnostics::DiagnosticEngine semantic_diagnostics;
    breadcrumbs::compiler::diagnostics::DiagnosticEngine lowering_diagnostics;
    breadcrumbs::compiler::diagnostics::DiagnosticEngine validation_diagnostics;
    std::unique_ptr<SymbolTable> symbol_table;
    SemanticModel semantic_model;
    LayoutModel layout_model;
    SchemaIrModel schema_ir;
    SourceFileId source_file_id;
};

[[nodiscard]] std::filesystem::path fixtures_root(std::string_view category) {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" / category;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

void trim_trailing_newlines(std::string& text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
}

[[nodiscard]] FrontendOutput run_frontend(const std::string& text) {
    FrontendOutput output;
    output.source_file_id = output.context.source_manager().add_source("/test/schema.brd", text);

    auto parse_result = Parser::parse(output.context.source_manager(), output.source_file_id,
                                      output.parser_diagnostics);
    output.ast = std::move(parse_result.ast);

    NamespaceBuilder namespace_builder;
    output.symbol_table = std::make_unique<SymbolTable>(
        namespace_builder.build(output.ast, output.symbol_diagnostics));

    SemanticValidator semantic_validator;
    output.semantic_model =
        semantic_validator.validate(output.ast, *output.symbol_table, output.semantic_diagnostics);

    SchemaIrBuilder schema_ir_builder;
    output.schema_ir =
        schema_ir_builder.build(output.ast, output.semantic_model, output.layout_model,
                                *output.symbol_table, output.context, output.lowering_diagnostics);

    SchemaIrValidator schema_ir_validator;
    schema_ir_validator.validate(output.schema_ir, output.context, output.validation_diagnostics);
    return output;
}

[[nodiscard]] CodegenResult run_backend(const std::string& source, const CodegenOptions& options) {
    FrontendOutput frontend = run_frontend(source);

    Backend backend;
    CodegenResult result = backend.generate(frontend.schema_ir, options);
    EXPECT_TRUE(frontend.parser_diagnostics.empty());
    EXPECT_TRUE(frontend.symbol_diagnostics.empty());
    EXPECT_TRUE(frontend.semantic_diagnostics.empty());
    EXPECT_TRUE(frontend.lowering_diagnostics.empty());
    EXPECT_TRUE(frontend.validation_diagnostics.empty());
    return result;
}

[[nodiscard]] std::string fixture_text(std::string_view category, std::string_view name,
                                       std::string_view extension) {
    std::string text =
        read_file(fixtures_root(category) / (std::string(name) + std::string(extension)));
    trim_trailing_newlines(text);
    return text;
}

[[nodiscard]] std::string schema_fixture_text(std::string_view name) {
    return fixture_text("schema_ir", name, ".brd");
}

[[nodiscard]] std::string backend_fixture_text(std::string_view name) {
    return fixture_text("backend", name, ".brd");
}

[[nodiscard]] std::string backend_golden_text(std::string_view name) {
    return fixture_text("backend", name, ".txt");
}

[[nodiscard]] std::string render_result(const CodegenResult& result) {
    std::ostringstream stream;
    for (const auto& file : result.files) {
        stream << "=== " << file.path << " ===\n";
        stream << file.content;
        if (!file.content.empty() && file.content.back() != '\n') {
            stream << '\n';
        }
    }
    std::string rendered = stream.str();
    trim_trailing_newlines(rendered);
    return rendered;
}

TEST(BackendCodegenTest, EmptySchemaGeneratesNoFiles) {
    const CodegenResult result = run_backend("", CodegenOptions{});
    EXPECT_TRUE(result.success) << result.error_message;
    EXPECT_TRUE(result.files.empty());
}

TEST(BackendCodegenTest, BuiltinScalarFieldsMatchGolden) {
    const std::string source = backend_fixture_text("builtin_scalar_fields");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("builtin_scalar_fields"));
}

TEST(BackendCodegenTest, EnumMatchesGolden) {
    const std::string source = schema_fixture_text("enum");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("enum"));
}

TEST(BackendCodegenTest, NamedTypeReferenceMatchesGolden) {
    const std::string source = schema_fixture_text("named_type_reference");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/breadcrumbs/geo.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("named_type_reference"));
}

TEST(BackendCodegenTest, SameFileForwardReferenceOrdersDefinitions) {
    const std::string source = backend_fixture_text("forward_record_reference");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("forward_record_reference"));
}

TEST(BackendCodegenTest, MultipleTopLevelNamespacesMatchGolden) {
    const std::string source = schema_fixture_text("multiple_top_level_namespaces");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2u);
    EXPECT_EQ(result.files[0].path, "generated/alpha/one.generated.hpp");
    EXPECT_EQ(result.files[1].path, "generated/beta/two.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("multiple_top_level_namespaces"));
}

TEST(BackendCodegenTest, CrossNamespaceReferenceMatchesGolden) {
    const std::string source = backend_fixture_text("cross_namespace_reference");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2u);
    EXPECT_EQ(result.files[0].path, "generated/alpha/one.generated.hpp");
    EXPECT_EQ(result.files[1].path, "generated/beta/two.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("cross_namespace_reference"));
}

TEST(BackendCodegenTest, CyclicNamespaceDependencyFailsClearly) {
    const std::string source = backend_fixture_text("cyclic_namespace_reference");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.files.empty());
    EXPECT_NE(result.error_message.find("cycle"), std::string::npos);
}

TEST(BackendCodegenTest, UnsupportedFieldFailsAtomically) {
    const std::string source = backend_fixture_text("valid_then_unsupported");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.files.empty());
    EXPECT_NE(result.error_message.find("string"), std::string::npos);
}

TEST(BackendCodegenTest, EnumReferenceMatchesGolden) {
    const std::string source = backend_fixture_text("enum_reference");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("enum_reference"));
}

} // namespace
