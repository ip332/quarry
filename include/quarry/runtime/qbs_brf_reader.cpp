#include "quarry/runtime/qbs_brf_reader.hpp"
#include "quarry/runtime/detail/brf_validation_cache.hpp"

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

std::optional<std::uint64_t> varuint(std::span<const std::uint8_t> bytes, std::size_t& cursor) {
    std::uint64_t value = 0U;
    unsigned length = 0U;
    for (unsigned shift = 0U; shift < 64U && cursor < bytes.size(); shift += 7U) {
        const auto byte = bytes[cursor++];
        ++length;
        if (shift == 63U && (byte & 0x7FU) > 1U)
            return std::nullopt;
        value |= static_cast<std::uint64_t>(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0U &&
            (length == 1U || value >= (std::uint64_t{1} << (7U * (length - 1U)))))
            return value;
    }
    return std::nullopt;
}

bool utf8(std::string_view value) {
    for (std::size_t i = 0U; i < value.size();) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c <= 0x7FU) {
            ++i;
            continue;
        }
        const std::size_t length = c <= 0xDFU ? 2U : c <= 0xEFU ? 3U : c <= 0xF4U ? 4U : 0U;
        if (length == 0U || length > value.size() - i)
            return false;
        const auto c1 = static_cast<unsigned char>(value[i + 1U]);
        if ((c1 & 0xC0U) != 0x80U ||
            (length >= 3U && (static_cast<unsigned char>(value[i + 2U]) & 0xC0U) != 0x80U) ||
            (length == 4U && (static_cast<unsigned char>(value[i + 3U]) & 0xC0U) != 0x80U))
            return false;
        if ((length == 2U && c < 0xC2U) ||
            (length == 3U && ((c == 0xE0U && c1 < 0xA0U) || (c == 0xEDU && c1 >= 0xA0U))) ||
            (length == 4U && ((c == 0xF0U && c1 < 0x90U) || (c == 0xF4U && c1 > 0x8FU))))
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

bool valid_enum(const quarry::compiler::qbs::ValidatedQbsView& schema,
                const quarry::compiler::qbs::QbsTypeView& type,
                std::span<const std::uint8_t> bytes) {
    if (type.reference >= schema.enum_count() || bytes.size() != type.encoded_width ||
        bytes.size() > 8U)
        return false;
    std::uint64_t value = 0U;
    for (const auto byte : bytes)
        value = (value << 8U) | byte;
    const auto values = schema.enum_type(type.reference).values;
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool validate_array(const quarry::compiler::qbs::ValidatedQbsView& schema,
                    const quarry::compiler::qbs::QbsTypeView& array_type,
                    std::span<const std::uint8_t> bytes, const BrfReadLimits& limits,
                    std::uint64_t& work, GenericBrfError* error) {
    std::size_t cursor = 0U;
    const auto count = varuint(bytes, cursor);
    if (!count.has_value())
        return false;
    if (*count > array_type.max_elements || *count > limits.max_array_elements_traversed) {
        set_error(error, GenericBrfError::bounds_exceeded);
        return false;
    }
    if (*count >
        limits.max_work_items - std::min(work, static_cast<std::uint64_t>(limits.max_work_items))) {
        set_error(error, GenericBrfError::resource_limit_exceeded);
        return false;
    }
    work += *count;
    if (array_type.reference >= schema.type_count())
        return false;
    const auto element = schema.type(array_type.reference);
    if (element.code == 15U || element.code == 16U) {
        set_error(error, GenericBrfError::unsupported_type);
        return false;
    }
    if (element.code == 13U || element.code == 14U || element.code == 15U) {
        for (std::uint64_t i = 0U; i < *count; ++i) {
            const auto length = varuint(bytes, cursor);
            if (!length.has_value() || *length > bytes.size() - std::min(cursor, bytes.size()) ||
                *length > element.max_bytes)
                return false;
            if (cursor > bytes.size() - static_cast<std::size_t>(*length))
                return false;
            if (element.code == 13U && !utf8({reinterpret_cast<const char*>(bytes.data() + cursor),
                                              static_cast<std::size_t>(*length)}))
                return false;
            cursor += static_cast<std::size_t>(*length);
        }
        return cursor == bytes.size();
    }
    if (element.encoded_width == 0U ||
        *count > (bytes.size() - std::min(cursor, bytes.size())) / element.encoded_width)
        return false;
    const auto total = static_cast<std::size_t>(*count) * element.encoded_width;
    if (cursor > bytes.size() - total || cursor + total != bytes.size())
        return false;
    for (std::uint64_t i = 0U; i < *count; ++i) {
        const auto item = bytes.subspan(
            cursor + static_cast<std::size_t>(i) * element.encoded_width, element.encoded_width);
        if (element.code == 1U && (item[0] > 1U || element.encoded_width != 1U))
            return false;
        if (element.code == 12U && !valid_enum(schema, element, item))
            return false;
    }
    return true;
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

bool validate_next_field(const quarry::compiler::qbs::ValidatedQbsView& schema,
                         const quarry::compiler::qbs::QbsRecordView& record_schema,
                         std::span<const std::uint8_t> bytes, RecordValidationState& state,
                         detail::ValidationCache& cache, ValidatedBrfRecordView& result,
                         std::uint64_t& work, BrfReadLimits limits, GenericBrfError* error) {
    if (state.field_cursor >= record_schema.field_count)
        return false;
    const auto i = static_cast<std::uint32_t>(state.field_cursor);
    if (++work > limits.max_work_items || !cache.account_work()) {
        set_error(error, GenericBrfError::resource_limit_exceeded);
        return false;
    }
    const auto field_schema =
        schema.find_field(state.qbs_record_index, static_cast<std::uint16_t>(i));
    if (!field_schema.has_value() || field_schema->byte_offset > state.fixed_region_end ||
        field_schema->slot_size > state.fixed_region_end - field_schema->byte_offset ||
        field_schema->presence_bit_index / 8U >= record_schema.presence_bitmap_size) {
        set_error(error, GenericBrfError::invalid_slot);
        return false;
    }
    const bool present = (bytes[16U + field_schema->presence_bit_index / 8U] &
                          (1U << (field_schema->presence_bit_index % 8U))) != 0U;
    const auto slot = bytes.subspan(field_schema->byte_offset, field_schema->slot_size);
    if (!present && std::any_of(slot.begin(), slot.end(), [](auto value) { return value != 0U; })) {
        set_error(error, GenericBrfError::invalid_presence);
        return false;
    }
    std::span<const std::uint8_t> value = slot;
    if (present && field_schema->storage == 2U) {
        if (slot.size() != 8U) {
            set_error(error, GenericBrfError::invalid_descriptor);
            return false;
        }
        const auto offset = u32(slot, 0U);
        const auto length = u32(slot, 4U);
        if (offset != state.variable_tail_cursor || offset > bytes.size() ||
            length > bytes.size() - offset) {
            set_error(error, GenericBrfError::invalid_variable_range);
            return false;
        }
        value = bytes.subspan(offset, length);
        state.variable_tail_cursor += length;
    }
    const auto type = schema.type(field_schema->type_index);
    if (present && type.code == 1U && (value.size() != 1U || value[0] > 1U)) {
        set_error(error, GenericBrfError::invalid_bool);
        return false;
    }
    if (present && type.code == 13U &&
        (value.size() > type.max_bytes ||
         !utf8({reinterpret_cast<const char*>(value.data()), value.size()}))) {
        set_error(error, GenericBrfError::invalid_utf8);
        return false;
    }
    if (present && type.code == 14U && value.size() > type.max_bytes) {
        set_error(error, GenericBrfError::bounds_exceeded);
        return false;
    }
    if (present && type.code == 12U && !valid_enum(schema, type, value)) {
        set_error(error, GenericBrfError::invalid_enum);
        return false;
    }
    if (present && type.code == 15U) {
        set_error(error, GenericBrfError::unsupported_type);
        return false;
    }
    if (present && type.code == 16U && !validate_array(schema, type, value, limits, work, error)) {
        if (error != nullptr && *error == GenericBrfError::none)
            set_error(error, GenericBrfError::malformed_array);
        return false;
    }
    detail::ValidatedFieldState field_state;
    field_state.qbs_field_index = field_schema->field_index;
    field_state.present = present;
    field_state.fixed_offset = field_schema->byte_offset;
    field_state.fixed_length = field_schema->slot_size;
    if (present && field_schema->storage == 2U) {
        field_state.payload_offset = u32(slot, 0U);
        field_state.payload_length = value.size();
    }
    if (present && type.code == 16U) {
        std::size_t array_cursor = 0U;
        const auto count = varuint(value, array_cursor);
        if (!count.has_value()) {
            set_error(error, GenericBrfError::malformed_array);
            return false;
        }
        field_state.array_count = *count;
    }
    if (!cache.add_field(state.node_index, field_state)) {
        set_error(error, GenericBrfError::resource_limit_exceeded);
        return false;
    }
    result.fields_.push_back({field_schema->field_index, value, present});
    ++state.field_cursor;
    return true;
}

RecordValidationStep
advance_record_validation(const quarry::compiler::qbs::ValidatedQbsView& schema,
                          const quarry::compiler::qbs::QbsRecordView& record_schema,
                          std::span<const std::uint8_t> bytes, RecordValidationState& state,
                          detail::ValidationCache& cache, ValidatedBrfRecordView& result,
                          std::uint64_t& work, BrfReadLimits limits, GenericBrfError* error) {
    if (state.phase == RecordValidationState::Phase::Complete)
        return RecordValidationStep::Complete;
    if (state.phase == RecordValidationState::Phase::Failed) {
        set_error(error, GenericBrfError::invalid_header);
        return RecordValidationStep::Error;
    }
    if (state.phase == RecordValidationState::Phase::Header) {
        if (bytes.size() > limits.max_record_bytes || bytes.size() < 16U) {
            set_error(error, bytes.size() < 16U ? GenericBrfError::truncated_header
                                                : GenericBrfError::resource_limit_exceeded);
            state.phase = RecordValidationState::Phase::Failed;
            return RecordValidationStep::Error;
        }
        if (bytes[0] != 2U) {
            set_error(error, GenericBrfError::unsupported_version);
            state.phase = RecordValidationState::Phase::Failed;
            return RecordValidationStep::Error;
        }
        if (bytes[1] != 0U) {
            set_error(error, GenericBrfError::unsupported_flags);
            state.phase = RecordValidationState::Phase::Failed;
            return RecordValidationStep::Error;
        }
        if (u16(bytes, 2U) != 16U) {
            set_error(error, GenericBrfError::invalid_header);
            state.phase = RecordValidationState::Phase::Failed;
            return RecordValidationStep::Error;
        }
        if (u32(bytes, 4U) != record_schema.record_id) {
            set_error(error, GenericBrfError::unexpected_record_id);
            state.phase = RecordValidationState::Phase::Failed;
            return RecordValidationStep::Error;
        }
        const auto fixed_size = u32(bytes, 8U);
        const auto record_size = u32(bytes, 12U);
        if (fixed_size != record_schema.fixed_region_size || record_size != bytes.size() ||
            fixed_size > bytes.size() - 16U || record_schema.presence_bitmap_size > fixed_size) {
            set_error(error, GenericBrfError::invalid_fixed_region);
            state.phase = RecordValidationState::Phase::Failed;
            return RecordValidationStep::Error;
        }
        state.fixed_region_end = 16U + fixed_size;
        state.variable_region_start = state.fixed_region_end;
        state.phase = RecordValidationState::Phase::Presence;
        return RecordValidationStep::Continue;
    }
    if (state.phase == RecordValidationState::Phase::Presence) {
        if (record_schema.presence_bitmap_size != 0U) {
            std::vector<std::uint8_t> used(record_schema.presence_bitmap_size, 0U);
            for (std::uint32_t i = 0U; i < record_schema.field_count; ++i) {
                const auto field_schema =
                    schema.find_field(state.qbs_record_index, static_cast<std::uint16_t>(i));
                if (!field_schema.has_value() ||
                    field_schema->presence_bit_index / 8U >= used.size()) {
                    set_error(error, GenericBrfError::invalid_slot);
                    state.phase = RecordValidationState::Phase::Failed;
                    return RecordValidationStep::Error;
                }
                used[field_schema->presence_bit_index / 8U] |=
                    static_cast<std::uint8_t>(1U << (field_schema->presence_bit_index % 8U));
            }
            for (std::size_t i = 0U; i < used.size(); ++i) {
                if ((bytes[16U + i] & static_cast<std::uint8_t>(~used[i])) != 0U) {
                    set_error(error, GenericBrfError::invalid_presence);
                    state.phase = RecordValidationState::Phase::Failed;
                    return RecordValidationStep::Error;
                }
            }
        }
        state.variable_tail_cursor = state.variable_region_start;
        state.field_cursor = 0U;
        state.phase = RecordValidationState::Phase::Fields;
        return RecordValidationStep::Continue;
    }
    if (state.phase == RecordValidationState::Phase::Fields) {
        if (state.field_cursor < record_schema.field_count) {
            if (!validate_next_field(schema, record_schema, bytes, state, cache, result, work,
                                     limits, error)) {
                state.phase = RecordValidationState::Phase::Failed;
                return RecordValidationStep::Error;
            }
            return RecordValidationStep::Continue;
        }
        state.phase = RecordValidationState::Phase::FinalizeTail;
        return RecordValidationStep::Continue;
    }
    if (state.phase == RecordValidationState::Phase::FinalizeTail) {
        if (state.variable_tail_cursor != bytes.size() || !cache.complete_node(state.node_index)) {
            set_error(error, state.variable_tail_cursor != bytes.size()
                                 ? GenericBrfError::noncanonical_tail
                                 : GenericBrfError::resource_limit_exceeded);
            state.phase = RecordValidationState::Phase::Failed;
            return RecordValidationStep::Error;
        }
        state.phase = RecordValidationState::Phase::Complete;
        return RecordValidationStep::Complete;
    }
    return RecordValidationStep::Error;
}

std::optional<FieldValueView> BrfArrayView::element(std::size_t index) const {
    if (index >= count_)
        return std::nullopt;
    std::size_t cursor = 0U;
    const auto count = varuint(bytes_, cursor);
    if (!count.has_value() || index >= *count)
        return std::nullopt;
    if (element_type_.code == 13U || element_type_.code == 14U || element_type_.code == 15U) {
        for (std::size_t i = 0U; i <= index; ++i) {
            const auto length = varuint(bytes_, cursor);
            if (!length.has_value() || *length > bytes_.size() - cursor ||
                cursor > bytes_.size() - static_cast<std::size_t>(*length))
                return std::nullopt;
            const auto item = bytes_.subspan(cursor, static_cast<std::size_t>(*length));
            cursor += static_cast<std::size_t>(*length);
            if (i == index)
                return FieldValueView(kind(element_type_.code), item, element_type_.encoded_width);
        }
        return std::nullopt;
    }
    const auto offset = cursor + index * element_type_.encoded_width;
    if (offset > bytes_.size() || element_type_.encoded_width > bytes_.size() - offset)
        return std::nullopt;
    return FieldValueView(kind(element_type_.code),
                          bytes_.subspan(offset, element_type_.encoded_width),
                          element_type_.encoded_width);
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

std::optional<BrfArrayView>
ValidatedBrfRecordView::array(const quarry::compiler::qbs::QbsFieldView& field_schema) const {
    for (const auto& field_view : fields_) {
        if (field_view.field_index != field_schema.field_index || !field_view.present)
            continue;
        const auto type = schema_->type(field_schema.type_index);
        if (type.code != 16U || type.reference >= schema_->type_count())
            return std::nullopt;
        const auto element_type = schema_->type(type.reference);
        std::size_t cursor = 0U;
        const auto count = varuint(field_view.bytes, cursor);
        return count.has_value() ? std::optional<BrfArrayView>(
                                       BrfArrayView(element_type, field_view.bytes, *count))
                                 : std::nullopt;
    }
    return std::nullopt;
}

std::optional<BrfArrayView> ValidatedBrfRecordView::array(std::uint16_t field_index) const {
    const auto field_schema = schema_->find_field(record_index_, field_index);
    return field_schema.has_value() ? array(*field_schema) : std::nullopt;
}

std::optional<ValidatedBrfRecordView>
validate_record_span(const quarry::compiler::qbs::ValidatedQbsView& schema,
                     const quarry::compiler::qbs::QbsRecordView& record_schema,
                     std::span<const std::uint8_t> bytes, BrfReadLimits limits,
                     GenericBrfError* error) {
    set_error(error, GenericBrfError::none);
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
    RecordValidationState state;
    state.qbs_record_index = record_index;
    state.record_bytes = bytes;
    std::uint64_t work = 0U;
    auto validation_cache = std::make_shared<detail::ValidationCache>(limits);
    const auto root_node = validation_cache->add_node(record_index, 0U, bytes.size());
    if (!root_node.has_value() || !validation_cache->begin_fields(*root_node).has_value()) {
        set_error(error, GenericBrfError::resource_limit_exceeded);
        return std::nullopt;
    }
    state.node_index = *root_node;
    ValidatedBrfRecordView result;
    result.schema_ = &schema;
    result.record_index_ = record_index;
    result.record_ = record_schema;
    result.bytes_ = bytes;
    result.validation_cache_ = validation_cache;
    for (;;) {
        const auto step = advance_record_validation(schema, record_schema, bytes, state,
                                                    *validation_cache, result, work, limits, error);
        if (step == RecordValidationStep::Complete)
            break;
        if (step == RecordValidationStep::Error)
            return std::nullopt;
    }
    return result;
}

std::optional<ValidatedBrfRecordView>
validate_brf_record(const quarry::compiler::qbs::ValidatedQbsView& schema,
                    const quarry::compiler::qbs::QbsRecordView& record_schema,
                    std::span<const std::uint8_t> bytes, BrfReadLimits limits,
                    GenericBrfError* error) {
    return validate_record_span(schema, record_schema, bytes, limits, error);
}

} // namespace quarry::runtime
