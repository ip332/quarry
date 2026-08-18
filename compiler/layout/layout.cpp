#include "compiler/layout/layout.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quarry::compiler::layout {
namespace {

constexpr std::string_view layout_pass = "layout";
constexpr std::string_view brf_v2_layout_pass = "brf-v2-layout";

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

void emit_too_many_fields(const semantic::SemanticRecord& record,
                          diagnostics::DiagnosticCollection& diagnostics,
                          context::CompilerContext& context) {
    diagnostics.emit(
        diagnostics::Diagnostic::create(diagnostic_id("BC7001"), diagnostics::Severity::Error,
                                        "record '" + record.fqn + "' has more than 256 fields")
            .at(record.source_range)
            .from_pass(std::string(layout_pass))
            .build());
    (void)context;
}

void emit_duplicate_record_fqn(const semantic::SemanticRecord& record,
                               diagnostics::DiagnosticCollection& diagnostics,
                               context::CompilerContext& context) {
    diagnostics.emit(diagnostics::Diagnostic::create(
                         diagnostic_id("BC7002"), diagnostics::Severity::Error,
                         "record '" + record.fqn + "' is declared more than once in layout input")
                         .at(record.source_range)
                         .from_pass(std::string(layout_pass))
                         .build());
    (void)context;
}

void emit_brf_v2_error(diagnostics::DiagnosticCollection& diagnostics, std::string message) {
    diagnostics.emit(diagnostics::Diagnostic::create(
                         diagnostic_id("BC7003"), diagnostics::Severity::Error, std::move(message))
                         .from_pass(std::string(brf_v2_layout_pass))
                         .build());
}

[[nodiscard]] bool checked_add(std::uint32_t lhs, std::uint32_t rhs, std::uint32_t& result) {
    if (rhs > std::numeric_limits<std::uint32_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool checked_multiply(std::uint32_t lhs, std::uint32_t rhs,
                                    std::uint32_t& result) {
    if (lhs != 0U && rhs > std::numeric_limits<std::uint32_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

[[nodiscard]] std::optional<std::uint32_t>
primitive_width(::quarry::schema_ir::PrimitiveType primitive) {
    using ::quarry::schema_ir::PrimitiveType;
    switch (primitive) {
    case PrimitiveType::PRIMITIVE_TYPE_BOOL:
    case PrimitiveType::PRIMITIVE_TYPE_I8:
    case PrimitiveType::PRIMITIVE_TYPE_U8:
        return 1U;
    case PrimitiveType::PRIMITIVE_TYPE_I16:
    case PrimitiveType::PRIMITIVE_TYPE_U16:
        return 2U;
    case PrimitiveType::PRIMITIVE_TYPE_I32:
    case PrimitiveType::PRIMITIVE_TYPE_U32:
    case PrimitiveType::PRIMITIVE_TYPE_F32:
        return 4U;
    case PrimitiveType::PRIMITIVE_TYPE_I64:
    case PrimitiveType::PRIMITIVE_TYPE_U64:
    case PrimitiveType::PRIMITIVE_TYPE_F64:
        return 8U;
    case PrimitiveType::PRIMITIVE_TYPE_UNSPECIFIED:
        return std::nullopt;
    case PrimitiveType::PrimitiveType_INT_MIN_SENTINEL_DO_NOT_USE_:
    case PrimitiveType::PrimitiveType_INT_MAX_SENTINEL_DO_NOT_USE_:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] LayoutTypeKind primitive_kind(::quarry::schema_ir::PrimitiveType primitive) {
    using ::quarry::schema_ir::PrimitiveType;
    switch (primitive) {
    case PrimitiveType::PRIMITIVE_TYPE_BOOL:
        return LayoutTypeKind::Bool;
    case PrimitiveType::PRIMITIVE_TYPE_I8:
        return LayoutTypeKind::I8;
    case PrimitiveType::PRIMITIVE_TYPE_U8:
        return LayoutTypeKind::U8;
    case PrimitiveType::PRIMITIVE_TYPE_I16:
        return LayoutTypeKind::I16;
    case PrimitiveType::PRIMITIVE_TYPE_U16:
        return LayoutTypeKind::U16;
    case PrimitiveType::PRIMITIVE_TYPE_I32:
        return LayoutTypeKind::I32;
    case PrimitiveType::PRIMITIVE_TYPE_U32:
        return LayoutTypeKind::U32;
    case PrimitiveType::PRIMITIVE_TYPE_I64:
        return LayoutTypeKind::I64;
    case PrimitiveType::PRIMITIVE_TYPE_U64:
        return LayoutTypeKind::U64;
    case PrimitiveType::PRIMITIVE_TYPE_F32:
        return LayoutTypeKind::F32;
    case PrimitiveType::PRIMITIVE_TYPE_F64:
        return LayoutTypeKind::F64;
    case PrimitiveType::PRIMITIVE_TYPE_UNSPECIFIED:
        return LayoutTypeKind::Bool;
    case PrimitiveType::PrimitiveType_INT_MIN_SENTINEL_DO_NOT_USE_:
    case PrimitiveType::PrimitiveType_INT_MAX_SENTINEL_DO_NOT_USE_:
        return LayoutTypeKind::Bool;
    }
    return LayoutTypeKind::Bool;
}

[[nodiscard]] std::uint32_t enum_width(std::uint64_t maximum) {
    if (maximum <= std::numeric_limits<std::uint8_t>::max()) {
        return 1U;
    }
    if (maximum <= std::numeric_limits<std::uint16_t>::max()) {
        return 2U;
    }
    if (maximum <= std::numeric_limits<std::uint32_t>::max()) {
        return 4U;
    }
    return 8U;
}

class BrfV2LayoutBuilder {
public:
    BrfV2LayoutBuilder(const ::quarry::schema_ir::SchemaIR& schema_ir,
                       diagnostics::DiagnosticCollection& diagnostics)
        : schema_ir_(schema_ir), diagnostics_(diagnostics) {}

    [[nodiscard]] LayoutModel build() {
        collect_namespace(schema_ir_.root_namespace());
        if (failed_) {
            return {};
        }

        std::vector<const ::quarry::schema_ir::RecordIR*> ordered_records;
        ordered_records.reserve(records_by_id_.size());
        for (const auto& [unused_id, record] : records_by_id_) {
            (void)unused_id;
            ordered_records.push_back(record);
        }
        std::sort(ordered_records.begin(), ordered_records.end(),
                  [](const auto* lhs, const auto* rhs) { return lhs->fqn() < rhs->fqn(); });

        LayoutModel result;
        result.records.reserve(ordered_records.size());
        for (const ::quarry::schema_ir::RecordIR* record : ordered_records) {
            if (record == nullptr || !build_record(record->ir_id())) {
                return {};
            }
            result.records.push_back(record_layouts_.at(record->ir_id()));
        }
        return result;
    }

private:
    enum class VisitState { Visiting, Complete };

    void fail(std::string message) {
        if (!failed_) {
            emit_brf_v2_error(diagnostics_, std::move(message));
        }
        failed_ = true;
    }

    void collect_namespace(const ::quarry::schema_ir::NamespaceIR& root_namespace) {
        std::vector<const ::quarry::schema_ir::NamespaceIR*> worklist{&root_namespace};
        while (!worklist.empty() && !failed_) {
            const ::quarry::schema_ir::NamespaceIR* namespace_ir = worklist.back();
            worklist.pop_back();

            for (const ::quarry::schema_ir::RecordIR& record : namespace_ir->records()) {
                if (record.ir_id() == 0U || record.fqn().empty()) {
                    fail("BRF v2 layout encountered a record without a valid identity");
                    return;
                }
                if (!records_by_id_.emplace(record.ir_id(), &record).second ||
                    !record_ids_by_fqn_.emplace(record.fqn(), record.ir_id()).second) {
                    fail("BRF v2 layout encountered a duplicate record identity");
                    return;
                }
            }
            for (const ::quarry::schema_ir::EnumIR& enumeration : namespace_ir->enums()) {
                if (enumeration.ir_id() == 0U || enumeration.fqn().empty()) {
                    fail("BRF v2 layout encountered an enum without a valid identity");
                    return;
                }
                if (!enums_by_id_.emplace(enumeration.ir_id(), &enumeration).second) {
                    fail("BRF v2 layout encountered a duplicate enum identity");
                    return;
                }
            }

            // Reverse-push so pop_back visits children in declaration order,
            // matching the former recursive depth-first traversal.
            for (auto child = namespace_ir->namespaces().rbegin();
                 child != namespace_ir->namespaces().rend(); ++child) {
                worklist.push_back(&*child);
            }
        }
    }

    [[nodiscard]] bool build_record(std::uint64_t record_id) {
        struct Frame {
            std::uint64_t record_id;
            int next_field = 0;
        };

        const auto initial_record = records_by_id_.find(record_id);
        if (initial_record == records_by_id_.end()) {
            fail("BRF v2 layout could not resolve record IR id " + std::to_string(record_id));
            return false;
        }
        const auto initial_state = states_.find(record_id);
        if (initial_state != states_.end()) {
            if (initial_state->second == VisitState::Visiting) {
                fail("BRF v2 layout encountered a recursive record reference at '" +
                     initial_record->second->fqn() + "'");
                return false;
            }
            return true;
        }

        std::vector<Frame> workstack;
        workstack.push_back({record_id});
        states_[record_id] = VisitState::Visiting;

        while (!workstack.empty() && !failed_) {
            Frame& frame = workstack.back();
            const auto record_it = records_by_id_.find(frame.record_id);
            if (record_it == records_by_id_.end()) {
                fail("BRF v2 layout could not resolve record IR id " +
                     std::to_string(frame.record_id));
                break;
            }
            const ::quarry::schema_ir::RecordIR& record = *record_it->second;

            bool dependency_pushed = false;
            while (frame.next_field < record.fields_size()) {
                const ::quarry::schema_ir::FieldType& field_type =
                    record.fields(frame.next_field++).type();
                const std::optional<std::uint64_t> dependency = record_dependency(field_type);
                if (!dependency.has_value()) {
                    continue;
                }

                const auto dependency_state = states_.find(*dependency);
                if (dependency_state != states_.end()) {
                    if (dependency_state->second == VisitState::Visiting) {
                        const auto dependency_record = records_by_id_.find(*dependency);
                        fail("BRF v2 layout encountered a recursive record reference at '" +
                             (dependency_record == records_by_id_.end()
                                  ? std::to_string(*dependency)
                                  : dependency_record->second->fqn()) +
                             "'");
                        break;
                    }
                    continue;
                }

                if (records_by_id_.find(*dependency) == records_by_id_.end()) {
                    fail("BRF v2 layout could not resolve record IR id " +
                         std::to_string(*dependency));
                    break;
                }
                states_[*dependency] = VisitState::Visiting;
                workstack.push_back({*dependency});
                dependency_pushed = true;
                break;
            }
            if (failed_) {
                break;
            }
            if (dependency_pushed) {
                continue;
            }

            if (!construct_record_layout(record)) {
                break;
            }
            states_[frame.record_id] = VisitState::Complete;
            workstack.pop_back();
        }

        return !failed_;
    }

    [[nodiscard]] std::optional<std::uint64_t>
    record_dependency(const ::quarry::schema_ir::FieldType& field_type) const {
        if (field_type.kind_case() == ::quarry::schema_ir::FieldType::kRecord) {
            return field_type.record().target_record_ir_id();
        }
        if (field_type.kind_case() == ::quarry::schema_ir::FieldType::kArray &&
            field_type.array().element_type().kind_case() ==
                ::quarry::schema_ir::FieldType::kRecord) {
            return field_type.array().element_type().record().target_record_ir_id();
        }
        return std::nullopt;
    }

    [[nodiscard]] bool construct_record_layout(
        const ::quarry::schema_ir::RecordIR& record) {
        if (record.fields_size() > 256) {
            fail("record '" + record.fqn() + "' has more than 256 fields in BRF v2 layout");
            return false;
        }

        RecordLayout result;
        result.fqn = record.fqn();
        result.record_id = record.record_id();
        result.header_size = kBrfV2HeaderSize;
        if (result.record_id == 0U) {
            fail("record '" + record.fqn() + "' has a zero record_id");
            return false;
        }

        const std::uint32_t field_count = static_cast<std::uint32_t>(record.fields_size());
        result.presence_bitmap_size = (field_count + 7U) / 8U;
        std::uint32_t cursor = result.header_size;
        if (!checked_add(cursor, result.presence_bitmap_size, cursor)) {
            fail("BRF v2 presence bitmap size overflows for record '" + record.fqn() + "'");
            return false;
        }

        std::unordered_set<std::uint32_t> field_indexes;
        result.fields.reserve(record.fields_size());
        for (int field_position = 0; field_position < record.fields_size(); ++field_position) {
            const ::quarry::schema_ir::FieldIR& field = record.fields(field_position);
            if (field.field_index() > std::numeric_limits<std::uint8_t>::max() ||
                !field_indexes.insert(field.field_index()).second) {
                fail("record '" + record.fqn() + "' has an invalid or duplicate field_index");
                return false;
            }

            FieldLayout field_layout;
            field_layout.field_index = field.field_index();
            field_layout.presence_bit_index = static_cast<std::uint32_t>(field_position);
            field_layout.name = field.name();
            if (!build_type(field.type(), field_layout.type)) {
                return false;
            }
            field_layout.location.byte_offset = cursor;
            field_layout.location.bit_offset = 0U;

            if (field_layout.type.classification == RecordClassification::VariableSize) {
                field_layout.storage = FieldStorage::VariableDescriptor;
                field_layout.descriptor_kind = DescriptorKind::DataOffsetByteLength;
                field_layout.slot_size = kBrfV2VariableDescriptorSize;
            } else if (field_layout.type.kind == LayoutTypeKind::Record) {
                field_layout.storage = FieldStorage::InlineFixedNestedRecord;
                field_layout.slot_size = field_layout.type.encoded_width;
            } else {
                field_layout.storage = FieldStorage::Fixed;
                field_layout.slot_size = field_layout.type.encoded_width;
            }

            if (field_layout.slot_size == 0U ||
                !checked_multiply(field_layout.slot_size, 8U,
                                 field_layout.location.bit_width) ||
                !checked_add(cursor, field_layout.slot_size, cursor)) {
                fail("BRF v2 fixed-region size overflows for field '" + field.name() + "' in '" +
                     record.fqn() + "'");
                return false;
            }
            result.fields.push_back(std::move(field_layout));
        }

        result.fixed_region_size = cursor - result.header_size;
        result.classification = RecordClassification::FixedSize;
        for (const FieldLayout& field : result.fields) {
            if (field.storage == FieldStorage::VariableDescriptor) {
                result.classification = RecordClassification::VariableSize;
                break;
            }
        }
        if (result.classification == RecordClassification::FixedSize) {
            result.complete_fixed_record_size = cursor;
        }

        record_layouts_[record.ir_id()] = std::move(result);
        return true;
    }

    [[nodiscard]] bool build_type(const ::quarry::schema_ir::FieldType& field_type,
                                  TypeLayout& result) {
        switch (field_type.kind_case()) {
        case ::quarry::schema_ir::FieldType::kPrimitive: {
            const std::optional<std::uint32_t> width = primitive_width(field_type.primitive());
            if (!width.has_value()) {
                fail("BRF v2 layout encountered an unspecified primitive type");
                return false;
            }
            result.kind = primitive_kind(field_type.primitive());
            result.encoded_width = *width;
            return true;
        }
        case ::quarry::schema_ir::FieldType::kEnumType: {
            const auto enum_it = enums_by_id_.find(field_type.enum_type().target_enum_ir_id());
            if (enum_it == enums_by_id_.end()) {
                fail("BRF v2 layout could not resolve enum IR id " +
                     std::to_string(field_type.enum_type().target_enum_ir_id()));
                return false;
            }
            std::uint64_t maximum = 0U;
            for (const ::quarry::schema_ir::EnumValueIR& value : enum_it->second->values()) {
                if (value.value() < 0) {
                    fail("BRF v2 layout does not support negative enum value '" + value.name() +
                         "'");
                    return false;
                }
                maximum = std::max(maximum, static_cast<std::uint64_t>(value.value()));
            }
            result.kind = LayoutTypeKind::Enum;
            result.encoded_width = enum_width(maximum);
            result.referenced_ir_id = enum_it->second->ir_id();
            result.referenced_fqn = enum_it->second->fqn();
            return true;
        }
        case ::quarry::schema_ir::FieldType::kString:
            if (field_type.string().max_bytes() == 0U) {
                fail("BRF v2 layout encountered a string without max_bytes");
                return false;
            }
            result.kind = LayoutTypeKind::String;
            result.classification = RecordClassification::VariableSize;
            result.max_bytes = field_type.string().max_bytes();
            return true;
        case ::quarry::schema_ir::FieldType::kBytes:
            if (field_type.bytes().max_bytes() == 0U) {
                fail("BRF v2 layout encountered bytes without max_bytes");
                return false;
            }
            result.kind = LayoutTypeKind::Bytes;
            result.classification = RecordClassification::VariableSize;
            result.max_bytes = field_type.bytes().max_bytes();
            return true;
        case ::quarry::schema_ir::FieldType::kRecord: {
            const std::uint64_t target_id = field_type.record().target_record_ir_id();
            const auto target_it = record_layouts_.find(target_id);
            if (target_it == record_layouts_.end()) {
                fail("BRF v2 layout could not resolve completed record IR id " +
                     std::to_string(target_id));
                return false;
            }
            const RecordLayout& target = target_it->second;
            result.kind = LayoutTypeKind::Record;
            result.classification = target.classification;
            result.referenced_ir_id = target_id;
            result.referenced_fqn = target.fqn;
            if (target.complete_fixed_record_size.has_value()) {
                result.encoded_width = *target.complete_fixed_record_size;
            }
            return true;
        }
        case ::quarry::schema_ir::FieldType::kArray: {
            if (field_type.array().max_elements() == 0U ||
                field_type.array().element_type().kind_case() ==
                    ::quarry::schema_ir::FieldType::kArray) {
                fail("BRF v2 layout encountered an unsupported array shape");
                return false;
            }
            result.kind = LayoutTypeKind::Array;
            result.classification = RecordClassification::VariableSize;
            result.max_elements = field_type.array().max_elements();
            result.element_type = std::make_unique<TypeLayout>();
            return build_type(field_type.array().element_type(), *result.element_type);
        }
        case ::quarry::schema_ir::FieldType::KIND_NOT_SET:
            fail("BRF v2 layout encountered a field without a type");
            return false;
        }
        fail("BRF v2 layout encountered an unknown field type");
        return false;
    }

    const ::quarry::schema_ir::SchemaIR& schema_ir_;
    diagnostics::DiagnosticCollection& diagnostics_;
    std::unordered_map<std::uint64_t, const ::quarry::schema_ir::RecordIR*> records_by_id_;
    std::unordered_map<std::string, std::uint64_t> record_ids_by_fqn_;
    std::unordered_map<std::uint64_t, const ::quarry::schema_ir::EnumIR*> enums_by_id_;
    std::unordered_map<std::uint64_t, VisitState> states_;
    std::unordered_map<std::uint64_t, RecordLayout> record_layouts_;
    bool failed_ = false;
};

} // namespace

TypeLayout::TypeLayout(const TypeLayout& other)
    : kind(other.kind), classification(other.classification), encoded_width(other.encoded_width),
      max_bytes(other.max_bytes), max_elements(other.max_elements),
      referenced_ir_id(other.referenced_ir_id), referenced_fqn(other.referenced_fqn),
      element_type(other.element_type == nullptr ? nullptr
                                                  : std::make_unique<TypeLayout>(*other.element_type)) {}

TypeLayout& TypeLayout::operator=(const TypeLayout& other) {
    if (this == &other) {
        return *this;
    }
    kind = other.kind;
    classification = other.classification;
    encoded_width = other.encoded_width;
    max_bytes = other.max_bytes;
    max_elements = other.max_elements;
    referenced_ir_id = other.referenced_ir_id;
    referenced_fqn = other.referenced_fqn;
    element_type = other.element_type == nullptr ? nullptr
                                                 : std::make_unique<TypeLayout>(*other.element_type);
    return *this;
}

const RecordLayout* LayoutModel::find_record(std::string_view fqn) const {
    const auto found =
        std::find_if(records.begin(), records.end(),
                     [fqn](const RecordLayout& record) { return record.fqn == fqn; });
    if (found == records.end()) {
        return nullptr;
    }
    return &*found;
}

LayoutModel LayoutComputer::compute(const semantic::SemanticModel& semantic_model,
                                    context::CompilerContext& context,
                                    diagnostics::DiagnosticCollection& diagnostics) const {
    LayoutModel layout_model;

    std::vector<const semantic::SemanticRecord*> ordered_records;
    ordered_records.reserve(semantic_model.records.size());
    for (const semantic::SemanticRecord& record : semantic_model.records) {
        ordered_records.push_back(&record);
    }

    std::stable_sort(ordered_records.begin(), ordered_records.end(),
                     [](const semantic::SemanticRecord* lhs, const semantic::SemanticRecord* rhs) {
                         return lhs->fqn < rhs->fqn;
                     });

    for (std::size_t index = 1; index < ordered_records.size(); ++index) {
        if (ordered_records[index - 1]->fqn == ordered_records[index]->fqn) {
            emit_duplicate_record_fqn(*ordered_records[index], diagnostics, context);
            return {};
        }
    }

    for (std::size_t index = 0; index < ordered_records.size(); ++index) {
        const semantic::SemanticRecord& record = *ordered_records[index];
        if (record.fields.size() > 256U) {
            emit_too_many_fields(record, diagnostics, context);
            return {};
        }

        RecordLayout record_layout;
        record_layout.fqn = record.fqn;
        record_layout.record_id = static_cast<std::uint32_t>(index + 1U);
        record_layout.fields.reserve(record.fields.size());
        for (std::size_t field_index = 0; field_index < record.fields.size(); ++field_index) {
            record_layout.fields.push_back(
                FieldLayout{.field_index = static_cast<std::uint32_t>(field_index)});
        }

        layout_model.records.push_back(std::move(record_layout));
    }

    return layout_model;
}

LayoutModel LayoutComputer::compute(const ::quarry::schema_ir::SchemaIR& schema_ir,
                                    diagnostics::DiagnosticCollection& diagnostics) const {
    return BrfV2LayoutBuilder(schema_ir, diagnostics).build();
}

} // namespace quarry::compiler::layout
