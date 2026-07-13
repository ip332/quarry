#include "compiler/source_schema/source_schema.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace breadcrumbs::compiler::source_schema {
namespace {

constexpr std::string_view normalization_pass = "source-schema-normalization";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

void emit_error(diagnostics::DiagnosticEngine& diagnostics, std::string_view id,
                std::string message, support::SourceRange range) {
    auto builder = diagnostics::Diagnostic::create(diagnostic_id(id), diagnostics::Severity::Error,
                                                   std::move(message))
                       .from_pass(std::string(normalization_pass));
    if (range.is_valid()) {
        builder.at(range);
    }
    diagnostics.emit(builder.build());
}

[[nodiscard]] bool is_identifier_start(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_';
}

[[nodiscard]] bool is_identifier_continue(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

[[nodiscard]] bool is_valid_identifier(std::string_view text) {
    if (text.empty() || !is_identifier_start(text.front())) {
        return false;
    }
    return std::all_of(text.begin() + 1, text.end(),
                       [](char character) { return is_identifier_continue(character); });
}

[[nodiscard]] SourceSchemaIdentifier lower_identifier(std::string_view text,
                                                      support::SourceRange range,
                                                      diagnostics::DiagnosticEngine& diagnostics,
                                                      std::string_view context) {
    if (!is_valid_identifier(text)) {
        emit_error(diagnostics, "BC2401",
                   "invalid " + std::string(context) + " '" + std::string(text) + "'", range);
        return {};
    }

    return SourceSchemaIdentifier{.text = std::string(text), .source_range = range};
}

[[nodiscard]] SourceSchemaQualifiedName lower_qualified_name(std::string_view text,
                                                             support::SourceRange range,
                                                             diagnostics::DiagnosticEngine& diagnostics,
                                                             std::string_view context) {
    SourceSchemaQualifiedName result;
    result.source_range = range;

    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('.', begin);
        const std::string_view part =
            end == std::string_view::npos ? text.substr(begin) : text.substr(begin, end - begin);
        if (!is_valid_identifier(part)) {
            emit_error(diagnostics, "BC2401",
                       "invalid " + std::string(context) + " '" + std::string(text) + "'", range);
            return {};
        }
        result.parts.push_back(SourceSchemaIdentifier{.text = std::string(part), .source_range = range});
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }

    return result;
}

[[nodiscard]] std::optional<NormalizedSourceSchemaType>
normalize_type_spelling(std::string_view spelling, support::SourceRange range,
                        diagnostics::DiagnosticEngine& diagnostics) {
    if (spelling.empty()) {
        emit_error(diagnostics, "BC2402", "malformed field type spelling", range);
        return std::nullopt;
    }

    if (spelling.size() >= 2 && spelling.ends_with("[]")) {
        const std::string_view element_spelling = spelling.substr(0, spelling.size() - 2);
        if (element_spelling.empty()) {
            emit_error(diagnostics, "BC2402",
                       "malformed array field type spelling '" + std::string(spelling) + "'",
                       range);
            return std::nullopt;
        }

        const std::optional<NormalizedSourceSchemaType> element_type =
            normalize_type_spelling(element_spelling, range, diagnostics);
        if (!element_type.has_value()) {
            return std::nullopt;
        }

        NormalizedSourceSchemaArrayType array_type;
        array_type.source_range = range;
        array_type.element_type = std::make_unique<NormalizedSourceSchemaType>(*element_type);
        return NormalizedSourceSchemaType{std::move(array_type)};
    }

    if (spelling.find('[') != std::string_view::npos || spelling.find(']') != std::string_view::npos) {
        emit_error(diagnostics, "BC2402",
                   "malformed field type spelling '" + std::string(spelling) + "'", range);
        return std::nullopt;
    }

    NormalizedSourceSchemaTypeReference type_reference;
    type_reference.source_range = range;
    type_reference.name = lower_qualified_name(spelling, range, diagnostics, "field type");
    return NormalizedSourceSchemaType{std::move(type_reference)};
}

[[nodiscard]] std::optional<NormalizedSourceSchemaAnnotation>
normalize_annotation(const SourceSchemaAnnotation& annotation,
                     diagnostics::DiagnosticEngine& diagnostics, std::string_view context) {
    NormalizedSourceSchemaAnnotation normalized;
    normalized.name = lower_identifier(annotation.name, annotation.name_range, diagnostics, context);
    if (normalized.name.text.empty()) {
        return std::nullopt;
    }
    normalized.value = annotation.value;
    normalized.source_range = annotation.source_range;
    normalized.value_range = annotation.value_range;
    return normalized;
}

[[nodiscard]] std::optional<NormalizedSourceSchemaField>
normalize_field(const SourceSchemaField& field, diagnostics::DiagnosticEngine& diagnostics) {
    NormalizedSourceSchemaField normalized;
    normalized.name =
        lower_identifier(field.name, field.name_range, diagnostics, "field identifier");
    if (normalized.name.text.empty()) {
        return std::nullopt;
    }
    normalized.source_range = field.source_range;
    normalized.max_bytes = field.max_bytes;
    normalized.max_bytes_range = field.max_bytes_range;
    normalized.max_elements = field.max_elements;
    normalized.max_elements_range = field.max_elements_range;
    const std::optional<NormalizedSourceSchemaType> type =
        normalize_type_spelling(field.type_spelling, field.type_range, diagnostics);
    if (!type.has_value()) {
        return std::nullopt;
    }
    normalized.type = *type;

    for (const SourceSchemaAnnotation& annotation : field.annotations) {
        const std::optional<NormalizedSourceSchemaAnnotation> normalized_annotation =
            normalize_annotation(annotation, diagnostics, "field annotation");
        if (!normalized_annotation.has_value()) {
            return std::nullopt;
        }
        normalized.annotations.push_back(*normalized_annotation);
    }
    return normalized;
}

[[nodiscard]] std::optional<NormalizedSourceSchemaEnumValue>
normalize_enum_value(const SourceSchemaEnumValue& value,
                     diagnostics::DiagnosticEngine& diagnostics) {
    NormalizedSourceSchemaEnumValue normalized;
    normalized.name =
        lower_identifier(value.name, value.name_range, diagnostics, "enum value identifier");
    if (normalized.name.text.empty()) {
        return std::nullopt;
    }
    normalized.source_range = value.source_range;
    normalized.value = value.value;
    normalized.value_range = value.value_range;
    return normalized;
}

[[nodiscard]] std::optional<NormalizedSourceSchemaEnum>
normalize_enum(const SourceSchemaEnum& enumeration, diagnostics::DiagnosticEngine& diagnostics) {
    NormalizedSourceSchemaEnum normalized;
    normalized.name = lower_identifier(enumeration.name, enumeration.name_range, diagnostics,
                                       "enum identifier");
    if (normalized.name.text.empty()) {
        return std::nullopt;
    }
    normalized.source_range = enumeration.source_range;
    for (const SourceSchemaAnnotation& annotation : enumeration.annotations) {
        const std::optional<NormalizedSourceSchemaAnnotation> normalized_annotation =
            normalize_annotation(annotation, diagnostics, "enum annotation");
        if (!normalized_annotation.has_value()) {
            return std::nullopt;
        }
        normalized.annotations.push_back(*normalized_annotation);
    }
    for (const SourceSchemaEnumValue& value : enumeration.values) {
        const std::optional<NormalizedSourceSchemaEnumValue> normalized_value =
            normalize_enum_value(value, diagnostics);
        if (!normalized_value.has_value()) {
            return std::nullopt;
        }
        normalized.values.push_back(*normalized_value);
    }
    return normalized;
}

} // namespace

[[nodiscard]] bool SourceSchemaQualifiedName::empty() const { return parts.empty(); }

[[nodiscard]] std::string SourceSchemaQualifiedName::text() const {
    std::string text;
    for (const SourceSchemaIdentifier& part : parts) {
        if (!text.empty()) {
            text.push_back('.');
        }
        text.append(part.text);
    }
    return text;
}

bool NormalizedSourceSchemaType::is_reference() const {
    return std::holds_alternative<NormalizedSourceSchemaTypeReference>(value);
}

bool NormalizedSourceSchemaType::is_array() const {
    return std::holds_alternative<NormalizedSourceSchemaArrayType>(value);
}

NormalizedSourceSchemaArrayType::NormalizedSourceSchemaArrayType() = default;

NormalizedSourceSchemaArrayType::NormalizedSourceSchemaArrayType(
    const NormalizedSourceSchemaArrayType& other) {
    source_range = other.source_range;
    if (other.element_type != nullptr) {
        element_type = std::make_unique<NormalizedSourceSchemaType>(*other.element_type);
    }
}

NormalizedSourceSchemaArrayType& NormalizedSourceSchemaArrayType::operator=(
    const NormalizedSourceSchemaArrayType& other) {
    if (this != &other) {
        source_range = other.source_range;
        element_type.reset();
        if (other.element_type != nullptr) {
            element_type = std::make_unique<NormalizedSourceSchemaType>(*other.element_type);
        }
    }
    return *this;
}

NormalizedSourceSchemaType::NormalizedSourceSchemaType() = default;

NormalizedSourceSchemaType::NormalizedSourceSchemaType(NormalizedSourceSchemaTypeReference reference)
    : value(std::move(reference)) {}

NormalizedSourceSchemaType::NormalizedSourceSchemaType(NormalizedSourceSchemaArrayType array)
    : value(std::move(array)) {}

NormalizedSourceSchemaType::NormalizedSourceSchemaType(const NormalizedSourceSchemaType& other) {
    value = other.value;
}

NormalizedSourceSchemaType& NormalizedSourceSchemaType::operator=(
    const NormalizedSourceSchemaType& other) {
    if (this != &other) {
        value = other.value;
    }
    return *this;
}

const NormalizedSourceSchemaTypeReference& NormalizedSourceSchemaType::reference() const {
    return std::get<NormalizedSourceSchemaTypeReference>(value);
}

const NormalizedSourceSchemaArrayType& NormalizedSourceSchemaType::array() const {
    return std::get<NormalizedSourceSchemaArrayType>(value);
}

SourceSchemaNormalizationResult
normalize_source_schema(const SourceSchemaDocument& schema,
                        diagnostics::DiagnosticEngine& diagnostics) {
    SourceSchemaNormalizationResult result;

    NormalizedSourceSchemaDocument normalized;
    normalized.source_range = schema.source_range;
    normalized.namespace_name =
        lower_qualified_name(schema.namespace_spelling, schema.namespace_range, diagnostics,
                             "namespace identifier");
    if (normalized.namespace_name.empty()) {
        return result;
    }

    normalized.record_name =
        lower_identifier(schema.record_name, schema.record_range, diagnostics, "record identifier");
    if (normalized.record_name.text.empty()) {
        return result;
    }
    normalized.record_source_range = schema.record_range;

    normalized.version = schema.version;
    normalized.version_range = schema.version_range;
    normalized.record_type_spelling = schema.record_type_spelling;
    normalized.record_type_range = schema.record_type_range;
    if (schema.imports.has_value()) {
        NormalizedSourceSchemaImports imports;
        imports.source_range = schema.imports->source_range;
        imports.empty = schema.imports->empty;
        normalized.imports = imports;
        normalized.imports_range = schema.imports->source_range;
        if (!schema.imports->empty) {
            emit_error(diagnostics, "BC2403", "non-empty YAML imports are not supported",
                       schema.imports->source_range.is_valid() ? schema.imports->source_range
                                                               : schema.source_range);
            return result;
        }
    }

    for (const SourceSchemaAnnotation& annotation : schema.annotations) {
        const std::optional<NormalizedSourceSchemaAnnotation> normalized_annotation =
            normalize_annotation(annotation, diagnostics, "schema annotation");
        if (!normalized_annotation.has_value()) {
            return result;
        }
        normalized.annotations.push_back(*normalized_annotation);
    }

    for (const SourceSchemaEnum& enumeration : schema.enums) {
        const std::optional<NormalizedSourceSchemaEnum> normalized_enum =
            normalize_enum(enumeration, diagnostics);
        if (!normalized_enum.has_value()) {
            return result;
        }
        normalized.enums.push_back(*normalized_enum);
    }

    for (const SourceSchemaField& field : schema.fields) {
        const std::optional<NormalizedSourceSchemaField> normalized_field =
            normalize_field(field, diagnostics);
        if (!normalized_field.has_value()) {
            return result;
        }
        normalized.fields.push_back(*normalized_field);
    }

    result.document = std::move(normalized);
    return result;
}

} // namespace breadcrumbs::compiler::source_schema
