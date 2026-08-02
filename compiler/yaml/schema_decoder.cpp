#include "compiler/yaml/schema_decoder.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace quarry::compiler::yaml {
namespace {

using source_schema::SourceSchemaAnnotation;
using source_schema::SourceSchemaDocument;
using source_schema::SourceSchemaEnum;
using source_schema::SourceSchemaEnumValue;
using source_schema::SourceSchemaField;
using source_schema::SourceSchemaImports;

constexpr std::string_view schema_pass = "yaml-schema-decoder";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] support::SourceRange combine_ranges(const support::SourceRange& begin,
                                                  const support::SourceRange& end) {
    if (!begin.is_valid()) {
        return end;
    }
    if (!end.is_valid()) {
        return begin;
    }
    return support::SourceRange(begin.begin(), end.end());
}

[[nodiscard]] const YamlScalarNode* scalar_value(const YamlNode& node) {
    return std::get_if<YamlScalarNode>(&node.value);
}

[[nodiscard]] const YamlMappingNode* mapping_value(const YamlNode& node) {
    return std::get_if<YamlMappingNode>(&node.value);
}

[[nodiscard]] std::optional<SourceSchemaImports>
decode_imports(const YamlNode& node, diagnostics::DiagnosticEngine& diagnostics) {
    const auto* sequence = std::get_if<YamlSequenceNode>(&node.value);
    if (sequence == nullptr) {
        auto builder = diagnostics::Diagnostic::create(
                           diagnostic_id("BC2301"), diagnostics::Severity::Error,
                           "expected a YAML sequence for top-level property 'imports'")
                           .from_pass(std::string(schema_pass));
        builder.at(node.source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    SourceSchemaImports imports;
    imports.source_range = node.source_range;
    imports.empty = sequence->elements.empty();
    imports.entries.reserve(sequence->elements.size());
    for (const std::unique_ptr<YamlNode>& element_ptr : sequence->elements) {
        if (element_ptr == nullptr) {
            return std::nullopt;
        }
        const YamlNode& element = *element_ptr;
        const auto* scalar = scalar_value(element);
        if (scalar == nullptr || scalar->value.empty()) {
            auto builder = diagnostics::Diagnostic::create(
                               diagnostic_id("BC2302"), diagnostics::Severity::Error,
                               "import paths must be non-empty YAML strings")
                               .from_pass(std::string(schema_pass));
            builder.at(element.source_range);
            diagnostics.emit(builder.build());
            return std::nullopt;
        }
        imports.entries.push_back(SourceSchemaImports::Import{
            .path = scalar->value,
            .source_range = element.source_range,
        });
    }
    return imports;
}

[[nodiscard]] std::optional<std::int64_t> parse_integer(std::string_view text) {
    std::int64_t value = 0;
    const std::string copy(text);
    const char* const begin = copy.data();
    const char* const end = copy.data() + copy.size();
    auto [ptr, ec] = std::from_chars(begin, end, value, 10);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

struct DecodedScalar {
    std::string value;
    YamlScalarKind kind = YamlScalarKind::Plain;
    support::SourceRange range = support::SourceRange::invalid();
};

[[nodiscard]] std::optional<DecodedScalar> decode_scalar(const YamlNode& node,
                                                        diagnostics::DiagnosticEngine& diagnostics,
                                                        std::string_view context,
                                                        std::string_view property_name,
                                                        std::string_view pass_name,
                                                        std::string_view diagnostic_id_value) {
    const auto* scalar = scalar_value(node);
    if (scalar == nullptr) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id(diagnostic_id_value),
                                            diagnostics::Severity::Error,
                                            "expected a scalar for " + std::string(context) +
                                                " property '" + std::string(property_name) + "'")
                .from_pass(std::string(pass_name));
        builder.at(node.source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    return DecodedScalar{.value = scalar->value, .kind = scalar->kind, .range = node.source_range};
}

[[nodiscard]] std::optional<std::int64_t> decode_integer_property(
    const YamlNode& node, diagnostics::DiagnosticEngine& diagnostics, std::string_view context,
    std::string_view property_name, std::string_view pass_name) {
    const std::optional<DecodedScalar> decoded =
        decode_scalar(node, diagnostics, context, property_name, pass_name, "BC2302");
    if (!decoded.has_value()) {
        return std::nullopt;
    }

    if (decoded->kind != YamlScalarKind::Plain) {
        auto builder =
            diagnostics::Diagnostic::create(
                diagnostic_id("BC2306"), diagnostics::Severity::Error,
                "property '" + std::string(property_name) + "' must be a native YAML integer")
                .from_pass(std::string(pass_name));
        builder.at(decoded->range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    const std::optional<std::int64_t> parsed = parse_integer(decoded->value);
    if (!parsed.has_value()) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2306"),
                                            diagnostics::Severity::Error,
                                            "property '" + std::string(property_name) +
                                                "' must be a native YAML integer")
                .from_pass(std::string(pass_name));
        builder.at(decoded->range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    return parsed;
}

[[nodiscard]] bool insert_unique_property(std::vector<std::pair<std::string, support::SourceRange>>& seen,
                                          std::string_view name, support::SourceRange range,
                                          diagnostics::DiagnosticEngine& diagnostics,
                                          std::string_view context, std::string_view pass_name) {
    const auto first = std::find_if(seen.begin(), seen.end(),
                                    [&](const auto& item) { return item.first == name; });
    if (first == seen.end()) {
        seen.emplace_back(name, range);
        return true;
    }

    auto builder =
        diagnostics::Diagnostic::create(diagnostic_id("BC2304"), diagnostics::Severity::Error,
                                        "duplicate property '" + std::string(name) + "' in " +
                                            std::string(context))
            .from_pass(std::string(pass_name));
    builder.at(range);
    builder.with_related(diagnostics::RelatedLocation::at_range(first->second,
                                                                "first property is here"));
    diagnostics.emit(builder.build());
    return false;
}

[[nodiscard]] std::optional<std::vector<SourceSchemaAnnotation>>
decode_annotations(const YamlNode& node, diagnostics::DiagnosticEngine& diagnostics,
                   std::string_view context, std::string_view pass_name) {
    const auto* mapping = mapping_value(node);
    if (mapping == nullptr) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2301"), diagnostics::Severity::Error,
                                            "expected a YAML mapping for " + std::string(context))
                .from_pass(std::string(pass_name));
        builder.at(node.source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    std::vector<std::pair<std::string, support::SourceRange>> seen;
    std::vector<SourceSchemaAnnotation> annotations;
    annotations.reserve(mapping->entries.size());

    for (const auto& entry : mapping->entries) {
        const auto* key = scalar_value(*entry.key);
        if (key == nullptr) {
            auto builder =
                diagnostics::Diagnostic::create(diagnostic_id("BC2302"), diagnostics::Severity::Error,
                                                "expected a scalar annotation key in " +
                                                    std::string(context))
                    .from_pass(std::string(pass_name));
            builder.at(entry.key->source_range);
            diagnostics.emit(builder.build());
            return std::nullopt;
        }

        if (!insert_unique_property(seen, key->value, entry.key->source_range, diagnostics,
                                    context, pass_name)) {
            return std::nullopt;
        }

        const std::optional<DecodedScalar> value =
            decode_scalar(*entry.value, diagnostics, context, key->value, pass_name, "BC2302");
        if (!value.has_value()) {
            return std::nullopt;
        }

        SourceSchemaAnnotation annotation;
        annotation.name = key->value;
        annotation.value = value->value;
        annotation.name_range = entry.key->source_range;
        annotation.value_range = value->range;
        annotation.source_range =
            combine_ranges(entry.key->source_range, entry.value->source_range);
        annotations.push_back(std::move(annotation));
    }

    return annotations;
}

[[nodiscard]] std::optional<SourceSchemaField>
decode_field(const YamlMappingEntry& entry, diagnostics::DiagnosticEngine& diagnostics,
             std::string_view pass_name) {
    const auto* field_name = scalar_value(*entry.key);
    if (field_name == nullptr) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2302"), diagnostics::Severity::Error,
                                            "expected a scalar field name")
                .from_pass(std::string(pass_name));
        builder.at(entry.key->source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    const auto* mapping = mapping_value(*entry.value);
    if (mapping == nullptr) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2301"), diagnostics::Severity::Error,
                                            "expected a YAML mapping for field '" +
                                                std::string(field_name->value) + "'")
                .from_pass(std::string(pass_name));
        builder.at(entry.value->source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    SourceSchemaField field;
    field.name = field_name->value;
    field.name_range = entry.key->source_range;
    field.source_range = combine_ranges(entry.key->source_range, entry.value->source_range);

    std::vector<std::pair<std::string, support::SourceRange>> seen;
    bool has_type = false;

    for (const auto& property : mapping->entries) {
        const auto* property_name = scalar_value(*property.key);
        if (property_name == nullptr) {
            auto builder =
                diagnostics::Diagnostic::create(diagnostic_id("BC2302"), diagnostics::Severity::Error,
                                                "expected a scalar property name in field '" +
                                                    field.name + "'")
                    .from_pass(std::string(pass_name));
            builder.at(property.key->source_range);
            diagnostics.emit(builder.build());
            return std::nullopt;
        }

        if (!insert_unique_property(seen, property_name->value, property.key->source_range,
                                    diagnostics, "field '" + field.name + "'", pass_name)) {
            return std::nullopt;
        }

        if (property_name->value == "type") {
            const std::optional<DecodedScalar> type_value =
                decode_scalar(*property.value, diagnostics, "field '" + field.name + "'",
                              "type", pass_name, "BC2302");
            if (!type_value.has_value()) {
                return std::nullopt;
            }
            field.type_spelling = type_value->value;
            field.type_range = type_value->range;
            has_type = true;
            continue;
        }

        if (property_name->value == "max_bytes") {
            const std::optional<std::int64_t> parsed = decode_integer_property(
                *property.value, diagnostics, "field '" + field.name + "'", "max_bytes",
                pass_name);
            if (!parsed.has_value()) {
                return std::nullopt;
            }
            field.max_bytes = parsed;
            field.max_bytes_range = property.value->source_range;
            continue;
        }

        if (property_name->value == "max_elements") {
            const std::optional<std::int64_t> parsed = decode_integer_property(
                *property.value, diagnostics, "field '" + field.name + "'", "max_elements",
                pass_name);
            if (!parsed.has_value()) {
                return std::nullopt;
            }
            field.max_elements = parsed;
            field.max_elements_range = property.value->source_range;
            continue;
        }

        if (property_name->value == "annotations") {
            std::optional<std::vector<SourceSchemaAnnotation>> annotations =
                decode_annotations(*property.value, diagnostics, "field '" + field.name +
                                                             "' annotations",
                                   pass_name);
            if (!annotations.has_value()) {
                return std::nullopt;
            }
            field.annotations = std::move(*annotations);
            continue;
        }

        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2305"), diagnostics::Severity::Error,
                                            "unknown property '" + property_name->value +
                                                "' in field '" + field.name + "'")
                .from_pass(std::string(pass_name));
        builder.at(property.key->source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    if (!has_type) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2303"), diagnostics::Severity::Error,
                                            "missing required property 'type' in field '" +
                                                field.name + "'")
                .from_pass(std::string(pass_name));
        builder.at(field.source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    return field;
}

[[nodiscard]] std::optional<SourceSchemaEnumValue>
decode_enum_value(const YamlMappingEntry& entry, diagnostics::DiagnosticEngine& diagnostics,
                  std::string_view enum_name, std::string_view pass_name) {
    const auto* value_name = scalar_value(*entry.key);
    if (value_name == nullptr) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2302"), diagnostics::Severity::Error,
                                            "expected a scalar enum value name in enum '" +
                                                std::string(enum_name) + "'")
                .from_pass(std::string(pass_name));
        builder.at(entry.key->source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    const std::optional<std::int64_t> parsed =
        decode_integer_property(*entry.value, diagnostics, "enum '" + std::string(enum_name) +
                                                       "'",
                                value_name->value, pass_name);
    if (!parsed.has_value()) {
        return std::nullopt;
    }

    SourceSchemaEnumValue value;
    value.name = value_name->value;
    value.name_range = entry.key->source_range;
    value.value = *parsed;
    value.value_range = entry.value->source_range;
    value.source_range = combine_ranges(entry.key->source_range, entry.value->source_range);
    return value;
}

[[nodiscard]] std::optional<SourceSchemaEnum>
decode_enum(const YamlMappingEntry& entry, diagnostics::DiagnosticEngine& diagnostics,
            std::string_view pass_name) {
    const auto* enum_name = scalar_value(*entry.key);
    if (enum_name == nullptr) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2302"), diagnostics::Severity::Error,
                                            "expected a scalar enum name")
                .from_pass(std::string(pass_name));
        builder.at(entry.key->source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    const auto* mapping = mapping_value(*entry.value);
    if (mapping == nullptr) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2301"), diagnostics::Severity::Error,
                                            "expected a YAML mapping for enum '" +
                                                std::string(enum_name->value) + "'")
                .from_pass(std::string(pass_name));
        builder.at(entry.value->source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    SourceSchemaEnum result;
    result.name = enum_name->value;
    result.name_range = entry.key->source_range;
    result.source_range = combine_ranges(entry.key->source_range, entry.value->source_range);

    std::vector<std::pair<std::string, support::SourceRange>> seen;
    bool has_values = false;

    for (const auto& property : mapping->entries) {
        const auto* property_name = scalar_value(*property.key);
        if (property_name == nullptr) {
            auto builder =
                diagnostics::Diagnostic::create(diagnostic_id("BC2302"), diagnostics::Severity::Error,
                                                "expected a scalar property name in enum '" +
                                                    result.name + "'")
                    .from_pass(std::string(pass_name));
            builder.at(property.key->source_range);
            diagnostics.emit(builder.build());
            return std::nullopt;
        }

        if (!insert_unique_property(seen, property_name->value, property.key->source_range,
                                    diagnostics, "enum '" + result.name + "'", pass_name)) {
            return std::nullopt;
        }

        if (property_name->value == "values") {
            const auto* values_mapping = mapping_value(*property.value);
            if (values_mapping == nullptr) {
                auto builder =
                    diagnostics::Diagnostic::create(
                        diagnostic_id("BC2301"), diagnostics::Severity::Error,
                        "expected a YAML mapping for enum '" + result.name + "' values")
                        .from_pass(std::string(pass_name));
                builder.at(property.value->source_range);
                diagnostics.emit(builder.build());
                return std::nullopt;
            }

            std::vector<std::pair<std::string, support::SourceRange>> seen_values;
            result.values.reserve(values_mapping->entries.size());
            for (const auto& value_entry : values_mapping->entries) {
                const auto decoded_value =
                    decode_enum_value(value_entry, diagnostics, result.name, pass_name);
                if (!decoded_value.has_value()) {
                    return std::nullopt;
                }

                const auto first = std::find_if(seen_values.begin(), seen_values.end(),
                                                [&](const auto& item) {
                                                    return item.first == decoded_value->name;
                                                });
                if (first != seen_values.end()) {
                    auto builder =
                        diagnostics::Diagnostic::create(
                            diagnostic_id("BC2304"), diagnostics::Severity::Error,
                            "duplicate property '" + decoded_value->name + "' in enum '" +
                                result.name + "' values")
                            .from_pass(std::string(pass_name));
                    builder.at(decoded_value->name_range);
                    builder.with_related(
                        diagnostics::RelatedLocation::at_range(first->second,
                                                               "first property is here"));
                    diagnostics.emit(builder.build());
                    return std::nullopt;
                }
                seen_values.emplace_back(decoded_value->name, decoded_value->name_range);
                result.values.push_back(*decoded_value);
            }
            has_values = true;
            continue;
        }

        if (property_name->value == "annotations") {
            std::optional<std::vector<SourceSchemaAnnotation>> annotations =
                decode_annotations(*property.value, diagnostics, "enum '" + result.name +
                                                             "' annotations",
                                   pass_name);
            if (!annotations.has_value()) {
                return std::nullopt;
            }
            result.annotations = std::move(*annotations);
            continue;
        }

        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2305"), diagnostics::Severity::Error,
                                            "unknown property '" + property_name->value +
                                                "' in enum '" + result.name + "'")
                .from_pass(std::string(pass_name));
        builder.at(property.key->source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    if (!has_values) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2303"), diagnostics::Severity::Error,
                                            "missing required property 'values' in enum '" +
                                                result.name + "'")
                .from_pass(std::string(pass_name));
        builder.at(result.source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    return result;
}

[[nodiscard]] bool mark_seen(std::vector<std::pair<std::string, support::SourceRange>>& seen,
                             std::string_view name, support::SourceRange range,
                             diagnostics::DiagnosticEngine& diagnostics, std::string_view context,
                             std::string_view pass_name) {
    const auto first = std::find_if(seen.begin(), seen.end(),
                                    [&](const auto& item) { return item.first == name; });
    if (first == seen.end()) {
        seen.emplace_back(name, range);
        return true;
    }

    auto builder =
        diagnostics::Diagnostic::create(diagnostic_id("BC2304"), diagnostics::Severity::Error,
                                        "duplicate property '" + std::string(name) + "' in " +
                                            std::string(context))
            .from_pass(std::string(pass_name));
    builder.at(range);
    builder.with_related(diagnostics::RelatedLocation::at_range(first->second,
                                                                "first property is here"));
    diagnostics.emit(builder.build());
    return false;
}

[[nodiscard]] std::optional<SourceSchemaDocument>
decode_root_mapping(const YamlMappingNode& mapping, const YamlDocument& document,
                    diagnostics::DiagnosticEngine& diagnostics) {
    SourceSchemaDocument schema;
    schema.source_range = document.source_range.is_valid() ? document.source_range
                                                           : support::SourceRange::invalid();

    std::vector<std::pair<std::string, support::SourceRange>> seen;
    bool has_namespace = false;
    bool has_record = false;
    bool has_version = false;
    bool has_type = false;
    bool has_fields = false;

    for (const auto& entry : mapping.entries) {
        const auto* key = scalar_value(*entry.key);
        if (key == nullptr) {
            auto builder =
                diagnostics::Diagnostic::create(diagnostic_id("BC2302"), diagnostics::Severity::Error,
                                                "expected a scalar top-level property name")
                    .from_pass(std::string(schema_pass));
            builder.at(entry.key->source_range);
            diagnostics.emit(builder.build());
            return std::nullopt;
        }

        if (!mark_seen(seen, key->value, entry.key->source_range, diagnostics, "schema root",
                       schema_pass)) {
            return std::nullopt;
        }

        if (key->value == "namespace") {
            const std::optional<DecodedScalar> value =
                decode_scalar(*entry.value, diagnostics, "schema root", "namespace", schema_pass,
                              "BC2302");
            if (!value.has_value()) {
                return std::nullopt;
            }
            schema.namespace_spelling = value->value;
            schema.namespace_range = value->range;
            has_namespace = true;
            continue;
        }

        if (key->value == "record") {
            const std::optional<DecodedScalar> value =
                decode_scalar(*entry.value, diagnostics, "schema root", "record", schema_pass,
                              "BC2302");
            if (!value.has_value()) {
                return std::nullopt;
            }
            schema.record_name = value->value;
            schema.record_range = value->range;
            has_record = true;
            continue;
        }

        if (key->value == "version") {
            const std::optional<std::int64_t> parsed =
                decode_integer_property(*entry.value, diagnostics, "schema root", "version",
                                        schema_pass);
            if (!parsed.has_value()) {
                return std::nullopt;
            }
            schema.version = *parsed;
            schema.version_range = entry.value->source_range;
            has_version = true;
            continue;
        }

        if (key->value == "type") {
            const std::optional<DecodedScalar> value =
                decode_scalar(*entry.value, diagnostics, "schema root", "type", schema_pass,
                              "BC2302");
            if (!value.has_value()) {
                return std::nullopt;
            }
            schema.record_type_spelling = value->value;
            schema.record_type_range = value->range;
            has_type = true;
            continue;
        }

        if (key->value == "fields") {
            const auto* fields_mapping = mapping_value(*entry.value);
            if (fields_mapping == nullptr) {
                auto builder =
                    diagnostics::Diagnostic::create(
                        diagnostic_id("BC2301"), diagnostics::Severity::Error,
                        "expected a YAML mapping for top-level property 'fields'")
                        .from_pass(std::string(schema_pass));
                builder.at(entry.value->source_range);
                diagnostics.emit(builder.build());
                return std::nullopt;
            }

            std::vector<std::pair<std::string, support::SourceRange>> seen_fields;
            schema.fields.reserve(fields_mapping->entries.size());
            for (const auto& field_entry : fields_mapping->entries) {
                const auto decoded_field =
                    decode_field(field_entry, diagnostics, schema_pass);
                if (!decoded_field.has_value()) {
                    return std::nullopt;
                }

                const auto first = std::find_if(seen_fields.begin(), seen_fields.end(),
                                                [&](const auto& item) {
                                                    return item.first == decoded_field->name;
                                                });
                if (first != seen_fields.end()) {
                    auto builder =
                        diagnostics::Diagnostic::create(
                            diagnostic_id("BC2304"), diagnostics::Severity::Error,
                            "duplicate property '" + decoded_field->name +
                                "' in top-level property 'fields'")
                            .from_pass(std::string(schema_pass));
                    builder.at(decoded_field->name_range);
                    builder.with_related(
                        diagnostics::RelatedLocation::at_range(first->second,
                                                               "first property is here"));
                    diagnostics.emit(builder.build());
                    return std::nullopt;
                }
                seen_fields.emplace_back(decoded_field->name, decoded_field->name_range);
                schema.fields.push_back(*decoded_field);
            }
            has_fields = true;
            continue;
        }

        if (key->value == "imports") {
            const std::optional<SourceSchemaImports> imports =
                decode_imports(*entry.value, diagnostics);
            if (!imports.has_value()) {
                return std::nullopt;
            }
            schema.imports = *imports;
            schema.imports_range = entry.value->source_range;
            continue;
        }

        if (key->value == "enums") {
            const auto* enums_mapping = mapping_value(*entry.value);
            if (enums_mapping == nullptr) {
                auto builder =
                    diagnostics::Diagnostic::create(
                        diagnostic_id("BC2301"), diagnostics::Severity::Error,
                        "expected a YAML mapping for top-level property 'enums'")
                        .from_pass(std::string(schema_pass));
                builder.at(entry.value->source_range);
                diagnostics.emit(builder.build());
                return std::nullopt;
            }

            std::vector<std::pair<std::string, support::SourceRange>> seen_enums;
            schema.enums.reserve(enums_mapping->entries.size());
            for (const auto& enum_entry : enums_mapping->entries) {
                const auto decoded_enum = decode_enum(enum_entry, diagnostics, schema_pass);
                if (!decoded_enum.has_value()) {
                    return std::nullopt;
                }

                const auto first = std::find_if(seen_enums.begin(), seen_enums.end(),
                                                [&](const auto& item) {
                                                    return item.first == decoded_enum->name;
                                                });
                if (first != seen_enums.end()) {
                    auto builder =
                        diagnostics::Diagnostic::create(
                            diagnostic_id("BC2304"), diagnostics::Severity::Error,
                            "duplicate property '" + decoded_enum->name +
                                "' in top-level property 'enums'")
                            .from_pass(std::string(schema_pass));
                    builder.at(decoded_enum->name_range);
                    builder.with_related(
                        diagnostics::RelatedLocation::at_range(first->second,
                                                               "first property is here"));
                    diagnostics.emit(builder.build());
                    return std::nullopt;
                }
                seen_enums.emplace_back(decoded_enum->name, decoded_enum->name_range);
                schema.enums.push_back(*decoded_enum);
            }
            continue;
        }

        if (key->value == "annotations") {
            std::optional<std::vector<SourceSchemaAnnotation>> annotations =
                decode_annotations(*entry.value, diagnostics, "schema root annotations",
                                   schema_pass);
            if (!annotations.has_value()) {
                return std::nullopt;
            }
            schema.annotations = std::move(*annotations);
            continue;
        }

        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2305"), diagnostics::Severity::Error,
                                            "unknown top-level property '" + key->value + "'")
                .from_pass(std::string(schema_pass));
        builder.at(entry.key->source_range);
        diagnostics.emit(builder.build());
        return std::nullopt;
    }

    const auto emit_missing = [&](std::string_view property) {
        auto builder =
            diagnostics::Diagnostic::create(diagnostic_id("BC2303"), diagnostics::Severity::Error,
                                            "missing required property '" + std::string(property) +
                                                "' in schema root")
                .from_pass(std::string(schema_pass));
        builder.at(schema.source_range);
        diagnostics.emit(builder.build());
    };

    if (!has_namespace) {
        emit_missing("namespace");
        return std::nullopt;
    }
    if (!has_record) {
        emit_missing("record");
        return std::nullopt;
    }
    if (!has_version) {
        emit_missing("version");
        return std::nullopt;
    }
    if (!has_type) {
        emit_missing("type");
        return std::nullopt;
    }
    if (!has_fields) {
        emit_missing("fields");
        return std::nullopt;
    }

    return schema;
}

} // namespace

source_schema::SourceSchemaDecodeResult
decode_schema(const YamlDocument& document, diagnostics::DiagnosticEngine& diagnostics) {
    source_schema::SourceSchemaDecodeResult result;
    const auto emit = [&](std::string_view id, std::string_view message, support::SourceRange range) {
        auto builder = diagnostics::Diagnostic::create(diagnostic_id(id), diagnostics::Severity::Error,
                                                       std::string(message))
                           .from_pass(std::string(schema_pass));
        if (range.is_valid()) {
            builder.at(range);
        }
        diagnostics.emit(builder.build());
    };

    if (document.root == nullptr) {
        emit("BC2301", "expected a YAML mapping for schema root", document.source_range);
        return result;
    }

    const auto* mapping = mapping_value(*document.root);
    if (mapping == nullptr) {
        emit("BC2301", "expected a YAML mapping for schema root", document.root->source_range);
        return result;
    }

    result.schema = decode_root_mapping(*mapping, document, diagnostics);
    return result;
}

} // namespace quarry::compiler::yaml
