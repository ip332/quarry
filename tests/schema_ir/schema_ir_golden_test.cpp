#include "compiler/context/compiler_context.hpp"
#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/frontend/yaml_compiler.hpp"
#include "compiler/schema_ir/schema_ir.hpp"
#include "compiler/support/source_manager.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <google/protobuf/text_format.h>

#include <gtest/gtest.h>

namespace {

using quarry::compiler::context::CompilerContext;
using quarry::compiler::diagnostics::DiagnosticCollection;
using quarry::compiler::diagnostics::DiagnosticEngine;
using quarry::compiler::frontend::YamlCompilationResult;
using quarry::compiler::frontend::YamlCompiler;
using quarry::compiler::schema_ir::SchemaIrModel;
using quarry::compiler::support::SourceFileId;

[[nodiscard]] std::filesystem::path fixtures_root() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" /
           "schema_ir_yaml";
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

[[nodiscard]] std::string diagnostics_summary(const DiagnosticCollection& diagnostics) {
    std::ostringstream stream;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        stream << diagnostic.compiler_pass() << ": " << diagnostic.id().str() << ": "
               << diagnostic.message() << '\n';
    }
    return stream.str();
}

struct GoldenOutput {
    CompilerContext context;
    DiagnosticCollection diagnostics;
    YamlCompilationResult result;
    SourceFileId source_file_id;
};

[[nodiscard]] GoldenOutput compile_yaml_fixture(std::string_view fixture_name) {
    GoldenOutput output;
    std::string text = read_file(fixtures_root() / (std::string(fixture_name) + ".brd"));
    trim_trailing_newlines(text);
    output.source_file_id =
        output.context.source_manager().add_source("/test/schema.yaml", std::move(text));
    YamlCompiler compiler;
    output.result = compiler.compile(output.source_file_id, output.context, output.diagnostics);
    return output;
}

void clear_source_metadata(::quarry::schema_ir::NamespaceIR* namespace_ir) {
    if (namespace_ir == nullptr) {
        return;
    }

    namespace_ir->clear_source_origin();
    for (int index = 0; index < namespace_ir->namespaces_size(); ++index) {
        clear_source_metadata(namespace_ir->mutable_namespaces(index));
    }
    for (int index = 0; index < namespace_ir->records_size(); ++index) {
        ::quarry::schema_ir::RecordIR* record = namespace_ir->mutable_records(index);
        record->clear_source_origin();
        for (int field_index = 0; field_index < record->fields_size(); ++field_index) {
            record->mutable_fields(field_index)->clear_source_origin();
        }
    }
    for (int index = 0; index < namespace_ir->enums_size(); ++index) {
        ::quarry::schema_ir::EnumIR* enum_ir = namespace_ir->mutable_enums(index);
        enum_ir->clear_source_origin();
        for (int value_index = 0; value_index < enum_ir->values_size(); ++value_index) {
            enum_ir->mutable_values(value_index)->clear_source_origin();
        }
    }
}

[[nodiscard]] std::string render_normalized_pbtxt(SchemaIrModel schema_ir) {
    clear_source_metadata(schema_ir.mutable_root_namespace());

    google::protobuf::TextFormat::Printer printer;
    printer.SetSingleLineMode(false);
    printer.SetPrintMessageFieldsInIndexOrder(true);

    std::string output;
    EXPECT_TRUE(printer.PrintToString(schema_ir, &output));
    trim_trailing_newlines(output);
    return output;
}

[[nodiscard]] std::string golden_text(std::string_view fixture_name) {
    std::string text =
        read_file(fixtures_root() / (std::string(fixture_name) + ".pbtxt"));
    trim_trailing_newlines(text);
    return text;
}

void expect_yaml_fixture_matches_golden(std::string_view fixture_name) {
    const GoldenOutput output = compile_yaml_fixture(fixture_name);

    ASSERT_TRUE(output.result.succeeded()) << diagnostics_summary(output.diagnostics);
    ASSERT_TRUE(output.diagnostics.empty()) << diagnostics_summary(output.diagnostics);
    ASSERT_TRUE(output.result.schema_ir.has_value());

    const std::string actual = render_normalized_pbtxt(*output.result.schema_ir);
    const std::string expected = golden_text(fixture_name);
    EXPECT_EQ(actual, expected) << "fixture: " << fixture_name;
}

TEST(SchemaIrGoldenTest, EmptyMatchesGolden) { expect_yaml_fixture_matches_golden("empty"); }

TEST(SchemaIrGoldenTest, SingleRecordMatchesGolden) {
    expect_yaml_fixture_matches_golden("single_record");
}

TEST(SchemaIrGoldenTest, BuiltinFieldsMatchesGolden) {
    expect_yaml_fixture_matches_golden("builtin_fields");
}

TEST(SchemaIrGoldenTest, NestedNamespaceMatchesGolden) {
    expect_yaml_fixture_matches_golden("nested_namespace");
}

TEST(SchemaIrGoldenTest, NamedTypeReferenceMatchesGolden) {
    expect_yaml_fixture_matches_golden("named_type_reference");
}

TEST(SchemaIrGoldenTest, EnumMatchesGolden) { expect_yaml_fixture_matches_golden("enum"); }

} // namespace
