#pragma once

#include "compiler/diagnostics/diagnostic.hpp"
#include "compiler/source_schema/source_schema.hpp"
#include "compiler/support/source_location.hpp"
#include "compiler/symbols/symbols.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace breadcrumbs::compiler::semantic {

enum class SemanticPrimitiveType {
    Bool,
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64,
    F32,
    F64,
};

enum class SemanticRecordType {
    Data,
    Command,
    Event,
    Configuration,
    Diagnostics,
};

struct SemanticStringType {
    std::uint32_t max_bytes = 0;
};

struct SemanticBytesType {
    std::uint32_t max_bytes = 0;
};

struct SemanticRecordReferenceType {
    std::string canonical_target_fqn;
};

struct SemanticEnumReferenceType {
    std::string canonical_target_fqn;
};

struct SemanticType;

struct SemanticArrayType {
    std::uint32_t max_elements = 0;
    std::unique_ptr<SemanticType> element_type;

    SemanticArrayType();
    SemanticArrayType(const SemanticArrayType& other);
    SemanticArrayType& operator=(const SemanticArrayType& other);
    SemanticArrayType(SemanticArrayType&&) noexcept = default;
    SemanticArrayType& operator=(SemanticArrayType&&) noexcept = default;
};

using SemanticTypeValue =
    std::variant<std::monostate, SemanticPrimitiveType, SemanticStringType, SemanticBytesType,
                 SemanticRecordReferenceType, SemanticEnumReferenceType, SemanticArrayType>;

struct SemanticType {
    SemanticTypeValue value;

    SemanticType();
    SemanticType(SemanticPrimitiveType primitive);
    SemanticType(SemanticStringType string_type);
    SemanticType(SemanticBytesType bytes_type);
    SemanticType(SemanticRecordReferenceType record_reference);
    SemanticType(SemanticEnumReferenceType enum_reference);
    SemanticType(SemanticArrayType array);

    [[nodiscard]] bool is_valid() const;
    [[nodiscard]] bool is_primitive() const;
    [[nodiscard]] SemanticPrimitiveType primitive() const;
    [[nodiscard]] bool is_string() const;
    [[nodiscard]] bool is_bytes() const;
    [[nodiscard]] bool is_record_reference() const;
    [[nodiscard]] bool is_enum_reference() const;
    [[nodiscard]] bool is_array() const;
    [[nodiscard]] const SemanticRecordReferenceType& record_reference() const;
    [[nodiscard]] const SemanticEnumReferenceType& enum_reference() const;
    [[nodiscard]] const SemanticArrayType& array() const;
};

struct SemanticField {
    support::SourceRange source_range;
    std::string name;
    SemanticType type;
};

struct SemanticRecord {
    support::SourceRange source_range;
    std::string fqn;
    std::optional<std::uint32_t> version = std::nullopt;
    std::optional<SemanticRecordType> record_type = std::nullopt;
    std::vector<SemanticField> fields;

    [[nodiscard]] const SemanticField* find_field(std::string_view name) const;
};

struct SemanticModel {
    std::vector<SemanticRecord> records;

    [[nodiscard]] const SemanticRecord* find_record(std::string_view fqn) const;
};

class SemanticValidator {
public:
    [[nodiscard]] SemanticModel
    validate(const source_schema::NormalizedSourceSchemaDocument& schema,
             const symbols::SymbolTable& symbol_model,
             diagnostics::DiagnosticEngine& diagnostics) const;
};

} // namespace breadcrumbs::compiler::semantic
