#include "descriptor_model.hpp"

#include <google/protobuf/descriptor.pb.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>

#ifndef QUARRY_PROTOC_EXECUTABLE
#error "QUARRY_PROTOC_EXECUTABLE must be defined"
#endif

namespace {

using quarry::tools::protobuf::DescriptorLoadResult;
using quarry::tools::protobuf::DescriptorModel;

[[nodiscard]] std::filesystem::path make_temp_directory(std::string_view stem) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string("quarry-protobuf-loader-") + std::string(stem) + "-" +
         std::to_string(suffix));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

void write_binary(const std::filesystem::path& path,
                  const google::protobuf::FileDescriptorSet& descriptor_set) {
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output);
    ASSERT_TRUE(descriptor_set.SerializeToOstream(&output));
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path);
    ASSERT_TRUE(output);
    output << text;
}

[[nodiscard]] std::string shell_quote(const std::filesystem::path& path) {
    std::string result = "'";
    for (const char character : path.string()) {
        if (character == '\'') {
            result += "'\\''";
        } else {
            result.push_back(character);
        }
    }
    result.push_back('\'');
    return result;
}

[[nodiscard]] DescriptorModel load_model(const std::filesystem::path& path) {
    const DescriptorLoadResult result =
        quarry::tools::protobuf::load_descriptor_set(path.string());
    EXPECT_TRUE(result.succeeded());
    if (!result.succeeded()) {
        for (const std::string& diagnostic : result.diagnostics) {
            ADD_FAILURE() << diagnostic;
        }
        return {};
    }
    return *result.model;
}

[[nodiscard]] google::protobuf::FileDescriptorProto make_file(std::string_view name,
                                                                std::string_view package) {
    google::protobuf::FileDescriptorProto file;
    file.set_name(std::string(name));
    file.set_package(std::string(package));
    file.set_syntax("proto3");
    return file;
}

google::protobuf::FileDescriptorSet make_descriptor_set(bool reverse) {
    google::protobuf::FileDescriptorProto first = make_file("z.proto", "zeta");
    auto* zed = first.add_message_type();
    zed->set_name("Zed");
    auto* value = zed->add_field();
    value->set_name("value");
    value->set_number(1);
    value->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
    value->set_type(google::protobuf::FieldDescriptorProto::TYPE_STRING);

    google::protobuf::FileDescriptorProto second = make_file("a.proto", "alpha");
    second.add_dependency("z.proto");
    auto* mode = second.add_enum_type();
    mode->set_name("Mode");
    auto* unknown = mode->add_value();
    unknown->set_name("MODE_UNKNOWN");
    unknown->set_number(0);
    auto* root = second.add_message_type();
    root->set_name("Root");
    auto* child = root->add_nested_type();
    child->set_name("Child");
    auto* child_value = child->add_field();
    child_value->set_name("id");
    child_value->set_number(1);
    child_value->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
    child_value->set_type(google::protobuf::FieldDescriptorProto::TYPE_UINT32);
    auto* root_value = root->add_field();
    root_value->set_name("zed");
    root_value->set_number(2);
    root_value->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
    root_value->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
    root_value->set_type_name(".zeta.Zed");

    google::protobuf::FileDescriptorSet result;
    if (reverse) {
        *result.add_file() = second;
        *result.add_file() = first;
    } else {
        *result.add_file() = first;
        *result.add_file() = second;
    }
    return result;
}

} // namespace

TEST(ProtobufDescriptorLoaderTest, LoadsAndEnumeratesDescriptorContents) {
    const std::filesystem::path root = make_temp_directory("enumerate");
    const std::filesystem::path descriptor_set = root / "schema.pb";
    write_binary(descriptor_set, make_descriptor_set(false));

    const DescriptorModel model = load_model(descriptor_set);

    ASSERT_EQ(model.files.size(), 2U);
    EXPECT_EQ(model.files[0].name, "a.proto");
    EXPECT_EQ(model.files[0].imports, std::vector<std::string>{"z.proto"});
    ASSERT_EQ(model.messages.size(), 3U);
    EXPECT_EQ(model.messages[0].fully_qualified_name, "alpha.Root");
    EXPECT_EQ(model.messages[0].nested_messages,
              std::vector<std::string>{"alpha.Root.Child"});
    ASSERT_EQ(model.enums.size(), 1U);
    EXPECT_EQ(model.enums[0].fully_qualified_name, "alpha.Mode");
    ASSERT_EQ(model.messages[0].fields.size(), 1U);
    EXPECT_EQ(model.messages[0].fields[0].type_name, "zeta.Zed");
    EXPECT_EQ(model.messages[0].fields[0].number, 2);
}

TEST(ProtobufDescriptorLoaderTest, EnumerationIsIndependentOfDescriptorFileOrder) {
    const std::filesystem::path root = make_temp_directory("deterministic");
    const std::filesystem::path first_path = root / "first.pb";
    const std::filesystem::path second_path = root / "second.pb";
    write_binary(first_path, make_descriptor_set(false));
    write_binary(second_path, make_descriptor_set(true));

    const DescriptorModel first = load_model(first_path);
    const DescriptorModel second = load_model(second_path);
    EXPECT_EQ(quarry::tools::protobuf::render_descriptor_list(first),
              quarry::tools::protobuf::render_descriptor_list(second));
}

TEST(ProtobufDescriptorLoaderTest, RejectsMalformedDescriptorSet) {
    const std::filesystem::path root = make_temp_directory("malformed");
    const std::filesystem::path descriptor_set = root / "bad.pb";
    write_text(descriptor_set, "not a protobuf descriptor set");

    const DescriptorLoadResult result =
        quarry::tools::protobuf::load_descriptor_set(descriptor_set.string());
    EXPECT_FALSE(result.succeeded());
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_NE(result.diagnostics.front().find("malformed"), std::string::npos);
}

TEST(ProtobufDescriptorLoaderTest, RejectsDuplicateFilesAndMissingImports) {
    google::protobuf::FileDescriptorSet descriptor_set;
    google::protobuf::FileDescriptorProto first = make_file("root.proto", "root");
    first.add_dependency("missing.proto");
    *descriptor_set.add_file() = first;
    *descriptor_set.add_file() = first;

    const std::filesystem::path root = make_temp_directory("duplicates");
    const std::filesystem::path path = root / "schema.pb";
    write_binary(path, descriptor_set);

    const DescriptorLoadResult result =
        quarry::tools::protobuf::load_descriptor_set(path.string());
    EXPECT_FALSE(result.succeeded());
    const std::string diagnostics =
        std::accumulate(result.diagnostics.begin(), result.diagnostics.end(), std::string{},
                        [](std::string current, const std::string& diagnostic) {
                            return current + diagnostic + "\n";
                        });
    EXPECT_NE(diagnostics.find("duplicate file"), std::string::npos);
    EXPECT_NE(diagnostics.find("missing file"), std::string::npos);
}

TEST(ProtobufDescriptorLoaderTest, RejectsUnknownMessageReference) {
    google::protobuf::FileDescriptorSet descriptor_set;
    google::protobuf::FileDescriptorProto file = make_file("root.proto", "root");
    auto* root = file.add_message_type();
    root->set_name("Root");
    auto* field = root->add_field();
    field->set_name("missing");
    field->set_number(1);
    field->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
    field->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
    field->set_type_name("root.Missing");
    *descriptor_set.add_file() = file;

    const std::filesystem::path directory = make_temp_directory("reference");
    const std::filesystem::path path = directory / "schema.pb";
    write_binary(path, descriptor_set);

    const DescriptorLoadResult result =
        quarry::tools::protobuf::load_descriptor_set(path.string());
    EXPECT_FALSE(result.succeeded());
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_NE(result.diagnostics.front().find("unknown type"), std::string::npos);
}

TEST(ProtobufDescriptorLoaderTest, LoadsDescriptorSetProducedByProtoc) {
    const std::filesystem::path root = make_temp_directory("protoc");
    write_text(root / "common.proto",
               "syntax = \"proto3\";\n"
               "package fixture.common;\n"
               "message Shared { uint32 id = 1; }\n");
    write_text(root / "root.proto",
               "syntax = \"proto3\";\n"
               "package fixture.root;\n"
               "import \"common.proto\";\n"
               "message Root { repeated fixture.common.Shared items = 1; }\n");
    const std::filesystem::path descriptor_set = root / "schema.pb";
    const std::string command =
        shell_quote(QUARRY_PROTOC_EXECUTABLE) + " --descriptor_set_out=" +
        shell_quote(descriptor_set) + " --include_imports --include_source_info --proto_path=" +
        shell_quote(root) + " " + shell_quote(root / "root.proto");
    ASSERT_EQ(std::system(command.c_str()), 0);

    const DescriptorModel model = load_model(descriptor_set);
    ASSERT_EQ(model.files.size(), 2U);
    ASSERT_EQ(model.messages.size(), 2U);
    EXPECT_EQ(model.messages[0].fully_qualified_name, "fixture.common.Shared");
    EXPECT_EQ(model.messages[1].fully_qualified_name, "fixture.root.Root");
    ASSERT_EQ(model.messages[1].fields.size(), 1U);
    EXPECT_EQ(model.messages[1].fields[0].type_name, "fixture.common.Shared");
    EXPECT_EQ(model.messages[1].fields[0].label,
              quarry::tools::protobuf::FieldLabel::Repeated);
}
