#include "quarry/runtime/qbs_brf_reader.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace quarry::runtime {
namespace {

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>((bytes[at] << 8U) | bytes[at + 1U]);
}

std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return (static_cast<std::uint32_t>(bytes[at]) << 24U) |
           (static_cast<std::uint32_t>(bytes[at + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 2U]) << 8U) | bytes[at + 3U];
}

bool utf8(std::string_view value) {
    for (std::size_t i = 0U; i < value.size();) {
        const auto c = static_cast<unsigned char>(value[i]);
        const std::size_t length = c < 0x80U ? 1U : c < 0xE0U ? 2U : c < 0xF0U ? 3U : 4U;
        if ((length == 1U && c >= 0x80U) || length > value.size() - i)
            return false;
        for (std::size_t j = 1U; j < length; ++j)
            if ((static_cast<unsigned char>(value[i + j]) & 0xC0U) != 0x80U)
                return false;
        i += length;
    }
    return true;
}

GenericBrfValueKind kind(std::uint8_t code) {
    if (code == 1U)
        return GenericBrfValueKind::boolean;
    if (code == 2U || code == 4U || code == 6U || code == 8U)
        return GenericBrfValueKind::signed_integer;
    if (code == 3U || code == 5U || code == 7U || code == 9U)
        return GenericBrfValueKind::unsigned_integer;
    if (code == 10U)
        return GenericBrfValueKind::float32;
    if (code == 11U)
        return GenericBrfValueKind::float64;
    if (code == 12U)
        return GenericBrfValueKind::enumeration;
    if (code == 13U)
        return GenericBrfValueKind::string;
    if (code == 14U)
        return GenericBrfValueKind::bytes;
    if (code == 15U)
        return GenericBrfValueKind::record;
    return GenericBrfValueKind::array;
}

void set_error(GenericBrfError* error, GenericBrfError value) {
    if (error != nullptr)
        *error = value;
}

} // namespace

std::optional<bool> FieldValueView::as_bool() const {
    if (kind_ != GenericBrfValueKind::boolean || bytes_.size() != 1U)
        return std::nullopt;
    return bytes_[0] != 0U;
}

std::optional<std::uint64_t> FieldValueView::as_unsigned() const {
    if (kind_ != GenericBrfValueKind::unsigned_integer && kind_ != GenericBrfValueKind::enumeration)
        return std::nullopt;
    if (width_ == 0U || width_ > 8U || bytes_.size() != width_)
        return std::nullopt;
    std::uint64_t value = 0U;
    for (const auto byte : bytes_)
        value = (value << 8U) | byte;
    return value;
}

std::optional<std::int64_t> FieldValueView::as_signed() const {
    if (kind_ != GenericBrfValueKind::signed_integer)
        return std::nullopt;
    const auto unsigned_value =
        FieldValueView(GenericBrfValueKind::unsigned_integer, bytes_, width_).as_unsigned();
    if (!unsigned_value.has_value())
        return std::nullopt;
    const auto bits = static_cast<unsigned>(width_) * 8U;
    if (bits == 64U)
        return std::bit_cast<std::int64_t>(*unsigned_value);
    const auto sign = std::uint64_t{1} << (bits - 1U);
    const auto extended = (*unsigned_value & sign) != 0U
                              ? *unsigned_value | (~std::uint64_t{0} << bits)
                              : *unsigned_value;
    return static_cast<std::int64_t>(extended);
}

std::optional<std::string_view> FieldValueView::as_string() const {
    if (kind_ != GenericBrfValueKind::string ||
        !utf8({reinterpret_cast<const char*>(bytes_.data()), bytes_.size()}))
        return std::nullopt;
    return std::string_view(reinterpret_cast<const char*>(bytes_.data()), bytes_.size());
}

bool ValidatedBrfRecordView::is_present(
    const quarry::compiler::qbs::QbsFieldView& field_schema) const {
    for (const auto& field : fields_)
        if (field.field_index == field_schema.field_index)
            return field.present;
    return false;
}

std::optional<FieldValueView>
ValidatedBrfRecordView::field(const quarry::compiler::qbs::QbsFieldView& field_schema) const {
    for (const auto& field_view : fields_) {
        if (field_view.field_index != field_schema.field_index)
            continue;
        if (!field_view.present)
            return std::nullopt;
        const auto type = schema_->type(field_schema.type_index);
        return FieldValueView(kind(type.code), field_view.bytes, type.encoded_width);
    }
    return std::nullopt;
}

std::optional<FieldValueView> ValidatedBrfRecordView::field(std::uint16_t field_index) const {
    const auto field_schema = schema_->find_field(record_index_, field_index);
    return field_schema.has_value() ? field(*field_schema) : std::nullopt;
}

std::optional<ValidatedBrfRecordView>
validate_brf_record(const quarry::compiler::qbs::ValidatedQbsView& schema,
                    const quarry::compiler::qbs::QbsRecordView& record_schema,
                    std::span<const std::uint8_t> bytes, BrfReadLimits limits,
                    GenericBrfError* error) {
    set_error(error, GenericBrfError::none);
    if (bytes.size() > limits.max_record_bytes || bytes.size() < 16U) {
        set_error(error, bytes.size() < 16U ? GenericBrfError::truncated_header
                                            : GenericBrfError::resource_limit_exceeded);
        return std::nullopt;
    }
    if (bytes[0] != 2U) {
        set_error(error, GenericBrfError::unsupported_version);
        return std::nullopt;
    }
    if (bytes[1] != 0U) {
        set_error(error, GenericBrfError::unsupported_flags);
        return std::nullopt;
    }
    if (u16(bytes, 2U) != 16U || u32(bytes, 4U) != record_schema.record_id) {
        set_error(error, u32(bytes, 4U) != record_schema.record_id
                             ? GenericBrfError::unexpected_record_id
                             : GenericBrfError::invalid_header);
        return std::nullopt;
    }
    const auto fixed_size = u32(bytes, 8U);
    const auto record_size = u32(bytes, 12U);
    if (fixed_size != record_schema.fixed_region_size || record_size != bytes.size() ||
        fixed_size > bytes.size() - 16U || record_schema.presence_bitmap_size > fixed_size) {
        set_error(error, GenericBrfError::invalid_fixed_region);
        return std::nullopt;
    }
    const auto bitmap_begin = std::size_t{16U};
    const auto bitmap_end = bitmap_begin + record_schema.presence_bitmap_size;
    (void)bitmap_end;
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
        set_error(error, GenericBrfError::invalid_header);
        return std::nullopt;
    }
    std::uint64_t work = 0U;
    std::uint64_t tail = 16U + fixed_size;
    ValidatedBrfRecordView result;
    result.schema_ = &schema;
    result.record_index_ = record_index;
    result.record_ = record_schema;
    result.bytes_ = bytes;
    for (std::uint32_t i = 0U; i < record_schema.field_count; ++i) {
        if (++work > limits.max_work_items) {
            set_error(error, GenericBrfError::resource_limit_exceeded);
            return std::nullopt;
        }
        const auto field_schema = schema.find_field(record_index, static_cast<std::uint16_t>(i));
        if (!field_schema.has_value() || field_schema->byte_offset > 16U + fixed_size ||
            field_schema->slot_size > 16U + fixed_size - field_schema->byte_offset ||
            field_schema->presence_bit_index / 8U >= record_schema.presence_bitmap_size) {
            set_error(error, GenericBrfError::invalid_slot);
            return std::nullopt;
        }
        const bool present = (bytes[bitmap_begin + field_schema->presence_bit_index / 8U] &
                              (1U << (field_schema->presence_bit_index % 8U))) != 0U;
        const auto slot = bytes.subspan(field_schema->byte_offset, field_schema->slot_size);
        if (!present &&
            std::any_of(slot.begin(), slot.end(), [](auto value) { return value != 0U; })) {
            set_error(error, GenericBrfError::invalid_presence);
            return std::nullopt;
        }
        std::span<const std::uint8_t> value = slot;
        if (present && field_schema->storage == 2U) {
            if (slot.size() != 8U) {
                set_error(error, GenericBrfError::invalid_descriptor);
                return std::nullopt;
            }
            const auto offset = u32(slot, 0U);
            const auto length = u32(slot, 4U);
            if (offset != tail || offset > bytes.size() || length > bytes.size() - offset) {
                set_error(error, GenericBrfError::invalid_variable_range);
                return std::nullopt;
            }
            value = bytes.subspan(offset, length);
            tail += length;
        }
        const auto type = schema.type(field_schema->type_index);
        if (present && type.code == 1U && (value.size() != 1U || value[0] > 1U)) {
            set_error(error, GenericBrfError::invalid_bool);
            return std::nullopt;
        }
        if (present && type.code == 13U &&
            !utf8({reinterpret_cast<const char*>(value.data()), value.size()})) {
            set_error(error, GenericBrfError::invalid_utf8);
            return std::nullopt;
        }
        result.fields_.push_back({field_schema->field_index, value, present});
    }
    if (record_schema.presence_bitmap_size != 0U) {
        std::vector<std::uint8_t> used(record_schema.presence_bitmap_size, 0U);
        for (std::uint32_t i = 0U; i < record_schema.field_count; ++i) {
            const auto field_schema =
                schema.find_field(record_index, static_cast<std::uint16_t>(i));
            if (!field_schema.has_value()) {
                set_error(error, GenericBrfError::invalid_slot);
                return std::nullopt;
            }
            used[field_schema->presence_bit_index / 8U] |=
                static_cast<std::uint8_t>(1U << (field_schema->presence_bit_index % 8U));
        }
        for (std::size_t i = 0U; i < used.size(); ++i) {
            if ((bytes[bitmap_begin + i] & static_cast<std::uint8_t>(~used[i])) != 0U) {
                set_error(error, GenericBrfError::invalid_presence);
                return std::nullopt;
            }
        }
    }
    if (tail != bytes.size()) {
        set_error(error, GenericBrfError::noncanonical_tail);
        return std::nullopt;
    }
    return result;
}

} // namespace quarry::runtime
