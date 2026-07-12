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

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    static std::uint64_t counter = 0;
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                            (std::string("breadcrumbs-backend-codegen-") +
                                             std::string(stem) + "-" + std::to_string(counter++));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("failed to open file for writing: " + path.string());
    }
    output << text;
}

void write_generated_files(const CodegenResult& result, const std::filesystem::path& root) {
    for (const auto& file : result.files) {
        write_text_file(root / file.path, file.content);
    }
}

[[nodiscard]] std::string generated_include_path(std::string_view output_directory,
                                                 std::string_view generated_path) {
    const std::string prefix = std::string(output_directory) + "/";
    if (generated_path.rfind(prefix, 0) == 0) {
        return std::string(generated_path.substr(prefix.size()));
    }
    return std::string(generated_path);
}

[[nodiscard]] int run_command(const std::string& command) {
    const int status = std::system(command.c_str());
    return status;
}

void compile_generated_header(const CodegenResult& result, std::string_view translation_unit) {
    ASSERT_FALSE(result.files.empty());

    const std::filesystem::path root = make_temp_directory("compile");
    const std::filesystem::path generated_root = root / CodegenOptions{}.output_directory;
    write_generated_files(result, root);

    const std::filesystem::path source_path = root / "compile.cpp";
    write_text_file(source_path, translation_unit);

    const std::filesystem::path object_path = root / "compile.o";
    const std::string compiler = BREADCRUMBS_TEST_CXX_COMPILER;
    std::ostringstream command;
    command << std::quoted(compiler) << " -std=c++20 -I" << std::quoted(generated_root.string())
            << " -c " << std::quoted(source_path.string()) << " -o "
            << std::quoted(object_path.string());

    const int status = run_command(command.str());
    ASSERT_EQ(status, 0) << "command failed: " << command.str();
}

[[nodiscard]] SchemaIrModel make_manual_negative_enum_schema_ir() {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    auto* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    root->set_name("");
    root->set_fqn("");

    auto* enum_ir = root->add_enums();
    enum_ir->set_ir_id(2);
    enum_ir->set_name("SignedValue");
    enum_ir->set_fqn("SignedValue");
    auto* first = enum_ir->add_values();
    first->set_name("Zero");
    first->set_value(0);
    auto* second = enum_ir->add_values();
    second->set_name("Negative");
    second->set_value(-1);
    return schema_ir;
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

TEST(BackendCodegenTest, NegativeEnumValuesArePreserved) {
    Backend backend;
    const CodegenResult result =
        backend.generate(make_manual_negative_enum_schema_ir(), CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_NE(render_result(result).find("enum class SignedValue : std::int64_t"),
              std::string::npos);
    EXPECT_NE(render_result(result).find("Zero = 0"), std::string::npos);
    EXPECT_NE(render_result(result).find("Negative = -1"), std::string::npos);
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

TEST(BackendCodegenTest, CrossNamespaceEnumReferenceMatchesGolden) {
    const std::string source = backend_fixture_text("cross_namespace_enum_reference");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2u);
    EXPECT_EQ(result.files[0].path, "generated/alpha/one.generated.hpp");
    EXPECT_EQ(result.files[1].path, "generated/beta/two.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("cross_namespace_enum_reference"));
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

TEST(BackendCodegenTest, EnumAndRecordSameNamespaceMatchGolden) {
    const std::string source = backend_fixture_text("mixed_same_namespace_dependencies");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("mixed_same_namespace_dependencies"));
}

TEST(BackendCodegenTest, MultipleEnumsHaveStableOrder) {
    const std::string source = backend_fixture_text("multiple_enums");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(render_result(result), backend_golden_text("multiple_enums"));

    const CodegenResult second_result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(second_result.success) << second_result.error_message;
    EXPECT_EQ(render_result(result), render_result(second_result));
}

TEST(BackendCodegenTest, GeneratedHeadersCompile) {
    const std::string source = backend_fixture_text("mixed_same_namespace_dependencies");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_FALSE(result.files.empty());

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstdint>\n"
        "#include <type_traits>\n"
        "static_assert(std::is_enum_v<::Status>);\n"
        "static_assert(std::is_same_v<std::underlying_type_t<::Status>, std::int64_t>);\n"
        "int main() {\n"
        "  ::Wrapper value{};\n"
        "  value.state = ::Status::Ready;\n"
        "  value.child.count = 1u;\n"
        "  return value.state == ::Status::Ready ? 0 : 1;\n"
        "}\n";

    compile_generated_header(result, translation_source);
}

} // namespace
