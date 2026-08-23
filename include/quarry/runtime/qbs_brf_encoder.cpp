#include "quarry/runtime/qbs_brf_encoder.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string_view>

namespace quarry::runtime {
namespace {

std::optional<std::vector<std::uint8_t>>
encode_brf_record_impl(const quarry::compiler::qbs::ValidatedQbsView&,
                       const quarry::compiler::qbs::QbsRecordView&,
                       std::span<const std::optional<BrfEncodeValue>>, GenericBrfEncodeError*,
                       BrfEncodeLimits, std::size_t);

bool encode_nested(const quarry::compiler::qbs::ValidatedQbsView&,
                   const quarry::compiler::qbs::QbsTypeView&, const BrfEncodeValue&,
                   std::vector<std::uint8_t>&, GenericBrfEncodeError*, BrfEncodeLimits,
                   std::size_t);

void set_error(GenericBrfEncodeError* error, GenericBrfEncodeError value) {
    if (error != nullptr)
        *error = value;
}

void put16(std::span<std::uint8_t> bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value);
}

void put32(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value);
}

void put64(std::span<std::uint8_t> bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned i = 0U; i < 8U; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (56U - i * 8U));
}

void put_integer(std::span<std::uint8_t> bytes, std::uint64_t value) {
    for (std::size_t i = 0U; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(value >> ((bytes.size() - 1U - i) * 8U));
}

void append_varuint(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    while (value >= 0x80U) {
        bytes.push_back(static_cast<std::uint8_t>(value) | 0x80U);
        value >>= 7U;
    }
    bytes.push_back(static_cast<std::uint8_t>(value));
}

bool signed_range(std::int64_t value, std::uint16_t width) {
    if (width == 0U || width > 8U)
        return false;
    if (width == 8U)
        return true;
    const auto bits = static_cast<unsigned>(width) * 8U;
    return value >= -(std::int64_t{1} << (bits - 1U)) &&
           value <= (std::int64_t{1} << (bits - 1U)) - 1;
}

bool unsigned_range(std::uint64_t value, std::uint16_t width) {
    return width != 0U && width <= 8U &&
           (width == 8U || value <= (std::uint64_t{1} << (width * 8U)) - 1U);
}

bool valid_utf8(std::string_view value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(value.data());
    std::size_t i = 0U;
    while (i < value.size()) {
        const auto first = bytes[i++];
        if (first <= 0x7FU)
            continue;
        std::uint32_t code_point = 0U;
        std::size_t continuation_count = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            code_point = first & 0x1FU;
            continuation_count = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            code_point = first & 0x0FU;
            continuation_count = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            code_point = first & 0x07U;
            continuation_count = 3U;
        } else {
            return false;
        }
        if (value.size() - i < continuation_count)
            return false;
        for (std::size_t j = 0U; j < continuation_count; ++j) {
            const auto next = bytes[i++];
            if ((next & 0xC0U) != 0x80U)
                return false;
            code_point = (code_point << 6U) | (next & 0x3FU);
        }
        if ((continuation_count == 1U && code_point < 0x80U) ||
            (continuation_count == 2U && code_point < 0x800U) ||
            (continuation_count == 3U && code_point < 0x10000U) || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU))
            return false;
    }
    return true;
}

bool write_scalar(const quarry::compiler::qbs::ValidatedQbsView& schema,
                  const quarry::compiler::qbs::QbsTypeView& type, const BrfEncodeValue& value,
                  std::span<std::uint8_t> slot, GenericBrfEncodeError* error) {
    const auto signed_type =
        type.code == 2U || type.code == 4U || type.code == 6U || type.code == 8U;
    const auto unsigned_type =
        type.code == 3U || type.code == 5U || type.code == 7U || type.code == 9U;
    if (type.code == 1U) {
        if (!std::holds_alternative<bool>(value) || slot.size() != 1U) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        slot[0] = std::get<bool>(value) ? 1U : 0U;
        return true;
    }
    if (signed_type) {
        if (!std::holds_alternative<std::int64_t>(value) ||
            !signed_range(std::get<std::int64_t>(value), type.encoded_width)) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        put_integer(slot, static_cast<std::uint64_t>(std::get<std::int64_t>(value)));
        return true;
    }
    if (unsigned_type) {
        if (!std::holds_alternative<std::uint64_t>(value) ||
            !unsigned_range(std::get<std::uint64_t>(value), type.encoded_width)) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        put_integer(slot, std::get<std::uint64_t>(value));
        return true;
    }
    if (type.code == 10U) {
        if (!std::holds_alternative<float>(value) || slot.size() != 4U) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        put32(slot, 0U, std::bit_cast<std::uint32_t>(std::get<float>(value)));
        return true;
    }
    if (type.code == 11U) {
        if (!std::holds_alternative<double>(value) || slot.size() != 8U) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        put64(slot, 0U, std::bit_cast<std::uint64_t>(std::get<double>(value)));
        return true;
    }
    if (type.code == 12U) {
        if (!std::holds_alternative<std::uint64_t>(value) ||
            type.reference >= schema.enum_count() ||
            !unsigned_range(std::get<std::uint64_t>(value), type.encoded_width)) {
            set_error(error, GenericBrfEncodeError::invalid_enum);
            return false;
        }
        const auto allowed = schema.enum_type(type.reference).values;
        if (std::find(allowed.begin(), allowed.end(), std::get<std::uint64_t>(value)) ==
            allowed.end()) {
            set_error(error, GenericBrfEncodeError::invalid_enum);
            return false;
        }
        put_integer(slot, std::get<std::uint64_t>(value));
        return true;
    }
    set_error(error, GenericBrfEncodeError::unsupported_type);
    return false;
}

bool write_variable(const quarry::compiler::qbs::QbsTypeView& type, const BrfEncodeValue& value,
                    std::vector<std::uint8_t>& payload, GenericBrfEncodeError* error) {
    if (type.code == 13U) {
        if (!std::holds_alternative<std::string>(value) ||
            !valid_utf8(std::get<std::string>(value)) ||
            std::get<std::string>(value).size() > type.max_bytes) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        const auto& string = std::get<std::string>(value);
        payload.assign(string.begin(), string.end());
        return true;
    }
    if (type.code == 14U) {
        if (!std::holds_alternative<std::vector<std::uint8_t>>(value)) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        payload = std::get<std::vector<std::uint8_t>>(value);
        if (payload.size() > type.max_bytes) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        return true;
    }
    set_error(error, GenericBrfEncodeError::unsupported_type);
    return false;
}

bool write_array(const quarry::compiler::qbs::ValidatedQbsView& schema,
                 const quarry::compiler::qbs::QbsTypeView& array_type, const BrfEncodeValue& value,
                 std::vector<std::uint8_t>& payload, GenericBrfEncodeError* error,
                 BrfEncodeLimits limits, std::size_t depth) {
    if (array_type.reference >= schema.type_count()) {
        set_error(error, GenericBrfEncodeError::invalid_value);
        return false;
    }
    const auto element = schema.type(array_type.reference);

    if (element.code == 15U) {
        if (!std::holds_alternative<BrfRecordArrayValue>(value) ||
            !std::get<BrfRecordArrayValue>(value) ||
            std::get<BrfRecordArrayValue>(value)->size() > array_type.max_elements ||
            element.reference >= schema.record_count()) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        const auto& children = *std::get<BrfRecordArrayValue>(value);
        const auto child_schema = schema.record(element.reference);
        append_varuint(payload, children.size());
        for (std::size_t i = 0U; i < children.size(); ++i) {
            if (i >= limits.max_work_items) {
                set_error(error, GenericBrfEncodeError::overflow);
                return false;
            }
            BrfEncodeValue child_value{children[i]};
            std::vector<std::uint8_t> child_bytes;
            if (!encode_nested(schema, element, child_value, child_bytes, error, limits, depth))
                return false;
            if (child_bytes.empty() ||
                child_bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
                (!child_schema.variable_size &&
                 (child_schema.complete_fixed_record_size == 0U ||
                  child_bytes.size() != child_schema.complete_fixed_record_size))) {
                set_error(error, GenericBrfEncodeError::overflow);
                return false;
            }
            if (child_schema.variable_size)
                append_varuint(payload, child_bytes.size());
            payload.insert(payload.end(), child_bytes.begin(), child_bytes.end());
        }
        return true;
    }
    if (!std::holds_alternative<BrfEncodeArray>(value)) {
        set_error(error, GenericBrfEncodeError::invalid_value);
        return false;
    }
    const auto& array = std::get<BrfEncodeArray>(value);
    const auto append_integer = [&](std::uint64_t item) {
        std::vector<std::uint8_t> encoded(element.encoded_width);
        put_integer(encoded, item);
        payload.insert(payload.end(), encoded.begin(), encoded.end());
    };
    const auto append_float = [&](std::uint64_t item, std::size_t width) {
        std::vector<std::uint8_t> encoded(width);
        put_integer(encoded, item);
        payload.insert(payload.end(), encoded.begin(), encoded.end());
    };
    std::size_t count = 0U;
    if (std::holds_alternative<BrfBoolArray>(array)) {
        if (element.code != 1U) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        const auto& values = std::get<BrfBoolArray>(array);
        count = values.size();
        if (count > array_type.max_elements) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        append_varuint(payload, count);
        for (const auto item : values)
            payload.push_back(item ? 1U : 0U);
        return true;
    }
    if (std::holds_alternative<BrfSignedArray>(array)) {
        const auto& values = std::get<BrfSignedArray>(array);
        const auto signed_type =
            element.code == 2U || element.code == 4U || element.code == 6U || element.code == 8U;
        if (!signed_type || values.size() > array_type.max_elements) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        append_varuint(payload, values.size());
        for (const auto item : values) {
            if (!signed_range(item, element.encoded_width)) {
                set_error(error, GenericBrfEncodeError::invalid_value);
                return false;
            }
            append_integer(static_cast<std::uint64_t>(item));
        }
        return true;
    }
    if (std::holds_alternative<BrfUnsignedArray>(array)) {
        const auto& values = std::get<BrfUnsignedArray>(array);
        const auto unsigned_type =
            element.code == 3U || element.code == 5U || element.code == 7U || element.code == 9U;
        const auto enum_type = element.code == 12U;
        if ((!unsigned_type && !enum_type) || values.size() > array_type.max_elements) {
            set_error(error, enum_type ? GenericBrfEncodeError::invalid_enum
                                       : GenericBrfEncodeError::invalid_value);
            return false;
        }
        append_varuint(payload, values.size());
        for (const auto item : values) {
            if (!unsigned_range(item, element.encoded_width)) {
                set_error(error, enum_type ? GenericBrfEncodeError::invalid_enum
                                           : GenericBrfEncodeError::invalid_value);
                return false;
            }
            if (enum_type && (element.reference >= schema.enum_count() ||
                              std::find(schema.enum_type(element.reference).values.begin(),
                                        schema.enum_type(element.reference).values.end(), item) ==
                                  schema.enum_type(element.reference).values.end())) {
                set_error(error, GenericBrfEncodeError::invalid_enum);
                return false;
            }
            append_integer(item);
        }
        return true;
    }
    if (std::holds_alternative<BrfFloat32Array>(array) && element.code == 10U) {
        const auto& values = std::get<BrfFloat32Array>(array);
        if (values.size() > array_type.max_elements) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        append_varuint(payload, values.size());
        for (const auto item : values)
            append_float(std::bit_cast<std::uint32_t>(item), 4U);
        return true;
    }
    if (std::holds_alternative<BrfFloat64Array>(array) && element.code == 11U) {
        const auto& values = std::get<BrfFloat64Array>(array);
        if (values.size() > array_type.max_elements) {
            set_error(error, GenericBrfEncodeError::invalid_value);
            return false;
        }
        append_varuint(payload, values.size());
        for (const auto item : values)
            append_float(std::bit_cast<std::uint64_t>(item), 8U);
        return true;
    }
    set_error(error, GenericBrfEncodeError::invalid_value);
    return false;
}

bool encode_nested(const quarry::compiler::qbs::ValidatedQbsView& schema,
                   const quarry::compiler::qbs::QbsTypeView& type, const BrfEncodeValue& value,
                   std::vector<std::uint8_t>& bytes, GenericBrfEncodeError* error,
                   BrfEncodeLimits limits, std::size_t depth) {
    if (!std::holds_alternative<BrfNestedRecordValue>(value) ||
        !std::get<BrfNestedRecordValue>(value) || type.reference >= schema.record_count()) {
        set_error(error, GenericBrfEncodeError::invalid_value);
        return false;
    }
    const auto& input = *std::get<BrfNestedRecordValue>(value);
    const auto child_schema = schema.record(type.reference);
    if (input.record_id != child_schema.record_id || input.identity != child_schema.identity) {
        set_error(error, GenericBrfEncodeError::invalid_schema);
        return false;
    }
    const auto child =
        encode_brf_record_impl(schema, child_schema, input.fields, error, limits, depth + 1U);
    if (!child.has_value())
        return false;
    bytes = *child;
    return true;
}

std::optional<std::vector<std::uint8_t>>
encode_brf_record_impl(const quarry::compiler::qbs::ValidatedQbsView& schema,
                       const quarry::compiler::qbs::QbsRecordView& record_schema,
                       std::span<const std::optional<BrfEncodeValue>> fields,
                       GenericBrfEncodeError* error, BrfEncodeLimits limits, std::size_t depth) {
    set_error(error, GenericBrfEncodeError::none);
    if (depth > limits.max_nested_records) {
        set_error(error, GenericBrfEncodeError::overflow);
        return std::nullopt;
    }
    if (fields.size() != record_schema.field_count ||
        record_schema.fixed_region_size > std::numeric_limits<std::uint32_t>::max() - 16U) {
        set_error(error, fields.size() != record_schema.field_count
                             ? GenericBrfEncodeError::field_count_mismatch
                             : GenericBrfEncodeError::invalid_schema);
        return std::nullopt;
    }
    std::size_t record_index = schema.record_count();
    for (std::size_t i = 0U; i < schema.record_count(); ++i) {
        const auto candidate = schema.record(i);
        if (candidate.record_id == record_schema.record_id &&
            candidate.identity == record_schema.identity) {
            record_index = i;
            break;
        }
    }
    if (record_index == schema.record_count()) {
        set_error(error, GenericBrfEncodeError::invalid_schema);
        return std::nullopt;
    }
    const auto fixed_end = static_cast<std::size_t>(16U) + record_schema.fixed_region_size;
    if (fixed_end > limits.max_record_bytes) {
        set_error(error, GenericBrfEncodeError::overflow);
        return std::nullopt;
    }
    std::vector<std::uint8_t> result(fixed_end, 0U);
    std::vector<std::vector<std::uint8_t>> variable_payloads(fields.size());
    std::vector<bool> variable_present(fields.size(), false);
    for (std::size_t i = 0U; i < fields.size(); ++i) {
        if (i >= limits.max_work_items) {
            set_error(error, GenericBrfEncodeError::overflow);
            return std::nullopt;
        }
        const auto field_schema = schema.find_field(record_index, static_cast<std::uint16_t>(i));
        if (!field_schema.has_value()) {
            set_error(error, GenericBrfEncodeError::invalid_schema);
            return std::nullopt;
        }
        if (field_schema->type_index >= schema.type_count()) {
            set_error(error, GenericBrfEncodeError::unsupported_type);
            return std::nullopt;
        }
        const auto type = schema.type(field_schema->type_index);
        if (type.code == 15U && type.reference >= schema.record_count()) {
            set_error(error, GenericBrfEncodeError::invalid_schema);
            return std::nullopt;
        }
        if (field_schema->presence_bit_index / 8U >= record_schema.presence_bitmap_size ||
            field_schema->byte_offset > record_schema.fixed_region_size + 16U ||
            field_schema->slot_size >
                record_schema.fixed_region_size + 16U - field_schema->byte_offset) {
            set_error(error, GenericBrfEncodeError::invalid_schema);
            return std::nullopt;
        }
        if (!fields[i].has_value()) {
            continue;
        }
        if (field_schema->storage == 2U) {
            const auto ok =
                type.code == 16U   ? write_array(schema, type, *fields[i], variable_payloads[i],
                                                 error, limits, depth)
                : type.code == 15U ? encode_nested(schema, type, *fields[i], variable_payloads[i],
                                                   error, limits, depth)
                                   : write_variable(type, *fields[i], variable_payloads[i], error);
            if (!ok)
                return std::nullopt;
            variable_present[i] = true;
            continue;
        }
        if (type.code == 13U || type.code == 14U || type.code == 16U) {
            set_error(error, GenericBrfEncodeError::invalid_schema);
            return std::nullopt;
        }
        if (type.code == 15U) {
            std::vector<std::uint8_t> child;
            if (!encode_nested(schema, type, *fields[i], child, error, limits, depth))
                return std::nullopt;
            if (child.size() != field_schema->slot_size) {
                set_error(error, GenericBrfEncodeError::invalid_schema);
                return std::nullopt;
            }
            std::copy(child.begin(), child.end(), result.begin() + field_schema->byte_offset);
            result[16U + field_schema->presence_bit_index / 8U] |=
                static_cast<std::uint8_t>(1U << (field_schema->presence_bit_index % 8U));
            continue;
        }
        result[16U + field_schema->presence_bit_index / 8U] |=
            static_cast<std::uint8_t>(1U << (field_schema->presence_bit_index % 8U));
        if (!write_scalar(schema, type, *fields[i],
                          std::span<std::uint8_t>(result).subspan(field_schema->byte_offset,
                                                                  field_schema->slot_size),
                          error))
            return std::nullopt;
    }
    std::size_t tail_size = 0U;
    for (std::size_t i = 0U; i < fields.size(); ++i) {
        if (!variable_present[i])
            continue;
        if (variable_payloads[i].size() > std::numeric_limits<std::uint32_t>::max() - tail_size ||
            fixed_end > std::numeric_limits<std::uint32_t>::max() - tail_size -
                            variable_payloads[i].size()) {
            set_error(error, GenericBrfEncodeError::overflow);
            return std::nullopt;
        }
        tail_size += variable_payloads[i].size();
    }
    result.resize(fixed_end + tail_size, 0U);
    if (result.size() > limits.max_record_bytes) {
        set_error(error, GenericBrfEncodeError::overflow);
        return std::nullopt;
    }
    result[0] = 2U;
    put16(result, 2U, 16U);
    put32(result, 4U, record_schema.record_id);
    put32(result, 8U, record_schema.fixed_region_size);
    put32(result, 12U, static_cast<std::uint32_t>(result.size()));
    std::size_t tail_cursor = fixed_end;
    for (std::size_t i = 0U; i < fields.size(); ++i) {
        if (!variable_present[i])
            continue;
        const auto field_schema = schema.find_field(record_index, static_cast<std::uint16_t>(i));
        put32(result, field_schema->byte_offset, static_cast<std::uint32_t>(tail_cursor));
        put32(result, field_schema->byte_offset + 4U,
              static_cast<std::uint32_t>(variable_payloads[i].size()));
        std::copy(variable_payloads[i].begin(), variable_payloads[i].end(),
                  result.begin() + static_cast<std::ptrdiff_t>(tail_cursor));
        tail_cursor += variable_payloads[i].size();
        result[16U + field_schema->presence_bit_index / 8U] |=
            static_cast<std::uint8_t>(1U << (field_schema->presence_bit_index % 8U));
    }
    return result;
}

} // namespace

std::optional<std::vector<std::uint8_t>>
encode_brf_record(const quarry::compiler::qbs::ValidatedQbsView& schema,
                  const quarry::compiler::qbs::QbsRecordView& record_schema,
                  std::span<const std::optional<BrfEncodeValue>> fields,
                  GenericBrfEncodeError* error, BrfEncodeLimits limits) {
    return encode_brf_record_impl(schema, record_schema, fields, error, limits, 0U);
}

} // namespace quarry::runtime
