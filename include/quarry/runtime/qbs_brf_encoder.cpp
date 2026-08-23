#include "quarry/runtime/qbs_brf_encoder.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace quarry::runtime {
namespace {

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

} // namespace

std::optional<std::vector<std::uint8_t>>
encode_brf_record(const quarry::compiler::qbs::ValidatedQbsView& schema,
                  const quarry::compiler::qbs::QbsRecordView& record_schema,
                  std::span<const std::optional<BrfEncodeValue>> fields,
                  GenericBrfEncodeError* error) {
    set_error(error, GenericBrfEncodeError::none);
    if (record_schema.variable_size || fields.size() != record_schema.field_count ||
        record_schema.fixed_region_size > std::numeric_limits<std::uint32_t>::max() - 16U) {
        set_error(error, fields.size() != record_schema.field_count
                             ? GenericBrfEncodeError::field_count_mismatch
                             : GenericBrfEncodeError::invalid_schema);
        return std::nullopt;
    }
    const auto total = static_cast<std::size_t>(16U) + record_schema.fixed_region_size;
    std::vector<std::uint8_t> result(total, 0U);
    result[0] = 2U;
    put16(result, 2U, 16U);
    put32(result, 4U, record_schema.record_id);
    put32(result, 8U, record_schema.fixed_region_size);
    put32(result, 12U, static_cast<std::uint32_t>(total));
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
    for (std::size_t i = 0U; i < fields.size(); ++i) {
        const auto field_schema = schema.find_field(record_index, static_cast<std::uint16_t>(i));
        if (!field_schema.has_value()) {
            set_error(error, GenericBrfEncodeError::invalid_schema);
            return std::nullopt;
        }
        if (field_schema->storage == 2U || field_schema->type_index >= schema.type_count()) {
            set_error(error, GenericBrfEncodeError::unsupported_type);
            return std::nullopt;
        }
        const auto type = schema.type(field_schema->type_index);
        if (type.code == 13U || type.code == 14U || type.code == 15U || type.code == 16U) {
            set_error(error, GenericBrfEncodeError::unsupported_type);
            return std::nullopt;
        }
        if (field_schema->presence_bit_index / 8U >= record_schema.presence_bitmap_size ||
            field_schema->byte_offset > record_schema.fixed_region_size + 16U ||
            field_schema->slot_size >
                record_schema.fixed_region_size + 16U - field_schema->byte_offset) {
            set_error(error, GenericBrfEncodeError::invalid_schema);
            return std::nullopt;
        }
        if (!fields[i].has_value())
            continue;
        result[16U + field_schema->presence_bit_index / 8U] |=
            static_cast<std::uint8_t>(1U << (field_schema->presence_bit_index % 8U));
        if (!write_scalar(schema, type, *fields[i],
                          std::span<std::uint8_t>(result).subspan(field_schema->byte_offset,
                                                                  field_schema->slot_size),
                          error))
            return std::nullopt;
    }
    return result;
}

} // namespace quarry::runtime
