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

#include <algorithm>
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

void compile_generated_header(const CodegenResult& result, std::string_view generated_path,
                              std::string_view translation_unit) {
    ASSERT_FALSE(result.files.empty());
    const auto generated_file =
        std::find_if(result.files.begin(), result.files.end(),
                     [&](const auto& file) { return file.path == generated_path; });
    ASSERT_NE(generated_file, result.files.end()) << generated_path;

    const std::filesystem::path root = make_temp_directory("compile");
    const std::filesystem::path generated_root = root / CodegenOptions{}.output_directory;
    write_generated_files(result, root);

    const std::filesystem::path source_path = root / "compile.cpp";
    write_text_file(source_path, translation_unit);

    const std::filesystem::path executable_path = root / "compile";
    const std::string compiler = BREADCRUMBS_TEST_CXX_COMPILER;
    std::ostringstream command;
    command << std::quoted(compiler) << " -std=c++20 -I" << std::quoted(generated_root.string())
            << " " << std::quoted(source_path.string()) << " -o "
            << std::quoted(executable_path.string());

    const int status = run_command(command.str());
    ASSERT_EQ(status, 0) << "command failed: " << command.str();

    std::ostringstream run_command_stream;
    run_command_stream << std::quoted(executable_path.string());
    const int run_status = run_command(run_command_stream.str());
    ASSERT_EQ(run_status, 0) << "command failed: " << executable_path.string();
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

[[nodiscard]] SchemaIrModel make_manual_unspecified_primitive_schema_ir() {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    auto* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    root->set_name("");
    root->set_fqn("");

    auto* record = root->add_records();
    record->set_ir_id(2);
    record->set_name("Broken");
    record->set_fqn("Broken");
    auto* field = record->add_fields();
    field->set_name("missing");
    field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_UNSPECIFIED);
    return schema_ir;
}

[[nodiscard]] SchemaIrModel make_manual_variable_length_schema_ir() {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    auto* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    root->set_name("");
    root->set_fqn("");

    auto* child = root->add_records();
    child->set_ir_id(2);
    child->set_name("Child");
    child->set_fqn("Child");
    auto* child_field = child->add_fields();
    child_field->set_name("count");
    child_field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    auto* mode = root->add_enums();
    mode->set_ir_id(3);
    mode->set_name("Mode");
    mode->set_fqn("Mode");
    auto* off = mode->add_values();
    off->set_name("Off");
    off->set_value(0);
    auto* on = mode->add_values();
    on->set_name("On");
    on->set_value(1);

    auto* example = root->add_records();
    example->set_ir_id(4);
    example->set_name("Example");
    example->set_fqn("Example");

    auto* name_field = example->add_fields();
    name_field->set_name("name");
    name_field->mutable_type()->mutable_string();

    auto* payload_field = example->add_fields();
    payload_field->set_name("payload");
    payload_field->mutable_type()->mutable_bytes();

    auto* counts_field = example->add_fields();
    counts_field->set_name("counts");
    counts_field->mutable_type()->mutable_array()->set_count(4);
    counts_field->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    auto* distances_field = example->add_fields();
    distances_field->set_name("distances");
    distances_field->mutable_type()->mutable_array()->set_count(2);
    distances_field->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_F64);

    auto* modes_field = example->add_fields();
    modes_field->set_name("modes");
    modes_field->mutable_type()->mutable_array()->set_count(2);
    modes_field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_enum_type()
        ->set_target_enum_ir_id(3);

    auto* children_field = example->add_fields();
    children_field->set_name("children");
    children_field->mutable_type()->mutable_array()->set_count(3);
    children_field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(2);

    return schema_ir;
}

[[nodiscard]] SchemaIrModel make_manual_cross_namespace_array_schema_ir() {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    auto* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    root->set_name("");
    root->set_fqn("");

    auto* alpha = root->add_namespaces();
    alpha->set_ir_id(2);
    alpha->set_name("alpha");
    alpha->set_fqn("alpha");

    auto* alpha_one = alpha->add_namespaces();
    alpha_one->set_ir_id(3);
    alpha_one->set_name("one");
    alpha_one->set_fqn("alpha.one");

    auto* element = alpha_one->add_records();
    element->set_ir_id(4);
    element->set_name("Element");
    element->set_fqn("alpha.one.Element");
    auto* element_field = element->add_fields();
    element_field->set_name("id");
    element_field->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    auto* mode = alpha_one->add_enums();
    mode->set_ir_id(5);
    mode->set_name("Mode");
    mode->set_fqn("alpha.one.Mode");
    auto* off = mode->add_values();
    off->set_name("Off");
    off->set_value(0);
    auto* on = mode->add_values();
    on->set_name("On");
    on->set_value(1);

    auto* beta = root->add_namespaces();
    beta->set_ir_id(6);
    beta->set_name("beta");
    beta->set_fqn("beta");

    auto* beta_two = beta->add_namespaces();
    beta_two->set_ir_id(7);
    beta_two->set_name("two");
    beta_two->set_fqn("beta.two");

    auto* basket = beta_two->add_records();
    basket->set_ir_id(8);
    basket->set_name("Basket");
    basket->set_fqn("beta.two.Basket");

    auto* elements_field = basket->add_fields();
    elements_field->set_name("elements");
    elements_field->mutable_type()->mutable_array()->set_count(2);
    elements_field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(4);

    auto* modes_field = basket->add_fields();
    modes_field->set_name("modes");
    modes_field->mutable_type()->mutable_array()->set_count(2);
    modes_field->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_enum_type()
        ->set_target_enum_ir_id(5);

    return schema_ir;
}

[[nodiscard]] SchemaIrModel make_manual_bounded_builder_schema_ir() {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    auto* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    root->set_name("");
    root->set_fqn("");

    auto* alpha = root->add_namespaces();
    alpha->set_ir_id(2);
    alpha->set_name("alpha");
    alpha->set_fqn("alpha");

    auto* alpha_one = alpha->add_namespaces();
    alpha_one->set_ir_id(3);
    alpha_one->set_name("one");
    alpha_one->set_fqn("alpha.one");

    auto* child = alpha_one->add_records();
    child->set_ir_id(4);
    child->set_name("Child");
    child->set_fqn("alpha.one.Child");
    auto* child_count = child->add_fields();
    child_count->set_name("count");
    child_count->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    auto* mode = alpha_one->add_enums();
    mode->set_ir_id(5);
    mode->set_name("Mode");
    mode->set_fqn("alpha.one.Mode");
    auto* off = mode->add_values();
    off->set_name("Off");
    off->set_value(0);
    auto* on = mode->add_values();
    on->set_name("On");
    on->set_value(1);

    auto* wrapper = alpha_one->add_records();
    wrapper->set_ir_id(6);
    wrapper->set_name("Wrapper");
    wrapper->set_fqn("alpha.one.Wrapper");

    auto* wrapper_child = wrapper->add_fields();
    wrapper_child->set_name("child");
    wrapper_child->mutable_type()->mutable_record()->set_target_record_ir_id(4);

    auto* wrapper_mode = wrapper->add_fields();
    wrapper_mode->set_name("mode");
    wrapper_mode->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(5);

    auto* wrapper_children = wrapper->add_fields();
    wrapper_children->set_name("children");
    wrapper_children->mutable_type()->mutable_array()->set_count(2);
    wrapper_children->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(4);

    auto* wrapper_modes = wrapper->add_fields();
    wrapper_modes->set_name("modes");
    wrapper_modes->mutable_type()->mutable_array()->set_count(2);
    wrapper_modes->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_enum_type()
        ->set_target_enum_ir_id(5);

    auto* beta = root->add_namespaces();
    beta->set_ir_id(7);
    beta->set_name("beta");
    beta->set_fqn("beta");

    auto* beta_two = beta->add_namespaces();
    beta_two->set_ir_id(8);
    beta_two->set_name("two");
    beta_two->set_fqn("beta.two");

    auto* envelope = beta_two->add_records();
    envelope->set_ir_id(9);
    envelope->set_name("Envelope");
    envelope->set_fqn("beta.two.Envelope");

    auto* active = envelope->add_fields();
    active->set_name("active");
    active->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL);

    auto* count = envelope->add_fields();
    count->set_name("count");
    count->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    auto* ratio = envelope->add_fields();
    ratio->set_name("ratio");
    ratio->mutable_type()->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_F64);

    auto* label = envelope->add_fields();
    label->set_name("label");
    label->mutable_type()->mutable_string()->set_max_bytes(5);

    auto* payload = envelope->add_fields();
    payload->set_name("payload");
    payload->mutable_type()->mutable_bytes()->set_max_bytes(4);

    auto* child_ref = envelope->add_fields();
    child_ref->set_name("child");
    child_ref->mutable_type()->mutable_record()->set_target_record_ir_id(4);

    auto* mode_ref = envelope->add_fields();
    mode_ref->set_name("mode");
    mode_ref->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(5);

    auto* children_ref = envelope->add_fields();
    children_ref->set_name("children");
    children_ref->mutable_type()->mutable_array()->set_count(2);
    children_ref->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_record()
        ->set_target_record_ir_id(4);

    auto* modes_ref = envelope->add_fields();
    modes_ref->set_name("modes");
    modes_ref->mutable_type()->mutable_array()->set_count(2);
    modes_ref->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_enum_type()
        ->set_target_enum_ir_id(5);

    auto* samples = envelope->add_fields();
    samples->set_name("samples");
    samples->mutable_type()->mutable_array()->set_count(3);
    samples->mutable_type()->mutable_array()->mutable_element_type()->set_primitive(
        ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32);

    auto* nested = envelope->add_fields();
    nested->set_name("nested");
    nested->mutable_type()->mutable_array()->set_count(2);
    nested->mutable_type()->mutable_array()->mutable_element_type()->mutable_array()->set_count(3);
    nested->mutable_type()
        ->mutable_array()
        ->mutable_element_type()
        ->mutable_array()
        ->mutable_element_type()
        ->set_primitive(::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U16);

    return schema_ir;
}

TEST(BackendCodegenTest, EmptySchemaGeneratesNoFiles) {
    const CodegenResult result = run_backend("", CodegenOptions{});
    EXPECT_TRUE(result.success) << result.error_message;
    EXPECT_TRUE(result.files.empty());
}

TEST(BackendCodegenTest, SingleRecordMatchesGolden) {
    const std::string source = backend_fixture_text("single_record");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("single_record"));
}

TEST(BackendCodegenTest, GeneratedSingleRecordHeaderCompilesAndRuns) {
    const std::string source = backend_fixture_text("single_record");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source = "#include \"" + header_include +
                                           "\"\n"
                                           "int main() {\n"
                                           "  ::ExampleBuilder builder;\n"
                                           "  const auto value = builder.build();\n"
                                           "  (void)value;\n"
                                           "  return 0;\n"
                                           "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, BuiltinScalarFieldsMatchGolden) {
    const std::string source = backend_fixture_text("builtin_scalar_fields");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("builtin_scalar_fields"));
}

TEST(BackendCodegenTest, GeneratedBuiltinScalarHeaderCompilesAndRuns) {
    const std::string source = backend_fixture_text("builtin_scalar_fields");
    const CodegenResult result = run_backend(source, CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <type_traits>\n"
        "int main() {\n"
        "  static_assert(std::is_same_v<decltype(::ExampleBuilder{}.set_active(false)), bool>);\n"
        "  ::ExampleBuilder builder;\n"
        "  if (builder.has_active() || builder.has_count() || builder.has_ratio()) {\n"
        "    return 1;\n"
        "  }\n"
        "  if (!builder.set_active(false)) {\n"
        "    return 2;\n"
        "  }\n"
        "  if (!builder.set_count(0u)) {\n"
        "    return 3;\n"
        "  }\n"
        "  if (!builder.set_ratio(0.0)) {\n"
        "    return 4;\n"
        "  }\n"
        "  const auto value = builder.build();\n"
        "  if (!value.has_active() || !value.has_count() || !value.has_ratio()) {\n"
        "    return 5;\n"
        "  }\n"
        "  if (value.active() == nullptr || *value.active() != false) {\n"
        "    return 6;\n"
        "  }\n"
        "  if (value.count() == nullptr || *value.count() != 0u) {\n"
        "    return 7;\n"
        "  }\n"
        "  if (value.ratio() == nullptr || *value.ratio() != 0.0) {\n"
        "    return 8;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
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

TEST(BackendCodegenTest, MalformedFieldTypeFailsAtomically) {
    Backend backend;
    const CodegenResult result =
        backend.generate(make_manual_unspecified_primitive_schema_ir(), CodegenOptions{});
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.files.empty());
    EXPECT_NE(result.error_message.find("unspecified primitive field type"), std::string::npos);
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

TEST(BackendCodegenTest, VariableLengthFieldsMatchGolden) {
    Backend backend;
    const CodegenResult result =
        backend.generate(make_manual_variable_length_schema_ir(), CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("variable_length_fields"));

    const CodegenResult second_result =
        backend.generate(make_manual_variable_length_schema_ir(), CodegenOptions{});
    ASSERT_TRUE(second_result.success) << second_result.error_message;
    EXPECT_EQ(render_result(result), render_result(second_result));
}

TEST(BackendCodegenTest, GeneratedVariableLengthHeaderCompilesAndRuns) {
    Backend backend;
    const CodegenResult result =
        backend.generate(make_manual_variable_length_schema_ir(), CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <string>\n"
        "#include <type_traits>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  static_assert(std::is_same_v<decltype(::ExampleBuilder{}.set_name(std::string{})), "
        "bool>);\n"
        "  ::ExampleBuilder builder;\n"
        "  if (!builder.set_name(\"example\")) {\n"
        "    return 1;\n"
        "  }\n"
        "  if (!builder.set_payload(std::vector<std::byte>{std::byte{0x01}})) {\n"
        "    return 2;\n"
        "  }\n"
        "  if (!builder.set_counts(std::vector<std::uint32_t>{1u, 2u, 3u, 4u})) {\n"
        "    return 3;\n"
        "  }\n"
        "  if (!builder.set_distances(std::vector<double>{3.5, 4.5})) {\n"
        "    return 4;\n"
        "  }\n"
        "  if (!builder.set_modes(std::vector<::Mode>{::Mode::On, ::Mode::Off})) {\n"
        "    return 5;\n"
        "  }\n"
        "  if (!builder.set_children(std::vector<::Child>{::Child{}, ::Child{}})) {\n"
        "    return 6;\n"
        "  }\n"
        "  const auto value = builder.build();\n"
        "  if (!value.has_name() || !value.has_payload() || !value.has_counts()) {\n"
        "    return 7;\n"
        "  }\n"
        "  if (value.name() == nullptr || *value.name() != \"example\") {\n"
        "    return 8;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, CrossNamespaceArrayReferenceMatchesGolden) {
    Backend backend;
    const CodegenResult result =
        backend.generate(make_manual_cross_namespace_array_schema_ir(), CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2u);
    EXPECT_EQ(result.files[0].path, "generated/alpha/one.generated.hpp");
    EXPECT_EQ(result.files[1].path, "generated/beta/two.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("cross_namespace_array_reference"));
}

TEST(BackendCodegenTest, GeneratedBuilderBoundsHeaderCompilesAndRuns) {
    Backend backend;
    const CodegenResult result =
        backend.generate(make_manual_bounded_builder_schema_ir(), CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2u);
    ASSERT_EQ(result.files[0].path, "generated/alpha/one.generated.hpp");
    ASSERT_EQ(result.files[1].path, "generated/beta/two.generated.hpp");

    const std::string translation_source =
        "#include \"beta/two.generated.hpp\"\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <string>\n"
        "#include <type_traits>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  ::alpha::one::ChildBuilder child_builder;\n"
        "  if (!child_builder.set_count(7u)) {\n"
        "    return 1;\n"
        "  }\n"
        "  const auto child = child_builder.build();\n"
        "  ::alpha::one::WrapperBuilder wrapper_builder;\n"
        "  if (!wrapper_builder.set_child(child) || "
        "!wrapper_builder.set_mode(::alpha::one::Mode::On)) {\n"
        "    return 2;\n"
        "  }\n"
        "  if (!wrapper_builder.set_children(std::vector<::alpha::one::Child>{child, child})) {\n"
        "    return 3;\n"
        "  }\n"
        "  if (!wrapper_builder.set_modes(std::vector<::alpha::one::Mode>{::alpha::one::Mode::Off, "
        "::alpha::one::Mode::On})) {\n"
        "    return 4;\n"
        "  }\n"
        "  const auto wrapper = wrapper_builder.build();\n"
        "  if (!wrapper.has_child() || !wrapper.has_mode() || !wrapper.has_children() || "
        "!wrapper.has_modes()) {\n"
        "    return 5;\n"
        "  }\n"
        "  ::beta::two::EnvelopeBuilder builder;\n"
        "  if (builder.has_active() || builder.has_label() || builder.has_payload()) {\n"
        "    return 6;\n"
        "  }\n"
        "  if (!builder.set_active(false)) {\n"
        "    return 7;\n"
        "  }\n"
        "  if (!builder.set_count(0u) || !builder.set_ratio(0.0)) {\n"
        "    return 8;\n"
        "  }\n"
        "  if (!builder.set_child(child) || !builder.set_mode(::alpha::one::Mode::Off)) {\n"
        "    return 9;\n"
        "  }\n"
        "  if (!builder.set_children(std::vector<::alpha::one::Child>{child, child})) {\n"
        "    return 10;\n"
        "  }\n"
        "  if (!builder.set_modes(std::vector<::alpha::one::Mode>{::alpha::one::Mode::On, "
        "::alpha::one::Mode::Off})) {\n"
        "    return 11;\n"
        "  }\n"
        "  if (!builder.set_samples(std::vector<std::uint32_t>{1u, 2u, 3u})) {\n"
        "    return 12;\n"
        "  }\n"
        "  if (!builder.set_nested(std::vector<std::vector<std::uint16_t>>{{1u, 2u, 3u}, {4u, 5u, "
        "6u}})) {\n"
        "    return 13;\n"
        "  }\n"
        "  if (builder.set_label(std::string(\"toolong\"))) {\n"
        "    return 14;\n"
        "  }\n"
        "  if (builder.has_label()) {\n"
        "    return 15;\n"
        "  }\n"
        "  if (!builder.set_label(std::string(\"hello\"))) {\n"
        "    return 16;\n"
        "  }\n"
        "  if (builder.set_label(std::string(\"toolong\"))) {\n"
        "    return 17;\n"
        "  }\n"
        "  if (!builder.has_label()) {\n"
        "    return 18;\n"
        "  }\n"
        "  if (!builder.set_payload(std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, "
        "std::byte{0x03}, std::byte{0x04}})) {\n"
        "    return 19;\n"
        "  }\n"
        "  if (builder.set_payload(std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, "
        "std::byte{0x03}, std::byte{0x04}, std::byte{0x05}})) {\n"
        "    return 20;\n"
        "  }\n"
        "  if (!builder.has_payload()) {\n"
        "    return 21;\n"
        "  }\n"
        "  if (builder.set_children(std::vector<::alpha::one::Child>{child, child, child})) {\n"
        "    return 22;\n"
        "  }\n"
        "  if (builder.set_modes(std::vector<::alpha::one::Mode>{::alpha::one::Mode::Off, "
        "::alpha::one::Mode::On, ::alpha::one::Mode::Off})) {\n"
        "    return 23;\n"
        "  }\n"
        "  if (builder.set_samples(std::vector<std::uint32_t>{1u, 2u, 3u, 4u})) {\n"
        "    return 24;\n"
        "  }\n"
        "  if (builder.set_nested(std::vector<std::vector<std::uint16_t>>{{1u, 2u, 3u, 4u}})) {\n"
        "    return 25;\n"
        "  }\n"
        "  if (builder.set_nested(std::vector<std::vector<std::uint16_t>>{{1u, 2u, 3u}, {4u, 5u, "
        "6u}, {7u, 8u, 9u}})) {\n"
        "    return 26;\n"
        "  }\n"
        "  const auto value = builder.build();\n"
        "  if (!value.has_active() || !value.has_count() || !value.has_ratio() || "
        "!value.has_label() || !value.has_payload()) {\n"
        "    return 27;\n"
        "  }\n"
        "  if (value.active() == nullptr || *value.active() != false) {\n"
        "    return 28;\n"
        "  }\n"
        "  if (value.count() == nullptr || *value.count() != 0u) {\n"
        "    return 29;\n"
        "  }\n"
        "  if (value.ratio() == nullptr || *value.ratio() != 0.0) {\n"
        "    return 30;\n"
        "  }\n"
        "  if (value.label() == nullptr || *value.label() != std::string(\"hello\")) {\n"
        "    return 31;\n"
        "  }\n"
        "  if (value.payload() == nullptr || value.payload()->size() != 4u) {\n"
        "    return 32;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/beta/two.generated.hpp", translation_source);
}

} // namespace
