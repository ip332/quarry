#include "translation.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace quarry::tools::protobuf {
namespace {

struct MappedMessage {
    std::string protobuf_name;
    std::string protobuf_file;
    std::string quarry_namespace;
    std::string quarry_name;
    std::filesystem::path output;
};

struct MappedEnum {
    std::string protobuf_name;
    std::string protobuf_file;
    std::string quarry_namespace;
    std::string quarry_name;
    std::string owner_message;
};

struct RenderedMessage {
    MappedMessage mapped;
    std::vector<std::string> dependencies;
    std::vector<std::string> owned_enums;
    std::string contents;
};

void add_diagnostic(TranslationResult& result, std::string message) {
    result.diagnostics.push_back(std::move(message));
}

[[nodiscard]] std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
                                        [](unsigned char character) { return std::isspace(character); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(),
                                       [](unsigned char character) { return std::isspace(character); })
                          .base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

[[nodiscard]] std::string strip_comment(std::string value) {
    const std::size_t comment = value.find('#');
    if (comment != std::string::npos) {
        value.resize(comment);
    }
    return trim(std::move(value));
}

[[nodiscard]] bool split_key_value(const std::string& text, std::string& key,
                                   std::string& value) {
    const std::size_t separator = text.find(':');
    if (separator == std::string::npos) {
        return false;
    }
    key = trim(text.substr(0, separator));
    value = strip_comment(text.substr(separator + 1));
    return !key.empty();
}

[[nodiscard]] bool parse_positive_u32(const std::string& text, std::uint32_t& value) {
    if (text.empty() || text.front() == '-') {
        return false;
    }
    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed == 0 ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

[[nodiscard]] std::map<std::string, BoundEntry> bounds_map(const BoundsConfig& config) {
    std::map<std::string, BoundEntry> result;
    for (const auto& entry : config.entries) {
        result.emplace(entry.path, entry);
    }
    return result;
}

void apply_bounds_layer(FieldBounds& target, ResolvedBounds& provenance,
                        const FieldBounds& layer, std::string label,
                        std::string source, std::string source_type,
                        bool override_existing = false) {
    const bool had_value = target.max_bytes.has_value() || target.max_elements.has_value();
    provenance.override_chain.push_back(label);
    if (layer.max_bytes.has_value() && (override_existing || !target.max_bytes.has_value())) {
        target.max_bytes = layer.max_bytes;
        provenance.source = std::move(source);
        provenance.source_type = std::move(source_type);
    }
    if (layer.max_elements.has_value() &&
        (override_existing || !target.max_elements.has_value())) {
        target.max_elements = layer.max_elements;
        if (!had_value && provenance.source.empty()) {
            provenance.source = std::move(source);
            provenance.source_type = std::move(source_type);
        }
    }
}

void apply_external_bounds(FieldBounds& target, ResolvedBounds& provenance,
                           const BoundEntry& entry) {
    apply_bounds_layer(target, provenance, entry.values,
                       "external:" + entry.path, entry.source,
                       entry.original_option.empty() ? "external_yaml" : "nanopb_options");
    if (!entry.original_option.empty()) {
        provenance.original_option = entry.original_option;
    }
    provenance.source_line = entry.source_line;
}

[[nodiscard]] std::vector<std::string> split_components(std::string_view value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find('.', begin);
        const std::size_t length =
            end == std::string_view::npos ? value.size() - begin : end - begin;
        result.emplace_back(value.substr(begin, length));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

[[nodiscard]] std::string lower_segment(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

[[nodiscard]] bool valid_quarry_component(std::string_view value) {
    if (value.empty() ||
        !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_')) {
        return false;
    }
    if (!std::all_of(value.begin() + 1, value.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '_';
    })) {
        return false;
    }
    static constexpr std::string_view keywords[] = {
        "import", "namespace", "record", "enum", "true", "false", "bool", "u8", "u16",
        "u32", "u64", "i8", "i16", "i32", "i64", "f32", "f64", "string", "bytes"};
    return std::none_of(std::begin(keywords), std::end(keywords),
                        [value](std::string_view keyword) { return keyword == value; });
}

[[nodiscard]] bool valid_enum_value_name(std::string_view value) {
    return valid_quarry_component(value);
}

[[nodiscard]] std::optional<MappedMessage> map_message(const DescriptorMessage& message,
                                                        TranslationResult& result) {
    const std::vector<std::string> components = split_components(message.fully_qualified_name);
    if (components.empty() || components.back().empty()) {
        add_diagnostic(result, "cannot map protobuf message '" + message.fully_qualified_name +
                                 "': empty declaration component");
        return std::nullopt;
    }
    std::vector<std::string> namespace_components;
    namespace_components.reserve(components.size());
    for (const std::string& component : components) {
        const std::string mapped = lower_segment(component);
        if (!valid_quarry_component(mapped)) {
            add_diagnostic(result, "cannot map protobuf message '" + message.fully_qualified_name +
                                     "': component '" + component +
                                     "' is not a valid Quarry name");
            return std::nullopt;
        }
        namespace_components.push_back(mapped);
    }
    const std::string record = components.back();
    std::string quarry_namespace;
    for (std::size_t index = 0; index < namespace_components.size(); ++index) {
        if (!quarry_namespace.empty()) {
            quarry_namespace += '.';
        }
        quarry_namespace += namespace_components[index];
    }
    std::filesystem::path output;
    for (const std::string& component : split_components(quarry_namespace)) {
        output /= component;
    }
    output /= lower_segment(record) + ".brd";
    return MappedMessage{message.fully_qualified_name, message.file_name, quarry_namespace, record,
                         output};
}

[[nodiscard]] const DescriptorMessage* find_message(const DescriptorModel& model,
                                                     std::string_view name) {
    const auto iterator = std::find_if(model.messages.begin(), model.messages.end(),
                                       [name](const DescriptorMessage& message) {
                                           return message.fully_qualified_name == name;
                                       });
    return iterator == model.messages.end() ? nullptr : &*iterator;
}

[[nodiscard]] std::string relative_import(const std::filesystem::path& from,
                                           const std::filesystem::path& to) {
    return std::filesystem::relative(to, from.parent_path()).generic_string();
}

[[nodiscard]] std::string mapped_type(const DescriptorField& field, const MappedMessage& owner,
                                      const std::map<std::string, MappedMessage>& mappings,
                                      const std::map<std::string, MappedEnum>& enum_mappings,
                                      TranslationResult& result) {
    std::string type;
    switch (field.type) {
    case FieldType::Bool: type = "bool"; break;
    case FieldType::Int32:
    case FieldType::Sint32:
    case FieldType::Sfixed32: type = "i32"; break;
    case FieldType::Uint32:
    case FieldType::Fixed32: type = "u32"; break;
    case FieldType::Int64:
    case FieldType::Sint64:
    case FieldType::Sfixed64: type = "i64"; break;
    case FieldType::Uint64:
    case FieldType::Fixed64: type = "u64"; break;
    case FieldType::Float: type = "f32"; break;
    case FieldType::Double: type = "f64"; break;
    case FieldType::String: type = "string"; break;
    case FieldType::Bytes: type = "bytes"; break;
    case FieldType::Message: {
        const auto iterator = mappings.find(field.type_name);
        if (iterator == mappings.end()) {
            add_diagnostic(result, "field '" + owner.protobuf_name + "." + field.name +
                                     "' references unmapped message '" + field.type_name + "'");
            return {};
        }
        if (iterator->second.quarry_namespace == owner.quarry_namespace) {
            type = iterator->second.quarry_name;
        } else {
            type = iterator->second.quarry_namespace + "." + iterator->second.quarry_name;
        }
        break;
    }
    case FieldType::Enum: {
        const auto iterator = enum_mappings.find(field.type_name);
        if (iterator == enum_mappings.end()) {
            add_diagnostic(result, "field '" + owner.protobuf_name + "." + field.name +
                                     "' references unmapped enum '" + field.type_name + "'");
            return {};
        }
        if (iterator->second.quarry_namespace == owner.quarry_namespace) {
            type = iterator->second.quarry_name;
        } else {
            type = iterator->second.quarry_namespace + "." + iterator->second.quarry_name;
        }
        break;
    }
    default:
        add_diagnostic(result, "field '" + owner.protobuf_name + "." + field.name +
                                 "' uses unsupported protobuf type '" +
                                 field_type_name(field.type) + "'");
        return {};
    }
    if (field.label == FieldLabel::Repeated) {
        type += "[]";
    }
    return type;
}

[[nodiscard]] std::string render_message(const DescriptorMessage& message,
                                         const MappedMessage& mapped,
                                         const std::map<std::string, MappedMessage>& mappings,
                                         const std::map<std::string, MappedEnum>& enum_mappings,
                                         const std::map<std::string, DescriptorEnum>& enums,
                                         const std::map<std::string, FieldBounds>& bounds,
                                         const std::vector<std::string>& owned_enums,
                                         const std::vector<std::string>& dependencies,
                                         TranslationResult& result) {
    std::ostringstream output;
    output << "namespace: " << mapped.quarry_namespace << "\n"
           << "record: " << mapped.quarry_name << "\n"
           << "version: 1\n"
           << "type: data\n";
    if (!dependencies.empty()) {
        output << "imports:\n";
        for (const std::string& dependency : dependencies) {
            output << "  - " << dependency << "\n";
        }
    }
    if (!owned_enums.empty()) {
        output << "enums:\n";
        for (const std::string& enum_name : owned_enums) {
            const DescriptorEnum& enumeration = enums.at(enum_name);
            output << "  " << enum_mappings.at(enum_name).quarry_name << ":\n"
                   << "    values:\n";
            for (const DescriptorEnumValue& value : enumeration.values) {
                output << "      " << value.name << ": " << value.number << "\n";
            }
        }
    }
    if (message.fields.empty()) {
        output << "fields: {}\n";
        return output.str();
    }
    output << "fields:\n";
    for (const DescriptorField& field : message.fields) {
        const std::string field_path = message.fully_qualified_name + "." + field.name;
        const std::string type = mapped_type(field, mapped, mappings, enum_mappings, result);
        output << "  " << field.name << ":\n"
               << "    type: " << type << "\n";
        const auto bound = bounds.find(field_path);
        if ((field.type == FieldType::String || field.type == FieldType::Bytes) &&
            bound != bounds.end() && bound->second.max_bytes.has_value()) {
            output << "    max_bytes: " << *bound->second.max_bytes << "\n";
        }
        if (field.label == FieldLabel::Repeated && bound != bounds.end() &&
            bound->second.max_elements.has_value()) {
            output << "    max_elements: " << *bound->second.max_elements << "\n";
        }
    }
    return output.str();
}

[[nodiscard]] std::string json_escape(std::string_view value) {
    std::string result;
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            result.push_back('\\');
        }
        if (character == '\n') {
            result += "\\n";
        } else {
            result.push_back(character);
        }
    }
    return result;
}

[[nodiscard]] std::string render_manifest(const std::string& root,
                                          const std::vector<RenderedMessage>& messages,
                                          const std::map<std::string, ResolvedBounds>& bounds,
                                          const std::map<std::string, MappedEnum>& enum_mappings,
                                          const std::map<std::string, DescriptorEnum>& enums,
                                          const std::map<std::string, MappedMessage>& mappings,
                                          const DescriptorModel& model) {
    std::ostringstream output;
    output << "{\n  \"protobuf_root\": \"" << json_escape(root) << "\",\n"
           << "  \"quarry_root\": \""
           << json_escape(messages.back().mapped.quarry_namespace + "." +
                          messages.back().mapped.quarry_name)
           << "\",\n  \"files\": [\n";
    for (std::size_t index = 0; index < messages.size(); ++index) {
        const RenderedMessage& message = messages[index];
        output << "    {\n      \"protobuf\": \""
               << json_escape(message.mapped.protobuf_name)
               << "\",\n      \"quarry\": \""
               << json_escape(message.mapped.quarry_namespace + "." +
                              message.mapped.quarry_name)
               << "\",\n      \"output\": \""
               << json_escape(message.mapped.output.generic_string())
               << "\",\n      \"dependencies\": [";
        for (std::size_t dep = 0; dep < message.dependencies.size(); ++dep) {
            if (dep != 0) output << ", ";
            output << "\"" << json_escape(message.dependencies[dep]) << "\"";
        }
        output << "],\n      \"owned_enums\": [";
        for (std::size_t enum_index = 0; enum_index < message.owned_enums.size(); ++enum_index) {
            if (enum_index != 0) output << ", ";
            const auto& mapped_enum = enum_mappings.at(message.owned_enums[enum_index]);
            output << "\"" << json_escape(mapped_enum.protobuf_name) << "\"";
        }
        output << "]\n    }" << (index + 1 == messages.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"explicit_roots\": [\n";
    for (std::size_t index = 0; index < messages.size(); ++index) {
        output << "    \"" << json_escape(messages[index].mapped.output.generic_string()) << "\""
               << (index + 1 == messages.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"bounds\": [\n";
    std::size_t index = 0;
    for (const auto& [path, field_bounds] : bounds) {
        output << "    {\"field\": \"" << json_escape(path) << "\"";
        if (field_bounds.values.max_bytes.has_value()) {
            output << ", \"max_bytes\": " << *field_bounds.values.max_bytes;
        }
        if (field_bounds.values.max_elements.has_value()) {
            output << ", \"max_elements\": " << *field_bounds.values.max_elements;
        }
        output << ", \"source\": \"" << json_escape(field_bounds.source)
               << "\", \"source_type\": \"" << json_escape(field_bounds.source_type)
               << "\", \"override_chain\": [";
        for (std::size_t chain_index = 0; chain_index < field_bounds.override_chain.size(); ++chain_index) {
            if (chain_index != 0) output << ", ";
            output << "\"" << json_escape(field_bounds.override_chain[chain_index]) << "\"";
        }
        output << "]";
        if (field_bounds.source_line != 0) {
            output << ", \"source_line\": " << field_bounds.source_line
                   << ", \"source_column\": " << field_bounds.source_column;
        }
        if (!field_bounds.original_option.empty()) {
            output << ", \"original_option\": \""
                   << json_escape(field_bounds.original_option) << "\"";
        }
        output << "}" << (++index == bounds.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"declarations\": [\n";
    std::vector<std::string> declaration_records;
    for (const auto& [name, mapped] : mappings) {
        declaration_records.push_back(
            "    {\"protobuf\": \"" + json_escape(name) +
            "\", \"file\": \"" + json_escape(mapped.protobuf_file) +
            "\", \"kind\": \"message\", \"quarry\": \"" +
            json_escape(mapped.quarry_namespace + "." + mapped.quarry_name) +
            "\", \"output\": \"" + json_escape(mapped.output.generic_string()) + "\"}");
    }
    for (const auto& [name, mapped] : enum_mappings) {
        std::ostringstream declaration;
        declaration << "    {\"protobuf\": \"" << json_escape(name)
                    << "\", \"file\": \"" << json_escape(mapped.protobuf_file)
                    << "\", \"kind\": \"enum\", \"quarry\": \""
                    << json_escape(mapped.quarry_namespace + "." + mapped.quarry_name)
                    << "\", \"owner\": \"" << json_escape(mapped.owner_message)
                    << "\", \"output\": \""
                    << json_escape(mappings.at(mapped.owner_message).output.generic_string())
                    << "\", \"values\": [";
        const auto& values = enums.at(name).values;
        for (std::size_t value_index = 0; value_index < values.size(); ++value_index) {
            if (value_index != 0) declaration << ", ";
            declaration << "{\"name\": \"" << json_escape(values[value_index].name)
                        << "\", \"number\": " << values[value_index].number << "}";
        }
        declaration << "]}";
        declaration_records.push_back(declaration.str());
    }
    for (std::size_t index = 0; index < declaration_records.size(); ++index) {
        output << declaration_records[index]
               << (index + 1 == declaration_records.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"fields\": [\n";
    std::vector<std::string> field_records;
    for (const auto& [message_name, mapped] : mappings) {
        const auto message = std::find_if(model.messages.begin(), model.messages.end(),
                                          [&](const DescriptorMessage& candidate) {
                                              return candidate.fully_qualified_name == message_name;
                                          });
        if (message == model.messages.end()) continue;
        for (const auto& field : message->fields) {
            const std::string path = message_name + "." + field.name;
            std::ostringstream entry;
            entry << "    {\"message\": \"" << json_escape(message_name)
                  << "\", \"name\": \"" << json_escape(field.name)
                  << "\", \"number\": " << field.number
                  << ", \"kind\": \"" << json_escape(field_type_name(field.type))
                  << "\", \"repeated\": "
                  << (field.label == FieldLabel::Repeated ? "true" : "false")
                  << ", \"quarry\": \"" << json_escape(mapped.quarry_namespace + "." +
                                                              mapped.quarry_name + "." + field.name)
                  << "\", \"ordinal\": " << field.declaration_order;
            const auto bound = bounds.find(path);
            if (bound != bounds.end()) {
                if (bound->second.values.max_bytes) entry << ", \"max_bytes\": " << *bound->second.values.max_bytes;
                if (bound->second.values.max_elements) entry << ", \"max_elements\": " << *bound->second.values.max_elements;
            }
            entry << "}";
            field_records.push_back(entry.str());
        }
    }
    for (std::size_t index = 0; index < field_records.size(); ++index) {
        output << field_records[index] << (index + 1 == field_records.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    return output.str();
}

} // namespace

TranslationResult load_bounds_config(const std::string& path, BoundsConfig& config) {
    TranslationResult result;
    std::ifstream input(path);
    if (!input) {
        add_diagnostic(result, "cannot open bounds file '" + path + "'");
        return result;
    }
    std::string line;
    std::string current_path;
    std::map<std::string, BoundEntry> parsed;
    bool saw_root = false;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.find('\t') != std::string::npos) {
            add_diagnostic(result, "bounds file line " + std::to_string(line_number) +
                                     " uses tabs; use two-space indentation");
            continue;
        }
        const std::string content = strip_comment(line);
        if (content.empty()) continue;
        const std::size_t indent = line.find_first_not_of(' ');
        std::string key;
        std::string value;
        if (!split_key_value(content, key, value)) {
            add_diagnostic(result, "bounds file line " + std::to_string(line_number) +
                                     " is not a key/value entry");
            continue;
        }
        if (indent == 0) {
            if (key != "bounds" || !value.empty() || saw_root) {
                add_diagnostic(result, "bounds file line " + std::to_string(line_number) +
                                         " must contain exactly one 'bounds:' root");
            } else {
                saw_root = true;
            }
            continue;
        }
        if (!saw_root || (indent != 2 && indent != 4)) {
            add_diagnostic(result, "bounds file line " + std::to_string(line_number) +
                                     " has invalid indentation");
            continue;
        }
        if (indent == 2 && value.empty()) {
            current_path = key;
            BoundEntry entry;
            entry.path = current_path;
            entry.source = std::filesystem::path(path).filename().generic_string();
            if (!parsed.emplace(current_path, std::move(entry)).second) {
                add_diagnostic(result, "bounds file contains duplicate field path '" +
                                         current_path + "'");
            }
            continue;
        }
        if (indent != 4 || current_path.empty() || value.empty()) {
            add_diagnostic(result, "bounds file line " + std::to_string(line_number) +
                                     " has an invalid field-bound entry");
            continue;
        }
        auto iterator = parsed.find(current_path);
        if (iterator == parsed.end()) {
            add_diagnostic(result, "bounds file line " + std::to_string(line_number) +
                                     " has no enclosing field path");
            continue;
        }
        std::uint32_t parsed_value = 0;
        if ((key != "max_bytes" && key != "max_elements") ||
            !parse_positive_u32(value, parsed_value)) {
            add_diagnostic(result, "bounds file line " + std::to_string(line_number) +
                                     " has invalid " + key + " value '" + value + "'");
            continue;
        }
        std::optional<std::uint32_t>& destination =
            key == "max_bytes" ? iterator->second.values.max_bytes : iterator->second.values.max_elements;
        if (destination.has_value()) {
            add_diagnostic(result, "bounds file contains duplicate " + key + " for '" +
                                     current_path + "'");
        } else {
            destination = parsed_value;
        }
    }
    if (!saw_root) {
        add_diagnostic(result, "bounds file is missing the 'bounds:' root");
    }
    if (result.succeeded()) {
        config.entries.clear();
        for (auto& entry : parsed) {
            config.entries.push_back(std::move(entry.second));
        }
    }
    return result;
}

[[nodiscard]] bool parse_nanopb_assignment(std::string_view token, std::string& name,
                                           std::uint32_t& value) {
    const std::size_t separator = token.find(':');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 == token.size()) {
        return false;
    }
    name = std::string(token.substr(0, separator));
    return parse_positive_u32(std::string(token.substr(separator + 1)), value);
}

TranslationResult load_nanopb_config(const std::string& path, BoundsConfig& config) {
    TranslationResult result;
    std::ifstream input(path);
    if (!input) {
        add_diagnostic(result, "cannot open Nanopb options file '" + path + "'");
        return result;
    }
    std::map<std::string, BoundEntry> parsed;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string content = strip_comment(line);
        if (content.empty()) continue;
        std::istringstream tokens(content);
        std::string field_path;
        tokens >> field_path;
        if (field_path.empty() || field_path.find_first_of("*?[]") != std::string::npos) {
            add_diagnostic(result, "Nanopb options file '" + path + "' line " +
                                     std::to_string(line_number) +
                                     " requires an exact field path; wildcards are unsupported");
            continue;
        }
        if (!field_path.empty() && field_path.front() == '.') field_path.erase(0, 1);
        std::string option;
        bool saw_assignment = false;
        BoundEntry entry;
        entry.path = field_path;
        entry.source = std::filesystem::path(path).filename().generic_string();
        entry.source_line = static_cast<std::uint32_t>(line_number);
        while (tokens >> option) {
            std::string name;
            std::uint32_t value = 0;
            if (!parse_nanopb_assignment(option, name, value)) {
                add_diagnostic(result, "Nanopb options file '" + path + "' line " +
                                         std::to_string(line_number) + " has malformed option '" +
                                         option + "'");
                continue;
            }
            if (name == "max_count") {
                if (entry.values.max_elements.has_value()) {
                    add_diagnostic(result, "Nanopb options file '" + path + "' line " +
                                             std::to_string(line_number) +
                                             " contains duplicate max_count for '" + field_path + "'");
                } else {
                    entry.values.max_elements = value;
                    saw_assignment = true;
                }
            } else if (name == "max_size") {
                if (entry.values.max_bytes.has_value()) {
                    add_diagnostic(result, "Nanopb options file '" + path + "' line " +
                                             std::to_string(line_number) +
                                             " contains duplicate max_size for '" + field_path + "'");
                } else {
                    entry.values.max_bytes = value;
                    saw_assignment = true;
                }
            } else if (name == "max_length") {
                add_diagnostic(result, "Nanopb option max_length for '" + field_path +
                                         "' is unsupported: its unit and presence semantics are not "
                                         "identical to Quarry max_bytes; use max_size or Quarry bounds");
            } else {
                add_diagnostic(result, "Nanopb option '" + name + "' for '" + field_path +
                                         "' is unsupported by Quarry");
            }
        }
        if (saw_assignment) {
            entry.original_option = content.substr(content.find_first_of(" \t") + 1);
            auto [iterator, inserted] = parsed.emplace(field_path, std::move(entry));
            if (!inserted) {
                add_diagnostic(result, "Nanopb options file '" + path +
                                         "' contains duplicate rule for '" + field_path + "'");
            }
        } else {
            add_diagnostic(result, "Nanopb options file '" + path + "' line " +
                                     std::to_string(line_number) +
                                     " contains no supported bound assignment");
        }
    }
    if (!result.succeeded()) return result;
    config.entries.clear();
    for (auto& entry : parsed) config.entries.push_back(std::move(entry.second));
    return result;
}

[[nodiscard]] TranslationResult merge_options_file(const std::string& path, OptionsFormat format,
                                                   BoundsConfig& config) {
    BoundsConfig parsed;
    TranslationResult result = format == OptionsFormat::Nanopb
                                    ? load_nanopb_config(path, parsed)
                                    : load_bounds_config(path, parsed);
    if (!result.succeeded()) return result;
    std::map<std::string, BoundEntry> merged = bounds_map(config);
    for (const BoundEntry& entry : parsed.entries) {
        auto [iterator, inserted] = merged.emplace(entry.path, entry);
        if (inserted) continue;
        if (entry.values.max_bytes.has_value()) iterator->second.values.max_bytes = entry.values.max_bytes;
        if (entry.values.max_elements.has_value()) iterator->second.values.max_elements = entry.values.max_elements;
        iterator->second.source = entry.source;
        iterator->second.source_line = entry.source_line;
        if (!entry.original_option.empty()) {
            if (!iterator->second.original_option.empty()) iterator->second.original_option += " | ";
            iterator->second.original_option += entry.original_option;
        }
    }
    config.entries.clear();
    for (auto& entry : merged) config.entries.push_back(std::move(entry.second));
    return result;
}

TranslationResult load_options_config(const std::vector<std::string>& paths, OptionsFormat format,
                                      BoundsConfig& config) {
    TranslationResult result;
    config.entries.clear();
    for (const std::string& path : paths) {
        TranslationResult file_result = merge_options_file(path, format, config);
        result.diagnostics.insert(result.diagnostics.end(), file_result.diagnostics.begin(),
                                  file_result.diagnostics.end());
        if (!file_result.succeeded()) return result;
    }
    return result;
}

TranslationResult translate_descriptor_model(const DescriptorModel& model, const std::string& root,
                                             const std::string& bounds_path,
                                             const std::string& output_directory,
                                             const std::string& options_path) {
    std::vector<std::string> paths;
    if (!options_path.empty()) {
        paths.push_back(options_path);
    } else {
        paths.push_back(bounds_path);
    }
    return translate_descriptor_model(model, root, paths, OptionsFormat::Quarry,
                                      output_directory);
}

TranslationResult translate_descriptor_model(const DescriptorModel& model, const std::string& root,
                                             const std::vector<std::string>& options_paths,
                                             OptionsFormat options_format,
                                             const std::string& output_directory) {
    TranslationResult result;
    BoundsConfig config;
    TranslationResult bounds_result = load_options_config(options_paths, options_format, config);
    result.diagnostics = std::move(bounds_result.diagnostics);
    if (!result.succeeded()) return result;
    const std::map<std::string, BoundEntry> external_bounds = bounds_map(config);
    const DescriptorMessage* root_message = find_message(model, root);
    if (root_message == nullptr) {
        add_diagnostic(result, "unknown protobuf root message '" + root + "'");
        return result;
    }

    std::map<std::string, int> state;
    std::vector<std::string> stack;
    std::vector<std::string> order;
    std::set<std::string> used_bounds;
    std::map<std::string, ResolvedBounds> resolved_provenance;
    std::map<std::string, FieldBounds> bounds;
    std::map<std::string, MappedMessage> mappings;
    std::set<std::string> mapped_outputs;
    std::map<std::string, DescriptorEnum> reachable_enums;

    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (!result.succeeded()) return;
        if (state[name] == 1) {
            const auto cycle = std::find(stack.begin(), stack.end(), name);
            std::ostringstream diagnostic;
            diagnostic << "recursive protobuf message graph: ";
            for (auto iterator = cycle; iterator != stack.end(); ++iterator) {
                if (iterator != cycle) diagnostic << " -> ";
                diagnostic << *iterator;
            }
            diagnostic << " -> " << name;
            add_diagnostic(result, diagnostic.str());
            return;
        }
        if (state[name] == 2) return;
        const DescriptorMessage* message = find_message(model, name);
        if (message == nullptr) {
            add_diagnostic(result, "message reference '" + name +
                                     "' is not present in the descriptor set");
            return;
        }
        state[name] = 1;
        stack.push_back(name);
        if (!message->extensions.empty()) {
            add_diagnostic(result, "protobuf message '" + name +
                                     "' contains unsupported extensions");
        }
        const auto file = std::find_if(model.files.begin(), model.files.end(),
                                       [message](const DescriptorFile& candidate) {
                                           return candidate.name == message->file_name;
                                       });
        if (file != model.files.end() &&
            (!file->services.empty() || !file->extensions.empty() ||
             !file->public_imports.empty() || !file->weak_imports.empty())) {
            add_diagnostic(result, "protobuf file '" + file->name +
                                     "' contains unsupported services, extensions, or import semantics "
                                     "reachable from '" + name + "'");
        }
        std::set<std::string> dependencies;
        for (const DescriptorField& field : message->fields) {
            const std::string field_path = name + "." + field.name;
            if (field.label == FieldLabel::Required) {
                add_diagnostic(result, "field '" + field_path +
                                         "' uses unsupported proto2 required semantics");
            }
            if (field.oneof_index >= 0) {
                add_diagnostic(result, "field '" + field_path +
                                         "' uses unsupported oneof semantics");
            }
            if (field.proto3_optional) {
                add_diagnostic(result, "field '" + field_path +
                                         "' uses unsupported proto3 optional presence");
            }
            if (field.has_default_value) {
                add_diagnostic(result, "field '" + field_path +
                                         "' uses unsupported proto2 default semantics");
            }
            const bool needs_bytes = field.type == FieldType::String || field.type == FieldType::Bytes;
            const bool needs_elements = field.label == FieldLabel::Repeated;
            if (needs_bytes || needs_elements) {
                FieldBounds effective;
                ResolvedBounds provenance;
                if (const auto external = external_bounds.find(field_path); external != external_bounds.end()) {
                    used_bounds.insert(field_path);
                    apply_external_bounds(effective, provenance, external->second);
                    if (!needs_bytes && external->second.values.max_bytes.has_value()) {
                        add_diagnostic(result, "bounds entry for field '" + field_path +
                                         "' has max_bytes but the field is not string or bytes");
                    }
                    if (!needs_elements && external->second.values.max_elements.has_value()) {
                        add_diagnostic(result, "bounds entry for field '" + field_path +
                                         "' has max_elements but the field is not repeated");
                    }
                }
                if (file != model.files.end() && file->default_bounds.has_value()) {
                    FieldBounds defaults = *file->default_bounds;
                    if (!needs_bytes) defaults.max_bytes.reset();
                    if (!needs_elements) defaults.max_elements.reset();
                    apply_bounds_layer(effective, provenance, defaults,
                                       "file_default:" + file->name, file->name, "file_option", true);
                }
                if (message->default_bounds.has_value()) {
                    FieldBounds defaults = *message->default_bounds;
                    if (!needs_bytes) defaults.max_bytes.reset();
                    if (!needs_elements) defaults.max_elements.reset();
                    apply_bounds_layer(effective, provenance, defaults,
                                       "message_default:" + name, message->file_name,
                                       "message_option", true);
                }
                if (field.custom_bounds.has_value()) {
                    apply_bounds_layer(effective, provenance, *field.custom_bounds,
                                       "field_option:" + field_path, message->file_name,
                                       "field_option", true);
                    if (!needs_bytes && field.custom_bounds->max_bytes.has_value()) {
                        add_diagnostic(result, "field option for '" + field_path +
                                         "' has max_bytes but the field is not string or bytes");
                    }
                    if (!needs_elements && field.custom_bounds->max_elements.has_value()) {
                        add_diagnostic(result, "field option for '" + field_path +
                                         "' has max_elements but the field is not repeated");
                    }
                }
                if (effective.max_bytes.has_value() || effective.max_elements.has_value()) {
                    bounds[field_path] = effective;
                    provenance.values = effective;
                    resolved_provenance[field_path] = std::move(provenance);
                } else {
                    add_diagnostic(result, "missing bounds entry for field '" + field_path + "'");
                }
                if (needs_bytes && !effective.max_bytes.has_value()) {
                        add_diagnostic(result, "field '" + field_path + "' requires max_bytes");
                }
                if (needs_elements && !effective.max_elements.has_value()) {
                        add_diagnostic(result, "field '" + field_path + "' requires max_elements");
                }
            }
            if (field.type == FieldType::Enum) {
                const auto enumeration = std::find_if(
                    model.enums.begin(), model.enums.end(), [&](const DescriptorEnum& candidate) {
                        return candidate.fully_qualified_name == field.type_name;
                    });
                if (enumeration == model.enums.end()) {
                    add_diagnostic(result, "field '" + field_path + "' references unknown enum '" +
                                             field.type_name + "'");
                } else {
                    reachable_enums.emplace(enumeration->fully_qualified_name, *enumeration);
                }
            } else if (field.type == FieldType::Group) {
                add_diagnostic(result, "field '" + field_path +
                                         "' uses unsupported protobuf group type");
            } else if (field.type == FieldType::Message) {
                const DescriptorMessage* dependency = find_message(model, field.type_name);
                if (dependency == nullptr) {
                    add_diagnostic(result, "field '" + field_path + "' references unknown message '" +
                                             field.type_name + "'");
                } else if (field.type_name == "google.protobuf.Any") {
                    add_diagnostic(result, "field '" + field_path +
                                             "' uses unsupported google.protobuf.Any");
                } else if (dependency->map_entry) {
                    add_diagnostic(result, "field '" + field_path + "' uses unsupported protobuf map type '" +
                                             field.type_name + "'");
                } else {
                    dependencies.insert(field.type_name);
                }
            } else if (field.type == FieldType::Unknown) {
                add_diagnostic(result, "field '" + field_path +
                                         "' uses an unknown protobuf scalar type");
            }
        }
        for (const std::string& dependency : dependencies) visit(dependency);
        stack.pop_back();
        state[name] = 2;
        order.push_back(name);
    };
    visit(root);
    if (!result.succeeded()) return result;

    for (const auto& [path, unused] : external_bounds) {
        if (!used_bounds.contains(path)) {
            add_diagnostic(result, (options_format == OptionsFormat::Nanopb ?
                                   "Nanopb option path '" : "bounds entry '") + path +
                                     "' is unused by reachable message graph");
        }
    }
    if (!result.succeeded()) return result;

    for (const std::string& name : order) {
        const DescriptorMessage* message = find_message(model, name);
        const auto mapped = map_message(*message, result);
        if (!mapped.has_value()) continue;
        if (!mapped_outputs.insert(mapped->output.generic_string()).second) {
            add_diagnostic(result, "translated messages collide at output '" +
                                     mapped->output.generic_string() + "'");
        }
        mappings.emplace(name, *mapped);
    }
    if (!result.succeeded()) return result;

    std::map<std::string, std::string> enum_owners;
    for (const auto& [enum_name, enumeration] : reachable_enums) {
        std::string owner;
        if (!enumeration.containing_message.empty()) {
            if (mappings.contains(enumeration.containing_message)) {
                owner = enumeration.containing_message;
            }
        } else {
            for (const std::string& message_name : order) {
                const DescriptorMessage* message = find_message(model, message_name);
                const bool referenced = std::any_of(
                    message->fields.begin(), message->fields.end(), [&](const DescriptorField& field) {
                        return field.type == FieldType::Enum && field.type_name == enum_name;
                    });
                if (referenced) {
                    owner = message_name;
                    break;
                }
            }
        }
        if (owner.empty()) {
            add_diagnostic(result, "cannot assign an owner for reachable enum '" + enum_name + "'");
            continue;
        }
        if (!valid_quarry_component(enumeration.name)) {
            add_diagnostic(result, "cannot map protobuf enum '" + enum_name +
                                     "': invalid Quarry enum name '" + enumeration.name + "'");
            continue;
        }
        std::set<std::string> value_names;
        std::set<std::int32_t> value_numbers;
        for (const auto& value : enumeration.values) {
            if (!valid_enum_value_name(value.name)) {
                add_diagnostic(result, "cannot map protobuf enum value '" + enum_name + "." +
                                         value.name + "': invalid Quarry name");
            }
            if (!value_names.insert(value.name).second) {
                add_diagnostic(result, "protobuf enum '" + enum_name + "' contains duplicate value '" +
                                         value.name + "'");
            }
            if (!value_numbers.insert(value.number).second) {
                add_diagnostic(result, "protobuf enum '" + enum_name +
                                         "' uses enum aliases, which are not supported");
            }
            if (value.number < 0) {
                add_diagnostic(result, "protobuf enum '" + enum_name +
                                         "' contains negative value '" + value.name + "'");
            }
        }
        enum_owners.emplace(enum_name, owner);
        for (const auto& [other_name, other_owner] : enum_owners) {
            if (other_owner == owner && other_name != enum_name &&
                reachable_enums.at(other_name).name == enumeration.name) {
                add_diagnostic(result, "enum name collision in generated source unit '" + owner +
                                         "' for '" + enum_name + "' and '" + other_name + "'");
            }
        }
    }
    if (!result.succeeded()) return result;

    std::map<std::string, MappedEnum> enum_mappings;
    for (const auto& [enum_name, enumeration] : reachable_enums) {
        const std::string& owner = enum_owners.at(enum_name);
        const auto& owner_mapping = mappings.at(owner);
        enum_mappings.emplace(enum_name, MappedEnum{enum_name, enumeration.file_name,
                                                    owner_mapping.quarry_namespace, enumeration.name,
                                                    owner});
    }

    std::vector<RenderedMessage> rendered;
    for (const std::string& name : order) {
        const DescriptorMessage* message = find_message(model, name);
        const MappedMessage& mapped = mappings.at(name);
        std::set<std::string> dependency_paths;
        for (const DescriptorField& field : message->fields) {
            if (field.type != FieldType::Message) continue;
            const auto dependency = mappings.find(field.type_name);
            if (dependency == mappings.end()) continue;
            if (dependency->second.quarry_namespace != mapped.quarry_namespace) {
                dependency_paths.insert(relative_import(mapped.output, dependency->second.output));
            }
        }
        std::vector<std::string> owned_enums;
        for (const auto& [enum_name, enum_mapping] : enum_mappings) {
            if (enum_mapping.owner_message == name) {
                owned_enums.push_back(enum_name);
            } else {
                const auto referenced = std::any_of(
                    message->fields.begin(), message->fields.end(), [&](const DescriptorField& field) {
                        return field.type == FieldType::Enum && field.type_name == enum_name;
                    });
                if (referenced) {
                    const auto owner_message = mappings.find(enum_mapping.owner_message);
                    if (owner_message != mappings.end() &&
                        owner_message->second.quarry_namespace != mapped.quarry_namespace) {
                        dependency_paths.insert(relative_import(mapped.output,
                                                                owner_message->second.output));
                    }
                }
            }
        }
        std::sort(owned_enums.begin(), owned_enums.end());
        const std::vector<std::string> dependencies(dependency_paths.begin(), dependency_paths.end());
        rendered.push_back(RenderedMessage{
            mapped, dependencies, owned_enums,
            render_message(*message, mapped, mappings, enum_mappings, reachable_enums, bounds,
                           owned_enums, dependencies, result)});
    }
    if (!result.succeeded()) return result;

    const std::filesystem::path output_directory_path(output_directory);
    const std::filesystem::path temporary = output_directory_path.string() + ".quarry-tmp";
    std::error_code error;
    if (std::filesystem::exists(temporary, error)) {
        add_diagnostic(result, "temporary translation directory already exists: '" +
                                 temporary.string() + "'");
        return result;
    }
    std::filesystem::create_directories(temporary, error);
    if (error) {
        add_diagnostic(result, "cannot create temporary translation directory '" +
                                 temporary.string() + "': " + error.message());
        return result;
    }
    for (const RenderedMessage& message : rendered) {
        const std::filesystem::path target = temporary / message.mapped.output;
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) break;
        std::ofstream file(target);
        if (!file || !(file << message.contents)) {
            error = std::make_error_code(std::errc::io_error);
            break;
        }
    }
    const std::filesystem::path manifest = temporary / "manifest.json";
    if (!error) {
        std::ofstream file(manifest);
        if (!file || !(file << render_manifest(root, rendered, resolved_provenance, enum_mappings,
                                               reachable_enums, mappings, model))) {
            error = std::make_error_code(std::errc::io_error);
        }
    }
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(temporary, cleanup_error);
        add_diagnostic(result, "cannot write translated output: " + error.message());
        return result;
    }
    std::filesystem::create_directories(output_directory_path, error);
    for (const RenderedMessage& message : rendered) {
        if (error) break;
        const std::filesystem::path source = temporary / message.mapped.output;
        const std::filesystem::path target = output_directory_path / message.mapped.output;
        std::filesystem::create_directories(target.parent_path(), error);
        if (!error) std::filesystem::rename(source, target, error);
    }
    if (!error) {
        std::filesystem::rename(manifest, output_directory_path / "manifest.json", error);
    }
    std::error_code cleanup_error;
    std::filesystem::remove_all(temporary, cleanup_error);
    if (error) {
        add_diagnostic(result, "cannot publish translated output: " + error.message());
    }
    return result;
}

} // namespace quarry::tools::protobuf
