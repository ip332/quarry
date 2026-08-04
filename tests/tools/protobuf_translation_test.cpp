#include "translation.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using quarry::tools::protobuf::DescriptorField;
using quarry::tools::protobuf::DescriptorEnum;
using quarry::tools::protobuf::DescriptorEnumValue;
using quarry::tools::protobuf::DescriptorFile;
using quarry::tools::protobuf::DescriptorMessage;
using quarry::tools::protobuf::DescriptorModel;
using quarry::tools::protobuf::FieldLabel;
using quarry::tools::protobuf::FieldType;

[[nodiscard]] std::filesystem::path temporary_directory(std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("quarry-protobuf-translation-" + std::string(name));
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path, error);
    EXPECT_FALSE(error);
    return path;
}

void write_text(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    ASSERT_TRUE(output);
    output << contents;
    ASSERT_TRUE(output.good());
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

[[nodiscard]] bool contains_filename(const std::filesystem::path& directory,
                                     std::string_view filename) {
    if (!std::filesystem::exists(directory)) return false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().filename() == filename) return true;
    }
    return false;
}

[[nodiscard]] DescriptorField field(std::string name, FieldType type, std::string type_name = {},
                                    FieldLabel label = FieldLabel::Optional) {
    DescriptorField result;
    result.name = std::move(name);
    result.type = type;
    result.type_name = std::move(type_name);
    result.label = label;
    return result;
}

[[nodiscard]] DescriptorModel representative_model() {
    DescriptorModel model;
    DescriptorMessage child;
    child.name = "Child";
    child.fully_qualified_name = "telemetry.Child";
    child.file_name = "child.proto";
    child.fields.push_back(field("label", FieldType::String));

    DescriptorMessage sample;
    sample.name = "Sample";
    sample.fully_qualified_name = "telemetry.Sample";
    sample.file_name = "sample.proto";
    sample.fields.push_back(field("count", FieldType::Int32));
    sample.fields.push_back(field("name", FieldType::String));
    sample.fields.push_back(field("payload", FieldType::Bytes));
    sample.fields.push_back(field("values", FieldType::Int32, {}, FieldLabel::Repeated));
    sample.fields.push_back(
        field("child", FieldType::Message, "telemetry.Child"));
    sample.fields.push_back(
        field("children", FieldType::Message, "telemetry.Child", FieldLabel::Repeated));

    model.messages = {std::move(child), std::move(sample)};
    DescriptorFile child_file;
    child_file.name = "child.proto";
    child_file.messages = {"telemetry.Child"};
    DescriptorFile sample_file;
    sample_file.name = "sample.proto";
    sample_file.messages = {"telemetry.Sample"};
    model.files = {std::move(child_file), std::move(sample_file)};
    return model;
}

} // namespace

TEST(ProtobufTranslationTest, EmitsBoundedReachableBundleAndManifest) {
    const std::filesystem::path root = temporary_directory("bundle");
    const std::filesystem::path bounds = root / "bounds.yaml";
    write_text(bounds,
               "bounds:\n"
               "  telemetry.Sample.name:\n"
               "    max_bytes: 64\n"
               "  telemetry.Sample.payload:\n"
               "    max_bytes: 256\n"
               "  telemetry.Sample.values:\n"
               "    max_elements: 4\n"
               "  telemetry.Sample.children:\n"
               "    max_elements: 2\n"
               "  telemetry.Child.label:\n"
               "    max_bytes: 32\n");

    const auto result = quarry::tools::protobuf::translate_descriptor_model(
        representative_model(), "telemetry.Sample", bounds.string(), (root / "generated").string());
    ASSERT_TRUE(result.succeeded())
        << (result.diagnostics.empty() ? "" : result.diagnostics.front());

    const std::filesystem::path output = root / "generated";
    const std::filesystem::path sample = output / "telemetry/sample/sample.brd";
    const std::filesystem::path child = output / "telemetry/child/child.brd";
    ASSERT_TRUE(std::filesystem::exists(sample));
    ASSERT_TRUE(std::filesystem::exists(child));
    EXPECT_EQ(read_text(sample),
              "namespace: telemetry.sample\n"
              "record: Sample\n"
              "version: 1\n"
              "type: data\n"
              "imports:\n"
              "  - ../child/child.brd\n"
              "fields:\n"
              "  count:\n"
              "    type: i32\n"
              "  name:\n"
              "    type: string\n"
              "    max_bytes: 64\n"
              "  payload:\n"
              "    type: bytes\n"
              "    max_bytes: 256\n"
              "  values:\n"
              "    type: i32[]\n"
              "    max_elements: 4\n"
              "  child:\n"
              "    type: telemetry.child.Child\n"
              "  children:\n"
              "    type: telemetry.child.Child[]\n"
              "    max_elements: 2\n");
    EXPECT_NE(read_text(child).find("max_bytes: 32"), std::string::npos);
    const std::string manifest = read_text(output / "manifest.json");
    EXPECT_NE(manifest.find("\"protobuf_root\": \"telemetry.Sample\""), std::string::npos);
    EXPECT_NE(manifest.find("\"explicit_roots\""), std::string::npos);
    EXPECT_NE(manifest.find("telemetry/sample/sample.brd"), std::string::npos);
}

TEST(ProtobufTranslationTest, EmitsOwnedEnumsAndMigrationMetadata) {
    const std::filesystem::path root = temporary_directory("enum");
    DescriptorModel model = representative_model();
    DescriptorEnum status;
    status.name = "Status";
    status.fully_qualified_name = "telemetry.Status";
    status.file_name = "status.proto";
    status.values = {DescriptorEnumValue{"UNKNOWN", 0}, DescriptorEnumValue{"READY", 1}};
    model.enums.push_back(status);
    DescriptorEnum mode;
    mode.name = "Mode";
    mode.fully_qualified_name = "telemetry.Sample.Mode";
    mode.file_name = "sample.proto";
    mode.containing_message = "telemetry.Sample";
    mode.values = {DescriptorEnumValue{"IDLE", 0}, DescriptorEnumValue{"ACTIVE", 1}};
    model.enums.push_back(mode);
    model.messages[1].fields.push_back(field("status", FieldType::Enum, "telemetry.Status"));
    model.messages[1].fields.push_back(
        field("mode", FieldType::Enum, "telemetry.Sample.Mode"));
    model.messages[1].fields.push_back(
        field("statuses", FieldType::Enum, "telemetry.Status", FieldLabel::Repeated));
    const std::filesystem::path bounds = root / "bounds.yaml";
    write_text(bounds,
               "bounds:\n"
               "  telemetry.Sample.name:\n"
               "    max_bytes: 64\n"
               "  telemetry.Sample.payload:\n"
               "    max_bytes: 256\n"
               "  telemetry.Sample.values:\n"
               "    max_elements: 4\n"
               "  telemetry.Sample.children:\n"
               "    max_elements: 2\n"
               "  telemetry.Sample.statuses:\n"
               "    max_elements: 3\n"
               "  telemetry.Child.label:\n"
               "    max_bytes: 32\n");
    const auto result = quarry::tools::protobuf::translate_descriptor_model(
        model, "telemetry.Sample", bounds.string(), (root / "generated").string());
    ASSERT_TRUE(result.succeeded())
        << (result.diagnostics.empty() ? "" : result.diagnostics.front());
    const std::string sample = read_text(root / "generated/telemetry/sample/sample.brd");
    EXPECT_NE(sample.find("  Status:\n    values:\n      UNKNOWN: 0\n      READY: 1\n"),
              std::string::npos);
    EXPECT_NE(sample.find("  Mode:\n    values:\n      IDLE: 0\n      ACTIVE: 1\n"),
              std::string::npos);
    EXPECT_NE(sample.find("type: Status\n"), std::string::npos);
    EXPECT_NE(sample.find("type: Status[]\n"), std::string::npos);
    const std::string manifest = read_text(root / "generated/manifest.json");
    EXPECT_NE(manifest.find("\"kind\": \"enum\""), std::string::npos);
    EXPECT_NE(manifest.find("\"number\": 1"), std::string::npos);
    EXPECT_NE(manifest.find("\"ordinal\""), std::string::npos);
}

TEST(ProtobufTranslationTest, RejectsMissingAndUnusedBounds) {
    const std::filesystem::path root = temporary_directory("bounds");
    const std::filesystem::path bounds = root / "bounds.yaml";
    write_text(bounds,
               "bounds:\n"
               "  telemetry.Sample.name:\n"
               "    max_bytes: 0\n"
               "  telemetry.Sample.unused:\n"
               "    max_elements: 2\n");
    const auto result = quarry::tools::protobuf::translate_descriptor_model(
        representative_model(), "telemetry.Sample", bounds.string(), (root / "generated").string());
    ASSERT_FALSE(result.succeeded());
    const std::string diagnostics = [&] {
        std::ostringstream output;
        for (const auto& diagnostic : result.diagnostics) output << diagnostic << "\n";
        return output.str();
    }();
    EXPECT_NE(diagnostics.find("invalid max_bytes value"), std::string::npos);
}

TEST(ProtobufTranslationTest, RejectsNegativeAndAliasedEnums) {
    const std::filesystem::path root = temporary_directory("enum-rejection");
    DescriptorModel model = representative_model();
    DescriptorEnum invalid;
    invalid.name = "InvalidStatus";
    invalid.fully_qualified_name = "telemetry.InvalidStatus";
    invalid.file_name = "status.proto";
    invalid.values = {DescriptorEnumValue{"NEGATIVE", -1},
                      DescriptorEnumValue{"ALIAS", 1},
                      DescriptorEnumValue{"OTHER_ALIAS", 1}};
    model.enums.push_back(invalid);
    model.messages[1].fields.push_back(
        field("status", FieldType::Enum, "telemetry.InvalidStatus"));
    const std::filesystem::path bounds = root / "bounds.yaml";
    write_text(bounds,
               "bounds:\n"
               "  telemetry.Sample.name:\n"
               "    max_bytes: 64\n"
               "  telemetry.Sample.payload:\n"
               "    max_bytes: 64\n"
               "  telemetry.Sample.values:\n"
               "    max_elements: 2\n"
               "  telemetry.Sample.children:\n"
               "    max_elements: 2\n"
               "  telemetry.Child.label:\n"
               "    max_bytes: 32\n");
    const auto result = quarry::tools::protobuf::translate_descriptor_model(
        model, "telemetry.Sample", bounds.string(), (root / "generated").string());
    ASSERT_FALSE(result.succeeded());
    std::ostringstream diagnostics;
    for (const auto& diagnostic : result.diagnostics) diagnostics << diagnostic << "\n";
    EXPECT_NE(diagnostics.str().find("negative value"), std::string::npos);
    EXPECT_NE(diagnostics.str().find("enum aliases"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(root / "generated"));
}

TEST(ProtobufTranslationTest, TranslationIsDeterministicAcrossBoundsOrdering) {
    const std::filesystem::path root = temporary_directory("deterministic");
    const std::string first_bounds =
        "bounds:\n"
        "  telemetry.Sample.name:\n"
        "    max_bytes: 64\n"
        "  telemetry.Sample.payload:\n"
        "    max_bytes: 256\n"
        "  telemetry.Sample.values:\n"
        "    max_elements: 4\n"
        "  telemetry.Sample.children:\n"
        "    max_elements: 2\n"
        "  telemetry.Child.label:\n"
        "    max_bytes: 32\n";
    const std::string second_bounds =
        "bounds:\n"
        "  telemetry.Child.label:\n"
        "    max_bytes: 32\n"
        "  telemetry.Sample.children:\n"
        "    max_elements: 2\n"
        "  telemetry.Sample.values:\n"
        "    max_elements: 4\n"
        "  telemetry.Sample.payload:\n"
        "    max_bytes: 256\n"
        "  telemetry.Sample.name:\n"
        "    max_bytes: 64\n";
    write_text(root / "first" / "bounds.yaml", first_bounds);
    write_text(root / "second" / "bounds.yaml", second_bounds);
    ASSERT_TRUE(quarry::tools::protobuf::translate_descriptor_model(
                    representative_model(), "telemetry.Sample", (root / "first" / "bounds.yaml").string(),
                    (root / "first").string())
                    .succeeded());
    ASSERT_TRUE(quarry::tools::protobuf::translate_descriptor_model(
                    representative_model(), "telemetry.Sample", (root / "second" / "bounds.yaml").string(),
                    (root / "second").string())
                    .succeeded());
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "first")) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".yaml") continue;
        const auto relative = std::filesystem::relative(entry.path(), root / "first");
        EXPECT_EQ(read_text(entry.path()), read_text(root / "second" / relative));
    }
}

TEST(ProtobufTranslationTest, RejectsRecursiveMessagesBeforeWriting) {
    const std::filesystem::path root = temporary_directory("recursive");
    DescriptorModel model = representative_model();
    model.messages[1].fields.push_back(
        field("self", FieldType::Message, "telemetry.Sample"));
    const std::filesystem::path bounds = root / "bounds.yaml";
    write_text(bounds,
               "bounds:\n"
               "  telemetry.Sample.name:\n"
               "    max_bytes: 64\n"
               "  telemetry.Sample.payload:\n"
               "    max_bytes: 64\n"
               "  telemetry.Sample.values:\n"
               "    max_elements: 2\n"
               "  telemetry.Sample.children:\n"
               "    max_elements: 2\n"
               "  telemetry.Child.label:\n"
               "    max_bytes: 32\n");
    const auto result = quarry::tools::protobuf::translate_descriptor_model(
        model, "telemetry.Sample", bounds.string(), (root / "generated").string());
    ASSERT_FALSE(result.succeeded());
    EXPECT_NE(result.diagnostics.front().find("recursive protobuf message graph"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(root / "generated"));
}

TEST(ProtobufTranslationTest, RejectsUnsupportedReachableConstructs) {
    const std::filesystem::path root = temporary_directory("unsupported");
    DescriptorModel model = representative_model();
    model.messages[1].fields.push_back(field("status", FieldType::Enum, "telemetry.Status"));
    DescriptorField oneof = field("choice", FieldType::Int32);
    oneof.oneof_index = 0;
    model.messages[1].fields.push_back(oneof);
    DescriptorMessage map_entry;
    map_entry.name = "LabelsEntry";
    map_entry.fully_qualified_name = "telemetry.Sample.LabelsEntry";
    map_entry.file_name = "sample.proto";
    map_entry.map_entry = true;
    model.messages.push_back(std::move(map_entry));
    model.messages[1].fields.push_back(
        field("labels", FieldType::Message, "telemetry.Sample.LabelsEntry", FieldLabel::Repeated));
    const std::filesystem::path bounds = root / "bounds.yaml";
    write_text(bounds,
               "bounds:\n"
               "  telemetry.Sample.name:\n"
               "    max_bytes: 64\n"
               "  telemetry.Sample.payload:\n"
               "    max_bytes: 64\n"
               "  telemetry.Sample.values:\n"
               "    max_elements: 2\n"
               "  telemetry.Sample.children:\n"
               "    max_elements: 2\n"
               "  telemetry.Sample.labels:\n"
               "    max_elements: 2\n"
               "  telemetry.Child.label:\n"
               "    max_bytes: 32\n");
    const auto result = quarry::tools::protobuf::translate_descriptor_model(
        model, "telemetry.Sample", bounds.string(), (root / "generated").string());
    ASSERT_FALSE(result.succeeded());
    std::ostringstream diagnostics;
    for (const auto& diagnostic : result.diagnostics) diagnostics << diagnostic << "\n";
    EXPECT_NE(diagnostics.str().find("unknown enum"), std::string::npos);
    EXPECT_NE(diagnostics.str().find("oneof semantics"), std::string::npos);
    EXPECT_NE(diagnostics.str().find("protobuf map type"), std::string::npos);
}

#ifndef QUARRY_PROTOC_EXECUTABLE
#define QUARRY_PROTOC_EXECUTABLE ""
#endif
#ifndef QUARRY_PROTOBUF_TRANSLATOR
#define QUARRY_PROTOBUF_TRANSLATOR ""
#endif
#ifndef QUARRY_SCHEMA_COMPILER
#define QUARRY_SCHEMA_COMPILER ""
#endif

[[nodiscard]] std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (char character : value) {
        if (character == '\'') result += "'\\''";
        else result += character;
    }
    result += "'";
    return result;
}

TEST(ProtobufTranslationTest, ProtocTranslationAndCompilerFollowThrough) {
    if (std::string_view(QUARRY_PROTOC_EXECUTABLE).empty() ||
        std::string_view(QUARRY_PROTOBUF_TRANSLATOR).empty() ||
        std::string_view(QUARRY_SCHEMA_COMPILER).empty()) {
        GTEST_SKIP() << "translator integration paths are unavailable";
    }
    const std::filesystem::path root = temporary_directory("follow-through");
    write_text(root / "schema.proto",
               "syntax = \"proto3\";\n"
               "package telemetry;\n"
               "enum Status { UNKNOWN = 0; READY = 1; }\n"
               "message Child { string label = 1; }\n"
               "message Sample {\n"
               "  string name = 1;\n"
               "  bytes payload = 2;\n"
               "  repeated int32 values = 3;\n"
               "  Child child = 4;\n"
               "  repeated Child children = 5;\n"
               "  Status status = 6;\n"
               "  repeated Status statuses = 7;\n"
               "}\n");
    write_text(root / "bounds.yaml",
               "bounds:\n"
               "  telemetry.Sample.name:\n"
               "    max_bytes: 64\n"
               "  telemetry.Sample.payload:\n"
               "    max_bytes: 128\n"
               "  telemetry.Sample.values:\n"
               "    max_elements: 4\n"
               "  telemetry.Sample.children:\n"
               "    max_elements: 2\n"
               "  telemetry.Sample.statuses:\n"
               "    max_elements: 3\n"
               "  telemetry.Child.label:\n"
               "    max_bytes: 32\n");
    const std::filesystem::path descriptor = root / "schema.pb";
    const std::filesystem::path generated = root / "generated";
    const std::string protoc = shell_quote(QUARRY_PROTOC_EXECUTABLE);
    const std::string translator = shell_quote(QUARRY_PROTOBUF_TRANSLATOR);
    const std::string compiler = shell_quote(QUARRY_SCHEMA_COMPILER);
    ASSERT_EQ(std::system((protoc + " --descriptor_set_out=" + shell_quote(descriptor.string()) +
                           " --include_imports --proto_path=" + shell_quote(root.string()) +
                           " " + shell_quote((root / "schema.proto").string()))
                              .c_str()),
              0);
    ASSERT_EQ(std::system((translator + " --descriptor-set " + shell_quote(descriptor.string()) +
                           " --root telemetry.Sample --bounds " +
                           shell_quote((root / "bounds.yaml").string()) + " --output-dir " +
                           shell_quote(generated.string()))
                              .c_str()),
              0);
    ASSERT_TRUE(std::filesystem::exists(generated / "manifest.json"));
    const std::filesystem::path compiler_output = root / "compiled";
    const std::string child = (generated / "telemetry/child/child.brd").string();
    const std::string sample = (generated / "telemetry/sample/sample.brd").string();
    ASSERT_EQ(std::system((compiler + " --language cpp --output-directory " +
                           shell_quote(compiler_output.string()) + " --root-file-stem child " +
                           shell_quote(child))
                              .c_str()),
              0);
    ASSERT_EQ(std::system((compiler + " --language cpp --output-directory " +
                           shell_quote(compiler_output.string()) + " --root-file-stem sample " +
                           shell_quote(sample))
                              .c_str()),
              0);
    EXPECT_TRUE(contains_filename(compiler_output, "child.generated.hpp"));
    EXPECT_TRUE(contains_filename(compiler_output, "sample.generated.hpp"));
}

TEST(ProtobufTranslationTest, ResolvesDescriptorOptionsAndInheritance) {
#ifndef QUARRY_PROTOBUF_OPTIONS_PROTO
    GTEST_SKIP() << "protobuf option definition path is unavailable";
#else
    if (std::string_view(QUARRY_PROTOC_EXECUTABLE).empty() ||
        std::string_view(QUARRY_PROTOBUF_TRANSLATOR).empty()) {
        GTEST_SKIP() << "translator integration paths are unavailable";
    }
    const std::filesystem::path root = temporary_directory("options");
    const std::filesystem::path options_proto = QUARRY_PROTOBUF_OPTIONS_PROTO;
    write_text(root / "schema.proto",
               "syntax = \"proto3\";\n"
               "package telemetry;\n"
               "import \"quarry_options.proto\";\n"
               "option (quarry.protobuf.file_default_bounds).max_bytes = 96;\n"
               "message Sample {\n"
               "  option (quarry.protobuf.message_default_bounds).max_elements = 4;\n"
               "  string name = 1 [(quarry.protobuf.field_bounds).max_bytes = 32];\n"
               "  bytes payload = 2;\n"
               "  repeated int32 values = 3;\n"
               "}\n");
    write_text(root / "bounds.yaml", "bounds:\n");
    const std::filesystem::path descriptor = root / "schema.pb";
    const std::filesystem::path generated = root / "generated";
    const std::string protoc = shell_quote(QUARRY_PROTOC_EXECUTABLE);
    const std::string translator = shell_quote(QUARRY_PROTOBUF_TRANSLATOR);
    ASSERT_EQ(std::system((protoc + " --descriptor_set_out=" + shell_quote(descriptor.string()) +
                           " --include_imports --proto_path=" + shell_quote(root.string()) +
                           " --proto_path=" + shell_quote(options_proto.parent_path().string()) +
                           " " + shell_quote((root / "schema.proto").string()))
                              .c_str()),
              0);
    ASSERT_EQ(std::system((translator + " --descriptor-set " + shell_quote(descriptor.string()) +
                           " --root telemetry.Sample --options " +
                           shell_quote((root / "bounds.yaml").string()) + " --output-dir " +
                           shell_quote(generated.string()))
                              .c_str()),
              0);
    const std::string manifest = read_text(generated / "manifest.json");
    EXPECT_NE(manifest.find("\"max_bytes\": 32"), std::string::npos);
    EXPECT_NE(manifest.find("\"max_elements\": 4"), std::string::npos);
    EXPECT_NE(manifest.find("\"max_bytes\": 96"), std::string::npos);
    EXPECT_NE(manifest.find("\"source_type\": \"field_option\""), std::string::npos);
    EXPECT_NE(manifest.find("\"source_type\": \"file_option\""), std::string::npos);
#endif
}
