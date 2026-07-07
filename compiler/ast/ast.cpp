#include "compiler/ast/ast.hpp"

#include <sstream>
#include <type_traits>
#include <utility>

namespace breadcrumbs::compiler::ast {
namespace {

void indent(std::ostream& output, int depth) {
    for (int index = 0; index < depth; ++index) {
        output << "  ";
    }
}

void dump_type(const TypeSyntax& type, std::ostream& output) {
    std::visit(
        [&](const auto& typed) {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, TypeReferenceSyntax>) {
                output << typed.name.text();
            } else if constexpr (std::is_same_v<Type, ArrayTypeSyntax>) {
                output << typed.element_type.name.text() << '[';
                if (typed.fixed_size.has_value()) {
                    output << *typed.fixed_size;
                }
                output << ']';
            }
        },
        type);
}

void dump_declaration(const DeclarationSyntax& declaration, std::ostream& output, int depth);

void dump_namespace(const NamespaceDeclarationSyntax& namespace_declaration, std::ostream& output,
                    int depth) {
    indent(output, depth);
    output << "namespace " << namespace_declaration.name.text() << '\n';
    for (const DeclarationPtr& child : namespace_declaration.declarations) {
        if (child != nullptr) {
            dump_declaration(*child, output, depth + 1);
        }
    }
}

void dump_record(const RecordDeclarationSyntax& record, std::ostream& output, int depth) {
    indent(output, depth);
    output << "record " << record.name.text << '\n';
    for (const FieldDeclarationSyntax& field : record.fields) {
        indent(output, depth + 1);
        output << "field " << field.name.text << ": ";
        dump_type(field.type, output);
        output << '\n';
    }
}

void dump_enum(const EnumDeclarationSyntax& enum_declaration, std::ostream& output, int depth) {
    indent(output, depth);
    output << "enum " << enum_declaration.name.text << '\n';
    for (const EnumValueDeclarationSyntax& value : enum_declaration.values) {
        indent(output, depth + 1);
        output << "value " << value.name.text;
        if (value.value.has_value()) {
            output << " = " << *value.value;
        }
        output << '\n';
    }
}

void dump_declaration(const DeclarationSyntax& declaration, std::ostream& output, int depth) {
    std::visit(
        [&](const auto& typed) {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, ImportDeclarationSyntax>) {
                indent(output, depth);
                output << "import " << typed.imported_name.text() << '\n';
            } else if constexpr (std::is_same_v<Type, NamespaceDeclarationSyntax>) {
                dump_namespace(typed, output, depth);
            } else if constexpr (std::is_same_v<Type, RecordDeclarationSyntax>) {
                dump_record(typed, output, depth);
            } else if constexpr (std::is_same_v<Type, EnumDeclarationSyntax>) {
                dump_enum(typed, output, depth);
            }
        },
        declaration.value);
}

} // namespace

bool QualifiedNameSyntax::empty() const { return parts.empty(); }

std::string QualifiedNameSyntax::text() const {
    std::string result;
    for (const IdentifierSyntax& part : parts) {
        if (!result.empty()) {
            result += '.';
        }
        result += part.text;
    }
    return result;
}

DeclarationPtr make_declaration(DeclarationSyntax::Value value) {
    return std::make_unique<DeclarationSyntax>(DeclarationSyntax{.value = std::move(value)});
}

std::string_view declaration_kind(const DeclarationSyntax& declaration) {
    return std::visit(
        [](const auto& typed) -> std::string_view {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, ImportDeclarationSyntax>) {
                return "import";
            } else if constexpr (std::is_same_v<Type, NamespaceDeclarationSyntax>) {
                return "namespace";
            } else if constexpr (std::is_same_v<Type, RecordDeclarationSyntax>) {
                return "record";
            } else if constexpr (std::is_same_v<Type, EnumDeclarationSyntax>) {
                return "enum";
            }
        },
        declaration.value);
}

std::string_view type_kind(const TypeSyntax& type) {
    return std::visit(
        [](const auto& typed) -> std::string_view {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, TypeReferenceSyntax>) {
                return "type_reference";
            } else if constexpr (std::is_same_v<Type, ArrayTypeSyntax>) {
                return "array_type";
            }
        },
        type);
}

void dump_schema_file(const SchemaFileSyntax& schema_file, std::ostream& output) {
    output << "schema_file\n";
    for (const DeclarationPtr& declaration : schema_file.declarations) {
        if (declaration != nullptr) {
            dump_declaration(*declaration, output, 1);
        }
    }
}

std::string dump_schema_file(const SchemaFileSyntax& schema_file) {
    std::ostringstream output;
    dump_schema_file(schema_file, output);
    return output.str();
}

} // namespace breadcrumbs::compiler::ast
