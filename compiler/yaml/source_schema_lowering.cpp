#include "compiler/yaml/source_schema_lowering.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace breadcrumbs::compiler::yaml {
namespace {

constexpr std::string_view lowering_pass = "yaml-source-lowering";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

void emit_error(diagnostics::DiagnosticEngine& diagnostics, std::string_view id,
                std::string message, support::SourceRange range) {
    auto builder = diagnostics::Diagnostic::create(diagnostic_id(id), diagnostics::Severity::Error,
                                                   std::move(message))
                       .from_pass(std::string(lowering_pass));
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

[[nodiscard]] std::optional<ast::IdentifierSyntax>
lower_identifier(std::string_view text, support::SourceRange range,
                 diagnostics::DiagnosticEngine& diagnostics, std::string_view context) {
    if (!is_valid_identifier(text)) {
        emit_error(diagnostics, "BC2401",
                   "invalid " + std::string(context) + " '" + std::string(text) + "'", range);
        return std::nullopt;
    }

    return ast::IdentifierSyntax{
        .source_range = range,
        .text = std::string(text),
    };
}

struct QualifiedNameParts {
    std::vector<std::string> parts;
    bool has_empty_component = false;
};

[[nodiscard]] QualifiedNameParts split_qualified_name(std::string_view text) {
    QualifiedNameParts result;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('.', begin);
        const std::string_view part =
            end == std::string_view::npos ? text.substr(begin) : text.substr(begin, end - begin);
        if (part.empty()) {
            result.has_empty_component = true;
            return result;
        }
        result.parts.emplace_back(part);
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

[[nodiscard]] std::optional<ast::QualifiedNameSyntax>
lower_qualified_name(std::string_view text, support::SourceRange range,
                     diagnostics::DiagnosticEngine& diagnostics, std::string_view context) {
    const QualifiedNameParts parts = split_qualified_name(text);
    if (parts.has_empty_component) {
        emit_error(diagnostics, "BC2401",
                   "invalid " + std::string(context) + " '" + std::string(text) + "'", range);
        return std::nullopt;
    }

    std::vector<ast::IdentifierSyntax> identifiers;
    identifiers.reserve(parts.parts.size());
    for (const std::string& part : parts.parts) {
        if (!is_valid_identifier(part)) {
            emit_error(diagnostics, "BC2401",
                       "invalid " + std::string(context) + " '" + std::string(text) + "'", range);
            return std::nullopt;
        }
        identifiers.push_back(ast::IdentifierSyntax{
            .source_range = range,
            .text = part,
        });
    }

    return ast::QualifiedNameSyntax{
        .source_range = range,
        .parts = std::move(identifiers),
    };
}

[[nodiscard]] std::optional<ast::TypeSyntax>
lower_type_spelling(std::string_view spelling, support::SourceRange range,
                    diagnostics::DiagnosticEngine& diagnostics) {
    if (spelling.empty()) {
        emit_error(diagnostics, "BC2402", "malformed field type spelling", range);
        return std::nullopt;
    }

    const std::size_t array_suffix = spelling.rfind("[]");
    if (array_suffix != std::string_view::npos && array_suffix + 2 == spelling.size()) {
        const std::string_view base = spelling.substr(0, array_suffix);
        if (base.empty() || base.find('[') != std::string_view::npos ||
            base.find(']') != std::string_view::npos) {
            emit_error(diagnostics, "BC2402",
                       "malformed array field type spelling '" + std::string(spelling) + "'",
                       range);
            return std::nullopt;
        }

        const std::optional<ast::QualifiedNameSyntax> element_name =
            lower_qualified_name(base, range, diagnostics, "field type spelling");
        if (!element_name.has_value()) {
            return std::nullopt;
        }

        return ast::TypeSyntax(ast::ArrayTypeSyntax{
            .source_range = range,
            .element_type = ast::TypeReferenceSyntax{.source_range = range, .name = *element_name},
            .kind = ast::ArrayTypeSyntaxKind::BoundedVariableLength,
            .fixed_size = std::nullopt,
            .fixed_size_source_range = support::SourceRange::invalid(),
        });
    }

    if (spelling.find('[') != std::string_view::npos ||
        spelling.find(']') != std::string_view::npos) {
        emit_error(diagnostics, "BC2402",
                   "malformed field type spelling '" + std::string(spelling) + "'", range);
        return std::nullopt;
    }

    const std::optional<ast::QualifiedNameSyntax> name =
        lower_qualified_name(spelling, range, diagnostics, "field type spelling");
    if (!name.has_value()) {
        return std::nullopt;
    }

    return ast::TypeSyntax(ast::TypeReferenceSyntax{
        .source_range = range,
        .name = *name,
    });
}

[[nodiscard]] std::optional<ast::AnnotationSyntax>
lower_annotation(const SourceSchemaAnnotation& annotation,
                 diagnostics::DiagnosticEngine& diagnostics, std::string_view context) {
    const std::optional<ast::QualifiedNameSyntax> name =
        lower_qualified_name(annotation.name, annotation.name_range, diagnostics, context);
    if (!name.has_value()) {
        return std::nullopt;
    }

    return ast::AnnotationSyntax{
        .source_range = annotation.source_range,
        .name = *name,
        .value = annotation.value,
    };
}

[[nodiscard]] std::vector<ast::AnnotationSyntax>
lower_annotations(const std::vector<SourceSchemaAnnotation>& annotations,
                  diagnostics::DiagnosticEngine& diagnostics, std::string_view context, bool* ok) {
    std::vector<ast::AnnotationSyntax> lowered;
    lowered.reserve(annotations.size());
    for (const SourceSchemaAnnotation& annotation : annotations) {
        const std::optional<ast::AnnotationSyntax> lowered_annotation =
            lower_annotation(annotation, diagnostics, context);
        if (!lowered_annotation.has_value()) {
            *ok = false;
            return {};
        }
        lowered.push_back(*lowered_annotation);
    }
    return lowered;
}

[[nodiscard]] std::optional<ast::FieldDeclarationSyntax>
lower_field(const SourceSchemaField& field, diagnostics::DiagnosticEngine& diagnostics) {
    const std::optional<ast::IdentifierSyntax> name =
        lower_identifier(field.name, field.name_range, diagnostics, "field identifier");
    if (!name.has_value()) {
        return std::nullopt;
    }

    const std::optional<ast::TypeSyntax> type =
        lower_type_spelling(field.type_spelling, field.type_range, diagnostics);
    if (!type.has_value()) {
        return std::nullopt;
    }

    bool annotations_ok = true;
    std::vector<ast::AnnotationSyntax> annotations =
        lower_annotations(field.annotations, diagnostics, "field annotation", &annotations_ok);
    if (!annotations_ok) {
        return std::nullopt;
    }

    ast::FieldDeclarationSyntax lowered{
        .source_range = field.source_range,
        .name = *name,
        .type = *type,
        .annotations = std::move(annotations),
        .max_bytes = field.max_bytes,
        .max_bytes_source_range = field.max_bytes_range,
        .max_elements = field.max_elements,
        .max_elements_source_range = field.max_elements_range,
    };
    return lowered;
}

[[nodiscard]] std::optional<ast::EnumValueDeclarationSyntax>
lower_enum_value(const SourceSchemaEnumValue& value, diagnostics::DiagnosticEngine& diagnostics) {
    const std::optional<ast::IdentifierSyntax> name =
        lower_identifier(value.name, value.name_range, diagnostics, "enum value identifier");
    if (!name.has_value()) {
        return std::nullopt;
    }

    return ast::EnumValueDeclarationSyntax{
        .source_range = value.source_range,
        .name = *name,
        .value = std::to_string(value.value),
        .annotations = {},
    };
}

[[nodiscard]] std::optional<ast::EnumDeclarationSyntax>
lower_enum(const SourceSchemaEnum& enumeration, diagnostics::DiagnosticEngine& diagnostics) {
    const std::optional<ast::IdentifierSyntax> name =
        lower_identifier(enumeration.name, enumeration.name_range, diagnostics, "enum identifier");
    if (!name.has_value()) {
        return std::nullopt;
    }

    bool annotations_ok = true;
    std::vector<ast::AnnotationSyntax> annotations =
        lower_annotations(enumeration.annotations, diagnostics, "enum annotation", &annotations_ok);
    if (!annotations_ok) {
        return std::nullopt;
    }

    ast::EnumDeclarationSyntax lowered{
        .source_range = enumeration.source_range,
        .name = *name,
        .values = {},
        .annotations = std::move(annotations),
    };
    lowered.values.reserve(enumeration.values.size());
    for (const SourceSchemaEnumValue& value : enumeration.values) {
        const std::optional<ast::EnumValueDeclarationSyntax> lowered_value =
            lower_enum_value(value, diagnostics);
        if (!lowered_value.has_value()) {
            return std::nullopt;
        }
        lowered.values.push_back(*lowered_value);
    }

    return lowered;
}

[[nodiscard]] bool imports_are_empty(const YamlNode& imports) {
    if (const auto* sequence = std::get_if<YamlSequenceNode>(&imports.value)) {
        return sequence->elements.empty();
    }
    if (const auto* mapping = std::get_if<YamlMappingNode>(&imports.value)) {
        return mapping->entries.empty();
    }
    if (const auto* scalar = std::get_if<YamlScalarNode>(&imports.value)) {
        return scalar->value.empty();
    }
    return false;
}

[[nodiscard]] std::optional<ast::NamespaceDeclarationSyntax>
lower_namespace(const SourceSchemaDocument& schema, diagnostics::DiagnosticEngine& diagnostics) {
    const std::optional<ast::QualifiedNameSyntax> namespace_name = lower_qualified_name(
        schema.namespace_spelling, schema.namespace_range, diagnostics, "namespace identifier");
    if (!namespace_name.has_value()) {
        return std::nullopt;
    }

    if (schema.imports != nullptr && !imports_are_empty(*schema.imports)) {
        emit_error(diagnostics, "BC2403", "non-empty YAML imports are not supported",
                   schema.imports_range.is_valid() ? schema.imports_range : schema.source_range);
        return std::nullopt;
    }

    bool annotations_ok = true;
    std::vector<ast::AnnotationSyntax> annotations =
        lower_annotations(schema.annotations, diagnostics, "schema annotation", &annotations_ok);
    if (!annotations_ok) {
        return std::nullopt;
    }

    std::vector<ast::DeclarationPtr> declarations;
    declarations.reserve(schema.enums.size() + 1);

    for (const SourceSchemaEnum& enumeration : schema.enums) {
        std::optional<ast::EnumDeclarationSyntax> lowered = lower_enum(enumeration, diagnostics);
        if (!lowered.has_value()) {
            return std::nullopt;
        }
        declarations.push_back(ast::make_declaration(std::move(*lowered)));
    }

    const std::optional<ast::IdentifierSyntax> record_name =
        lower_identifier(schema.record_name, schema.record_range, diagnostics, "record identifier");
    if (!record_name.has_value()) {
        return std::nullopt;
    }

    std::vector<ast::FieldDeclarationSyntax> fields;
    fields.reserve(schema.fields.size());
    for (const SourceSchemaField& field : schema.fields) {
        const std::optional<ast::FieldDeclarationSyntax> lowered_field =
            lower_field(field, diagnostics);
        if (!lowered_field.has_value()) {
            return std::nullopt;
        }
        fields.push_back(*lowered_field);
    }

    ast::RecordDeclarationSyntax record{
        .source_range = schema.source_range,
        .name = *record_name,
        .version = schema.version,
        .version_source_range = schema.version_range,
        .record_type_spelling = schema.record_type_spelling,
        .record_type_source_range = schema.record_type_range,
        .fields = std::move(fields),
        .annotations = {},
    };
    declarations.push_back(ast::make_declaration(std::move(record)));

    ast::NamespaceDeclarationSyntax lowered{
        .source_range = schema.source_range,
        .name = *namespace_name,
        .declarations = std::move(declarations),
        .annotations = std::move(annotations),
    };
    return lowered;
}

} // namespace

SourceSchemaLoweringResult lower_source_schema(const SourceSchemaDocument& schema,
                                               diagnostics::DiagnosticEngine& diagnostics) {
    SourceSchemaLoweringResult result;
    std::optional<ast::NamespaceDeclarationSyntax> lowered = lower_namespace(schema, diagnostics);
    if (!lowered.has_value()) {
        return result;
    }

    ast::SchemaFileSyntax ast_file;
    ast_file.source_range = schema.source_range;
    ast_file.declarations.push_back(ast::make_declaration(std::move(*lowered)));
    result.ast = std::move(ast_file);
    return result;
}

} // namespace breadcrumbs::compiler::yaml
