#include "compiler/schema_ir/validation.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace breadcrumbs::compiler::schema_ir {
namespace {

constexpr std::string_view schema_ir_pass = "schema_ir_validation";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    return parsed.value();
}

enum class ObjectKind {
    Namespace,
    Record,
    Enum,
};

struct ObjectInfo {
    ObjectKind kind;
    std::optional<support::SourceRange> source_range;
};

[[nodiscard]] std::string_view kind_name(ObjectKind kind) {
    switch (kind) {
    case ObjectKind::Namespace:
        return "namespace";
    case ObjectKind::Record:
        return "record";
    case ObjectKind::Enum:
        return "enum";
    }

    return "object";
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

[[nodiscard]] std::string qualify_fqn(std::string_view parent_fqn, std::string_view name) {
    if (parent_fqn.empty()) {
        return std::string(name);
    }

    std::string qualified(parent_fqn);
    qualified.push_back('.');
    qualified.append(name);
    return qualified;
}

[[nodiscard]] std::optional<support::SourceFileId>
find_source_file_id(const support::SourceManager& source_manager, std::string_view path) {
    const std::vector<support::SourceFile>& sources = source_manager.sources();
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (sources[index].path == path) {
            return support::SourceFileId(static_cast<support::SourceFileId::ValueType>(index));
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<support::SourceRange>
source_range_from_origin(const ::breadcrumbs::schema_ir::SourceOrigin& origin,
                         const support::SourceManager& source_manager) {
    std::string_view path = origin.file();
    if (path.empty()) {
        path = origin.source_unit();
    }
    if (path.empty()) {
        return std::nullopt;
    }

    const std::optional<support::SourceFileId> file_id = find_source_file_id(source_manager, path);
    if (!file_id.has_value()) {
        return std::nullopt;
    }

    const support::SourceLocation begin(*file_id,
                                        static_cast<std::size_t>(origin.span().start_offset()));
    const support::SourceLocation end(*file_id,
                                      static_cast<std::size_t>(origin.span().end_offset()));
    const support::SourceRange range(begin, end);
    if (!source_manager.is_valid_range(range)) {
        return std::nullopt;
    }

    return range;
}

void emit_diagnostic(diagnostics::DiagnosticCollection& diagnostics, diagnostics::DiagnosticId id,
                     diagnostics::Severity severity, std::string message,
                     std::optional<support::SourceRange> range,
                     std::optional<diagnostics::RelatedLocation> related = std::nullopt) {
    auto builder = diagnostics::Diagnostic::create(std::move(id), severity, std::move(message));
    builder.from_pass(std::string(schema_ir_pass));
    if (range.has_value()) {
        builder.at(*range);
    }
    if (related.has_value()) {
        builder.with_related(std::move(*related));
    }
    diagnostics.emit(builder.build());
}

[[nodiscard]] diagnostics::RelatedLocation
make_related_location(const std::optional<support::SourceRange>& range, std::string message) {
    if (range.has_value()) {
        return diagnostics::RelatedLocation::at_range(*range, std::move(message));
    }

    return diagnostics::RelatedLocation::at_location(support::SourceLocation::invalid(),
                                                     std::move(message));
}

} // namespace

class SchemaIrValidatorImpl {
public:
    SchemaIrValidatorImpl(const SchemaIrModel& schema_ir, context::CompilerContext& context,
                          diagnostics::DiagnosticCollection& diagnostics)
        : schema_ir_(schema_ir), source_manager_(context.source_manager()),
          diagnostics_(diagnostics) {}

    void validate() {
        if (!schema_ir_.has_root_namespace()) {
            emit_diagnostic(diagnostics_, diagnostic_id("BC6001"), diagnostics::Severity::Error,
                            "schema IR root namespace is missing", std::nullopt);
            return;
        }

        collect_ids(schema_ir_.root_namespace());
        validate_namespace(schema_ir_.root_namespace(), std::nullopt, true);
    }

private:
    void collect_ids(const ::breadcrumbs::schema_ir::NamespaceIR& namespace_ir) {
        const std::optional<support::SourceRange> namespace_range =
            source_range_from_origin(namespace_ir.source_origin(), source_manager_);
        register_object(namespace_ir.ir_id(), ObjectKind::Namespace, namespace_range);

        for (const ::breadcrumbs::schema_ir::NamespaceIR& child : namespace_ir.namespaces()) {
            collect_ids(child);
        }

        for (const ::breadcrumbs::schema_ir::RecordIR& record : namespace_ir.records()) {
            const std::optional<support::SourceRange> record_range =
                source_range_from_origin(record.source_origin(), source_manager_);
            register_object(record.ir_id(), ObjectKind::Record, record_range);
            register_record_id(record.record_id(), record_range);
        }

        for (const ::breadcrumbs::schema_ir::EnumIR& enum_ir : namespace_ir.enums()) {
            const std::optional<support::SourceRange> enum_range =
                source_range_from_origin(enum_ir.source_origin(), source_manager_);
            register_object(enum_ir.ir_id(), ObjectKind::Enum, enum_range);
        }
    }

    void register_record_id(uint32_t record_id, const std::optional<support::SourceRange>& range) {
        if (record_id == 0U) {
            emit_diagnostic(diagnostics_, diagnostic_id("BC6010"), diagnostics::Severity::Error,
                            "schema IR record has a missing or zero record_id", range);
            return;
        }

        const auto inserted = record_ids_.emplace(record_id, range);
        if (!inserted.second) {
            const std::optional<support::SourceRange> previous_range = inserted.first->second;
            emit_diagnostic(diagnostics_, diagnostic_id("BC6011"), diagnostics::Severity::Error,
                            "schema IR record id " + std::to_string(record_id) + " is duplicated",
                            range,
                            previous_range.has_value()
                                ? std::optional<diagnostics::RelatedLocation>(make_related_location(
                                      previous_range, "previous record with this id is here"))
                                : std::nullopt);
        }
    }

    void register_object(uint64_t ir_id, ObjectKind kind,
                         const std::optional<support::SourceRange>& range) {
        if (ir_id == 0) {
            emit_diagnostic(diagnostics_, diagnostic_id("BC6004"), diagnostics::Severity::Error,
                            std::string("schema IR ") + std::string(kind_name(kind)) +
                                " has a missing or zero ir_id",
                            range);
            return;
        }

        const auto inserted = objects_by_id_.emplace(ir_id, ObjectInfo{kind, range});
        if (!inserted.second) {
            const std::optional<support::SourceRange> previous_range =
                inserted.first->second.source_range;
            emit_diagnostic(diagnostics_, diagnostic_id("BC6004"), diagnostics::Severity::Error,
                            "schema IR object id " + std::to_string(ir_id) + " is duplicated",
                            range,
                            previous_range.has_value()
                                ? std::optional<diagnostics::RelatedLocation>(make_related_location(
                                      previous_range, "previous object is here"))
                                : std::nullopt);
            return;
        }

        if (kind == ObjectKind::Record) {
            record_by_id_.emplace(ir_id, inserted.first->second);
        } else if (kind == ObjectKind::Enum) {
            enum_by_id_.emplace(ir_id, inserted.first->second);
        }
    }

    bool validate_name(std::string_view name, std::string_view item_kind,
                       const std::optional<support::SourceRange>& range) {
        if (is_valid_identifier(name)) {
            return true;
        }

        emit_diagnostic(diagnostics_, diagnostic_id("BC6001"), diagnostics::Severity::Error,
                        std::string("schema IR ") + std::string(item_kind) +
                            " has an invalid name '" + std::string(name) + "'",
                        range);
        return false;
    }

    bool validate_fqn(std::string_view parent_fqn, std::string_view name,
                      std::string_view actual_fqn, ObjectKind kind,
                      const std::optional<support::SourceRange>& range) {
        const std::string expected_fqn = qualify_fqn(parent_fqn, name);
        if (expected_fqn == actual_fqn) {
            return true;
        }

        emit_diagnostic(diagnostics_, diagnostic_id("BC6001"), diagnostics::Severity::Error,
                        std::string("schema IR ") + std::string(kind_name(kind)) + " '" +
                            std::string(name) + "' has unexpected fully qualified name '" +
                            std::string(actual_fqn) + "'",
                        range);
        return false;
    }

    void validate_namespace(const ::breadcrumbs::schema_ir::NamespaceIR& namespace_ir,
                            std::optional<std::string_view> parent_fqn, bool is_root) {
        const std::optional<support::SourceRange> namespace_range =
            source_range_from_origin(namespace_ir.source_origin(), source_manager_);

        if (!is_root) {
            validate_name(namespace_ir.name(), kind_name(ObjectKind::Namespace), namespace_range);
            if (parent_fqn.has_value()) {
                validate_fqn(*parent_fqn, namespace_ir.name(), namespace_ir.fqn(),
                             ObjectKind::Namespace, namespace_range);
            }
        }

        std::unordered_map<std::string, std::optional<support::SourceRange>> local_names;
        local_names.reserve(static_cast<std::size_t>(namespace_ir.namespaces_size() +
                                                     namespace_ir.records_size() +
                                                     namespace_ir.enums_size()));

        for (const ::breadcrumbs::schema_ir::NamespaceIR& child : namespace_ir.namespaces()) {
            const std::optional<support::SourceRange> child_range =
                source_range_from_origin(child.source_origin(), source_manager_);
            const auto [it, inserted] = local_names.emplace(std::string(child.name()), child_range);
            if (!inserted) {
                emit_diagnostic(
                    diagnostics_, diagnostic_id("BC6002"), diagnostics::Severity::Error,
                    "schema IR namespace '" + std::string(child.name()) +
                        "' is declared more than once in this namespace",
                    child_range,
                    it->second.has_value()
                        ? std::optional<diagnostics::RelatedLocation>(make_related_location(
                              it->second, "previous namespace declaration is here"))
                        : std::nullopt);
            }

            validate_namespace(child, namespace_ir.fqn(), false);
        }

        for (const ::breadcrumbs::schema_ir::RecordIR& record : namespace_ir.records()) {
            const std::optional<support::SourceRange> record_range =
                source_range_from_origin(record.source_origin(), source_manager_);
            const auto [it, inserted] =
                local_names.emplace(std::string(record.name()), record_range);
            if (!inserted) {
                emit_diagnostic(
                    diagnostics_, diagnostic_id("BC6002"), diagnostics::Severity::Error,
                    "schema IR record '" + std::string(record.name()) +
                        "' is declared more than once in this namespace",
                    record_range,
                    it->second.has_value()
                        ? std::optional<diagnostics::RelatedLocation>(
                              make_related_location(it->second, "previous declaration is here"))
                        : std::nullopt);
            }

            validate_name(record.name(), kind_name(ObjectKind::Record), record_range);
            validate_fqn(namespace_ir.fqn(), record.name(), record.fqn(), ObjectKind::Record,
                         record_range);
            validate_record(record);
        }

        for (const ::breadcrumbs::schema_ir::EnumIR& enum_ir : namespace_ir.enums()) {
            const std::optional<support::SourceRange> enum_range =
                source_range_from_origin(enum_ir.source_origin(), source_manager_);
            const auto [it, inserted] =
                local_names.emplace(std::string(enum_ir.name()), enum_range);
            if (!inserted) {
                emit_diagnostic(
                    diagnostics_, diagnostic_id("BC6002"), diagnostics::Severity::Error,
                    "schema IR enum '" + std::string(enum_ir.name()) +
                        "' is declared more than once in this namespace",
                    enum_range,
                    it->second.has_value()
                        ? std::optional<diagnostics::RelatedLocation>(
                              make_related_location(it->second, "previous declaration is here"))
                        : std::nullopt);
            }

            validate_name(enum_ir.name(), kind_name(ObjectKind::Enum), enum_range);
            validate_fqn(namespace_ir.fqn(), enum_ir.name(), enum_ir.fqn(), ObjectKind::Enum,
                         enum_range);
            validate_enum(enum_ir);
        }
    }

    void validate_record(const ::breadcrumbs::schema_ir::RecordIR& record) {
        std::unordered_map<std::string, std::optional<support::SourceRange>> field_names;
        field_names.reserve(static_cast<std::size_t>(record.fields_size()));
        std::unordered_map<std::uint32_t, std::optional<support::SourceRange>> field_indexes;
        field_indexes.reserve(static_cast<std::size_t>(record.fields_size()));

        for (const ::breadcrumbs::schema_ir::FieldIR& field : record.fields()) {
            const std::optional<support::SourceRange> field_range =
                source_range_from_origin(field.source_origin(), source_manager_);
            const std::uint32_t field_index = field.field_index();
            if (field_index > 255U) {
                emit_diagnostic(diagnostics_, diagnostic_id("BC6009"), diagnostics::Severity::Error,
                                "schema IR field '" + std::string(field.name()) +
                                    "' has field_index " + std::to_string(field_index) +
                                    " which exceeds the uint8 limit",
                                field_range);
            } else {
                const auto [index_it, index_inserted] =
                    field_indexes.emplace(field_index, field_range);
                if (!index_inserted) {
                    emit_diagnostic(
                        diagnostics_, diagnostic_id("BC6008"), diagnostics::Severity::Error,
                        "schema IR field '" + std::string(field.name()) + "' reuses field_index " +
                            std::to_string(field_index) + " within the same record",
                        field_range,
                        index_it->second.has_value()
                            ? std::optional<diagnostics::RelatedLocation>(make_related_location(
                                  index_it->second, "previous field with this index is here"))
                            : std::nullopt);
                }
            }
            const auto [it, inserted] = field_names.emplace(std::string(field.name()), field_range);
            if (!inserted) {
                emit_diagnostic(
                    diagnostics_, diagnostic_id("BC6003"), diagnostics::Severity::Error,
                    "schema IR field '" + std::string(field.name()) +
                        "' is declared more than once in this record",
                    field_range,
                    it->second.has_value()
                        ? std::optional<diagnostics::RelatedLocation>(make_related_location(
                              it->second, "previous field declaration is here"))
                        : std::nullopt);
            }

            validate_name(field.name(), "field", field_range);
            validate_field_type(field.type(), field_range);
        }
    }

    void validate_enum(const ::breadcrumbs::schema_ir::EnumIR& enum_ir) {
        std::unordered_map<std::string, std::optional<support::SourceRange>> value_names;
        value_names.reserve(static_cast<std::size_t>(enum_ir.values_size()));

        for (const ::breadcrumbs::schema_ir::EnumValueIR& value : enum_ir.values()) {
            const std::optional<support::SourceRange> value_range =
                source_range_from_origin(value.source_origin(), source_manager_);
            const auto [it, inserted] = value_names.emplace(std::string(value.name()), value_range);
            if (!inserted) {
                emit_diagnostic(
                    diagnostics_, diagnostic_id("BC6003"), diagnostics::Severity::Error,
                    "schema IR enum value '" + std::string(value.name()) +
                        "' is declared more than once in this enum",
                    value_range,
                    it->second.has_value()
                        ? std::optional<diagnostics::RelatedLocation>(make_related_location(
                              it->second, "previous enum value declaration is here"))
                        : std::nullopt);
            }

            validate_name(value.name(), "enum value", value_range);
        }
    }

    void validate_field_type(const ::breadcrumbs::schema_ir::FieldType& field_type,
                             const std::optional<support::SourceRange>& field_range) {
        if (field_type.has_primitive()) {
            if (field_type.primitive() == ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_UNSPECIFIED) {
                emit_diagnostic(diagnostics_, diagnostic_id("BC6007"), diagnostics::Severity::Error,
                                "schema IR field type primitive is unspecified", field_range);
            }
            return;
        }

        if (field_type.has_record()) {
            validate_record_reference(field_type.record(), field_range);
            return;
        }

        if (field_type.has_enum_type()) {
            validate_enum_reference(field_type.enum_type(), field_range);
            return;
        }

        if (field_type.has_array()) {
            validate_array_type(field_type.array(), field_range);
            return;
        }

        if (field_type.has_bytes() || field_type.has_string()) {
            return;
        }

        emit_diagnostic(diagnostics_, diagnostic_id("BC6007"), diagnostics::Severity::Error,
                        "schema IR field type is missing", field_range);
    }

    void validate_array_type(const ::breadcrumbs::schema_ir::ArrayType& array_type,
                             const std::optional<support::SourceRange>& field_range) {
        validate_field_type(array_type.element_type(), field_range);
    }

    void validate_record_reference(const ::breadcrumbs::schema_ir::RecordRef& record_ref,
                                   const std::optional<support::SourceRange>& field_range) {
        const uint64_t target_id = record_ref.target_record_ir_id();
        if (target_id == 0) {
            emit_diagnostic(diagnostics_, diagnostic_id("BC6005"), diagnostics::Severity::Error,
                            "schema IR record reference is missing a target id", field_range);
            return;
        }

        const auto record_it = record_by_id_.find(target_id);
        if (record_it != record_by_id_.end()) {
            return;
        }

        const auto enum_it = enum_by_id_.find(target_id);
        if (enum_it != enum_by_id_.end()) {
            emit_diagnostic(diagnostics_, diagnostic_id("BC6006"), diagnostics::Severity::Error,
                            "schema IR record reference " + std::to_string(target_id) +
                                " resolves to an enum",
                            field_range,
                            enum_it->second.source_range.has_value()
                                ? std::optional<diagnostics::RelatedLocation>(make_related_location(
                                      enum_it->second.source_range, "referenced enum is here"))
                                : std::nullopt);
            return;
        }

        emit_diagnostic(diagnostics_, diagnostic_id("BC6005"), diagnostics::Severity::Error,
                        "schema IR record reference " + std::to_string(target_id) +
                            " does not resolve",
                        field_range);
    }

    void validate_enum_reference(const ::breadcrumbs::schema_ir::EnumRef& enum_ref,
                                 const std::optional<support::SourceRange>& field_range) {
        const uint64_t target_id = enum_ref.target_enum_ir_id();
        if (target_id == 0) {
            emit_diagnostic(diagnostics_, diagnostic_id("BC6005"), diagnostics::Severity::Error,
                            "schema IR enum reference is missing a target id", field_range);
            return;
        }

        const auto enum_it = enum_by_id_.find(target_id);
        if (enum_it != enum_by_id_.end()) {
            return;
        }

        const auto record_it = record_by_id_.find(target_id);
        if (record_it != record_by_id_.end()) {
            emit_diagnostic(diagnostics_, diagnostic_id("BC6006"), diagnostics::Severity::Error,
                            "schema IR enum reference " + std::to_string(target_id) +
                                " resolves to a record",
                            field_range,
                            record_it->second.source_range.has_value()
                                ? std::optional<diagnostics::RelatedLocation>(make_related_location(
                                      record_it->second.source_range, "referenced record is here"))
                                : std::nullopt);
            return;
        }

        emit_diagnostic(diagnostics_, diagnostic_id("BC6005"), diagnostics::Severity::Error,
                        "schema IR enum reference " + std::to_string(target_id) +
                            " does not resolve",
                        field_range);
    }

    const SchemaIrModel& schema_ir_;
    const support::SourceManager& source_manager_;
    diagnostics::DiagnosticCollection& diagnostics_;
    std::unordered_map<uint64_t, ObjectInfo> objects_by_id_;
    std::unordered_map<uint64_t, ObjectInfo> record_by_id_;
    std::unordered_map<uint64_t, ObjectInfo> enum_by_id_;
    std::unordered_map<uint32_t, std::optional<support::SourceRange>> record_ids_;
};

void SchemaIrValidator::validate(const SchemaIrModel& schema_ir, context::CompilerContext& context,
                                 diagnostics::DiagnosticCollection& diagnostics) const {
    SchemaIrValidatorImpl validator(schema_ir, context, diagnostics);
    validator.validate();
}

} // namespace breadcrumbs::compiler::schema_ir
