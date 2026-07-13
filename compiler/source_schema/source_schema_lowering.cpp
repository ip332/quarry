#include "compiler/source_schema/source_schema_lowering.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace breadcrumbs::compiler::source_schema {
namespace {

constexpr std::string_view lowering_pass = "source-schema-lowering";

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

[[nodiscard]] ast::IdentifierSyntax lower_identifier(const SourceSchemaIdentifier& identifier) {
    return ast::IdentifierSyntax{
        .source_range = identifier.source_range,
        .text = identifier.text,
    };
}

[[nodiscard]] ast::QualifiedNameSyntax lower_qualified_name(const SourceSchemaQualifiedName& name) {
    ast::QualifiedNameSyntax lowered;
    lowered.source_range = name.source_range;
    lowered.parts.reserve(name.parts.size());
    for (const SourceSchemaIdentifier& part : name.parts) {
        lowered.parts.push_back(lower_identifier(part));
    }
    return lowered;
}

[[nodiscard]] std::optional<ast::TypeReferenceSyntax>
lower_type_reference(const NormalizedSourceSchemaType& type, diagnostics::DiagnosticEngine& diagnostics,
                     support::SourceRange range) {
    if (const auto* reference = std::get_if<NormalizedSourceSchemaTypeReference>(&type.value)) {
        return ast::TypeReferenceSyntax{
            .source_range = reference->source_range,
            .name = lower_qualified_name(reference->name),
        };
    }

    emit_error(diagnostics, "BC2402",
               "compatibility AST cannot represent nested arrays in field types", range);
    return std::nullopt;
}

[[nodiscard]] std::optional<ast::TypeSyntax>
lower_type(const NormalizedSourceSchemaType& type, diagnostics::DiagnosticEngine& diagnostics) {
    return std::visit(
        [&](const auto& typed) -> std::optional<ast::TypeSyntax> {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, NormalizedSourceSchemaTypeReference>) {
                return ast::TypeSyntax(ast::TypeReferenceSyntax{
                    .source_range = typed.source_range,
                    .name = lower_qualified_name(typed.name),
                });
            } else {
                if (typed.element_type == nullptr) {
                    emit_error(diagnostics, "BC2402",
                               "normalized source schema array is missing an element type",
                               typed.source_range);
                    return std::nullopt;
                }
                const std::optional<ast::TypeReferenceSyntax> element_type =
                    lower_type_reference(*typed.element_type, diagnostics, typed.source_range);
                if (!element_type.has_value()) {
                    return std::nullopt;
                }

                ast::ArrayTypeSyntax array_type;
                array_type.source_range = typed.source_range;
                array_type.element_type = *element_type;
                array_type.kind = ast::ArrayTypeSyntaxKind::BoundedVariableLength;
                array_type.fixed_size = std::nullopt;
                array_type.fixed_size_source_range = support::SourceRange::invalid();
                return ast::TypeSyntax(std::move(array_type));
            }
        },
        type.value);
}

[[nodiscard]] ast::AnnotationSyntax lower_annotation(const NormalizedSourceSchemaAnnotation& annotation) {
    ast::AnnotationSyntax lowered;
    lowered.source_range = annotation.source_range;
    lowered.name = lower_qualified_name(SourceSchemaQualifiedName{
        .source_range = annotation.name.source_range,
        .parts = {annotation.name},
    });
    lowered.value = annotation.value;
    return lowered;
}

[[nodiscard]] std::optional<ast::FieldDeclarationSyntax>
lower_field(const NormalizedSourceSchemaField& field, diagnostics::DiagnosticEngine& diagnostics) {
    const std::optional<ast::TypeSyntax> lowered_type = lower_type(field.type, diagnostics);
    if (!lowered_type.has_value()) {
        return std::nullopt;
    }

    ast::FieldDeclarationSyntax lowered{
        .source_range = field.source_range,
        .name = lower_identifier(field.name),
        .type = *lowered_type,
        .annotations = {},
        .max_bytes = field.max_bytes,
        .max_bytes_source_range = field.max_bytes_range,
        .max_elements = field.max_elements,
        .max_elements_source_range = field.max_elements_range,
    };
    lowered.annotations.reserve(field.annotations.size());
    for (const NormalizedSourceSchemaAnnotation& annotation : field.annotations) {
        lowered.annotations.push_back(lower_annotation(annotation));
    }
    return lowered;
}

[[nodiscard]] ast::EnumValueDeclarationSyntax lower_enum_value(const NormalizedSourceSchemaEnumValue& value) {
    return ast::EnumValueDeclarationSyntax{
        .source_range = value.source_range,
        .name = lower_identifier(value.name),
        .value = std::to_string(value.value),
        .annotations = {},
    };
}

[[nodiscard]] ast::EnumDeclarationSyntax lower_enum(const NormalizedSourceSchemaEnum& enumeration) {
    ast::EnumDeclarationSyntax lowered{
        .source_range = enumeration.source_range,
        .name = lower_identifier(enumeration.name),
        .values = {},
        .annotations = {},
    };
    lowered.annotations.reserve(enumeration.annotations.size());
    for (const NormalizedSourceSchemaAnnotation& annotation : enumeration.annotations) {
        lowered.annotations.push_back(lower_annotation(annotation));
    }
    lowered.values.reserve(enumeration.values.size());
    for (const NormalizedSourceSchemaEnumValue& value : enumeration.values) {
        lowered.values.push_back(lower_enum_value(value));
    }
    return lowered;
}

} // namespace

SourceSchemaLoweringResult
lower_source_schema(const NormalizedSourceSchemaDocument& schema,
                    diagnostics::DiagnosticEngine& diagnostics) {
    SourceSchemaLoweringResult result;
    (void)diagnostics;

    ast::NamespaceDeclarationSyntax lowered_namespace{
        .source_range = schema.source_range,
        .name = lower_qualified_name(schema.namespace_name),
        .declarations = {},
        .annotations = {},
    };
    lowered_namespace.annotations.reserve(schema.annotations.size());
    for (const NormalizedSourceSchemaAnnotation& annotation : schema.annotations) {
        lowered_namespace.annotations.push_back(lower_annotation(annotation));
    }

    lowered_namespace.declarations.reserve(schema.enums.size() + 1U);
    for (const NormalizedSourceSchemaEnum& enumeration : schema.enums) {
        lowered_namespace.declarations.push_back(ast::make_declaration(lower_enum(enumeration)));
    }

    std::vector<ast::FieldDeclarationSyntax> fields;
    fields.reserve(schema.fields.size());
    for (const NormalizedSourceSchemaField& field : schema.fields) {
        const std::optional<ast::FieldDeclarationSyntax> lowered_field = lower_field(field, diagnostics);
        if (!lowered_field.has_value()) {
            return result;
        }
        fields.push_back(*lowered_field);
    }

    ast::RecordDeclarationSyntax record{
        .source_range = schema.record_source_range,
        .name = lower_identifier(schema.record_name),
        .version = schema.version,
        .version_source_range = schema.version_range,
        .record_type_spelling = schema.record_type_spelling,
        .record_type_source_range = schema.record_type_range,
        .fields = std::move(fields),
        .annotations = {},
    };

    lowered_namespace.declarations.push_back(ast::make_declaration(std::move(record)));

    ast::SchemaFileSyntax ast_file;
    ast_file.source_range = schema.source_range;
    ast_file.declarations.push_back(ast::make_declaration(std::move(lowered_namespace)));
    result.ast = std::move(ast_file);
    return result;
}

} // namespace breadcrumbs::compiler::source_schema
