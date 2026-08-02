#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/support/source_location.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace quarry::compiler::source_schema {

struct SourceSchemaAnnotation {
    std::string name;
    std::string value;
    support::SourceRange source_range = support::SourceRange::invalid();
    support::SourceRange name_range = support::SourceRange::invalid();
    support::SourceRange value_range = support::SourceRange::invalid();
};

struct SourceSchemaField {
    std::string name;
    support::SourceRange source_range = support::SourceRange::invalid();
    support::SourceRange name_range = support::SourceRange::invalid();
    std::string type_spelling;
    support::SourceRange type_range = support::SourceRange::invalid();
    std::optional<std::int64_t> max_bytes = std::nullopt;
    support::SourceRange max_bytes_range = support::SourceRange::invalid();
    std::optional<std::int64_t> max_elements = std::nullopt;
    support::SourceRange max_elements_range = support::SourceRange::invalid();
    std::vector<SourceSchemaAnnotation> annotations{};
};

struct SourceSchemaEnumValue {
    std::string name;
    support::SourceRange source_range = support::SourceRange::invalid();
    support::SourceRange name_range = support::SourceRange::invalid();
    std::int64_t value = 0;
    support::SourceRange value_range = support::SourceRange::invalid();
};

struct SourceSchemaEnum {
    std::string name;
    support::SourceRange source_range = support::SourceRange::invalid();
    support::SourceRange name_range = support::SourceRange::invalid();
    std::vector<SourceSchemaEnumValue> values;
    std::vector<SourceSchemaAnnotation> annotations;
};

struct SourceSchemaImports {
    support::SourceRange source_range = support::SourceRange::invalid();
    bool empty = true;
    struct Import {
        std::string path;
        support::SourceRange source_range = support::SourceRange::invalid();
    };
    std::vector<Import> entries;
};

struct SourceSchemaDocument {
    support::SourceRange source_range = support::SourceRange::invalid();
    std::string namespace_spelling;
    support::SourceRange namespace_range = support::SourceRange::invalid();
    std::string record_name;
    support::SourceRange record_range = support::SourceRange::invalid();
    std::int64_t version = 0;
    support::SourceRange version_range = support::SourceRange::invalid();
    std::string record_type_spelling{};
    support::SourceRange record_type_range = support::SourceRange::invalid();
    std::optional<SourceSchemaImports> imports;
    support::SourceRange imports_range = support::SourceRange::invalid();
    std::vector<SourceSchemaField> fields;
    std::vector<SourceSchemaEnum> enums;
    std::vector<SourceSchemaAnnotation> annotations;
};

struct SourceSchemaDecodeResult {
    std::optional<SourceSchemaDocument> schema;
};

struct SourceSchemaIdentifier {
    std::string text;
    support::SourceRange source_range = support::SourceRange::invalid();
};

struct SourceSchemaQualifiedName {
    support::SourceRange source_range = support::SourceRange::invalid();
    std::vector<SourceSchemaIdentifier> parts;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::string text() const;
};

struct NormalizedSourceSchemaAnnotation {
    SourceSchemaIdentifier name;
    std::string value;
    support::SourceRange source_range = support::SourceRange::invalid();
    support::SourceRange value_range = support::SourceRange::invalid();
};

struct NormalizedSourceSchemaType;

struct NormalizedSourceSchemaTypeReference {
    support::SourceRange source_range = support::SourceRange::invalid();
    SourceSchemaQualifiedName name;
};

struct NormalizedSourceSchemaArrayType {
    support::SourceRange source_range = support::SourceRange::invalid();
    std::unique_ptr<NormalizedSourceSchemaType> element_type;

    NormalizedSourceSchemaArrayType();
    NormalizedSourceSchemaArrayType(const NormalizedSourceSchemaArrayType& other);
    NormalizedSourceSchemaArrayType& operator=(const NormalizedSourceSchemaArrayType& other);
    NormalizedSourceSchemaArrayType(NormalizedSourceSchemaArrayType&&) noexcept = default;
    NormalizedSourceSchemaArrayType& operator=(NormalizedSourceSchemaArrayType&&) noexcept = default;
};

using NormalizedSourceSchemaTypeValue =
    std::variant<NormalizedSourceSchemaTypeReference, NormalizedSourceSchemaArrayType>;

struct NormalizedSourceSchemaType {
    NormalizedSourceSchemaTypeValue value;

    NormalizedSourceSchemaType();
    NormalizedSourceSchemaType(NormalizedSourceSchemaTypeReference reference);
    NormalizedSourceSchemaType(NormalizedSourceSchemaArrayType array);
    NormalizedSourceSchemaType(const NormalizedSourceSchemaType& other);
    NormalizedSourceSchemaType& operator=(const NormalizedSourceSchemaType& other);
    NormalizedSourceSchemaType(NormalizedSourceSchemaType&&) noexcept = default;
    NormalizedSourceSchemaType& operator=(NormalizedSourceSchemaType&&) noexcept = default;

    [[nodiscard]] bool is_reference() const;
    [[nodiscard]] bool is_array() const;
    [[nodiscard]] const NormalizedSourceSchemaTypeReference& reference() const;
    [[nodiscard]] const NormalizedSourceSchemaArrayType& array() const;
};

struct NormalizedSourceSchemaField {
    SourceSchemaIdentifier name;
    support::SourceRange source_range = support::SourceRange::invalid();
    NormalizedSourceSchemaType type;
    std::optional<std::int64_t> max_bytes = std::nullopt;
    support::SourceRange max_bytes_range = support::SourceRange::invalid();
    std::optional<std::int64_t> max_elements = std::nullopt;
    support::SourceRange max_elements_range = support::SourceRange::invalid();
    std::vector<NormalizedSourceSchemaAnnotation> annotations{};
};

struct NormalizedSourceSchemaEnumValue {
    SourceSchemaIdentifier name;
    support::SourceRange source_range = support::SourceRange::invalid();
    std::int64_t value = 0;
    support::SourceRange value_range = support::SourceRange::invalid();
};

struct NormalizedSourceSchemaEnum {
    SourceSchemaIdentifier name;
    support::SourceRange source_range = support::SourceRange::invalid();
    std::vector<NormalizedSourceSchemaEnumValue> values;
    std::vector<NormalizedSourceSchemaAnnotation> annotations;
};

struct NormalizedSourceSchemaImports {
    support::SourceRange source_range = support::SourceRange::invalid();
    bool empty = true;
    struct Import {
        std::string path;
        support::SourceRange source_range = support::SourceRange::invalid();
    };
    std::vector<Import> entries;
};

struct NormalizedSourceSchemaDocument {
    support::SourceRange source_range = support::SourceRange::invalid();
    SourceSchemaQualifiedName namespace_name;
    SourceSchemaIdentifier record_name;
    support::SourceRange record_source_range = support::SourceRange::invalid();
    std::int64_t version = 0;
    support::SourceRange version_range = support::SourceRange::invalid();
    std::string record_type_spelling{};
    support::SourceRange record_type_range = support::SourceRange::invalid();
    std::optional<NormalizedSourceSchemaImports> imports;
    support::SourceRange imports_range = support::SourceRange::invalid();
    std::vector<NormalizedSourceSchemaField> fields;
    std::vector<NormalizedSourceSchemaEnum> enums;
    std::vector<NormalizedSourceSchemaAnnotation> annotations;
};

struct SourceSchemaNormalizationResult {
    std::optional<NormalizedSourceSchemaDocument> document;
};

[[nodiscard]] SourceSchemaNormalizationResult
normalize_source_schema(const SourceSchemaDocument& schema,
                        diagnostics::DiagnosticEngine& diagnostics);

} // namespace quarry::compiler::source_schema
