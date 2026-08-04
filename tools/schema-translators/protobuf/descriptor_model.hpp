#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace quarry::tools::protobuf {

enum class FieldLabel {
    Optional,
    Required,
    Repeated,
};

enum class FieldType {
    Double,
    Float,
    Int64,
    Uint64,
    Int32,
    Fixed64,
    Fixed32,
    Bool,
    String,
    Group,
    Message,
    Bytes,
    Uint32,
    Enum,
    Sfixed32,
    Sfixed64,
    Sint32,
    Sint64,
    Unknown,
};

struct DescriptorBounds {
    std::optional<std::uint32_t> max_bytes;
    std::optional<std::uint32_t> max_elements;
};

struct DescriptorField {
    std::string name;
    std::int32_t number = 0;
    std::uint32_t declaration_order = 0;
    FieldLabel label = FieldLabel::Optional;
    FieldType type = FieldType::Unknown;
    std::string type_name;
    std::int32_t oneof_index = -1;
    bool proto3_optional = false;
    bool packed = false;
    bool has_default_value = false;
    std::optional<DescriptorBounds> custom_bounds;
};

struct DescriptorEnumValue {
    std::string name;
    std::int32_t number = 0;
};

struct DescriptorEnum {
    std::string name;
    std::string fully_qualified_name;
    std::string file_name;
    std::string containing_message;
    std::vector<DescriptorEnumValue> values;
};

struct DescriptorMessage {
    std::string name;
    std::string fully_qualified_name;
    std::string file_name;
    std::string containing_message;
    std::vector<std::string> nested_messages;
    std::vector<std::string> nested_enums;
    std::vector<std::string> extensions;
    std::vector<DescriptorField> fields;
    bool map_entry = false;
    std::optional<DescriptorBounds> default_bounds;
};

struct DescriptorFile {
    std::string name;
    std::string package;
    std::string syntax;
    std::vector<std::string> imports;
    std::vector<std::string> public_imports;
    std::vector<std::string> weak_imports;
    std::vector<std::string> messages;
    std::vector<std::string> enums;
    std::vector<std::string> services;
    std::vector<std::string> extensions;
    std::optional<DescriptorBounds> default_bounds;
};

struct DescriptorModel {
    std::vector<DescriptorFile> files;
    std::vector<std::string> packages;
    std::vector<DescriptorMessage> messages;
    std::vector<DescriptorEnum> enums;
};

struct DescriptorLoadResult {
    std::optional<DescriptorModel> model;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool succeeded() const { return model.has_value() && diagnostics.empty(); }
};

[[nodiscard]] DescriptorLoadResult load_descriptor_set(const std::string& path);

[[nodiscard]] std::string render_descriptor_list(const DescriptorModel& model);

[[nodiscard]] std::string field_type_name(FieldType type);
[[nodiscard]] std::string field_label_name(FieldLabel label);

} // namespace quarry::tools::protobuf
