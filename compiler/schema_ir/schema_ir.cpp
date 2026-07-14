#include "compiler/schema_ir/schema_ir.hpp"

#include "compiler/source_schema/source_schema.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace breadcrumbs::compiler::schema_ir {
namespace {

constexpr std::string_view schema_ir_pass = "schema_ir";

struct SchemaIrBuildState {
    const support::SourceManager* source_manager = nullptr;
    diagnostics::DiagnosticCollection* diagnostics = nullptr;
    bool* failed = nullptr;
    std::uint64_t* next_ir_id = nullptr;
    std::unordered_map<std::string, std::uint64_t>* ir_ids_by_fqn = nullptr;
};

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] std::string qualify_fqn(std::string_view parent_fqn, std::string_view name) {
    if (parent_fqn.empty()) {
        return std::string(name);
    }
    std::string qualified = std::string(parent_fqn);
    qualified.push_back('.');
    qualified.append(name);
    return qualified;
}

[[nodiscard]] ::breadcrumbs::schema_ir::PrimitiveType
primitive_type_for_semantic(semantic::SemanticPrimitiveType primitive) {
    switch (primitive) {
    case semantic::SemanticPrimitiveType::Bool:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_BOOL;
    case semantic::SemanticPrimitiveType::I8:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_I8;
    case semantic::SemanticPrimitiveType::U8:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U8;
    case semantic::SemanticPrimitiveType::I16:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_I16;
    case semantic::SemanticPrimitiveType::U16:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U16;
    case semantic::SemanticPrimitiveType::I32:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_I32;
    case semantic::SemanticPrimitiveType::U32:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U32;
    case semantic::SemanticPrimitiveType::I64:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_I64;
    case semantic::SemanticPrimitiveType::U64:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_U64;
    case semantic::SemanticPrimitiveType::F32:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_F32;
    case semantic::SemanticPrimitiveType::F64:
        return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_F64;
    }

    return ::breadcrumbs::schema_ir::PRIMITIVE_TYPE_UNSPECIFIED;
}

[[nodiscard]] ::breadcrumbs::schema_ir::RecordType
record_type_for_semantic(semantic::SemanticRecordType record_type) {
    switch (record_type) {
    case semantic::SemanticRecordType::Data:
        return ::breadcrumbs::schema_ir::RECORD_TYPE_DATA;
    case semantic::SemanticRecordType::Command:
        return ::breadcrumbs::schema_ir::RECORD_TYPE_COMMAND;
    case semantic::SemanticRecordType::Event:
        return ::breadcrumbs::schema_ir::RECORD_TYPE_EVENT;
    case semantic::SemanticRecordType::Configuration:
        return ::breadcrumbs::schema_ir::RECORD_TYPE_CONFIGURATION;
    case semantic::SemanticRecordType::Diagnostics:
        return ::breadcrumbs::schema_ir::RECORD_TYPE_DIAGNOSTICS;
    }

    return ::breadcrumbs::schema_ir::RECORD_TYPE_UNSPECIFIED;
}

void emit_internal_error(SchemaIrBuildState state, std::string message,
                         support::SourceRange range = support::SourceRange::invalid()) {
    auto builder = diagnostics::Diagnostic::create(
        diagnostic_id("BC1004"), diagnostics::Severity::InternalCompilerError, std::move(message));
    builder.from_pass(std::string(schema_ir_pass));
    if (range.is_valid()) {
        builder.at(range);
    }
    state.diagnostics->emit(builder.build());
    if (state.failed != nullptr) {
        *state.failed = true;
    }
}

void populate_source_origin(::breadcrumbs::schema_ir::SourceOrigin* origin,
                            const support::SourceManager& source_manager,
                            support::SourceRange range) {
    if (origin == nullptr || !range.is_valid()) {
        return;
    }

    const std::optional<std::string_view> source_path =
        source_manager.source_path(range.begin().file_id());
    if (source_path.has_value()) {
        origin->set_source_unit(std::string(*source_path));
        origin->set_file(std::string(*source_path));
    }

    ::breadcrumbs::schema_ir::SourceSpan* span = origin->mutable_span();
    span->set_start_offset(static_cast<std::uint32_t>(range.begin().byte_offset()));
    span->set_end_offset(static_cast<std::uint32_t>(range.end().byte_offset()));

    const std::optional<support::LineColumn> begin = source_manager.line_column(range.begin());
    const std::optional<support::LineColumn> end = source_manager.line_column(range.end());
    if (begin.has_value()) {
        span->set_start_line(static_cast<std::uint32_t>(begin->line));
        span->set_start_column(static_cast<std::uint32_t>(begin->column));
    }
    if (end.has_value()) {
        span->set_end_line(static_cast<std::uint32_t>(end->line));
        span->set_end_column(static_cast<std::uint32_t>(end->column));
    }
}

[[nodiscard]] ::breadcrumbs::schema_ir::NamespaceIR*
find_namespace_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.namespaces_size(); ++index) {
        ::breadcrumbs::schema_ir::NamespaceIR* child = parent.mutable_namespaces(index);
        if (child != nullptr && child->name() == name) {
            return child;
        }
    }
    return nullptr;
}

[[nodiscard]] ::breadcrumbs::schema_ir::RecordIR*
find_record_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.records_size(); ++index) {
        ::breadcrumbs::schema_ir::RecordIR* record = parent.mutable_records(index);
        if (record != nullptr && record->name() == name) {
            return record;
        }
    }
    return nullptr;
}

[[nodiscard]] ::breadcrumbs::schema_ir::EnumIR*
find_enum_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string_view name) {
    for (int index = 0; index < parent.enums_size(); ++index) {
        ::breadcrumbs::schema_ir::EnumIR* enum_ir = parent.mutable_enums(index);
        if (enum_ir != nullptr && enum_ir->name() == name) {
            return enum_ir;
        }
    }
    return nullptr;
}

[[nodiscard]] ::breadcrumbs::schema_ir::NamespaceIR&
ensure_namespace_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string name,
                       const support::SourceManager& source_manager, support::SourceRange range) {
    if (::breadcrumbs::schema_ir::NamespaceIR* existing = find_namespace_child(parent, name);
        existing != nullptr) {
        return *existing;
    }

    ::breadcrumbs::schema_ir::NamespaceIR* child = parent.add_namespaces();
    child->set_ir_id(0);
    child->set_name(std::move(name));
    child->set_fqn(qualify_fqn(parent.fqn(), child->name()));
    populate_source_origin(child->mutable_source_origin(), source_manager, range);
    return *child;
}

[[nodiscard]] ::breadcrumbs::schema_ir::RecordIR&
ensure_record_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string name,
                    const support::SourceManager& source_manager, support::SourceRange range) {
    if (::breadcrumbs::schema_ir::RecordIR* existing = find_record_child(parent, name);
        existing != nullptr) {
        return *existing;
    }

    ::breadcrumbs::schema_ir::RecordIR* record = parent.add_records();
    record->set_ir_id(0);
    record->set_name(std::move(name));
    record->set_fqn(qualify_fqn(parent.fqn(), record->name()));
    populate_source_origin(record->mutable_source_origin(), source_manager, range);
    return *record;
}

[[nodiscard]] ::breadcrumbs::schema_ir::EnumIR&
ensure_enum_child(::breadcrumbs::schema_ir::NamespaceIR& parent, std::string name,
                  const support::SourceManager& source_manager, support::SourceRange range) {
    if (::breadcrumbs::schema_ir::EnumIR* existing = find_enum_child(parent, name);
        existing != nullptr) {
        return *existing;
    }

    ::breadcrumbs::schema_ir::EnumIR* enum_ir = parent.add_enums();
    enum_ir->set_ir_id(0);
    enum_ir->set_name(std::move(name));
    enum_ir->set_fqn(qualify_fqn(parent.fqn(), enum_ir->name()));
    populate_source_origin(enum_ir->mutable_source_origin(), source_manager, range);
    return *enum_ir;
}

[[nodiscard]] std::optional<std::uint64_t> find_ir_id(const SchemaIrBuildState& state,
                                                      std::string_view fqn) {
    if (state.ir_ids_by_fqn == nullptr) {
        return std::nullopt;
    }

    const auto found = state.ir_ids_by_fqn->find(std::string(fqn));
    if (found == state.ir_ids_by_fqn->end()) {
        return std::nullopt;
    }
    return found->second;
}

[[nodiscard]] ::breadcrumbs::schema_ir::FieldType lower_semantic_type_with_state(
    const semantic::SemanticType& semantic_type, std::string_view record_fqn,
    std::string_view field_name, support::SourceRange field_range, SchemaIrBuildState state) {
    ::breadcrumbs::schema_ir::FieldType field_type;
    if (!semantic_type.is_valid()) {
        emit_internal_error(state,
                            "schema IR lowering encountered an invalid semantic field type for '" +
                                std::string(record_fqn) + "." + std::string(field_name) + "'",
                            field_range);
        return field_type;
    }

    return std::visit(
        [&](const auto& typed) -> ::breadcrumbs::schema_ir::FieldType {
            using Type = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Type, semantic::SemanticPrimitiveType>) {
                field_type.set_primitive(primitive_type_for_semantic(typed));
                return field_type;
            } else if constexpr (std::is_same_v<Type, semantic::SemanticStringType>) {
                field_type.mutable_string()->set_max_bytes(typed.max_bytes);
                return field_type;
            } else if constexpr (std::is_same_v<Type, semantic::SemanticBytesType>) {
                field_type.mutable_bytes()->set_max_bytes(typed.max_bytes);
                return field_type;
            } else if constexpr (std::is_same_v<Type, semantic::SemanticRecordReferenceType>) {
                const std::optional<std::uint64_t> target_ir_id = find_ir_id(state, typed.canonical_target_fqn);
                if (!target_ir_id.has_value()) {
                    emit_internal_error(state,
                                        "schema IR lowering could not locate record reference '" +
                                            typed.canonical_target_fqn + "'",
                                        field_range);
                    return {};
                }
                field_type.mutable_record()->set_target_record_ir_id(*target_ir_id);
                return field_type;
            } else if constexpr (std::is_same_v<Type, semantic::SemanticEnumReferenceType>) {
                const std::optional<std::uint64_t> target_ir_id = find_ir_id(state, typed.canonical_target_fqn);
                if (!target_ir_id.has_value()) {
                    emit_internal_error(state,
                                        "schema IR lowering could not locate enum reference '" +
                                            typed.canonical_target_fqn + "'",
                                        field_range);
                    return {};
                }
                field_type.mutable_enum_type()->set_target_enum_ir_id(*target_ir_id);
                return field_type;
            } else if constexpr (std::is_same_v<Type, semantic::SemanticArrayType>) {
                if (!typed.element_type) {
                    emit_internal_error(
                        state,
                        "schema IR lowering encountered an invalid semantic field type for '" +
                            std::string(record_fqn) + "." + std::string(field_name) + "'",
                        field_range);
                    return {};
                }

                ::breadcrumbs::schema_ir::FieldType element_type = lower_semantic_type_with_state(
                    *typed.element_type, record_fqn, field_name, field_range, state);
                if (state.failed != nullptr && *state.failed) {
                    return {};
                }

                ::breadcrumbs::schema_ir::ArrayType* array_type = field_type.mutable_array();
                array_type->set_max_elements(typed.max_elements);
                array_type->mutable_element_type()->CopyFrom(std::move(element_type));
                return field_type;
            } else {
                emit_internal_error(state,
                                    "schema IR lowering encountered an invalid semantic field "
                                    "type for '" + std::string(record_fqn) + "." +
                                        std::string(field_name) + "'",
                                    field_range);
                return {};
            }
        },
        semantic_type.value);
}

class NormalizedSourceSchemaSchemaIrBuilderImpl {
public:
    NormalizedSourceSchemaSchemaIrBuilderImpl(
        const source_schema::NormalizedSourceSchemaDocument& schema,
        const semantic::SemanticModel& semantic_model, const layout::LayoutModel& layout_model,
        context::CompilerContext& context, diagnostics::DiagnosticCollection& diagnostics)
        : schema_(schema), semantic_model_(semantic_model), layout_model_(layout_model),
          source_manager_(context.source_manager()), diagnostics_(diagnostics) {
        state_.source_manager = &source_manager_;
        state_.diagnostics = &diagnostics_;
        state_.failed = &layout_failed_;
        state_.next_ir_id = &next_ir_id_;
        state_.ir_ids_by_fqn = &ir_ids_by_fqn_;
    }

    [[nodiscard]] SchemaIrModel build() {
        SchemaIrModel schema_ir;
        schema_ir.set_schema_ir_version(1);

        ::breadcrumbs::schema_ir::NamespaceIR* root = schema_ir.mutable_root_namespace();
        root->set_ir_id(next_ir_id_++);
        root->set_name("");
        root->set_fqn("");
        populate_source_origin(root->mutable_source_origin(), source_manager_, schema_.source_range);

        ::breadcrumbs::schema_ir::NamespaceIR* current_namespace = root;
        std::string current_namespace_fqn;
        if (schema_.namespace_name.parts.empty()) {
            emit_internal_error(state_, "schema IR lowering encountered an empty namespace name",
                                schema_.namespace_name.source_range);
            return {};
        }

        for (const source_schema::SourceSchemaIdentifier& part : schema_.namespace_name.parts) {
            ::breadcrumbs::schema_ir::NamespaceIR& child = ensure_namespace_child(
                *current_namespace, part.text, source_manager_, part.source_range);
            if (child.ir_id() == 0U) {
                child.set_ir_id(next_ir_id_++);
            }
            current_namespace = &child;
            current_namespace_fqn = qualify_fqn(current_namespace_fqn, part.text);
        }

        for (const source_schema::NormalizedSourceSchemaEnum& enumeration : schema_.enums) {
            ::breadcrumbs::schema_ir::EnumIR& enum_ir = ensure_enum_child(
                *current_namespace, enumeration.name.text, source_manager_,
                enumeration.source_range);
            if (enum_ir.ir_id() == 0U) {
                enum_ir.set_ir_id(next_ir_id_++);
            }
            ir_ids_by_fqn_[qualify_fqn(current_namespace_fqn, enumeration.name.text)] =
                enum_ir.ir_id();
        }

        ::breadcrumbs::schema_ir::RecordIR& record = ensure_record_child(
            *current_namespace, schema_.record_name.text, source_manager_,
            schema_.record_source_range);
        if (record.ir_id() == 0U) {
            record.set_ir_id(next_ir_id_++);
        }
        const std::string record_fqn = qualify_fqn(current_namespace_fqn, schema_.record_name.text);
        ir_ids_by_fqn_[record_fqn] = record.ir_id();

        populate_enums(*current_namespace);
        populate_record(*current_namespace, record_fqn, record);

        if (layout_failed_) {
            return {};
        }
        return schema_ir;
    }

private:
    [[nodiscard]] const semantic::SemanticRecord*
    find_semantic_record(std::string_view record_fqn) const {
        return semantic_model_.find_record(record_fqn);
    }

    [[nodiscard]] const layout::RecordLayout*
    find_layout_record(std::string_view record_fqn) const {
        return layout_model_.find_record(record_fqn);
    }

    void populate_enums(::breadcrumbs::schema_ir::NamespaceIR& namespace_ir) {
        for (const source_schema::NormalizedSourceSchemaEnum& enumeration : schema_.enums) {
            ::breadcrumbs::schema_ir::EnumIR* enum_ir = find_enum_child(namespace_ir, enumeration.name.text);
            if (enum_ir == nullptr) {
                emit_internal_error(state_, "schema IR lowering could not locate enum '" +
                                                enumeration.name.text + "'",
                                    enumeration.source_range);
                return;
            }

            for (const source_schema::NormalizedSourceSchemaEnumValue& value : enumeration.values) {
                ::breadcrumbs::schema_ir::EnumValueIR* value_ir = enum_ir->add_values();
                value_ir->set_name(value.name.text);
                value_ir->set_value(value.value);
                populate_source_origin(value_ir->mutable_source_origin(), source_manager_,
                                       value.source_range);
            }
        }
    }

    void populate_record(::breadcrumbs::schema_ir::NamespaceIR& namespace_ir,
                         std::string_view record_fqn,
                         ::breadcrumbs::schema_ir::RecordIR& record) {
        const semantic::SemanticRecord* semantic_record = find_semantic_record(record_fqn);
        const layout::RecordLayout* layout_record = find_layout_record(record_fqn);
        if (semantic_record == nullptr) {
            emit_internal_error(state_,
                                "schema IR lowering could not locate semantic record '" +
                                    std::string(record_fqn) + "'",
                                schema_.record_source_range);
            return;
        }
        if (layout_record == nullptr) {
            emit_internal_error(state_,
                                "schema IR lowering could not locate layout record '" +
                                    std::string(record_fqn) + "'",
                                schema_.record_source_range);
            return;
        }
        populate_source_origin(record.mutable_source_origin(), source_manager_,
                               schema_.record_source_range);
        if (semantic_record->fields.size() != schema_.fields.size() ||
            layout_record->fields.size() != schema_.fields.size()) {
            emit_internal_error(
                state_,
                "schema IR lowering observed inconsistent field counts for record '" +
                    std::string(record_fqn) + "'",
                schema_.record_source_range);
            return;
        }
        if (layout_record->record_id == 0U) {
            emit_internal_error(state_,
                                "schema IR lowering observed a zero record_id for record '" +
                                    std::string(record_fqn) + "'",
                                schema_.record_source_range);
            return;
        }

        record.set_record_id(layout_record->record_id);
        if (semantic_record->version.has_value()) {
            record.set_schema_version(*semantic_record->version);
        }
        if (semantic_record->record_type.has_value()) {
            record.set_record_type(record_type_for_semantic(*semantic_record->record_type));
        }

        for (std::size_t field_index = 0; field_index < schema_.fields.size(); ++field_index) {
            const source_schema::NormalizedSourceSchemaField& field = schema_.fields[field_index];
            const semantic::SemanticField& semantic_field = semantic_record->fields[field_index];
            const layout::FieldLayout& layout_field = layout_record->fields[field_index];
            if (semantic_field.name != field.name.text) {
                emit_internal_error(state_,
                                    "schema IR lowering observed inconsistent semantic field "
                                    "ordering for record '" +
                                        std::string(record_fqn) + "'",
                    field.source_range);
                return;
            }

            if (field_index > std::numeric_limits<std::uint32_t>::max()) {
                emit_internal_error(
                    state_,
                    "schema IR lowering observed an unrepresentable field index for record '" +
                        std::string(record_fqn) + "' field '" + field.name.text + "'",
                    field.source_range);
                return;
            }
            const std::uint32_t expected_field_index =
                static_cast<std::uint32_t>(field_index);
            if (layout_field.field_index != expected_field_index) {
                emit_internal_error(
                    state_,
                    "schema IR lowering observed inconsistent field index for record '" +
                        std::string(record_fqn) + "' field '" + field.name.text +
                        "': expected " + std::to_string(expected_field_index) + ", actual " +
                        std::to_string(layout_field.field_index),
                    field.source_range);
                return;
            }

            ::breadcrumbs::schema_ir::FieldIR* field_ir = record.add_fields();
            field_ir->set_name(field.name.text);
            field_ir->set_field_index(layout_field.field_index);
            field_ir->mutable_type()->CopyFrom(lower_semantic_type_with_state(
                semantic_field.type, record_fqn, field.name.text, field.source_range, state_));
            if (layout_failed_) {
                return;
            }
            populate_source_origin(field_ir->mutable_source_origin(), source_manager_,
                                   field.source_range);
        }
        (void)namespace_ir;
    }

    const source_schema::NormalizedSourceSchemaDocument& schema_;
    const semantic::SemanticModel& semantic_model_;
    const layout::LayoutModel& layout_model_;
    const support::SourceManager& source_manager_;
    diagnostics::DiagnosticCollection& diagnostics_;
    SchemaIrBuildState state_;
    std::uint64_t next_ir_id_ = 1;
    std::unordered_map<std::string, std::uint64_t> ir_ids_by_fqn_;
    bool layout_failed_ = false;
};

} // namespace

SchemaIrModel SchemaIrBuilder::build(const source_schema::NormalizedSourceSchemaDocument& schema,
                                    const semantic::SemanticModel& semantic_model,
                                    const layout::LayoutModel& layout_model,
                                    context::CompilerContext& context,
                                    diagnostics::DiagnosticCollection& diagnostics) const {
    NormalizedSourceSchemaSchemaIrBuilderImpl builder(schema, semantic_model, layout_model,
                                                      context, diagnostics);
    return builder.build();
}

} // namespace breadcrumbs::compiler::schema_ir
