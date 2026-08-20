#include "compiler/qbs/qbs.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace quarry::compiler::qbs {
namespace {

constexpr std::string_view kQbsPass = "qbs-model";
constexpr std::uint32_t kRecordDescriptorSize = 28U;
constexpr std::uint32_t kFieldDescriptorSize = 28U;
constexpr std::uint32_t kTypeDescriptorSize = 16U;
constexpr std::uint32_t kEnumDescriptorSize = 16U;
constexpr std::uint32_t kSectionDirectoryEntrySize = 12U;

[[nodiscard]] diagnostics::DiagnosticId diagnostic_id(std::string_view value) {
    const std::optional<diagnostics::DiagnosticId> parsed = diagnostics::DiagnosticId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

void emit_error(diagnostics::DiagnosticCollection& diagnostics, std::string message) {
    diagnostics.emit(diagnostics::Diagnostic::create(
                         diagnostic_id("BC8001"), diagnostics::Severity::Error, std::move(message))
                         .from_pass(std::string(kQbsPass))
                         .build());
}

[[nodiscard]] bool checked_add(std::uint32_t lhs, std::uint32_t rhs, std::uint32_t& result) {
    if (rhs > std::numeric_limits<std::uint32_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool checked_multiply(std::uint32_t lhs, std::uint32_t rhs, std::uint32_t& result) {
    if (lhs != 0U && rhs > std::numeric_limits<std::uint32_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

[[nodiscard]] bool fits_reference(std::uint32_t value) { return value <= kQbsMaxTableReference; }

void append_u8(std::vector<std::uint8_t>& output, std::uint8_t value) { output.push_back(value); }

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void append_string(std::vector<std::uint8_t>& output, std::string_view value) {
    append_u32(output, static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

[[nodiscard]] std::uint8_t type_code(layout::LayoutTypeKind kind) {
    using layout::LayoutTypeKind;
    switch (kind) {
    case LayoutTypeKind::Bool:
        return 1U;
    case LayoutTypeKind::I8:
        return 2U;
    case LayoutTypeKind::U8:
        return 3U;
    case LayoutTypeKind::I16:
        return 4U;
    case LayoutTypeKind::U16:
        return 5U;
    case LayoutTypeKind::I32:
        return 6U;
    case LayoutTypeKind::U32:
        return 7U;
    case LayoutTypeKind::I64:
        return 8U;
    case LayoutTypeKind::U64:
        return 9U;
    case LayoutTypeKind::F32:
        return 10U;
    case LayoutTypeKind::F64:
        return 11U;
    case LayoutTypeKind::Enum:
        return 12U;
    case LayoutTypeKind::String:
        return 13U;
    case LayoutTypeKind::Bytes:
        return 14U;
    case LayoutTypeKind::Record:
        return 15U;
    case LayoutTypeKind::Array:
        return 16U;
    }
    return 0U;
}

void append_type_identity(std::vector<std::uint8_t>& output, const layout::TypeLayout& type) {
    append_u8(output, type_code(type.kind));
    append_u8(output, type.classification == layout::RecordClassification::FixedSize ? 0U : 1U);
    append_u32(output, type.encoded_width);
    append_u32(output, type.max_elements);
    append_u32(output, type.max_bytes);
    append_string(output, type.referenced_fqn);
    append_u32(output, static_cast<std::uint32_t>(type.enum_values.size()));
    for (const std::uint64_t value : type.enum_values) {
        append_u64(output, value);
    }
    append_u8(output, type.element_type != nullptr ? 1U : 0U);
    if (type.element_type != nullptr) {
        append_type_identity(output, *type.element_type);
    }
}

struct PendingType {
    std::string key;
    QbsTypeModel model;
    std::string referenced_key;
};

class Builder {
public:
    Builder(const ::quarry::schema_ir::SchemaIR& schema_ir, const layout::LayoutModel& layout_model,
            QbsBuildOptions options, diagnostics::DiagnosticCollection& diagnostics)
        : schema_ir_(schema_ir), layout_model_(layout_model), options_(options),
          diagnostics_(diagnostics) {}

    [[nodiscard]] std::optional<QbsImageModel> run() {
        model_.mode = options_.mode;
        collect_schema_objects();
        if (failed_) {
            return std::nullopt;
        }
        collect_strings();
        collect_enums();
        if (failed_ || !build_records()) {
            return std::nullopt;
        }
        finalize_types();
        if (failed_) {
            return std::nullopt;
        }
        build_identity_input();
        if (failed_) {
            return std::nullopt;
        }
        QbsImageModel result = std::move(model_);
        QbsModelBuilder validator;
        if (!validator.validate(result, diagnostics_)) {
            return std::nullopt;
        }
        return result;
    }

private:
    [[nodiscard]] std::vector<const layout::RecordLayout*> ordered_layout_records() const {
        std::vector<const layout::RecordLayout*> records;
        records.reserve(layout_model_.records.size());
        for (const auto& record : layout_model_.records) {
            records.push_back(&record);
        }
        std::sort(records.begin(), records.end(),
                  [](const auto* lhs, const auto* rhs) { return lhs->fqn < rhs->fqn; });
        return records;
    }

    void fail(std::string message) {
        if (!failed_) {
            emit_error(diagnostics_, std::move(message));
        }
        failed_ = true;
    }

    void collect_schema_objects() {
        std::vector<const ::quarry::schema_ir::NamespaceIR*> worklist{&schema_ir_.root_namespace()};
        while (!worklist.empty() && !failed_) {
            const auto* namespace_ir = worklist.back();
            worklist.pop_back();
            for (const auto& record : namespace_ir->records()) {
                if (record.fqn().empty() || record.ir_id() == 0U || record.record_id() == 0U) {
                    fail("QBS encountered a record without a valid identity");
                    return;
                }
                if (!records_by_fqn_.emplace(record.fqn(), &record).second) {
                    fail("QBS encountered duplicate record FQN: " + record.fqn());
                    return;
                }
            }
            for (const auto& enumeration : namespace_ir->enums()) {
                if (enumeration.fqn().empty() || enumeration.ir_id() == 0U) {
                    fail("QBS encountered an enum without a valid identity");
                    return;
                }
                if (!enums_by_ir_id_.emplace(enumeration.ir_id(), &enumeration).second) {
                    fail("QBS encountered duplicate enum IR identity");
                    return;
                }
            }
            for (auto child = namespace_ir->namespaces().rbegin();
                 child != namespace_ir->namespaces().rend(); ++child) {
                worklist.push_back(&*child);
            }
        }
    }

    void collect_strings() {
        if (options_.mode != BuildMode::Reflective) {
            return;
        }
        std::set<std::string> names;
        for (const auto* record : ordered_layout_records()) {
            const auto record_it = records_by_fqn_.find(record->fqn);
            if (record_it == records_by_fqn_.end()) {
                fail("QBS record is missing from Schema IR: " + record->fqn);
                return;
            }
            names.insert(record_it->second->name());
            for (const auto& field : record->fields) {
                names.insert(field.name);
            }
        }
        for (const auto& [unused_id, enumeration] : enums_by_ir_id_) {
            (void)unused_id;
            if (find_enum_layout(enumeration->fqn()) != nullptr) {
                names.insert(enumeration->name());
            }
        }
        if (names.size() > kQbsNoStringIndex) {
            fail("QBS string table exceeds 16-bit reference capacity");
            return;
        }
        model_.strings.assign(names.begin(), names.end());
        for (std::size_t index = 0; index < model_.strings.size(); ++index) {
            string_indexes_.emplace(model_.strings[index], static_cast<std::uint16_t>(index));
        }
    }

    [[nodiscard]] std::uint16_t string_index(std::string_view value) const {
        if (options_.mode != BuildMode::Reflective) {
            return kQbsNoStringIndex;
        }
        const auto found = string_indexes_.find(std::string(value));
        return found == string_indexes_.end() ? kQbsNoStringIndex : found->second;
    }

    void collect_enums() {
        std::vector<const ::quarry::schema_ir::EnumIR*> enums;
        enums.reserve(enums_by_ir_id_.size());
        for (const auto& [unused_id, enumeration] : enums_by_ir_id_) {
            (void)unused_id;
            enums.push_back(enumeration);
        }
        std::sort(enums.begin(), enums.end(),
                  [](const auto* lhs, const auto* rhs) { return lhs->fqn() < rhs->fqn(); });
        for (const auto* enumeration : enums) {
            if (find_enum_layout(enumeration->fqn()) == nullptr) {
                continue;
            }
            QbsEnumModel result;
            result.table_index = static_cast<std::uint16_t>(model_.enums.size());
            result.fqn = enumeration->fqn();
            result.name_string_index = string_index(enumeration->name());
            for (const auto& value : enumeration->values()) {
                if (value.value() < 0) {
                    fail("QBS enum contains unsupported negative value: " + value.name());
                    return;
                }
                result.values.push_back(static_cast<std::uint64_t>(value.value()));
            }
            std::sort(result.values.begin(), result.values.end());
            if (std::adjacent_find(result.values.begin(), result.values.end()) !=
                result.values.end()) {
                fail("QBS enum contains duplicate numeric values: " + result.fqn);
                return;
            }
            const auto layout_it = find_enum_layout(result.fqn);
            if (layout_it == nullptr) {
                fail("QBS enum is missing canonical Layout IR metadata: " + result.fqn);
                return;
            }
            result.encoded_width = layout_it->encoded_width;
            result.value_start = static_cast<std::uint32_t>(model_.enum_values.size());
            model_.enum_values.insert(model_.enum_values.end(), result.values.begin(),
                                      result.values.end());
            enum_indexes_by_ir_id_[enumeration->ir_id()] = result.table_index;
            model_.enums.push_back(std::move(result));
        }
    }

    [[nodiscard]] const layout::TypeLayout* find_enum_layout(std::string_view fqn) const {
        for (const auto* record : ordered_layout_records()) {
            for (const auto& field : record->fields) {
                const layout::TypeLayout* found = find_enum_layout(field.type, fqn);
                if (found != nullptr) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] static const layout::TypeLayout* find_enum_layout(const layout::TypeLayout& type,
                                                                    std::string_view fqn) {
        if (type.kind == layout::LayoutTypeKind::Enum && type.referenced_fqn == fqn) {
            return &type;
        }
        return type.element_type == nullptr ? nullptr : find_enum_layout(*type.element_type, fqn);
    }

    [[nodiscard]] std::string type_key(const layout::TypeLayout& type) const {
        std::vector<std::uint8_t> bytes;
        append_type_identity(bytes, type);
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    [[nodiscard]] std::optional<std::string> collect_type(const layout::TypeLayout& type) {
        const std::string key = type_key(type);
        if (pending_types_.find(key) != pending_types_.end()) {
            return key;
        }
        PendingType pending;
        pending.key = key;
        pending.model.code = static_cast<TypeCode>(type_code(type.kind));
        pending.model.fixed_size = type.classification == layout::RecordClassification::FixedSize;
        pending.model.encoded_width = type.encoded_width;
        pending.model.max_elements = type.max_elements;
        pending.model.max_bytes = type.max_bytes;
        if (type.kind == layout::LayoutTypeKind::Enum) {
            const auto found = enum_indexes_by_ir_id_.find(type.referenced_ir_id);
            if (found == enum_indexes_by_ir_id_.end()) {
                fail("QBS field references unknown enum: " + type.referenced_fqn);
                return std::nullopt;
            }
            pending.referenced_key = "enum:" + type.referenced_fqn;
        } else if (type.kind == layout::LayoutTypeKind::Record) {
            if (records_by_fqn_.find(type.referenced_fqn) == records_by_fqn_.end() ||
                layout_model_.find_record(type.referenced_fqn) == nullptr) {
                fail("QBS field references unknown nested record: " + type.referenced_fqn);
                return std::nullopt;
            }
            pending.referenced_key = "record:" + type.referenced_fqn;
        } else if (type.kind == layout::LayoutTypeKind::Array) {
            if (type.element_type == nullptr) {
                fail("QBS array type has no element type");
                return std::nullopt;
            }
            const auto element_key = collect_type(*type.element_type);
            if (!element_key.has_value()) {
                return std::nullopt;
            }
            pending.referenced_key = "type:" + *element_key;
        }
        pending_types_.emplace(key, std::move(pending));
        return key;
    }

    [[nodiscard]] bool build_records() {
        if (layout_model_.records.size() > static_cast<std::size_t>(kQbsMaxTableReference) + 1U) {
            fail("QBS record table exceeds 16-bit reference capacity");
            return false;
        }
        const auto ordered_records = ordered_layout_records();
        for (std::size_t index = 0; index < ordered_records.size(); ++index) {
            const auto& layout_record = *ordered_records[index];
            const auto schema_record = records_by_fqn_.find(layout_record.fqn);
            if (schema_record == records_by_fqn_.end()) {
                fail("QBS record is missing from Schema IR: " + layout_record.fqn);
                return false;
            }
            QbsRecordModel record;
            record.table_index = static_cast<std::uint16_t>(index);
            record.record_id = layout_record.record_id;
            record.field_start = static_cast<std::uint32_t>(model_.fields.size());
            if (layout_record.fields.size() > std::numeric_limits<std::uint16_t>::max()) {
                fail("QBS record field count exceeds 16-bit capacity: " + layout_record.fqn);
                return false;
            }
            record.field_count = static_cast<std::uint16_t>(layout_record.fields.size());
            record.variable_size =
                layout_record.classification == layout::RecordClassification::VariableSize;
            record.presence_bitmap_size = layout_record.presence_bitmap_size;
            record.fixed_region_size = layout_record.fixed_region_size;
            record.complete_fixed_record_size = layout_record.complete_fixed_record_size;
            record.name_string_index = string_index(schema_record->second->name());
            record.fqn = layout_record.fqn;
            model_.records.push_back(std::move(record));
            for (const auto& field : layout_record.fields) {
                if (field.field_index > std::numeric_limits<std::uint16_t>::max() ||
                    field.presence_bit_index > std::numeric_limits<std::uint16_t>::max()) {
                    fail("QBS field metadata exceeds 16-bit representation: " + field.name);
                    return false;
                }
                const auto type = collect_type(field.type);
                if (!type.has_value()) {
                    return false;
                }
                pending_field_types_.push_back(*type);
                QbsFieldModel qbs_field;
                qbs_field.owning_record_index = static_cast<std::uint16_t>(index);
                qbs_field.field_index = static_cast<std::uint16_t>(field.field_index);
                qbs_field.byte_offset = field.location.byte_offset;
                qbs_field.bit_offset = field.location.bit_offset;
                qbs_field.bit_width = field.location.bit_width;
                qbs_field.presence_bit_index = static_cast<std::uint16_t>(field.presence_bit_index);
                qbs_field.slot_size = field.slot_size;
                qbs_field.storage = storage_for(field.storage);
                qbs_field.descriptor_kind = descriptor_for(field.descriptor_kind);
                qbs_field.name_string_index = string_index(field.name);
                model_.fields.push_back(std::move(qbs_field));
            }
        }
        return true;
    }

    [[nodiscard]] static Storage storage_for(layout::FieldStorage storage) {
        switch (storage) {
        case layout::FieldStorage::Fixed:
            return Storage::Fixed;
        case layout::FieldStorage::InlineFixedNestedRecord:
            return Storage::InlineFixedNestedRecord;
        case layout::FieldStorage::VariableDescriptor:
            return Storage::VariableDescriptor;
        }
        return Storage::Fixed;
    }

    [[nodiscard]] static DescriptorKind descriptor_for(layout::DescriptorKind descriptor) {
        switch (descriptor) {
        case layout::DescriptorKind::None:
            return DescriptorKind::None;
        case layout::DescriptorKind::DataOffsetByteLength:
            return DescriptorKind::DataOffsetByteLength;
        }
        return DescriptorKind::None;
    }

    void finalize_types() {
        std::vector<PendingType> pending;
        pending.reserve(pending_types_.size());
        for (auto& [unused_key, type] : pending_types_) {
            (void)unused_key;
            pending.push_back(std::move(type));
        }
        std::sort(pending.begin(), pending.end(),
                  [](const PendingType& lhs, const PendingType& rhs) { return lhs.key < rhs.key; });
        std::unordered_map<std::string, std::uint16_t> indexes;
        for (std::size_t index = 0; index < pending.size(); ++index) {
            if (!fits_reference(static_cast<std::uint32_t>(index))) {
                fail("QBS type table exceeds 16-bit reference capacity");
                return;
            }
            indexes.emplace(pending[index].key, static_cast<std::uint16_t>(index));
        }
        for (const auto& item : pending) {
            QbsTypeModel type = item.model;
            if (!item.referenced_key.empty()) {
                std::string key = item.referenced_key;
                if (key.rfind("type:", 0) == 0U) {
                    key = key.substr(5U);
                } else if (key.rfind("enum:", 0) == 0U) {
                    const auto found = enum_index_by_fqn(key.substr(5U));
                    if (!found.has_value()) {
                        fail("QBS type references unknown enum table entry");
                        return;
                    }
                    type.reference = *found;
                    model_.types.push_back(type);
                    continue;
                } else if (key.rfind("record:", 0) == 0U) {
                    const auto found = record_index_by_fqn(key.substr(7U));
                    if (!found.has_value()) {
                        fail("QBS type references unknown record table entry");
                        return;
                    }
                    type.reference = *found;
                    model_.types.push_back(type);
                    continue;
                }
                const auto found = indexes.find(key);
                if (found == indexes.end()) {
                    fail("QBS type references unknown type table entry");
                    return;
                }
                type.reference = found->second;
            }
            model_.types.push_back(type);
        }
        for (std::size_t index = 0; index < model_.fields.size(); ++index) {
            const auto found = indexes.find(pending_field_types_[index]);
            if (found == indexes.end()) {
                fail("QBS field references unknown type table entry");
                return;
            }
            model_.fields[index].type_index = found->second;
        }
    }

    [[nodiscard]] std::optional<std::uint16_t> enum_index_by_fqn(std::string_view fqn) const {
        for (const auto& enumeration : model_.enums) {
            if (enumeration.fqn == fqn) {
                return enumeration.table_index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint16_t> record_index_by_fqn(std::string_view fqn) const {
        for (const auto& record : model_.records) {
            if (record.fqn == fqn) {
                return record.table_index;
            }
        }
        return std::nullopt;
    }

    void build_identity_input() {
        auto& output = model_.schema_identity_input;
        append_string(output, "quarry.qbs.schema");
        append_u8(output, kBrfFormatVersion);
        for (const auto* record : ordered_layout_records()) {
            append_string(output, record->fqn);
            append_u32(output, record->record_id);
            append_u8(output,
                      record->classification == layout::RecordClassification::FixedSize ? 0U : 1U);
            append_u32(output, record->header_size);
            append_u32(output, record->presence_bitmap_size);
            append_u32(output, record->fixed_region_size);
            append_u32(output, record->complete_fixed_record_size.value_or(0U));
            append_u32(output, static_cast<std::uint32_t>(record->fields.size()));
            for (const auto& field : record->fields) {
                append_u32(output, field.field_index);
                append_u32(output, field.presence_bit_index);
                append_u32(output, field.location.byte_offset);
                append_u8(output, field.location.bit_offset);
                append_u32(output, field.location.bit_width);
                append_u32(output, field.slot_size);
                append_u8(output, static_cast<std::uint8_t>(field.storage));
                append_u8(output, static_cast<std::uint8_t>(field.descriptor_kind));
                append_type_identity(output, field.type);
            }
        }
        std::vector<const ::quarry::schema_ir::EnumIR*> enums;
        for (const auto& [unused_id, enumeration] : enums_by_ir_id_) {
            (void)unused_id;
            enums.push_back(enumeration);
        }
        std::sort(enums.begin(), enums.end(),
                  [](const auto* lhs, const auto* rhs) { return lhs->fqn() < rhs->fqn(); });
        // Enum table indexes are a serialization detail and are deliberately
        // excluded from the structural identity input.
        std::uint32_t used_enum_count = 0U;
        for (const auto* enumeration : enums) {
            if (find_enum_layout(enumeration->fqn()) != nullptr) {
                ++used_enum_count;
            }
        }
        append_u32(output, used_enum_count);
        for (const auto* enumeration : enums) {
            const auto* enum_layout = find_enum_layout(enumeration->fqn());
            if (enum_layout == nullptr) {
                continue;
            }
            append_string(output, enumeration->fqn());
            append_u32(output, enum_layout->encoded_width);
            std::vector<std::uint64_t> values;
            for (const auto& value : enumeration->values()) {
                values.push_back(static_cast<std::uint64_t>(value.value()));
            }
            std::sort(values.begin(), values.end());
            append_u32(output, static_cast<std::uint32_t>(values.size()));
            for (const std::uint64_t value : values) {
                append_u64(output, value);
            }
        }
    }

    const ::quarry::schema_ir::SchemaIR& schema_ir_;
    const layout::LayoutModel& layout_model_;
    QbsBuildOptions options_;
    diagnostics::DiagnosticCollection& diagnostics_;
    QbsImageModel model_;
    bool failed_ = false;
    std::map<std::string, const ::quarry::schema_ir::RecordIR*> records_by_fqn_;
    std::unordered_map<std::uint64_t, const ::quarry::schema_ir::EnumIR*> enums_by_ir_id_;
    std::unordered_map<std::uint64_t, std::uint16_t> enum_indexes_by_ir_id_;
    std::map<std::string, std::uint16_t> string_indexes_;
    std::unordered_map<std::string, PendingType> pending_types_;
    std::vector<std::string> pending_field_types_;
};

} // namespace

std::optional<QbsImageModel>
QbsModelBuilder::build(const ::quarry::schema_ir::SchemaIR& schema_ir,
                       const layout::LayoutModel& layout_model, QbsBuildOptions options,
                       diagnostics::DiagnosticCollection& diagnostics) const {
    return Builder(schema_ir, layout_model, options, diagnostics).run();
}

bool QbsModelBuilder::validate(const QbsImageModel& model,
                               diagnostics::DiagnosticCollection& diagnostics) const {
    if (model.format_version != kQbsFormatVersion ||
        model.brf_format_version != kBrfFormatVersion) {
        emit_error(diagnostics, "QBS model has unsupported format version");
        return false;
    }
    if (model.records.size() > static_cast<std::size_t>(kQbsMaxTableReference) + 1U ||
        model.types.size() > static_cast<std::size_t>(kQbsMaxTableReference) + 1U ||
        model.enums.size() > static_cast<std::size_t>(kQbsMaxTableReference) + 1U ||
        model.strings.size() > kQbsMaxTableReference) {
        emit_error(diagnostics, "QBS model table exceeds 16-bit reference capacity");
        return false;
    }
    std::uint32_t projected_size = 40U;
    const std::uint32_t section_count = static_cast<std::uint32_t>(
        3U + (!model.enums.empty() ? 2U : 0U) + (!model.strings.empty() ? 1U : 0U));
    if (!checked_add(projected_size, section_count * kSectionDirectoryEntrySize, projected_size)) {
        emit_error(diagnostics, "QBS projected image size overflows 32 bits");
        return false;
    }
    const auto add_section = [&](std::size_t count, std::uint32_t stride) {
        std::uint32_t size = 0U;
        return count <= std::numeric_limits<std::uint32_t>::max() &&
               checked_multiply(static_cast<std::uint32_t>(count), stride, size) &&
               checked_add(projected_size, size, projected_size);
    };
    if (!add_section(model.records.size(), kRecordDescriptorSize) ||
        !add_section(model.fields.size(), kFieldDescriptorSize) ||
        !add_section(model.types.size(), kTypeDescriptorSize) ||
        !add_section(model.enums.size(), kEnumDescriptorSize) ||
        model.enum_values.size() > std::numeric_limits<std::uint32_t>::max() / 8U ||
        !add_section(model.enum_values.size(), 8U)) {
        emit_error(diagnostics, "QBS projected table size overflows 32 bits");
        return false;
    }
    if (!model.strings.empty()) {
        std::uint32_t offset_count = 0U;
        std::uint32_t offset_bytes = 0U;
        if (model.strings.size() > std::numeric_limits<std::uint32_t>::max() - 1U ||
            !checked_add(static_cast<std::uint32_t>(model.strings.size()), 1U, offset_count) ||
            !checked_multiply(offset_count, 4U, offset_bytes) ||
            !checked_add(projected_size, 4U, projected_size) ||
            !checked_add(projected_size, offset_bytes, projected_size)) {
            emit_error(diagnostics, "QBS projected string table size overflows 32 bits");
            return false;
        }
        std::uint32_t string_bytes = 0U;
        for (const auto& string : model.strings) {
            if (string.size() > std::numeric_limits<std::uint32_t>::max() - string_bytes ||
                !checked_add(string_bytes, static_cast<std::uint32_t>(string.size()),
                             string_bytes)) {
                emit_error(diagnostics, "QBS projected string data size overflows 32 bits");
                return false;
            }
        }
        if (!checked_add(projected_size, string_bytes, projected_size)) {
            emit_error(diagnostics, "QBS projected string table size overflows 32 bits");
            return false;
        }
    }
    std::set<std::uint32_t> record_ids;
    std::set<std::string> record_fqns;
    for (std::size_t index = 0; index < model.records.size(); ++index) {
        const auto& record = model.records[index];
        if (record.record_id == 0U || !record_ids.insert(record.record_id).second ||
            !record_fqns.insert(record.fqn).second) {
            emit_error(diagnostics, "QBS record table contains a duplicate or invalid identity");
            return false;
        }
        std::uint32_t field_end = 0U;
        if (record.table_index != index ||
            !checked_add(record.field_start, record.field_count, field_end) ||
            field_end > model.fields.size()) {
            emit_error(diagnostics, "QBS record table contains an invalid field range");
            return false;
        }
        if (model.mode == BuildMode::Minimal && record.name_string_index != kQbsNoStringIndex) {
            emit_error(diagnostics, "minimal QBS model contains a record name");
            return false;
        }
        if (record.name_string_index != kQbsNoStringIndex &&
            record.name_string_index >= model.strings.size()) {
            emit_error(diagnostics, "QBS record references an invalid string index");
            return false;
        }
    }
    for (std::size_t index = 0; index < model.fields.size(); ++index) {
        const auto& field = model.fields[index];
        if (field.owning_record_index >= model.records.size() ||
            field.type_index >= model.types.size() ||
            (model.mode == BuildMode::Minimal && field.name_string_index != kQbsNoStringIndex) ||
            (field.name_string_index != kQbsNoStringIndex &&
             field.name_string_index >= model.strings.size())) {
            emit_error(diagnostics, "QBS field contains an invalid reference");
            return false;
        }
        const auto& owner = model.records[field.owning_record_index];
        std::uint32_t fixed_end = 0U;
        if (!checked_add(layout::kBrfV2HeaderSize, owner.presence_bitmap_size, fixed_end) ||
            !checked_add(fixed_end, owner.fixed_region_size, fixed_end)) {
            emit_error(diagnostics, "QBS record fixed-region arithmetic overflows 32 bits");
            return false;
        }
        std::uint32_t owner_field_end = 0U;
        if (!checked_add(owner.field_start, owner.field_count, owner_field_end) ||
            index < owner.field_start || index >= owner_field_end ||
            field.field_index != index - owner.field_start) {
            emit_error(diagnostics, "QBS field table is inconsistent with its owning record");
            return false;
        }
        const auto& type = model.types[field.type_index];
        const bool fixed_storage = field.storage == Storage::Fixed;
        const bool inline_storage = field.storage == Storage::InlineFixedNestedRecord;
        const bool variable_storage = field.storage == Storage::VariableDescriptor;
        if ((fixed_storage && !type.fixed_size) ||
            (inline_storage && (type.code != TypeCode::Record || !type.fixed_size)) ||
            (variable_storage && type.fixed_size) ||
            (variable_storage && field.descriptor_kind != DescriptorKind::DataOffsetByteLength) ||
            (!variable_storage && field.descriptor_kind != DescriptorKind::None) ||
            (variable_storage && field.slot_size != 8U)) {
            emit_error(diagnostics, "QBS field storage is inconsistent with its type");
            return false;
        }
        std::uint32_t slot_end = 0U;
        std::uint32_t presence_bits = 0U;
        if (!checked_add(field.byte_offset, field.slot_size, slot_end) ||
            !checked_multiply(owner.presence_bitmap_size, 8U, presence_bits) ||
            field.byte_offset < layout::kBrfV2HeaderSize || slot_end > fixed_end ||
            field.presence_bit_index >= presence_bits || field.bit_offset > 7U ||
            field.bit_width == 0U) {
            emit_error(diagnostics, "QBS field location is outside the representable BRF range");
            return false;
        }
    }
    for (std::size_t index = 0; index < model.enums.size(); ++index) {
        const auto& enumeration = model.enums[index];
        std::uint32_t value_end = 0U;
        if (enumeration.table_index != index ||
            !checked_add(enumeration.value_start,
                         static_cast<std::uint32_t>(enumeration.values.size()), value_end) ||
            value_end > model.enum_values.size() ||
            (enumeration.name_string_index != kQbsNoStringIndex &&
             enumeration.name_string_index >= model.strings.size())) {
            emit_error(diagnostics, "QBS enum contains an invalid reference");
            return false;
        }
        if (model.mode == BuildMode::Minimal &&
            enumeration.name_string_index != kQbsNoStringIndex) {
            emit_error(diagnostics, "minimal QBS model contains an enum name");
            return false;
        }
        if (!std::is_sorted(enumeration.values.begin(), enumeration.values.end()) ||
            std::adjacent_find(enumeration.values.begin(), enumeration.values.end()) !=
                enumeration.values.end() ||
            !std::equal(enumeration.values.begin(), enumeration.values.end(),
                        model.enum_values.begin() + enumeration.value_start)) {
            emit_error(diagnostics, "QBS enum values are not canonicalized");
            return false;
        }
        if (enumeration.encoded_width == 0U || enumeration.encoded_width > 8U) {
            emit_error(diagnostics, "QBS enum has an invalid encoded width");
            return false;
        }
        const std::uint32_t value_bits = enumeration.encoded_width * 8U;
        if (value_bits < 64U) {
            const std::uint64_t maximum = (std::uint64_t{1} << value_bits) - 1U;
            if (std::any_of(enumeration.values.begin(), enumeration.values.end(),
                            [maximum](std::uint64_t value) { return value > maximum; })) {
                emit_error(diagnostics, "QBS enum value does not fit its encoded width");
                return false;
            }
        }
    }
    for (std::size_t index = 0; index < model.types.size(); ++index) {
        const auto& type = model.types[index];
        const auto raw_code = static_cast<std::uint8_t>(type.code);
        if (raw_code < static_cast<std::uint8_t>(TypeCode::Bool) ||
            raw_code > static_cast<std::uint8_t>(TypeCode::Array)) {
            emit_error(diagnostics, "QBS type contains an unsupported type code");
            return false;
        }
        if (type.code == TypeCode::Enum && type.reference >= model.enums.size()) {
            emit_error(diagnostics, "QBS type references an invalid enum table entry");
            return false;
        }
        if (type.code == TypeCode::Record && type.reference >= model.records.size()) {
            emit_error(diagnostics, "QBS type references an invalid record table entry");
            return false;
        }
        if (type.code == TypeCode::Array && type.reference >= model.types.size()) {
            emit_error(diagnostics, "QBS array type references an invalid element type");
            return false;
        }
        const bool intrinsically_fixed =
            type.code != TypeCode::String && type.code != TypeCode::Bytes &&
            type.code != TypeCode::Array && type.code != TypeCode::Record;
        if (type.code != TypeCode::Record && type.fixed_size != intrinsically_fixed) {
            emit_error(diagnostics, "QBS type has inconsistent fixed/variable classification");
            return false;
        }
        if (type.code == TypeCode::Array && type.max_elements == 0U) {
            emit_error(diagnostics, "QBS array type has no element bound");
            return false;
        }
    }
    if (model.mode == BuildMode::Minimal && !model.strings.empty()) {
        emit_error(diagnostics, "minimal QBS model contains reflective strings");
        return false;
    }
    if (!std::is_sorted(model.strings.begin(), model.strings.end()) ||
        std::adjacent_find(model.strings.begin(), model.strings.end()) != model.strings.end()) {
        emit_error(diagnostics, "QBS strings are not canonicalized");
        return false;
    }
    return true;
}

} // namespace quarry::compiler::qbs
