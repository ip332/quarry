#pragma once

#include "compiler/support/source_location.hpp"
#include "compiler/yaml/yaml_document.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace breadcrumbs::compiler::yaml {

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
    std::optional<std::int64_t> max_bytes;
    support::SourceRange max_bytes_range = support::SourceRange::invalid();
    std::optional<std::int64_t> max_elements;
    support::SourceRange max_elements_range = support::SourceRange::invalid();
    std::vector<SourceSchemaAnnotation> annotations;
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

struct SourceSchemaDocument {
    support::SourceRange source_range = support::SourceRange::invalid();
    std::string namespace_spelling;
    support::SourceRange namespace_range = support::SourceRange::invalid();
    std::string record_name;
    support::SourceRange record_range = support::SourceRange::invalid();
    std::int64_t version = 0;
    support::SourceRange version_range = support::SourceRange::invalid();
    std::string record_type_spelling;
    support::SourceRange record_type_range = support::SourceRange::invalid();
    YamlNodePtr imports;
    support::SourceRange imports_range = support::SourceRange::invalid();
    std::vector<SourceSchemaField> fields;
    std::vector<SourceSchemaEnum> enums;
    std::vector<SourceSchemaAnnotation> annotations;
};

struct YamlDecodeResult {
    std::optional<SourceSchemaDocument> schema;
};

} // namespace breadcrumbs::compiler::yaml
