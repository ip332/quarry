#include "compiler/backend/backend.hpp"
#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/schema_ir/validation.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <google/protobuf/text_format.h>

#include <gtest/gtest.h>

#ifndef QUARRY_TEST_GENERATED_CODE_API_VERSION
#error "QUARRY_TEST_GENERATED_CODE_API_VERSION must be defined"
#endif

#ifndef QUARRY_TEST_GENERATED_INCLUDE_DIR
#error "QUARRY_TEST_GENERATED_INCLUDE_DIR must be defined"
#endif

namespace {

using quarry::compiler::backend::Backend;
using quarry::compiler::backend::CodegenOptions;
using quarry::compiler::backend::CodegenResult;
using quarry::compiler::backend::PlanResult;
using quarry::compiler::schema_ir::SchemaIrModel;
using quarry::compiler::schema_ir::SchemaIrValidator;

[[nodiscard]] std::filesystem::path fixtures_root(std::string_view category) {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" / category;
}

[[nodiscard]] std::filesystem::path backend_fixtures_root() { return fixtures_root("backend"); }

[[nodiscard]] std::filesystem::path backend_schema_ir_fixtures_root() {
    return backend_fixtures_root() / "schema_ir";
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

[[nodiscard]] std::string
diagnostics_summary(const quarry::compiler::diagnostics::DiagnosticCollection& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.compiler_pass() << ": " << diagnostic.id().str() << ": "
               << diagnostic.message() << '\n';
    }
    return stream.str();
}

[[nodiscard]] std::string backend_fixture_text(std::string_view name) {
    std::string text = read_file(backend_fixtures_root() / (std::string(name) + ".txt"));
    trim_trailing_newlines(text);
    return text;
}

[[nodiscard]] std::string schema_ir_fixture_text(std::string_view name) {
    std::string text =
        read_file(backend_schema_ir_fixtures_root() / (std::string(name) + ".pbtxt"));
    trim_trailing_newlines(text);
    return text;
}

struct LoadedSchemaIrFixture {
    std::optional<SchemaIrModel> schema_ir;
    std::string error_message;
};

[[nodiscard]] LoadedSchemaIrFixture load_validated_schema_ir_fixture(std::string_view name) {
    LoadedSchemaIrFixture output;
    const std::string text = schema_ir_fixture_text(name);

    SchemaIrModel schema_ir;
    if (!google::protobuf::TextFormat::ParseFromString(text, &schema_ir)) {
        output.error_message = "failed to parse Schema IR pbtxt fixture: " + std::string(name);
        return output;
    }

    quarry::compiler::context::CompilerContext context;
    quarry::compiler::diagnostics::DiagnosticCollection diagnostics;
    SchemaIrValidator validator;
    validator.validate(schema_ir, context, diagnostics);
    if (!diagnostics.empty()) {
        output.error_message = diagnostics_summary(diagnostics);
        return output;
    }

    output.schema_ir = std::move(schema_ir);
    return output;
}

[[nodiscard]] CodegenResult run_backend_fixture(std::string_view name,
                                                const CodegenOptions& options) {
    const LoadedSchemaIrFixture fixture = load_validated_schema_ir_fixture(name);
    if (!fixture.schema_ir.has_value()) {
        ADD_FAILURE() << "fixture: " << name << '\n' << fixture.error_message;
        return {};
    }

    Backend backend;
    return backend.generate(*fixture.schema_ir, options);
}

[[nodiscard]] PlanResult plan_backend_fixture(std::string_view name,
                                              const CodegenOptions& options) {
    const LoadedSchemaIrFixture fixture = load_validated_schema_ir_fixture(name);
    if (!fixture.schema_ir.has_value()) {
        ADD_FAILURE() << "fixture: " << name << '\n' << fixture.error_message;
        return {};
    }

    Backend backend;
    return backend.plan(*fixture.schema_ir, options);
}

[[nodiscard]] std::string backend_golden_text(std::string_view name) {
    return backend_fixture_text(name);
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
                                            (std::string("quarry-backend-codegen-") +
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
    const std::string compiler = QUARRY_TEST_CXX_COMPILER;
    const std::filesystem::path repo_root =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    const std::filesystem::path generated_include_root = QUARRY_TEST_GENERATED_INCLUDE_DIR;
    std::ostringstream command;
    command << std::quoted(compiler) << " -std=c++20 -I" << std::quoted(generated_root.string())
            << " -I" << std::quoted(generated_include_root.string())
            << " -I" << std::quoted(repo_root.string()) << " "
            << std::quoted(source_path.string()) << " -o "
            << std::quoted(executable_path.string());

    const int status = run_command(command.str());
    ASSERT_EQ(status, 0) << "command failed: " << command.str();

    std::ostringstream run_command_stream;
    run_command_stream << std::quoted(executable_path.string());
    const int run_status = run_command(run_command_stream.str());
    ASSERT_EQ(run_status, 0) << "command failed: " << executable_path.string();
}

[[nodiscard]] std::string compile_source_expect_failure(std::string_view source_text) {
    const std::filesystem::path root = make_temp_directory("compile-failure");
    const std::filesystem::path source_path = root / "compile.cpp";
    const std::filesystem::path executable_path = root / "compile";
    const std::filesystem::path output_path = root / "compiler-output.txt";
    write_text_file(source_path, source_text);

    const std::string compiler = QUARRY_TEST_CXX_COMPILER;
    const std::filesystem::path repo_root =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    const std::filesystem::path generated_include_root = QUARRY_TEST_GENERATED_INCLUDE_DIR;
    std::ostringstream command;
    command << std::quoted(compiler) << " -std=c++20 -I"
            << std::quoted(generated_include_root.string()) << " -I"
            << std::quoted(repo_root.string()) << " " << std::quoted(source_path.string()) << " -o "
            << std::quoted(executable_path.string()) << " > "
            << std::quoted(output_path.string()) << " 2>&1";

    const int status = run_command(command.str());
    EXPECT_NE(status, 0) << "command unexpectedly succeeded: " << command.str();
    return read_file(output_path);
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
    field->mutable_type()->set_primitive(::quarry::schema_ir::PRIMITIVE_TYPE_UNSPECIFIED);
    return schema_ir;
}

[[nodiscard]] SchemaIrModel make_manual_cyclic_namespace_schema_ir() {
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

    auto* first = alpha_one->add_records();
    first->set_ir_id(4);
    first->set_record_id(1);
    first->set_name("First");
    first->set_fqn("alpha.one.First");
    auto* first_field = first->add_fields();
    first_field->set_name("other");
    first_field->mutable_type()->mutable_record()->set_target_record_ir_id(7);

    auto* beta = root->add_namespaces();
    beta->set_ir_id(5);
    beta->set_name("beta");
    beta->set_fqn("beta");

    auto* beta_two = beta->add_namespaces();
    beta_two->set_ir_id(6);
    beta_two->set_name("two");
    beta_two->set_fqn("beta.two");

    auto* second = beta_two->add_records();
    second->set_ir_id(7);
    second->set_record_id(2);
    second->set_name("Second");
    second->set_fqn("beta.two.Second");
    auto* second_field = second->add_fields();
    second_field->set_name("other");
    second_field->mutable_type()->mutable_record()->set_target_record_ir_id(4);

    return schema_ir;
}

[[nodiscard]] SchemaIrModel make_manual_record_array_enum_schema_ir() {
    SchemaIrModel schema_ir;
    schema_ir.set_schema_ir_version(1);
    auto* root = schema_ir.mutable_root_namespace();
    root->set_ir_id(1);
    root->set_name("");
    root->set_fqn("");

    auto* mode_enum = root->add_enums();
    mode_enum->set_ir_id(2);
    mode_enum->set_name("Mode");
    mode_enum->set_fqn("Mode");
    auto* off_value = mode_enum->add_values();
    off_value->set_name("Off");
    off_value->set_value(0);
    auto* on_value = mode_enum->add_values();
    on_value->set_name("On");
    on_value->set_value(1);

    auto* element = root->add_records();
    element->set_ir_id(3);
    element->set_record_id(1);
    element->set_name("Element");
    element->set_fqn("Element");
    auto* mode_field = element->add_fields();
    mode_field->set_name("mode");
    mode_field->mutable_type()->mutable_enum_type()->set_target_enum_ir_id(2);

    auto* basket = root->add_records();
    basket->set_ir_id(4);
    basket->set_record_id(2);
    basket->set_name("Basket");
    basket->set_fqn("Basket");
    auto* elements_field = basket->add_fields();
    elements_field->set_name("elements");
    auto* array_type = elements_field->mutable_type()->mutable_array();
    array_type->set_max_elements(4);
    array_type->mutable_element_type()->mutable_record()->set_target_record_ir_id(3);

    return schema_ir;
}

TEST(BackendCodegenTest, EmptySchemaGeneratesNoFiles) {
    const CodegenResult result = run_backend_fixture("empty", CodegenOptions{});
    EXPECT_TRUE(result.success) << result.error_message;
    EXPECT_TRUE(result.files.empty());
}

TEST(BackendCodegenTest, SingleRecordMatchesGolden) {
    const CodegenResult result = run_backend_fixture("single_record", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("single_record"));
}

TEST(BackendCodegenTest, GenerationPlanUsesCustomRootStemAndExtensionForRootNamespace) {
    CodegenOptions options;
    options.output_directory = "out";
    options.root_file_stem = "telemetry";
    options.file_extension = ".hpp";

    const PlanResult result = plan_backend_fixture("single_record", options);
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.plan.files.size(), 1u);
    EXPECT_EQ(result.plan.files.front().relative_output_path, "telemetry.hpp");
    EXPECT_EQ(result.plan.files.front().generated_include_path, "telemetry.hpp");
}

TEST(BackendCodegenTest, GenerationPlanUsesNamespacePathForNestedNamespaceOutput) {
    CodegenOptions options;
    options.output_directory = "out";
    options.root_file_stem = "root";
    options.file_extension = ".hpp";

    const PlanResult result = plan_backend_fixture("named_type_reference", options);
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.plan.files.size(), 1u);
    EXPECT_EQ(result.plan.files.front().relative_output_path, "quarry/geo.hpp");
    EXPECT_EQ(result.plan.files.front().generated_include_path, "quarry/geo.hpp");
}

TEST(BackendCodegenTest, GenerationPlanPreservesMultiFileOrderingAndIncludePaths) {
    CodegenOptions options;
    options.output_directory = "out";
    options.root_file_stem = "root";
    options.file_extension = ".hpp";

    const PlanResult plan = plan_backend_fixture("cross_namespace_reference", options);
    ASSERT_TRUE(plan.success) << plan.error_message;
    ASSERT_EQ(plan.plan.files.size(), 2u);
    EXPECT_EQ(plan.plan.files[0].relative_output_path, "alpha/one.hpp");
    EXPECT_EQ(plan.plan.files[0].generated_include_path, "alpha/one.hpp");
    EXPECT_EQ(plan.plan.files[1].relative_output_path, "beta/two.hpp");
    EXPECT_EQ(plan.plan.files[1].generated_include_path, "beta/two.hpp");

    const CodegenResult rendered = run_backend_fixture("cross_namespace_reference", options);
    ASSERT_TRUE(rendered.success) << rendered.error_message;
    ASSERT_EQ(rendered.files.size(), 2u);
    EXPECT_EQ(rendered.files[0].path, "out/alpha/one.hpp");
    EXPECT_EQ(rendered.files[1].path, "out/beta/two.hpp");
    EXPECT_NE(rendered.files[1].content.find("#include \"alpha/one.hpp\""), std::string::npos);
}

TEST(BackendCodegenTest, GeneratedRecordsAssertRuntimeGeneratedCodeApiVersion) {
    const CodegenResult result = run_backend_fixture("single_record", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1);

    const std::string expected_assertion =
        "static_assert(::quarry::runtime::kGeneratedCodeApiVersion == " +
        std::to_string(QUARRY_TEST_GENERATED_CODE_API_VERSION) + "U";
    EXPECT_NE(result.files.front().content.find(expected_assertion),
              std::string::npos);
    EXPECT_NE(result.files.front().content.find(
                  "Generated Quarry code is incompatible with the installed Quarry "
                  "runtime."),
              std::string::npos);
}

TEST(BackendCodegenTest, RuntimeGeneratedCodeApiVersionMismatchFailsCompilation) {
    const std::string output = compile_source_expect_failure(
        "#include \"runtime/binary_record.hpp\"\n"
        "static_assert(::quarry::runtime::kGeneratedCodeApiVersion == 999U,\n"
        "              \"Generated Quarry code is incompatible with the installed "
        "Quarry runtime. Regenerate the code using a compatible "
        "quarry-schema-compiler release.\");\n"
        "int main() { return 0; }\n");

    EXPECT_NE(output.find("Generated Quarry code is incompatible with the installed "
                          "Quarry runtime."),
              std::string::npos);
}

TEST(BackendCodegenTest, GeneratedSingleRecordHeaderCompilesAndRuns) {
    const CodegenResult result = run_backend_fixture("single_record", CodegenOptions{});
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

TEST(BackendCodegenTest, GeneratedEmptyRecordEncoderEmitsHeaderOnlyRecord) {
    const CodegenResult result = run_backend_fixture("single_record", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const auto value = ::ExampleBuilder{}.build();\n"
        "  const auto encoded = encode(value);\n"
        "  if (!encoded.has_value()) {\n"
        "    return 1;\n"
        "  }\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00)};\n"
        "  return *encoded == expected ? 0 : 2;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, BuiltinScalarFieldsMatchGolden) {
    const CodegenResult result = run_backend_fixture("builtin_scalar_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("builtin_scalar_fields"));
}

TEST(BackendCodegenTest, GeneratedBuiltinScalarHeaderCompilesAndRuns) {
    const CodegenResult result = run_backend_fixture("builtin_scalar_fields", CodegenOptions{});
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

TEST(BackendCodegenTest, GeneratedScalarEncoderEmitsExactBytes) {
    const CodegenResult result = run_backend_fixture("builtin_scalar_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  ::ExampleBuilder builder;\n"
        "  if (!builder.set_count(0x01020304U)) {\n"
        "    return 1;\n"
        "  }\n"
        "  if (!builder.set_active(false)) {\n"
        "    return 2;\n"
        "  }\n"
        "  if (!builder.set_ratio(1.5)) {\n"
        "    return 3;\n"
        "  }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  if (!encoded.has_value()) {\n"
        "    return 4;\n"
        "  }\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x03), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x16),\n"
        "      byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x01), byte(0x01), byte(0x04),\n"
        "      byte(0x02), byte(0x05), byte(0x08),\n"
        "      byte(0x00),\n"
        "      byte(0x01), byte(0x02), byte(0x03), byte(0x04),\n"
        "      byte(0x3F), byte(0xF8), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00)};\n"
        "  return *encoded == expected ? 0 : 5;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedScalarEncoderDistinguishesAbsentAndPresentDefault) {
    const CodegenResult result = run_backend_fixture("builtin_scalar_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  ::ExampleBuilder empty_builder;\n"
        "  const auto empty_encoded = encode(empty_builder.build());\n"
        "  if (!empty_encoded.has_value() || empty_encoded->size() != 16U) {\n"
        "    return 1;\n"
        "  }\n"
        "  ::ExampleBuilder builder;\n"
        "  if (!builder.set_active(false)) {\n"
        "    return 2;\n"
        "  }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  if (!encoded.has_value()) {\n"
        "    return 3;\n"
        "  }\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x00), byte(0x00), byte(0x01), byte(0x00)};\n"
        "  return *encoded == expected ? 0 : 4;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedScalarDecoderReadsManualBytes) {
    const CodegenResult result = run_backend_fixture("builtin_scalar_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> input{\n"
        "      byte(0x01), byte(0x00), byte(0x03), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x16),\n"
        "      byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x01), byte(0x01), byte(0x04),\n"
        "      byte(0x02), byte(0x05), byte(0x08),\n"
        "      byte(0x00),\n"
        "      byte(0x01), byte(0x02), byte(0x03), byte(0x04),\n"
        "      byte(0x3F), byte(0xF8), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00)};\n"
        "  const auto decoded = decode_Example(input);\n"
        "  if (!decoded.has_value()) {\n"
        "    return 1;\n"
        "  }\n"
        "  if (!decoded->has_active() || decoded->active() == nullptr || *decoded->active()) {\n"
        "    return 2;\n"
        "  }\n"
        "  if (!decoded->has_count() || decoded->count() == nullptr || *decoded->count() != 0x01020304U) {\n"
        "    return 3;\n"
        "  }\n"
        "  if (!decoded->has_ratio() || decoded->ratio() == nullptr || *decoded->ratio() != 1.5) {\n"
        "    return 4;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedScalarDecoderReportsFieldPayloadByteOffset) {
    const CodegenResult result = run_backend_fixture("builtin_scalar_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> input{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x06),\n"
        "      byte(0x01), byte(0x00), byte(0x03),\n"
        "      byte(0xAA), byte(0xBB), byte(0xCC)};\n"
        "  const auto decoded = decode_Example_result(input);\n"
        "  if (decoded.value.has_value()) { return 1; }\n"
        "  if (decoded.error != ::quarry::runtime::DecodeError::invalid_field_length) {\n"
        "    return 2;\n"
        "  }\n"
        "  if (decoded.path.size() != 1U ||\n"
        "      static_cast<unsigned int>(decoded.path[0].field_index) != 1U ||\n"
        "      decoded.path[0].array_index.has_value()) {\n"
        "    return 3;\n"
        "  }\n"
        "  if (!decoded.byte_offset.has_value() || *decoded.byte_offset != 19U) {\n"
        "    return 4;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedScalarDecoderRoundTripsAndPreservesPresence) {
    const CodegenResult result = run_backend_fixture("builtin_scalar_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "int main() {\n"
        "  ::ExampleBuilder builder;\n"
        "  if (!builder.set_active(false) || !builder.set_count(0U)) {\n"
        "    return 1;\n"
        "  }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  if (!encoded.has_value()) {\n"
        "    return 2;\n"
        "  }\n"
        "  const auto decoded = decode_Example(*encoded);\n"
        "  if (!decoded.has_value()) {\n"
        "    return 3;\n"
        "  }\n"
        "  if (!decoded->has_active() || decoded->active() == nullptr || *decoded->active()) {\n"
        "    return 4;\n"
        "  }\n"
        "  if (!decoded->has_count() || decoded->count() == nullptr || *decoded->count() != 0U) {\n"
        "    return 5;\n"
        "  }\n"
        "  if (decoded->has_ratio()) {\n"
        "    return 6;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedScalarDecoderRejectsInvalidKnownFieldsAndIgnoresUnknownFields) {
    const CodegenResult result = run_backend_fixture("builtin_scalar_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> invalid_bool{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x00), byte(0x00), byte(0x01), byte(0x02)};\n"
        "  if (decode_Example(invalid_bool).has_value()) {\n"
        "    return 1;\n"
        "  }\n"
        "  const std::vector<std::byte> short_count{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00)};\n"
        "  if (decode_Example(short_count).has_value()) {\n"
        "    return 2;\n"
        "  }\n"
        "  const std::vector<std::byte> unknown_field{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x09), byte(0x00), byte(0x01), byte(0xFF)};\n"
        "  const auto decoded = decode_Example(unknown_field);\n"
        "  if (!decoded.has_value()) {\n"
        "    return 3;\n"
        "  }\n"
        "  if (decoded->has_active() || decoded->has_count() || decoded->has_ratio()) {\n"
        "    return 4;\n"
        "  }\n"
        "  std::vector<std::byte> unexpected_record = unknown_field;\n"
        "  unexpected_record[7] = byte(0x02);\n"
        "  return decode_Example(unexpected_record).has_value() ? 5 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedDiagnosticDecodeReportsStructuralAndSchemaErrors) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  using ::quarry::runtime::DecodeError;\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> truncated_header{byte(0x01)};\n"
        "  if (decode_Example_result(truncated_header).error != DecodeError::truncated_header) {\n"
        "    return 1;\n"
        "  }\n"
        "  const std::vector<std::byte> wrong_record_id{\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x03),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00)};\n"
        "  if (decode_Example_result(wrong_record_id).error != DecodeError::unexpected_record_id) {\n"
        "    return 2;\n"
        "  }\n"
        "  const std::vector<std::byte> invalid_bool{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x09), byte(0x00), byte(0x01), byte(0x02)};\n"
        "  if (decode_Example_result(invalid_bool).error != DecodeError::invalid_bool) {\n"
        "    return 3;\n"
        "  }\n"
        "  const std::vector<std::byte> invalid_utf8{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x00), byte(0x00), byte(0x02), byte(0xC0), byte(0x80)};\n"
        "  if (decode_Example_result(invalid_utf8).error != DecodeError::invalid_utf8) {\n"
        "    return 4;\n"
        "  }\n"
        "  if (decode_Example(invalid_utf8).has_value()) {\n"
        "    return 5;\n"
        "  }\n"
        "  const std::vector<std::byte> over_bound_string{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x14),\n"
        "      byte(0x00), byte(0x00), byte(0x11),\n"
        "      byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61),\n"
        "      byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61),\n"
        "      byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61)};\n"
        "  if (decode_Example_result(over_bound_string).error != DecodeError::bounds_exceeded) {\n"
        "    return 6;\n"
        "  }\n"
        "  const std::vector<std::byte> count_over_bound{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x02), byte(0x00), byte(0x01), byte(0x05)};\n"
        "  if (decode_Example_result(count_over_bound).error != DecodeError::bounds_exceeded) {\n"
        "    return 7;\n"
        "  }\n"
        "  const std::vector<std::byte> unknown_enum{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x04), byte(0x00), byte(0x02), byte(0x01), byte(0x07)};\n"
        "  if (decode_Example_result(unknown_enum).error != DecodeError::unknown_enum_value) {\n"
        "    return 8;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedDiagnosticEncodeReportsSchemaErrors) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <string>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  using ::quarry::runtime::EncodeError;\n"
        "  ::ExampleBuilder invalid_string_builder;\n"
        "  if (!invalid_string_builder.set_name(std::string(\"\\xC0\\x80\", 2U))) {\n"
        "    return 1;\n"
        "  }\n"
        "  const auto invalid_string = encode_result(invalid_string_builder.build());\n"
        "  if (invalid_string.has_value() || invalid_string.error != EncodeError::invalid_utf8) {\n"
        "    return 2;\n"
        "  }\n"
        "  if (encode(invalid_string_builder.build()).has_value()) {\n"
        "    return 3;\n"
        "  }\n"
        "  ::ExampleBuilder unknown_enum_builder;\n"
        "  if (!unknown_enum_builder.set_modes(std::vector<::Mode>{static_cast<::Mode>(7)})) {\n"
        "    return 4;\n"
        "  }\n"
        "  const auto unknown_enum = encode_result(unknown_enum_builder.build());\n"
        "  if (unknown_enum.has_value() || unknown_enum.error != EncodeError::unknown_enum_value) {\n"
        "    return 5;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, EnumMatchesGolden) {
    const CodegenResult result = run_backend_fixture("enum", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("enum"));
}

TEST(BackendCodegenTest, NegativeEnumValuesArePreserved) {
    const CodegenResult result = run_backend_fixture("negative_enum_values", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_NE(render_result(result).find("enum class SignedValue : std::int64_t"),
              std::string::npos);
    EXPECT_NE(render_result(result).find("Zero = 0"), std::string::npos);
    EXPECT_NE(render_result(result).find("Negative = -1"), std::string::npos);
}

TEST(BackendCodegenTest, NamedTypeReferenceMatchesGolden) {
    const CodegenResult result = run_backend_fixture("named_type_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/quarry/geo.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("named_type_reference"));
}

TEST(BackendCodegenTest, SameFileForwardReferenceOrdersDefinitions) {
    const CodegenResult result = run_backend_fixture("forward_record_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("forward_record_reference"));
}

TEST(BackendCodegenTest, NestedRecordFieldUsesCompleteEmbeddedRecord) {
    const CodegenResult result = run_backend_fixture("forward_record_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    compile_generated_header(
        result, "generated/schema.generated.hpp",
        "#include \"generated/schema.generated.hpp\"\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <vector>\n"
        "\n"
        "int main() {\n"
        "  using byte = std::byte;\n"
        "  B empty_child;\n"
        "  ABuilder empty_parent_builder;\n"
        "  if (!empty_parent_builder.set_value(empty_child)) { return 1; }\n"
        "  const auto empty_parent = encode(empty_parent_builder.build());\n"
        "  const std::vector<byte> expected_empty{\n"
        "      byte{0x01}, byte{0x00}, byte{0x01}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x01},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x13},\n"
        "      byte{0x00}, byte{0x00}, byte{0x10},\n"
        "      byte{0x01}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x02},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "  };\n"
        "  if (!empty_parent.has_value() || *empty_parent != expected_empty) { return 2; }\n"
        "  const auto decoded_empty = decode_A(*empty_parent);\n"
        "  if (!decoded_empty.has_value() || !decoded_empty->has_value() ||\n"
        "      decoded_empty->value()->has_count()) {\n"
        "    return 3;\n"
        "  }\n"
        "\n"
        "  BBuilder child_builder;\n"
        "  if (!child_builder.set_count(0x01020304U)) { return 4; }\n"
        "  ABuilder parent_builder;\n"
        "  if (!parent_builder.set_value(child_builder.build())) { return 5; }\n"
        "  const auto encoded_parent = encode(parent_builder.build());\n"
        "  const std::vector<byte> expected_populated{\n"
        "      byte{0x01}, byte{0x00}, byte{0x01}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x01},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x1A},\n"
        "      byte{0x00}, byte{0x00}, byte{0x17},\n"
        "      byte{0x01}, byte{0x00}, byte{0x01}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x02},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x07},\n"
        "      byte{0x00}, byte{0x00}, byte{0x04},\n"
        "      byte{0x01}, byte{0x02}, byte{0x03}, byte{0x04},\n"
        "  };\n"
        "  if (!encoded_parent.has_value() || *encoded_parent != expected_populated) { return 6; }\n"
        "  const auto decoded_parent = decode_A(*encoded_parent);\n"
        "  if (!decoded_parent.has_value() || !decoded_parent->has_value() ||\n"
        "      !decoded_parent->value()->has_count() ||\n"
        "      *decoded_parent->value()->count() != 0x01020304U) {\n"
        "    return 7;\n"
        "  }\n"
        "\n"
        "  const std::vector<byte> wrong_nested_id{\n"
        "      byte{0x01}, byte{0x00}, byte{0x01}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x01},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x1A},\n"
        "      byte{0x00}, byte{0x00}, byte{0x17},\n"
        "      byte{0x01}, byte{0x00}, byte{0x01}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x03},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x07},\n"
        "      byte{0x00}, byte{0x00}, byte{0x04},\n"
        "      byte{0x01}, byte{0x02}, byte{0x03}, byte{0x04},\n"
        "  };\n"
        "  if (decode_A(wrong_nested_id).has_value()) { return 8; }\n"
        "\n"
        "  const std::vector<byte> truncated_nested_header{\n"
        "      byte{0x01}, byte{0x00}, byte{0x01}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x01},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x12},\n"
        "      byte{0x00}, byte{0x00}, byte{0x0F},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00},\n"
        "  };\n"
        "  if (decode_A(truncated_nested_header).has_value()) { return 9; }\n"
        "\n"
        "  std::vector<byte> trailing_nested = expected_populated;\n"
        "  trailing_nested[15] = byte{0x1B};\n"
        "  trailing_nested[18] = byte{0x18};\n"
        "  trailing_nested.push_back(byte{0xFF});\n"
        "  if (decode_A(trailing_nested).has_value()) { return 10; }\n"
        "\n"
        "  const std::vector<byte> unknown_nested_field{\n"
        "      byte{0x01}, byte{0x00}, byte{0x01}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x01},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x1E},\n"
        "      byte{0x00}, byte{0x00}, byte{0x1B},\n"
        "      byte{0x01}, byte{0x00}, byte{0x02}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x02},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x0B},\n"
        "      byte{0x00}, byte{0x00}, byte{0x04},\n"
        "      byte{0x03}, byte{0x04}, byte{0x01},\n"
        "      byte{0x01}, byte{0x02}, byte{0x03}, byte{0x04}, byte{0xFF},\n"
        "  };\n"
        "  const auto decoded_unknown = decode_A(unknown_nested_field);\n"
        "  if (!decoded_unknown.has_value() || !decoded_unknown->has_value() ||\n"
        "      !decoded_unknown->value()->has_count() ||\n"
        "      *decoded_unknown->value()->count() != 0x01020304U) {\n"
        "    return 11;\n"
        "  }\n"
        "  return 0;\n"
        "}\n");
}

TEST(BackendCodegenTest, MultipleTopLevelNamespacesMatchGolden) {
    const CodegenResult result =
        run_backend_fixture("multiple_top_level_namespaces", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2u);
    EXPECT_EQ(result.files[0].path, "generated/alpha/one.generated.hpp");
    EXPECT_EQ(result.files[1].path, "generated/beta/two.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("multiple_top_level_namespaces"));
}

TEST(BackendCodegenTest, CrossNamespaceReferenceMatchesGolden) {
    const CodegenResult result = run_backend_fixture("cross_namespace_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2u);
    EXPECT_EQ(result.files[0].path, "generated/alpha/one.generated.hpp");
    EXPECT_EQ(result.files[1].path, "generated/beta/two.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("cross_namespace_reference"));
}

TEST(BackendCodegenTest, CrossNamespaceNestedRecordCodecCompilesAndRoundTrips) {
    const CodegenResult result = run_backend_fixture("cross_namespace_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    compile_generated_header(
        result, "generated/beta/two.generated.hpp",
        "#include \"generated/beta/two.generated.hpp\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "\n"
        "int main() {\n"
        "  using byte = std::byte;\n"
        "  ::alpha::one::First first;\n"
        "  ::beta::two::SecondBuilder builder;\n"
        "  if (!builder.set_first(first)) { return 1; }\n"
        "  const auto encoded = ::beta::two::encode(builder.build());\n"
        "  const std::vector<byte> expected{\n"
        "      byte{0x01}, byte{0x00}, byte{0x01}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x02},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x13},\n"
        "      byte{0x00}, byte{0x00}, byte{0x10},\n"
        "      byte{0x01}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x01},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "      byte{0x00}, byte{0x00}, byte{0x00}, byte{0x00},\n"
        "  };\n"
        "  if (!encoded.has_value() || *encoded != expected) { return 2; }\n"
        "  const auto decoded = ::beta::two::decode_Second(expected);\n"
        "  if (!decoded.has_value() || !decoded->has_first()) { return 3; }\n"
        "  return 0;\n"
        "}\n");
}

TEST(BackendCodegenTest, CrossNamespaceEnumReferenceMatchesGolden) {
    const CodegenResult result =
        run_backend_fixture("cross_namespace_enum_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2u);
    EXPECT_EQ(result.files[0].path, "generated/alpha/one.generated.hpp");
    EXPECT_EQ(result.files[1].path, "generated/beta/two.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("cross_namespace_enum_reference"));
}

TEST(BackendCodegenTest, CyclicNamespaceDependencyFailsClearly) {
    const CodegenResult result =
        Backend{}.generate(make_manual_cyclic_namespace_schema_ir(), CodegenOptions{});
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
    const CodegenResult result = run_backend_fixture("enum_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("enum_reference"));
}

TEST(BackendCodegenTest, EnumAndRecordSameNamespaceMatchGolden) {
    const CodegenResult result =
        run_backend_fixture("mixed_same_namespace_dependencies", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("mixed_same_namespace_dependencies"));
}

TEST(BackendCodegenTest, NestedRecordFieldsMatchGolden) {
    const CodegenResult result = run_backend_fixture("nested_record_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("nested_record_fields"));
}

TEST(BackendCodegenTest, MultiLevelNestedRecordsRoundTripSupportedFields) {
    const CodegenResult result = run_backend_fixture("nested_record_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    compile_generated_header(
        result, "generated/schema.generated.hpp",
        "#include \"generated/schema.generated.hpp\"\n"
        "#include <cstdint>\n"
        "#include <string>\n"
        "#include <vector>\n"
        "\n"
        "int main() {\n"
        "  InnerBuilder inner_builder;\n"
        "  if (!inner_builder.set_count(7U)) { return 1; }\n"
        "  if (!inner_builder.set_label(\"ok\")) { return 2; }\n"
        "  if (!inner_builder.set_tags(std::vector<std::string>{\"A\", \"\"})) { return 3; }\n"
        "  const Inner inner = inner_builder.build();\n"
        "\n"
        "  MiddleBuilder middle_builder;\n"
        "  if (!middle_builder.set_inner(inner)) { return 4; }\n"
        "\n"
        "  OuterBuilder outer_builder;\n"
        "  if (!outer_builder.set_middle(middle_builder.build())) { return 5; }\n"
        "  if (!outer_builder.set_fallback(Inner{})) { return 6; }\n"
        "  const auto encoded = encode(outer_builder.build());\n"
        "  if (!encoded.has_value()) { return 7; }\n"
        "\n"
        "  const auto decoded = decode_Outer(*encoded);\n"
        "  if (!decoded.has_value() || !decoded->has_middle() || !decoded->has_fallback()) {\n"
        "    return 8;\n"
        "  }\n"
        "  const Inner* decoded_inner = decoded->middle()->inner();\n"
        "  if (decoded_inner == nullptr || !decoded_inner->has_count() ||\n"
        "      *decoded_inner->count() != 7U || !decoded_inner->has_label() ||\n"
        "      *decoded_inner->label() != \"ok\" || !decoded_inner->has_tags()) {\n"
        "    return 9;\n"
        "  }\n"
        "  if (decoded_inner->tags()->size() != 2U || (*decoded_inner->tags())[0] != \"A\" ||\n"
        "      !(*decoded_inner->tags())[1].empty()) {\n"
        "    return 10;\n"
        "  }\n"
        "  if (decoded->fallback()->has_count() || decoded->fallback()->has_label() ||\n"
        "      decoded->fallback()->has_tags()) {\n"
        "    return 11;\n"
        "  }\n"
        "  return 0;\n"
        "}\n");
}

TEST(BackendCodegenTest, RecordArrayComposesWithNestedRecordFields) {
    const CodegenResult result = run_backend_fixture("nested_record_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    compile_generated_header(
        result, "generated/schema.generated.hpp",
        "#include \"generated/schema.generated.hpp\"\n"
        "#include <cstdint>\n"
        "#include <string>\n"
        "#include <vector>\n"
        "\n"
        "int main() {\n"
        "  InnerBuilder inner_builder;\n"
        "  if (!inner_builder.set_count(11U)) { return 1; }\n"
        "  if (!inner_builder.set_label(\"in\")) { return 2; }\n"
        "  const Inner inner = inner_builder.build();\n"
        "\n"
        "  MiddleBuilder middle_builder;\n"
        "  if (!middle_builder.set_inner(inner)) { return 3; }\n"
        "  const Middle middle = middle_builder.build();\n"
        "\n"
        "  GroupBuilder group_builder;\n"
        "  if (!group_builder.set_middles(std::vector<Middle>{Middle{}, middle})) {\n"
        "    return 4;\n"
        "  }\n"
        "  const auto encoded = encode(group_builder.build());\n"
        "  if (!encoded.has_value()) { return 5; }\n"
        "\n"
        "  const auto decoded = decode_Group(*encoded);\n"
        "  if (!decoded.has_value() || !decoded->has_middles() ||\n"
        "      decoded->middles()->size() != 2U) {\n"
        "    return 6;\n"
        "  }\n"
        "  if ((*decoded->middles())[0].has_inner()) {\n"
        "    return 7;\n"
        "  }\n"
        "  const Inner* decoded_inner = (*decoded->middles())[1].inner();\n"
        "  if (decoded_inner == nullptr || !decoded_inner->has_count() ||\n"
        "      *decoded_inner->count() != 11U || !decoded_inner->has_label() ||\n"
        "      *decoded_inner->label() != \"in\") {\n"
        "    return 8;\n"
        "  }\n"
        "  return 0;\n"
        "}\n");
}

TEST(BackendCodegenTest, GeneratedDiagnosticCodecsPropagateNestedAndRecordArrayErrors) {
    const CodegenResult result = run_backend_fixture("nested_record_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    compile_generated_header(
        result, "generated/schema.generated.hpp",
        "#include \"generated/schema.generated.hpp\"\n"
        "#include <cstddef>\n"
        "#include <string>\n"
        "#include <vector>\n"
        "\n"
        "int main() {\n"
        "  using ::quarry::runtime::DecodeError;\n"
        "  using ::quarry::runtime::EncodeError;\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  InnerBuilder invalid_inner_builder;\n"
        "  if (!invalid_inner_builder.set_label(std::string(\"\\xC0\\x80\", 2U))) { return 1; }\n"
        "  MiddleBuilder invalid_middle_builder;\n"
        "  if (!invalid_middle_builder.set_inner(invalid_inner_builder.build())) { return 2; }\n"
        "  OuterBuilder invalid_outer_builder;\n"
        "  if (!invalid_outer_builder.set_middle(invalid_middle_builder.build())) { return 3; }\n"
        "  const auto invalid_nested = encode_result(invalid_outer_builder.build());\n"
        "  if (invalid_nested.has_value() || invalid_nested.error != EncodeError::invalid_utf8) {\n"
        "    return 4;\n"
        "  }\n"
        "  if (invalid_nested.byte_offset.has_value()) { return 20; }\n"
        "\n"
        "  GroupBuilder invalid_group_builder;\n"
        "  if (!invalid_group_builder.set_middles(std::vector<Middle>{invalid_middle_builder.build()})) {\n"
        "    return 5;\n"
        "  }\n"
        "  const auto invalid_array = encode_result(invalid_group_builder.build());\n"
        "  if (invalid_array.has_value() || invalid_array.error != EncodeError::invalid_utf8) {\n"
        "    return 6;\n"
        "  }\n"
        "  if (invalid_array.byte_offset.has_value()) { return 21; }\n"
        "\n"
        "  const std::vector<std::byte> wrong_nested_record_id{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x13),\n"
        "      byte(0x00), byte(0x00), byte(0x10),\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x09),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "  };\n"
        "  const auto wrong_nested_result = decode_Middle_result(wrong_nested_record_id);\n"
        "  if (wrong_nested_result.error != DecodeError::unexpected_record_id) {\n"
        "    return 7;\n"
        "  }\n"
        "  if (!wrong_nested_result.byte_offset.has_value() ||\n"
        "      *wrong_nested_result.byte_offset != 19U) {\n"
        "    return 22;\n"
        "  }\n"
        "\n"
        "  const std::vector<std::byte> wrong_array_element_record_id{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x15),\n"
        "      byte(0x00), byte(0x00), byte(0x12),\n"
        "      byte(0x01), byte(0x10),\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x09),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "  };\n"
        "  const auto wrong_array_result = decode_Group_result(wrong_array_element_record_id);\n"
        "  if (wrong_array_result.error != DecodeError::unexpected_record_id) {\n"
        "    return 8;\n"
        "  }\n"
        "  if (!wrong_array_result.byte_offset.has_value() ||\n"
        "      *wrong_array_result.byte_offset != 21U) {\n"
        "    return 23;\n"
        "  }\n"
        "  return 0;\n"
        "}\n");
}

TEST(BackendCodegenTest, MultipleEnumsHaveStableOrder) {
    const CodegenResult result = run_backend_fixture("multiple_enums", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(render_result(result), backend_golden_text("multiple_enums"));

    const CodegenResult second_result = run_backend_fixture("multiple_enums", CodegenOptions{});
    ASSERT_TRUE(second_result.success) << second_result.error_message;
    EXPECT_EQ(render_result(result), render_result(second_result));
}

TEST(BackendCodegenTest, VariableLengthFieldsMatchGolden) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("variable_length_fields"));

    const CodegenResult second_result =
        run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(second_result.success) << second_result.error_message;
    EXPECT_EQ(render_result(result), render_result(second_result));
}

TEST(BackendCodegenTest, GeneratedVariableLengthHeaderCompilesAndRuns) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
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

TEST(BackendCodegenTest, GeneratedRecordArrayCodecPreservesPresenceAndElements) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> absent{\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00)};\n"
        "  const auto decoded_absent = decode_Example(absent);\n"
        "  if (!decoded_absent.has_value() || decoded_absent->has_children()) {\n"
        "    return 1;\n"
        "  }\n"
        "\n"
        "  ::ExampleBuilder empty_array_builder;\n"
        "  if (!empty_array_builder.set_children(std::vector<::Child>{})) { return 2; }\n"
        "  const auto empty_array = encode(empty_array_builder.build());\n"
        "  const std::vector<std::byte> expected_empty_array{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x05), byte(0x00), byte(0x01),\n"
        "      byte(0x00)};\n"
        "  if (!empty_array.has_value() || *empty_array != expected_empty_array) { return 3; }\n"
        "  const auto decoded_empty_array = decode_Example(*empty_array);\n"
        "  if (!decoded_empty_array.has_value() || !decoded_empty_array->has_children() ||\n"
        "      !decoded_empty_array->children()->empty()) {\n"
        "    return 4;\n"
        "  }\n"
        "\n"
        "  ::Child empty_child;\n"
        "  ::ChildBuilder populated_child_builder;\n"
        "  if (!populated_child_builder.set_count(0x01020304U)) { return 5; }\n"
        "  const ::Child populated_child = populated_child_builder.build();\n"
        "  ::ExampleBuilder builder;\n"
        "  if (!builder.set_children(std::vector<::Child>{empty_child, populated_child})) {\n"
        "    return 6;\n"
        "  }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x2D),\n"
        "      byte(0x05), byte(0x00), byte(0x2A),\n"
        "      byte(0x02),\n"
        "      byte(0x10),\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x17),\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x07),\n"
        "      byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x01), byte(0x02), byte(0x03), byte(0x04)};\n"
        "  if (!encoded.has_value() || *encoded != expected) { return 7; }\n"
        "  const auto decoded = decode_Example(*encoded);\n"
        "  if (!decoded.has_value() || !decoded->has_children() ||\n"
        "      decoded->children()->size() != 2U || (*decoded->children())[0].has_count() ||\n"
        "      !(*decoded->children())[1].has_count() ||\n"
        "      *(*decoded->children())[1].count() != 0x01020304U) {\n"
        "    return 8;\n"
        "  }\n"
        "  ::ExampleBuilder too_many_builder;\n"
        "  if (too_many_builder.set_children(std::vector<::Child>{\n"
        "          ::Child{}, ::Child{}, ::Child{}, ::Child{}})) {\n"
        "    return 9;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedStringAndBytesEncoderEmitsExactBytes) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <string>\n"
        "#include <string_view>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  ::ExampleBuilder builder;\n"
        "  const std::string name(\"hi\\0\\xC2\\xA2\", 5U);\n"
        "  if (!builder.set_name(name)) {\n"
        "    return 1;\n"
        "  }\n"
        "  if (!builder.set_payload(std::vector<std::byte>{byte(0x00), byte(0xFF), byte(0x80)})) {\n"
        "    return 2;\n"
        "  }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  if (!encoded.has_value()) {\n"
        "    return 3;\n"
        "  }\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x02), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x0E),\n"
        "      byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x01), byte(0x05), byte(0x03),\n"
        "      byte(0x68), byte(0x69), byte(0x00), byte(0xC2), byte(0xA2),\n"
        "      byte(0x00), byte(0xFF), byte(0x80)};\n"
        "  if (*encoded != expected) {\n"
        "    return 4;\n"
        "  }\n"
        "  ::ExampleBuilder invalid_builder;\n"
        "  if (!invalid_builder.set_name(std::string(\"\\xC0\\x80\", 2U))) {\n"
        "    return 5;\n"
        "  }\n"
        "  return encode(invalid_builder.build()).has_value() ? 6 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedStringAndBytesEncoderDistinguishesEmptyPresence) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <string>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  ::ExampleBuilder empty_builder;\n"
        "  const auto empty_encoded = encode(empty_builder.build());\n"
        "  if (!empty_encoded.has_value() || empty_encoded->size() != 16U) {\n"
        "    return 1;\n"
        "  }\n"
        "  ::ExampleBuilder builder;\n"
        "  if (!builder.set_name(std::string{}) || !builder.set_payload(std::vector<std::byte>{})) {\n"
        "    return 2;\n"
        "  }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  if (!encoded.has_value()) {\n"
        "    return 3;\n"
        "  }\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x02), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x06),\n"
        "      byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x01), byte(0x00), byte(0x00)};\n"
        "  if (*encoded != expected) {\n"
        "    return 4;\n"
        "  }\n"
        "  const auto decoded = decode_Example(*encoded);\n"
        "  if (!decoded.has_value() || !decoded->has_name() || !decoded->has_payload()) {\n"
        "    return 5;\n"
        "  }\n"
        "  if (decoded->name() == nullptr || !decoded->name()->empty()) {\n"
        "    return 6;\n"
        "  }\n"
        "  return decoded->payload() == nullptr || !decoded->payload()->empty() ? 7 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedStringAndBytesDecoderReadsManualBytes) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <string>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> input{\n"
        "      byte(0x01), byte(0x00), byte(0x02), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x0E),\n"
        "      byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x01), byte(0x05), byte(0x03),\n"
        "      byte(0x68), byte(0x69), byte(0x00), byte(0xC2), byte(0xA2),\n"
        "      byte(0x00), byte(0xFF), byte(0x80)};\n"
        "  const auto decoded = decode_Example(input);\n"
        "  if (!decoded.has_value() || !decoded->has_name() || !decoded->has_payload()) {\n"
        "    return 1;\n"
        "  }\n"
        "  if (decoded->name() == nullptr || *decoded->name() != std::string(\"hi\\0\\xC2\\xA2\", 5U)) {\n"
        "    return 2;\n"
        "  }\n"
        "  const std::vector<std::byte> expected_payload{byte(0x00), byte(0xFF), byte(0x80)};\n"
        "  return decoded->payload() == nullptr || *decoded->payload() != expected_payload ? 3 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedStringAndBytesDecoderRejectsMalformedWireValues) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> invalid_utf8{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x00), byte(0x00), byte(0x02), byte(0xC0), byte(0x80)};\n"
        "  if (decode_Example(invalid_utf8).has_value()) {\n"
        "    return 1;\n"
        "  }\n"
        "  const std::vector<std::byte> truncated_utf8{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x00), byte(0x00), byte(0x01), byte(0xC2)};\n"
        "  if (decode_Example(truncated_utf8).has_value()) {\n"
        "    return 2;\n"
        "  }\n"
        "  const std::vector<std::byte> over_bound_string{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x14),\n"
        "      byte(0x00), byte(0x00), byte(0x11),\n"
        "      byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61),\n"
        "      byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61),\n"
        "      byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61)};\n"
        "  if (decode_Example(over_bound_string).has_value()) {\n"
        "    return 3;\n"
        "  }\n"
        "  const std::vector<std::byte> over_bound_bytes{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x08),\n"
        "      byte(0x01), byte(0x00), byte(0x05),\n"
        "      byte(0x00), byte(0x01), byte(0x02), byte(0x03), byte(0x04)};\n"
        "  return decode_Example(over_bound_bytes).has_value() ? 4 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedArrayEncoderDistinguishesEmptyPresence) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  ::ExampleBuilder absent_builder;\n"
        "  const auto absent_encoded = encode(absent_builder.build());\n"
        "  if (!absent_encoded.has_value() || absent_encoded->size() != 16U) {\n"
        "    return 1;\n"
        "  }\n"
        "  ::ExampleBuilder builder;\n"
        "  if (!builder.set_counts(std::vector<std::uint32_t>{})) {\n"
        "    return 2;\n"
        "  }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  if (!encoded.has_value()) {\n"
        "    return 3;\n"
        "  }\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x02), byte(0x00), byte(0x01), byte(0x00)};\n"
        "  if (*encoded != expected) {\n"
        "    return 4;\n"
        "  }\n"
        "  const auto decoded = decode_Example(*encoded);\n"
        "  if (!decoded.has_value() || !decoded->has_counts() || decoded->counts() == nullptr ||\n"
        "      !decoded->counts()->empty()) {\n"
        "    return 5;\n"
        "  }\n"
        "  return decode_Example(*absent_encoded)->has_counts() ? 6 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedArrayEncoderEmitsExactBytesForFixedWidthElements) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  ::ExampleBuilder builder;\n"
        "  if (!builder.set_counts(std::vector<std::uint32_t>{0x01020304U, 0xA0B0C0D0U})) {\n"
        "    return 1;\n"
        "  }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  if (!encoded.has_value()) {\n"
        "    return 2;\n"
        "  }\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x0C),\n"
        "      byte(0x02), byte(0x00), byte(0x09),\n"
        "      byte(0x02), byte(0x01), byte(0x02), byte(0x03), byte(0x04),\n"
        "      byte(0xA0), byte(0xB0), byte(0xC0), byte(0xD0)};\n"
        "  if (*encoded != expected) {\n"
        "    return 3;\n"
        "  }\n"
        "  const auto decoded = decode_Example(*encoded);\n"
        "  const std::vector<std::uint32_t> expected_counts{0x01020304U, 0xA0B0C0D0U};\n"
        "  return !decoded.has_value() || decoded->counts() == nullptr ||\n"
        "                 *decoded->counts() != expected_counts\n"
        "             ? 4\n"
        "             : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedStringArrayCodecPreservesPresenceAndElements) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <string>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  ::ExampleBuilder empty_builder;\n"
        "  if (!empty_builder.set_tags(std::vector<std::string>{})) { return 1; }\n"
        "  const auto empty_encoded = encode(empty_builder.build());\n"
        "  const std::vector<std::byte> empty_expected{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x0A), byte(0x00), byte(0x01), byte(0x00)};\n"
        "  if (!empty_encoded.has_value() || *empty_encoded != empty_expected) { return 2; }\n"
        "  ::ExampleBuilder builder;\n"
        "  const std::string nul(\"\\0\", 1U);\n"
        "  const std::vector<std::string> tags{\"A\", \"\", \"\\xE2\\x82\\xAC\" + nul};\n"
        "  if (!builder.set_tags(tags)) { return 3; }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x0C),\n"
        "      byte(0x0A), byte(0x00), byte(0x09),\n"
        "      byte(0x03), byte(0x01), byte(0x41), byte(0x00),\n"
        "      byte(0x04), byte(0xE2), byte(0x82), byte(0xAC), byte(0x00)};\n"
        "  if (!encoded.has_value() || *encoded != expected) { return 4; }\n"
        "  const auto decoded = decode_Example(expected);\n"
        "  if (!decoded.has_value() || decoded->tags() == nullptr || *decoded->tags() != tags) {\n"
        "    return 5;\n"
        "  }\n"
        "  ::ExampleBuilder invalid_builder;\n"
        "  if (!invalid_builder.set_tags(std::vector<std::string>{std::string(\"\\xC0\\x80\", 2U)})) {\n"
        "    return 6;\n"
        "  }\n"
        "  return encode(invalid_builder.build()).has_value() ? 7 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedBytesArrayCodecPreservesArbitraryElements) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  ::ExampleBuilder builder;\n"
        "  const std::vector<std::vector<std::byte>> chunks{\n"
        "      std::vector<std::byte>{},\n"
        "      std::vector<std::byte>{byte(0x00), byte(0xFF), byte(0x80)},\n"
        "      std::vector<std::byte>{byte(0xAA)}};\n"
        "  if (!builder.set_chunks(chunks)) { return 1; }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x0B),\n"
        "      byte(0x0B), byte(0x00), byte(0x08),\n"
        "      byte(0x03), byte(0x00), byte(0x03), byte(0x00), byte(0xFF), byte(0x80),\n"
        "      byte(0x01), byte(0xAA)};\n"
        "  if (!encoded.has_value() || *encoded != expected) { return 2; }\n"
        "  const auto decoded = decode_Example(expected);\n"
        "  return !decoded.has_value() || decoded->chunks() == nullptr || *decoded->chunks() != chunks\n"
        "             ? 3\n"
        "             : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedArrayCodecHandlesMixedFixedWidthArrays) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
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
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  ::ExampleBuilder builder;\n"
        "  if (!builder.set_name(\"ok\")) { return 1; }\n"
        "  if (!builder.set_payload(std::vector<std::byte>{byte(0xFF)})) { return 2; }\n"
        "  if (!builder.set_counts(std::vector<std::uint32_t>{1U})) { return 3; }\n"
        "  if (!builder.set_distances(std::vector<double>{1.5})) { return 4; }\n"
        "  if (!builder.set_modes(std::vector<::Mode>{::Mode::On, ::Mode::Off})) { return 5; }\n"
        "  if (!builder.set_flags(std::vector<bool>{false, true})) { return 6; }\n"
        "  if (!builder.set_deltas(std::vector<std::int16_t>{-2, 0x1234})) { return 7; }\n"
        "  if (!builder.set_ratios(std::vector<float>{1.5F, -2.0F})) { return 8; }\n"
        "  if (!builder.set_active(true)) { return 9; }\n"
        "  if (!builder.set_tags(std::vector<std::string>{\"A\", \"\"})) { return 10; }\n"
        "  if (!builder.set_chunks(std::vector<std::vector<std::byte>>{\n"
        "          std::vector<std::byte>{byte(0x00), byte(0xFF)}, std::vector<std::byte>{}})) {\n"
        "    return 11;\n"
        "  }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  if (!encoded.has_value()) { return 12; }\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x0B), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x50),\n"
        "      byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x01), byte(0x02), byte(0x01),\n"
        "      byte(0x02), byte(0x03), byte(0x05),\n"
        "      byte(0x03), byte(0x08), byte(0x09),\n"
        "      byte(0x04), byte(0x11), byte(0x03),\n"
        "      byte(0x06), byte(0x14), byte(0x03),\n"
        "      byte(0x07), byte(0x17), byte(0x05),\n"
        "      byte(0x08), byte(0x1C), byte(0x09),\n"
        "      byte(0x09), byte(0x25), byte(0x01),\n"
        "      byte(0x0A), byte(0x26), byte(0x04),\n"
        "      byte(0x0B), byte(0x2A), byte(0x05),\n"
        "      byte(0x6F), byte(0x6B), byte(0xFF),\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x01), byte(0x3F), byte(0xF8), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x02), byte(0x01), byte(0x00),\n"
        "      byte(0x02), byte(0x00), byte(0x01),\n"
        "      byte(0x02), byte(0xFF), byte(0xFE), byte(0x12), byte(0x34),\n"
        "      byte(0x02), byte(0x3F), byte(0xC0), byte(0x00), byte(0x00),\n"
        "      byte(0xC0), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x01),\n"
        "      byte(0x02), byte(0x01), byte(0x41), byte(0x00),\n"
        "      byte(0x02), byte(0x02), byte(0x00), byte(0xFF), byte(0x00)};\n"
        "  if (*encoded != expected) { return 13; }\n"
        "  const auto decoded = decode_Example(expected);\n"
        "  if (!decoded.has_value() || decoded->name() == nullptr || *decoded->name() != \"ok\") {\n"
        "    return 14;\n"
        "  }\n"
        "  if (decoded->payload() == nullptr || *decoded->payload() != std::vector<std::byte>{byte(0xFF)}) {\n"
        "    return 15;\n"
        "  }\n"
        "  if (decoded->counts() == nullptr || *decoded->counts() != std::vector<std::uint32_t>{1U}) {\n"
        "    return 16;\n"
        "  }\n"
        "  if (decoded->distances() == nullptr || *decoded->distances() != std::vector<double>{1.5}) {\n"
        "    return 17;\n"
        "  }\n"
        "  if (decoded->modes() == nullptr || *decoded->modes() != std::vector<::Mode>{::Mode::On, ::Mode::Off}) {\n"
        "    return 18;\n"
        "  }\n"
        "  if (decoded->flags() == nullptr || *decoded->flags() != std::vector<bool>{false, true}) {\n"
        "    return 19;\n"
        "  }\n"
        "  if (decoded->deltas() == nullptr || *decoded->deltas() != std::vector<std::int16_t>{-2, 0x1234}) {\n"
        "    return 20;\n"
        "  }\n"
        "  if (decoded->ratios() == nullptr || *decoded->ratios() != std::vector<float>{1.5F, -2.0F}) {\n"
        "    return 21;\n"
        "  }\n"
        "  if (decoded->tags() == nullptr || *decoded->tags() != std::vector<std::string>{\"A\", \"\"}) {\n"
        "    return 22;\n"
        "  }\n"
        "  if (decoded->chunks() == nullptr || *decoded->chunks() !=\n"
        "          std::vector<std::vector<std::byte>>{\n"
        "              std::vector<std::byte>{byte(0x00), byte(0xFF)}, std::vector<std::byte>{}}) {\n"
        "    return 23;\n"
        "  }\n"
        "  return decoded->active() == nullptr || !*decoded->active() ? 24 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedArrayDecoderReportsByteOffsetForCountVaruintFailure) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> missing_count{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x03),\n"
        "      byte(0x02), byte(0x00), byte(0x00)};\n"
        "  const auto decoded = decode_Example_result(missing_count);\n"
        "  if (decoded.value.has_value()) { return 1; }\n"
        "  if (decoded.error != ::quarry::runtime::DecodeError::malformed_varuint) {\n"
        "    return 2;\n"
        "  }\n"
        "  if (decoded.path.size() != 1U ||\n"
        "      static_cast<unsigned int>(decoded.path[0].field_index) != 2U ||\n"
        "      decoded.path[0].array_index.has_value()) {\n"
        "    return 3;\n"
        "  }\n"
        "  if (!decoded.byte_offset.has_value() || *decoded.byte_offset != 19U) {\n"
        "    return 4;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedArrayDecoderRejectsMalformedPayloads) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> missing_count{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x03),\n"
        "      byte(0x02), byte(0x00), byte(0x00)};\n"
        "  if (decode_Example(missing_count).has_value()) { return 1; }\n"
        "  const std::vector<std::byte> count_over_bound{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x02), byte(0x00), byte(0x01), byte(0x05)};\n"
        "  if (decode_Example(count_over_bound).has_value()) { return 2; }\n"
        "  const std::vector<std::byte> truncated_element{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x07),\n"
        "      byte(0x02), byte(0x00), byte(0x04),\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00)};\n"
        "  if (decode_Example(truncated_element).has_value()) { return 3; }\n"
        "  const std::vector<std::byte> trailing_element_byte{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x09),\n"
        "      byte(0x02), byte(0x00), byte(0x06),\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00), byte(0x01), byte(0xFF)};\n"
        "  if (decode_Example(trailing_element_byte).has_value()) { return 4; }\n"
        "  const std::vector<std::byte> invalid_bool{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x06), byte(0x00), byte(0x02), byte(0x01), byte(0x02)};\n"
        "  if (decode_Example(invalid_bool).has_value()) { return 5; }\n"
        "  const std::vector<std::byte> unknown_enum{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x04), byte(0x00), byte(0x02), byte(0x01), byte(0x07)};\n"
        "  return decode_Example(unknown_enum).has_value() ? 6 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedVariableLengthArrayDecoderRejectsMalformedPayloads) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> missing_string_count{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x03),\n"
        "      byte(0x0A), byte(0x00), byte(0x00)};\n"
        "  if (decode_Example(missing_string_count).has_value()) { return 1; }\n"
        "  const std::vector<std::byte> truncated_string_length{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x0A), byte(0x00), byte(0x02), byte(0x01), byte(0x80)};\n"
        "  if (decode_Example(truncated_string_length).has_value()) { return 2; }\n"
        "  const std::vector<std::byte> string_count_over_bound{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x0A), byte(0x00), byte(0x01), byte(0x04)};\n"
        "  if (decode_Example(string_count_over_bound).has_value()) { return 3; }\n"
        "  const std::vector<std::byte> string_element_over_bound{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x0E),\n"
        "      byte(0x0A), byte(0x00), byte(0x0B),\n"
        "      byte(0x01), byte(0x09),\n"
        "      byte(0x61), byte(0x61), byte(0x61), byte(0x61), byte(0x61),\n"
        "      byte(0x61), byte(0x61), byte(0x61), byte(0x61)};\n"
        "  if (decode_Example(string_element_over_bound).has_value()) { return 4; }\n"
        "  const std::vector<std::byte> invalid_utf8_later_element{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x09),\n"
        "      byte(0x0A), byte(0x00), byte(0x06),\n"
        "      byte(0x02), byte(0x01), byte(0x41), byte(0x02), byte(0xC0), byte(0x80)};\n"
        "  if (decode_Example(invalid_utf8_later_element).has_value()) { return 5; }\n"
        "  const std::vector<std::byte> truncated_string_element{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x06),\n"
        "      byte(0x0A), byte(0x00), byte(0x03), byte(0x01), byte(0x02), byte(0x41)};\n"
        "  if (decode_Example(truncated_string_element).has_value()) { return 6; }\n"
        "  const std::vector<std::byte> trailing_string_byte{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x0A), byte(0x00), byte(0x02), byte(0x00), byte(0xFF)};\n"
        "  if (decode_Example(trailing_string_byte).has_value()) { return 7; }\n"
        "  const std::vector<std::byte> missing_bytes_count{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x03),\n"
        "      byte(0x0B), byte(0x00), byte(0x00)};\n"
        "  if (decode_Example(missing_bytes_count).has_value()) { return 8; }\n"
        "  const std::vector<std::byte> truncated_bytes_length{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x0B), byte(0x00), byte(0x02), byte(0x01), byte(0x80)};\n"
        "  if (decode_Example(truncated_bytes_length).has_value()) { return 9; }\n"
        "  const std::vector<std::byte> bytes_element_over_bound{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x08),\n"
        "      byte(0x0B), byte(0x00), byte(0x05),\n"
        "      byte(0x01), byte(0x04), byte(0x00), byte(0x01), byte(0x02), byte(0x03)};\n"
        "  if (decode_Example(bytes_element_over_bound).has_value()) { return 10; }\n"
        "  const std::vector<std::byte> truncated_bytes_element{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x06),\n"
        "      byte(0x0B), byte(0x00), byte(0x03), byte(0x01), byte(0x02), byte(0xAA)};\n"
        "  if (decode_Example(truncated_bytes_element).has_value()) { return 11; }\n"
        "  const std::vector<std::byte> trailing_bytes_byte{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x05),\n"
        "      byte(0x0B), byte(0x00), byte(0x02), byte(0x00), byte(0xFF)};\n"
        "  return decode_Example(trailing_bytes_byte).has_value() ? 12 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedEnumEncoderEmitsExactBytesAndRejectsUnknownValue) {
    const CodegenResult result = run_backend_fixture("enum_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  ::PaintBuilder builder;\n"
        "  if (!builder.set_color(::Color::Green)) {\n"
        "    return 1;\n"
        "  }\n"
        "  const auto encoded = encode(builder.build());\n"
        "  if (!encoded.has_value()) {\n"
        "    return 2;\n"
        "  }\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x00), byte(0x00), byte(0x01), byte(0x02)};\n"
        "  if (*encoded != expected) {\n"
        "    return 3;\n"
        "  }\n"
        "  ::PaintBuilder unknown_builder;\n"
        "  if (!unknown_builder.set_color(static_cast<::Color>(7))) {\n"
        "    return 4;\n"
        "  }\n"
        "  return encode(unknown_builder.build()).has_value() ? 5 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedEnumDecoderReadsKnownValueAndRejectsUnknownValue) {
    const CodegenResult result = run_backend_fixture("enum_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const std::vector<std::byte> known{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x00), byte(0x00), byte(0x01), byte(0x02)};\n"
        "  const auto decoded = decode_Paint(known);\n"
        "  if (!decoded.has_value() || !decoded->has_color() || decoded->color() == nullptr ||\n"
        "      *decoded->color() != ::Color::Green) {\n"
        "    return 1;\n"
        "  }\n"
        "  const std::vector<std::byte> unknown{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x04),\n"
        "      byte(0x00), byte(0x00), byte(0x01), byte(0x07)};\n"
        "  return decode_Paint(unknown).has_value() ? 2 : 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, CrossNamespaceArrayReferenceMatchesGolden) {
    const CodegenResult result =
        run_backend_fixture("cross_namespace_array_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 2u);
    EXPECT_EQ(result.files[0].path, "generated/alpha/one.generated.hpp");
    EXPECT_EQ(result.files[1].path, "generated/beta/two.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("cross_namespace_array_reference"));
}

TEST(BackendCodegenTest, CrossNamespaceRecordArrayCodecCompilesAndRoundTrips) {
    const CodegenResult result =
        run_backend_fixture("cross_namespace_array_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    compile_generated_header(
        result, "generated/beta/two.generated.hpp",
        "#include \"generated/beta/two.generated.hpp\"\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <vector>\n"
        "\n"
        "int main() {\n"
        "  ::alpha::one::ElementBuilder element_builder;\n"
        "  if (!element_builder.set_id(42U)) { return 1; }\n"
        "  ::beta::two::BasketBuilder basket_builder;\n"
        "  if (!basket_builder.set_elements(std::vector<::alpha::one::Element>{\n"
        "          ::alpha::one::Element{}, element_builder.build()})) {\n"
        "    return 2;\n"
        "  }\n"
        "  const auto encoded = ::beta::two::encode(basket_builder.build());\n"
        "  if (!encoded.has_value()) { return 3; }\n"
        "  const auto decoded = ::beta::two::decode_Basket(*encoded);\n"
        "  if (!decoded.has_value() || !decoded->has_elements() ||\n"
        "      decoded->elements()->size() != 2U || (*decoded->elements())[0].has_id() ||\n"
        "      !(*decoded->elements())[1].has_id() || *(*decoded->elements())[1].id() != 42U) {\n"
        "    return 4;\n"
        "  }\n"
        "  return 0;\n"
        "}\n");
}

TEST(BackendCodegenTest, GeneratedRecordArrayDecoderRejectsMalformedPayloads) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const auto append_u32 = [&](std::vector<std::byte>& out, unsigned int value) {\n"
        "    out.push_back(byte((value >> 24U) & 0xFFU));\n"
        "    out.push_back(byte((value >> 16U) & 0xFFU));\n"
        "    out.push_back(byte((value >> 8U) & 0xFFU));\n"
        "    out.push_back(byte(value & 0xFFU));\n"
        "  };\n"
        "  const auto make_parent = [&](std::vector<std::byte> payload) {\n"
        "    std::vector<std::byte> out{byte(0x01), byte(0x00), byte(0x01), byte(0x00)};\n"
        "    append_u32(out, 2U);\n"
        "    append_u32(out, 0U);\n"
        "    append_u32(out, static_cast<unsigned int>(3U + payload.size()));\n"
        "    out.push_back(byte(0x05));\n"
        "    out.push_back(byte(0x00));\n"
        "    out.push_back(byte(static_cast<unsigned int>(payload.size())));\n"
        "    out.insert(out.end(), payload.begin(), payload.end());\n"
        "    return out;\n"
        "  };\n"
        "  const auto child_empty = [&](unsigned int version, unsigned int flags,\n"
        "                               unsigned int record_id, unsigned int payload_length) {\n"
        "    std::vector<std::byte> out{byte(version), byte(flags), byte(0x00), byte(0x00)};\n"
        "    append_u32(out, record_id);\n"
        "    append_u32(out, 0U);\n"
        "    append_u32(out, payload_length);\n"
        "    return out;\n"
        "  };\n"
        "  const std::vector<std::vector<std::byte>> invalid{\n"
        "      make_parent({}),\n"
        "      make_parent({byte(0x80)}),\n"
        "      make_parent({byte(0x04)}),\n"
        "      make_parent({byte(0x01)}),\n"
        "      make_parent({byte(0x01), byte(0x80)}),\n"
        "      make_parent({byte(0x01), byte(0x10)}),\n"
        "      make_parent({byte(0x00), byte(0xFF)}),\n"
        "      make_parent([&] { auto payload = std::vector<std::byte>{byte(0x01), byte(0x10)};\n"
        "                       auto child = child_empty(1U, 0U, 9U, 0U);\n"
        "                       payload.insert(payload.end(), child.begin(), child.end());\n"
        "                       return payload; }()),\n"
        "      make_parent([&] { auto payload = std::vector<std::byte>{byte(0x01), byte(0x10)};\n"
        "                       auto child = child_empty(2U, 0U, 1U, 0U);\n"
        "                       payload.insert(payload.end(), child.begin(), child.end());\n"
        "                       return payload; }()),\n"
        "      make_parent([&] { auto payload = std::vector<std::byte>{byte(0x01), byte(0x10)};\n"
        "                       auto child = child_empty(1U, 1U, 1U, 0U);\n"
        "                       payload.insert(payload.end(), child.begin(), child.end());\n"
        "                       return payload; }()),\n"
        "      make_parent([&] { auto payload = std::vector<std::byte>{byte(0x01), byte(0x11)};\n"
        "                       auto child = child_empty(1U, 0U, 1U, 0U);\n"
        "                       payload.insert(payload.end(), child.begin(), child.end());\n"
        "                       payload.push_back(byte(0xFF));\n"
        "                       return payload; }()),\n"
        "      make_parent([&] { auto payload = std::vector<std::byte>{byte(0x01), byte(0x14)};\n"
        "                       auto child = std::vector<std::byte>{\n"
        "                           byte(0x01), byte(0x00), byte(0x01), byte(0x00)};\n"
        "                       append_u32(child, 1U);\n"
        "                       append_u32(child, 0U);\n"
        "                       append_u32(child, 7U);\n"
        "                       child.push_back(byte(0x00));\n"
        "                       child.push_back(byte(0x00));\n"
        "                       child.push_back(byte(0x04));\n"
        "                       child.push_back(byte(0x01));\n"
        "                       payload.insert(payload.end(), child.begin(), child.end());\n"
        "                       return payload; }()),\n"
        "  };\n"
        "  for (const auto& bytes : invalid) {\n"
        "    if (decode_Example(bytes).has_value()) {\n"
        "      return 1;\n"
        "    }\n"
        "  }\n"
        "  auto unknown_child_payload = std::vector<std::byte>{byte(0x01), byte(0x1B)};\n"
        "  auto unknown_child = std::vector<std::byte>{\n"
        "      byte(0x01), byte(0x00), byte(0x02), byte(0x00)};\n"
        "  append_u32(unknown_child, 1U);\n"
        "  append_u32(unknown_child, 0U);\n"
        "  append_u32(unknown_child, 11U);\n"
        "  unknown_child.insert(unknown_child.end(), {byte(0x00), byte(0x00), byte(0x04),\n"
        "                                             byte(0x03), byte(0x04), byte(0x01),\n"
        "                                             byte(0x01), byte(0x02), byte(0x03),\n"
        "                                             byte(0x04), byte(0xFF)});\n"
        "  unknown_child_payload.insert(unknown_child_payload.end(), unknown_child.begin(),\n"
        "                               unknown_child.end());\n"
        "  const auto decoded = decode_Example(make_parent(unknown_child_payload));\n"
        "  if (!decoded.has_value() || !decoded->has_children() ||\n"
        "      decoded->children()->size() != 1U || !(*decoded->children())[0].has_count() ||\n"
        "      *(*decoded->children())[0].count() != 0x01020304U) {\n"
        "    return 2;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedRecordArrayDecoderPropagatesNestedFieldAndArrayIndexPath) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);

    const std::string header_include =
        generated_include_path(CodegenOptions{}.output_directory, result.files.front().path);
    const std::string translation_source =
        "#include \"" + header_include +
        "\"\n"
        "#include <cstddef>\n"
        "#include <vector>\n"
        "int main() {\n"
        "  const auto byte = [](unsigned int value) {\n"
        "    return static_cast<std::byte>(static_cast<unsigned char>(value));\n"
        "  };\n"
        "  const auto append_u32 = [&](std::vector<std::byte>& out, unsigned int value) {\n"
        "    out.push_back(byte((value >> 24U) & 0xFFU));\n"
        "    out.push_back(byte((value >> 16U) & 0xFFU));\n"
        "    out.push_back(byte((value >> 8U) & 0xFFU));\n"
        "    out.push_back(byte(value & 0xFFU));\n"
        "  };\n"
        "  const auto make_child = [&](unsigned int count_field_length,\n"
        "                              std::vector<std::byte> count_payload) {\n"
        "    std::vector<std::byte> out{byte(0x01), byte(0x00), byte(0x01), byte(0x00)};\n"
        "    append_u32(out, 1U);\n"
        "    append_u32(out, 0U);\n"
        "    append_u32(out, static_cast<unsigned int>(3U + count_payload.size()));\n"
        "    out.push_back(byte(0x00));\n"
        "    out.push_back(byte(0x00));\n"
        "    out.push_back(byte(count_field_length));\n"
        "    out.insert(out.end(), count_payload.begin(), count_payload.end());\n"
        "    return out;\n"
        "  };\n"
        "  const auto valid_child =\n"
        "      make_child(4U, {byte(0x00), byte(0x00), byte(0x00), byte(0x07)});\n"
        "  const auto malformed_child = make_child(3U, {byte(0xAA), byte(0xBB), byte(0xCC)});\n"
        "  std::vector<std::byte> children_payload;\n"
        "  children_payload.push_back(byte(0x02));\n"
        "  children_payload.push_back(byte(static_cast<unsigned int>(valid_child.size())));\n"
        "  children_payload.insert(children_payload.end(), valid_child.begin(),\n"
        "                          valid_child.end());\n"
        "  children_payload.push_back(byte(static_cast<unsigned int>(malformed_child.size())));\n"
        "  children_payload.insert(children_payload.end(), malformed_child.begin(),\n"
        "                          malformed_child.end());\n"
        "  std::vector<std::byte> parent{byte(0x01), byte(0x00), byte(0x01), byte(0x00)};\n"
        "  append_u32(parent, 2U);\n"
        "  append_u32(parent, 0U);\n"
        "  append_u32(parent, static_cast<unsigned int>(3U + children_payload.size()));\n"
        "  parent.push_back(byte(0x05));\n"
        "  parent.push_back(byte(0x00));\n"
        "  parent.push_back(byte(static_cast<unsigned int>(children_payload.size())));\n"
        "  parent.insert(parent.end(), children_payload.begin(), children_payload.end());\n"
        "  const auto decoded = decode_Example_result(parent);\n"
        "  if (decoded.value.has_value()) { return 1; }\n"
        "  if (decoded.error != ::quarry::runtime::DecodeError::invalid_field_length) {\n"
        "    return 2;\n"
        "  }\n"
        "  if (decoded.path.size() != 2U) { return 3; }\n"
        "  if (static_cast<unsigned int>(decoded.path[0].field_index) != 0U ||\n"
        "      decoded.path[0].array_index.has_value()) {\n"
        "    return 4;\n"
        "  }\n"
        "  if (static_cast<unsigned int>(decoded.path[1].field_index) != 5U) { return 5; }\n"
        "  if (!decoded.path[1].array_index.has_value() ||\n"
        "      *decoded.path[1].array_index != 1U) {\n"
        "    return 6;\n"
        "  }\n"
        "  if (!decoded.byte_offset.has_value() || *decoded.byte_offset != 64U) {\n"
        "    return 7;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedRecordArrayEncoderPropagatesNestedFieldAndArrayIndexPath) {
    const CodegenResult result =
        Backend{}.generate(make_manual_record_array_enum_schema_ir(), CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    ASSERT_EQ(result.files.front().path, "generated/schema.generated.hpp");

    const std::string translation_source =
        "#include \"schema.generated.hpp\"\n"
        "#include <vector>\n"
        "int main() {\n"
        "  ::ElementBuilder element_builder;\n"
        "  if (!element_builder.set_mode(static_cast<::Mode>(99))) { return 1; }\n"
        "  ::BasketBuilder basket_builder;\n"
        "  if (!basket_builder.set_elements(std::vector<::Element>{\n"
        "          element_builder.build(), element_builder.build()})) {\n"
        "    return 2;\n"
        "  }\n"
        "  const auto encoded = encode_result(basket_builder.build());\n"
        "  if (encoded.value.has_value()) { return 3; }\n"
        "  if (encoded.error != ::quarry::runtime::EncodeError::unknown_enum_value) {\n"
        "    return 4;\n"
        "  }\n"
        "  if (encoded.path.size() != 2U) { return 5; }\n"
        "  if (static_cast<unsigned int>(encoded.path[0].field_index) != 0U ||\n"
        "      encoded.path[0].array_index.has_value()) {\n"
        "    return 6;\n"
        "  }\n"
        "  if (static_cast<unsigned int>(encoded.path[1].field_index) != 0U) { return 7; }\n"
        "  if (!encoded.path[1].array_index.has_value() ||\n"
        "      *encoded.path[1].array_index != 0U) {\n"
        "    return 8;\n"
        "  }\n"
        "  if (encoded.byte_offset.has_value()) { return 9; }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

TEST(BackendCodegenTest, GeneratedBuilderBoundsHeaderCompilesAndRuns) {
    const CodegenResult result = run_backend_fixture("variable_length_fields", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    ASSERT_EQ(result.files[0].path, "generated/schema.generated.hpp");

    const std::string translation_source =
        "#include \"schema.generated.hpp\"\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <string>\n"
        "#include <type_traits>\n"
        "#include <vector>\n"
        "int main() {\n"
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
        "  if (!builder.set_flags(std::vector<bool>{false, true})) {\n"
        "    return 7;\n"
        "  }\n"
        "  if (!builder.set_deltas(std::vector<std::int16_t>{-1, 2})) {\n"
        "    return 8;\n"
        "  }\n"
        "  if (!builder.set_ratios(std::vector<float>{1.0F, 2.0F})) {\n"
        "    return 9;\n"
        "  }\n"
        "  if (!builder.set_active(true)) {\n"
        "    return 10;\n"
        "  }\n"
        "  const auto value = builder.build();\n"
        "  if (!value.has_name() || !value.has_payload() || !value.has_counts() ||\n"
        "      !value.has_flags() || !value.has_deltas() || !value.has_ratios() ||\n"
        "      !value.has_active()) {\n"
        "    return 11;\n"
        "  }\n"
        "  if (value.name() == nullptr || *value.name() != \"example\") {\n"
        "    return 12;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";

    compile_generated_header(result, "generated/schema.generated.hpp", translation_source);
}

} // namespace
