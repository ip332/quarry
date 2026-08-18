#pragma once

#include "compiler/layout/layout.hpp"
#include "quarry/runtime/binary_record_v2.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace quarry::compiler::layout {

[[nodiscard]] inline std::optional<runtime::BrfV2TypeLayout>
to_brf_v2_runtime_type(const TypeLayout& type,
                       const std::unordered_map<std::string, std::uint32_t>& record_ids) {
    runtime::BrfV2TypeLayout result;
    result.encoded_width = type.encoded_width;
    result.max_bytes = type.max_bytes;
    result.max_elements = type.max_elements;
    switch (type.kind) {
    case LayoutTypeKind::Bool:
        result.kind = runtime::BrfV2TypeKind::Bool;
        break;
    case LayoutTypeKind::I8:
        result.kind = runtime::BrfV2TypeKind::I8;
        break;
    case LayoutTypeKind::U8:
        result.kind = runtime::BrfV2TypeKind::U8;
        break;
    case LayoutTypeKind::I16:
        result.kind = runtime::BrfV2TypeKind::I16;
        break;
    case LayoutTypeKind::U16:
        result.kind = runtime::BrfV2TypeKind::U16;
        break;
    case LayoutTypeKind::I32:
        result.kind = runtime::BrfV2TypeKind::I32;
        break;
    case LayoutTypeKind::U32:
        result.kind = runtime::BrfV2TypeKind::U32;
        break;
    case LayoutTypeKind::I64:
        result.kind = runtime::BrfV2TypeKind::I64;
        break;
    case LayoutTypeKind::U64:
        result.kind = runtime::BrfV2TypeKind::U64;
        break;
    case LayoutTypeKind::F32:
        result.kind = runtime::BrfV2TypeKind::F32;
        break;
    case LayoutTypeKind::F64:
        result.kind = runtime::BrfV2TypeKind::F64;
        break;
    case LayoutTypeKind::Enum:
        result.kind = runtime::BrfV2TypeKind::Enum;
        break;
    case LayoutTypeKind::String:
        result.kind = runtime::BrfV2TypeKind::String;
        break;
    case LayoutTypeKind::Bytes:
        result.kind = runtime::BrfV2TypeKind::Bytes;
        break;
    case LayoutTypeKind::Record: {
        result.kind = runtime::BrfV2TypeKind::Record;
        const auto found = record_ids.find(type.referenced_fqn);
        if (found == record_ids.end()) {
            return std::nullopt;
        }
        result.nested_record_id = found->second;
        break;
    }
    case LayoutTypeKind::Array:
        result.kind = runtime::BrfV2TypeKind::Array;
        if (type.element_type == nullptr) {
            return std::nullopt;
        }
        {
            const auto element = to_brf_v2_runtime_type(*type.element_type, record_ids);
            if (!element.has_value()) {
                return std::nullopt;
            }
            result.element_type = std::make_shared<runtime::BrfV2TypeLayout>(*element);
        }
        break;
    }
    return result;
}

[[nodiscard]] inline std::optional<runtime::BrfV2LayoutRegistry>
to_brf_v2_runtime_layout(const LayoutModel& model) {
    std::unordered_map<std::string, std::uint32_t> record_ids;
    record_ids.reserve(model.records.size());
    for (const RecordLayout& record : model.records) {
        record_ids.emplace(record.fqn, record.record_id);
    }

    runtime::BrfV2LayoutRegistry result;
    result.records.reserve(model.records.size());
    for (const RecordLayout& record : model.records) {
        runtime::BrfV2RecordLayout runtime_record;
        runtime_record.record_id = record.record_id;
        runtime_record.presence_bitmap_size = record.presence_bitmap_size;
        runtime_record.fixed_region_size = record.fixed_region_size;
        runtime_record.complete_fixed_record_size = record.complete_fixed_record_size;
        runtime_record.fields.reserve(record.fields.size());
        for (const FieldLayout& field : record.fields) {
            const auto type = to_brf_v2_runtime_type(field.type, record_ids);
            if (!type.has_value()) {
                return std::nullopt;
            }
            runtime::BrfV2FieldLayout runtime_field;
            runtime_field.field_index = field.field_index;
            runtime_field.presence_bit_index = field.presence_bit_index;
            runtime_field.byte_offset = field.location.byte_offset;
            runtime_field.bit_offset = field.location.bit_offset;
            runtime_field.bit_width = field.location.bit_width;
            runtime_field.slot_size = field.slot_size;
            runtime_field.type = *type;
            switch (field.storage) {
            case FieldStorage::Fixed:
                runtime_field.storage = runtime::BrfV2FieldStorage::Fixed;
                break;
            case FieldStorage::InlineFixedNestedRecord:
                runtime_field.storage = runtime::BrfV2FieldStorage::InlineFixedNestedRecord;
                break;
            case FieldStorage::VariableDescriptor:
                runtime_field.storage = runtime::BrfV2FieldStorage::VariableDescriptor;
                break;
            }
            runtime_record.fields.push_back(std::move(runtime_field));
        }
        result.records.push_back(std::move(runtime_record));
    }
    return result;
}

} // namespace quarry::compiler::layout
