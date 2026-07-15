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

namespace {

using breadcrumbs::compiler::backend::Backend;
using breadcrumbs::compiler::backend::CodegenOptions;
using breadcrumbs::compiler::backend::CodegenResult;
using breadcrumbs::compiler::schema_ir::SchemaIrModel;
using breadcrumbs::compiler::schema_ir::SchemaIrValidator;

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
diagnostics_summary(const breadcrumbs::compiler::diagnostics::DiagnosticCollection& diagnostics) {
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

    breadcrumbs::compiler::context::CompilerContext context;
    breadcrumbs::compiler::diagnostics::DiagnosticCollection diagnostics;
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
    const std::filesystem::path repo_root =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::ostringstream command;
    command << std::quoted(compiler) << " -std=c++20 -I" << std::quoted(generated_root.string())
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
    EXPECT_EQ(result.files.front().path, "generated/breadcrumbs/geo.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("named_type_reference"));
}

TEST(BackendCodegenTest, SameFileForwardReferenceOrdersDefinitions) {
    const CodegenResult result = run_backend_fixture("forward_record_reference", CodegenOptions{});
    ASSERT_TRUE(result.success) << result.error_message;
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files.front().path, "generated/schema.generated.hpp");
    EXPECT_EQ(render_result(result), backend_golden_text("forward_record_reference"));
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
        "  const auto encoded = encode(builder.build());\n"
        "  if (!encoded.has_value()) { return 10; }\n"
        "  const std::vector<std::byte> expected{\n"
        "      byte(0x01), byte(0x00), byte(0x09), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x41),\n"
        "      byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x01), byte(0x02), byte(0x01),\n"
        "      byte(0x02), byte(0x03), byte(0x05),\n"
        "      byte(0x03), byte(0x08), byte(0x09),\n"
        "      byte(0x04), byte(0x11), byte(0x03),\n"
        "      byte(0x06), byte(0x14), byte(0x03),\n"
        "      byte(0x07), byte(0x17), byte(0x05),\n"
        "      byte(0x08), byte(0x1C), byte(0x09),\n"
        "      byte(0x09), byte(0x25), byte(0x01),\n"
        "      byte(0x6F), byte(0x6B), byte(0xFF),\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00), byte(0x01),\n"
        "      byte(0x01), byte(0x3F), byte(0xF8), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x02), byte(0x01), byte(0x00),\n"
        "      byte(0x02), byte(0x00), byte(0x01),\n"
        "      byte(0x02), byte(0xFF), byte(0xFE), byte(0x12), byte(0x34),\n"
        "      byte(0x02), byte(0x3F), byte(0xC0), byte(0x00), byte(0x00),\n"
        "      byte(0xC0), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x01)};\n"
        "  if (*encoded != expected) { return 11; }\n"
        "  const auto decoded = decode_Example(expected);\n"
        "  if (!decoded.has_value() || decoded->name() == nullptr || *decoded->name() != \"ok\") {\n"
        "    return 12;\n"
        "  }\n"
        "  if (decoded->payload() == nullptr || *decoded->payload() != std::vector<std::byte>{byte(0xFF)}) {\n"
        "    return 13;\n"
        "  }\n"
        "  if (decoded->counts() == nullptr || *decoded->counts() != std::vector<std::uint32_t>{1U}) {\n"
        "    return 14;\n"
        "  }\n"
        "  if (decoded->distances() == nullptr || *decoded->distances() != std::vector<double>{1.5}) {\n"
        "    return 15;\n"
        "  }\n"
        "  if (decoded->modes() == nullptr || *decoded->modes() != std::vector<::Mode>{::Mode::On, ::Mode::Off}) {\n"
        "    return 16;\n"
        "  }\n"
        "  if (decoded->flags() == nullptr || *decoded->flags() != std::vector<bool>{false, true}) {\n"
        "    return 17;\n"
        "  }\n"
        "  if (decoded->deltas() == nullptr || *decoded->deltas() != std::vector<std::int16_t>{-2, 0x1234}) {\n"
        "    return 18;\n"
        "  }\n"
        "  if (decoded->ratios() == nullptr || *decoded->ratios() != std::vector<float>{1.5F, -2.0F}) {\n"
        "    return 19;\n"
        "  }\n"
        "  return decoded->active() == nullptr || !*decoded->active() ? 20 : 0;\n"
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

TEST(BackendCodegenTest, GeneratedDecoderRejectsPresentUnsupportedKnownField) {
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
        "  const std::vector<std::byte> absent_unsupported{\n"
        "      byte(0x01), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00)};\n"
        "  const auto decoded = decode_Example(absent_unsupported);\n"
        "  if (!decoded.has_value() || decoded->has_name() || decoded->has_payload() ||\n"
        "      decoded->has_counts() || decoded->has_distances() || decoded->has_modes() ||\n"
        "      decoded->has_children() || decoded->has_flags() || decoded->has_deltas() ||\n"
        "      decoded->has_ratios() || decoded->has_active()) {\n"
        "    return 1;\n"
        "  }\n"
        "  const std::vector<std::byte> present_unsupported{\n"
        "      byte(0x01), byte(0x00), byte(0x01), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x02),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x00),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x07),\n"
        "      byte(0x05), byte(0x00), byte(0x04),\n"
        "      byte(0x00), byte(0x00), byte(0x00), byte(0x01)};\n"
        "  return decode_Example(present_unsupported).has_value() ? 2 : 0;\n"
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
