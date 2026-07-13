#pragma once

#include "compiler/support/source_location.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace breadcrumbs::compiler::ast {

struct IdentifierSyntax {
    support::SourceRange source_range;
    std::string text;
};

struct QualifiedNameSyntax {
    support::SourceRange source_range;
    std::vector<IdentifierSyntax> parts;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::string text() const;
};

struct AnnotationSyntax {
    support::SourceRange source_range;
    QualifiedNameSyntax name;
    std::optional<std::string> value;
};

struct TypeReferenceSyntax {
    support::SourceRange source_range;
    QualifiedNameSyntax name;
};

enum class ArrayTypeSyntaxKind {
    LegacyFixedSize,
    BoundedVariableLength,
};

struct ArrayTypeSyntax {
    support::SourceRange source_range;
    TypeReferenceSyntax element_type;
    ArrayTypeSyntaxKind kind = ArrayTypeSyntaxKind::LegacyFixedSize;
    std::optional<std::size_t> fixed_size;
    support::SourceRange fixed_size_source_range = support::SourceRange::invalid();
};

using TypeSyntax = std::variant<TypeReferenceSyntax, ArrayTypeSyntax>;

struct FieldDeclarationSyntax {
    support::SourceRange source_range;
    IdentifierSyntax name;
    TypeSyntax type;
    std::vector<AnnotationSyntax> annotations;
    std::optional<std::int64_t> max_bytes;
    support::SourceRange max_bytes_source_range = support::SourceRange::invalid();
    std::optional<std::int64_t> max_elements;
    support::SourceRange max_elements_source_range = support::SourceRange::invalid();
};

struct EnumValueDeclarationSyntax {
    support::SourceRange source_range;
    IdentifierSyntax name;
    std::optional<std::string> value;
    std::vector<AnnotationSyntax> annotations;
};

struct EnumDeclarationSyntax {
    support::SourceRange source_range;
    IdentifierSyntax name;
    std::vector<EnumValueDeclarationSyntax> values;
    std::vector<AnnotationSyntax> annotations;
};

struct RecordDeclarationSyntax {
    support::SourceRange source_range;
    IdentifierSyntax name;
    std::int64_t version = 0;
    support::SourceRange version_source_range = support::SourceRange::invalid();
    std::string record_type_spelling;
    support::SourceRange record_type_source_range = support::SourceRange::invalid();
    std::vector<FieldDeclarationSyntax> fields;
    std::vector<AnnotationSyntax> annotations;
};

struct ImportDeclarationSyntax {
    support::SourceRange source_range;
    QualifiedNameSyntax imported_name;
};

struct DeclarationSyntax;
struct NamespaceDeclarationSyntax;
using DeclarationPtr = std::unique_ptr<DeclarationSyntax>;

struct NamespaceDeclarationSyntax {
    support::SourceRange source_range;
    QualifiedNameSyntax name;
    std::vector<DeclarationPtr> declarations;
    std::vector<AnnotationSyntax> annotations;
};

struct DeclarationSyntax {
    using Value = std::variant<ImportDeclarationSyntax, NamespaceDeclarationSyntax,
                               RecordDeclarationSyntax, EnumDeclarationSyntax>;

    Value value;
};

struct SchemaFileSyntax {
    support::SourceRange source_range;
    std::vector<DeclarationPtr> declarations;
};

using Ast = SchemaFileSyntax;

[[nodiscard]] DeclarationPtr make_declaration(DeclarationSyntax::Value value);
[[nodiscard]] std::string_view declaration_kind(const DeclarationSyntax& declaration);
[[nodiscard]] std::string_view type_kind(const TypeSyntax& type);

void dump_schema_file(const SchemaFileSyntax& schema_file, std::ostream& output);
[[nodiscard]] std::string dump_schema_file(const SchemaFileSyntax& schema_file);

} // namespace breadcrumbs::compiler::ast
