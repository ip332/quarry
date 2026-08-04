#include "descriptor_model.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/text_format.h>

#include "quarry_options.pb.h"

namespace quarry::tools::protobuf {
namespace {

using google::protobuf::DescriptorProto;
using google::protobuf::EnumDescriptorProto;
using google::protobuf::FieldDescriptorProto;
using google::protobuf::FileDescriptorProto;
using google::protobuf::FileDescriptorSet;

void add_diagnostic(DescriptorLoadResult& result, std::string message);

[[nodiscard]] std::string qualify(std::string_view prefix, std::string_view name);

[[nodiscard]] std::string option_text(const google::protobuf::Message& options) {
    std::string text;
    google::protobuf::TextFormat::Printer printer;
    if (!printer.PrintToString(options, &text)) return {};
    return text;
}

class DescriptorPoolFinder final : public google::protobuf::TextFormat::Finder {
public:
    DescriptorPoolFinder(const google::protobuf::DescriptorPool& pool,
                        google::protobuf::MessageFactory* factory = nullptr)
        : pool_(pool), factory_(factory) {}

    const google::protobuf::FieldDescriptor* FindExtension(
        google::protobuf::Message* message, const std::string& name) const override {
        const auto* extension = pool_.FindExtensionByName(name);
        if (extension == nullptr ||
            extension->containing_type()->full_name() != message->GetDescriptor()->full_name()) {
            return nullptr;
        }
        return extension;
    }

    const google::protobuf::FieldDescriptor* FindExtensionByNumber(
        const google::protobuf::Descriptor* descriptor, int number) const override {
        return pool_.FindExtensionByNumber(descriptor, number);
    }

    google::protobuf::MessageFactory* FindExtensionFactory(
        const google::protobuf::FieldDescriptor*) const override {
        return factory_;
    }

private:
    const google::protobuf::DescriptorPool& pool_;
    google::protobuf::MessageFactory* factory_;
};

std::optional<DescriptorBounds> read_dynamic_bounds(DescriptorLoadResult& result,
                                                     const google::protobuf::DescriptorPool& pool,
                                                     const google::protobuf::Message& options,
                                                     std::string_view extension_name,
                                                     std::string_view owner) {
    const auto* extension = pool.FindExtensionByName(std::string(extension_name));
    const auto* descriptor = pool.FindMessageTypeByName(
        std::string(options.GetDescriptor()->full_name()));
    if (extension == nullptr || descriptor == nullptr) {
        return std::nullopt;
    }
    google::protobuf::DynamicMessageFactory factory(&pool);
    const auto* prototype = factory.GetPrototype(descriptor);
    if (prototype == nullptr) {
        add_diagnostic(result, "cannot construct protobuf options descriptor for '" +
                                 std::string(owner) + "'");
        return std::nullopt;
    }
    std::unique_ptr<google::protobuf::Message> dynamic_options(prototype->New());
    DescriptorPoolFinder finder(pool, &factory);
    google::protobuf::TextFormat::Parser parser;
    parser.SetFinder(&finder);
    parser.AllowFieldNumber(true);
    if (!parser.ParseFromString(option_text(options), dynamic_options.get())) {
        add_diagnostic(result, "malformed protobuf options for '" + std::string(owner) + "'");
        return std::nullopt;
    }
    const auto* reflection = dynamic_options->GetReflection();
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields(*dynamic_options, &fields);
    const google::protobuf::FieldDescriptor* dynamic_extension = nullptr;
    for (const auto* field : fields) {
        if (field->is_extension() && field->full_name() == extension_name) {
            dynamic_extension = field;
            break;
        }
    }
    if (dynamic_extension == nullptr) return std::nullopt;
    const auto& value = reflection->GetMessage(*dynamic_options, dynamic_extension);
    const auto* bounds_descriptor = value.GetDescriptor();
    const auto* bounds_reflection = value.GetReflection();
    const auto* max_bytes = bounds_descriptor->FindFieldByName("max_bytes");
    const auto* max_elements = bounds_descriptor->FindFieldByName("max_elements");
    if (max_bytes == nullptr || max_elements == nullptr) {
        add_diagnostic(result, "malformed protobuf Bounds definition for '" + std::string(owner) + "'");
        return std::nullopt;
    }
    DescriptorBounds converted;
    if (bounds_reflection->HasField(value, max_bytes)) {
        converted.max_bytes = bounds_reflection->GetUInt32(value, max_bytes);
    }
    if (bounds_reflection->HasField(value, max_elements)) {
        converted.max_elements = bounds_reflection->GetUInt32(value, max_elements);
    }
    if (!converted.max_bytes.has_value() && !converted.max_elements.has_value()) return std::nullopt;
    if (converted.max_bytes == 0) {
        add_diagnostic(result, "custom bounds for '" + std::string(owner) +
                                 "' has invalid max_bytes value 0");
    }
    if (converted.max_elements == 0) {
        add_diagnostic(result, "custom bounds for '" + std::string(owner) +
                                 "' has invalid max_elements value 0");
    }
    return converted;
}

const DescriptorProto* find_message_proto(const DescriptorProto& message,
                                          std::string_view prefix,
                                          std::string_view requested) {
    const std::string name = qualify(prefix, message.name());
    if (name == requested) return &message;
    for (const auto& nested : message.nested_type()) {
        if (const auto* found = find_message_proto(nested, name, requested)) return found;
    }
    return nullptr;
}

void apply_custom_options(DescriptorLoadResult& result, const google::protobuf::DescriptorPool& pool,
                          const FileDescriptorSet& descriptor_set, DescriptorModel& model) {
    std::map<std::string, const FileDescriptorProto*> sources;
    for (const auto& source : descriptor_set.file()) sources.emplace(source.name(), &source);
    for (DescriptorFile& file : model.files) {
        const auto source = sources.find(file.name);
        const auto* descriptor = pool.FindFileByName(file.name);
        if (source == sources.end() || descriptor == nullptr) {
            add_diagnostic(result, "cannot resolve descriptor source file '" + file.name + "'");
            continue;
        }
        const auto& options = descriptor->options();
        file.default_bounds = read_dynamic_bounds(
            result, pool, options, "quarry.protobuf.file_default_bounds", file.name);
    }
    for (DescriptorMessage& message : model.messages) {
        const auto source = sources.find(message.file_name);
        const DescriptorProto* message_proto = nullptr;
        if (source != sources.end()) {
            for (const auto& candidate : source->second->message_type()) {
                message_proto = find_message_proto(candidate, source->second->package(),
                                                   message.fully_qualified_name);
                if (message_proto != nullptr) break;
            }
        }
        if (message_proto == nullptr) {
            add_diagnostic(result, "cannot resolve message source '" + message.fully_qualified_name + "'");
            continue;
        }
        const auto* descriptor = pool.FindMessageTypeByName(message.fully_qualified_name);
        if (descriptor == nullptr) {
            add_diagnostic(result, "cannot resolve message descriptor '" +
                                     message.fully_qualified_name + "'");
            continue;
        }
        const auto& options = descriptor->options();
        message.default_bounds = read_dynamic_bounds(
            result, pool, options, "quarry.protobuf.message_default_bounds",
            message.fully_qualified_name);
        for (DescriptorField& field : message.fields) {
            const auto field_iterator = std::find_if(
                message_proto->field().begin(), message_proto->field().end(),
                [&](const FieldDescriptorProto& candidate) { return candidate.name() == field.name; });
            if (field_iterator == message_proto->field().end()) {
                add_diagnostic(result, "cannot resolve field descriptor '" +
                                         message.fully_qualified_name + "." + field.name + "'");
                continue;
            }
            const auto* field_descriptor = descriptor->FindFieldByName(field.name);
            if (field_descriptor == nullptr) {
                add_diagnostic(result, "cannot resolve field descriptor '" +
                                         message.fully_qualified_name + "." + field.name + "'");
                continue;
            }
            const auto& field_options = field_descriptor->options();
            field.custom_bounds = read_dynamic_bounds(
                result, pool, field_options, "quarry.protobuf.field_bounds",
                message.fully_qualified_name + "." + field.name);
        }
    }
}

[[nodiscard]] std::string qualify(std::string_view prefix, std::string_view name) {
    if (prefix.empty()) {
        return std::string(name);
    }
    return std::string(prefix) + "." + std::string(name);
}

[[nodiscard]] std::string without_leading_dot(std::string value) {
    if (!value.empty() && value.front() == '.') {
        value.erase(value.begin());
    }
    return value;
}

void add_diagnostic(DescriptorLoadResult& result, std::string message) {
    result.diagnostics.push_back(std::move(message));
}

[[nodiscard]] FieldLabel field_label(FieldDescriptorProto::Label label) {
    switch (label) {
    case FieldDescriptorProto::LABEL_REQUIRED:
        return FieldLabel::Required;
    case FieldDescriptorProto::LABEL_REPEATED:
        return FieldLabel::Repeated;
    case FieldDescriptorProto::LABEL_OPTIONAL:
        return FieldLabel::Optional;
    default:
        return FieldLabel::Optional;
    }
    return FieldLabel::Optional;
}

[[nodiscard]] FieldType field_type(FieldDescriptorProto::Type type) {
    switch (type) {
    case FieldDescriptorProto::TYPE_DOUBLE:
        return FieldType::Double;
    case FieldDescriptorProto::TYPE_FLOAT:
        return FieldType::Float;
    case FieldDescriptorProto::TYPE_INT64:
        return FieldType::Int64;
    case FieldDescriptorProto::TYPE_UINT64:
        return FieldType::Uint64;
    case FieldDescriptorProto::TYPE_INT32:
        return FieldType::Int32;
    case FieldDescriptorProto::TYPE_FIXED64:
        return FieldType::Fixed64;
    case FieldDescriptorProto::TYPE_FIXED32:
        return FieldType::Fixed32;
    case FieldDescriptorProto::TYPE_BOOL:
        return FieldType::Bool;
    case FieldDescriptorProto::TYPE_STRING:
        return FieldType::String;
    case FieldDescriptorProto::TYPE_GROUP:
        return FieldType::Group;
    case FieldDescriptorProto::TYPE_MESSAGE:
        return FieldType::Message;
    case FieldDescriptorProto::TYPE_BYTES:
        return FieldType::Bytes;
    case FieldDescriptorProto::TYPE_UINT32:
        return FieldType::Uint32;
    case FieldDescriptorProto::TYPE_ENUM:
        return FieldType::Enum;
    case FieldDescriptorProto::TYPE_SFIXED32:
        return FieldType::Sfixed32;
    case FieldDescriptorProto::TYPE_SFIXED64:
        return FieldType::Sfixed64;
    case FieldDescriptorProto::TYPE_SINT32:
        return FieldType::Sint32;
    case FieldDescriptorProto::TYPE_SINT64:
        return FieldType::Sint64;
    default:
        return FieldType::Unknown;
    }
    return FieldType::Unknown;
}

void collect_enum(const EnumDescriptorProto& source, std::string_view prefix,
                  std::string_view file_name, std::string_view containing_message,
                  DescriptorModel& model, DescriptorFile& file) {
    DescriptorEnum enumeration;
    enumeration.name = source.name();
    enumeration.fully_qualified_name = qualify(prefix, source.name());
    enumeration.file_name = std::string(file_name);
    enumeration.containing_message = std::string(containing_message);
    for (const auto& value : source.value()) {
        enumeration.values.push_back(
            DescriptorEnumValue{.name = value.name(), .number = value.number()});
    }
    model.enums.push_back(std::move(enumeration));
    file.enums.push_back(qualify(prefix, source.name()));
}

void collect_message(const DescriptorProto& source, std::string_view prefix,
                     std::string_view file_name, std::string_view containing_message,
                     DescriptorModel& model, DescriptorFile& file) {
    DescriptorMessage message;
    message.name = source.name();
    message.fully_qualified_name = qualify(prefix, source.name());
    message.file_name = std::string(file_name);
    message.containing_message = std::string(containing_message);
    for (int index = 0; index < source.field_size(); ++index) {
        const FieldDescriptorProto& field = source.field(index);
        DescriptorField descriptor_field;
        descriptor_field.name = field.name();
        descriptor_field.number = field.number();
        descriptor_field.declaration_order = static_cast<std::uint32_t>(index);
        descriptor_field.label = field_label(field.label());
        descriptor_field.type = field_type(field.type());
        descriptor_field.type_name = without_leading_dot(field.type_name());
        descriptor_field.oneof_index = field.has_oneof_index() ? field.oneof_index() : -1;
        descriptor_field.proto3_optional = field.proto3_optional();
        descriptor_field.packed = field.options().packed();
        descriptor_field.has_default_value = field.has_default_value();
        message.fields.push_back(std::move(descriptor_field));
    }
    for (const auto& nested_enum : source.enum_type()) {
        message.nested_enums.push_back(qualify(message.fully_qualified_name, nested_enum.name()));
        collect_enum(nested_enum, message.fully_qualified_name, file_name,
                     message.fully_qualified_name, model, file);
    }
    for (const auto& nested_message : source.nested_type()) {
        message.nested_messages.push_back(
            qualify(message.fully_qualified_name, nested_message.name()));
        collect_message(nested_message, message.fully_qualified_name, file_name,
                        message.fully_qualified_name, model, file);
    }
    message.map_entry = source.options().map_entry();
    for (const auto& extension : source.extension()) {
        message.extensions.push_back(qualify(message.fully_qualified_name, extension.name()));
    }
    model.messages.push_back(std::move(message));
    file.messages.push_back(qualify(prefix, source.name()));
}

[[nodiscard]] std::string quote(std::string_view value) {
    std::string result = "\"";
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            result.push_back('\\');
        }
        if (character == '\n') {
            result += "\\n";
        } else if (character == '\r') {
            result += "\\r";
        } else {
            result.push_back(character);
        }
    }
    result.push_back('"');
    return result;
}

void sort_model(DescriptorModel& model) {
    std::sort(model.files.begin(), model.files.end(),
              [](const DescriptorFile& lhs, const DescriptorFile& rhs) {
                  return lhs.name < rhs.name;
              });
    for (DescriptorFile& file : model.files) {
        std::sort(file.imports.begin(), file.imports.end());
        std::sort(file.public_imports.begin(), file.public_imports.end());
        std::sort(file.weak_imports.begin(), file.weak_imports.end());
        std::sort(file.messages.begin(), file.messages.end());
        std::sort(file.enums.begin(), file.enums.end());
        std::sort(file.services.begin(), file.services.end());
        std::sort(file.extensions.begin(), file.extensions.end());
    }
    std::sort(model.packages.begin(), model.packages.end());
    model.packages.erase(std::unique(model.packages.begin(), model.packages.end()),
                         model.packages.end());
    std::sort(model.messages.begin(), model.messages.end(),
              [](const DescriptorMessage& lhs, const DescriptorMessage& rhs) {
                  return lhs.fully_qualified_name < rhs.fully_qualified_name;
              });
    for (DescriptorMessage& message : model.messages) {
        std::sort(message.nested_messages.begin(), message.nested_messages.end());
        std::sort(message.nested_enums.begin(), message.nested_enums.end());
        std::sort(message.extensions.begin(), message.extensions.end());
                std::sort(message.fields.begin(), message.fields.end(),
                  [](const DescriptorField& lhs, const DescriptorField& rhs) {
                      if (lhs.declaration_order != rhs.declaration_order) {
                          return lhs.declaration_order < rhs.declaration_order;
                      }
                      if (lhs.number != rhs.number) return lhs.number < rhs.number;
                      return lhs.name < rhs.name;
                  });
    }
    std::sort(model.enums.begin(), model.enums.end(),
              [](const DescriptorEnum& lhs, const DescriptorEnum& rhs) {
                  return lhs.fully_qualified_name < rhs.fully_qualified_name;
              });
    for (DescriptorEnum& enumeration : model.enums) {
        std::sort(enumeration.values.begin(), enumeration.values.end(),
                  [](const DescriptorEnumValue& lhs, const DescriptorEnumValue& rhs) {
                      if (lhs.number != rhs.number) {
                          return lhs.number < rhs.number;
                      }
                      return lhs.name < rhs.name;
                  });
    }
}

} // namespace

std::string field_type_name(FieldType type) {
    switch (type) {
    case FieldType::Double: return "double";
    case FieldType::Float: return "float";
    case FieldType::Int64: return "int64";
    case FieldType::Uint64: return "uint64";
    case FieldType::Int32: return "int32";
    case FieldType::Fixed64: return "fixed64";
    case FieldType::Fixed32: return "fixed32";
    case FieldType::Bool: return "bool";
    case FieldType::String: return "string";
    case FieldType::Group: return "group";
    case FieldType::Message: return "message";
    case FieldType::Bytes: return "bytes";
    case FieldType::Uint32: return "uint32";
    case FieldType::Enum: return "enum";
    case FieldType::Sfixed32: return "sfixed32";
    case FieldType::Sfixed64: return "sfixed64";
    case FieldType::Sint32: return "sint32";
    case FieldType::Sint64: return "sint64";
    case FieldType::Unknown: return "unknown";
    }
    return "unknown";
}

std::string field_label_name(FieldLabel label) {
    switch (label) {
    case FieldLabel::Optional: return "optional";
    case FieldLabel::Required: return "required";
    case FieldLabel::Repeated: return "repeated";
    }
    return "optional";
}

DescriptorLoadResult load_descriptor_set(const std::string& path) {
    DescriptorLoadResult result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        add_diagnostic(result, "cannot open descriptor set '" + path + "'");
        return result;
    }

    FileDescriptorSet descriptor_set;
    if (!descriptor_set.ParseFromIstream(&input)) {
        add_diagnostic(result, "descriptor set '" + path + "' is malformed");
        return result;
    }
    if (descriptor_set.file().empty()) {
        add_diagnostic(result, "descriptor set '" + path + "' contains no files");
        return result;
    }

    DescriptorModel model;
    std::set<std::string> file_names;
    std::map<std::string, std::string> declaration_kinds;

    for (const FileDescriptorProto& source : descriptor_set.file()) {
        if (source.name().empty()) {
            add_diagnostic(result, "descriptor set contains a file with an empty name");
            continue;
        }
        if (!file_names.insert(source.name()).second) {
            add_diagnostic(result, "descriptor set contains duplicate file '" + source.name() + "'");
            continue;
        }
        if (!source.syntax().empty() && source.syntax() != "proto2" && source.syntax() != "proto3") {
            add_diagnostic(result, "file '" + source.name() + "' has unsupported syntax '" +
                                     source.syntax() + "'");
        }
        DescriptorFile file;
        file.name = source.name();
        file.package = source.package();
        file.syntax = source.syntax();
        for (const std::string& dependency : source.dependency()) {
            file.imports.push_back(dependency);
        }
        for (const auto& message : source.message_type()) {
            collect_message(message, source.package(), source.name(), "", model, file);
        }
        for (const auto& enumeration : source.enum_type()) {
            collect_enum(enumeration, source.package(), source.name(), "", model, file);
        }
        for (const auto& service : source.service()) {
            file.services.push_back(qualify(source.package(), service.name()));
        }
        for (const auto& extension : source.extension()) {
            file.extensions.push_back(qualify(source.package(), extension.name()));
        }
        for (const int index : source.public_dependency()) {
            if (index >= 0 && index < source.dependency_size()) {
                file.public_imports.push_back(source.dependency(index));
            }
        }
        for (const int index : source.weak_dependency()) {
            if (index >= 0 && index < source.dependency_size()) {
                file.weak_imports.push_back(source.dependency(index));
            }
        }
        model.files.push_back(std::move(file));
        if (!source.package().empty()) {
            model.packages.push_back(source.package());
        }
    }

    for (const DescriptorFile& file : model.files) {
        std::set<std::string> imports;
        for (const std::string& dependency : file.imports) {
            if (!imports.insert(dependency).second) {
                add_diagnostic(result, "file '" + file.name + "' imports '" + dependency +
                                         "' more than once");
            }
            if (!file_names.contains(dependency)) {
                add_diagnostic(result, "file '" + file.name + "' imports missing file '" +
                                         dependency + "'");
            }
        }
    }

    for (const DescriptorMessage& message : model.messages) {
        if (!declaration_kinds.emplace(message.fully_qualified_name, "message").second) {
            add_diagnostic(result, "duplicate declaration '" + message.fully_qualified_name + "'");
        }
    }
    for (const DescriptorEnum& enumeration : model.enums) {
        if (!declaration_kinds.emplace(enumeration.fully_qualified_name, "enum").second) {
            add_diagnostic(result, "duplicate declaration '" + enumeration.fully_qualified_name + "'");
        }
    }

    for (const DescriptorMessage& message : model.messages) {
        std::set<std::string> field_names;
        std::set<std::int32_t> field_numbers;
        for (const DescriptorField& field : message.fields) {
            if (field.number <= 0) {
                add_diagnostic(result, "message '" + message.fully_qualified_name +
                                         "' has invalid field number " +
                                         std::to_string(field.number));
            }
            if (!field_names.insert(field.name).second) {
                add_diagnostic(result, "message '" + message.fully_qualified_name +
                                         "' contains duplicate field '" + field.name + "'");
            }
            if (!field_numbers.insert(field.number).second) {
                add_diagnostic(result, "message '" + message.fully_qualified_name +
                                         "' reuses field number " + std::to_string(field.number));
            }
            if (field.type == FieldType::Message || field.type == FieldType::Enum ||
                field.type == FieldType::Group) {
                if (field.type_name.empty()) {
                    add_diagnostic(result, "field '" + message.fully_qualified_name + "." +
                                             field.name + "' has no referenced type name");
                } else if (!declaration_kinds.contains(field.type_name)) {
                    add_diagnostic(result, "field '" + message.fully_qualified_name + "." +
                                             field.name + "' references unknown type '" +
                                             field.type_name + "'");
                }
            }
        }
    }

    if (result.diagnostics.empty()) {
        google::protobuf::DescriptorPool pool(google::protobuf::DescriptorPool::generated_pool());
        std::map<std::string, FileDescriptorProto> pending;
        for (const FileDescriptorProto& source : descriptor_set.file()) {
            pending.emplace(source.name(), source);
        }
        while (!pending.empty()) {
            bool progress = false;
            for (auto iterator = pending.begin(); iterator != pending.end();) {
                if (pool.FindFileByName(iterator->first) != nullptr ||
                    pool.BuildFile(iterator->second) != nullptr) {
                    iterator = pending.erase(iterator);
                    progress = true;
                } else {
                    ++iterator;
                }
            }
            if (!progress) {
                for (const auto& [name, unused] : pending) {
                    add_diagnostic(result, "cannot resolve descriptor dependencies for option file '" +
                                             name + "'");
                }
                break;
            }
        }
        if (result.diagnostics.empty()) {
            apply_custom_options(result, pool, descriptor_set, model);
        }
    }

    sort_model(model);
    if (result.diagnostics.empty()) {
        result.model = std::move(model);
    }
    return result;
}

std::string render_descriptor_list(const DescriptorModel& model) {
    std::ostringstream output;
    output << "files:\n";
    for (const DescriptorFile& file : model.files) {
        output << "  - name: " << quote(file.name) << "\n"
               << "    package: " << quote(file.package) << "\n"
               << "    syntax: " << quote(file.syntax) << "\n"
               << "    imports:\n";
        for (const std::string& import : file.imports) {
            output << "      - " << quote(import) << "\n";
        }
        output << "    public_imports:\n";
        for (const std::string& import : file.public_imports) {
            output << "      - " << quote(import) << "\n";
        }
        output << "    weak_imports:\n";
        for (const std::string& import : file.weak_imports) {
            output << "      - " << quote(import) << "\n";
        }
        output << "    messages:\n";
        for (const std::string& message : file.messages) {
            output << "      - " << quote(message) << "\n";
        }
        output << "    enums:\n";
        for (const std::string& enumeration : file.enums) {
            output << "      - " << quote(enumeration) << "\n";
        }
        output << "    services:\n";
        for (const std::string& service : file.services) {
            output << "      - " << quote(service) << "\n";
        }
        output << "    extensions:\n";
        for (const std::string& extension : file.extensions) {
            output << "      - " << quote(extension) << "\n";
        }
    }
    output << "packages:\n";
    for (const std::string& package : model.packages) {
        output << "  - " << quote(package) << "\n";
    }
    output << "messages:\n";
    for (const DescriptorMessage& message : model.messages) {
        output << "  - name: " << quote(message.fully_qualified_name) << "\n"
               << "    file: " << quote(message.file_name) << "\n"
               << "    containing_message: " << quote(message.containing_message) << "\n"
               << "    nested_messages:\n";
        for (const std::string& nested : message.nested_messages) {
            output << "      - " << quote(nested) << "\n";
        }
        output << "    nested_enums:\n";
        for (const std::string& nested : message.nested_enums) {
            output << "      - " << quote(nested) << "\n";
        }
        output << "    map_entry: " << (message.map_entry ? "true" : "false") << "\n"
               << "    extensions:\n";
        for (const std::string& extension : message.extensions) {
            output << "      - " << quote(extension) << "\n";
        }
        output << "    fields:\n";
        for (const DescriptorField& field : message.fields) {
            output << "      - name: " << quote(field.name) << "\n"
                   << "        number: " << field.number << "\n"
                   << "        declaration_order: " << field.declaration_order << "\n"
                   << "        label: " << field_label_name(field.label) << "\n"
                   << "        type: " << field_type_name(field.type) << "\n"
                   << "        type_name: " << quote(field.type_name) << "\n"
                   << "        oneof_index: " << field.oneof_index << "\n"
                   << "        proto3_optional: " << (field.proto3_optional ? "true" : "false")
                   << "\n"
                   << "        packed: " << (field.packed ? "true" : "false") << "\n"
                   << "        has_default_value: "
                   << (field.has_default_value ? "true" : "false") << "\n";
        }
    }
    output << "enums:\n";
    for (const DescriptorEnum& enumeration : model.enums) {
        output << "  - name: " << quote(enumeration.fully_qualified_name) << "\n"
               << "    file: " << quote(enumeration.file_name) << "\n"
               << "    containing_message: " << quote(enumeration.containing_message) << "\n"
               << "    values:\n";
        for (const DescriptorEnumValue& value : enumeration.values) {
            output << "      - name: " << quote(value.name) << "\n"
                   << "        number: " << value.number << "\n";
        }
    }
    return output.str();
}

} // namespace quarry::tools::protobuf
